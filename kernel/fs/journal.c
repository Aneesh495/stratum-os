#include <kernel/journal.h>
#include <kernel/slab.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <shared/errno.h>

static uint32_t s_crc32_table[256];
static bool     s_crc32_initialized = false;

static void crc32_init_table(void) {
    if (s_crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = 0xEDB88320U ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = true;
}

uint32_t journal_crc32(const void *data, size_t length) {
    if (!s_crc32_initialized) {
        crc32_init_table();
    }
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        c = s_crc32_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

static int journal_read_raw_block(journal_t *j, uint64_t blk_idx, void *buf) {
    uint64_t sector = (j->start_block + blk_idx) * (JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE);
    int64_t rd = virtio_blk_read(j->dev, sector, JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE, buf);
    return (rd == JOURNAL_BLOCK_SIZE) ? 0 : -STRATUM_EIO;
}

static int journal_write_raw_block(journal_t *j, uint64_t blk_idx, const void *buf) {
    uint64_t sector = (j->start_block + blk_idx) * (JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE);
    int64_t wr = virtio_blk_write(j->dev, sector, JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE, buf);
    return (wr == JOURNAL_BLOCK_SIZE) ? 0 : -STRATUM_EIO;
}

static uint32_t next_journal_slot(journal_t *j, uint32_t slot) {
    slot++;
    if (slot >= j->block_count) {
        slot = 1; /* Slot 0 is reserved for journal superblock */
    }
    return slot;
}

int journal_format(virtio_blk_dev_t *dev, uint64_t start_block, uint32_t block_count) {
    if (!dev || block_count < 8) return -STRATUM_EINVAL;

    journal_sb_t *sb = (journal_sb_t *)kmalloc(sizeof(journal_sb_t));
    if (!sb) return -STRATUM_ENOMEM;

    memset(sb, 0, sizeof(journal_sb_t));
    sb->magic = JOURNAL_MAGIC_SB;
    sb->block_size = JOURNAL_BLOCK_SIZE;
    sb->total_blocks = block_count;
    sb->head_block = 1;
    sb->tail_block = 1;
    sb->sequence_num = 1;
    sb->checksum = journal_crc32(sb, sizeof(journal_sb_t) - 4056);

    uint64_t sector = start_block * (JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE);
    int64_t wr = virtio_blk_write(dev, sector, JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE, sb);
    virtio_blk_flush(dev);

    kfree(sb);
    return (wr == JOURNAL_BLOCK_SIZE) ? 0 : -STRATUM_EIO;
}

int journal_init(virtio_blk_dev_t *dev, uint64_t start_block, uint32_t block_count, journal_t *j) {
    if (!dev || !j || block_count < 8) return -STRATUM_EINVAL;

    memset(j, 0, sizeof(journal_t));
    j->dev = dev;
    j->start_block = start_block;
    j->block_count = block_count;
    spin_lock_init(&j->lock);

    int res = journal_read_raw_block(j, 0, &j->sb);
    if (res != 0 || j->sb.magic != JOURNAL_MAGIC_SB) {
        kprintf("[JOURNAL] No valid journal found. Formatting journal area (%u blocks)...\n", block_count);
        res = journal_format(dev, start_block, block_count);
        if (res != 0) return res;

        res = journal_read_raw_block(j, 0, &j->sb);
        if (res != 0 || j->sb.magic != JOURNAL_MAGIC_SB) {
            kprintf("[JOURNAL] Failed to initialize journal superblock!\n");
            return -STRATUM_EIO;
        }
    }

    j->active = true;

    /* Replay any uncheckpointed transactions from prior session / crash */
    int replayed = journal_recover(j);
    if (replayed > 0) {
        kprintf("[JOURNAL] Crash recovery successfully replayed %d transaction(s).\n", replayed);
    }

    kprintf("[JOURNAL] Journal active: start=%lu count=%u head=%u tail=%u seq=%lu\n",
            j->start_block, j->block_count, j->sb.head_block, j->sb.tail_block, j->sb.sequence_num);
    return 0;
}

int journal_begin(journal_t *j, journal_tx_t **out_tx) {
    if (!j || !out_tx) return -STRATUM_EINVAL;

    journal_tx_t *tx = (journal_tx_t *)kmalloc(sizeof(journal_tx_t));
    if (!tx) return -STRATUM_ENOMEM;

    memset(tx, 0, sizeof(journal_tx_t));
    uint64_t flags;
    spin_lock_irqsave(&j->lock, &flags);
    tx->sequence_num = j->sb.sequence_num++;
    spin_unlock_irqrestore(&j->lock, flags);

    *out_tx = tx;
    return 0;
}

int journal_write_block(journal_tx_t *tx, uint64_t target_block, const void *data) {
    if (!tx || !data) return -STRATUM_EINVAL;

    /* Check if target block already exists in this transaction */
    for (uint32_t i = 0; i < tx->num_blocks; i++) {
        if (tx->blocks[i].target_block == target_block) {
            memcpy(tx->blocks[i].data, data, JOURNAL_BLOCK_SIZE);
            return 0;
        }
    }

    if (tx->num_blocks >= JOURNAL_MAX_TX_BLOCKS) {
        return -STRATUM_ENOMEM;
    }

    tx->blocks[tx->num_blocks].target_block = target_block;
    memcpy(tx->blocks[tx->num_blocks].data, data, JOURNAL_BLOCK_SIZE);
    tx->num_blocks++;
    return 0;
}

int journal_read_block(journal_tx_t *tx, uint64_t target_block, void *buf) {
    if (!tx || !buf) return -STRATUM_EINVAL;

    for (int i = (int)tx->num_blocks - 1; i >= 0; i--) {
        if (tx->blocks[i].target_block == target_block) {
            memcpy(buf, tx->blocks[i].data, JOURNAL_BLOCK_SIZE);
            return 0;
        }
    }
    return -STRATUM_ENOENT;
}

int journal_commit(journal_t *j, journal_tx_t *tx) {
    if (!j || !tx) return -STRATUM_EINVAL;
    if (tx->num_blocks == 0) {
        kfree(tx);
        return 0;
    }

    uint64_t flags;
    spin_lock_irqsave(&j->lock, &flags);

    /* 1. Prepare Descriptor Block */
    journal_desc_block_t *desc = (journal_desc_block_t *)kmalloc(sizeof(journal_desc_block_t));
    if (!desc) {
        spin_unlock_irqrestore(&j->lock, flags);
        kfree(tx);
        return -STRATUM_ENOMEM;
    }
    memset(desc, 0, sizeof(journal_desc_block_t));
    desc->magic = JOURNAL_MAGIC_DESC;
    desc->sequence_num = tx->sequence_num;
    desc->num_blocks = tx->num_blocks;
    for (uint32_t i = 0; i < tx->num_blocks; i++) {
        desc->target_blocks[i] = tx->blocks[i].target_block;
    }

    /* 2. Write descriptor block to journal log */
    uint32_t slot = j->sb.tail_block;
    journal_write_raw_block(j, slot, desc);
    slot = next_journal_slot(j, slot);

    /* 3. Write payload blocks to journal log */
    uint32_t combined_crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < tx->num_blocks; i++) {
        journal_write_raw_block(j, slot, tx->blocks[i].data);
        uint32_t block_crc = journal_crc32(tx->blocks[i].data, JOURNAL_BLOCK_SIZE);
        combined_crc ^= block_crc;
        slot = next_journal_slot(j, slot);
    }

    /* Flush payload blocks to disk before writing commit block */
    virtio_blk_flush(j->dev);

    /* 4. Prepare and write Commit Block */
    journal_commit_block_t *commit = (journal_commit_block_t *)kmalloc(sizeof(journal_commit_block_t));
    if (!commit) {
        kfree(desc);
        spin_unlock_irqrestore(&j->lock, flags);
        kfree(tx);
        return -STRATUM_ENOMEM;
    }
    memset(commit, 0, sizeof(journal_commit_block_t));
    commit->magic = JOURNAL_MAGIC_COMMIT;
    commit->sequence_num = tx->sequence_num;
    commit->data_crc32 = combined_crc;

    journal_write_raw_block(j, slot, commit);
    slot = next_journal_slot(j, slot);

    /* Flush commit block (transaction is now durably committed!) */
    virtio_blk_flush(j->dev);

    /* 5. Checkpoint: Write payload blocks to their actual locations on disk */
    for (uint32_t i = 0; i < tx->num_blocks; i++) {
        uint64_t target_sec = tx->blocks[i].target_block * (JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE);
        virtio_blk_write(j->dev, target_sec, JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE, tx->blocks[i].data);
    }
    virtio_blk_flush(j->dev);

    /* 6. Advance journal tail and head in superblock */
    j->sb.tail_block = slot;
    j->sb.head_block = slot;
    journal_write_raw_block(j, 0, &j->sb);
    virtio_blk_flush(j->dev);

    kfree(desc);
    kfree(commit);
    spin_unlock_irqrestore(&j->lock, flags);

    kfree(tx);
    return 0;
}

int journal_checkpoint(journal_t *j) {
    if (!j) return -STRATUM_EINVAL;

    uint64_t flags;
    spin_lock_irqsave(&j->lock, &flags);
    j->sb.head_block = j->sb.tail_block;
    journal_write_raw_block(j, 0, &j->sb);
    virtio_blk_flush(j->dev);
    spin_unlock_irqrestore(&j->lock, flags);
    return 0;
}

int journal_recover(journal_t *j) {
    if (!j) return -STRATUM_EINVAL;

    uint32_t cur_slot = j->sb.head_block;
    uint32_t end_slot = j->sb.tail_block;
    int replayed_count = 0;

    journal_desc_block_t *desc = (journal_desc_block_t *)kmalloc(sizeof(journal_desc_block_t));
    journal_commit_block_t *commit = (journal_commit_block_t *)kmalloc(sizeof(journal_commit_block_t));
    uint8_t *payload = (uint8_t *)kmalloc(JOURNAL_BLOCK_SIZE);

    if (!desc || !commit || !payload) {
        if (desc) kfree(desc);
        if (commit) kfree(commit);
        if (payload) kfree(payload);
        return -STRATUM_ENOMEM;
    }

    while (cur_slot != end_slot) {
        int res = journal_read_raw_block(j, cur_slot, desc);
        if (res != 0 || desc->magic != JOURNAL_MAGIC_DESC) {
            break; /* No further valid transactions */
        }

        uint32_t num_blocks = desc->num_blocks;
        if (num_blocks > JOURNAL_MAX_TX_BLOCKS) {
            break;
        }

        /* Locate commit block slot */
        uint32_t commit_slot = cur_slot;
        for (uint32_t b = 0; b <= num_blocks; b++) {
            commit_slot = next_journal_slot(j, commit_slot);
        }

        res = journal_read_raw_block(j, commit_slot, commit);
        if (res != 0 || commit->magic != JOURNAL_MAGIC_COMMIT ||
            commit->sequence_num != desc->sequence_num) {
            /* Uncommitted/partial transaction during crash: stop replay */
            break;
        }

        /* Valid committed transaction: replay each block */
        uint32_t block_slot = next_journal_slot(j, cur_slot);
        for (uint32_t i = 0; i < num_blocks; i++) {
            journal_read_raw_block(j, block_slot, payload);
            uint64_t target_sec = desc->target_blocks[i] * (JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE);
            virtio_blk_write(j->dev, target_sec, JOURNAL_BLOCK_SIZE / VIRTIO_BLK_SECTOR_SIZE, payload);
            block_slot = next_journal_slot(j, block_slot);
        }

        virtio_blk_flush(j->dev);
        replayed_count++;

        cur_slot = next_journal_slot(j, commit_slot);
    }

    /* Reset journal to clean state */
    j->sb.head_block = 1;
    j->sb.tail_block = 1;
    journal_write_raw_block(j, 0, &j->sb);
    virtio_blk_flush(j->dev);

    kfree(desc);
    kfree(commit);
    kfree(payload);

    return replayed_count;
}
