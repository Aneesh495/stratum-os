#include <kernel/ethernet.h>
#include <kernel/arp.h>
#include <kernel/ipv4.h>
#include <kernel/string.h>
#include <shared/errno.h>

int ethernet_send(netif_t *nif, pbuf_t *p, const uint8_t *dst_mac, uint16_t ethertype) {
    if (!nif || !p || !dst_mac) return -STRATUM_EINVAL;
    if (!nif->output) return -STRATUM_ENOSYS;
    return nif->output(nif, p, dst_mac, ethertype);
}

int ethernet_input(pbuf_t *p) {
    if (!p || p->len < ETH_HLEN) {
        if (p) pbuf_free(p);
        return -STRATUM_EINVAL;
    }

    eth_hdr_t *eh = (eth_hdr_t *)p->payload;
    netif_t *nif = net_get_default_if();

    /* Filter frames not destined to us or broadcast */
    bool is_bcast = true;
    for (int i = 0; i < ETH_ALEN; i++) {
        if (eh->dest[i] != 0xFF) {
            is_bcast = false;
            break;
        }
    }

    if (!is_bcast && nif && memcmp(eh->dest, nif->mac, ETH_ALEN) != 0) {
        pbuf_free(p);
        return 0;
    }

    uint16_t ethertype = ntohs(eh->type);
    p->payload += ETH_HLEN;
    p->len -= ETH_HLEN;

    int res = 0;
    if (ethertype == ETHERTYPE_ARP) {
        res = arp_input(p);
    } else if (ethertype == ETHERTYPE_IP) {
        res = ipv4_input(p);
    }

    pbuf_free(p);
    return res;
}
