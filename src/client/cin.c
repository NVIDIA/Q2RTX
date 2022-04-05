/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "client.h"
#include "client/sound/sound.h"
#include "client/sound/vorbis.h"
#include "common/files.h"
#include "refresh/images.h"

typedef struct
{
    byte	*data;
    int		count;
} cblock_t;

typedef struct
{
    int     s_khz_original;
    int     s_rate;
    int     s_width;
    int     s_channels;

    int     width;
    int     height;

    // order 1 huffman stuff
    int     *hnodes1;	// [256][256][2];
    int     numhnodes1[256];

    int     h_used[512];
    int     h_count[512];

    byte    palette[768];
    bool    palette_active;

    char    file_name[MAX_QPATH];
    qhandle_t file;

    int     start_time; // cls.realtime for first cinematic frame
    int     frame_index;
} cinematics_t;

static cinematics_t cin = { 0 };

/*
==================
SCR_StopCinematic
==================
*/
void SCR_StopCinematic(void)
{
    cin.start_time = 0;	// done

    S_UnqueueRawSamples();

    if (cl.image_precache[0])
    {
        R_UnregisterImage(cl.image_precache[0]);
        cl.image_precache[0] = 0;
    }

    if (cin.file)
    {
        FS_FCloseFile(cin.file);
        cin.file = 0;
    }
    if (cin.hnodes1)
    {
        Z_Free(cin.hnodes1);
        cin.hnodes1 = NULL;
    }

    // switch the sample rate back to its original value if necessary
    if (cin.s_khz_original != 0)
    {
        Cvar_Set("s_khz", va("%d", cin.s_khz_original));
        cin.s_khz_original = 0;
    }
}

/*
====================
SCR_FinishCinematic

Called when either the cinematic completes, or it is aborted
====================
*/
void SCR_FinishCinematic(void)
{
    SCR_StopCinematic();

    // tell the server to advance to the next map / cinematic
    CL_ClientCommand(va("nextserver %i\n", cl.servercount));
}

//==========================================================================

/*
==================
SmallestNode1
==================
*/
int	SmallestNode1(int numhnodes)
{
    int		i;
    int		best, bestnode;

    best = 99999999;
    bestnode = -1;
    for (i = 0; i < numhnodes; i++)
    {
        if (cin.h_used[i])
            continue;
        if (!cin.h_count[i])
            continue;
        if (cin.h_count[i] < best)
        {
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
void Huff1TableInit(void)
{
    int		prev;
    int		j;
    int		*node, *nodebase;
    byte	counts[256];
    int		numhnodes;

    cin.hnodes1 = Z_Malloc(256 * 256 * 2 * 4);
    memset(cin.hnodes1, 0, 256 * 256 * 2 * 4);

    for (prev = 0; prev < 256; prev++)
    {
        memset(cin.h_count, 0, sizeof(cin.h_count));
        memset(cin.h_used, 0, sizeof(cin.h_used));

        // read a row of counts
        FS_Read(counts, sizeof(counts), cin.file);
        for (j = 0; j < 256; j++)
            cin.h_count[j] = counts[j];

        // build the nodes
        numhnodes = 256;
        nodebase = cin.hnodes1 + prev * 256 * 2;

        while (numhnodes != 511)
        {
            node = nodebase + (numhnodes - 256) * 2;

            // pick two lowest counts
            node[0] = SmallestNode1(numhnodes);
            if (node[0] == -1)
                break;	// no more

            node[1] = SmallestNode1(numhnodes);
            if (node[1] == -1)
                break;

            cin.h_count[numhnodes] = cin.h_count[node[0]] + cin.h_count[node[1]];
            numhnodes++;
        }

        cin.numhnodes1[prev] = numhnodes - 1;
    }
}

/*
==================
Huff1Decompress
==================
*/
cblock_t Huff1Decompress(cblock_t in)
{
    byte		*input;
    byte		*out_p;
    int			nodenum;
    int			count;
    cblock_t	out;
    int			inbyte;
    int			*hnodes, *hnodesbase;
    //int		i;

        // get decompressed count
    count = in.data[0] + (in.data[1] << 8) + (in.data[2] << 16) + (in.data[3] << 24);
    input = in.data + 4;
    out_p = out.data = Z_Malloc(count);

    // read bits

    hnodesbase = cin.hnodes1 - 256 * 2;	// nodes 0-255 aren't stored

    hnodes = hnodesbase;
    nodenum = cin.numhnodes1[0];
    while (count)
    {
        inbyte = *input++;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
        //-----------
        if (nodenum < 256)
        {
            hnodes = hnodesbase + (nodenum << 9);
            *out_p++ = nodenum;
            if (!--count)
                break;
            nodenum = cin.numhnodes1[nodenum];
        }
        nodenum = hnodes[nodenum * 2 + (inbyte & 1)];
        inbyte >>= 1;
    }

    if (input - in.data != in.count && input - in.data != in.count + 1)
    {
        Com_Printf("Decompression overread by %li", (input - in.data) - in.count);
    }
    out.count = out_p - out.data;

    return out;
}

extern uint32_t d_8to24table[256];

/*
==================
SCR_ReadNextFrame
==================
*/
qhandle_t SCR_ReadNextFrame(void)
{
    int		r;
    int		command;
    byte	samples[22050 / 14 * 4];
    byte	compressed[0x20000];
    int		size;
    byte	*pic;
    cblock_t	in, huf1;
    int		start, end, count;

    // read the next frame
    r = FS_Read(&command, 4, cin.file);
    if (r == 0)		// we'll give it one more chance
        r = FS_Read(&command, 4, cin.file);

    if (r != 4)
        return 0;
    command = LittleLong(command);
    if (command == 2)
        return 0;	// last frame marker

    if (command == 1)
    {	// read palette
        FS_Read(cin.palette, sizeof(cin.palette), cin.file);
        cin.palette_active = true;
    }

    // decompress the next frame
    FS_Read(&size, 4, cin.file);
    size = LittleLong(size);
    if (size > sizeof(compressed) || size < 1)
        Com_Error(ERR_DROP, "Bad compressed frame size");
    FS_Read(compressed, size, cin.file);

    // read sound
    start = cin.frame_index*cin.s_rate / 14;
    end = (cin.frame_index + 1)*cin.s_rate / 14;
    count = end - start;

    FS_Read(samples, count*cin.s_width*cin.s_channels, cin.file);

    S_RawSamples(count, cin.s_rate, cin.s_width, cin.s_channels, samples, 1.0f);

    in.data = compressed;
    in.count = size;

    huf1 = Huff1Decompress(in);

    pic = huf1.data;

    uint32_t* rgba = Z_Malloc(cin.width * cin.height * 4);
    uint32_t* wptr = rgba;

    for (int y = 0; y < cin.height; y++)
    {
        if (cin.palette_active)
        {
            for (int x = 0; x < cin.width; x++)
            {
                byte* src = cin.palette + (*pic) * 3;
                *wptr = MakeColor(src[0], src[1], src[2], 255);
                pic++;
                wptr++;
            }
        }
        else
        {
            for (int x = 0; x < cin.width; x++)
            {
                *wptr = d_8to24table[*pic];
                pic++;
                wptr++;
            }
        }
    }

    Z_Free(huf1.data);

    cin.frame_index++;

    const char* image_name = va("%s[%d]", cin.file_name, cin.frame_index);
    return R_RegisterRawImage(image_name, cin.width, cin.height, (byte*)rgba, IT_SPRITE, IF_SRGB);
}


/*
==================
SCR_RunCinematic

==================
*/
void SCR_RunCinematic(void)
{
    int		frame;

    if (cin.start_time <= 0)
        return;

    if (cin.frame_index == -1)
        return; // static image

    if (cls.key_dest != KEY_GAME)
    {
        // pause if menu or console is up
        cin.start_time = cls.realtime - cin.frame_index * 1000 / 14;

        S_UnqueueRawSamples();

        return;
    }

    frame = (cls.realtime - cin.start_time)*14.0 / 1000;
    if (frame <= cin.frame_index)
        return;
    if (frame > cin.frame_index + 1)
    {
        // Com_Printf("Dropped frame: %i > %i\n", frame, cin.frame_index + 1);
        cin.start_time = cls.realtime - cin.frame_index * 1000 / 14;
    }

    R_UnregisterImage(cl.image_precache[0]);
    cl.image_precache[0] = SCR_ReadNextFrame();

    if (!cl.image_precache[0])
    {
        SCR_FinishCinematic();
        cin.start_time = 1;	// hack to get the black screen behind loading
        SCR_BeginLoadingPlaque();
        cin.start_time = 0;
        return;
    }
}

/*
==================
SCR_PlayCinematic

==================
*/
void SCR_PlayCinematic(const char *name)
{
    int		width, height;
    int		old_khz;

    // make sure CD isn't playing music
    OGG_Stop();

    cin.s_khz_original = 0;

    cin.frame_index = 0;
    cin.start_time = 0;

    if (!COM_CompareExtension(name, ".pcx"))
    {
        cl.image_precache[0] = R_RegisterPic2(name);
        if (!cl.image_precache[0]) {
            SCR_FinishCinematic();
            return;
        }
    }
    else if (!COM_CompareExtension(name, ".cin"))
    {
        if (!Cvar_VariableValue("cl_cinematics"))
        {
            SCR_FinishCinematic();
            return;
        }

        Q_snprintf(cin.file_name, sizeof(cin.file_name), "video/%s", name);

        FS_FOpenFile(cin.file_name, &cin.file, FS_MODE_READ);
        if (!cin.file)
        {
            Com_WPrintf("Cinematic \"%s\" not found. Skipping.\n", name);
            SCR_FinishCinematic();
            return;
        }

        FS_Read(&width, 4, cin.file);
        FS_Read(&height, 4, cin.file);
        cin.width = LittleLong(width);
        cin.height = LittleLong(height);

        FS_Read(&cin.s_rate, 4, cin.file);
        cin.s_rate = LittleLong(cin.s_rate);
        FS_Read(&cin.s_width, 4, cin.file);
        cin.s_width = LittleLong(cin.s_width);
        FS_Read(&cin.s_channels, 4, cin.file);
        cin.s_channels = LittleLong(cin.s_channels);

        Huff1TableInit();

        cin.palette_active = false;

        // switch to 22 khz sound if necessary
        old_khz = Cvar_VariableValue("s_khz");
        if (old_khz != cin.s_rate / 1000 && s_started == SS_DMA)
        {
            cin.s_khz_original = old_khz;
            Cvar_Set("s_khz", va("%d", cin.s_rate / 1000));
        }

        cin.frame_index = 0;
        cl.image_precache[0] = SCR_ReadNextFrame();
        cin.start_time = cls.realtime;
    }
    else
    {
        SCR_FinishCinematic();
        return;
    }

    cls.state = ca_cinematic;

    SCR_EndLoadingPlaque();     // get rid of loading plaque
    Con_Close(false);          // get rid of connection screen
}
