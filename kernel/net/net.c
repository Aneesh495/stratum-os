#include <kernel/net.h>
#include <kernel/ethernet.h>
#include <kernel/arp.h>
#include <kernel/ipv4.h>
#include <kernel/icmp.h>
#include <kernel/udp.h>
#include <kernel/tcp.h>
#include <kernel/socket.h>
#include <kernel/virtio_net.h>
#include <kernel/slab.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <shared/errno.h>

static kmem_cache_t *g_pbuf_cache = NULL;
static netif_t       g_default_netif;
static bool          g_net_initialized = false;

uint16_t net_checksum(const void *data, size_t length) {
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;

    while (length > 1) {
        sum += *words++;
        length -= 2;
    }

    if (length > 0) {
        sum += *(const uint8_t *)words;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

uint16_t net_pseudo_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t proto, const void *payload, size_t length) {
    uint32_t sum = 0;

    /* Pseudo header */
    uint32_t s = htonl(src_ip);
    uint32_t d = htonl(dst_ip);
    sum += (s & 0xFFFF) + (s >> 16);
    sum += (d & 0xFFFF) + (d >> 16);
    sum += htons((uint16_t)proto);
    sum += htons((uint16_t)length);

    /* Payload */
    const uint16_t *words = (const uint16_t *)payload;
    size_t rem = length;
    while (rem > 1) {
        sum += *words++;
        rem -= 2;
    }
    if (rem > 0) {
        sum += *(const uint8_t *)words;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

pbuf_t *pbuf_alloc(size_t size) {
    if (size > PBUF_MAX_SIZE) return NULL;
    if (!g_pbuf_cache) return NULL;

    pbuf_t *p = (pbuf_t *)kmem_cache_alloc(g_pbuf_cache);
    if (!p) return NULL;

    memset(p, 0, sizeof(pbuf_t));
    p->payload = p->data;
    p->len = (uint16_t)size;
    p->tot_len = (uint16_t)size;
    p->refcount = 1;
    p->next = NULL;

    return p;
}

void pbuf_ref(pbuf_t *p) {
    if (!p) return;
    __atomic_add_fetch(&p->refcount, 1, __ATOMIC_SEQ_CST);
}

void pbuf_free(pbuf_t *p) {
    if (!p) return;
    if (__atomic_sub_fetch(&p->refcount, 1, __ATOMIC_SEQ_CST) == 0) {
        kmem_cache_free(g_pbuf_cache, p);
    }
}

static int virtio_net_output(netif_t *nif, pbuf_t *p, const uint8_t *dst_mac, uint16_t ethertype) {
    if (!nif || !p || !dst_mac) return -STRATUM_EINVAL;
    virtio_net_dev_t *dev = (virtio_net_dev_t *)nif->driver_priv;
    if (!dev) return -STRATUM_ENODEV;

    /* Build Ethernet Frame */
    uint8_t frame[ETH_FRAME_LEN];
    eth_hdr_t *eh = (eth_hdr_t *)frame;
    memcpy(eh->dest, dst_mac, ETH_ALEN);
    memcpy(eh->src, nif->mac, ETH_ALEN);
    eh->type = htons(ethertype);

    size_t payload_len = p->tot_len;
    if (ETH_HLEN + payload_len > ETH_FRAME_LEN) {
        return -STRATUM_EMSGSIZE;
    }

    memcpy(frame + ETH_HLEN, p->payload, payload_len);
    size_t total_len = ETH_HLEN + payload_len;
    if (total_len < 60) {
        memset(frame + total_len, 0, 60 - total_len);
        total_len = 60;
    }

    int res = virtio_net_transmit(dev, frame, total_len);
    return (res > 0) ? 0 : res;
}

netif_t *net_get_default_if(void) {
    return &g_default_netif;
}

void net_poll(void) {
    virtio_net_dev_t *dev = (virtio_net_dev_t *)g_default_netif.driver_priv;
    if (!dev) return;

    static uint8_t rx_raw[2048];
    while (1) {
        int bytes = virtio_net_receive(dev, rx_raw, sizeof(rx_raw));
        if (bytes <= 0) break;
        if ((size_t)bytes < ETH_HLEN) continue;

        pbuf_t *p = pbuf_alloc((size_t)bytes);
        if (!p) continue;

        memcpy(p->data, rx_raw, (size_t)bytes);
        p->len = (uint16_t)bytes;
        p->tot_len = (uint16_t)bytes;

        ethernet_input(p);
    }
}

void net_init(void) {
    if (g_net_initialized) return;

    g_pbuf_cache = kmem_cache_create("pbuf_cache", sizeof(pbuf_t), 8);
    kassert(g_pbuf_cache != NULL);

    memset(&g_default_netif, 0, sizeof(netif_t));
    strcpy(g_default_netif.name, "eth0");

    virtio_net_dev_t *dev = virtio_net_get_primary();
    if (dev) {
        memcpy(g_default_netif.mac, dev->mac, ETH_ALEN);
        g_default_netif.driver_priv = dev;
        g_default_netif.output = virtio_net_output;
    }

    /* QEMU User Networking defaults: Guest is 10.0.2.15, Gateway is 10.0.2.2 */
    g_default_netif.ip_addr = IP4_ADDR(10, 0, 2, 15);
    g_default_netif.netmask = IP4_ADDR(255, 255, 255, 0);
    g_default_netif.gateway = IP4_ADDR(10, 0, 2, 2);

    arp_init();
    ipv4_init();
    icmp_init();
    udp_init();
    tcp_init();
    socket_init();

    g_net_initialized = true;
    kprintf("[NET] Network subsystem initialized: iface=%s ip=10.0.2.15 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
            g_default_netif.name,
            g_default_netif.mac[0], g_default_netif.mac[1], g_default_netif.mac[2],
            g_default_netif.mac[3], g_default_netif.mac[4], g_default_netif.mac[5]);
}
