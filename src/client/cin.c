/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "client.h"

typedef struct {
    uint32_t    width;
    uint32_t    height;
    uint32_t    s_rate;
    uint32_t    s_width;
    uint32_t    s_channels;
} cheader_t;

typedef struct {
    int16_t     children[2];
} hnode_t;

typedef struct {
    const char  *name;
    uint32_t    size;
    uint16_t    start;
    uint16_t    crop;
} crop_t;

typedef struct {
    int         width;
    int         height;
    int         crop;
    int         s_rate;
    int         s_width;
    int         s_channels;

    qhandle_t   static_pic;

    uint32_t    *pic;
    uint32_t    palette[256];

    hnode_t     hnodes[256][256];
    int         numhnodes[256];

    int         h_count[512];
    bool        h_used[512];

    qhandle_t   file;
    unsigned    frame;
    unsigned    time;
} cinematic_t;

static cinematic_t  cin;

static const crop_t cin_crop[] = {
    { "ntro.cin",   82836235, 727, 30 },
    { "end.cin",    19311290,   0, 30 },
    { "rintro.cin", 38434032,   0, 24 },
    { "rend.cin",   22580919,   0, 24 },
    { "xin.cin",    13226649,   0, 32 },
    { "xout.cin",   11194445,   0, 32 },
};

/*
==================
SCR_StopCinematic
==================
*/
void SCR_StopCinematic(void)
{
    R_DiscardRawPic();

    Z_Free(cin.pic);
    FS_CloseFile(cin.file);
    memset(&cin, 0, sizeof(cin));
}

/*
====================
SCR_FinishCinematic

Called when either the cinematic completes, or it is aborted
====================
*/
void SCR_FinishCinematic(void)
{
    // stop cinematic, but keep static pic
    if (cin.file) {
        SCR_StopCinematic();
        SCR_BeginLoadingPlaque();
    }

    // tell the server to advance to the next map / cinematic
    CL_ClientCommand(va("nextserver %i\n", cl.servercount));
}

/*
==================
SmallestNode1
==================
*/
static int SmallestNode1(int numhnodes)
{
    int     i;
    int     best, bestnode;

    best = 99999999;
    bestnode = -1;
    for (i = 0; i < numhnodes; i++) {
        if (cin.h_used[i])
            continue;
        if (!cin.h_count[i])
            continue;
        if (cin.h_count[i] < best) {
            best = cin.h_count[i];
            bestnode = i;
        }
    }

    if (bestnode == -1)
        return -1;

    cin.h_used[bestnode] = true;
    return bestnode;
}

/*
==================
Huff1TableInit

Reads the 64k counts table and initializes the node trees
==================
*/
static bool Huff1TableInit(void)
{
    for (int prev = 0; prev < 256; prev++) {
        hnode_t *hnodes = cin.hnodes[prev];
        byte counts[256];
        int numhnodes;

        memset(cin.h_count, 0, sizeof(cin.h_count));
        memset(cin.h_used, 0, sizeof(cin.h_used));

        // read a row of counts
        if (FS_Read(counts, sizeof(counts), cin.file) != sizeof(counts))
            return false;

        for (int i = 0; i < 256; i++)
            cin.h_count[i] = counts[i];

        // build the nodes
        for (numhnodes = 256; numhnodes < 512; numhnodes++) {
            hnode_t *node = &hnodes[numhnodes - 256];

            // pick two lowest counts
            node->children[0] = SmallestNode1(numhnodes);
            if (node->children[0] == -1)
                break;  // no more

            node->children[1] = SmallestNode1(numhnodes);
            if (node->children[1] == -1)
                break;

            cin.h_count[numhnodes] =
                cin.h_count[node->children[0]] +
                cin.h_count[node->children[1]];
        }

        cin.numhnodes[prev] = numhnodes - 1;
    }

    return true;
}

/*
==================
Huff1Decompress
==================
*/
static bool Huff1Decompress(const byte *data, int size)
{
    const byte  *in, *in_end;
    uint32_t    *out;
    int         prev, bitpos, inbyte, count;

    in = data + 4;
    in_end = data + size;

    out = cin.pic;
    count = cin.width * cin.height;

    // read bits
    prev = bitpos = inbyte = 0;
    for (int i = 0; i < count; i++) {
        int nodenum = cin.numhnodes[prev];
        hnode_t *hnodes = cin.hnodes[prev];

        while (nodenum >= 256) {
            if (bitpos == 0) {
                if (in >= in_end)
                    return false;
                inbyte = *in++;
                bitpos = 8;
            }
            nodenum = hnodes[nodenum - 256].children[inbyte & 1];
            inbyte >>= 1;
            bitpos--;
        }

        *out++ = cin.palette[nodenum];
        prev = nodenum;
    }

    return true;
}

/*
==================
GetVerticalCrop
==================
*/
static int GetVerticalCrop(void)
{
    const crop_t *c;
    int i;

    for (i = 0, c = cin_crop; i < q_countof(cin_crop); i++, c++) {
        if (!Q_stricmp(cl.mapname, c->name) && FS_Length(cin.file) == c->size) {
            if (cin.frame >= c->start)
                return c->crop * 2;
            break;
        }
    }

    return 0;
}


/*
==================
SCR_ReadNextFrame
==================
*/
static bool SCR_ReadNextFrame(void)
{
    uint32_t    command, size;
    byte        compressed[0x20000];

    // read the next frame
    if (FS_Read(&command, 4, cin.file) != 4)
        return false;
    command = LittleLong(command);
    if (command >= 2)
        return false;   // last frame marker
    if (command == 1) {
        // read palette
        byte palette[768], *p;
        int i;

        if (FS_Read(palette, sizeof(palette), cin.file) != sizeof(palette))
            return false;

        for (i = 0, p = palette; i < 256; i++, p += 3)
            cin.palette[i] = MakeColor(p[0], p[1], p[2], 255);
    }

    // decompress the next frame
    if (FS_Read(&size, 4, cin.file) != 4)
        return false;
    size = LittleLong(size);
    if (size < 4 || size > sizeof(compressed)) {
        Com_EPrintf("Bad compressed frame size\n");
        return false;
    }
    if (FS_Read(compressed, size, cin.file) != size)
        return false;
    if (!Huff1Decompress(compressed, size)) {
        Com_EPrintf("Decompression overread\n");
        return false;
    }

    // read sound
    if (cin.s_rate) {
        unsigned start = cin.frame * cin.s_rate / 14;
        unsigned end = (cin.frame + 1) * cin.s_rate / 14;
        unsigned s_size = (end - start) * cin.s_width * cin.s_channels;
        byte samples[22050 / 14 * 4];

        Q_assert(s_size <= sizeof(samples));
        if (FS_Read(samples, s_size, cin.file) != s_size)
            return false;

#if USE_BIG_ENDIAN
        if (cin.s_width == 2) {
            uint16_t *data = (uint16_t *)samples;
            for (int i = 0; i < s_size >> 1; i++)
                data[i] = LittleShort(data[i]);
        }
#endif
        S_RawSamples(end - start, cin.s_rate, cin.s_width, cin.s_channels, samples);
    }

    cin.crop = GetVerticalCrop();

    R_UpdateRawPic(cin.width, cin.height, cin.pic);
    cin.frame++;
    return true;
}

/*
==================
SCR_RunCinematic
==================
*/
void SCR_RunCinematic(void)
{
    unsigned    frame;

    if (cls.state != ca_cinematic)
        return;

    if (!cin.file)
        return;     // static image

    if (cls.key_dest != KEY_GAME) {
        // pause if menu or console is up
        cin.time = cls.realtime - cin.frame * 1000 / 14;
        return;
    }

    frame = (cls.realtime - cin.time) * 14 / 1000;
    if (frame <= cin.frame)
        return;

    if (frame > cin.frame + 1) {
        Com_DPrintf("Dropped frame: %u > %u\n", frame, cin.frame + 1);
        cin.time = cls.realtime - cin.frame * 1000 / 14;
    }

    if (!SCR_ReadNextFrame()) {
        SCR_FinishCinematic();
        return;
    }
}

/*
==================
SCR_DrawCinematic
==================
*/
void SCR_DrawCinematic(void)
{
    R_DrawFill8(0, 0, r_config.width, r_config.height, 0);

    if (cin.width > 0 && cin.height > cin.crop) {
        float scale_w = (float)r_config.width / cin.width;
        float scale_h = (float)r_config.height / (cin.height - cin.crop);
        float scale = min(scale_w, scale_h);

        int w = Q_rint(cin.width * scale);
        int h = Q_rint(cin.height * scale);
        int x = (r_config.width - w) / 2;
        int y = (r_config.height - h) / 2;

        if (cin.pic)
            R_DrawStretchRaw(x, y, w, h);
        else if (cin.static_pic)
            R_DrawStretchPic(x, y, w, h, cin.static_pic);
    }
}

/*
==================
SCR_StartCinematic
==================
*/
static bool SCR_StartCinematic(const char *name)
{
    cheader_t header;
    char    fullname[MAX_QPATH];
    int     ret;

    if (Q_snprintf(fullname, sizeof(fullname), "video/%s", name) >= sizeof(fullname)) {
        Com_EPrintf("Oversize cinematic name\n");
        return false;
    }

    ret = FS_OpenFile(fullname, &cin.file, FS_MODE_READ);
    if (!cin.file) {
        Com_EPrintf("Couldn't open %s: %s\n", fullname, Q_ErrorString(ret));
        return false;
    }

    if (FS_Read(&header, sizeof(header), cin.file) != sizeof(header)) {
        Com_EPrintf("Error reading cinematic header\n");
        return false;
    }

    cin.width = LittleLong(header.width);
    cin.height = LittleLong(header.height);
    cin.s_rate = LittleLong(header.s_rate);
    cin.s_width = LittleLong(header.s_width);
    cin.s_channels = LittleLong(header.s_channels);

    if (cin.width < 1 || cin.width > 640 || cin.height < 1 || cin.height > 480) {
        Com_EPrintf("Bad cinematic video dimensions\n");
        return false;
    }
    if (cin.s_rate && (cin.s_rate < 8000 || cin.s_rate > 22050 ||
                       cin.s_width < 1 || cin.s_width > 2 ||
                       cin.s_channels < 1 || cin.s_channels > 2)) {
        Com_EPrintf("Bad cinematic audio parameters\n");
        return false;
    }

    if (!Huff1TableInit()) {
        Com_EPrintf("Error reading huffman table\n");
        return false;
    }

    cin.frame = 0;
    cin.time = cls.realtime;
    cin.pic = Z_Malloc(cin.width * cin.height * 4);

    return SCR_ReadNextFrame();
}

/*
==================
SCR_ReloadCinematic
==================
*/
void SCR_ReloadCinematic(void)
{
    if (cin.pic) {
        R_UpdateRawPic(cin.width, cin.height, cin.pic);
    } else if (cl.mapname[0]) {
        cin.static_pic = R_RegisterPic2(cl.mapname);
        R_GetPicSize(&cin.width, &cin.height, cin.static_pic);
    }
}

/*
==================
SCR_PlayCinematic
==================
*/
void SCR_PlayCinematic(const char *name)
{
    // make sure CD isn't playing music
    OGG_Stop();

    if (!COM_CompareExtension(name, ".pcx")) {
        cin.static_pic = R_RegisterPic2(name);
        if (!cin.static_pic)
            goto finish;
        R_GetPicSize(&cin.width, &cin.height, cin.static_pic);
    } else if (!COM_CompareExtension(name, ".cin")) {
        if (!SCR_StartCinematic(name))
            goto finish;
    } else {
        goto finish;
    }

    // save picture name for reloading
    Q_strlcpy(cl.mapname, name, sizeof(cl.mapname));

    cls.state = ca_cinematic;

    SCR_EndLoadingPlaque();     // get rid of loading plaque
    Con_Close(false);           // get rid of connection screen
    return;

finish:
    SCR_FinishCinematic();
}
