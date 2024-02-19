#include <kernel/virtio_blk.h>
#include <kernel/pmm.h>
#include <kernel/slab.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <kernel/x86_64.h>
#include <shared/errno.h>

static virtio_blk_dev_t g_primary_blk;
static bool             g_primary_blk_registered = false;

virtio_blk_dev_t *virtio_blk_get_primary(void) {
    return g_primary_blk_registered ? &g_primary_blk : NULL;
}

static int64_t virtio_blk_do_request(virtio_blk_dev_t *dev, uint32_t type,
                                     uint64_t sector, uint32_t count, void *buf) {
    if (!dev || !dev->vq) return -STRATUM_EINVAL;
    if (count == 0 && type != VIRTIO_BLK_T_FLUSH) return 0;

    uint64_t flags;
    spin_lock_irqsave(&dev->lock, &flags);

    /* Allocate request header and status byte on heap */
    virtio_blk_req_hdr_t *hdr = (virtio_blk_req_hdr_t *)kmalloc(sizeof(virtio_blk_req_hdr_t));
    if (!hdr) {
        spin_unlock_irqrestore(&dev->lock, flags);
        return -STRATUM_ENOMEM;
    }

    uint8_t *status = (uint8_t *)kmalloc(1);
    if (!status) {
        kfree(hdr);
        spin_unlock_irqrestore(&dev->lock, flags);
        return -STRATUM_ENOMEM;
    }

    hdr->type = type;
    hdr->reserved = 0;
    hdr->sector = sector;
    *status = 0xFF; /* Sentinel */

    vring_buf_t bufs[3];
    uint32_t num_out = 0;
    uint32_t num_in = 0;

    /* Buffer 0: Header (device-readable) */
    bufs[0].phys_addr = virt_to_phys(hdr);
    bufs[0].len = sizeof(virtio_blk_req_hdr_t);
    bufs[0].is_write = false;
    num_out++;

    /* Buffer 1: Data payload */
    if (count > 0 && buf != NULL) {
        bufs[1].phys_addr = virt_to_phys(buf);
        bufs[1].len = count * VIRTIO_BLK_SECTOR_SIZE;
        if (type == VIRTIO_BLK_T_IN) {
            bufs[1].is_write = true;
            num_in++;
        } else {
            bufs[1].is_write = false;
            num_out++;
        }
    }

    /* Buffer 2: Status byte (device-writable) */
    uint32_t status_buf_idx = (count > 0 && buf != NULL) ? 2 : 1;
    bufs[status_buf_idx].phys_addr = virt_to_phys(status);
    bufs[status_buf_idx].len = 1;
    bufs[status_buf_idx].is_write = true;
    num_in++;

    int res = virtqueue_add_buf(dev->vq, bufs, num_out, num_in, hdr);
    if (res != 0) {
        kfree(hdr);
        kfree(status);
        spin_unlock_irqrestore(&dev->lock, flags);
        return res;
    }

    virtqueue_kick(dev->vq);

    /* Poll for completion */
    uint64_t timeout_iters = 10000000ULL;
    uint32_t resp_len = 0;
    void *completed = NULL;
    while (timeout_iters-- > 0) {
        completed = virtqueue_get_buf(dev->vq, &resp_len);
        if (completed != NULL) {
            break;
        }
        __asm__ volatile("pause");
    }

    if (!completed) {
        kprintf("[VIRTIO-BLK] ERROR: Request timed out at sector %lu!\n", sector);
        kfree(hdr);
        kfree(status);
        spin_unlock_irqrestore(&dev->lock, flags);
        return -STRATUM_ETIMEDOUT;
    }

    uint8_t final_status = *status;
    kfree(hdr);
    kfree(status);

    spin_unlock_irqrestore(&dev->lock, flags);

    if (final_status != VIRTIO_BLK_S_OK) {
        kprintf("[VIRTIO-BLK] ERROR: Device returned status %u\n", final_status);
        return -STRATUM_EIO;
    }

    return (int64_t)(count * VIRTIO_BLK_SECTOR_SIZE);
}

int64_t virtio_blk_read(virtio_blk_dev_t *dev, uint64_t sector, uint32_t count, void *buf) {
    return virtio_blk_do_request(dev, VIRTIO_BLK_T_IN, sector, count, buf);
}

int64_t virtio_blk_write(virtio_blk_dev_t *dev, uint64_t sector, uint32_t count, const void *buf) {
    if (dev && dev->read_only) return -STRATUM_EROFS;
    return virtio_blk_do_request(dev, VIRTIO_BLK_T_OUT, sector, count, (void *)buf);
}

int virtio_blk_flush(virtio_blk_dev_t *dev) {
    int64_t res = virtio_blk_do_request(dev, VIRTIO_BLK_T_FLUSH, 0, 0, NULL);
    return (res >= 0) ? 0 : (int)res;
}

int virtio_blk_init(void) {
    pci_device_t *pci_dev = pci_find_device(0x1AF4, VIRTIO_BLK_PCI_MODERN_DEVICE_ID);
    if (!pci_dev) {
        pci_dev = pci_find_device(0x1AF4, VIRTIO_BLK_PCI_LEGACY_DEVICE_ID);
    }

    if (!pci_dev) {
        kprintf("[VIRTIO-BLK] No Virtio-blk PCI device detected.\n");
        return -STRATUM_ENODEV;
    }

    virtio_blk_dev_t *dev = &g_primary_blk;
    memset(dev, 0, sizeof(virtio_blk_dev_t));
    spin_lock_init(&dev->lock);

    int res = virtio_probe_device(pci_dev, &dev->vdev);
    if (res != 0) {
        kprintf("[VIRTIO-BLK] Failed to probe virtio device: %d\n", res);
        return res;
    }

    /* Negotiate supported features */
    uint64_t features = VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH;
    virtio_negotiate_features(&dev->vdev, features);

    /* Setup request virtqueue (queue index 0) */
    dev->vq = virtio_setup_queue(&dev->vdev, 0);
    if (!dev->vq) {
        kprintf("[VIRTIO-BLK] Failed to setup request virtqueue 0!\n");
        return -STRATUM_ENOMEM;
    }

    /* Read device capacity and block size */
    if (dev->vdev.is_modern && dev->vdev.device_cfg) {
        volatile virtio_blk_config_t *cfg = (volatile virtio_blk_config_t *)dev->vdev.device_cfg;
        dev->capacity_sectors = cfg->capacity;
        dev->block_size = (dev->vdev.negotiated_features & VIRTIO_BLK_F_BLK_SIZE) ? cfg->blk_size : 512;
    } else {
        uint32_t lo = inl((uint16_t)(dev->vdev.io_base + 0x14));
        uint32_t hi = inl((uint16_t)(dev->vdev.io_base + 0x18));
        dev->capacity_sectors = ((uint64_t)hi << 32) | lo;
        dev->block_size = 512;
    }

    dev->read_only = (dev->vdev.negotiated_features & VIRTIO_BLK_F_RO) != 0;

    /* Mark driver as operational */
    virtio_driver_ok(&dev->vdev);
    g_primary_blk_registered = true;

    kprintf("[VIRTIO-BLK] Block device initialized: capacity=%lu sectors (%lu MiB, block_size=%u, ro=%u)\n",
            dev->capacity_sectors,
            (dev->capacity_sectors * VIRTIO_BLK_SECTOR_SIZE) / (1024 * 1024),
            dev->block_size, dev->read_only);

    return 0;
}
