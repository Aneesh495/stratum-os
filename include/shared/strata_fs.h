#ifndef STRATUM_SHARED_STRATA_FS_H
#define STRATUM_SHARED_STRATA_FS_H

#if defined(__has_include)
  #if __has_include(<kernel/types.h>)
    #include <kernel/types.h>
  #else
    #include <stdint.h>
  #endif
#else
  #include <stdint.h>
#endif

#define STRATA_MAGIC            0x5354524154414653ULL /* "STRATAFS" */
#define STRATA_VERSION          1
#define STRATA_BLOCK_SIZE       4096
#define STRATA_SECTOR_SIZE      512
#define STRATA_SECTORS_PER_BLK  (STRATA_BLOCK_SIZE / STRATA_SECTOR_SIZE)
#define STRATA_INODE_SIZE       256
#define STRATA_INODES_PER_BLK   (STRATA_BLOCK_SIZE / STRATA_INODE_SIZE)
#define STRATA_DIRECT_BLOCKS    12
#define STRATA_NAME_MAX         255

/* File modes */
#define STRATA_MODE_DIR         0040000
#define STRATA_MODE_REG         0100000
#define STRATA_MODE_LNK         0120000

/* Journal block types */
#define STRATA_JRNL_MAGIC       0x4A524E4C53545231ULL /* "JRNLSTR1" */
#define STRATA_JRNL_DESC        0x01
#define STRATA_JRNL_DATA        0x02
#define STRATA_JRNL_COMMIT      0x03
#define STRATA_JRNL_REVOKE      0x04

typedef struct strata_superblock {
    uint64_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint64_t block_bitmap_start;
    uint64_t block_bitmap_blocks;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t journal_start;
    uint64_t journal_blocks;
    uint64_t root_inode;
    uint64_t last_mount_time;
    uint64_t last_write_time;
    uint32_t mount_count;
    uint32_t state_flags;
    uint32_t crc32c;
    uint8_t  padding[3996];
} __attribute__((packed)) strata_superblock_t;

typedef struct strata_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t gid;
    uint32_t link_count;
    uint64_t size_bytes;
    uint64_t atime_sec;
    uint64_t mtime_sec;
    uint64_t ctime_sec;
    uint64_t blocks_count;
    uint64_t direct_blocks[STRATA_DIRECT_BLOCKS];
    uint64_t indirect_block;
    uint64_t double_indirect;
    uint32_t flags;
    uint32_t generation;
    uint32_t crc32c;
    uint8_t  reserved[88];
} __attribute__((packed)) strata_inode_t;

typedef struct strata_dirent {
    uint64_t inode_no;
    uint16_t record_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[256];
} __attribute__((packed)) strata_dirent_t;

typedef struct strata_journal_header {
    uint64_t magic;
    uint32_t block_size;
    uint32_t journal_blocks;
    uint64_t head_block;
    uint64_t tail_block;
    uint64_t sequence_num;
    uint32_t crc32c;
    uint8_t  padding[4060];
} __attribute__((packed)) strata_journal_header_t;

typedef struct strata_journal_descriptor {
    uint32_t block_type;
    uint32_t count;
    uint64_t txn_id;
    uint64_t target_blocks[254];
} __attribute__((packed)) strata_journal_descriptor_t;

typedef struct strata_journal_commit {
    uint32_t block_type;
    uint32_t crc32c;
    uint64_t txn_id;
    uint64_t commit_time;
    uint8_t  padding[4072];
} __attribute__((packed)) strata_journal_commit_t;

#endif /* STRATUM_SHARED_STRATA_FS_H */
