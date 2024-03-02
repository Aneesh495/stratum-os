#ifndef STRATUM_KERNEL_ICMP_H
#define STRATUM_KERNEL_ICMP_H

#include <kernel/net.h>

#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

typedef struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_hdr_t;

void icmp_init(void);
int  icmp_input(pbuf_t *p, uint32_t src_ip);

#endif /* STRATUM_KERNEL_ICMP_H */
