#ifndef STRATUM_KERNEL_ARP_H
#define STRATUM_KERNEL_ARP_H

#include <kernel/net.h>

#define ARP_HW_ETHERNET     0x0001
#define ARP_PROTO_IP        0x0800
#define ARP_OP_REQUEST      0x0001
#define ARP_OP_REPLY        0x0002

#define ARP_TABLE_SIZE      64

typedef struct arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[ETH_ALEN];
    uint32_t sender_ip;     /* Network byte order */
    uint8_t  target_mac[ETH_ALEN];
    uint32_t target_ip;     /* Network byte order */
} __attribute__((packed)) arp_hdr_t;

typedef struct arp_entry {
    uint32_t ip;            /* Host byte order */
    uint8_t  mac[ETH_ALEN];
    uint64_t timestamp_ms;
    bool     valid;
} arp_entry_t;

void arp_init(void);
int  arp_input(pbuf_t *p);
int  arp_lookup(uint32_t ip, uint8_t *out_mac);
int  arp_insert(uint32_t ip, const uint8_t *mac);
int  arp_send_request(netif_t *nif, uint32_t target_ip);
int  arp_send_reply(netif_t *nif, uint32_t target_ip, const uint8_t *target_mac);

#endif /* STRATUM_KERNEL_ARP_H */
