#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#define MJP_ACCEL_VENDOR_ID 0x1234
#define MJP_ACCEL_DEVICE_ID 0x11e8

#define MJP_ACCEL_DMA_ADDR_LO_OFFSET 0x00
#define MJP_ACCEL_DMA_ADDR_HI_OFFSET 0x04
#define MJP_ACCEL_DMA_LEN_OFFSET 0x08

struct mjp_accel {
    struct pci_dev *pdev;
    void __iomem *regs;
};

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

    pci_set_drvdata(pdev, mjp);

    dma_addr_lo = ioread32(mjp->regs + MJP_ACCEL_DMA_ADDR_LO_OFFSET);

    pr_info("dma_addr_lo = %d\n", dma_addr_lo);

    iowrite32(7, mjp->regs + MJP_ACCEL_DMA_ADDR_LO_OFFSET);
    iowrite32(3, mjp->regs + MJP_ACCEL_DMA_ADDR_HI_OFFSET);

    return 0;
}

static void my_remove(struct pci_dev *pdev)
{
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
    pr_info("hello from module\n");

    return pci_register_driver(&my_pci_driver);
}

static void __exit my_exit(void)
{
    pci_unregister_driver(&my_pci_driver);
    pr_info("goodbye\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
