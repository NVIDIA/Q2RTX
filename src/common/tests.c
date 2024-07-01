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

#include "shared/shared.h"
#include "common/bsp.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/files.h"
#include "common/mdfour.h"
#include "common/tests.h"
#include "refresh/refresh.h"
#include "system/system.h"
#include "client/sound/sound.h"

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

    seconds = Q_atof(Cmd_Argv(1));
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
    int i = Q_atoi(Cmd_Argv(1));

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
        buf[Q_rand() % (sizeof(buf) - 1)] = ' ';

    if (Cmd_Argc() > 1)
        count = Q_atoi(Cmd_Argv(1));
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
    int ret;
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
            Com_EPrintf("Couldn't load %s: %s\n", name, BSP_ErrorString(ret));
            errors++;
            continue;
        }

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

static const int numwildtests = q_countof(wildtests);

static void Com_TestWild_f(void)
{
    const wildtest_t *w;
    bool match;
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

static const int numnormtests = q_countof(normtests);

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
                FS_NormalizePathBuffer(buffer, n->in, sizeof(buffer));
            } else {
                // test in place operation
                strcpy(buffer, n->in);
                FS_NormalizePath(buffer);
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

typedef struct {
    const char *string;
    int result;
} info_validate_test_t;

static const info_validate_test_t info_validate_tests[] = {
    { "",                       0 },
    { "\\",                     0 },
    { "\\\\",                   0 },
    { "\\\\\\",                 0 },
    { "\\\\\\key\\value",       1 },
    { "\\\\value",              1 },
    { "\\key\\\\key\\value",    1 },
    { "\\key\\value",           1 },
    { "\\key\\value\\",         0 },
    { "key\\value",             1 },
    { "key\\value\\",           0 },
    { "\\key",                  0 },
    { "\\key\\",                0 },
    { "key",                    0 },
    { "key\\",                  0 },
    { "\\ke;y\\value",          0 },
    { "\\ke\"y\\value",         0 },
    { "\\key\\va;lue",          0 },
    { "\\key\\va\"lue",         0 },
    { "\\ke\x81y\\value",       0 },
    { "\\key\\val\x82ue",       0 },
    { "\\ke\ny\\value",         0 },
    { "\\key\\val\nue",         0 },
    { "\\abcdabcdabcdabcd"
        "abcdabcdabcdabcd"
        "abcdabcdabcdabcd"
        "abcdabcdabcdabcd"
        "\\value",              0 },
    { "\\key\\"
        "abcdabcdabcdabcd"
        "abcdabcdabcdabcd"
        "abcdabcdabcdabcd"
        "abcdabcdabcdabcd",     0 },
};

static const int num_info_validate_tests = q_countof(info_validate_tests);

typedef struct {
    const char *string;
    const char *key;
    const char *result;
} info_remove_test_t;

static const info_remove_test_t info_remove_tests[] = {
    { "",                                           "",     ""               },
    { "\\key\\value",                               "key",  ""               },
    { "key\\value",                                 "key",  ""               },
    { "\\key1\\value1\\key2\\value2",               "key1", "\\key2\\value2" },
    { "\\key1\\value1\\key2\\value2",               "key2", "\\key1\\value1" },
    { "\\key1\\value1\\key2\\value2\\key1\\value3", "key1", "\\key2\\value2" },
    { "\\key\\value",                               "",     "\\key\\value"   },
    { "\\\\value",                                  "",     ""               },
};

static const int num_info_remove_tests = q_countof(info_remove_tests);

typedef struct {
    const char *string;
    const char *key;
    const char *value;
    int result_b;
    const char *result_s;
} info_set_test_t;

static const info_set_test_t info_set_tests[] = {
    { "",                                           "",         "",             1,  ""                              },
    { "\\key\\value1",                              "key",      "value2",       1,  "\\key\\value2"                 },
    { "\\key\\value1",                              "key",      "",             1,  ""                              },
    { "\\key\\value1",                              "",         "value2",       1,  "\\key\\value1\\\\value2"       },
    { "\\key\\value1",                              "ke\"y",    "value2",       0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "ke\\y",    "value2",       0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "ke;y",     "value2",       0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "ke\xa2y",  "value2",       0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "ke\xdcy",  "value2",       0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "ke\xbby",  "value2",       0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "key",      "val\"ue2",     0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "key",      "val\\ue2",     0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "key",      "val;ue2",      0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "key",      "val\xa2ue2",   0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "key",      "val\xdcue2",   0,  "\\key\\value1"                 },
    { "\\key\\value1",                              "key",      "val\xbbue2",   0,  "\\key\\value1"                 },
    { "\\key1\\value1\\key2\\value2",               "key1",     "value3",       1,  "\\key2\\value2\\key1\\value3"  },
    { "\\key1\\value1\\key2\\value2",               "key1",     "",             1,  "\\key2\\value2"                },
    { "\\key1\\value1\\key2\\value2\\key1\\value3", "key1",     "value4",       1,  "\\key2\\value2\\key1\\value4"  },
    { "\\key1\\value1\\key2\\value2",               "key1",     "v\xe1lue3",    1,  "\\key2\\value2\\key1\\value3"  },
    { "\\key\\value",                               "key",      "\r\n",         1,  "\\key\\"                       },
    { "\\key1\\value1\\key2\\value2",               "key1",     "\r\n",         1,  "\\key2\\value2\\key1\\"        },
};

static const int num_info_set_tests = q_countof(info_set_tests);

static void Com_TestInfo_f(void)
{
    const info_validate_test_t *v;
    const info_remove_test_t *r;
    const info_set_test_t *s;
    char buffer[MAX_INFO_STRING];
    int i, errors;
    bool result;

    errors = 0;
    for (i = 0; i < num_info_validate_tests; i++) {
        v = &info_validate_tests[i];
        result = Info_Validate(v->string);
        if (result != v->result) {
            Com_EPrintf("Info_Validate( \"%s\" ) == %d, expected %d\n",
                        v->string, result, v->result);
            errors++;
        }
    }

    for (i = 0; i < num_info_remove_tests; i++) {
        r = &info_remove_tests[i];
        Q_strlcpy(buffer, r->string, sizeof(buffer));
        Info_RemoveKey(buffer, r->key);
        if (strcmp(buffer, r->result)) {
            Com_EPrintf("Info_RemoveKey( \"%s\", \"%s\" ) == \"%s\", expected \"%s\"\n",
                        r->string, r->key, buffer, r->result);
            errors++;
        }
    }

    for (i = 0; i < num_info_set_tests; i++) {
        s = &info_set_tests[i];
        Q_strlcpy(buffer, s->string, sizeof(buffer));
        result = Info_SetValueForKey(buffer, s->key, s->value);
        if (result != s->result_b || strcmp(buffer, s->result_s)) {
            Com_EPrintf("Info_SetValueForKey( \"%s\", \"%s\", \"%s\" ) == \"%s\" (%d), expected \"%s\" (%d)\n",
                        s->string, s->key, s->value, buffer, result, s->result_s, s->result_b);
            errors++;
        }
    }

    Com_Printf("%d failures, %d strings tested\n", errors,
               num_info_validate_tests +
               num_info_remove_tests +
               num_info_set_tests);
}

typedef struct {
    size_t size;
    size_t len1, len2;
    bool overflow1, overflow2;
    const char *res;
} snprintf_test_t;

static const snprintf_test_t snprintf_tests[] = {
    { 12, 11, 11, 0, 0, "hello world"     },
    { 11, 11, 10, 1, 0, "hello worl"      },
    { 10, 11,  9, 1, 0, "hello wor"       },
    { 0,  11,  0, 1, 1, "xxxxxxxxxxxxxxx" },
};

static const int num_snprintf_tests = q_countof(snprintf_tests);

static void Com_TestSnprintf_f(void)
{
    const snprintf_test_t *t;
    char buf[16], *ptr;
    size_t len;
    int i, errors;
    bool overflow;

    errors = 0;
    for (i = 0; i < num_snprintf_tests; i++) {
        t = &snprintf_tests[i];

        ptr = t->size ? buf : NULL;

        memset(buf, 'x', 15); buf[15] = 0;
        len = Q_snprintf(ptr, t->size, "hello world");
        overflow = len >= t->size;
        if (t->len1 != len || strcmp(buf, t->res) || overflow != t->overflow1) {
            Com_EPrintf("%s( %p, %zu ) == \"%s\" (%zu) [%d], expected \"%s\" (%zu) [%d]\n",
                        "Q_snprintf", ptr, t->size, buf, len, overflow, t->res, t->len1, t->overflow1);
            errors++;
        }

        memset(buf, 'x', 15); buf[15] = 0;
        len = Q_scnprintf(ptr, t->size, "hello world");
        overflow = len >= t->size;
        if (t->len2 != len || strcmp(buf, t->res) || overflow != t->overflow2) {
            Com_EPrintf("%s( %p, %zu ) == \"%s\" (%zu) [%d], expected \"%s\" (%zu) [%d]\n",
                        "Q_scnprintf", ptr, t->size, buf, len, overflow, t->res, t->len2, t->overflow2);
            errors++;
        }
    }

    Com_Printf("%d failures, %d strings tested\n", errors, num_snprintf_tests * 2);
}

#if USE_REF
static void Com_TestModels_f(void)
{
    void **list;
    int i, count, errors;
    unsigned start, end;

    list = FS_ListFiles("models", ".md2", FS_SEARCH_SAVEPATH, &count);
    if (!list) {
        Com_Printf("No models found\n");
        return;
    }

    start = Sys_Milliseconds();

    errors = 0;
    for (i = 0; i < count; i++) {
        if (!R_RegisterModel(list[i])) {
            errors++;
            continue;
        }
    }

    end = Sys_Milliseconds();

    Com_Printf("%d msec, %d failures, %d models tested\n",
               end - start, errors, count);

    FS_FreeList(list);
}
#endif

#if USE_CLIENT
static void Com_TestSounds_f(void)
{
    void **list;
    int i, count, errors;
    unsigned start, end;

    list = FS_ListFiles(NULL, ".wav", FS_SEARCH_SAVEPATH, &count);
    if (!list) {
        Com_Printf("No sounds found\n");
        return;
    }

    start = Sys_Milliseconds();

    S_BeginRegistration();

    errors = 0;
    for (i = 0; i < count; i++) {
        if (i > 0 && !(i & (MAX_SOUNDS_OLD - 1))) {
            S_EndRegistration();
            S_BeginRegistration();
        }
        if (!S_RegisterSound(va("#%s", (char *)list[i]))) {
            errors++;
            continue;
        }
    }

    S_EndRegistration();

    end = Sys_Milliseconds();

    Com_Printf("%d msec, %d failures, %d sounds tested\n",
               end - start, errors, count);

    FS_FreeList(list);
}
#endif

static const char *const mdfour_str[] = {
    "", "a", "abc", "message digest", "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
    "12345678901234567890123456789012345678901234567890123456789012345678901234567890"
};

static const char *const mdfour_res[] = {
    "31d6cfe0d16ae931b73c59d7e0c089c0", "bde52cb31de33e46245e05fbdbd6fb24",
    "a448017aaf21d8525fc10ae87aa6729d", "d9130a8164549fe818874806e1c7014b",
    "d79e1c308aa5bbcdeea8ed63df412da9", "043f8582f241db351ce627e153e7f0e4",
    "e33b4ddc9c38f2199c3e7b164fcc0536"
};

static const uint32_t blocksum_res[] = {
    0xc6f640b7, 0x2aec8e5f, 0x5da10e2e, 0x24dc0744, 0x3767d26c, 0xb2897fb9, 0xe5c1f1ac
};

static bool mdfour_test(int num, int chunk)
{
    struct mdfour md;
    uint8_t digest[16];
    uint8_t *data = (uint8_t *)mdfour_str[num];
    size_t size = strlen(mdfour_str[num]);
    const char *res = mdfour_res[num];
    int i;

    mdfour_begin(&md);
    if (chunk == -1) {
        mdfour_update(&md, data, size);
    } else while (size) {
        size_t n = min(size, chunk);
        mdfour_update(&md, data, n);
        data += n;
        size -= n;
    }
    mdfour_result(&md, digest);

    for (i = 0; i < 16; i++) {
        int c1 = Q_charhex(res[i*2+0]);
        int c2 = Q_charhex(res[i*2+1]);
        if (digest[i] != (c1 << 4 | c2))
            break;
    }

    if (i != 16) {
        Com_EPrintf("String '%s', expected '%s', calculated '", mdfour_str[num], mdfour_res[num]);
        for (i = 0; i < 16; i++)
            Com_EPrintf("%02x", digest[i]);
        Com_EPrintf("'\n");
        return false;
    }

    return true;
}

static void Com_MdfourTest_f(void)
{
    static const int8_t chunks[] = { -1, 1, 3, 7, 16, 32, 64 };
    int errors = 0;
    int tests = 0;

    for (int i = 0; i < q_countof(chunks); i++) {
        Com_Printf("Testing chunk size %d...\n", chunks[i]);
        for (int j = 0; j < q_countof(mdfour_str); j++) {
            if (!mdfour_test(j, chunks[i]))
                errors++;
            tests++;
        }
    }

    for (int i = 0; i < q_countof(mdfour_str); i++) {
        uint32_t res = Com_BlockChecksum((uint8_t *)mdfour_str[i], strlen(mdfour_str[i]));
        if (res != blocksum_res[i]) {
            Com_EPrintf("String '%s', expected %#x, calculated %#x\n", mdfour_str[i], blocksum_res[i], res);
            errors++;
        }
        tests++;
    }

    Com_Printf("%d failures, %d strings tested\n", errors, tests);
}

typedef struct {
    const char *ext;
    const char *name;
    bool result;
} extcmptest_t;

static const extcmptest_t extcmptests[] = {
    { ".foo;.bar",          "test.bar",         true  },
    { ".foo;.bar",          "test.FOO",         true  },
    { ".foo;.bar",          "test.baz",         false },
    { ".foo;.BAR;.baz;",    "test.bar",         true  },
    { ".abc;.foo;.def",     "",                 false },
    { "",                   "test",             true  },
    { "",                   "test.foo",         true  },
    { ".foo.bar",           "test.foo.bar",     true  },
    { ".bar;;.baz",         "test",             true  },
    { ";;;",                "test.foo",         true  },
};

static const int numextcmptests = q_countof(extcmptests);

static void Com_ExtCmpTest_f(void)
{
    int errors = 0;

    for (int i = 0; i < numextcmptests; i++) {
        const extcmptest_t *t = &extcmptests[i];
        bool res = FS_ExtCmp(t->ext, t->name);
        if (res != t->result) {
            Com_EPrintf("FS_ExtCmp(\"%s\", \"%s\") == %d, expected %d\n",
                        t->ext, t->name, res, t->result);
            errors++;
        }
    }

    Com_Printf("%d failures, %d strings tested\n", errors, numextcmptests);
}

void TST_Init(void)
{
    Cmd_AddCommand("error", Com_Error_f);
    Cmd_AddCommand("errordrop", Com_ErrorDrop_f);
    Cmd_AddCommand("freeze", Com_Freeze_f);
    Cmd_AddCommand("crash", Com_Crash_f);
    Cmd_AddCommand("printjunk", Com_PrintJunk_f);
    Cmd_AddCommand("bsptest", BSP_Test_f);
    Cmd_AddCommand("wildtest", Com_TestWild_f);
    Cmd_AddCommand("normtest", Com_TestNorm_f);
    Cmd_AddCommand("infotest", Com_TestInfo_f);
    Cmd_AddCommand("snprintftest", Com_TestSnprintf_f);
#if USE_REF
    Cmd_AddCommand("modeltest", Com_TestModels_f);
#endif
#if USE_CLIENT
    Cmd_AddCommand("soundtest", Com_TestSounds_f);
#endif
    Cmd_AddCommand("mdfourtest", Com_MdfourTest_f);
    Cmd_AddCommand("extcmptest", Com_ExtCmpTest_f);
}

