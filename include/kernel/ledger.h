#ifndef STRATUM_KERNEL_LEDGER_H
#define STRATUM_KERNEL_LEDGER_H

#include <shared/ledger.h>
#include <kernel/spinlock.h>

#define LEDGER_MEMPOOL_CAPACITY 256
#define LEDGER_CHAIN_CAPACITY   1024

typedef struct {
    spinlock_t          lock;
    bool                initialized;
    uint64_t            chain_height;
    uint64_t            total_txs;
    ledger_block_t      chain[LEDGER_CHAIN_CAPACITY];
    ledger_tx_t         mempool[LEDGER_MEMPOOL_CAPACITY];
    uint32_t            mempool_count;
    char                storage_path[128];
} ledger_state_t;

/* Kernel Ledger API */
int  ledger_init(const char *storage_path);
void ledger_hash_tx(ledger_tx_t *tx);
int  ledger_submit_tx(uint64_t sender, uint64_t recipient, uint64_t amount,
                      const void *payload, uint32_t payload_len);
int  ledger_create_block(uint64_t timestamp, ledger_block_t *out_block);
int  ledger_append_block(const ledger_block_t *block);
int  ledger_validate_block(const ledger_block_t *block, const ledger_block_t *prev_block);
int  ledger_persist(void);
int  ledger_recover(void);
int  ledger_get_latest_block(ledger_block_t *out_block);
int  ledger_replicate_to_peer(uint32_t peer_ip, uint16_t peer_port);
int  ledger_receive_replication_stream(int client_fd);

#endif /* STRATUM_KERNEL_LEDGER_H */
