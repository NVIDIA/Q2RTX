/*
Copyright (C) 2003-2008 Andrey Nazarov

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
#include "common/mdfour.h"

/*
=======================================================================

DEMOS MENU

=======================================================================
*/

#define DEMO_EXTENSIONS ".dm2;.dm2.gz;.mvd2;.mvd2.gz"

#define DEMO_EXTRASIZE  q_offsetof(demoEntry_t, name)

#define DEMO_MVD_POV    "\x90\xcd\xd6\xc4\x91" // [MVD]
#define DEMO_DIR_SIZE   "\x90\xc4\xc9\xd2\x91" // [DIR]

#define ENTRY_UP    1
#define ENTRY_DN    2
#define ENTRY_DEMO  3

#define COL_NAME    0
#define COL_DATE    1
#define COL_SIZE    2
#define COL_MAP     3
#define COL_POV     4
#define COL_MAX     5

typedef struct {
    unsigned    type;
    int64_t     size;
    time_t      mtime;
    char        name[1];
} demoEntry_t;

typedef struct m_demos_s {
    menuFrameWork_t menu;
    menuList_t      list;
    int             numDirs;
    uint8_t         hash[16];
    char            browse[MAX_OSPATH];
    int             selection;
    int             year;
    int             widest_map, widest_pov;
    uint64_t        total_bytes;
    char            status[32];
} m_demos_t;

static m_demos_t    m_demos;

static cvar_t       *ui_sortdemos;
static cvar_t       *ui_listalldemos;

static void BuildName(const file_info_t *info, char **cache)
{
    char buffer[MAX_OSPATH];
    char date[MAX_QPATH];
    demoInfo_t demo;
    demoEntry_t *e;
    struct tm *tm;
    size_t len;

    memset(&demo, 0, sizeof(demo));
    strcpy(demo.map, "???");
    strcpy(demo.pov, "???");

    if (cache) {
        char *s = *cache;
        char *p = strchr(s, '\\');
        if (p) {
            *p = 0;
            Q_strlcpy(demo.map, s, sizeof(demo.map));
            s = p + 1;
            p = strchr(s, '\\');
            if (p) {
                *p = 0;
                Q_strlcpy(demo.pov, s, sizeof(demo.pov));
                s = p + 1;
            }
        }
        *cache = s;
    } else {
        Q_concat(buffer, sizeof(buffer), m_demos.browse, "/", info->name);
        CL_GetDemoInfo(buffer, &demo);
        if (demo.mvd) {
            strcpy(demo.pov, DEMO_MVD_POV);
        }
    }

    // resize columns
    len = strlen(demo.map);
    if (len > 8) {
        len = 8;
    }
    if (len > m_demos.widest_map) {
        m_demos.widest_map = len;
    }

    len = strlen(demo.pov);
    if (len > m_demos.widest_pov) {
        m_demos.widest_pov = len;
    }

    // format date
    len = 0;
    if ((tm = localtime(&info->mtime)) != NULL) {
        if (tm->tm_year == m_demos.year) {
            len = strftime(date, sizeof(date), "%b %d %H:%M", tm);
        } else {
            len = strftime(date, sizeof(date), "%b %d  %Y", tm);
        }
    }
    if (!len) {
        strcpy(date, "???");
    }

    Com_FormatSize(buffer, sizeof(buffer), info->size);

    e = UI_FormatColumns(DEMO_EXTRASIZE,
                         info->name, date, buffer, demo.map, demo.pov, NULL);
    e->type = ENTRY_DEMO;
    e->size = info->size;
    e->mtime = info->mtime;

    m_demos.total_bytes += info->size;

    m_demos.list.items[m_demos.list.numItems++] = e;
}

static void BuildDir(const char *name, int type)
{
    demoEntry_t *e = UI_FormatColumns(DEMO_EXTRASIZE, name, "-", DEMO_DIR_SIZE, "-", "-", NULL);

    e->type = type;
    e->size = 0;
    e->mtime = 0;

    m_demos.list.items[m_demos.list.numItems++] = e;
}

static char *LoadCache(void **list)
{
    char buffer[MAX_OSPATH], *cache;
    int i, len;
    uint8_t hash[16];

    if (Q_concat(buffer, sizeof(buffer), m_demos.browse, "/" COM_DEMOCACHE_NAME) >= sizeof(buffer)) {
        return NULL;
    }
    len = FS_LoadFileEx(buffer, (void **)&cache, FS_TYPE_REAL | FS_PATH_GAME, TAG_FILESYSTEM);
    if (!cache) {
        return NULL;
    }
    if (len < 33) {
        goto fail;
    }

    for (i = 0; i < 16; i++) {
        int c1 = Q_charhex(cache[i * 2 + 0]);
        int c2 = Q_charhex(cache[i * 2 + 1]);
        hash[i] = (c1 << 4) | c2;
    }

    if (cache[32] != '\\') {
        goto fail;
    }

    if (memcmp(hash, m_demos.hash, 16)) {
        goto fail;
    }

    Com_DPrintf("%s: loading from cache\n", __func__);
    return cache;

fail:
    FS_FreeFile(cache);
    return NULL;
}

static void WriteCache(void)
{
    char buffer[MAX_OSPATH];
    qhandle_t f;
    int i;
    char *map, *pov;
    demoEntry_t *e;

    if (m_demos.list.numItems == m_demos.numDirs) {
        return;
    }
    if (Q_concat(buffer, sizeof(buffer), m_demos.browse, "/" COM_DEMOCACHE_NAME) >= sizeof(buffer)) {
        return;
    }
    FS_FOpenFile(buffer, &f, FS_MODE_WRITE);
    if (!f) {
        return;
    }

    for (i = 0; i < 16; i++) {
        FS_FPrintf(f, "%02x", m_demos.hash[i]);
    }
    FS_FPrintf(f, "\\");

    for (i = m_demos.numDirs; i < m_demos.list.numItems; i++) {
        e = m_demos.list.items[i];
        map = UI_GetColumn(e->name, COL_MAP);
        pov = UI_GetColumn(e->name, COL_POV);
        FS_FPrintf(f, "%s\\%s\\", map, pov);
    }
    FS_FCloseFile(f);
}

static void CalcHash(void **list)
{
    struct mdfour md;
    file_info_t *info;
    size_t len;

    mdfour_begin(&md);
    while (*list) {
        info = *list++;
        len = sizeof(*info) + strlen(info->name) - 1;
        mdfour_update(&md, (uint8_t *)info, len);
    }
    mdfour_result(&md, m_demos.hash);
}

static menuSound_t Change(menuCommon_t *self)
{
    demoEntry_t *e;

    if (!m_demos.list.numItems) {
        m_demos.menu.status = "No demos found";
        return QMS_BEEP;
    }

    e = m_demos.list.items[m_demos.list.curvalue];
    switch (e->type) {
    case ENTRY_DEMO:
        m_demos.menu.status = "Press Enter to play demo";
        break;
    default:
        m_demos.menu.status = "Press Enter to change directory";
        break;
    }

    return QMS_SILENT;
}

static void BuildList(void)
{
    int numDirs, numDemos;
    void **dirlist, **demolist;
    char *cache, *p;
    unsigned flags;
    size_t len;
    int i;

    // this can be a lengthy process
    S_StopAllSounds();

    m_demos.menu.status = "Building list...";
    SCR_UpdateScreen();

    // list files
    flags = ui_listalldemos->integer ? 0 : FS_TYPE_REAL | FS_PATH_GAME;
    dirlist = FS_ListFiles(m_demos.browse, NULL, flags |
                           FS_SEARCH_DIRSONLY, &numDirs);
    demolist = FS_ListFiles(m_demos.browse, DEMO_EXTENSIONS, flags |
                            FS_SEARCH_EXTRAINFO, &numDemos);
    numDemos = min(numDemos, MAX_LISTED_FILES - numDirs);

    // alloc entries
    m_demos.list.items = UI_Malloc(sizeof(demoEntry_t *) * (numDirs + numDemos + 1));
    m_demos.list.numItems = 0;
    m_demos.list.curvalue = 0;
    m_demos.list.prestep = 0;

    m_demos.widest_map = 3;
    m_demos.widest_pov = 3;
    m_demos.total_bytes = 0;

    // start with minimum size
    m_demos.menu.size(&m_demos.menu);

    if (strcmp(m_demos.browse, "/")) {
        BuildDir("..", ENTRY_UP);
    }

    // add directories
    if (dirlist) {
        for (i = 0; i < numDirs; i++) {
            BuildDir(dirlist[i], ENTRY_DN);
        }
        FS_FreeList(dirlist);
    }

    m_demos.numDirs = m_demos.list.numItems;

    // add demos
    if (demolist) {
        CalcHash(demolist);
        if ((cache = LoadCache(demolist)) != NULL) {
            p = cache + 32 + 1;
            for (i = 0; i < numDemos; i++) {
                BuildName(demolist[i], &p);
            }
            FS_FreeFile(cache);
        } else {
            for (i = 0; i < numDemos; i++) {
                BuildName(demolist[i], NULL);
                if ((i & 7) == 0) {
                    m_demos.menu.size(&m_demos.menu);
                    SCR_UpdateScreen();
                }
            }
        }
        WriteCache();
        FS_FreeList(demolist);
    }

    // update status line and sort
    Change(&m_demos.list.generic);
    if (m_demos.list.sortdir) {
        m_demos.list.sort(&m_demos.list);
    }

    // resize columns
    m_demos.menu.size(&m_demos.menu);

    // format our extra status line
    i = m_demos.list.numItems - m_demos.numDirs;
    len = Q_scnprintf(m_demos.status, sizeof(m_demos.status),
                      "%d demo%s, ", i, i == 1 ? "" : "s");
    Com_FormatSizeLong(m_demos.status + len, sizeof(m_demos.status) - len,
                       m_demos.total_bytes);

    SCR_UpdateScreen();
}

static void FreeList(void)
{
    int i;

    if (m_demos.list.items) {
        for (i = 0; i < m_demos.list.numItems; i++) {
            Z_Free(m_demos.list.items[i]);
        }
        Z_Free(m_demos.list.items);
        m_demos.list.items = NULL;
        m_demos.list.numItems = 0;
    }
}

static menuSound_t LeaveDirectory(void)
{
    char    *s;
    int     i;

    s = strrchr(m_demos.browse, '/');
    if (!s) {
        return QMS_BEEP;
    }

    if (s == m_demos.browse) {
        strcpy(m_demos.browse, "/");
    } else {
        *s = 0;
    }

    // rebuild list
    FreeList();
    BuildList();
    MenuList_Init(&m_demos.list);

    // move cursor to the previous directory
    for (i = 0; i < m_demos.numDirs; i++) {
        demoEntry_t *e = m_demos.list.items[i];
        if (!strcmp(e->name, s + 1)) {
            MenuList_SetValue(&m_demos.list, i);
            break;
        }
    }

    return QMS_OUT;
}

static bool FileNameOk(const char *s)
{
    while (*s) {
        if (*s == '\n' || *s == '"' || *s == ';') {
            return false;
        }
        s++;
    }
    return true;
}

static menuSound_t EnterDirectory(demoEntry_t *e)
{
    size_t  baselen, len;

    baselen = strlen(m_demos.browse);
    len = strlen(e->name);
    if (baselen + 1 + len >= sizeof(m_demos.browse)) {
        return QMS_BEEP;
    }
    if (!FileNameOk(e->name)) {
        return QMS_BEEP;
    }

    if (baselen == 0 || m_demos.browse[baselen - 1] != '/') {
        m_demos.browse[baselen++] = '/';
    }

    memcpy(m_demos.browse + baselen, e->name, len + 1);

    // rebuild list
    FreeList();
    BuildList();
    MenuList_Init(&m_demos.list);
    return QMS_IN;
}

static menuSound_t PlayDemo(demoEntry_t *e)
{
    if (strlen(m_demos.browse) + 1 + strlen(e->name) >= sizeof(m_demos.browse)) {
        return QMS_BEEP;
    }
    if (!FileNameOk(e->name)) {
        return QMS_BEEP;
    }
    Cbuf_AddText(&cmd_buffer, va("demo \"%s/%s\"\n", m_demos.browse, e->name));
    return QMS_SILENT;
}

static menuSound_t Activate(menuCommon_t *self)
{
    demoEntry_t *e;

    if (!m_demos.list.numItems) {
        return QMS_BEEP;
    }

    e = m_demos.list.items[m_demos.list.curvalue];
    switch (e->type) {
    case ENTRY_UP:
        return LeaveDirectory();
    case ENTRY_DN:
        return EnterDirectory(e);
    case ENTRY_DEMO:
        return PlayDemo(e);
    }

    return QMS_NOTHANDLED;
}

static int sizecmp(const void *p1, const void *p2)
{
    demoEntry_t *e1 = *(demoEntry_t **)p1;
    demoEntry_t *e2 = *(demoEntry_t **)p2;

    if (e1->size > e2->size) {
        return m_demos.list.sortdir;
    }
    if (e1->size < e2->size) {
        return -m_demos.list.sortdir;
    }
    return 0;
}

static int timecmp(const void *p1, const void *p2)
{
    demoEntry_t *e1 = *(demoEntry_t **)p1;
    demoEntry_t *e2 = *(demoEntry_t **)p2;

    if (e1->mtime > e2->mtime) {
        return m_demos.list.sortdir;
    }
    if (e1->mtime < e2->mtime) {
        return -m_demos.list.sortdir;
    }
    return 0;
}

static int namecmp(const void *p1, const void *p2)
{
    demoEntry_t *e1 = *(demoEntry_t **)p1;
    demoEntry_t *e2 = *(demoEntry_t **)p2;
    char *s1 = UI_GetColumn(e1->name, m_demos.list.sortcol);
    char *s2 = UI_GetColumn(e2->name, m_demos.list.sortcol);

    return Q_stricmp(s1, s2) * m_demos.list.sortdir;
}

static menuSound_t Sort(menuList_t *self)
{
    switch (m_demos.list.sortcol) {
    case COL_NAME:
    case COL_MAP:
    case COL_POV:
        MenuList_Sort(&m_demos.list, m_demos.numDirs, namecmp);
        break;
    case COL_DATE:
        MenuList_Sort(&m_demos.list, m_demos.numDirs, timecmp);
        break;
    case COL_SIZE:
        MenuList_Sort(&m_demos.list, m_demos.numDirs, sizecmp);
        break;
    }

    return QMS_SILENT;
}

static void Size(menuFrameWork_t *self)
{
    int w1, w2;

    m_demos.list.generic.x      = 0;
    m_demos.list.generic.y      = CHAR_HEIGHT;
    m_demos.list.generic.width  = 0;
    m_demos.list.generic.height = uis.height - CHAR_HEIGHT * 2 - 1;

    w1 = 17 + m_demos.widest_map + m_demos.widest_pov;
    w2 = uis.width - w1 * CHAR_WIDTH - MLIST_PADDING * 4 - MLIST_SCROLLBAR_WIDTH;
    if (w2 > 8 * CHAR_WIDTH) {
        // everything fits
        m_demos.list.columns[0].width = w2;
        m_demos.list.columns[1].width = 12 * CHAR_WIDTH + MLIST_PADDING;
        m_demos.list.columns[2].width = 5 * CHAR_WIDTH + MLIST_PADDING;
        m_demos.list.columns[3].width = m_demos.widest_map * CHAR_WIDTH + MLIST_PADDING;
        m_demos.list.columns[4].width = m_demos.widest_pov * CHAR_WIDTH + MLIST_PADDING;
        m_demos.list.numcolumns = COL_MAX;
    } else {
        // map and pov don't fit
        w2 = uis.width - 17 * CHAR_WIDTH - MLIST_PADDING * 2 - MLIST_SCROLLBAR_WIDTH;
        m_demos.list.columns[0].width = w2;
        m_demos.list.columns[1].width = 12 * CHAR_WIDTH + MLIST_PADDING;
        m_demos.list.columns[2].width = 5 * CHAR_WIDTH + MLIST_PADDING;
        m_demos.list.columns[3].width = 0;
        m_demos.list.columns[4].width = 0;
        m_demos.list.numcolumns = COL_MAX - 2;
    }
}

static menuSound_t Keydown(menuFrameWork_t *self, int key)
{
    if (key == K_BACKSPACE) {
        LeaveDirectory();
        return QMS_OUT;
    }

    return QMS_NOTHANDLED;
}

static void Draw(menuFrameWork_t *self)
{
    Menu_Draw(self);
    if (uis.width >= 640) {
        UI_DrawString(uis.width, uis.height - CHAR_HEIGHT,
                      UI_RIGHT, m_demos.status);
    }
}

static void Pop(menuFrameWork_t *self)
{
    // save previous position
    m_demos.selection = m_demos.list.curvalue;
    FreeList();
}

static void Expose(menuFrameWork_t *self)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    if (tm) {
        m_demos.year = tm->tm_year;
    }

    // check that target directory exists
    if (strcmp(m_demos.browse, "/")
        && ui_listalldemos->integer == 0
        && os_access(va("%s%s", fs_gamedir, m_demos.browse), F_OK)) {
        strcpy(m_demos.browse, "/");
    }

    BuildList();

    // move cursor to previous position
    MenuList_SetValue(&m_demos.list, m_demos.selection);
}

static void Free(menuFrameWork_t *self)
{
    Z_Free(m_demos.menu.items);
    memset(&m_demos, 0, sizeof(m_demos));
}

static void ui_sortdemos_changed(cvar_t *self)
{
    int i = Cvar_ClampInteger(self, -COL_MAX, COL_MAX);

    if (i > 0) {
        // ascending
        m_demos.list.sortdir = 1;
        m_demos.list.sortcol = i - 1;
    } else if (i < 0) {
        // descending
        m_demos.list.sortdir = -1;
        m_demos.list.sortcol = -i - 1;
    } else {
        // don't sort
        m_demos.list.sortdir = 0;
        m_demos.list.sortcol = 0;
    }

    if (m_demos.list.items && m_demos.list.sortdir) {
        m_demos.list.sort(&m_demos.list);
    }
}

void M_Menu_Demos(void)
{
    ui_sortdemos = Cvar_Get("ui_sortdemos", "1", 0);
    ui_sortdemos->changed = ui_sortdemos_changed;

    ui_listalldemos = Cvar_Get("ui_listalldemos", "0", 0);

    m_demos.menu.name = "demos";
    m_demos.menu.title = "Demo Browser";

    strcpy(m_demos.browse, "/demos");

    m_demos.menu.draw       = Draw;
    m_demos.menu.expose     = Expose;
    m_demos.menu.pop        = Pop;
    m_demos.menu.size       = Size;
    m_demos.menu.keydown    = Keydown;
    m_demos.menu.free       = Free;
    m_demos.menu.image      = uis.backgroundHandle;
    m_demos.menu.color.u32  = uis.color.background.u32;
    m_demos.menu.transparent    = uis.transparent;

    m_demos.list.generic.type   = MTYPE_LIST;
    m_demos.list.generic.flags  = QMF_HASFOCUS;
    m_demos.list.generic.activate = Activate;
    m_demos.list.generic.change = Change;
    m_demos.list.numcolumns     = COL_MAX;
    m_demos.list.sortdir        = 1;
    m_demos.list.sortcol        = COL_NAME;
    m_demos.list.extrasize      = DEMO_EXTRASIZE;
    m_demos.list.sort           = Sort;
    m_demos.list.mlFlags        = MLF_HEADER | MLF_SCROLLBAR;

    m_demos.list.columns[0].name    = m_demos.browse;
    m_demos.list.columns[0].uiFlags = UI_LEFT;
    m_demos.list.columns[1].name    = "Date";
    m_demos.list.columns[1].uiFlags = UI_CENTER;
    m_demos.list.columns[2].name    = "Size";
    m_demos.list.columns[2].uiFlags = UI_RIGHT;
    m_demos.list.columns[3].name    = "Map";
    m_demos.list.columns[3].uiFlags = UI_CENTER;
    m_demos.list.columns[4].name    = "POV";
    m_demos.list.columns[4].uiFlags = UI_CENTER;

    ui_sortdemos_changed(ui_sortdemos);

    Menu_AddItem(&m_demos.menu, &m_demos.list);

    List_Append(&ui_menus, &m_demos.menu.entry);
}
