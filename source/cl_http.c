/*
Copyright (C) 2008 r1ch.net

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

#include "cl_local.h"
#include <curl/curl.h>

static cvar_t  *cl_http_downloads;
static cvar_t  *cl_http_filelists;
static cvar_t  *cl_http_max_connections;
static cvar_t  *cl_http_proxy;
#ifdef _DEBUG
static cvar_t  *cl_http_debug;
#endif

#define FOR_EACH_DLQ(q) \
    LIST_FOR_EACH (dlqueue_t, q, &download_queue, entry)
#define FOR_EACH_DLQ_SAFE(q, n) \
    LIST_FOR_EACH_SAFE (dlqueue_t, q, n, &download_queue, entry)

// size limits for filelists
#define MAX_DLSIZE  0x100000    // 1 MiB
#define MIN_DLSIZE  0x020000    // 128 KiB

typedef enum {
    DL_OTHER,
    DL_LIST,
    DL_PAK
} dltype_t;

typedef enum {
    DL_PENDING,
    DL_RUNNING,
    DL_DONE
} dlstate_t;

typedef struct {
    list_t      entry;
    dltype_t    type;
    dlstate_t   state;
    char        path[1];
} dlqueue_t;

typedef struct {
    CURL        *curl;
    char        path[MAX_OSPATH];
    FILE        *file;
    dlqueue_t   *queue;
    size_t      size;
    size_t      position;
    double      speed;
    char        url[576];
    char        *buffer;
} dlhandle_t;

static list_t   download_queue;      //queue of paths we need
static dlhandle_t download_handles[4]; //actual download handles, don't raise this!

static char     download_server[512];    //base url prefix to download from
static char     download_referer[32];    //libcurl requires a static string :(

static qboolean curl_initialized;
static CURLM    *multi;
static int      handle_count;
static int      pending_count;
static qboolean abort_downloads;


/*
===============================
R1Q2 HTTP Downloading Functions
===============================
HTTP downloading is used if the server provides a content
server url in the connect message. Any missing content the
client needs will then use the HTTP server instead of auto
downloading via UDP. CURL is used to enable multiple files
to be downloaded in parallel to improve performance on high
latency links when small files such as textures are needed.
Since CURL natively supports gzip content encoding, any files
on the HTTP server should ideally be gzipped to conserve
bandwidth.
*/

// libcurl callback to update progress info. Mainly just used as
// a way to cancel the transfer if required.
static int progress_func (void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    dlhandle_t *dl = (dlhandle_t *)clientp;

    dl->position = (unsigned)dlnow;

    //don't care which download shows as long as something does :)
    if (!abort_downloads) {
        strcpy (cls.download.name, dl->queue->path);
        //cls.download.position = dl->position;

        if (dltotal)
            cls.download.percent = (int)((dlnow / dltotal) * 100.0f);
        else
            cls.download.percent = 0;
    }

    return abort_downloads;
}

// libcurl callback for filelists.
static size_t recv_func (void *ptr, size_t size, size_t nmemb, void *stream) {
    size_t new_size, bytes = size * nmemb;
    dlhandle_t *dl = (dlhandle_t *)stream;

    if (!dl->size) {
        new_size = bytes + 1;
        if (new_size > MAX_DLSIZE)
            goto oversize;
        if (new_size < MIN_DLSIZE)
            new_size = MIN_DLSIZE;
        dl->size = new_size;
        dl->buffer = Z_Malloc (dl->size);
    } else if (dl->position + bytes >= dl->size) {
        char *tmp = dl->buffer;

        new_size = dl->size * 2;
        if (new_size > MAX_DLSIZE)
            new_size = MAX_DLSIZE;
        if (dl->position + bytes >= new_size)
            goto oversize;
        dl->buffer = Z_Malloc (new_size);
        memcpy (dl->buffer, tmp, dl->size);
        Z_Free (tmp);
        dl->size = new_size;
    }

    memcpy (dl->buffer + dl->position, ptr, bytes);
    dl->position += bytes;
    dl->buffer[dl->position] = 0;

    return bytes;

oversize:
    Com_DPrintf ("[HTTP] Oversize file while trying to download '%s'\n", dl->url);
    return 0;
}

// libcurl callback to update header info.
static size_t header_func (void *ptr, size_t size, size_t nmemb, void *stream) {
    size_t len, bytes = size * nmemb;
    dlhandle_t *dl = (dlhandle_t *)stream;
    char buffer[64];

    if (dl->size)
        return bytes;

    if (bytes <= 16)
        return bytes;

    if (bytes > sizeof(buffer)-1)
        bytes = sizeof(buffer)-1;

    memcpy (buffer, ptr, bytes);
    buffer[bytes] = 0;

    if (!Q_strncasecmp (buffer, "Content-Length: ", 16)) {
        //allocate buffer based on what the server claims content-length is. +1 for nul
        len = strtoul (buffer + 16, NULL, 10);
        if (len >= MAX_DLSIZE) {
            Com_DPrintf ("[HTTP] Oversize file while trying to download '%s'\n", dl->url);
            return 0;
        }
        dl->size = len + 1;
        dl->buffer = Z_Malloc (dl->size);
    }

    return bytes;
}

#ifdef _DEBUG
static int debug_func (CURL *c, curl_infotype type, char *data, size_t size, void * ptr) {
    char buffer[MAXPRINTMSG];

    if (type == CURLINFO_TEXT) {
        if (size > sizeof(buffer)-1)
            size = sizeof(buffer)-1;
        memcpy (buffer, data, size);
        buffer[size] = 0;
        Com_LPrintf (PRINT_DEVELOPER, "[HTTP] %s\n", buffer);
    }

    return 0;
}
#endif

// Properly escapes a path with HTTP %encoding. libcurl's function
// seems to treat '/' and such as illegal chars and encodes almost
// the entire url...
static void escape_path (const char *path, char *escaped) {
    static const char allowed[] = ";/?:@&=+$,[]-_.!~*'()";
    int     c;
    size_t  len;
    char    *p;

    p = escaped;
    while(*path) {
        c = *path++;
        if (!Q_isalnum (c) && !strchr(allowed, c)) {
            sprintf (p, "%%%02x", c);
            p += 3;
        } else {
            *p++ = c;
        }
    }
    *p = 0;

    //using ./ in a url is legal, but all browsers condense the path and some IDS / request
    //filtering systems act a bit funky if http requests come in with uncondensed paths.
    len = strlen(escaped);
    p = escaped;
    while ((p = strstr (p, "./"))) {
        memmove (p, p+2, len - (p - escaped) - 1);
        len -= 2;
    }
}

// Actually starts a download by adding it to the curl multi handle.
static void start_download (dlqueue_t *entry, dlhandle_t *dl) {
    size_t  len;
    char    temp[MAX_QPATH];
    char    escaped[MAX_QPATH*4];
    CURLMcode ret;
 
    //yet another hack to accomodate filelists, how i wish i could push :(
    //NULL file handle indicates filelist.
    if (entry->type == DL_LIST) {
        dl->file = NULL;
        dl->path[0] = 0;
        escape_path (entry->path, escaped);
    } else {
        len = Q_snprintf (dl->path, sizeof(dl->path), "%s/%s.tmp", fs_gamedir, entry->path);
        if (len >= sizeof(dl->path)) {
            Com_EPrintf ("[HTTP] Refusing oversize temporary file path.\n");
            goto fail;
        }

        // FIXME: should use baseq2 instead of empty gamedir?
        len = Q_snprintf (temp, sizeof(temp), "%s/%s", fs_game->string, entry->path);
        if (len >= sizeof(temp)) {
            Com_EPrintf ("[HTTP] Refusing oversize server file path.\n");
            goto fail;
        }
        escape_path (temp, escaped);

        FS_CreatePath (dl->path);

        //don't bother with http resume... too annoying if server doesn't support it.
        dl->file = fopen (dl->path, "wb");
        if (!dl->file) {
            Com_EPrintf ("[HTTP] Couldn't open '%s' for writing.\n", dl->path);
            goto fail;
        }
    }

    len = Q_snprintf (dl->url, sizeof(dl->url), "%s%s", download_server, escaped);
    if (len >= sizeof(dl->url)) {
        Com_EPrintf ("[HTTP] Refusing oversize download URL.\n");
        goto fail;
    }

    dl->buffer = NULL;
    dl->speed = 0;
    dl->size = 0;
    dl->position = 0;
    dl->queue = entry;
    if (!dl->curl)
        dl->curl = curl_easy_init ();

    curl_easy_setopt (dl->curl, CURLOPT_ENCODING, "");
#ifdef _DEBUG
    if (cl_http_debug->integer) {
        curl_easy_setopt (dl->curl, CURLOPT_DEBUGFUNCTION, debug_func);
        curl_easy_setopt (dl->curl, CURLOPT_VERBOSE, 1);
    }
#endif
    curl_easy_setopt (dl->curl, CURLOPT_NOPROGRESS, 0);
    if (dl->file) {
        curl_easy_setopt (dl->curl, CURLOPT_WRITEDATA, dl->file);
        curl_easy_setopt (dl->curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt (dl->curl, CURLOPT_WRITEHEADER, NULL);
        curl_easy_setopt (dl->curl, CURLOPT_HEADERFUNCTION, NULL);
    } else {
        curl_easy_setopt (dl->curl, CURLOPT_WRITEDATA, dl);
        curl_easy_setopt (dl->curl, CURLOPT_WRITEFUNCTION, recv_func);
        curl_easy_setopt (dl->curl, CURLOPT_WRITEHEADER, dl);
        curl_easy_setopt (dl->curl, CURLOPT_HEADERFUNCTION, header_func);
    }
    curl_easy_setopt (dl->curl, CURLOPT_PROXY, cl_http_proxy->string);
    curl_easy_setopt (dl->curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt (dl->curl, CURLOPT_MAXREDIRS, 5);
    curl_easy_setopt (dl->curl, CURLOPT_PROGRESSFUNCTION, progress_func);
    curl_easy_setopt (dl->curl, CURLOPT_PROGRESSDATA, dl);
    curl_easy_setopt (dl->curl, CURLOPT_USERAGENT, com_version->string);
    curl_easy_setopt (dl->curl, CURLOPT_REFERER, download_referer);
    curl_easy_setopt (dl->curl, CURLOPT_URL, dl->url);

    ret = curl_multi_add_handle (multi, dl->curl);
    if (ret != CURLM_OK) {
        Com_EPrintf ("[HTTP] Failed to add download handle: %s\n",
            curl_multi_strerror (ret));
fail:
        entry->state = DL_DONE;
        return;
    }

    handle_count++;
    Com_DPrintf ("[HTTP] Fetching %s...\n", dl->url);
    entry->state = DL_RUNNING;
}

// Quake 2 is exiting or we're changing servers. Clean up.
static void cleanup_downloads (void) {
    dlqueue_t   *q, *n;
    dlhandle_t  *dl;
    int         i;

    FOR_EACH_DLQ_SAFE (q, n) {
        Z_Free (q);
    }
 
    List_Init (&download_queue);

    download_server[0] = 0;
    download_referer[0] = 0;
    abort_downloads = qfalse;
    handle_count = pending_count = 0;

    for (i = 0; i < 4; i++) {
        dl = &download_handles[i];

        if (dl->file) {
            fclose (dl->file);
            remove (dl->path);
            dl->file = NULL;
        }

        if (dl->buffer) {
            Z_Free (dl->buffer);
            dl->buffer = NULL;
        }

        if (dl->curl) {
            if (multi)
                curl_multi_remove_handle (multi, dl->curl);
            curl_easy_cleanup (dl->curl);
            dl->curl = NULL;
        }

        dl->queue = NULL;
    }

    if (multi) {
        curl_multi_cleanup (multi);
        multi = NULL;
    }
}


/*
===============
HTTP_Init

Init libcurl and multi handle.
===============
*/
void HTTP_Init (void) {
    cl_http_downloads = Cvar_Get ("cl_http_downloads", "1", 0);
    cl_http_filelists = Cvar_Get ("cl_http_filelists", "1", 0);
    cl_http_max_connections = Cvar_Get ("cl_http_max_connections", "2", 0);
    //cl_http_max_connections->changed = _cl_http_max_connections_changed;
    cl_http_proxy = Cvar_Get ("cl_http_proxy", "", 0);
#ifdef _DEBUG
    cl_http_debug = Cvar_Get ("cl_http_debug", "0", 0);
#endif

    curl_global_init (CURL_GLOBAL_NOTHING);
    List_Init( &download_queue );
    curl_initialized = qtrue;
    Com_DPrintf ("%s initialized.\n", curl_version());
}

void HTTP_Shutdown (void) {
    if (!curl_initialized)
        return;

    cleanup_downloads();

    curl_global_cleanup ();
    curl_initialized = qfalse;
}


/*
===============
HTTP_SetServer

A new server is specified, so we nuke all our state.
===============
*/
void HTTP_SetServer (const char *url) {
    cleanup_downloads ();

    if (!*url)
        return;
    if (strncmp (url, "http://", 7)) {
        Com_Printf ("[HTTP] Ignoring download server URL with non-HTTP schema.\n");
        return;
    }

    multi = curl_multi_init ();

    Q_strlcpy (download_server, url, sizeof(download_server));
    Q_snprintf (download_referer, sizeof(download_referer),
        "quake2://%s", NET_AdrToString (&cls.serverAddress));

    Com_Printf ("[HTTP] Download server at %s\n", download_server);
}

/*
===============
HTTP_CancelDownloads

Cancel all downloads and nuke the queue.
===============
*/
void HTTP_CancelDownloads (void) {
    dlqueue_t   *q;

    if (!download_server[0])
        return;

    CL_ResetPrecacheCheck ();
    abort_downloads = qtrue;

    FOR_EACH_DLQ (q) {
        if (q->state == DL_PENDING)
            q->state = DL_DONE;
    }

    if (!pending_count && !handle_count)
        download_server[0] = 0;

    pending_count = 0;
}

static void queue_download(const char *path, dltype_t type) {
    dlqueue_t *q;
    size_t len;

    FOR_EACH_DLQ (q) {
        //avoid sending duplicate requests
        if (!FS_pathcmp (path, q->path))
            return;
    }

    len = strlen (path);
    if (len >= MAX_QPATH) {
        Com_EPrintf ("[HTTP] Refusing to queue oversize quake path.\n");
        return;
    }

    q = Z_Malloc (sizeof(*q) + len);
    memcpy (q->path, path, len + 1);
    q->type = type;
    q->state = DL_PENDING;

    //paks get bumped to the top and HTTP switches to single downloading.
    //this prevents someone on 28k dialup trying to do both the main .pak
    //and referenced configstrings data at once.
    if (type == DL_PAK)
        List_Insert (&download_queue, &q->entry);
    else
        List_Append (&download_queue, &q->entry);

    //if a download entry has made it this far, finish_download is guaranteed to be called.
    Com_DPrintf ("[HTTP] Queued %s...\n", path);
    pending_count++;
}

/*
===============
HTTP_QueueDownload

Called from the precache check to queue a download. Return value of
false will cause standard UDP downloading to be used instead.
===============
*/
qboolean HTTP_QueueDownload (const char *path) {
    size_t      len;
    qboolean    need_list;
    char        temp[MAX_QPATH];

    // no http server (or we got booted)
    if (!download_server[0] || abort_downloads || !cl_http_downloads->integer)
        return qfalse;

    // first download queued, so we want the mod filelist
    need_list = LIST_EMPTY (&download_queue);

    queue_download (path, DL_OTHER);

    if (!cl_http_filelists->integer)
        return qtrue;

    if (need_list) {
        //grab the filelist
        len = Q_snprintf (temp, sizeof(temp), "%s.filelist", fs_game->string);
        if (len < sizeof(temp))
            queue_download (temp, DL_LIST);

        //this is a nasty hack to let the server know what we're doing so admins don't
        //get confused by a ton of people stuck in CNCT state. it's assumed the server
        //is running r1q2 if we're even able to do http downloading so hopefully this
        //won't spew an error msg.
        CL_ClientCommand("download http\n");
    }

    //special case for map file lists, i really wanted a server-push mechanism for this, but oh well
    len = strlen (path);
    if (len > 4 && !Q_stricmp (path + len - 4, ".bsp")) {
        len = Q_snprintf (temp, sizeof(temp), "%s/%s", fs_game->string, path);
        if (len + 5 < sizeof(temp)) {
            memcpy (temp + len - 4, ".filelist", 10);
            queue_download (temp, DL_LIST);
        }
    }

    return qtrue;
}

/*
===============
HTTP_DownloadsPending

See if we're still busy with some downloads. Called by precacher just
before it loads the map since we could be downloading the map. If we're
busy still, it'll wait and finish_download will pick up from where
it left.
===============
*/
qboolean HTTP_DownloadsPending (void) {
    if (!download_server[0])
        return qfalse;

    return pending_count || handle_count;
}

static qboolean check_extension (const char *ext) {
    static const char allowed[][4] = {
        "pcx", "wal", "wav", "md2", "sp2", "tga", "png",
        "jpg", "bsp", "ent", "txt", "dm2", "loc", ""
    };
    int i;

    for (i = 0; allowed[i][0]; i++)
        if (!strcmp (ext, allowed[i]))
            return qtrue;
    return qfalse;
}

// Validate a path supplied by a filelist.
static void check_and_queue_download (char *path) {
    size_t      len;
    char        *ext;
    dltype_t    type;
    int         flags;

    len = strlen(path);
    if (len >= MAX_QPATH)
        return;

    ext = strrchr (path, '.');
    if (!ext)
        return;

    ext++;
    if (!ext[0])
        return;

    Q_strlwr (ext);

    if (!strcmp (ext, "pak")) {
        Com_Printf ("[HTTP] Filelist is requesting a .pak file '%s'\n", path);
        type = DL_PAK;
    } else {
        type = DL_OTHER;
        if (!check_extension(ext)) {
            Com_WPrintf ("[HTTP] Illegal file type '%s' in filelist.\n", path);
            return;
        }
    }

    if (path[0] == '@'){
        if (type == DL_PAK) {
            Com_WPrintf ("[HTTP] '@' prefix used on a pak file '%s' in filelist.\n", path);
            return;
        }
        flags = FS_PATH_GAME;
        path++;
        len--;
    } else if (type == DL_PAK) {
        //by definition paks are game-local
        flags = FS_PATH_GAME|FS_TYPE_REAL;
    } else {
        flags = 0;
    }

    if (strstr (path, "..") ||
        !Q_ispath (path[0]) ||
        !Q_ispath (path[len-1]) ||
        strstr(path, "//") ||
        strchr (path, '\\') ||
        strchr (path, ':') ||
        (type == DL_OTHER && !strchr (path, '/')) ||
        (type == DL_PAK && strchr(path, '/')))
    {
        Com_WPrintf ("[HTTP] Illegal path '%s' in filelist.\n", path);
        return;
    }

    if (FS_LoadFileEx (path, NULL, flags, TAG_FREE) == INVALID_LENGTH) {
        queue_download (path, type);
    }
}

// A filelist is in memory, scan and validate it and queue up the files.
static void parse_file_list (dlhandle_t *dl) {
    char    *list;
    char    *p;

    if (cl_http_filelists->integer && !abort_downloads) {
        list = dl->buffer;
        while (1) {
            p = strchr (list, '\n');
            if (p) {
                if (p > list && *(p - 1) == '\r')
                    *(p - 1) = 0;
                *p = 0;
                if (*list)
                    check_and_queue_download (list);
                list = p + 1;
            } else {
                if (*list)
                    check_and_queue_download (list);
                break;
            }
        }
    }

    Z_Free (dl->buffer);
    dl->buffer = NULL;
}

// A pak file just downloaded, let's see if we can remove some stuff from
// the queue which is in the .pak.
static void rescan_queue (void) {
    dlqueue_t   *q;

    pending_count = 0;
    
    if (abort_downloads)
        return;

    FOR_EACH_DLQ (q) {
        if (q->state == DL_PENDING) {
            if (q->type == DL_OTHER && FS_LoadFile (q->path, NULL) != INVALID_LENGTH)
                q->state = DL_DONE;
            else
                pending_count++;
        }
    }
}

// curl doesn't provide reverse-lookup of the void * ptr, so search for it
static dlhandle_t *find_handle (CURL *curl) {
    size_t      i;
    dlhandle_t  *dl;

    for (i = 0; i < 4; i++) {
        dl = &download_handles[i];
        if (dl->curl == curl) {
            return dl;
        }
    }

    Com_Error (ERR_FATAL, "CURL handle not found for CURLMSG_DONE");
}

// A download finished, find out what it was, whether there were any errors and
// if so, how severe. If none, rename file and other such stuff.
static void finish_download (void) {
    int         msgs_in_queue;
    CURLMsg     *msg;
    CURLcode    result;
    dlhandle_t  *dl;
    CURL        *curl;
    long        response;
    double      time;
    double      size;
    char        temp[MAX_OSPATH];

    do {
        msg = curl_multi_info_read (multi, &msgs_in_queue);
        if (!msg)
            return;

        if (msg->msg != CURLMSG_DONE)
            continue;

        curl = msg->easy_handle;
        dl = find_handle (curl);

        //we mark everything as done even if it errored to prevent multiple
        //attempts.
        dl->queue->state = DL_DONE;

        //filelist processing is done on read
        if (dl->file) {
            fclose (dl->file);
            dl->file = NULL;
        }

        //might be aborted
        if (pending_count)
            pending_count--;
        handle_count--;
        cls.download.name[0] = 0;
        //cls.download.position = 0;
        cls.download.percent = 0;

        result = msg->data.result;

        switch (result) {
        //for some reason curl returns CURLE_OK for a 404...
        case CURLE_HTTP_RETURNED_ERROR:
        case CURLE_OK:
            curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &response);
            if (response == 404) {
                Com_Printf ("[HTTP] %s: 404 Not Found [%d remaining file%s]\n",
                    dl->queue->path, pending_count, pending_count == 1 ? "" : "s");
                if (dl->path[0]) {
                    remove (dl->path);
                    dl->path[0] = 0;
                }
                curl_easy_getinfo (curl, CURLINFO_SIZE_DOWNLOAD, &size);
                if (size > 512) {
                    //ick
                    Com_EPrintf ("[HTTP] Oversized 404 body received (%d bytes).\n", (int)size);
                    goto fatal2;
                }
                curl_multi_remove_handle (multi, curl);
                continue;
            } else if (response == 200) {
                break;
            } else {
                //every other code is treated as fatal
                Com_EPrintf ("[HTTP] Unexpected response code received (%d).\n", (int)response);
                goto fatal1;
            }
            break;

        //fatal error, disable http
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_PROXY:
            Com_EPrintf ("[HTTP] Fatal error: %s.\n", curl_easy_strerror (result));
fatal1:
            if (dl->path[0]) {
                remove (dl->path);
                dl->path[0] = 0;
            }
fatal2:
            curl_multi_remove_handle (multi, curl);
            if (!abort_downloads)
                HTTP_CancelDownloads ();
            continue;
        default:
            if (dl->path[0]) {
                remove (dl->path);
                dl->path[0] = 0;
            }
            if (abort_downloads && result == CURLE_ABORTED_BY_CALLBACK) {
                Com_DPrintf ("[HTTP] Download '%s' aborted by callback\n", dl->queue->path);
            } else {
                Com_WPrintf ("[HTTP] %s: %s [%d remaining file%s]\n",
                    dl->queue->path, curl_easy_strerror (result),
                    pending_count, pending_count == 1 ? "" : "s");
            }
            curl_multi_remove_handle (multi, curl);
            continue;
        }

        //show some stats
        curl_easy_getinfo (curl, CURLINFO_TOTAL_TIME, &time);
        curl_easy_getinfo (curl, CURLINFO_SIZE_DOWNLOAD, &size);

        //FIXME:
        //technically i shouldn't need to do this as curl will auto reuse the
        //existing handle when you change the url. however, the handle_count goes
        //all weird when reusing a download slot in this way. if you can figure
        //out why, please let me know.
        curl_multi_remove_handle (multi, curl);

        Com_Printf ("[HTTP] %s: %.f bytes, %.2fkB/sec [%d remaining file%s]\n",
            dl->queue->path, size, (size / 1024.0) / time, pending_count, 
            pending_count == 1 ? "" : "s");

        if (dl->path[0]) {
            //rename the temp file
            Q_snprintf (temp, sizeof(temp), "%s/%s", fs_gamedir, dl->queue->path);

            if (rename (dl->path, temp))
                Com_EPrintf ("[HTTP] Failed to rename '%s' to '%s'\n",
                    dl->path, dl->queue->path);
            dl->path[0] = 0;

            //a pak file is very special...
            if (dl->queue->type == DL_PAK) {
                CL_RestartFilesystem (qfalse);
                rescan_queue ();
            }
        } else {
            parse_file_list (dl);
        }
    } while (msgs_in_queue > 0);

    if (!handle_count && abort_downloads)
        download_server[0] = 0;

    // done current batch, see if we have more to dl - maybe a .bsp needs downloaded
    if ((cls.state == ca_connected || cls.state == ca_loading) && !HTTP_DownloadsPending())
        CL_RequestNextDownload ();
}

// Find a free download handle to start another queue entry on.
static dlhandle_t *get_free_handle (void) {
    dlhandle_t  *dl;
    int         i;

    for (i = 0; i < 4; i++) {
        dl = &download_handles[i];
        if (!dl->queue || dl->queue->state == DL_DONE)
            return dl;
    }

    return NULL;
}

// Start another HTTP download if possible.
static void start_next_download (void) {
    dlqueue_t   *q;

    if (!pending_count || abort_downloads || handle_count >= cl_http_max_connections->integer) {
        return;
    }

    //not enough downloads running, queue some more!
    FOR_EACH_DLQ (q) {
        if (q->state == DL_RUNNING) {
            if (q->type == DL_PAK)
                break; // hack for pak file single downloading
        } else if (q->state == DL_PENDING) {
            dlhandle_t *dl = get_free_handle();
            if (dl)
                start_download (q, dl);
            break;
        }
    }
}

/*
===============
HTTP_RunDownloads

This calls curl_multi_perform do actually do stuff. Called every frame while
connecting to minimise latency. Also starts new downloads if we're not doing
the maximum already.
===============
*/
void HTTP_RunDownloads (void) {
    int         new_count;
    CURLMcode   ret;

    if (!download_server[0])
        return;

    start_next_download ();

    do {
        ret = curl_multi_perform (multi, &new_count);
        if (new_count < handle_count) {
            //hmm, something either finished or errored out.
            finish_download ();
            handle_count = new_count;
        }
    } while (ret == CURLM_CALL_MULTI_PERFORM);

    if (ret != CURLM_OK) {
        Com_EPrintf ("[HTTP] Error running downloads: %s.\n",
            curl_multi_strerror(ret));
        HTTP_CancelDownloads ();
    }

    start_next_download ();
}

