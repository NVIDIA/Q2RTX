/*
Copyright (C) 2008 r1ch.net

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
#include <curl/curl.h>

static cvar_t  *cl_http_downloads;
static cvar_t  *cl_http_filelists;
static cvar_t  *cl_http_max_connections;
static cvar_t  *cl_http_proxy;
static cvar_t  *cl_http_default_url;

#ifdef _DEBUG
static cvar_t  *cl_http_debug;
#endif

#if USE_UI
static cvar_t  *cl_http_blocking_timeout;
#endif

// size limits for filelists, must be power of two
#define MAX_DLSIZE  0x100000    // 1 MiB
#define MIN_DLSIZE  0x8000      // 32 KiB

typedef struct {
    CURL        *curl;
    char        path[MAX_OSPATH];
    FILE        *file;
    dlqueue_t   *queue;
    size_t      size;
    size_t      position;
    char        url[576];
    char        *buffer;
    qboolean    multi_added;    //to prevent multiple removes
} dlhandle_t;

static dlhandle_t   download_handles[4]; //actual download handles, don't raise this!
static char     download_server[512];    //base url prefix to download from
static char     download_referer[32];    //libcurl requires a static string :(
static qboolean download_default_repo;

static qboolean curl_initialized;
static CURLM    *curl_multi;
static int      curl_handles;


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

// libcurl callback to update progress info.
static int progress_func(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
    dlhandle_t *dl = (dlhandle_t *)clientp;

    //don't care which download shows as long as something does :)
    cls.download.current = dl->queue;

    if (dltotal)
        cls.download.percent = (int)((dlnow / dltotal) * 100.0);
    else
        cls.download.percent = 0;

    cls.download.position = (int)dlnow;

    return 0;
}

// libcurl callback for filelists.
static size_t recv_func(void *ptr, size_t size, size_t nmemb, void *stream)
{
    dlhandle_t *dl = (dlhandle_t *)stream;
    size_t new_size, bytes;

    if (!nmemb)
        return 0;

    if (size > SIZE_MAX / nmemb)
        goto oversize;

    if (dl->position > MAX_DLSIZE)
        goto oversize;

    bytes = size * nmemb;
    if (bytes >= MAX_DLSIZE - dl->position)
        goto oversize;

    // grow buffer in MIN_DLSIZE chunks. +1 for NUL.
    new_size = ALIGN(dl->position + bytes + 1, MIN_DLSIZE);
    if (new_size > dl->size) {
        dl->size = new_size;
        dl->buffer = Z_Realloc(dl->buffer, new_size);
    }

    memcpy(dl->buffer + dl->position, ptr, bytes);
    dl->position += bytes;
    dl->buffer[dl->position] = 0;

    return bytes;

oversize:
    Com_DPrintf("[HTTP] Oversize file while trying to download '%s'\n", dl->url);
    return 0;
}

#ifdef _DEBUG
static int debug_func(CURL *c, curl_infotype type, char *data, size_t size, void *ptr)
{
    char buffer[MAXPRINTMSG];

    if (type == CURLINFO_TEXT) {
        if (size > sizeof(buffer) - 1)
            size = sizeof(buffer) - 1;
        memcpy(buffer, data, size);
        buffer[size] = 0;
        Com_LPrintf(PRINT_DEVELOPER, "[HTTP] %s\n", buffer);
    }

    return 0;
}
#endif

// Properly escapes a path with HTTP %encoding. libcurl's function
// seems to treat '/' and such as illegal chars and encodes almost
// the entire url...
static void escape_path(const char *path, char *escaped)
{
    static const char allowed[] = ";/?:@&=+$,[]-_.!~*'()";
    int     c;
    char    *p;

    p = escaped;
    while (*path) {
        c = *path++;
        if (!Q_isalnum(c) && !strchr(allowed, c)) {
            sprintf(p, "%%%02x", c);
            p += 3;
        } else {
            *p++ = c;
        }
    }
    *p = 0;
}

// curl doesn't provide a way to convert HTTP response code to string...
static const char *http_strerror(int response)
{
    static char buffer[32];

    //common codes
    switch (response) {
    case 200:
        return "200 OK";
    case 401:
        return "401 Unauthorized";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 500:
        return "500 Internal Server Error";
    case 503:
        return "503 Service Unavailable";
    }

    if (response < 100 || response >= 600) {
        Q_snprintf(buffer, sizeof(buffer), "%d <bad code>", response);
        return buffer;
    }

    //generic classes
    if (response < 200) {
        Q_snprintf(buffer, sizeof(buffer), "%d Informational", response);
    } else if (response < 300) {
        Q_snprintf(buffer, sizeof(buffer), "%d Success", response);
    } else if (response < 400) {
        Q_snprintf(buffer, sizeof(buffer), "%d Redirection", response);
    } else if (response < 500) {
        Q_snprintf(buffer, sizeof(buffer), "%d Client Error", response);
    } else {
        Q_snprintf(buffer, sizeof(buffer), "%d Server Error", response);
    }

    return buffer;
}

// Use "baseq2" instead of empty gamedir consistently for all kinds of downloads.
static const char *http_gamedir(void)
{
    if (*fs_game->string)
        return fs_game->string;

    return BASEGAME;
}

// Actually starts a download by adding it to the curl multi handle.
static void start_download(dlqueue_t *entry, dlhandle_t *dl)
{
    size_t  len;
    char    temp[MAX_QPATH];
    char    escaped[MAX_QPATH * 4];
    CURLMcode ret;
    qerror_t err;

    //yet another hack to accomodate filelists, how i wish i could push :(
    //NULL file handle indicates filelist.
    if (entry->type == DL_LIST) {
        dl->file = NULL;
        dl->path[0] = 0;
        //filelist paths are absolute
        escape_path(entry->path, escaped);
    } else {
        len = Q_snprintf(dl->path, sizeof(dl->path), "%s/%s.tmp", fs_gamedir, entry->path);
        if (len >= sizeof(dl->path)) {
            Com_EPrintf("[HTTP] Refusing oversize temporary file path.\n");
            goto fail;
        }

        //prepend quake path with gamedir
        len = Q_snprintf(temp, sizeof(temp), "%s/%s", http_gamedir(), entry->path);
        if (len >= sizeof(temp)) {
            Com_EPrintf("[HTTP] Refusing oversize server file path.\n");
            goto fail;
        }
        escape_path(temp, escaped);

        err = FS_CreatePath(dl->path);
        if (err < 0) {
            Com_EPrintf("[HTTP] Couldn't create path to '%s': %s\n", dl->path, Q_ErrorString(err));
            goto fail;
        }

        //don't bother with http resume... too annoying if server doesn't support it.
        dl->file = fopen(dl->path, "wb");
        if (!dl->file) {
            Com_EPrintf("[HTTP] Couldn't open '%s' for writing: %s\n", dl->path, strerror(errno));
            goto fail;
        }
    }

    len = Q_snprintf(dl->url, sizeof(dl->url), "%s%s", download_server, escaped);
    if (len >= sizeof(dl->url)) {
        Com_EPrintf("[HTTP] Refusing oversize download URL.\n");
        goto fail;
    }

    dl->buffer = NULL;
    dl->size = 0;
    dl->position = 0;
    dl->queue = entry;
    if (!dl->curl)
        dl->curl = curl_easy_init();

    curl_easy_setopt(dl->curl, CURLOPT_ENCODING, "");
#ifdef _DEBUG
    if (cl_http_debug->integer) {
        curl_easy_setopt(dl->curl, CURLOPT_DEBUGFUNCTION, debug_func);
        curl_easy_setopt(dl->curl, CURLOPT_VERBOSE, 1);
    }
#endif
    curl_easy_setopt(dl->curl, CURLOPT_NOPROGRESS, 0);
    if (dl->file) {
        curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, dl->file);
        curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, NULL);
    } else {
        curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, dl);
        curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, recv_func);
    }
    curl_easy_setopt(dl->curl, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt(dl->curl, CURLOPT_PROXY, cl_http_proxy->string);
    curl_easy_setopt(dl->curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(dl->curl, CURLOPT_MAXREDIRS, 5);
    curl_easy_setopt(dl->curl, CURLOPT_PROGRESSFUNCTION, progress_func);
    curl_easy_setopt(dl->curl, CURLOPT_PROGRESSDATA, dl);
    curl_easy_setopt(dl->curl, CURLOPT_USERAGENT, com_version->string);
    curl_easy_setopt(dl->curl, CURLOPT_REFERER, download_referer);
    curl_easy_setopt(dl->curl, CURLOPT_URL, dl->url);

    ret = curl_multi_add_handle(curl_multi, dl->curl);
    if (ret != CURLM_OK) {
        Com_EPrintf("[HTTP] Failed to add download handle: %s\n",
                    curl_multi_strerror(ret));
fail:
        CL_FinishDownload(entry);

        // see if we have more to dl
        CL_RequestNextDownload();
        return;
    }

    Com_DPrintf("[HTTP] Fetching %s...\n", dl->url);
    entry->state = DL_RUNNING;
    dl->multi_added = qtrue;
    curl_handles++;
}

#if USE_UI

/*
===============
HTTP_FetchFile

Fetches data from an arbitrary URL in a blocking fashion. Doesn't touch any
global variables and thus doesn't interfere with existing client downloads.
===============
*/
ssize_t HTTP_FetchFile(const char *url, void **data) {
    dlhandle_t tmp;
    CURL *curl;
    CURLcode ret;
    long response;

    *data = NULL;

    curl = curl_easy_init();
    if (!curl)
        return -1;

    memset(&tmp, 0, sizeof(tmp));

    curl_easy_setopt(curl, CURLOPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &tmp);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, recv_func);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt(curl, CURLOPT_PROXY, cl_http_proxy->string);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, com_version->string);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, cl_http_blocking_timeout->integer);

    ret = curl_easy_perform(curl);

    if (ret == CURLE_HTTP_RETURNED_ERROR)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);

    curl_easy_cleanup(curl);

    if (ret == CURLE_OK) {
        *data = tmp.buffer;
        return tmp.position;
    }

    Com_EPrintf("[HTTP] Failed to fetch '%s': %s\n",
                url, ret == CURLE_HTTP_RETURNED_ERROR ?
                http_strerror(response) : curl_easy_strerror(ret));
    if (tmp.buffer)
        Z_Free(tmp.buffer);
    return -1;
}

#endif

/*
===============
HTTP_CleanupDownloads

Disconnected from server, or fatal HTTP error occured. Clean up.
===============
*/
void HTTP_CleanupDownloads(void)
{
    dlhandle_t  *dl;
    int         i;

    download_server[0] = 0;
    download_referer[0] = 0;
    download_default_repo = qfalse;

    curl_handles = 0;

    for (i = 0; i < 4; i++) {
        dl = &download_handles[i];

        if (dl->file) {
            fclose(dl->file);
            remove(dl->path);
            dl->file = NULL;
        }

        if (dl->buffer) {
            Z_Free(dl->buffer);
            dl->buffer = NULL;
        }

        if (dl->curl) {
            if (curl_multi && dl->multi_added)
                curl_multi_remove_handle(curl_multi, dl->curl);
            curl_easy_cleanup(dl->curl);
            dl->curl = NULL;
        }

        dl->queue = NULL;
        dl->multi_added = qfalse;
    }

    if (curl_multi) {
        curl_multi_cleanup(curl_multi);
        curl_multi = NULL;
    }
}


/*
===============
HTTP_Init

Init libcurl and multi handle.
===============
*/
void HTTP_Init(void)
{
    cl_http_downloads = Cvar_Get("cl_http_downloads", "1", 0);
    cl_http_filelists = Cvar_Get("cl_http_filelists", "1", 0);
    cl_http_max_connections = Cvar_Get("cl_http_max_connections", "2", 0);
    //cl_http_max_connections->changed = _cl_http_max_connections_changed;
    cl_http_proxy = Cvar_Get("cl_http_proxy", "", 0);
    cl_http_default_url = Cvar_Get("cl_http_default_url", "", 0);

#ifdef _DEBUG
    cl_http_debug = Cvar_Get("cl_http_debug", "0", 0);
#endif

#if USE_UI
    cl_http_blocking_timeout = Cvar_Get("cl_http_blocking_timeout", "15", 0);
#endif

    curl_global_init(CURL_GLOBAL_NOTHING);
    curl_initialized = qtrue;
    Com_DPrintf("%s initialized.\n", curl_version());
}

void HTTP_Shutdown(void)
{
    if (!curl_initialized)
        return;

    HTTP_CleanupDownloads();

    curl_global_cleanup();
    curl_initialized = qfalse;
}


/*
===============
HTTP_SetServer

A new server is specified, so we nuke all our state.
===============
*/
void HTTP_SetServer(const char *url)
{
    if (curl_multi) {
        Com_EPrintf("[HTTP] Set server without cleanup?\n");
        return;
    }

    // ignore on the local server
    if (NET_IsLocalAddress(&cls.serverAddress))
        return;

    // ignore if downloads are permanently disabled
    if (allow_download->integer == -1)
        return;

    // ignore if HTTP downloads are disabled
    if (cl_http_downloads->integer == 0)
        return;

    // use default URL for servers that don't specify one. treat 404 from
    // default repository as fatal error and revert to UDP downloading.
    if (!url) {
        url = cl_http_default_url->string;
        download_default_repo = qtrue;
    } else {
        download_default_repo = qfalse;
    }

    if (!*url)
        return;

    if (strncmp(url, "http://", 7)) {
        Com_Printf("[HTTP] Ignoring download server URL with non-HTTP schema.\n");
        return;
    }

    curl_multi = curl_multi_init();

    Q_strlcpy(download_server, url, sizeof(download_server));
    Q_snprintf(download_referer, sizeof(download_referer),
               "quake2://%s", NET_AdrToString(&cls.serverAddress));

    Com_Printf("[HTTP] Download server at %s\n", download_server);
}

/*
===============
HTTP_QueueDownload

Called from the precache check to queue a download. Return value of
Q_ERR_NOSYS will cause standard UDP downloading to be used instead.
===============
*/
qerror_t HTTP_QueueDownload(const char *path, dltype_t type)
{
    size_t      len;
    qboolean    need_list;
    char        temp[MAX_QPATH];
    qerror_t    ret;

    // no http server (or we got booted)
    if (!curl_multi)
        return Q_ERR_NOSYS;

    // first download queued, so we want the mod filelist
    need_list = LIST_EMPTY(&cls.download.queue);

    ret = CL_QueueDownload(path, type);
    if (ret)
        return ret;

    if (!cl_http_filelists->integer)
        return Q_ERR_SUCCESS;

    if (need_list) {
        //grab the filelist
        len = Q_snprintf(temp, sizeof(temp), "%s.filelist", http_gamedir());
        if (len < sizeof(temp))
            CL_QueueDownload(temp, DL_LIST);

        //this is a nasty hack to let the server know what we're doing so admins don't
        //get confused by a ton of people stuck in CNCT state. it's assumed the server
        //is running r1q2 if we're even able to do http downloading so hopefully this
        //won't spew an error msg.
        if (!download_default_repo)
            CL_ClientCommand("download http\n");
    }

    //special case for map file lists, i really wanted a server-push mechanism for this, but oh well
    len = strlen(path);
    if (len > 4 && !Q_stricmp(path + len - 4, ".bsp")) {
        len = Q_snprintf(temp, sizeof(temp), "%s/%s", http_gamedir(), path);
        if (len < sizeof(temp) - 5) {
            memcpy(temp + len - 4, ".filelist", 10);
            CL_QueueDownload(temp, DL_LIST);
        }
    }

    return Q_ERR_SUCCESS;
}

// Validate a path supplied by a filelist.
static void check_and_queue_download(char *path)
{
    size_t      len;
    char        *ext;
    dltype_t    type;
    unsigned    flags;
    int         valid;

    len = strlen(path);
    if (len >= MAX_QPATH)
        return;

    ext = strrchr(path, '.');
    if (!ext)
        return;

    ext++;
    if (!ext[0])
        return;

    Q_strlwr(ext);

    if (!strcmp(ext, "pak") || !strcmp(ext, "pkz")) {
        Com_Printf("[HTTP] Filelist is requesting a .%s file '%s'\n", ext, path);
        type = DL_PAK;
    } else {
        type = DL_OTHER;
        if (!CL_CheckDownloadExtension(ext)) {
            Com_WPrintf("[HTTP] Illegal file type '%s' in filelist.\n", path);
            return;
        }
    }

    if (path[0] == '@') {
        if (type == DL_PAK) {
            Com_WPrintf("[HTTP] '@' prefix used on a pak file '%s' in filelist.\n", path);
            return;
        }
        flags = FS_PATH_GAME;
        path++;
        len--;
    } else if (type == DL_PAK) {
        //by definition paks are game-local
        flags = FS_PATH_GAME | FS_TYPE_REAL;
    } else {
        flags = 0;
    }

    len = FS_NormalizePath(path, path);
    if (len == 0)
        return;

    valid = FS_ValidatePath(path);

    if (valid == PATH_INVALID ||
        !Q_ispath(path[0]) ||
        !Q_ispath(path[len - 1]) ||
        strstr(path, "..") ||
        (type == DL_OTHER && !strchr(path, '/')) ||
        (type == DL_PAK && strchr(path, '/'))) {
        Com_WPrintf("[HTTP] Illegal path '%s' in filelist.\n", path);
        return;
    }

    if (FS_FileExistsEx(path, flags))
        return;

    if (valid == PATH_MIXED_CASE)
        Q_strlwr(path);

    if (CL_IgnoreDownload(path))
        return;

    CL_QueueDownload(path, type);
}

// A filelist is in memory, scan and validate it and queue up the files.
static void parse_file_list(dlhandle_t *dl)
{
    char    *list;
    char    *p;

    if (!dl->buffer)
        return;

    if (cl_http_filelists->integer) {
        list = dl->buffer;
        while (*list) {
            p = strchr(list, '\n');
            if (p) {
                if (p > list && *(p - 1) == '\r')
                    *(p - 1) = 0;
                *p = 0;
            }

            if (*list)
                check_and_queue_download(list);

            if (!p)
                break;
            list = p + 1;
        }
    }

    Z_Free(dl->buffer);
    dl->buffer = NULL;
}

// A pak file just downloaded, let's see if we can remove some stuff from
// the queue which is in the .pak.
static void rescan_queue(void)
{
    dlqueue_t   *q;

    FOR_EACH_DLQ(q) {
        if (q->state == DL_PENDING && q->type < DL_LIST && FS_FileExists(q->path))
            CL_FinishDownload(q);
    }
}

// Fatal HTTP error occured, remove any special entries from
// queue and fall back to UDP downloading.
static void abort_downloads(void)
{
    dlqueue_t   *q;

    HTTP_CleanupDownloads();

    cls.download.current = NULL;
    cls.download.percent = 0;
    cls.download.position = 0;

    FOR_EACH_DLQ(q) {
        if (q->state != DL_DONE && q->type >= DL_LIST)
            CL_FinishDownload(q);
        else if (q->state == DL_RUNNING)
            q->state = DL_PENDING;
    }

    CL_RequestNextDownload();
    CL_StartNextDownload();
}

// curl doesn't provide reverse-lookup of the void * ptr, so search for it
static dlhandle_t *find_handle(CURL *curl)
{
    size_t      i;
    dlhandle_t  *dl;

    for (i = 0; i < 4; i++) {
        dl = &download_handles[i];
        if (dl->curl == curl) {
            return dl;
        }
    }

    Com_Error(ERR_FATAL, "CURL handle not found for CURLMSG_DONE");
    return NULL;
}

// A download finished, find out what it was, whether there were any errors and
// if so, how severe. If none, rename file and other such stuff.
static qboolean finish_download(void)
{
    int         msgs_in_queue;
    CURLMsg     *msg;
    CURLcode    result;
    dlhandle_t  *dl;
    CURL        *curl;
    long        response;
    double      sec, bytes;
    char        size[16], speed[16];
    char        temp[MAX_OSPATH];
    qboolean    fatal_error = qfalse;
    const char  *err;
    print_type_t level;

    do {
        msg = curl_multi_info_read(curl_multi, &msgs_in_queue);
        if (!msg)
            break;

        if (msg->msg != CURLMSG_DONE)
            continue;

        curl = msg->easy_handle;
        dl = find_handle(curl);

        cls.download.current = NULL;
        cls.download.percent = 0;
        cls.download.position = 0;

        //filelist processing is done on read
        if (dl->file) {
            fclose(dl->file);
            dl->file = NULL;
        }

        curl_handles--;

        result = msg->data.result;

        switch (result) {
            //for some reason curl returns CURLE_OK for a 404...
        case CURLE_HTTP_RETURNED_ERROR:
        case CURLE_OK:
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
            if (result == CURLE_OK && response == 200) {
                //success
                break;
            }

            err = http_strerror(response);

            //404 is non-fatal unless accessing default repository
            if (response == 404 && (!download_default_repo || !dl->path[0])) {
                level = PRINT_ALL;
                goto fail1;
            }

            //every other code is treated as fatal
            //not marking download as done since
            //we are falling back to UDP
            level = PRINT_ERROR;
            fatal_error = qtrue;
            goto fail2;

        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_PROXY:
            //connection problems are fatal
            err = curl_easy_strerror(result);
            level = PRINT_ERROR;
            fatal_error = qtrue;
            goto fail2;

        default:
            err = curl_easy_strerror(result);
            level = PRINT_WARNING;
fail1:
            //we mark download as done even if it errored
            //to prevent multiple attempts.
            CL_FinishDownload(dl->queue);
fail2:
            Com_LPrintf(level,
                        "[HTTP] %s [%s] [%d remaining file%s]\n",
                        dl->queue->path, err, cls.download.pending,
                        cls.download.pending == 1 ? "" : "s");
            if (dl->path[0]) {
                remove(dl->path);
                dl->path[0] = 0;
            }
            if (dl->buffer) {
                Z_Free(dl->buffer);
                dl->buffer = NULL;
            }
            if (dl->multi_added) {
                //remove the handle and mark it as such
                curl_multi_remove_handle(curl_multi, curl);
                dl->multi_added = qfalse;
            }
            continue;
        }

        //mark as done
        CL_FinishDownload(dl->queue);

        //show some stats
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &sec);
        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &bytes);
        if (sec < 0.001)
            sec = 0.001;
        Com_FormatSizeLong(size, sizeof(size), bytes);
        Com_FormatSizeLong(speed, sizeof(speed), bytes / sec);

        if (dl->multi_added) {
            //remove the handle and mark it as such
            curl_multi_remove_handle(curl_multi, curl);
            dl->multi_added = qfalse;
        }

        Com_Printf("[HTTP] %s [%s, %s/sec] [%d remaining file%s]\n",
                   dl->queue->path, size, speed, cls.download.pending,
                   cls.download.pending == 1 ? "" : "s");

        if (dl->path[0]) {
            //rename the temp file
            Q_snprintf(temp, sizeof(temp), "%s/%s", fs_gamedir, dl->queue->path);

            if (rename(dl->path, temp))
                Com_EPrintf("[HTTP] Failed to rename '%s' to '%s': %s\n",
                            dl->path, dl->queue->path, strerror(errno));
            dl->path[0] = 0;

            //a pak file is very special...
            if (dl->queue->type == DL_PAK) {
                CL_RestartFilesystem(qfalse);
                rescan_queue();
            }
        } else if (!fatal_error) {
            parse_file_list(dl);
        }
    } while (msgs_in_queue > 0);

    //fatal error occured, disable HTTP
    if (fatal_error) {
        abort_downloads();
        return qfalse;
    }

    // see if we have more to dl
    CL_RequestNextDownload();
    return qtrue;
}

// Find a free download handle to start another queue entry on.
static dlhandle_t *get_free_handle(void)
{
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
static void start_next_download(void)
{
    dlqueue_t   *q;

    if (!cls.download.pending || curl_handles >= cl_http_max_connections->integer) {
        return;
    }

    //not enough downloads running, queue some more!
    FOR_EACH_DLQ(q) {
        if (q->state == DL_RUNNING) {
            if (q->type == DL_PAK)
                break; // hack for pak file single downloading
        } else if (q->state == DL_PENDING) {
            dlhandle_t *dl = get_free_handle();
            if (dl)
                start_download(q, dl);
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
void HTTP_RunDownloads(void)
{
    int         new_count;
    CURLMcode   ret;

    if (!curl_multi)
        return;

    start_next_download();

    do {
        ret = curl_multi_perform(curl_multi, &new_count);
        if (new_count < curl_handles) {
            //hmm, something either finished or errored out.
            if (!finish_download())
                return; //aborted
            curl_handles = new_count;
        }
    } while (ret == CURLM_CALL_MULTI_PERFORM);

    if (ret != CURLM_OK) {
        Com_EPrintf("[HTTP] Error running downloads: %s.\n",
                    curl_multi_strerror(ret));
        abort_downloads();
        return;
    }

    start_next_download();
}

