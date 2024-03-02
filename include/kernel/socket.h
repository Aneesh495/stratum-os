#ifndef STRATUM_KERNEL_SOCKET_H
#define STRATUM_KERNEL_SOCKET_H

#include <kernel/net.h>
#include <kernel/tcp.h>
#include <kernel/udp.h>
#include <kernel/file.h>

#define AF_INET         2

#define SOCK_STREAM     1
#define SOCK_DGRAM      2

#define IPPROTO_IP      0
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

typedef uint16_t sa_family_t;
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    uint8_t        sin_zero[8];
};

typedef enum socket_state {
    SOCKET_STATE_UNBOUND = 0,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_LISTENING,
    SOCKET_STATE_CONNECTING,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_DISCONNECTED,
    SOCKET_STATE_CLOSED
} socket_state_t;

typedef struct socket {
    int             domain;
    int             type;
    int             protocol;
    socket_state_t  state;
    bool            nonblocking;

    union {
        tcp_pcb_t *tcp;
        udp_pcb_t *udp;
    } pcb;

    file_t         *file;
    spinlock_t      lock;
} socket_t;

void socket_init(void);

int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const struct sockaddr *addr, uint32_t addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, struct sockaddr *addr, uint32_t *addrlen);
int sys_connect(int fd, const struct sockaddr *addr, uint32_t addrlen);
int64_t sys_send(int fd, const void *buf, size_t len, int flags);
int64_t sys_recv(int fd, void *buf, size_t len, int flags);
int64_t sys_sendto(int fd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, uint32_t addrlen);
int64_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, uint32_t *addrlen);
int sys_shutdown(int fd, int how);

file_t *socket_create_file(socket_t *sock);

#endif /* STRATUM_KERNEL_SOCKET_H */
