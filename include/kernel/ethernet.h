#ifndef STRATUM_KERNEL_ETHERNET_H
#define STRATUM_KERNEL_ETHERNET_H

#include <kernel/net.h>

typedef struct eth_hdr {
    uint8_t  dest[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

int ethernet_send(netif_t *nif, pbuf_t *p, const uint8_t *dst_mac, uint16_t ethertype);
int ethernet_input(pbuf_t *p);

#endif /* STRATUM_KERNEL_ETHERNET_H */
