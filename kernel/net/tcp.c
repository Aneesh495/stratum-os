#include <kernel/tcp.h>
#include <kernel/ipv4.h>
#include <kernel/socket.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/kernel.h>
#include <shared/errno.h>

static tcp_pcb_t *g_tcp_pcbs = NULL;
static spinlock_t g_tcp_lock;
static uint16_t   g_tcp_ephemeral_port = 49152;
static uint32_t   g_iss_counter = 100000;

void tcp_init(void) {
    spin_lock_init(&g_tcp_lock);
    g_tcp_pcbs = NULL;
    g_tcp_ephemeral_port = 49152;
    g_iss_counter = 100000;
}

tcp_pcb_t *tcp_new(struct socket *sock) {
    tcp_pcb_t *pcb = (tcp_pcb_t *)kmalloc(sizeof(tcp_pcb_t));
    if (!pcb) return NULL;

    memset(pcb, 0, sizeof(tcp_pcb_t));
    pcb->sock = sock;
    pcb->state = TCP_STATE_CLOSED;
    pcb->rcv_wnd = TCP_DEFAULT_WND;
    pcb->snd_wnd = TCP_DEFAULT_WND;
    spin_lock_init(&pcb->lock);

    uint64_t flags;
    spin_lock_irqsave(&g_tcp_lock, &flags);
    pcb->next = g_tcp_pcbs;
    g_tcp_pcbs = pcb;
    spin_unlock_irqrestore(&g_tcp_lock, flags);

    return pcb;
}

int tcp_bind(tcp_pcb_t *pcb, uint16_t port) {
    if (!pcb) return -STRATUM_EINVAL;

    uint64_t flags;
    spin_lock_irqsave(&g_tcp_lock, &flags);

    if (port == 0) {
        port = g_tcp_ephemeral_port++;
        if (g_tcp_ephemeral_port > 65000) g_tcp_ephemeral_port = 49152;
    }

    /* Check port collision */
    for (tcp_pcb_t *c = g_tcp_pcbs; c != NULL; c = c->next) {
        if (c != pcb && c->local_port == port) {
            spin_unlock_irqrestore(&g_tcp_lock, flags);
            return -STRATUM_EADDRINUSE;
        }
    }

    pcb->local_port = port;
    spin_unlock_irqrestore(&g_tcp_lock, flags);
    return 0;
}

int tcp_listen(tcp_pcb_t *pcb, int backlog) {
    if (!pcb) return -STRATUM_EINVAL;
    if (pcb->local_port == 0) {
        int b_res = tcp_bind(pcb, 0);
        if (b_res != 0) return b_res;
    }

    uint64_t flags;
    spin_lock_irqsave(&pcb->lock, &flags);
    pcb->state = TCP_STATE_LISTEN;
    pcb->backlog_capacity = (backlog > 0) ? backlog : 5;
    pcb->backlog_count = 0;
    pcb->backlog_head = NULL;
    spin_unlock_irqrestore(&pcb->lock, flags);

    return 0;
}

int tcp_send_segment(tcp_pcb_t *pcb, uint8_t flags, const void *data, size_t len) {
    if (!pcb) return -STRATUM_EINVAL;

    size_t total_len = sizeof(tcp_hdr_t) + len;
    pbuf_t *p = pbuf_alloc(total_len);
    if (!p) return -STRATUM_ENOMEM;

    tcp_hdr_t *th = (tcp_hdr_t *)p->payload;
    th->src_port = htons(pcb->local_port);
    th->dst_port = htons(pcb->remote_port);
    th->seq_num = htonl(pcb->snd_nxt);
    th->ack_num = htonl(pcb->rcv_nxt);
    th->data_offset_flags = htons((uint16_t)((5 << 12) | flags));
    th->window_size = htons((uint16_t)pcb->rcv_wnd);
    th->checksum = 0;
    th->urgent_ptr = 0;

    if (data && len > 0) {
        memcpy(p->payload + sizeof(tcp_hdr_t), data, len);
    }

    netif_t *nif = net_get_default_if();
    uint32_t sip = (pcb->local_ip != 0) ? pcb->local_ip : (nif ? nif->ip_addr : 0);
    th->checksum = net_pseudo_checksum(sip, pcb->remote_ip, IP_PROTO_TCP, p->payload, total_len);

    int res = ipv4_output(p, pcb->remote_ip, IP_PROTO_TCP);
    pbuf_free(p);
    return res;
}

int tcp_connect(tcp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port) {
    if (!pcb) return -STRATUM_EINVAL;
    if (pcb->local_port == 0) {
        int b_res = tcp_bind(pcb, 0);
        if (b_res != 0) return b_res;
    }

    uint64_t flags;
    spin_lock_irqsave(&pcb->lock, &flags);

    pcb->remote_ip = remote_ip;
    pcb->remote_port = remote_port;
    pcb->snd_nxt = __atomic_fetch_add(&g_iss_counter, 1000, __ATOMIC_RELAXED);
    pcb->snd_una = pcb->snd_nxt;
    pcb->state = TCP_STATE_SYN_SENT;

    /* Transmit SYN */
    tcp_send_segment(pcb, TCP_FLAG_SYN, NULL, 0);
    pcb->snd_nxt++;

    spin_unlock_irqrestore(&pcb->lock, flags);
    return 0;
}

tcp_pcb_t *tcp_accept(tcp_pcb_t *pcb) {
    if (!pcb) return NULL;

    uint64_t flags;
    spin_lock_irqsave(&pcb->lock, &flags);

    if (pcb->backlog_head == NULL) {
        spin_unlock_irqrestore(&pcb->lock, flags);
        return NULL;
    }

    tcp_pcb_t *accepted = pcb->backlog_head;
    pcb->backlog_head = accepted->backlog_next;
    accepted->backlog_next = NULL;
    pcb->backlog_count--;

    spin_unlock_irqrestore(&pcb->lock, flags);
    return accepted;
}

int64_t tcp_send(tcp_pcb_t *pcb, const void *data, size_t len) {
    if (!pcb || !data) return -STRATUM_EINVAL;
    if (pcb->state != TCP_STATE_ESTABLISHED) return -STRATUM_ENOTCONN;

    uint64_t flags;
    spin_lock_irqsave(&pcb->lock, &flags);

    size_t bytes_sent = 0;
    const uint8_t *src = (const uint8_t *)data;

    while (bytes_sent < len) {
        size_t chunk = len - bytes_sent;
        if (chunk > TCP_DEFAULT_MSS) {
            chunk = TCP_DEFAULT_MSS;
        }

        int s_res = tcp_send_segment(pcb, TCP_FLAG_ACK | TCP_FLAG_PSH, src + bytes_sent, chunk);
        if (s_res != 0) {
            spin_unlock_irqrestore(&pcb->lock, flags);
            return (bytes_sent > 0) ? (int64_t)bytes_sent : s_res;
        }

        pcb->snd_nxt += (uint32_t)chunk;
        bytes_sent += chunk;
    }

    spin_unlock_irqrestore(&pcb->lock, flags);
    return (int64_t)bytes_sent;
}

int64_t tcp_recv(tcp_pcb_t *pcb, void *buf, size_t len) {
    if (!pcb || !buf) return -STRATUM_EINVAL;

    uint64_t flags;
    spin_lock_irqsave(&pcb->lock, &flags);

    if (pcb->rx_count == 0) {
        if (pcb->state == TCP_STATE_CLOSE_WAIT || pcb->state == TCP_STATE_CLOSED) {
            spin_unlock_irqrestore(&pcb->lock, flags);
            return 0; /* EOF */
        }
        spin_unlock_irqrestore(&pcb->lock, flags);
        return -STRATUM_EAGAIN;
    }

    size_t to_read = (len < pcb->rx_count) ? len : pcb->rx_count;
    uint8_t *dst = (uint8_t *)buf;

    for (size_t i = 0; i < to_read; i++) {
        dst[i] = pcb->rx_buf[pcb->rx_tail];
        pcb->rx_tail = (pcb->rx_tail + 1) % TCP_RING_BUF_SIZE;
    }

    pcb->rx_count -= (uint32_t)to_read;
    spin_unlock_irqrestore(&pcb->lock, flags);

    return (int64_t)to_read;
}

int tcp_close(tcp_pcb_t *pcb) {
    if (!pcb) return -STRATUM_EINVAL;

    uint64_t flags;
    spin_lock_irqsave(&pcb->lock, &flags);

    if (pcb->state == TCP_STATE_ESTABLISHED) {
        tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        pcb->snd_nxt++;
        pcb->state = TCP_STATE_FIN_WAIT_1;
    } else if (pcb->state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        pcb->snd_nxt++;
        pcb->state = TCP_STATE_LAST_ACK;
    } else {
        pcb->state = TCP_STATE_CLOSED;
    }

    spin_unlock_irqrestore(&pcb->lock, flags);
    return 0;
}

int tcp_input(pbuf_t *p, uint32_t src_ip, uint32_t dst_ip) {
    if (!p || p->len < sizeof(tcp_hdr_t)) return -STRATUM_EINVAL;

    tcp_hdr_t *th = (tcp_hdr_t *)p->payload;
    uint16_t src_port = ntohs(th->src_port);
    uint16_t dst_port = ntohs(th->dst_port);
    uint32_t seq = ntohl(th->seq_num);
    uint32_t ack = ntohl(th->ack_num);
    uint16_t flags_word = ntohs(th->data_offset_flags);
    uint8_t  tcp_flags = (uint8_t)(flags_word & 0x3F);
    uint8_t  doff = (uint8_t)((flags_word >> 12) * 4);

    if (p->len < doff) return -STRATUM_EINVAL;

    /* Checksum verification */
    uint16_t csum = net_pseudo_checksum(src_ip, dst_ip, IP_PROTO_TCP, p->payload, p->len);
    if (csum != 0) {
        return -STRATUM_EIO;
    }

    size_t payload_len = p->len - doff;
    const uint8_t *payload = p->payload + doff;

    uint64_t list_flags;
    spin_lock_irqsave(&g_tcp_lock, &list_flags);

    /* 1. Find exact match (established/connecting) */
    tcp_pcb_t *matched = NULL;
    tcp_pcb_t *listener = NULL;

    for (tcp_pcb_t *c = g_tcp_pcbs; c != NULL; c = c->next) {
        if (c->local_port == dst_port) {
            if (c->remote_port == src_port && c->remote_ip == src_ip) {
                matched = c;
                break;
            }
            if (c->state == TCP_STATE_LISTEN && listener == NULL) {
                listener = c;
            }
        }
    }

    if (!matched && listener && (tcp_flags & TCP_FLAG_SYN)) {
        /* Allocate child connection for listening socket */
        if (listener->backlog_count < listener->backlog_capacity) {
            tcp_pcb_t *child = (tcp_pcb_t *)kmalloc(sizeof(tcp_pcb_t));
            if (child) {
                memset(child, 0, sizeof(tcp_pcb_t));
                child->local_ip = dst_ip;
                child->local_port = dst_port;
                child->remote_ip = src_ip;
                child->remote_port = src_port;
                child->rcv_nxt = seq + 1;
                child->snd_nxt = __atomic_fetch_add(&g_iss_counter, 1000, __ATOMIC_RELAXED);
                child->snd_una = child->snd_nxt;
                child->state = TCP_STATE_SYN_RECEIVED;
                child->rcv_wnd = TCP_DEFAULT_WND;
                child->snd_wnd = TCP_DEFAULT_WND;
                spin_lock_init(&child->lock);

                /* Send SYN-ACK */
                tcp_send_segment(child, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                child->snd_nxt++;

                /* Enqueue to listener backlog */
                child->backlog_next = listener->backlog_head;
                listener->backlog_head = child;
                listener->backlog_count++;

                /* Add to global list */
                child->next = g_tcp_pcbs;
                g_tcp_pcbs = child;
            }
        }
        spin_unlock_irqrestore(&g_tcp_lock, list_flags);
        return 0;
    }

    spin_unlock_irqrestore(&g_tcp_lock, list_flags);

    if (!matched) {
        /* Send RST if packet not destined to active or listening port */
        if (!(tcp_flags & TCP_FLAG_RST)) {
            pbuf_t *rp = pbuf_alloc(sizeof(tcp_hdr_t));
            if (rp) {
                tcp_hdr_t *rh = (tcp_hdr_t *)rp->payload;
                rh->src_port = htons(dst_port);
                rh->dst_port = htons(src_port);
                rh->seq_num = htonl(ack);
                rh->ack_num = htonl(seq + (uint32_t)payload_len + ((tcp_flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) ? 1 : 0));
                rh->data_offset_flags = htons((5 << 12) | TCP_FLAG_RST | TCP_FLAG_ACK);
                rh->window_size = 0;
                rh->checksum = 0;
                rh->urgent_ptr = 0;
                rh->checksum = net_pseudo_checksum(dst_ip, src_ip, IP_PROTO_TCP, rp->payload, sizeof(tcp_hdr_t));
                ipv4_output(rp, src_ip, IP_PROTO_TCP);
                pbuf_free(rp);
            }
        }
        return -STRATUM_ECONNREFUSED;
    }

    uint64_t pcb_flags;
    spin_lock_irqsave(&matched->lock, &pcb_flags);

    switch (matched->state) {
    case TCP_STATE_SYN_SENT:
        if ((tcp_flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            matched->rcv_nxt = seq + 1;
            matched->snd_una = ack;
            matched->state = TCP_STATE_ESTABLISHED;
            /* Send final ACK of 3-way handshake */
            tcp_send_segment(matched, TCP_FLAG_ACK, NULL, 0);
        }
        break;

    case TCP_STATE_SYN_RECEIVED:
        if (tcp_flags & TCP_FLAG_ACK) {
            matched->snd_una = ack;
            matched->state = TCP_STATE_ESTABLISHED;
        }
        break;

    case TCP_STATE_ESTABLISHED:
        if (tcp_flags & TCP_FLAG_ACK) {
            matched->snd_una = ack;
        }

        /* Ingest payload data */
        if (payload_len > 0) {
            for (size_t i = 0; i < payload_len; i++) {
                if (matched->rx_count < TCP_RING_BUF_SIZE) {
                    matched->rx_buf[matched->rx_head] = payload[i];
                    matched->rx_head = (matched->rx_head + 1) % TCP_RING_BUF_SIZE;
                    matched->rx_count++;
                }
            }
            matched->rcv_nxt += (uint32_t)payload_len;
            /* Acknowledge data */
            tcp_send_segment(matched, TCP_FLAG_ACK, NULL, 0);
        }

        /* Handle FIN teardown */
        if (tcp_flags & TCP_FLAG_FIN) {
            matched->rcv_nxt += 1;
            tcp_send_segment(matched, TCP_FLAG_ACK, NULL, 0);
            matched->state = TCP_STATE_CLOSE_WAIT;
        }
        break;

    case TCP_STATE_FIN_WAIT_1:
        if (tcp_flags & TCP_FLAG_ACK) {
            matched->state = TCP_STATE_FIN_WAIT_2;
        }
        if (tcp_flags & TCP_FLAG_FIN) {
            matched->rcv_nxt += 1;
            tcp_send_segment(matched, TCP_FLAG_ACK, NULL, 0);
            matched->state = (matched->state == TCP_STATE_FIN_WAIT_2) ? TCP_STATE_TIME_WAIT : TCP_STATE_CLOSING;
        }
        break;

    case TCP_STATE_FIN_WAIT_2:
        if (tcp_flags & TCP_FLAG_FIN) {
            matched->rcv_nxt += 1;
            tcp_send_segment(matched, TCP_FLAG_ACK, NULL, 0);
            matched->state = TCP_STATE_TIME_WAIT;
        }
        break;

    case TCP_STATE_LAST_ACK:
        if (tcp_flags & TCP_FLAG_ACK) {
            matched->state = TCP_STATE_CLOSED;
        }
        break;

    default:
        break;
    }

    spin_unlock_irqrestore(&matched->lock, pcb_flags);
    return 0;
}
