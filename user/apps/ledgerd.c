#include <user/strat_api.h>
#include <shared/ledger.h>

extern void u_printf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

static ledger_block_t g_user_chain[16];
static uint32_t       g_user_chain_len = 0;

int ledgerd_init(void) {
    u_printf("[LEDGERD] Initializing userland distributed ledger daemon on port %d...\n", LEDGER_PORT);
    g_user_chain_len = 0;
    memset(g_user_chain, 0, sizeof(g_user_chain));

    /* Genesis Block */
    ledger_block_t *gen = &g_user_chain[0];
    gen->magic = LEDGER_MAGIC;
    gen->version = LEDGER_VERSION;
    gen->block_index = 0;
    gen->timestamp = 1000;
    gen->nonce = 0xABCD;
    gen->tx_count = 0;
    g_user_chain_len = 1;

    u_printf("[LEDGERD] Genesis block established (index 0). Daemon ready.\n");
    return 0;
}

int ledgerd_add_tx_and_commit(uint64_t sender, uint64_t recipient, uint64_t amount) {
    if (g_user_chain_len >= 16) return -1;

    ledger_block_t *blk = &g_user_chain[g_user_chain_len];
    blk->magic = LEDGER_MAGIC;
    blk->version = LEDGER_VERSION;
    blk->block_index = g_user_chain_len;
    blk->timestamp = 2000 + g_user_chain_len;
    blk->nonce = 100;
    memcpy(blk->prev_hash, g_user_chain[g_user_chain_len - 1].block_hash, 32);

    blk->tx_count = 1;
    ledger_tx_t *tx = &blk->txs[0];
    tx->tx_id = g_user_chain_len;
    tx->sender_id = sender;
    tx->recipient_id = recipient;
    tx->amount = amount;
    tx->timestamp = blk->timestamp;
    tx->payload_len = 16;
    memcpy(tx->payload, "LEDGER_TX_RECORD", 16);

    /* Generate dummy hash & Merkle */
    for (int i = 0; i < 32; i++) {
        tx->tx_hash[i] = (uint8_t)(i ^ g_user_chain_len);
        blk->merkle_root[i] = tx->tx_hash[i];
        blk->block_hash[i] = (uint8_t)(0x5A ^ i ^ g_user_chain_len);
    }

    g_user_chain_len++;
    u_printf("[LEDGERD] Forged block %lu (1 tx, sender=%lu, recipient=%lu, amount=%lu)\n",
             blk->block_index, sender, recipient, amount);
    return 0;
}

int ledgerd_persist_to_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        u_printf("[LEDGERD] Failed to open storage '%s' (err=%d)\n", path, fd);
        return fd;
    }

    ledger_file_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = LEDGER_MAGIC;
    hdr.version = LEDGER_VERSION;
    hdr.block_count = g_user_chain_len;
    hdr.total_tx_count = g_user_chain_len > 0 ? (g_user_chain_len - 1) : 0;

    write(fd, &hdr, sizeof(hdr));
    for (uint32_t i = 0; i < g_user_chain_len; i++) {
        write(fd, &g_user_chain[i], sizeof(ledger_block_t));
    }

    fsync(fd);
    close(fd);
    u_printf("[LEDGERD] Persisted %u blocks to '%s' with fsync durability.\n", g_user_chain_len, path);
    return 0;
}
