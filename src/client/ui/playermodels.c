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

#include "ui.h"
#include "common/files.h"

/*
=============================================================================

PLAYER MODELS

=============================================================================
*/

static qboolean IconOfSkinExists(char *skin, char **pcxfiles, int npcxfiles)
{
    int i;
    char scratch[MAX_OSPATH];

    COM_StripExtension(skin, scratch, sizeof(scratch));
    Q_strlcat(scratch, "_i.pcx", sizeof(scratch));

    for (i = 0; i < npcxfiles; i++) {
        if (Q_stricmp(pcxfiles[i], scratch) == 0)
            return qtrue;
    }

    return qfalse;
}

static int pmicmpfnc(const void *_a, const void *_b)
{
    const playerModelInfo_t *a = (const playerModelInfo_t *)_a;
    const playerModelInfo_t *b = (const playerModelInfo_t *)_b;

    /*
    ** sort by male, female, then alphabetical
    */
    if (strcmp(a->directory, "male") == 0)
        return -1;
    else if (strcmp(b->directory, "male") == 0)
        return 1;

    if (strcmp(a->directory, "female") == 0)
        return -1;
    else if (strcmp(b->directory, "female") == 0)
        return 1;

    return strcmp(a->directory, b->directory);
}

void PlayerModel_Load(void)
{
    char scratch[MAX_QPATH];
    size_t len;
    int ndirs = 0;
    char *dirnames[MAX_PLAYERMODELS];
    int i, j;
    char **list;
    char *s, *p;
    int numFiles;
    playerModelInfo_t *pmi;

    uis.numPlayerModels = 0;

    // get a list of directories
    if (!(list = (char **)FS_ListFiles(NULL, "players/*/tris.md2", FS_SEARCH_BYFILTER | FS_SEARCH_SAVEPATH, &numFiles))) {
        return;
    }

    for (i = 0; i < numFiles; i++) {
        len = Q_strlcpy(scratch, list[i], sizeof(scratch));
        if (len >= sizeof(scratch))
            continue;

        // make short name for the model
        if (!(s = strchr(scratch, '/')))
            continue;
        s++;

        if (!(p = strchr(s, '/')))
            continue;
        *p = 0;

        for (j = 0; j < ndirs; j++) {
            if (!strcmp(dirnames[j], s)) {
                break;
            }
        }

        if (j != ndirs) {
            continue;
        }

        dirnames[ndirs++] = UI_CopyString(s);
        if (ndirs == MAX_PLAYERMODELS) {
            break;
        }
    }

    FS_FreeList((void **)list);

    if (!ndirs) {
        return;
    }

    // go through the subdirectories
    for (i = 0; i < ndirs; i++) {
        int k, s;
        char **pcxnames;
        char **skinnames;
        int npcxfiles;
        int nskins = 0;

        // verify the existence of tris.md2
        Q_concat(scratch, sizeof(scratch), "players/", dirnames[i], "/tris.md2", NULL);
        if (!FS_FileExists(scratch)) {
            goto skip;
        }

        // verify the existence of at least one pcx skin
        Q_concat(scratch, sizeof(scratch), "players/", dirnames[i], NULL);
        pcxnames = (char **)FS_ListFiles(scratch, ".pcx", 0, &npcxfiles);
        if (!pcxnames) {
            goto skip;
        }

        // count valid skins, which consist of a skin with a matching "_i" icon
        for (k = 0; k < npcxfiles; k++) {
            if (!strstr(pcxnames[k], "_i.pcx")) {
                if (IconOfSkinExists(pcxnames[k], pcxnames, npcxfiles)) {
                    nskins++;
                }
            }
        }

        if (!nskins) {
            FS_FreeList((void **)pcxnames);
            goto skip;
        }

        skinnames = UI_Malloc(sizeof(char *) * (nskins + 1));
        skinnames[nskins] = NULL;

        // copy the valid skins
        for (s = 0, k = 0; k < npcxfiles; k++) {
            if (!strstr(pcxnames[k], "_i.pcx")) {
                if (IconOfSkinExists(pcxnames[k], pcxnames, npcxfiles)) {
                    COM_StripExtension(pcxnames[k], scratch, sizeof(scratch));
                    skinnames[s++] = UI_CopyString(scratch);
                }
            }
        }

        FS_FreeList((void **)pcxnames);

        // at this point we have a valid player model
        pmi = &uis.pmi[uis.numPlayerModels++];
        pmi->nskins = nskins;
        pmi->skindisplaynames = skinnames;
        pmi->directory = dirnames[i];
        continue;

skip:
        Z_Free(dirnames[i]);
    }

    qsort(uis.pmi, uis.numPlayerModels, sizeof(uis.pmi[0]), pmicmpfnc);
}

void PlayerModel_Free(void)
{
    playerModelInfo_t *pmi;
    int i, j;

    for (i = 0, pmi = uis.pmi; i < uis.numPlayerModels; i++, pmi++) {
        if (pmi->skindisplaynames) {
            for (j = 0; j < pmi->nskins; j++) {
                Z_Free(pmi->skindisplaynames[j]);
            }
            Z_Free(pmi->skindisplaynames);
        }
        Z_Free(pmi->directory);
        memset(pmi, 0, sizeof(*pmi));
    }

    uis.numPlayerModels = 0;
}

