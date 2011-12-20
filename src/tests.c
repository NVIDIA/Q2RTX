/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "common.h"
#include "files.h"
#include "sys_public.h"
#include "q_list.h"
#include "bsp.h"

// test error shutdown procedures
static void Com_Error_f(void)
{
    Com_Error(ERR_FATAL, "%s", Cmd_Argv(1));
}

static void Com_ErrorDrop_f(void)
{
    Com_Error(ERR_DROP, "%s", Cmd_Argv(1));
}

static void Com_Freeze_f(void)
{
    unsigned time, msec;
    float seconds;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <seconds>\n", Cmd_Argv(0));
        return;
    }

    seconds = atof(Cmd_Argv(1));
    if (seconds < 0) {
        return;
    }

    time = Sys_Milliseconds();
    msec = seconds * 1000;
    while (Sys_Milliseconds() - time < msec)
        ;
}

// test crash dumps and NX support
static void Com_Crash_f(void)
{
    static byte buf1[16];
    byte buf2[16], *buf3;
    int i = atoi(Cmd_Argv(1));

    switch (i) {
    case 1:
        // data
        memset(buf1, 0xcc, 16);
        buf1[0] = 0xc3;
        ((void (*)(void))buf1)();
        break;
    case 2:
        // stack
        memset(buf2, 0xcc, 16);
        buf2[0] = 0xc3;
        ((void (*)(void))buf2)();
        break;
    case 3:
        // heap
        buf3 = Z_Malloc(16);
        memset(buf3, 0xcc, 16);
        buf3[0] = 0xc3;
        ((void (*)(void))buf3)();
        Z_Free(buf3);
        break;
    default:
        *(uint32_t *)1024 = 0x123456;
        break;
    }
}

// use twice normal print buffer size to test for overflows, etc
static void Com_PrintJunk_f(void)
{
    char buf[MAXPRINTMSG * 2];
    int i, count;

    // generate some junk
    for (i = 0; i < sizeof(buf) - 1; i++)
        buf[i] = 'a' + i % ('z' - 'a' + 1);
    buf[i] = 0;

    // randomly break into words
    for (i = 0; i < 64; i++)
        buf[rand() % (sizeof(buf) - 1)] = ' ';

    if (Cmd_Argc() > 1)
        count = atoi(Cmd_Argv(1));
    else
        count = 1;

    for (i = 0; i < count; i++)
        Com_Printf("%s", buf);
    Com_Printf("\n");
}

static void BSP_Test_f(void)
{
    void **list;
    char *name;
    int i, count, errors;
    bsp_t *bsp;
    qerror_t ret;
    unsigned start, end;

    list = FS_ListFiles("maps", ".bsp", FS_SEARCH_SAVEPATH, &count);
    if (!list) {
        Com_Printf("No maps found\n");
        return;
    }

    start = Sys_Milliseconds();

    errors = 0;
    for (i = 0; i < count; i++) {
        name = list[i];
        ret = BSP_Load(name, &bsp);
        if (!bsp) {
            Com_EPrintf("%s: %s\n", name, Q_ErrorString(ret));
            errors++;
            continue;
        }

        Com_DPrintf("%s: success\n", name);
        BSP_Free(bsp);
    }

    end = Sys_Milliseconds();

    Com_Printf("%d msec, %d failures, %d maps tested\n",
               end - start, errors, count);

    FS_FreeList(list);
}

typedef struct {
    const char *filter;
    const char *string;
    int result;
} wildtest_t;

static const wildtest_t wildtests[] = {
    { "",               "",                 1 },
    { "*",              "foo",              1 },
    { "***",            "foo",              1 },
    { "foo*bar",        "foobar",           1 },
    { "foo*bar*baz",    "foobaz",           0 },
    { "bar*",           "bar",              1 },
    { "bar*",           "barfoo",           1 },
    { "???*",           "barfoo",           1 },
    { "???\\*",         "bar*",             1 },
    { "???\\*",         "bar*bar",          0 },
    { "???\\*",         "bar?",             0 },
    { "*\\?",           "bar?",             1 },
    { "\\\\",           "\\",               1 },
    { "\\",             "\\",               0 },
    { "foo*bar\\*baz",  "foo*abcbar*baz",   1 },
    { "\\a\\b\\c",      "abc",              1 },
};

static const int numwildtests = sizeof(wildtests) / sizeof(wildtests[0]);

static void Com_TestWild_f(void)
{
    const wildtest_t *w;
    qboolean match;
    int i, errors;

    errors = 0;
    for (i = 0; i < numwildtests; i++) {
        w = &wildtests[i];
        match = Com_WildCmp(w->filter, w->string);
        if (match != w->result) {
            Com_EPrintf(
                "Com_WildCmp( \"%s\", \"%s\" ) == %d, expected %d\n",
                w->filter, w->string, match, w->result);
            errors++;
        }
    }

    Com_Printf("%d failures, %d patterns tested\n",
               errors, numwildtests);
}

typedef struct {
    const char *in;
    const char *out;
} normtest_t;

static const normtest_t normtests[] = {
    { "",               "",         },
    { "///",            "",         },
    { "foo///",         "foo/",     },
    { "\\/\\",          "",         },
    { "///foo",         "foo"       },
    { "\\/foo",         "foo"       },
    { "foo\\bar",       "foo/bar"   },
    { "foo/..",         ""          },
    { "foo.bar.baz/..", ""          },
    { "foo/../bar",     "bar"       },
    { "foo/.././bar",   "bar"       },
    { "foo/./../bar",   "bar"       },
    { "foo/./bar",      "foo/bar"   },
    { "foo//bar",       "foo/bar"   },
    { "foo///.////bar", "foo/bar"   },
    { "foo/./././bar",  "foo/bar"   },
    { "./foo",          "foo"       },
    { "../foo",         "foo"       },
    { "../../../foo",   "foo"       },
    { "./../../foo",    "foo"       },
    { "../bar/../foo",  "foo"       },
    { "foo/bar/..",     "foo"       },
    { "foo/bar/../",    "foo/"      },
    { "foo/bar/.",      "foo/bar"   },
    { "foo/bar/./",     "foo/bar/"  },
    { "..",             ""          },
    { ".",              ""          },
    { "/..",            ""          },
    { "/.",             ""          },
    { "../",            ""          },
    { "./",             ""          },
    { "/../",           ""          },
    { "/./",            ""          },
    { "../..",          ""          },
    { "../../../../",   ""          },
    { "../foo..bar/",   "foo..bar/" },
    { "......./",       "......./"  },

    { "foo/bar/baz/abc/../def",                             "foo/bar/baz/def"   },
    { "foo/bar/baz/abc/../../def",                          "foo/bar/def"       },
    { "foo/bar/../baz/abc/../def",                          "foo/baz/def"       },
    { "foo/bar/../../baz/abc/../../def",                    "def"               },
    { "foo/bar/../../../../baz/abc/../../zzz/../def/ghi",   "def/ghi"           },
};

static const int numnormtests = sizeof(normtests) / sizeof(normtests[0]);

static void Com_TestNorm_f(void)
{
    const normtest_t *n;
    char buffer[MAX_QPATH];
    int i, errors, pass;

    for (pass = 0; pass < 2; pass++) {
        errors = 0;
        for (i = 0; i < numnormtests; i++) {
            n = &normtests[i];
            if (pass == 0) {
                FS_NormalizePath(buffer, n->in);
            } else {
                // test in place operation
                strcpy(buffer, n->in);
                FS_NormalizePath(buffer, buffer);
            }
            if (strcmp(n->out, buffer)) {
                Com_EPrintf(
                    "FS_NormalizePath( \"%s\" ) == \"%s\", expected \"%s\" (pass %d)\n",
                    n->in, buffer, n->out, pass);
                errors++;
            }
        }
        if (errors)
            break;
    }

    Com_Printf("%d failures, %d paths tested (%d passes)\n",
               errors, numnormtests, pass);
}

void Com_InitTests(void)
{
    Cmd_AddCommand("error", Com_Error_f);
    Cmd_AddCommand("errordrop", Com_ErrorDrop_f);
    Cmd_AddCommand("freeze", Com_Freeze_f);
    Cmd_AddCommand("crash", Com_Crash_f);
    Cmd_AddCommand("printjunk", Com_PrintJunk_f);
    Cmd_AddCommand("bsptest", BSP_Test_f);
    Cmd_AddCommand("wildtest", Com_TestWild_f);
    Cmd_AddCommand("normtest", Com_TestNorm_f);
}

