#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioctl.h>

#include "accelerator.h"
#include "include/hw.h"

struct mjp_dma_buf {
    void *cpu_addr;
    dma_addr_t dma_addr;
    uint64_t size;
};

struct mjp_ring {
    struct mjp_dma_buf buf;
    uint32_t size; // bookkeeping: number of descriptors in the ring
    uint32_t prod; // bookkeeping: producer index
    uint32_t cons; // bookkeeping: consumer index
};

struct mjp_accel {
    struct pci_dev *pdev;
    void __iomem *regs;
    struct cdev cdev;
    dev_t devt;

    struct mjp_ring sub_ring;
    struct mjp_ring cpl_ring;

    /* Temporary debug buffer.
     * Remove it once the basic ring loopback/debug path (plan stage 3.1) is
     * established.
     */
    struct mjp_dma_buf test_buf;
};

/* Data Operations */

static int copy_data_from_uspace(unsigned long arg,
                                 struct mjp_dma_buf *dma_buf)
{
    struct mjp_accel_user_data req;
    
    if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
        return -EFAULT;
    }

    if (req.size > dma_buf->size) {
        pr_info("Data too big for buffer\n");
        return -ENOMEM;
    }

    if (!access_ok((void __user *)req.data, req.size)) {
        return -EFAULT;
    }

    if (copy_from_user(dma_buf->cpu_addr,
                       (void __user *)req.data,
                       req.size)) {
        return -EFAULT;
    }

    pr_info("Data size: %llu\n", req.size);
    pr_info("Element 0: %llu\n", *((uint64_t *)dma_buf->cpu_addr));
    pr_info("Element 1: %llu\n", *((uint64_t *)dma_buf->cpu_addr + 1));
    pr_info("Element 2: %llu\n", *((uint64_t *)dma_buf->cpu_addr + 2));
    pr_info("Element 3: %llu\n", *((uint64_t *)dma_buf->cpu_addr + 3));

    return 0;
}

/* FILE OPERATIONS */

static int accel_open(struct inode *inode, struct file *file)
{
    struct mjp_accel *mjp;

    mjp = container_of(inode->i_cdev,
                       struct mjp_accel,
                       cdev);

    file->private_data = mjp;

    return 0;
}

static int accel_release(struct inode *inode, struct file *file)
{
    return 0;
}

static long accel_ioctl(struct file *file,
                        unsigned int cmd,
                        unsigned long arg)
{
    struct mjp_accel *mjp = file->private_data;

    switch (cmd) {
        case MJP_IOC_PING:
            pr_info("mjp_accel: ioctl called\n");
            return 0;
        case MJP_IOC_COPY:
            pr_info("mjp_accel: ioctl copy data\n");
            return copy_data_from_uspace(arg, &mjp->test_buf);
        default:
            return -ENOTTY;
    }
}

static const struct file_operations accel_fops = {
    .owner = THIS_MODULE,
    .open = accel_open,
    .release = accel_release,
    .unlocked_ioctl = accel_ioctl,
};

/* DEVICE SETUP */

static struct class *mjp_accel_class;

static const struct pci_device_id mjp_ids[] = {
    { PCI_DEVICE(MJP_VENDOR_ID, MJP_DEVICE_ID) },
    { }
};

/* Allocate a DMA buffer */
static int mjp_dma_alloc(struct pci_dev *pdev,
                         struct mjp_dma_buf *dma_buf,
                         size_t size)
{
    dma_buf->cpu_addr = dma_alloc_coherent(&pdev->dev,
                                           size,
                                           &dma_buf->dma_addr,
                                           GFP_KERNEL);

    if (!dma_buf->cpu_addr) {
        return -ENOMEM;
    }

    dma_buf->size = size;

    return 0;
}

static struct mjp_accel *mjp_pci_setup(struct pci_dev *pdev)
{
    int res = 0;
    struct mjp_accel *mjp;

    res = pcim_enable_device(pdev);
    if (res) {
        return NULL;
    }

    res = pcim_iomap_regions(pdev, BIT(0), "mjp-accel");
    if (res) {
        return NULL;
    }

    mjp = devm_kzalloc(&pdev->dev, sizeof(*mjp), GFP_KERNEL);
    if (!mjp) {
        return NULL;
    }

    mjp->regs = pcim_iomap_table(pdev)[0];

    mjp->pdev = pdev;
    pci_set_drvdata(pdev, mjp);

    return mjp;
}

static int mjp_dma_setup(struct pci_dev *pdev)
{
    int res = 0;

    res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

    if (res) {
        res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    }

    return res;
}

static int mjp_device_program_regs(struct mjp_accel *mjp,
                                   struct mjp_dma_buf *dma_buf)
{
    iowrite32(lower_32_bits(dma_buf->dma_addr),
              mjp->regs + MJP_DMA_ADDR_LO);
    iowrite32(upper_32_bits(dma_buf->dma_addr),
              mjp->regs + MJP_DMA_ADDR_HI);
    return 0;
}

static int mjp_cdev_setup(struct mjp_accel *mjp, struct pci_dev *pdev)
{
    int res;

    res = alloc_chrdev_region(&mjp->devt, 0, 1, "mjp_accel");
    if (res)
        return res;

    cdev_init(&mjp->cdev, &accel_fops);
    mjp->cdev.owner = THIS_MODULE;

    res = cdev_add(&mjp->cdev, mjp->devt, 1);
    if (res) {
        goto err_chrdev;
    }

    if (IS_ERR(device_create(mjp_accel_class,
                             &pdev->dev,
                             mjp->devt,
                             NULL,
                             "mjp_accel"))) {
        res = -ENODEV;
        goto err_cdev;
    }

    return 0;

err_cdev:
    cdev_del(&mjp->cdev);
err_chrdev:
    unregister_chrdev_region(mjp->devt, 1);
    return res;
}

static int mjp_queues_program_regs(struct mjp_accel *mjp)
{
    struct mjp_ring *sub_ring = &mjp->sub_ring;
    struct mjp_ring *cpl_ring = &mjp->cpl_ring;

    iowrite32(lower_32_bits(sub_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_BASE_LO));
    iowrite32(upper_32_bits(sub_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_BASE_HI));
    iowrite32(sub_ring->size,
              mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_SIZE));
    iowrite32(sub_ring->prod,
              mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_PROD));
    iowrite32(sub_ring->cons,
              mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_CONS));

    iowrite32(lower_32_bits(cpl_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_BASE_LO));
    iowrite32(upper_32_bits(cpl_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_BASE_HI));
    iowrite32(cpl_ring->size,
              mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_SIZE));
    iowrite32(cpl_ring->prod,
              mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_PROD));
    iowrite32(cpl_ring->cons,
              mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_CONS));

    return 0;
}

static int mjp_queues_setup(struct mjp_accel *mjp)
{
    int res;
    size_t sub_ring_bytes = MJP_SUB_RING_SIZE * sizeof(struct mjp_sub_desc);
    size_t cpl_ring_bytes = MJP_CPL_RING_SIZE * sizeof(struct mjp_cpl_desc);

    if ((res = mjp_dma_alloc(mjp->pdev,
                             &mjp->sub_ring.buf,
                             sub_ring_bytes)) != 0) {
        return res;
    }

    if ((res = mjp_dma_alloc(mjp->pdev,
                             &mjp->cpl_ring.buf,
                             cpl_ring_bytes)) != 0) {
        goto err_allocating_cpl_ring;
    }

    mjp->sub_ring.prod = 0;
    mjp->sub_ring.cons = 0;
    mjp->sub_ring.size = MJP_SUB_RING_SIZE;

    mjp->cpl_ring.prod = 0;
    mjp->cpl_ring.cons = 0;
    mjp->cpl_ring.size = MJP_CPL_RING_SIZE;

    if ((res = mjp_queues_program_regs(mjp)) != 0) {
        goto err_programming_queue_regs;
    }

    return 0;

err_programming_queue_regs:
    dma_free_coherent(&mjp->pdev->dev,
                      mjp->cpl_ring.buf.size,
                      mjp->cpl_ring.buf.cpu_addr,
                      mjp->cpl_ring.buf.dma_addr);

err_allocating_cpl_ring:
    dma_free_coherent(&mjp->pdev->dev,
                      mjp->sub_ring.buf.size,
                      mjp->sub_ring.buf.cpu_addr,
                      mjp->sub_ring.buf.dma_addr);
    return res;
}

static void mjp_queues_cleanup(struct mjp_accel *mjp)
{
    dma_free_coherent(&mjp->pdev->dev,
                      mjp->sub_ring.buf.size,
                      mjp->sub_ring.buf.cpu_addr,
                      mjp->sub_ring.buf.dma_addr);
    dma_free_coherent(&mjp->pdev->dev,
                      mjp->cpl_ring.buf.size,
                      mjp->cpl_ring.buf.cpu_addr,
                      mjp->cpl_ring.buf.dma_addr);
}

static int mjp_device_reset(struct mjp_accel *mjp)
{
    iowrite32(0x1, mjp->regs + MJP_REG_RESET);
    return 0;
}

static int mjp_probe(struct pci_dev *pdev,
                    const struct pci_device_id *id)
{
    struct mjp_accel *mjp = NULL;
    int res = 0;

    if (!(mjp = mjp_pci_setup(pdev))) {
        return -ENOMEM;
    }

    if ((res = mjp_dma_setup(pdev)) != 0) {
        return res;
    }

    if ((res = mjp_device_reset(mjp)) != 0) {
        return res;
    }
    
    if ((res = mjp_dma_alloc(pdev, &mjp->test_buf, PAGE_SIZE)) != 0) {
        goto err_allocating_test_buf;
    }

    if ((res = mjp_queues_setup(mjp)) != 0) {
        goto err_setting_up_queues;
    }

    if ((res = mjp_device_program_regs(mjp, &mjp->test_buf)) != 0) {
        goto err_programming_regs;
    }

    if ((res = mjp_cdev_setup(mjp, pdev)) != 0) {
        goto err_setting_up_cdev;
    }

    return res;

err_setting_up_cdev:
err_programming_regs:
    mjp_queues_cleanup(mjp);

err_setting_up_queues:
    dma_free_coherent(&pdev->dev,
                      mjp->test_buf.size,
                      mjp->test_buf.cpu_addr,
                      mjp->test_buf.dma_addr);

    mjp->test_buf.cpu_addr = NULL;
    mjp->test_buf.dma_addr = 0;
    mjp->test_buf.size = 0;

err_allocating_test_buf:
    return res;
}

static void mjp_remove(struct pci_dev *pdev)
{
    struct mjp_accel *mjp = pci_get_drvdata(pdev);

    dma_free_coherent(&pdev->dev,
                      mjp->test_buf.size,
                      mjp->test_buf.cpu_addr,
                      mjp->test_buf.dma_addr);
    mjp_queues_cleanup(mjp);
    mjp_device_reset(mjp);
    device_destroy(mjp_accel_class, mjp->devt);
    cdev_del(&mjp->cdev);
    unregister_chrdev_region(mjp->devt, 1);

    pr_info("Removing accelerator\n");
}

static struct pci_driver mjp_pci_driver = {
    .name     = "mjp_accel",
    .id_table = mjp_ids,
    .probe    = mjp_probe,
    .remove   = mjp_remove,
};

MODULE_DEVICE_TABLE(pci, mjp_ids);

static int __init mjp_init(void)
{
    int res;

    pr_info("hello from module\n");

    mjp_accel_class = class_create("mjp_accel");
    if (IS_ERR(mjp_accel_class)) {
        return PTR_ERR(mjp_accel_class);
    }

    res = pci_register_driver(&mjp_pci_driver);
    if (res) {
        class_destroy(mjp_accel_class);
        return res;
    }

    return 0;
}

static void __exit mjp_exit(void)
{
    pci_unregister_driver(&mjp_pci_driver);
    class_destroy(mjp_accel_class);
    pr_info("goodbye\n");
}

module_init(mjp_init);
module_exit(mjp_exit);

MODULE_LICENSE("GPL");
