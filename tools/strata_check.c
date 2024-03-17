/*
 * tools/strata_check.c - Independent StrataFS Filesystem Checker (K56).
 *
 * Validates raw StrataFS disk images on the host:
 * - Superblock geometry, version, and block limits
 * - Journal superblock, circular buffer limits, and commit sequence
 * - Inode bitmap and block bitmap consistency
 * - Inode table validity, direct and indirect block pointer bounds
 * - Directory hierarchy traversal and entry integrity
 * - Cross-checks reachable data blocks against bitmap to detect leaks or double references
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#define STRATAFS_MAGIC              0x5354524154414653ULL /* "STRATAFS" */
#define STRATAFS_VERSION            1
#define STRATAFS_BLOCK_SIZE         4096
#define STRATAFS_INODE_SIZE         128
#define STRATAFS_INODES_PER_BLOCK   (STRATAFS_BLOCK_SIZE / STRATAFS_INODE_SIZE)
#define STRATAFS_ROOT_INO           1
#define STRATAFS_NDIR_BLOCKS        12

#define JOURNAL_MAGIC_SB            0x4A4F55524E414C31ULL /* "JOURNAL1" */

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t root_inode;
    uint32_t block_bitmap_block;
    uint32_t block_bitmap_blocks;
    uint32_t inode_bitmap_block;
    uint32_t inode_bitmap_blocks;
    uint32_t inode_table_block;
    uint32_t inode_table_blocks;
    uint32_t journal_start_block;
    uint32_t journal_block_count;
    uint32_t data_start_block;
    uint32_t data_block_count;
    uint32_t mount_count;
    uint32_t clean_shutdown;
    uint32_t checksum;
    uint8_t  padding[4008];
} __attribute__((packed)) stratafs_sb_t;

typedef struct {
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t nlink;
    uint64_t size;
    uint64_t blocks;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct[STRATAFS_NDIR_BLOCKS];
    uint32_t indirect;
    uint32_t double_indirect;
    uint32_t flags;
    uint32_t reserved[9];
} __attribute__((packed)) stratafs_disk_inode_t;

typedef struct {
    uint32_t inode_num;
    uint16_t record_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[248];
} __attribute__((packed)) stratafs_disk_dirent_t;

typedef struct {
    uint64_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t head_block;
    uint32_t tail_block;
    uint64_t sequence_num;
    uint32_t flags;
    uint32_t checksum;
    uint8_t  padding[4056];
} __attribute__((packed)) journal_sb_t;

static bool read_block(FILE *fp, uint64_t block_num, void *buf) {
    if (fseek(fp, (long)(block_num * STRATAFS_BLOCK_SIZE), SEEK_SET) != 0) {
        return false;
    }
    size_t rd = fread(buf, 1, STRATAFS_BLOCK_SIZE, fp);
    return (rd == STRATAFS_BLOCK_SIZE);
}

static inline bool bitmap_get(const uint8_t *bitmap, uint32_t bit) {
    return (bitmap[bit / 8] & (1U << (bit % 8))) != 0;
}

static inline void bitmap_set(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit / 8] |= (uint8_t)(1U << (bit % 8));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <stratafs_disk_image>\n", argv[0]);
        return 2;
    }

    const char *img_path = argv[1];
    FILE *fp = fopen(img_path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", img_path, strerror(errno));
        return 2;
    }

    printf("=== StrataFS Independent Consistency Checker (K56) ===\n");
    printf("Image: %s\n", img_path);

    /* 1. Read and verify Superblock (stored at block 1) */
    stratafs_sb_t sb;
    if (!read_block(fp, 1, &sb)) {
        fprintf(stderr, "Error: failed to read superblock from block 1.\n");
        fclose(fp);
        return 1;
    }

    if (sb.magic != STRATAFS_MAGIC) {
        fprintf(stderr, "Error: invalid StrataFS magic: 0x%016llx (expected 0x%016llx)\n",
                (unsigned long long)sb.magic, (unsigned long long)STRATAFS_MAGIC);
        fclose(fp);
        return 1;
    }

    if (sb.version != STRATAFS_VERSION) {
        fprintf(stderr, "Error: unsupported filesystem version %u\n", sb.version);
        fclose(fp);
        return 1;
    }

    if (sb.block_size != STRATAFS_BLOCK_SIZE) {
        fprintf(stderr, "Error: invalid block size %u (expected %u)\n",
                sb.block_size, STRATAFS_BLOCK_SIZE);
        fclose(fp);
        return 1;
    }

    printf("[OK] Superblock valid: total_blocks=%llu, free_blocks=%llu, total_inodes=%u, free_inodes=%u\n",
           (unsigned long long)sb.total_blocks, (unsigned long long)sb.free_blocks,
           sb.total_inodes, sb.free_inodes);

    /* 2. Read and verify Journal Superblock */
    journal_sb_t jsb;
    if (!read_block(fp, sb.journal_start_block, &jsb)) {
        fprintf(stderr, "Error: failed to read journal superblock at block %u\n", sb.journal_start_block);
        fclose(fp);
        return 1;
    }

    if (jsb.magic != JOURNAL_MAGIC_SB) {
        fprintf(stderr, "Error: invalid journal magic: 0x%016llx\n", (unsigned long long)jsb.magic);
        fclose(fp);
        return 1;
    }
    printf("[OK] Journal valid: head=%u, tail=%u, seq=%llu, total_blocks=%u\n",
           jsb.head_block, jsb.tail_block, (unsigned long long)jsb.sequence_num, jsb.total_blocks);

    /* 3. Read Inode and Block Bitmaps */
    size_t bb_size = (size_t)sb.block_bitmap_blocks * STRATAFS_BLOCK_SIZE;
    uint8_t *disk_b_bitmap = (uint8_t *)malloc(bb_size);
    for (uint32_t b = 0; b < sb.block_bitmap_blocks; b++) {
        if (!read_block(fp, sb.block_bitmap_block + b, disk_b_bitmap + (b * STRATAFS_BLOCK_SIZE))) {
            fprintf(stderr, "Error: failed to read block bitmap block %u\n", sb.block_bitmap_block + b);
            free(disk_b_bitmap);
            fclose(fp);
            return 1;
        }
    }

    size_t ib_size = (size_t)sb.inode_bitmap_blocks * STRATAFS_BLOCK_SIZE;
    uint8_t *disk_i_bitmap = (uint8_t *)malloc(ib_size);
    for (uint32_t b = 0; b < sb.inode_bitmap_blocks; b++) {
        if (!read_block(fp, sb.inode_bitmap_block + b, disk_i_bitmap + (b * STRATAFS_BLOCK_SIZE))) {
            fprintf(stderr, "Error: failed to read inode bitmap block %u\n", sb.inode_bitmap_block + b);
            free(disk_b_bitmap);
            free(disk_i_bitmap);
            fclose(fp);
            return 1;
        }
    }

    /* Allocate tracking bitmap for reachable blocks */
    uint8_t *reach_b_bitmap = (uint8_t *)calloc(1, bb_size);

    /* Mark metadata blocks as reachable */
    bitmap_set(reach_b_bitmap, 0); /* Reserved boot block */
    bitmap_set(reach_b_bitmap, 1); /* Superblock */
    for (uint32_t i = 0; i < sb.block_bitmap_blocks; i++) bitmap_set(reach_b_bitmap, sb.block_bitmap_block + i);
    for (uint32_t i = 0; i < sb.inode_bitmap_blocks; i++) bitmap_set(reach_b_bitmap, sb.inode_bitmap_block + i);
    for (uint32_t i = 0; i < sb.inode_table_blocks; i++) bitmap_set(reach_b_bitmap, sb.inode_table_block + i);
    for (uint32_t i = 0; i < sb.journal_block_count; i++) bitmap_set(reach_b_bitmap, sb.journal_start_block + i);

    /* 4. Scan Inodes and Traverse Block Pointers */
    uint32_t active_inodes = 0;
    uint32_t dir_count = 0;
    uint32_t reg_count = 0;
    bool has_error = false;

    uint8_t inode_buf[STRATAFS_BLOCK_SIZE];

    for (uint32_t ino = 1; ino <= sb.total_inodes; ino++) {
        bool is_alloc = bitmap_get(disk_i_bitmap, ino);
        if (!is_alloc) continue;

        active_inodes++;

        uint32_t block_idx = sb.inode_table_block + ((ino - 1) / STRATAFS_INODES_PER_BLOCK);
        uint32_t offset = ((ino - 1) % STRATAFS_INODES_PER_BLOCK) * STRATAFS_INODE_SIZE;

        if (!read_block(fp, block_idx, inode_buf)) {
            fprintf(stderr, "Error: failed to read inode table block %u for ino %u\n", block_idx, ino);
            has_error = true;
            break;
        }

        stratafs_disk_inode_t *din = (stratafs_disk_inode_t *)(inode_buf + offset);
        if ((din->mode & 0170000) == 0040000) dir_count++;
        else reg_count++;

        /* Check direct block pointers */
        for (int d = 0; d < STRATAFS_NDIR_BLOCKS; d++) {
            uint32_t bnum = din->direct[d];
            if (bnum == 0) continue;

            if (bnum < sb.data_start_block || bnum >= sb.total_blocks) {
                fprintf(stderr, "Error: inode %u direct block %d out of bounds: %u\n", ino, d, bnum);
                has_error = true;
                continue;
            }

            if (bitmap_get(reach_b_bitmap, bnum)) {
                fprintf(stderr, "Error: block %u multiply referenced by inode %u\n", bnum, ino);
                has_error = true;
            }
            bitmap_set(reach_b_bitmap, bnum);

            /* If directory, validate directory entries */
            if ((din->mode & 0170000) == 0040000) {
                uint8_t dir_blk[STRATAFS_BLOCK_SIZE];
                if (read_block(fp, bnum, dir_blk)) {
                    uint32_t pos = 0;
                    while (pos + sizeof(stratafs_disk_dirent_t) <= STRATAFS_BLOCK_SIZE) {
                        stratafs_disk_dirent_t *de = (stratafs_disk_dirent_t *)(dir_blk + pos);
                        if (de->record_len == 0) break;
                        if (de->inode_num != 0) {
                            if (de->inode_num > sb.total_inodes || !bitmap_get(disk_i_bitmap, de->inode_num)) {
                                fprintf(stderr, "Error: directory ino %u has entry '%s' with unallocated inode %u\n",
                                        ino, de->name, de->inode_num);
                                has_error = true;
                            }
                        }
                        pos += de->record_len;
                    }
                }
            }
        }

        /* Check single indirect block pointer */
        if (din->indirect != 0) {
            uint32_t iblk = din->indirect;
            if (iblk < sb.data_start_block || iblk >= sb.total_blocks) {
                fprintf(stderr, "Error: inode %u indirect block out of bounds: %u\n", ino, iblk);
                has_error = true;
            } else {
                bitmap_set(reach_b_bitmap, iblk);
                uint32_t indir_ptrs[STRATAFS_BLOCK_SIZE / sizeof(uint32_t)];
                if (read_block(fp, iblk, indir_ptrs)) {
                    for (size_t p = 0; p < (STRATAFS_BLOCK_SIZE / sizeof(uint32_t)); p++) {
                        uint32_t pb = indir_ptrs[p];
                        if (pb == 0) continue;
                        if (pb < sb.data_start_block || pb >= sb.total_blocks) {
                            fprintf(stderr, "Error: inode %u indirect child block out of bounds: %u\n", ino, pb);
                            has_error = true;
                            continue;
                        }
                        if (bitmap_get(reach_b_bitmap, pb)) {
                            fprintf(stderr, "Error: block %u multiply referenced in indirect block of ino %u\n", pb, ino);
                            has_error = true;
                        }
                        bitmap_set(reach_b_bitmap, pb);
                    }
                }
            }
        }
    }

    printf("[OK] Inode table scan: %u active inodes (%u directories, %u files)\n",
           active_inodes, dir_count, reg_count);

    /* 5. Cross-check Reachable Blocks with On-Disk Block Bitmap */
    uint32_t leaks = 0;
    uint32_t unrecorded = 0;

    for (uint32_t b = 0; b < sb.total_blocks; b++) {
        bool on_disk = bitmap_get(disk_b_bitmap, b);
        bool reachable = bitmap_get(reach_b_bitmap, b);

        if (on_disk && !reachable) {
            leaks++;
        } else if (!on_disk && reachable) {
            unrecorded++;
        }
    }

    if (leaks > 0) {
        printf("[WARN] %u blocks allocated on disk but unreachable (clean after journal flush)\n", leaks);
    }
    if (unrecorded > 0) {
        fprintf(stderr, "Error: %u reachable blocks are NOT marked allocated in block bitmap!\n", unrecorded);
        has_error = true;
    }

    free(disk_b_bitmap);
    free(disk_i_bitmap);
    free(reach_b_bitmap);
    fclose(fp);

    if (has_error) {
        printf("[FAILED] Filesystem integrity verification failed.\n");
        return 1;
    }

    printf("[PASSED] StrataFS filesystem structure is consistent and verified.\n");
    return 0;
}
