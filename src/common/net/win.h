/*
Copyright (C) 2012 Andrey Nazarov

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

//
// net_win.h -- Windows sockets wrapper
//

static const struct {
    int err;
    const char *msg;
} wsa_error_table[] = {
    { S_OK,                     "Success"                                   },
    { WSAEINTR,                 "Interrupted function call"                 },
    { WSAEBADF,                 "File handle is not valid"                  },
    { WSAEACCES,                "Permission denied"                         },
    { WSAEFAULT,                "Bad address"                               },
    { WSAEINVAL,                "Invalid argument"                          },
    { WSAEMFILE,                "Too many open files"                       },
    { WSAEWOULDBLOCK,           "Resource temporarily unavailable"          },
    { WSAEINPROGRESS,           "Operation now in progress"                 },
    { WSAEALREADY,              "Operation already in progress"             },
    { WSAENOTSOCK,              "Socket operation on nonsocket"             },
    { WSAEDESTADDRREQ,          "Destination address required"              },
    { WSAEMSGSIZE,              "Message too long"                          },
    { WSAEPROTOTYPE,            "Protocol wrong type for socket"            },
    { WSAENOPROTOOPT,           "Bad protocol option"                       },
    { WSAEPROTONOSUPPORT,       "Protocol not supported"                    },
    { WSAESOCKTNOSUPPORT,       "Socket type not supported"                 },
    { WSAEOPNOTSUPP,            "Operation not supported"                   },
    { WSAEPFNOSUPPORT,          "Protocol family not supported"             },
    { WSAEAFNOSUPPORT,  "Address family not supported by protocol family"   },
    { WSAEADDRINUSE,            "Address already in use"                    },
    { WSAEADDRNOTAVAIL,         "Cannot assign requested address"           },
    { WSAENETDOWN,              "Network is down"                           },
    { WSAENETUNREACH,           "Network is unreachable"                    },
    { WSAENETRESET,             "Network dropped connection on reset"       },
    { WSAECONNABORTED,          "Software caused connection abort"          },
    { WSAECONNRESET,            "Connection reset by peer"                  },
    { WSAENOBUFS,               "No buffer space available"                 },
    { WSAEISCONN,               "Socket is already connected"               },
    { WSAENOTCONN,              "Socket is not connected"                   },
    { WSAESHUTDOWN,             "Cannot send after socket shutdown"         },
    { WSAETOOMANYREFS,          "Too many references"                       },
    { WSAETIMEDOUT,             "Connection timed out"                      },
    { WSAECONNREFUSED,          "Connection refused"                        },
    { WSAELOOP,                 "Cannot translate name"                     },
    { WSAENAMETOOLONG,          "Name too long"                             },
    { WSAEHOSTDOWN,             "Host is down"                              },
    { WSAEHOSTUNREACH,          "No route to host"                          },
    { WSAENOTEMPTY,             "Directory not empty"                       },
    { WSAEPROCLIM,              "Too many processes"                        },
    { WSAEUSERS,                "User quota exceeded"                       },
    { WSAEDQUOT,                "Disk quota exceeded"                       },
    { WSAESTALE,                "Stale file handle reference"               },
    { WSAEREMOTE,               "Item is remote"                            },
    { WSASYSNOTREADY,           "Network subsystem is unavailable"          },
    { WSAVERNOTSUPPORTED,       "Winsock.dll version out of range"          },
    { WSANOTINITIALISED,        "Successful WSAStartup not yet performed"   },
    { WSAEDISCON,               "Graceful shutdown in progress"             },
    { WSAENOMORE,               "No more results"                           },
    { WSAECANCELLED,            "Call has been canceled"                    },
    { WSAEINVALIDPROCTABLE,     "Procedure call table is invalid"           },
    { WSAEINVALIDPROVIDER,      "Service provider is invalid"               },
    { WSAEPROVIDERFAILEDINIT,   "Service provider failed to initialize"     },
    { WSASYSCALLFAILURE,        "System call failure"                       },
    { WSASERVICE_NOT_FOUND,     "Service not found"                         },
    { WSATYPE_NOT_FOUND,        "Class type not found"                      },
    { WSA_E_NO_MORE,            "No more results"                           },
    { WSA_E_CANCELLED,          "Call was canceled"                         },
    { WSAEREFUSED,              "Database query was refused"                },
    { WSAHOST_NOT_FOUND,        "Host not found"                            },
    { WSATRY_AGAIN,             "Nonauthoritative host not found"           },
    { WSANO_RECOVERY,           "This is a nonrecoverable error"            },
    { WSANO_DATA,       "Valid name, no data record of requested type"      },
    { -1,                       "Unknown error"                             }
};

static const char *os_error_string(int err)
{
    int i;

    for (i = 0; wsa_error_table[i].err != -1; i++) {
        if (wsa_error_table[i].err == err)
            break;
    }

    return wsa_error_table[i].msg;
}

static ssize_t os_udp_recv(qsocket_t sock, void *data,
                           size_t len, netadr_t *from)
{
    struct sockaddr_storage addr;
    int addrlen;
    int ret;
    int tries;

    for (tries = 0; tries < MAX_ERROR_RETRIES; tries++) {
        memset(&addr, 0, sizeof(addr));
        addrlen = sizeof(addr);
        ret = recvfrom(sock, data, len, 0,
                       (struct sockaddr *)&addr, &addrlen);

        NET_SockadrToNetadr(&addr, from);

        if (ret != SOCKET_ERROR)
            return ret;

        net_error = WSAGetLastError();

        // wouldblock is silent
        if (net_error == WSAEWOULDBLOCK)
            return NET_AGAIN;

#if USE_ICMP
        if (net_error == WSAECONNRESET || net_error == WSAENETRESET) {
            // winsock has already provided us with
            // a valid address from ICMP error packet
            NET_ErrorEvent(sock, from, net_error, 0);
            continue;
        }
#endif

        break;
    }

    return NET_ERROR;
}

static ssize_t os_udp_send(qsocket_t sock, const void *data,
                           size_t len, const netadr_t *to)
{
    struct sockaddr_storage addr;
    int addrlen;
    int ret;

    addrlen = NET_NetadrToSockadr(to, &addr);

    ret = sendto(sock, data, len, 0,
                 (struct sockaddr *)&addr, addrlen);

    if (ret != SOCKET_ERROR)
        return ret;

    net_error = WSAGetLastError();

    // wouldblock is silent
    if (net_error == WSAEWOULDBLOCK || net_error == WSAEINTR)
        return NET_AGAIN;

    // some PPP links do not allow broadcasts
    if (net_error == WSAEADDRNOTAVAIL && to->type == NA_BROADCAST)
        return NET_AGAIN;

    return NET_ERROR;
}

static neterr_t os_get_error(void)
{
    net_error = WSAGetLastError();
    if (net_error == WSAEWOULDBLOCK)
        return NET_AGAIN;

    return NET_ERROR;
}

static ssize_t os_recv(qsocket_t sock, void *data, size_t len, int flags)
{
    int ret = recv(sock, data, len, flags);

    if (ret == SOCKET_ERROR)
        return os_get_error();

    return ret;
}

static ssize_t os_send(qsocket_t sock, const void *data, size_t len, int flags)
{
    int ret = send(sock, data, len, flags);

    if (ret == SOCKET_ERROR)
        return os_get_error();

    return ret;
}

static neterr_t os_listen(qsocket_t sock, int backlog)
{
    if (listen(sock, backlog) == SOCKET_ERROR) {
        net_error = WSAGetLastError();
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_accept(qsocket_t sock, qsocket_t *newsock, netadr_t *from)
{
    struct sockaddr_storage addr;
    int addrlen;
    SOCKET s;

    memset(&addr, 0, sizeof(addr));
    addrlen = sizeof(addr);
    s = accept(sock, (struct sockaddr *)&addr, &addrlen);

    NET_SockadrToNetadr(&addr, from);

    if (s == INVALID_SOCKET) {
        *newsock = -1;
        return os_get_error();
    }

    *newsock = s;
    return NET_OK;
}

static neterr_t os_connect(qsocket_t sock, const netadr_t *to)
{
    struct sockaddr_storage addr;
    int addrlen;

    addrlen = NET_NetadrToSockadr(to, &addr);

    if (connect(sock, (struct sockaddr *)&addr, addrlen) == SOCKET_ERROR) {
        net_error = WSAGetLastError();
        if (net_error == WSAEWOULDBLOCK)
            return NET_OK;

        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_make_nonblock(qsocket_t sock, int val)
{
    u_long _val = val;

    if (ioctlsocket(sock, FIONBIO, &_val) == SOCKET_ERROR) {
        net_error = WSAGetLastError();
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_setsockopt(qsocket_t sock, int level, int name, int val)
{
    u_long _val = val;

    if (setsockopt(sock, level, name, (char *)&_val, sizeof(_val)) == SOCKET_ERROR) {
        net_error = WSAGetLastError();
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_getsockopt(qsocket_t sock, int level, int name, int *val)
{
    u_long _val;
    int optlen = sizeof(_val);

    if (getsockopt(sock, level, name, (char *)&_val, &optlen) == SOCKET_ERROR) {
        net_error = WSAGetLastError();
        return NET_ERROR;
    }

    *val = _val;
    return NET_OK;
}

static neterr_t os_bind(qsocket_t sock, const struct sockaddr *addr, size_t addrlen)
{
    if (bind(sock, addr, addrlen) == SOCKET_ERROR) {
        net_error = WSAGetLastError();
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_getsockname(qsocket_t sock, netadr_t *name)
{
    struct sockaddr_storage addr;
    int addrlen;

    memset(&addr, 0, sizeof(addr));
    addrlen = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &addrlen) == SOCKET_ERROR) {
        net_error = WSAGetLastError();
        return NET_ERROR;
    }

    NET_SockadrToNetadr(&addr, name);
    return NET_OK;
}

static void os_closesocket(qsocket_t sock)
{
    closesocket(sock);
}

static qsocket_t os_socket(int domain, int type, int protocol)
{
    SOCKET s = socket(domain, type, protocol);

    if (s == INVALID_SOCKET) {
        net_error = WSAGetLastError();
        return -1;
    }

    return s;
}

static ioentry_t *os_add_io(qsocket_t fd)
{
    ioentry_t *e;
    int i;

    for (i = 0, e = io_entries; i < io_numfds; i++, e++) {
        if (!e->inuse)
            break;
    }

    if (i == io_numfds) {
        if (++io_numfds > FD_SETSIZE)
            Com_Error(ERR_FATAL, "%s: no more space for fd: %Id", __func__, fd);
    }

    e->fd = fd;
    return e;
}

static ioentry_t *os_get_io(qsocket_t fd)
{
    ioentry_t *e;
    int i;

    for (i = 0, e = io_entries; i < io_numfds; i++, e++) {
        if (!e->inuse)
            continue;
        if (e->fd == fd)
            return e;
    }

    Com_Error(ERR_FATAL, "%s: fd not found: %Id", __func__, fd);
    return NULL;
}

static qsocket_t os_get_fd(ioentry_t *e)
{
    return e->fd;
}

static int os_select(int nfds, fd_set *rfds, fd_set *wfds,
                     fd_set *efds, struct timeval *tv)
{
    int ret = select(nfds, rfds, wfds, efds, tv);

    if (ret == SOCKET_ERROR)
        net_error = WSAGetLastError();

    return ret;
}

static void os_net_init(void)
{
    WSADATA ws;
    int ret;

    ret = WSAStartup(MAKEWORD(1, 1), &ws);
    if (ret) {
        Com_Error(ERR_FATAL, "Winsock initialization failed: %s (%d)",
                  os_error_string(ret), ret);
    }

    Com_DPrintf("Winsock initialized\n");
}

static void os_net_shutdown(void)
{
    WSACleanup();
}

