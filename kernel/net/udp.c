#include <kernel/udp.h>
#include <kernel/ipv4.h>
#include <kernel/socket.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <shared/errno.h>

static udp_pcb_t  *g_udp_pcbs = NULL;
static spinlock_t  g_udp_lock;
static uint16_t    g_udp_ephemeral_port = 49152;

void udp_init(void) {
    spin_lock_init(&g_udp_lock);
    g_udp_pcbs = NULL;
    g_udp_ephemeral_port = 49152;
}

udp_pcb_t *udp_new(struct socket *sock) {
    udp_pcb_t *pcb = (udp_pcb_t *)kmalloc(sizeof(udp_pcb_t));
    if (!pcb) return NULL;

    memset(pcb, 0, sizeof(udp_pcb_t));
    pcb->sock = sock;

    uint64_t flags;
    spin_lock_irqsave(&g_udp_lock, &flags);
    pcb->next = g_udp_pcbs;
    g_udp_pcbs = pcb;
    spin_unlock_irqrestore(&g_udp_lock, flags);

    return pcb;
}

int udp_bind(udp_pcb_t *pcb, uint16_t port) {
    if (!pcb) return -STRATUM_EINVAL;

    uint64_t flags;
    spin_lock_irqsave(&g_udp_lock, &flags);

    if (port == 0) {
        port = g_udp_ephemeral_port++;
        if (g_udp_ephemeral_port > 65000) g_udp_ephemeral_port = 49152;
    }

    /* Check if port is already in use */
    for (udp_pcb_t *cur = g_udp_pcbs; cur != NULL; cur = cur->next) {
        if (cur != pcb && cur->local_port == port) {
            spin_unlock_irqrestore(&g_udp_lock, flags);
            return -STRATUM_EADDRINUSE;
        }
    }

    pcb->local_port = port;
    spin_unlock_irqrestore(&g_udp_lock, flags);
    return 0;
}

int udp_connect(udp_pcb_t *pcb, uint32_t ip, uint16_t port) {
    if (!pcb) return -STRATUM_EINVAL;
    pcb->remote_ip = ip;
    pcb->remote_port = port;
    if (pcb->local_port == 0) {
        udp_bind(pcb, 0);
    }
    return 0;
}

void udp_close(udp_pcb_t *pcb) {
    if (!pcb) return;

    uint64_t flags;
    spin_lock_irqsave(&g_udp_lock, &flags);

    udp_pcb_t *prev = NULL;
    udp_pcb_t *cur = g_udp_pcbs;
    while (cur) {
        if (cur == pcb) {
            if (prev) prev->next = cur->next;
            else g_udp_pcbs = cur->next;
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    spin_unlock_irqrestore(&g_udp_lock, flags);
    kfree(pcb);
}

int udp_output(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port,
               const void *data, size_t len) {
    if (len > 1472) return -STRATUM_EMSGSIZE;

    size_t udp_len = sizeof(udp_hdr_t) + len;
    pbuf_t *p = pbuf_alloc(udp_len);
    if (!p) return -STRATUM_ENOMEM;

    udp_hdr_t *uh = (udp_hdr_t *)p->payload;
    uh->src_port = htons(src_port);
    uh->dst_port = htons(dst_port);
    uh->length = htons((uint16_t)udp_len);
    uh->checksum = 0;

    if (data && len > 0) {
        memcpy(p->payload + sizeof(udp_hdr_t), data, len);
    }

    netif_t *nif = net_get_default_if();
    uint32_t sip = (src_ip != 0) ? src_ip : (nif ? nif->ip_addr : 0);
    uh->checksum = net_pseudo_checksum(sip, dst_ip, IP_PROTO_UDP, p->payload, udp_len);

    int res = ipv4_output(p, dst_ip, IP_PROTO_UDP);
    pbuf_free(p);
    return res;
}

int udp_input(pbuf_t *p, uint32_t src_ip, uint32_t dst_ip) {
    (void)src_ip;
    (void)dst_ip;
    if (!p || p->len < sizeof(udp_hdr_t)) return -STRATUM_EINVAL;

    udp_hdr_t *uh = (udp_hdr_t *)p->payload;
    uint16_t dst_port = ntohs(uh->dst_port);
    uint16_t length = ntohs(uh->length);

    if (p->len < length || length < sizeof(udp_hdr_t)) {
        return -STRATUM_EINVAL;
    }

    uint64_t flags;
    spin_lock_irqsave(&g_udp_lock, &flags);

    for (udp_pcb_t *cur = g_udp_pcbs; cur != NULL; cur = cur->next) {
        if (cur->local_port == dst_port) {
            /* Matched socket */
            spin_unlock_irqrestore(&g_udp_lock, flags);
            return 0;
        }
    }

    spin_unlock_irqrestore(&g_udp_lock, flags);
    return -STRATUM_ECONNREFUSED;
}
