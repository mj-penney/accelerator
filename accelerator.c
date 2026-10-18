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

static atomic64_t job_counter = ATOMIC64_INIT(0);

static u64 alloc_job_id(void)
{
    return (u64)atomic64_inc_return(&job_counter);
}

struct mjp_dma_buf {
    void *cpu_addr;
    dma_addr_t dma_addr;
    uint64_t size;
};

struct mjp_ring {
    struct mjp_dma_buf buf;
};

struct mjp_queue {
    struct mjp_ring sub_ring;
    struct mjp_ring cpl_ring;
};

struct mjp_accel {
    struct pci_dev *pdev;
    void __iomem *regs;
    struct cdev cdev;
    dev_t devt;

    struct mjp_queue *queues;
};

/* Data Operations */

/*
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
*/

static int mjp_queue_push(struct mjp_accel *mjp, struct mjp_sub_desc *job)
{
    struct mjp_queue *queue = mjp->queues;
    struct mjp_ring *ring = &queue->sub_ring;
    struct mjp_dma_buf *dma_buf = &ring->buf;

    uint32_t prod_idx = ioread32(mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_PROD));
    uint32_t cons_idx = ioread32(mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_CONS));

    if (prod_idx - cons_idx == MJP_SUB_RING_SIZE) {
        return 1; // error - ring full, drop job request
    }

    struct mjp_sub_desc *desc = (struct mjp_sub_desc *)dma_buf->cpu_addr;
    desc = &desc[prod_idx % MJP_SUB_RING_SIZE];

    memcpy(desc, job, sizeof(struct mjp_sub_desc));

    pr_info("Submitted job: job_id: %llu\n", job->job_id);

    prod_idx++;
    iowrite32(prod_idx, mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_PROD));

    return 0;
}

static int mjp_queue_pop(struct mjp_accel *mjp)
{
    struct mjp_queue *queue = mjp->queues;
    struct mjp_ring *ring = &queue->cpl_ring;
    struct mjp_dma_buf *dma_buf = &ring->buf;

    uint32_t cons_idx = ioread32(mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_CONS));

    struct mjp_cpl_desc *desc = (struct mjp_cpl_desc *)dma_buf->cpu_addr;
    desc = &desc[cons_idx % MJP_CPL_RING_SIZE];

    pr_info("Received completed job: job_id: %llu\n", desc->job_id);

    cons_idx++;
    iowrite32(cons_idx, mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_CONS));

    return 0;
}

static int mjp_submit_job(struct mjp_accel *mjp)
{
    struct mjp_sub_desc desc;

    desc.job_id = alloc_job_id();
    
    if (mjp_queue_push(mjp, &desc)) {
        return 1; // error
    }

    iowrite32(1, mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_DOORBELL));

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
            pr_info("mjp_accel: old ioctl no longer in use\n");
            return 0;
        case MJP_IOC_QUEUE_TEST:
            pr_info("mjp_accel: testing queue\n");
            return mjp_submit_job(mjp);
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

    pci_set_master(pdev);

    return mjp;
}

static irqreturn_t mjp_irq_handler(int irq, void *dev_id)
{
    pr_info("Interrupt triggered\n");

    struct mjp_accel *mjp = dev_id;
    mjp_queue_pop(mjp);

    return IRQ_HANDLED;
}

static int mjp_msix_setup(struct pci_dev *pdev, struct mjp_accel *mjp)
{
    int res, nvecs;

    nvecs = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX);
    if (nvecs < 0) {
        return nvecs;
    }

    res = devm_request_irq(&pdev->dev,
                           pci_irq_vector(pdev, 0),
                           mjp_irq_handler,
                           0,
                           "mjp-accel",
                           mjp);

    return res;
}

static int mjp_dma_setup(struct pci_dev *pdev)
{
    int res = 0;

    res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

    //if (res) {
    //    res = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    //}

    return res;
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

static int mjp_device_program_regs(struct mjp_accel *mjp)
{
    struct mjp_queue *queue = &mjp->queues[0];
    struct mjp_ring *sub_ring = &queue->sub_ring;
    struct mjp_ring *cpl_ring = &queue->cpl_ring;

    iowrite32(lower_32_bits(sub_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_BASE_LO));
    iowrite32(upper_32_bits(sub_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_BASE_HI));
    iowrite32(0, mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_PROD));
    iowrite32(0, mjp->regs + MJP_Q_REG(0, MJP_REG_SUB_CONS));

    iowrite32(lower_32_bits(cpl_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_BASE_LO));
    iowrite32(upper_32_bits(cpl_ring->buf.dma_addr),
              mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_BASE_HI));
    iowrite32(0, mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_PROD));
    iowrite32(0, mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_CONS));

    pr_info("Sub ring base DMA addr: 0x%llx\n", (uint64_t)sub_ring->buf.dma_addr);

    return 0;
}

static int mjp_queues_setup(struct mjp_accel *mjp)
{
    int res;
    struct mjp_queue *queues;
    size_t sub_ring_bytes = MJP_SUB_RING_SIZE * sizeof(struct mjp_sub_desc);
    size_t cpl_ring_bytes = MJP_CPL_RING_SIZE * sizeof(struct mjp_cpl_desc);


    if (!(mjp->queues = kcalloc(1, sizeof(struct mjp_queue), GFP_KERNEL))) {
        res = 1;
        goto err_allocating_queue;
    }
    queues = mjp->queues;

    if ((res = mjp_dma_alloc(mjp->pdev,
                             &queues[0].sub_ring.buf,
                             sub_ring_bytes)) != 0) {
        goto err_allocating_sub_ring;
    }

    if ((res = mjp_dma_alloc(mjp->pdev,
                             &queues[0].cpl_ring.buf,
                             cpl_ring_bytes)) != 0) {
        goto err_allocating_cpl_ring;
    }

    return 0;

err_allocating_cpl_ring:
    dma_free_coherent(&mjp->pdev->dev,
                      queues[0].sub_ring.buf.size,
                      queues[0].sub_ring.buf.cpu_addr,
                      queues[0].sub_ring.buf.dma_addr);
err_allocating_sub_ring:
    kfree(queues);
err_allocating_queue:
    return res;
}

static void mjp_queues_cleanup(struct mjp_accel *mjp)
{
    struct mjp_queue *queues = mjp->queues;
    dma_free_coherent(&mjp->pdev->dev,
                      queues[0].sub_ring.buf.size,
                      queues[0].sub_ring.buf.cpu_addr,
                      queues[0].sub_ring.buf.dma_addr);
    dma_free_coherent(&mjp->pdev->dev,
                      queues[0].cpl_ring.buf.size,
                      queues[0].cpl_ring.buf.cpu_addr,
                      queues[0].cpl_ring.buf.dma_addr);
    kfree(mjp->queues);
}

static int mjp_device_reset(struct mjp_accel *mjp)
{
    iowrite32(MJP_CONTROL_DEVICE_RESET, mjp->regs + MJP_REG_CONTROL);

    mjp_device_status dev_status = ioread32(mjp->regs + MJP_REG_DEVICE_STATUS);
    if (dev_status == MJP_DEVICE_STATUS_RESET) {
        return 0;
    }

    return 1;
}

static int mjp_device_enable(struct mjp_accel *mjp)
{
    iowrite32(MJP_CONTROL_DEVICE_ENABLE, mjp->regs + MJP_REG_CONTROL);

    mjp_device_status dev_status = ioread32(mjp->regs + MJP_REG_DEVICE_STATUS);
    if (dev_status == MJP_DEVICE_STATUS_ENABLED) {
        return 0;
    }

    return 1;
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

    if ((res = mjp_queues_setup(mjp)) != 0) {
        return res;
    }

    if ((res = mjp_msix_setup(pdev, mjp)) != 0) {
        return res;
    }

    if ((res = mjp_cdev_setup(mjp, pdev)) != 0) {
        goto err_setting_up_cdev;
    }

    if ((res = mjp_device_reset(mjp)) != 0) {
        goto err_resetting_device;
    }

    if ((res = mjp_device_program_regs(mjp)) != 0) {
        goto err_programming_device_regs;
    }

    if ((res = mjp_device_enable(mjp)) != 0) {
        goto err_enabling_device;
    }

    return res;

err_enabling_device:
err_programming_device_regs:
err_resetting_device:
err_setting_up_cdev:
    mjp_queues_cleanup(mjp);

    return res;
}

static void mjp_remove(struct pci_dev *pdev)
{
    struct mjp_accel *mjp = pci_get_drvdata(pdev);

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
