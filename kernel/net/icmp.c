#include <kernel/icmp.h>
#include <kernel/ipv4.h>
#include <kernel/string.h>
#include <shared/errno.h>

void icmp_init(void) {
}

int icmp_input(pbuf_t *p, uint32_t src_ip) {
    if (!p || p->len < sizeof(icmp_hdr_t)) return -STRATUM_EINVAL;

    icmp_hdr_t *req = (icmp_hdr_t *)p->payload;
    if (net_checksum(req, p->len) != 0) {
        return -STRATUM_EIO;
    }

    if (req->type == ICMP_TYPE_ECHO_REQUEST) {
        /* Generate Echo Reply */
        pbuf_t *reply_p = pbuf_alloc(p->len);
        if (!reply_p) return -STRATUM_ENOMEM;

        icmp_hdr_t *reply = (icmp_hdr_t *)reply_p->payload;
        reply->type = ICMP_TYPE_ECHO_REPLY;
        reply->code = 0;
        reply->checksum = 0;
        reply->id = req->id;
        reply->sequence = req->sequence;

        /* Copy data payload */
        size_t payload_len = p->len - sizeof(icmp_hdr_t);
        if (payload_len > 0) {
            memcpy(reply_p->payload + sizeof(icmp_hdr_t),
                   p->payload + sizeof(icmp_hdr_t), payload_len);
        }

        reply->checksum = net_checksum(reply, p->len);

        int res = ipv4_output(reply_p, src_ip, IP_PROTO_ICMP);
        pbuf_free(reply_p);
        return res;
    }

    return 0;
}
