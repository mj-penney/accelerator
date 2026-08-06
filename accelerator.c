#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioctl.h>

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

/* FILE OPERATIONS */

#define MJP_ACCEL_IOC_MAGIC  'm'
#define MJP_ACCEL_IOC_PING   _IO(MJP_ACCEL_IOC_MAGIC, 0)

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

static int my_probe(struct pci_dev *pdev,
                    const struct pci_device_id *id)
{
    struct mjp_accel *mjp;
    uint32_t dma_addr_lo = 0;
    //uint32_t dma_addr_hi = 0;
    int res = 0;

    res = pcim_enable_device(pdev);
    if (res) {
        return res;
    }

    res = pcim_iomap_regions(pdev, BIT(0), "mjp-accel");
    if (res) {
        return res;
    }

    mjp = devm_kzalloc(&pdev->dev, sizeof(*mjp), GFP_KERNEL);
    if (!mjp) {
        return -ENOMEM;
    }

    mjp->regs = pcim_iomap_table(pdev)[0];

    mjp->pdev = pdev;
    pci_set_drvdata(pdev, mjp);

    dma_addr_lo = ioread32(mjp->regs + MJP_ACCEL_DMA_ADDR_LO);

    pr_info("dma_addr_lo = %d\n", dma_addr_lo);

    iowrite32(7, mjp->regs + MJP_ACCEL_DMA_ADDR_LO);
    iowrite32(3, mjp->regs + MJP_ACCEL_DMA_ADDR_HI);

    /* set dma mask */

    res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
    if (res) {
        res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    }
    if (res) {
        return res;
    }
    
    /* allocate a dma buffer */

    void *cpu_addr;
    dma_addr_t dma_addr;

    cpu_addr = dma_alloc_coherent(&pdev->dev,
                                  PAGE_SIZE,
                                  &dma_addr,
                                  GFP_KERNEL);

    if (!cpu_addr) {
        return -ENOMEM;
    }

    /* program the device */

    iowrite32(lower_32_bits(dma_addr),
              mjp->regs + MJP_ACCEL_DMA_ADDR_LO);
    iowrite32(upper_32_bits(dma_addr),
              mjp->regs + MJP_ACCEL_DMA_ADDR_HI);
    iowrite32(PAGE_SIZE, mjp->regs + MJP_ACCEL_DMA_ADDR_SIZE);

    /* clean up buffer */



    /* set up cdev */

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

    return 0;
}

static void my_remove(struct pci_dev *pdev)
{
    struct mjp_accel *mjp = pci_get_drvdata(pdev);

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
