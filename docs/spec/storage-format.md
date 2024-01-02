# StrataFS Storage Format Specification

Version: 1.0.0
Status: Frozen

## 1. Overview
StrataFS is an original journaled filesystem designed for the Stratum operating system, providing atomicity for metadata modifications and durability for synchronous file updates.

## 2. Fundamental Constants
- Block Size: 4096 bytes (`STRATA_BLOCK_SIZE`)
- Sector Size: 512 bytes (`STRATA_SECTOR_SIZE`, 8 sectors per block)
- Superblock Magic: `0x5354524154414653` ("STRATAFS" in ASCII)
- Version: 1
- Inode Size: 256 bytes (16 inodes per 4 KiB block)
- Direct Blocks per Inode: 12
- Indirect Blocks: 1 single-indirect (1024 blocks), 1 double-indirect (1,048,576 blocks)

## 3. On-Disk Layout
```
+---------------+---------------------+--------------------+
| Block Index   | Region Name         | Description        |
+---------------+---------------------+--------------------+
| 0             | Boot / Reserved     | Partition/MBR area |
| 1             | Superblock          | File system meta   |
| 2             | Superblock Backup   | Secondary copy     |
| 3 .. B_bm     | Block Bitmap        | Free block tracker |
| B_bm+1 .. I_bm| Inode Bitmap        | Free inode tracker |
| I_bm+1 .. I_tb| Inode Table         | Array of inodes    |
| I_tb+1 .. J_end| Redo Journal       | Circular log ring  |
| J_end+1 .. End| Data Blocks         | File & dir storage |
+---------------+---------------------+--------------------+
```

## 4. On-Disk Structures

### 4.1 Superblock (`strata_superblock_t`)
```c
struct strata_superblock {
    uint64_t magic;               /* STRATA_MAGIC */
    uint32_t version;             /* STRATA_VERSION (1) */
    uint32_t block_size;          /* 4096 */
    uint64_t total_blocks;        /* Total blocks on disk */
    uint64_t free_blocks;         /* Count of unallocated blocks */
    uint64_t total_inodes;        /* Total inodes supported */
    uint64_t free_inodes;         /* Count of unallocated inodes */
    uint64_t block_bitmap_start;  /* First block of block bitmap */
    uint64_t block_bitmap_blocks;
    uint64_t inode_bitmap_start;  /* First block of inode bitmap */
    uint64_t inode_bitmap_blocks;
    uint64_t inode_table_start;   /* First block of inode table */
    uint64_t inode_table_blocks;
    uint64_t journal_start;       /* First block of redo journal */
    uint64_t journal_blocks;      /* Size of journal in blocks */
    uint64_t root_inode;          /* Inode number of root dir (usually 1) */
    uint64_t last_mount_time;
    uint64_t last_write_time;
    uint32_t mount_count;
    uint32_t state_flags;         /* 0 = Clean, 1 = Dirty / Recover Needed */
    uint32_t crc32c;              /* CRC32C of superblock struct up to crc32c */
    uint8_t  padding[3996];       /* Pad to 4096 bytes */
};
```

### 4.2 Inode Structure (`strata_inode_t`, 256 bytes)
```c
struct strata_inode {
    uint16_t mode;             /* File type and permissions */
    uint16_t uid;
    uint32_t gid;
    uint32_t link_count;       /* Hard link count */
    uint64_t size_bytes;       /* File size in bytes */
    uint64_t atime_sec;
    uint64_t mtime_sec;
    uint64_t ctime_sec;
    uint64_t blocks_count;     /* 4K blocks allocated to file */
    uint64_t direct_blocks[12];/* Direct block pointers */
    uint64_t indirect_block;   /* Single indirect block pointer */
    uint64_t double_indirect;  /* Double indirect block pointer */
    uint32_t flags;
    uint32_t generation;
    uint32_t crc32c;           /* CRC32C of inode metadata */
    uint8_t  reserved[88];     /* Pad to 256 bytes */
};
```

### 4.3 Directory Entry (`strata_dirent_t`)
Variable-length directory entries packed inside data blocks:
```c
struct strata_dirent {
    uint64_t inode_no;     /* Target inode number (0 if deleted) */
    uint16_t record_len;   /* Length of this directory record */
    uint8_t  name_len;     /* Length of file name */
    uint8_t  file_type;    /* 1=Regular, 2=Directory, 3=Symlink */
    char     name[256];    /* UTF-8 file name, null terminated */
};
```

## 5. Redo Journal and Transaction Ordering
The journal operates as a fixed circular log ring of blocks.

### 5.1 Journal Block Types
1. `JOURNAL_BLOCK_HEADER`: Contains journal sequence metadata, head, and tail offsets.
2. `JOURNAL_BLOCK_DESCRIPTOR`: Marks start of transaction, list of home block numbers in this transaction.
3. `JOURNAL_BLOCK_DATA`: Logged copy of dirty metadata block.
4. `JOURNAL_BLOCK_COMMIT`: Marks atomic transaction completion with transaction sequence and checksum.

### 5.2 Transaction Lifetime and Durability Sequence
For any metadata-modifying operation or `fsync`:
1. **Prepare**: Allocate blocks in memory block cache; pin blocks; record in-memory transaction.
2. **Write Log**: Write transaction descriptor block and logged metadata blocks to journal log ring on disk.
3. **Barrier 1**: Issue Virtio-Block `VIRTIO_BLK_T_FLUSH` command to guarantee journal payload reached non-volatile medium.
4. **Write Commit**: Write `JOURNAL_BLOCK_COMMIT` with valid CRC32C to disk.
5. **Barrier 2**: Issue second `VIRTIO_BLK_T_FLUSH`. At this instant, the transaction is durable and atomic.
6. **Checkpointing**: Dirty blocks in block cache can now be lazily written back to their home locations. Once written to home, the journal tail advances.

### 5.3 Crash Recovery Protocol
On mount:
1. Read `JOURNAL_BLOCK_HEADER`. If journal is clean (head == tail), mount proceeds.
2. Scan forward from tail to head:
   - Identify valid descriptor blocks.
   - Verify every subsequent data block up to the matching commit block.
   - Compute CRC32C of commit block.
   - If commit block is missing, damaged, or torn: discard the incomplete transaction.
   - If commit block is valid: replay all associated data blocks to their target home block numbers.
3. Once all valid committed transactions are replayed, issue a storage flush.
4. Update journal head and tail to match, mark clean state, and complete mount.
