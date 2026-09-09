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

#define MJP_ACCEL_VENDOR_ID 0x1234
#define MJP_ACCEL_DEVICE_ID 0x11e8

#define MJP_ACCEL_DMA_ADDR_LO 0x00
#define MJP_ACCEL_DMA_ADDR_HI 0x04
#define MJP_ACCEL_DMA_ADDR_SIZE 0x08

struct mjp_accel {
    struct pci_dev *pdev;
    void __iomem *regs;
    struct cdev cdev;
    dev_t devt;
};

/* DMA Buffers */

struct mjp_dma_buf {
    void *cpu_addr;
    dma_addr_t dma_addr;
    uint64_t size;
};

// main buffer (put queues here in the future)
static struct mjp_dma_buf mjp_main_buf = {
    .cpu_addr = 0x0,
    .dma_addr = 0,
    .size = 0,
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
    switch (cmd) {

    case MJP_ACCEL_IOC_PING:
        pr_info("mjp_accel: ioctl called\n");
        return 0;

    case MJP_ACCEL_IOC_COPY:
        pr_info("mjp_accel: ioctl copy data\n");
        return copy_data_from_uspace(arg, &mjp_main_buf);

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

static const struct pci_device_id my_ids[] = {
    { PCI_DEVICE(MJP_ACCEL_VENDOR_ID, MJP_ACCEL_DEVICE_ID) },
    { }
};

/* Allocate a DMA buffer */
static int mjp_dma_alloc(struct pci_dev *pdev, struct mjp_dma_buf *dma_buf)
{
    dma_buf->cpu_addr = dma_alloc_coherent(&pdev->dev,
                                           PAGE_SIZE,
                                           &dma_buf->dma_addr,
                                           GFP_KERNEL);

    if (!dma_buf->cpu_addr) {
        return -ENOMEM;
    }

    dma_buf->size = PAGE_SIZE;

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
              mjp->regs + MJP_ACCEL_DMA_ADDR_LO);
    iowrite32(upper_32_bits(dma_buf->dma_addr),
              mjp->regs + MJP_ACCEL_DMA_ADDR_HI);
    iowrite32(PAGE_SIZE, mjp->regs + MJP_ACCEL_DMA_ADDR_SIZE);

    return 0;
}

static int mjp_cdev_setup(struct mjp_accel *mjp, struct pci_dev *pdev)
{
    int res = 0;

    res = alloc_chrdev_region(&mjp->devt, 0, 1, "mjp_accel");
    if (res) {
        return res;
    }

    cdev_init(&mjp->cdev, &accel_fops);
    mjp->cdev.owner = THIS_MODULE;

    res = cdev_add(&mjp->cdev, mjp->devt, 1);
    if (res) {
        unregister_chrdev_region(mjp->devt, 1);
        return res;
    }

    device_create(mjp_accel_class,
                  &pdev->dev,
                  mjp->devt,
                  NULL,
                  "mjp_accel");

    return res;
}

static int my_probe(struct pci_dev *pdev,
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
    
    if ((res = mjp_dma_alloc(pdev, &mjp_main_buf)) != 0) {
        return res;
    }

    if ((res = mjp_device_program_regs(mjp, &mjp_main_buf)) != 0) {
        goto err_dma;
    }

    if ((res = mjp_cdev_setup(mjp, pdev)) != 0) {
        goto err_dma;
    }

    return 0;

err_dma:
    dma_free_coherent(&pdev->dev,
                      mjp_main_buf.size,
                      mjp_main_buf.cpu_addr,
                      mjp_main_buf.dma_addr);

    mjp_main_buf.cpu_addr = NULL;
    mjp_main_buf.dma_addr = 0;
    mjp_main_buf.size = 0;

    return res;
}

static void my_remove(struct pci_dev *pdev)
{
    struct mjp_accel *mjp = pci_get_drvdata(pdev);

    dma_free_coherent(&pdev->dev,
                      mjp_main_buf.size,
                      mjp_main_buf.cpu_addr,
                      mjp_main_buf.dma_addr);
    device_destroy(mjp_accel_class, mjp->devt);
    cdev_del(&mjp->cdev);
    unregister_chrdev_region(mjp->devt, 1);

    pr_info("Removing accelerator\n");
}

static struct pci_driver my_pci_driver = {
    .name     = "my_accel",
    .id_table = my_ids,
    .probe    = my_probe,
    .remove   = my_remove,
};

MODULE_DEVICE_TABLE(pci, my_ids);

static int __init my_init(void)
{
    int res;

    pr_info("hello from module\n");

    mjp_accel_class = class_create("mjp_accel");
    if (IS_ERR(mjp_accel_class)) {
        return PTR_ERR(mjp_accel_class);
    }

    res = pci_register_driver(&my_pci_driver);
    if (res) {
        class_destroy(mjp_accel_class);
        return res;
    }

    return 0;
}

static void __exit my_exit(void)
{
    pci_unregister_driver(&my_pci_driver);
    class_destroy(mjp_accel_class);
    pr_info("goodbye\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
