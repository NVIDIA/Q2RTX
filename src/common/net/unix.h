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
// net_unix.h -- BSD sockets wrapper
//

static const char *os_error_string(int err)
{
    return strerror(err);
}

// returns true if failed socket operation should be retried.
static qboolean process_error_queue(qsocket_t sock, const netadr_t *to)
{
#ifdef IP_RECVERR
    byte buffer[1024];
    struct sockaddr_storage from_addr;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    struct sock_extended_err *ee;
    netadr_t from;
    int tries;
    qboolean found = qfalse;

    for (tries = 0; tries < MAX_ERROR_RETRIES; tries++) {
        memset(&from_addr, 0, sizeof(from_addr));

        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &from_addr;
        msg.msg_namelen = sizeof(from_addr);
        msg.msg_control = buffer;
        msg.msg_controllen = sizeof(buffer);

        if (recvmsg(sock, &msg, MSG_ERRQUEUE) == -1) {
            if (errno != EWOULDBLOCK)
                Com_DPrintf("%s: %s\n", __func__, strerror(errno));
            break;
        }

        if (!(msg.msg_flags & MSG_ERRQUEUE)) {
            Com_DPrintf("%s: no extended error received\n", __func__);
            break;
        }

        // find an ICMP error message
        for (cmsg = CMSG_FIRSTHDR(&msg);
             cmsg != NULL;
             cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level != IPPROTO_IP &&
                cmsg->cmsg_level != IPPROTO_IPV6) {
                continue;
            }
            if (cmsg->cmsg_type != IP_RECVERR &&
                cmsg->cmsg_type != IPV6_RECVERR) {
                continue;
            }
            ee = (struct sock_extended_err *)CMSG_DATA(cmsg);
            if (ee->ee_origin == SO_EE_ORIGIN_ICMP ||
                ee->ee_origin == SO_EE_ORIGIN_ICMP6) {
                break;
            }
        }

        if (!cmsg) {
            Com_DPrintf("%s: no ICMP error found\n", __func__);
            break;
        }

        NET_SockadrToNetadr(&from_addr, &from);

        // check for offender address being current packet destination
        if (to != NULL && NET_IsEqualBaseAdr(&from, to) &&
            (from.port == 0 || from.port == to->port)) {
            Com_DPrintf("%s: found offending address: %s\n", __func__,
                        NET_AdrToString(&from));
            found = qtrue;
        }

        // handle ICMP error
        NET_ErrorEvent(sock, &from, ee->ee_errno, ee->ee_info);
    }

    return !!tries && !found;
#else
    return qfalse;
#endif
}

static ssize_t os_udp_recv(qsocket_t sock, void *data,
                           size_t len, netadr_t *from)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;
    ssize_t ret;
    int tries;

    for (tries = 0; tries < MAX_ERROR_RETRIES; tries++) {
        memset(&addr, 0, sizeof(addr));
        addrlen = sizeof(addr);
        ret = recvfrom(sock, data, len, 0,
                       (struct sockaddr *)&addr, &addrlen);

        NET_SockadrToNetadr(&addr, from);

        if (ret >= 0)
            return ret;

        net_error = errno;

        // wouldblock is silent
        if (net_error == EWOULDBLOCK)
            return NET_AGAIN;

        if (!process_error_queue(sock, NULL))
            break;
    }

    return NET_ERROR;
}

static ssize_t os_udp_send(qsocket_t sock, const void *data,
                           size_t len, const netadr_t *to)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;
    ssize_t ret;
    int tries;

    addrlen = NET_NetadrToSockadr(to, &addr);

    for (tries = 0; tries < MAX_ERROR_RETRIES; tries++) {
        ret = sendto(sock, data, len, 0,
                     (struct sockaddr *)&addr, addrlen);
        if (ret >= 0)
            return ret;

        net_error = errno;

        // wouldblock is silent
        if (net_error == EWOULDBLOCK)
            return NET_AGAIN;

        if (!process_error_queue(sock, to))
            break;
    }

    return NET_ERROR;
}

static neterr_t os_get_error(void)
{
    net_error = errno;
    if (net_error == EWOULDBLOCK)
        return NET_AGAIN;

    return NET_ERROR;
}

static ssize_t os_recv(qsocket_t sock, void *data, size_t len, int flags)
{
    ssize_t ret = recv(sock, data, len, flags);

    if (ret == -1)
        return os_get_error();

    return ret;
}

static ssize_t os_send(qsocket_t sock, const void *data, size_t len, int flags)
{
    ssize_t ret = send(sock, data, len, flags);

    if (ret == -1)
        return os_get_error();

    return ret;
}

static neterr_t os_listen(qsocket_t sock, int backlog)
{
    if (listen(sock, backlog) == -1) {
        net_error = errno;
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_accept(qsocket_t sock, qsocket_t *newsock, netadr_t *from)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int s;

    memset(&addr, 0, sizeof(addr));
    addrlen = sizeof(addr);
    s = accept(sock, (struct sockaddr *)&addr, &addrlen);

    NET_SockadrToNetadr(&addr, from);

    if (s == -1) {
        *newsock = -1;
        return os_get_error();
    }

    *newsock = s;
    return NET_OK;
}

static neterr_t os_connect(qsocket_t sock, const netadr_t *to)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;

    addrlen = NET_NetadrToSockadr(to, &addr);

    if (connect(sock, (struct sockaddr *)&addr, addrlen) == -1) {
        net_error = errno;
        if (net_error == EINPROGRESS)
            return NET_OK;

        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_make_nonblock(qsocket_t sock, int val)
{
    if (ioctl(sock, FIONBIO, &val) == -1) {
        net_error = errno;
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_setsockopt(qsocket_t sock, int level, int name, int val)
{
    if (setsockopt(sock, level, name, &val, sizeof(val)) == -1) {
        net_error = errno;
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_getsockopt(qsocket_t sock, int level, int name, int *val)
{
    socklen_t _optlen = sizeof(*val);

    if (getsockopt(sock, level, name, val, &_optlen) == -1) {
        net_error = errno;
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_bind(qsocket_t sock, const struct sockaddr *addr, size_t addrlen)
{
    if (bind(sock, addr, addrlen) == -1) {
        net_error = errno;
        return NET_ERROR;
    }

    return NET_OK;
}

static neterr_t os_getsockname(qsocket_t sock, netadr_t *name)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;

    memset(&addr, 0, sizeof(addr));
    addrlen = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &addrlen) == -1) {
        net_error = errno;
        return NET_ERROR;
    }

    NET_SockadrToNetadr(&addr, name);
    return NET_OK;
}

static void os_closesocket(qsocket_t sock)
{
    close(sock);
}

static qsocket_t os_socket(int domain, int type, int protocol)
{
    int s = socket(domain, type, protocol);

    if (s == -1) {
        net_error = errno;
        return -1;
    }

    return s;
}

static ioentry_t *_os_get_io(qsocket_t fd, const char *func)
{
    if (fd < 0 || fd >= FD_SETSIZE)
        Com_Error(ERR_FATAL, "%s: fd out of range: %d", func, fd);

    return &io_entries[fd];
}

static ioentry_t *os_add_io(qsocket_t fd)
{
    if (fd >= io_numfds) {
        io_numfds = fd + 1;
    }

    return _os_get_io(fd, __func__);
}

static ioentry_t *os_get_io(qsocket_t fd)
{
    return _os_get_io(fd, __func__);
}

static qsocket_t os_get_fd(ioentry_t *e)
{
    return e - io_entries;
}

static int os_select(int nfds, fd_set *rfds, fd_set *wfds,
                     fd_set *efds, struct timeval *tv)
{
    int ret = select(nfds, rfds, wfds, efds, tv);

    if (ret == -1) {
        net_error = errno;
        if (net_error == EINTR)
            return 0;
    }

    return ret;
}

static void os_net_init(void)
{
}

static void os_net_shutdown(void)
{
}

