#include <kernel/socket.h>
#include <kernel/process.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/kernel.h>
#include <shared/errno.h>

static kmem_cache_t *g_socket_cache = NULL;

static int64_t socket_file_read(file_t *file, void *buf, size_t count) {
    if (!file || !file->priv || !buf) return -STRATUM_EINVAL;
    socket_t *sock = (socket_t *)file->priv;
    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        return tcp_recv(sock->pcb.tcp, buf, count);
    }
    return -STRATUM_ENOTSUP;
}

static int64_t socket_file_write(file_t *file, const void *buf, size_t count) {
    if (!file || !file->priv || !buf) return -STRATUM_EINVAL;
    socket_t *sock = (socket_t *)file->priv;
    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        return tcp_send(sock->pcb.tcp, buf, count);
    } else if (sock->type == SOCK_DGRAM && sock->pcb.udp) {
        return udp_output(0, sock->pcb.udp->local_port, sock->pcb.udp->remote_ip,
                          sock->pcb.udp->remote_port, buf, count);
    }
    return -STRATUM_ENOTSUP;
}

static int socket_file_poll(file_t *file, uint32_t events) {
    if (!file || !file->priv) return 0;
    socket_t *sock = (socket_t *)file->priv;
    int revents = 0;

    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        tcp_pcb_t *pcb = sock->pcb.tcp;
        if (events & POLLIN) {
            if (pcb->state == TCP_STATE_LISTEN && pcb->backlog_head != NULL) {
                revents |= POLLIN;
            } else if (pcb->rx_count > 0 || pcb->state == TCP_STATE_CLOSE_WAIT || pcb->state == TCP_STATE_CLOSED) {
                revents |= POLLIN;
            }
        }
        if (events & POLLOUT) {
            if (pcb->state == TCP_STATE_ESTABLISHED) {
                revents |= POLLOUT;
            }
        }
    } else if (sock->type == SOCK_DGRAM) {
        if (events & POLLOUT) {
            revents |= POLLOUT;
        }
    }

    return revents;
}

static int socket_file_close(file_t *file) {
    if (!file || !file->priv) return 0;
    socket_t *sock = (socket_t *)file->priv;

    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        tcp_close(sock->pcb.tcp);
    } else if (sock->type == SOCK_DGRAM && sock->pcb.udp) {
        udp_close(sock->pcb.udp);
    }

    kmem_cache_free(g_socket_cache, sock);
    file->priv = NULL;
    return 0;
}

static const file_ops_t g_socket_file_ops = {
    .read  = socket_file_read,
    .write = socket_file_write,
    .poll  = socket_file_poll,
    .close = socket_file_close,
};

void socket_init(void) {
    g_socket_cache = kmem_cache_create("socket_cache", sizeof(socket_t), 8);
    kassert(g_socket_cache != NULL);
}

file_t *socket_create_file(socket_t *sock) {
    if (!sock) return NULL;
    file_t *f = file_alloc(FILE_TYPE_SOCKET, &g_socket_file_ops, sock);
    if (f) {
        sock->file = f;
    }
    return f;
}

static fd_table_t g_kernel_fds;
static bool       g_kernel_fds_inited = false;

static fd_table_t *get_active_fd_table(void) {
    process_t *p = process_get_current();
    if (p) return &p->fds;
    if (!g_kernel_fds_inited) {
        fd_table_init(&g_kernel_fds);
        g_kernel_fds_inited = true;
    }
    return &g_kernel_fds;
}

int sys_socket(int domain, int type, int protocol) {
    if (domain != AF_INET) return -STRATUM_EAFNOSUPPORT;
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -STRATUM_EINVAL;

    socket_t *sock = (socket_t *)kmem_cache_alloc(g_socket_cache);
    if (!sock) return -STRATUM_ENOMEM;

    memset(sock, 0, sizeof(socket_t));
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = SOCKET_STATE_UNBOUND;
    spin_lock_init(&sock->lock);

    if (type == SOCK_STREAM) {
        sock->pcb.tcp = tcp_new(sock);
        if (!sock->pcb.tcp) {
            kmem_cache_free(g_socket_cache, sock);
            return -STRATUM_ENOMEM;
        }
    } else {
        sock->pcb.udp = udp_new(sock);
        if (!sock->pcb.udp) {
            kmem_cache_free(g_socket_cache, sock);
            return -STRATUM_ENOMEM;
        }
    }

    file_t *f = socket_create_file(sock);
    if (!f) {
        if (type == SOCK_STREAM) tcp_close(sock->pcb.tcp);
        else udp_close(sock->pcb.udp);
        kmem_cache_free(g_socket_cache, sock);
        return -STRATUM_ENOMEM;
    }

    fd_table_t *table = get_active_fd_table();
    int fd = fd_alloc(table, f);
    if (fd < 0) {
        file_close(f);
        return fd;
    }

    return fd;
}

static socket_t *get_socket_from_fd(int fd) {
    fd_table_t *table = get_active_fd_table();
    file_t *f = fd_get(table, fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->priv) {
        return NULL;
    }
    return (socket_t *)f->priv;
}

int sys_bind(int fd, const struct sockaddr *addr, uint32_t addrlen) {
    if (!addr || addrlen < sizeof(struct sockaddr_in)) return -STRATUM_EINVAL;
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (sin->sin_family != AF_INET) return -STRATUM_EAFNOSUPPORT;

    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;

    uint16_t port = ntohs(sin->sin_port);
    int res = 0;
    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        res = tcp_bind(sock->pcb.tcp, port);
    } else if (sock->type == SOCK_DGRAM && sock->pcb.udp) {
        res = udp_bind(sock->pcb.udp, port);
    } else {
        res = -STRATUM_EINVAL;
    }

    if (res == 0) {
        sock->state = SOCKET_STATE_BOUND;
    }
    return res;
}

int sys_listen(int fd, int backlog) {
    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;
    if (sock->type != SOCK_STREAM || !sock->pcb.tcp) return -STRATUM_EOPNOTSUPP;

    int res = tcp_listen(sock->pcb.tcp, backlog);
    if (res == 0) {
        sock->state = SOCKET_STATE_LISTENING;
    }
    return res;
}

int sys_accept(int fd, struct sockaddr *addr, uint32_t *addrlen) {
    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;
    if (sock->type != SOCK_STREAM || !sock->pcb.tcp) return -STRATUM_EOPNOTSUPP;
    if (sock->state != SOCKET_STATE_LISTENING) return -STRATUM_EINVAL;

    tcp_pcb_t *child_pcb = tcp_accept(sock->pcb.tcp);
    if (!child_pcb) {
        return -STRATUM_EAGAIN;
    }

    socket_t *child_sock = (socket_t *)kmem_cache_alloc(g_socket_cache);
    if (!child_sock) {
        tcp_close(child_pcb);
        return -STRATUM_ENOMEM;
    }

    memset(child_sock, 0, sizeof(socket_t));
    child_sock->domain = AF_INET;
    child_sock->type = SOCK_STREAM;
    child_sock->protocol = IPPROTO_TCP;
    child_sock->state = SOCKET_STATE_CONNECTED;
    child_sock->pcb.tcp = child_pcb;
    child_pcb->sock = child_sock;
    spin_lock_init(&child_sock->lock);

    file_t *child_f = socket_create_file(child_sock);
    if (!child_f) {
        tcp_close(child_pcb);
        kmem_cache_free(g_socket_cache, child_sock);
        return -STRATUM_ENOMEM;
    }

    fd_table_t *table = get_active_fd_table();
    int child_fd = fd_alloc(table, child_f);
    if (child_fd < 0) {
        file_close(child_f);
        return child_fd;
    }

    if (addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        memset(sin, 0, sizeof(struct sockaddr_in));
        sin->sin_family = AF_INET;
        sin->sin_port = htons(child_pcb->remote_port);
        sin->sin_addr.s_addr = htonl(child_pcb->remote_ip);
        *addrlen = sizeof(struct sockaddr_in);
    }

    return child_fd;
}

int sys_connect(int fd, const struct sockaddr *addr, uint32_t addrlen) {
    if (!addr || addrlen < sizeof(struct sockaddr_in)) return -STRATUM_EINVAL;
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (sin->sin_family != AF_INET) return -STRATUM_EAFNOSUPPORT;

    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;

    uint32_t remote_ip = ntohl(sin->sin_addr.s_addr);
    uint16_t remote_port = ntohs(sin->sin_port);

    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        int res = tcp_connect(sock->pcb.tcp, remote_ip, remote_port);
        if (res == 0) {
            sock->state = SOCKET_STATE_CONNECTING;
        }
        return res;
    } else if (sock->type == SOCK_DGRAM && sock->pcb.udp) {
        int res = udp_connect(sock->pcb.udp, remote_ip, remote_port);
        if (res == 0) {
            sock->state = SOCKET_STATE_CONNECTED;
        }
        return res;
    }

    return -STRATUM_EINVAL;
}

int64_t sys_send(int fd, const void *buf, size_t len, int flags) {
    (void)flags;
    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;

    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        return tcp_send(sock->pcb.tcp, buf, len);
    } else if (sock->type == SOCK_DGRAM && sock->pcb.udp) {
        return udp_output(0, sock->pcb.udp->local_port, sock->pcb.udp->remote_ip,
                          sock->pcb.udp->remote_port, buf, len);
    }

    return -STRATUM_EINVAL;
}

int64_t sys_recv(int fd, void *buf, size_t len, int flags) {
    (void)flags;
    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;

    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        return tcp_recv(sock->pcb.tcp, buf, len);
    }

    return -STRATUM_EINVAL;
}

int64_t sys_sendto(int fd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, uint32_t addrlen) {
    (void)flags;
    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;

    if (sock->type == SOCK_DGRAM && sock->pcb.udp) {
        if (!dest_addr || addrlen < sizeof(struct sockaddr_in)) return -STRATUM_EINVAL;
        const struct sockaddr_in *sin = (const struct sockaddr_in *)dest_addr;
        uint32_t rip = ntohl(sin->sin_addr.s_addr);
        uint16_t rport = ntohs(sin->sin_port);
        return udp_output(0, sock->pcb.udp->local_port, rip, rport, buf, len);
    }

    return sys_send(fd, buf, len, flags);
}

int64_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, uint32_t *addrlen) {
    (void)src_addr;
    (void)addrlen;
    return sys_recv(fd, buf, len, flags);
}

int sys_shutdown(int fd, int how) {
    (void)how;
    socket_t *sock = get_socket_from_fd(fd);
    if (!sock) return -STRATUM_EBADF;

    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        return tcp_close(sock->pcb.tcp);
    }

    return 0;
}
