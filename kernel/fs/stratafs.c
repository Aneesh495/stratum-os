#include <kernel/stratafs.h>
#include <kernel/slab.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <shared/errno.h>

static stratafs_t g_stratafs;
static bool       g_stratafs_active = false;

static vfs_node_t *stratafs_create_vfs_node(stratafs_t *fs, uint32_t ino, const stratafs_disk_inode_t *dinode, const char *name);
extern const vfs_node_ops_t g_stratafs_node_ops;

int stratafs_read_block(stratafs_t *fs, uint64_t block_num, void *buf) {
    if (!fs || !fs->dev || !buf) return -STRATUM_EINVAL;
    uint64_t sector = block_num * STRATAFS_SECTORS_PER_BLOCK;
    int64_t rd = virtio_blk_read(fs->dev, sector, STRATAFS_SECTORS_PER_BLOCK, buf);
    return (rd == STRATAFS_BLOCK_SIZE) ? 0 : -STRATUM_EIO;
}

int stratafs_write_block(stratafs_t *fs, uint64_t block_num, const void *buf) {
    if (!fs || !fs->dev || !buf) return -STRATUM_EINVAL;
    uint64_t sector = block_num * STRATAFS_SECTORS_PER_BLOCK;
    int64_t wr = virtio_blk_write(fs->dev, sector, STRATAFS_SECTORS_PER_BLOCK, buf);
    return (wr == STRATAFS_BLOCK_SIZE) ? 0 : -STRATUM_EIO;
}

static inline int stratafs_read_block_tx(stratafs_t *fs, journal_tx_t *tx, uint64_t block_num, void *buf) {
    if (tx && journal_read_block(tx, block_num, buf) == 0) {
        return 0;
    }
    return stratafs_read_block(fs, block_num, buf);
}

int stratafs_format(virtio_blk_dev_t *dev) {
    if (!dev) return -STRATUM_EINVAL;

    kprintf("[STRATAFS] Formatting storage device with StrataFS layout...\n");

    uint64_t total_blocks = dev->capacity_sectors / STRATAFS_SECTORS_PER_BLOCK;
    if (total_blocks < 1024) {
        kprintf("[STRATAFS] Device too small for StrataFS (%lu blocks)\n", total_blocks);
        return -STRATUM_ENOSPC;
    }

    /* 1. Initialize Superblock */
    stratafs_sb_t *sb = (stratafs_sb_t *)kmalloc(sizeof(stratafs_sb_t));
    if (!sb) return -STRATUM_ENOMEM;
    memset(sb, 0, sizeof(stratafs_sb_t));

    sb->magic = STRATAFS_MAGIC;
    sb->version = STRATAFS_VERSION;
    sb->block_size = STRATAFS_BLOCK_SIZE;
    sb->total_blocks = total_blocks;
    sb->total_inodes = 2048;
    sb->root_inode = STRATAFS_ROOT_INO;

    sb->block_bitmap_block = 2;
    sb->block_bitmap_blocks = 1;
    sb->inode_bitmap_block = 3;
    sb->inode_bitmap_blocks = 1;
    sb->inode_table_block = 4;
    sb->inode_table_blocks = 64; /* 64 blocks * 32 inodes/block = 2048 inodes */
    sb->journal_start_block = 68;
    sb->journal_block_count = 512;
    sb->data_start_block = 580;
    sb->data_block_count = (uint32_t)(total_blocks - sb->data_start_block);

    sb->free_blocks = sb->data_block_count;
    sb->free_inodes = sb->total_inodes;

    /* 2. Format Journal */
    int res = journal_format(dev, sb->journal_start_block, sb->journal_block_count);
    if (res != 0) {
        kfree(sb);
        return res;
    }

    /* 3. Initialize Block Bitmap (mark metadata blocks 0..580 as used) */
    uint8_t *bitmap = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!bitmap) {
        kfree(sb);
        return -STRATUM_ENOMEM;
    }
    memset(bitmap, 0, STRATAFS_BLOCK_SIZE);

    /* Metadata blocks 0 to 579 are pre-allocated */
    for (uint32_t b = 0; b < sb->data_start_block; b++) {
        bitmap[b / 8] |= (uint8_t)(1 << (b % 8));
    }
    /* Root directory uses block 580 */
    bitmap[sb->data_start_block / 8] |= (uint8_t)(1 << (sb->data_start_block % 8));
    sb->free_blocks--;

    uint64_t bbm_sec = sb->block_bitmap_block * STRATAFS_SECTORS_PER_BLOCK;
    virtio_blk_write(dev, bbm_sec, STRATAFS_SECTORS_PER_BLOCK, bitmap);

    /* 4. Initialize Inode Bitmap (mark inode 0 reserved, inode 1 root directory) */
    memset(bitmap, 0, STRATAFS_BLOCK_SIZE);
    bitmap[0] = 0x03; /* Inodes 0 and 1 marked allocated */
    sb->free_inodes -= 2;

    uint64_t ibm_sec = sb->inode_bitmap_block * STRATAFS_SECTORS_PER_BLOCK;
    virtio_blk_write(dev, ibm_sec, STRATAFS_SECTORS_PER_BLOCK, bitmap);

    /* 5. Initialize Inode Table */
    uint8_t *zero_block = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    memset(zero_block, 0, STRATAFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < sb->inode_table_blocks; i++) {
        uint64_t it_sec = (sb->inode_table_block + i) * STRATAFS_SECTORS_PER_BLOCK;
        virtio_blk_write(dev, it_sec, STRATAFS_SECTORS_PER_BLOCK, zero_block);
    }

    /* Write Root Inode (Inode 1) into Inode Table Block 4 */
    stratafs_disk_inode_t root_inode;
    memset(&root_inode, 0, sizeof(stratafs_disk_inode_t));
    root_inode.mode = VFS_MODE_IFDIR | 0755;
    root_inode.nlink = 2;
    root_inode.size = STRATAFS_BLOCK_SIZE;
    root_inode.blocks = STRATAFS_SECTORS_PER_BLOCK;
    root_inode.direct[0] = sb->data_start_block;

    /* Root directory entry block (580) */
    memset(zero_block, 0, STRATAFS_BLOCK_SIZE);
    stratafs_disk_dirent_t *dot = (stratafs_disk_dirent_t *)zero_block;
    dot->inode_num = STRATAFS_ROOT_INO;
    dot->record_len = sizeof(stratafs_disk_dirent_t);
    dot->name_len = 1;
    dot->file_type = STRATAFS_FT_DIR;
    strcpy(dot->name, ".");

    stratafs_disk_dirent_t *dotdot = (stratafs_disk_dirent_t *)(zero_block + sizeof(stratafs_disk_dirent_t));
    dotdot->inode_num = STRATAFS_ROOT_INO;
    dotdot->record_len = sizeof(stratafs_disk_dirent_t);
    dotdot->name_len = 2;
    dotdot->file_type = STRATAFS_FT_DIR;
    strcpy(dotdot->name, "..");

    uint64_t root_data_sec = sb->data_start_block * STRATAFS_SECTORS_PER_BLOCK;
    virtio_blk_write(dev, root_data_sec, STRATAFS_SECTORS_PER_BLOCK, zero_block);

    /* Write root inode to first inode table block */
    uint64_t it0_sec = sb->inode_table_block * STRATAFS_SECTORS_PER_BLOCK;
    virtio_blk_read(dev, it0_sec, STRATAFS_SECTORS_PER_BLOCK, zero_block);
    memcpy(zero_block + sizeof(stratafs_disk_inode_t), &root_inode, sizeof(stratafs_disk_inode_t));
    virtio_blk_write(dev, it0_sec, STRATAFS_SECTORS_PER_BLOCK, zero_block);

    /* 6. Write Superblock */
    sb->checksum = journal_crc32(sb, sizeof(stratafs_sb_t) - 4008);
    uint64_t sb_sec = 1 * STRATAFS_SECTORS_PER_BLOCK;
    virtio_blk_write(dev, sb_sec, STRATAFS_SECTORS_PER_BLOCK, sb);
    virtio_blk_flush(dev);

    kfree(sb);
    kfree(bitmap);
    kfree(zero_block);

    kprintf("[STRATAFS] Format complete: %lu total blocks, %lu free data blocks, 2048 inodes.\n",
            total_blocks, total_blocks - 581);
    return 0;
}

int stratafs_alloc_block(stratafs_t *fs, journal_tx_t *tx, uint32_t *out_block) {
    if (!fs || !out_block) return -STRATUM_EINVAL;

    uint8_t *bitmap = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!bitmap) return -STRATUM_ENOMEM;

    int res = stratafs_read_block_tx(fs, tx, fs->sb.block_bitmap_block, bitmap);
    if (res != 0) {
        kfree(bitmap);
        return res;
    }

    uint32_t start = fs->sb.data_start_block;
    uint32_t limit = (uint32_t)fs->sb.total_blocks;

    for (uint32_t b = start; b < limit; b++) {
        if (!(bitmap[b / 8] & (1 << (b % 8)))) {
            bitmap[b / 8] |= (uint8_t)(1 << (b % 8));
            fs->sb.free_blocks--;

            journal_write_block(tx, fs->sb.block_bitmap_block, bitmap);

            *out_block = b;
            kfree(bitmap);
            return 0;
        }
    }

    kfree(bitmap);
    return -STRATUM_ENOSPC;
}

int stratafs_free_block(stratafs_t *fs, journal_tx_t *tx, uint32_t block) {
    if (!fs || block < fs->sb.data_start_block || block >= fs->sb.total_blocks) {
        return -STRATUM_EINVAL;
    }

    uint8_t *bitmap = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!bitmap) return -STRATUM_ENOMEM;

    int res = stratafs_read_block_tx(fs, tx, fs->sb.block_bitmap_block, bitmap);
    if (res != 0) {
        kfree(bitmap);
        return res;
    }

    bitmap[block / 8] &= (uint8_t)~(1 << (block % 8));
    fs->sb.free_blocks++;

    journal_write_block(tx, fs->sb.block_bitmap_block, bitmap);
    kfree(bitmap);
    return 0;
}

int stratafs_alloc_inode(stratafs_t *fs, journal_tx_t *tx, uint16_t mode, uint32_t *out_ino) {
    if (!fs || !out_ino) return -STRATUM_EINVAL;

    uint8_t *bitmap = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!bitmap) return -STRATUM_ENOMEM;

    int res = stratafs_read_block_tx(fs, tx, fs->sb.inode_bitmap_block, bitmap);
    if (res != 0) {
        kfree(bitmap);
        return res;
    }

    for (uint32_t ino = 1; ino < fs->sb.total_inodes; ino++) {
        if (!(bitmap[ino / 8] & (1 << (ino % 8)))) {
            bitmap[ino / 8] |= (uint8_t)(1 << (ino % 8));
            fs->sb.free_inodes--;

            journal_write_block(tx, fs->sb.inode_bitmap_block, bitmap);

            stratafs_disk_inode_t dinode;
            memset(&dinode, 0, sizeof(stratafs_disk_inode_t));
            dinode.mode = mode;
            dinode.nlink = (mode & VFS_MODE_IFDIR) ? 2 : 1;
            dinode.atime = timer_get_uptime_ms();
            dinode.mtime = dinode.atime;
            dinode.ctime = dinode.atime;

            stratafs_write_inode(fs, tx, ino, &dinode);

            *out_ino = ino;
            kfree(bitmap);
            return 0;
        }
    }

    kfree(bitmap);
    return -STRATUM_ENOSPC;
}

int stratafs_free_inode(stratafs_t *fs, journal_tx_t *tx, uint32_t ino) {
    if (!fs || ino < 1 || ino >= fs->sb.total_inodes) return -STRATUM_EINVAL;

    uint8_t *bitmap = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!bitmap) return -STRATUM_ENOMEM;

    int res = stratafs_read_block_tx(fs, tx, fs->sb.inode_bitmap_block, bitmap);
    if (res != 0) {
        kfree(bitmap);
        return res;
    }

    bitmap[ino / 8] &= (uint8_t)~(1 << (ino % 8));
    fs->sb.free_inodes++;

    journal_write_block(tx, fs->sb.inode_bitmap_block, bitmap);

    stratafs_disk_inode_t zero_inode;
    memset(&zero_inode, 0, sizeof(stratafs_disk_inode_t));
    stratafs_write_inode(fs, tx, ino, &zero_inode);

    kfree(bitmap);
    return 0;
}

int stratafs_read_inode(stratafs_t *fs, uint32_t ino, stratafs_disk_inode_t *out_inode) {
    if (!fs || ino >= fs->sb.total_inodes || !out_inode) return -STRATUM_EINVAL;

    uint32_t block_idx = fs->sb.inode_table_block + (ino / STRATAFS_INODES_PER_BLOCK);
    uint32_t offset = (ino % STRATAFS_INODES_PER_BLOCK) * STRATAFS_INODE_SIZE;

    uint8_t *buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!buf) return -STRATUM_ENOMEM;

    int res = stratafs_read_block(fs, block_idx, buf);
    if (res == 0) {
        memcpy(out_inode, buf + offset, sizeof(stratafs_disk_inode_t));
    }
    kfree(buf);
    return res;
}

int stratafs_write_inode(stratafs_t *fs, journal_tx_t *tx, uint32_t ino, const stratafs_disk_inode_t *inode) {
    if (!fs || ino >= fs->sb.total_inodes || !inode) return -STRATUM_EINVAL;

    uint32_t block_idx = fs->sb.inode_table_block + (ino / STRATAFS_INODES_PER_BLOCK);
    uint32_t offset = (ino % STRATAFS_INODES_PER_BLOCK) * STRATAFS_INODE_SIZE;

    uint8_t *buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!buf) return -STRATUM_ENOMEM;

    int res = stratafs_read_block_tx(fs, tx, block_idx, buf);
    if (res == 0) {
        memcpy(buf + offset, inode, sizeof(stratafs_disk_inode_t));
        if (tx) {
            journal_write_block(tx, block_idx, buf);
        } else {
            stratafs_write_block(fs, block_idx, buf);
        }
    }
    kfree(buf);
    return res;
}

static int stratafs_get_block_mapping(stratafs_t *fs, journal_tx_t *tx, stratafs_disk_inode_t *inode,
                                      uint32_t file_block_idx, bool allocate, uint32_t *out_phys_block) {
    if (file_block_idx < STRATAFS_NDIR_BLOCKS) {
        if (inode->direct[file_block_idx] == 0) {
            if (!allocate) {
                *out_phys_block = 0;
                return 0;
            }
            uint32_t new_blk = 0;
            int a_res = stratafs_alloc_block(fs, tx, &new_blk);
            if (a_res != 0) return a_res;

            /* Zero the newly allocated block */
            uint8_t *z = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
            if (z) {
                memset(z, 0, STRATAFS_BLOCK_SIZE);
                journal_write_block(tx, new_blk, z);
                kfree(z);
            }

            inode->direct[file_block_idx] = new_blk;
            inode->blocks += STRATAFS_SECTORS_PER_BLOCK;
        }
        *out_phys_block = inode->direct[file_block_idx];
        return 0;
    }

    uint32_t indir_idx = file_block_idx - STRATAFS_NDIR_BLOCKS;
    if (indir_idx < STRATAFS_INDIR_BLOCKS) {
        if (inode->indirect == 0) {
            if (!allocate) {
                *out_phys_block = 0;
                return 0;
            }
            uint32_t indir_blk = 0;
            int a_res = stratafs_alloc_block(fs, tx, &indir_blk);
            if (a_res != 0) return a_res;

            uint8_t *z = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
            if (z) {
                memset(z, 0, STRATAFS_BLOCK_SIZE);
                journal_write_block(tx, indir_blk, z);
                kfree(z);
            }

            inode->indirect = indir_blk;
            inode->blocks += STRATAFS_SECTORS_PER_BLOCK;
        }

        uint32_t *entries = (uint32_t *)kmalloc(STRATAFS_BLOCK_SIZE);
        if (!entries) return -STRATUM_ENOMEM;
        stratafs_read_block_tx(fs, tx, inode->indirect, entries);

        if (entries[indir_idx] == 0) {
            if (!allocate) {
                *out_phys_block = 0;
                kfree(entries);
                return 0;
            }
            uint32_t new_blk = 0;
            int a_res = stratafs_alloc_block(fs, tx, &new_blk);
            if (a_res != 0) {
                kfree(entries);
                return a_res;
            }

            uint8_t *z = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
            if (z) {
                memset(z, 0, STRATAFS_BLOCK_SIZE);
                journal_write_block(tx, new_blk, z);
                kfree(z);
            }

            entries[indir_idx] = new_blk;
            inode->blocks += STRATAFS_SECTORS_PER_BLOCK;
            journal_write_block(tx, inode->indirect, entries);
        }

        *out_phys_block = entries[indir_idx];
        kfree(entries);
        return 0;
    }

    return -STRATUM_EFBIG;
}

int64_t stratafs_read_file(stratafs_t *fs, stratafs_disk_inode_t *inode, uint64_t offset, void *buf, size_t count) {
    if (!fs || !inode || !buf) return -STRATUM_EINVAL;
    if (offset >= inode->size) return 0;

    size_t to_read = count;
    if (offset + to_read > inode->size) {
        to_read = (size_t)(inode->size - offset);
    }

    uint8_t *dst = (uint8_t *)buf;
    size_t bytes_read = 0;
    uint8_t *block_buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!block_buf) return -STRATUM_ENOMEM;

    while (bytes_read < to_read) {
        uint64_t cur_pos = offset + bytes_read;
        uint32_t blk_idx = (uint32_t)(cur_pos / STRATAFS_BLOCK_SIZE);
        uint32_t blk_off = (uint32_t)(cur_pos % STRATAFS_BLOCK_SIZE);
        size_t chunk = STRATAFS_BLOCK_SIZE - blk_off;
        if (chunk > to_read - bytes_read) {
            chunk = to_read - bytes_read;
        }

        uint32_t phys_blk = 0;
        int m_res = stratafs_get_block_mapping(fs, NULL, inode, blk_idx, false, &phys_blk);
        if (m_res != 0) {
            kfree(block_buf);
            return (bytes_read > 0) ? (int64_t)bytes_read : m_res;
        }

        if (phys_blk != 0) {
            stratafs_read_block(fs, phys_blk, block_buf);
            memcpy(dst + bytes_read, block_buf + blk_off, chunk);
        } else {
            memset(dst + bytes_read, 0, chunk); /* Sparse hole */
        }

        bytes_read += chunk;
    }

    kfree(block_buf);
    return (int64_t)bytes_read;
}

int64_t stratafs_write_file(stratafs_t *fs, journal_tx_t *tx, uint32_t ino, stratafs_disk_inode_t *inode,
                            uint64_t offset, const void *buf, size_t count) {
    if (!fs || !tx || !inode || !buf) return -STRATUM_EINVAL;

    const uint8_t *src = (const uint8_t *)buf;
    size_t bytes_written = 0;
    uint8_t *block_buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!block_buf) return -STRATUM_ENOMEM;

    while (bytes_written < count) {
        uint64_t cur_pos = offset + bytes_written;
        uint32_t blk_idx = (uint32_t)(cur_pos / STRATAFS_BLOCK_SIZE);
        uint32_t blk_off = (uint32_t)(cur_pos % STRATAFS_BLOCK_SIZE);
        size_t chunk = STRATAFS_BLOCK_SIZE - blk_off;
        if (chunk > count - bytes_written) {
            chunk = count - bytes_written;
        }

        uint32_t phys_blk = 0;
        int m_res = stratafs_get_block_mapping(fs, tx, inode, blk_idx, true, &phys_blk);
        if (m_res != 0) {
            kfree(block_buf);
            return (bytes_written > 0) ? (int64_t)bytes_written : m_res;
        }

        if (chunk == STRATAFS_BLOCK_SIZE) {
            memcpy(block_buf, src + bytes_written, chunk);
        } else {
            stratafs_read_block_tx(fs, tx, phys_blk, block_buf);
            memcpy(block_buf + blk_off, src + bytes_written, chunk);
        }

        journal_write_block(tx, phys_blk, block_buf);
        bytes_written += chunk;
    }

    if (offset + bytes_written > inode->size) {
        inode->size = offset + bytes_written;
    }
    inode->mtime = timer_get_uptime_ms();
    stratafs_write_inode(fs, tx, ino, inode);

    kfree(block_buf);
    return (int64_t)bytes_written;
}

int stratafs_dir_lookup(stratafs_t *fs, stratafs_disk_inode_t *dir, const char *name, uint32_t *out_ino) {
    if (!fs || !dir || !name || !out_ino) return -STRATUM_EINVAL;

    uint8_t *buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!buf) return -STRATUM_ENOMEM;

    uint32_t num_blocks = (uint32_t)((dir->size + STRATAFS_BLOCK_SIZE - 1) / STRATAFS_BLOCK_SIZE);
    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys_blk = 0;
        if (stratafs_get_block_mapping(fs, NULL, dir, b, false, &phys_blk) != 0 || phys_blk == 0) {
            continue;
        }

        stratafs_read_block(fs, phys_blk, buf);

        uint32_t off = 0;
        while (off < STRATAFS_BLOCK_SIZE) {
            stratafs_disk_dirent_t *de = (stratafs_disk_dirent_t *)(buf + off);
            if (de->record_len == 0) break;

            if (de->inode_num != 0 && strcmp(de->name, name) == 0) {
                *out_ino = de->inode_num;
                kfree(buf);
                return 0;
            }
            off += de->record_len;
        }
    }

    kfree(buf);
    return -STRATUM_ENOENT;
}

int stratafs_dir_add(stratafs_t *fs, journal_tx_t *tx, uint32_t dir_ino, stratafs_disk_inode_t *dir,
                     const char *name, uint32_t ino, uint8_t file_type) {
    if (!fs || !tx || !dir || !name) return -STRATUM_EINVAL;

    uint8_t *buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!buf) return -STRATUM_ENOMEM;

    uint32_t num_blocks = (uint32_t)((dir->size + STRATAFS_BLOCK_SIZE - 1) / STRATAFS_BLOCK_SIZE);
    if (num_blocks == 0) num_blocks = 1;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys_blk = 0;
        int m_res = stratafs_get_block_mapping(fs, tx, dir, b, true, &phys_blk);
        if (m_res != 0) {
            kfree(buf);
            return m_res;
        }

        stratafs_read_block_tx(fs, tx, phys_blk, buf);

        uint32_t off = 0;
        while (off < STRATAFS_BLOCK_SIZE) {
            stratafs_disk_dirent_t *de = (stratafs_disk_dirent_t *)(buf + off);
            if (de->record_len == 0 || de->inode_num == 0) {
                de->inode_num = ino;
                de->record_len = sizeof(stratafs_disk_dirent_t);
                de->name_len = (uint8_t)strlen(name);
                de->file_type = file_type;
                strncpy(de->name, name, sizeof(de->name) - 1);

                journal_write_block(tx, phys_blk, buf);

                if ((b * STRATAFS_BLOCK_SIZE + off + sizeof(stratafs_disk_dirent_t)) > dir->size) {
                    dir->size = b * STRATAFS_BLOCK_SIZE + off + sizeof(stratafs_disk_dirent_t);
                    stratafs_write_inode(fs, tx, dir_ino, dir);
                }

                kfree(buf);
                return 0;
            }
            off += de->record_len;
        }
    }

    kfree(buf);
    return -STRATUM_ENOSPC;
}

int stratafs_dir_remove(stratafs_t *fs, journal_tx_t *tx, uint32_t dir_ino, stratafs_disk_inode_t *dir, const char *name) {
    if (!fs || !tx || !dir || !name) return -STRATUM_EINVAL;

    uint8_t *buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (!buf) return -STRATUM_ENOMEM;

    uint32_t num_blocks = (uint32_t)((dir->size + STRATAFS_BLOCK_SIZE - 1) / STRATAFS_BLOCK_SIZE);
    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys_blk = 0;
        if (stratafs_get_block_mapping(fs, NULL, dir, b, false, &phys_blk) != 0 || phys_blk == 0) {
            continue;
        }

        stratafs_read_block_tx(fs, tx, phys_blk, buf);

        uint32_t off = 0;
        while (off < STRATAFS_BLOCK_SIZE) {
            stratafs_disk_dirent_t *de = (stratafs_disk_dirent_t *)(buf + off);
            if (de->record_len == 0) break;

            if (de->inode_num != 0 && strcmp(de->name, name) == 0) {
                de->inode_num = 0; /* Mark unlinked */
                journal_write_block(tx, phys_blk, buf);
                stratafs_write_inode(fs, tx, dir_ino, dir);
                kfree(buf);
                return 0;
            }
            off += de->record_len;
        }
    }

    kfree(buf);
    return -STRATUM_ENOENT;
}

/* --- VFS Integration --- */

static int stratafs_vfs_lookup(vfs_node_t *parent, const char *name, vfs_node_t **out_child) {
    stratafs_t *fs = (stratafs_t *)parent->mount->fs_priv;
    stratafs_disk_inode_t dinode;
    int res = stratafs_read_inode(fs, (uint32_t)parent->inode_num, &dinode);
    if (res != 0) return res;

    uint32_t child_ino = 0;
    res = stratafs_dir_lookup(fs, &dinode, name, &child_ino);
    if (res != 0) return res;

    stratafs_disk_inode_t child_dinode;
    res = stratafs_read_inode(fs, child_ino, &child_dinode);
    if (res != 0) return res;

    vfs_node_t *node = stratafs_create_vfs_node(fs, child_ino, &child_dinode, name);
    if (!node) return -STRATUM_ENOMEM;

    node->mount = parent->mount;
    *out_child = node;
    return 0;
}

static int stratafs_vfs_create(vfs_node_t *parent, const char *name, uint32_t mode, vfs_node_t **out_child) {
    stratafs_t *fs = (stratafs_t *)parent->mount->fs_priv;

    journal_tx_t *tx = NULL;
    int res = journal_begin(&fs->journal, &tx);
    if (res != 0) return res;

    stratafs_disk_inode_t p_dinode;
    stratafs_read_inode(fs, (uint32_t)parent->inode_num, &p_dinode);

    uint32_t new_ino = 0;
    res = stratafs_alloc_inode(fs, tx, (uint16_t)(VFS_MODE_IFREG | (mode & 0777)), &new_ino);
    if (res != 0) {
        journal_commit(&fs->journal, tx);
        return res;
    }

    res = stratafs_dir_add(fs, tx, (uint32_t)parent->inode_num, &p_dinode, name, new_ino, STRATAFS_FT_REG);
    if (res != 0) {
        stratafs_free_inode(fs, tx, new_ino);
        journal_commit(&fs->journal, tx);
        return res;
    }

    journal_commit(&fs->journal, tx);

    stratafs_disk_inode_t child_dinode;
    stratafs_read_inode(fs, new_ino, &child_dinode);

    vfs_node_t *child = stratafs_create_vfs_node(fs, new_ino, &child_dinode, name);
    if (!child) return -STRATUM_ENOMEM;
    child->mount = parent->mount;

    *out_child = child;
    return 0;
}

static int stratafs_vfs_mkdir(vfs_node_t *parent, const char *name, uint32_t mode, vfs_node_t **out_child) {
    stratafs_t *fs = (stratafs_t *)parent->mount->fs_priv;

    journal_tx_t *tx = NULL;
    int res = journal_begin(&fs->journal, &tx);
    if (res != 0) return res;

    stratafs_disk_inode_t p_dinode;
    stratafs_read_inode(fs, (uint32_t)parent->inode_num, &p_dinode);

    uint32_t new_ino = 0;
    res = stratafs_alloc_inode(fs, tx, (uint16_t)(VFS_MODE_IFDIR | (mode & 0777)), &new_ino);
    if (res != 0) {
        journal_commit(&fs->journal, tx);
        return res;
    }

    /* Allocate one data block for the directory dot and dotdot entries */
    uint32_t dir_blk = 0;
    stratafs_alloc_block(fs, tx, &dir_blk);

    uint8_t *buf = (uint8_t *)kmalloc(STRATAFS_BLOCK_SIZE);
    if (buf) {
        memset(buf, 0, STRATAFS_BLOCK_SIZE);
        stratafs_disk_dirent_t *dot = (stratafs_disk_dirent_t *)buf;
        dot->inode_num = new_ino;
        dot->record_len = sizeof(stratafs_disk_dirent_t);
        dot->name_len = 1;
        dot->file_type = STRATAFS_FT_DIR;
        strcpy(dot->name, ".");

        stratafs_disk_dirent_t *dotdot = (stratafs_disk_dirent_t *)(buf + sizeof(stratafs_disk_dirent_t));
        dotdot->inode_num = (uint32_t)parent->inode_num;
        dotdot->record_len = sizeof(stratafs_disk_dirent_t);
        dotdot->name_len = 2;
        dotdot->file_type = STRATAFS_FT_DIR;
        strcpy(dotdot->name, "..");

        journal_write_block(tx, dir_blk, buf);
        kfree(buf);
    }

    stratafs_disk_inode_t new_dinode;
    stratafs_read_inode(fs, new_ino, &new_dinode);
    new_dinode.direct[0] = dir_blk;
    new_dinode.size = STRATAFS_BLOCK_SIZE;
    new_dinode.blocks = STRATAFS_SECTORS_PER_BLOCK;
    stratafs_write_inode(fs, tx, new_ino, &new_dinode);

    res = stratafs_dir_add(fs, tx, (uint32_t)parent->inode_num, &p_dinode, name, new_ino, STRATAFS_FT_DIR);
    if (res != 0) {
        stratafs_free_inode(fs, tx, new_ino);
        journal_commit(&fs->journal, tx);
        return res;
    }

    p_dinode.nlink++;
    stratafs_write_inode(fs, tx, (uint32_t)parent->inode_num, &p_dinode);

    journal_commit(&fs->journal, tx);

    vfs_node_t *child = stratafs_create_vfs_node(fs, new_ino, &new_dinode, name);
    if (child) {
        child->mount = parent->mount;
    }
    *out_child = child;
    return 0;
}

static int stratafs_vfs_unlink(vfs_node_t *parent, const char *name) {
    stratafs_t *fs = (stratafs_t *)parent->mount->fs_priv;

    journal_tx_t *tx = NULL;
    int res = journal_begin(&fs->journal, &tx);
    if (res != 0) return res;

    stratafs_disk_inode_t p_dinode;
    stratafs_read_inode(fs, (uint32_t)parent->inode_num, &p_dinode);

    uint32_t target_ino = 0;
    res = stratafs_dir_lookup(fs, &p_dinode, name, &target_ino);
    if (res != 0) {
        journal_commit(&fs->journal, tx);
        return res;
    }

    res = stratafs_dir_remove(fs, tx, (uint32_t)parent->inode_num, &p_dinode, name);
    if (res != 0) {
        journal_commit(&fs->journal, tx);
        return res;
    }

    /* Free target inode data blocks */
    stratafs_disk_inode_t t_dinode;
    stratafs_read_inode(fs, target_ino, &t_dinode);

    for (int i = 0; i < STRATAFS_NDIR_BLOCKS; i++) {
        if (t_dinode.direct[i] != 0) {
            stratafs_free_block(fs, tx, t_dinode.direct[i]);
            t_dinode.direct[i] = 0;
        }
    }
    if (t_dinode.indirect != 0) {
        uint32_t *ind = (uint32_t *)kmalloc(STRATAFS_BLOCK_SIZE);
        if (ind) {
            stratafs_read_block(fs, t_dinode.indirect, ind);
            for (uint32_t k = 0; k < STRATAFS_INDIR_BLOCKS; k++) {
                if (ind[k] != 0) stratafs_free_block(fs, tx, ind[k]);
            }
            kfree(ind);
        }
        stratafs_free_block(fs, tx, t_dinode.indirect);
        t_dinode.indirect = 0;
    }

    stratafs_free_inode(fs, tx, target_ino);
    journal_commit(&fs->journal, tx);
    return 0;
}

static int64_t stratafs_vfs_read(vfs_node_t *node, uint64_t offset, void *buf, size_t count) {
    stratafs_t *fs = (stratafs_t *)node->mount->fs_priv;
    stratafs_disk_inode_t dinode;
    stratafs_read_inode(fs, (uint32_t)node->inode_num, &dinode);
    return stratafs_read_file(fs, &dinode, offset, buf, count);
}

static int64_t stratafs_vfs_write(vfs_node_t *node, uint64_t offset, const void *buf, size_t count) {
    stratafs_t *fs = (stratafs_t *)node->mount->fs_priv;
    stratafs_disk_inode_t dinode;
    stratafs_read_inode(fs, (uint32_t)node->inode_num, &dinode);

    journal_tx_t *tx = NULL;
    int res = journal_begin(&fs->journal, &tx);
    if (res != 0) return res;

    int64_t wr = stratafs_write_file(fs, tx, (uint32_t)node->inode_num, &dinode, offset, buf, count);
    journal_commit(&fs->journal, tx);

    if (wr > 0) {
        node->size = dinode.size;
    }
    return wr;
}

static int stratafs_vfs_stat(vfs_node_t *node, vfs_stat_t *out_st) {
    stratafs_t *fs = (stratafs_t *)node->mount->fs_priv;
    stratafs_disk_inode_t dinode;
    int res = stratafs_read_inode(fs, (uint32_t)node->inode_num, &dinode);
    if (res != 0) return res;

    memset(out_st, 0, sizeof(vfs_stat_t));
    out_st->st_ino = node->inode_num;
    out_st->st_mode = dinode.mode;
    out_st->st_nlink = dinode.nlink;
    out_st->st_uid = dinode.uid;
    out_st->st_gid = dinode.gid;
    out_st->st_size = dinode.size;
    out_st->st_blksize = STRATAFS_BLOCK_SIZE;
    out_st->st_blocks = dinode.blocks;
    out_st->st_atime = dinode.atime;
    out_st->st_mtime = dinode.mtime;
    out_st->st_ctime = dinode.ctime;
    return 0;
}

static int stratafs_vfs_sync(vfs_node_t *node) {
    stratafs_t *fs = (stratafs_t *)node->mount->fs_priv;
    return journal_checkpoint(&fs->journal);
}

const vfs_node_ops_t g_stratafs_node_ops = {
    .open     = NULL,
    .close    = NULL,
    .read     = stratafs_vfs_read,
    .write    = stratafs_vfs_write,
    .lookup   = stratafs_vfs_lookup,
    .create   = stratafs_vfs_create,
    .mkdir    = stratafs_vfs_mkdir,
    .unlink   = stratafs_vfs_unlink,
    .readdir  = NULL,
    .stat     = stratafs_vfs_stat,
    .sync     = stratafs_vfs_sync,
    .truncate = NULL,
};

static vfs_node_t *stratafs_create_vfs_node(stratafs_t *fs, uint32_t ino, const stratafs_disk_inode_t *dinode, const char *name) {
    (void)fs;
    vfs_node_t *node = vfs_node_alloc();
    if (!node) return NULL;

    strncpy(node->name, name, sizeof(node->name) - 1);
    node->inode_num = ino;
    node->type = (dinode->mode & VFS_MODE_IFDIR) ? VFS_TYPE_DIR : VFS_TYPE_REG;
    node->mode = dinode->mode;
    node->size = dinode->size;
    node->atime = dinode->atime;
    node->mtime = dinode->mtime;
    node->ctime = dinode->ctime;
    node->ops = &g_stratafs_node_ops;

    return node;
}

int stratafs_mount_fs(vfs_mount_t *mnt, const char *dev_name) {
    (void)dev_name;
    virtio_blk_dev_t *blk = virtio_blk_get_primary();
    if (!blk) {
        kprintf("[STRATAFS] Primary Virtio-blk device not found!\n");
        return -STRATUM_ENODEV;
    }

    stratafs_t *fs = &g_stratafs;
    memset(fs, 0, sizeof(stratafs_t));
    fs->dev = blk;
    spin_lock_init(&fs->lock);
    fs->mount = mnt;

    /* Read Superblock */
    int res = stratafs_read_block(fs, 1, &fs->sb);
    if (res != 0 || fs->sb.magic != STRATAFS_MAGIC) {
        kprintf("[STRATAFS] Superblock invalid or unformatted. Formatting disk...\n");
        res = stratafs_format(blk);
        if (res != 0) return res;

        res = stratafs_read_block(fs, 1, &fs->sb);
        if (res != 0 || fs->sb.magic != STRATAFS_MAGIC) {
            kprintf("[STRATAFS] Failed to read superblock after formatting!\n");
            return -STRATUM_EIO;
        }
    }

    /* Initialize Journal and crash recovery */
    res = journal_init(blk, fs->sb.journal_start_block, fs->sb.journal_block_count, &fs->journal);
    if (res != 0) {
        kprintf("[STRATAFS] Journal initialization failed: %d\n", res);
        return res;
    }

    /* Create Root Node */
    stratafs_disk_inode_t root_dinode;
    res = stratafs_read_inode(fs, STRATAFS_ROOT_INO, &root_dinode);
    if (res != 0) return res;

    vfs_node_t *root_node = stratafs_create_vfs_node(fs, STRATAFS_ROOT_INO, &root_dinode, "/");
    if (!root_node) return -STRATUM_ENOMEM;

    root_node->mount = mnt;
    mnt->root_node = root_node;
    mnt->fs_priv = fs;
    g_stratafs_active = true;

    kprintf("[STRATAFS] Mounted StrataFS on '%s' (%lu MiB volume, %u free blocks)\n",
            mnt->mountpoint, (fs->sb.total_blocks * STRATAFS_BLOCK_SIZE) / (1024 * 1024),
            (uint32_t)fs->sb.free_blocks);
    return 0;
}

static int stratafs_mount_op(vfs_mount_t *mnt, const char *dev) {
    return stratafs_mount_fs(mnt, dev);
}

static int stratafs_unmount_op(vfs_mount_t *mnt) {
    if (!mnt || !mnt->fs_priv) return -STRATUM_EINVAL;
    stratafs_t *fs = (stratafs_t *)mnt->fs_priv;
    journal_checkpoint(&fs->journal);
    g_stratafs_active = false;
    return 0;
}

static int stratafs_sync_op(vfs_mount_t *mnt) {
    if (!mnt || !mnt->fs_priv) return -STRATUM_EINVAL;
    stratafs_t *fs = (stratafs_t *)mnt->fs_priv;
    return journal_checkpoint(&fs->journal);
}

static const vfs_mount_ops_t g_stratafs_mount_ops = {
    .mount   = stratafs_mount_op,
    .unmount = stratafs_unmount_op,
    .sync    = stratafs_sync_op,
    .statfs  = NULL,
};

static vfs_filesystem_t g_stratafs_fs = {
    .name      = "stratafs",
    .mount_ops = &g_stratafs_mount_ops,
    .next      = NULL,
};

int stratafs_init(void) {
    return vfs_register_fs(&g_stratafs_fs);
}

bool stratafs_is_mounted(void) {
    return g_stratafs_active;
}
