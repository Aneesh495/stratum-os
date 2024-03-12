#ifndef STRATUM_SHARED_LEDGER_H
#define STRATUM_SHARED_LEDGER_H

#include <stdint.h>
#include <stddef.h>
#include <shared/sha256.h>

#define LEDGER_MAGIC                0x4C454447 /* 'LEDG' */
#define LEDGER_VERSION              1
#define LEDGER_PORT                 9090
#define LEDGER_MAX_TX_PER_BLOCK     32
#define LEDGER_MAX_PAYLOAD          128
#define LEDGER_PATH                 "/strata/ledger.dat"

/* Transaction Structure */
typedef struct ledger_tx {
    uint64_t tx_id;
    uint64_t sender_id;
    uint64_t recipient_id;
    uint64_t amount;
    uint64_t timestamp;
    uint32_t payload_len;
    uint8_t  payload[LEDGER_MAX_PAYLOAD];
    uint8_t  signature[SHA256_DIGEST_SIZE];
    uint8_t  tx_hash[SHA256_DIGEST_SIZE];
} __attribute__((packed)) ledger_tx_t;

/* Block Structure */
typedef struct ledger_block {
    uint32_t    magic;
    uint32_t    version;
    uint64_t    block_index;
    uint64_t    timestamp;
    uint64_t    nonce;
    uint8_t     prev_hash[SHA256_DIGEST_SIZE];
    uint8_t     merkle_root[SHA256_DIGEST_SIZE];
    uint8_t     block_hash[SHA256_DIGEST_SIZE];
    uint32_t    tx_count;
    ledger_tx_t txs[LEDGER_MAX_TX_PER_BLOCK];
} __attribute__((packed)) ledger_block_t;

/* Ledger Header for on-disk file */
typedef struct ledger_file_header {
    uint32_t magic;
    uint32_t version;
    uint64_t block_count;
    uint64_t total_tx_count;
    uint8_t  latest_hash[SHA256_DIGEST_SIZE];
    uint8_t  reserved[64];
} __attribute__((packed)) ledger_file_header_t;

/* Network Replication Message Protocol */
#define LEDGER_NET_MAGIC            0x5354524C /* 'STRL' */

#define LEDGER_MSG_HELLO            1
#define LEDGER_MSG_SUBMIT_TX        2
#define LEDGER_MSG_ANNOUNCE_BLOCK   3
#define LEDGER_MSG_SYNC_REQUEST     4
#define LEDGER_MSG_SYNC_RESPONSE    5
#define LEDGER_MSG_ACK              6

typedef struct ledger_net_hdr {
    uint32_t magic;
    uint16_t msg_type;
    uint16_t flags;
    uint32_t payload_len;
    uint32_t checksum;
} __attribute__((packed)) ledger_net_hdr_t;

typedef struct ledger_msg_hello {
    uint64_t node_id;
    uint64_t latest_block_index;
    uint8_t  latest_block_hash[SHA256_DIGEST_SIZE];
} __attribute__((packed)) ledger_msg_hello_t;

typedef struct ledger_msg_sync_req {
    uint64_t start_block_index;
    uint64_t max_blocks;
} __attribute__((packed)) ledger_msg_sync_req_t;

#endif /* STRATUM_SHARED_LEDGER_H */
