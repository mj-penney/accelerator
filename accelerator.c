#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioctl.h>
#include <linux/rhashtable.h>
#include <linux/completion.h>
#include <linux/slab.h>

#include "include/hw.h"

static atomic64_t job_counter = ATOMIC64_INIT(0);

static u64 alloc_job_id(void)
{
    return (u64)atomic64_inc_return(&job_counter);
}

///////////////////////////////////////////////////////////////////////////////
// DMA DATA BUFFER

#define MJP_DBUF_FREE         0U
#define MJP_DBUF_ALLOCATED    1U
//#define MJP_DBUF_PENDING_FREE 2U
typedef uint16_t mjp_dbuf_alloc_status;

#define MJP_DBUF_IDLE      1U
#define MJP_DBUF_IN_FLIGHT 0U
typedef uint16_t mjp_dbuf_dev_status;

struct mjp_dbuf {
    void                  *cpu_addr;
    dma_addr_t            dma_addr;
    u64                   size_bytes;
    u64                   used_bytes;
    mjp_dbuf_handle       handle;
    mjp_dbuf_alloc_status alloc_status;
    mjp_dbuf_dev_status   dev_status;
};

#define MJP_DBUF_POOL_SIZE 16
#define MJP_DBUF_SIZE_BYTES 1024

struct mjp_dbuf_pool {
    struct mjp_dbuf *pool;
    u64 size;
};

///////////////////////////////////////////////////////////////////////////////
// DMA QUEUE

struct mjp_dma_buf {
    void *cpu_addr;
    dma_addr_t dma_addr;
    uint64_t size;
};

struct mjp_ring {
    struct mjp_dma_buf buf;
    uint64_t size; // number of descriptors, not bytes
};

struct mjp_queue {
    struct mjp_ring sub_ring;
    struct mjp_ring cpl_ring;
};

///////////////////////////////////////////////////////////////////////////////
// DEVICE

struct mjp_job_tracker {
    u64 job_id;
    struct rhash_head node;
    struct completion done;
    int status;
};

static const struct rhashtable_params job_tracker_params = {
    .head_offset = offsetof(struct mjp_job_tracker, node),
    .key_offset = offsetof(struct mjp_job_tracker, job_id),
    .key_len = sizeof(u64),
};

struct mjp_accel {
    struct pci_dev *pdev;
    void __iomem *regs;
    struct cdev cdev;
    dev_t devt;

    struct mjp_queue *queues;

    struct mjp_dbuf_pool dbuf_pool;

    struct rhashtable job_tracker;
};

///////////////////////////////////////////////////////////////////////////////
// CRUDE DMA DATA BUFFER ALLOCATOR CODE

/* Get a DMA data buffer given a handle.
 * Users will only specify buffers by their handles, so use this to get the
 * buffer before submitting a job.
 *
 * Don't need a lock for this.
 */
static struct mjp_dbuf *mjp_get_dbuf(struct mjp_accel *mjp,
                                     mjp_dbuf_handle handle)
{
    u64 i;
    struct mjp_dbuf *dbuf;

    for (i = 0; i < mjp->dbuf_pool.size; i++) {
        dbuf = &mjp->dbuf_pool.pool[i];
        if (dbuf->handle == handle) {
            return dbuf;
        }
    }

    return NULL;
}

/* A data buffer is only free if it is marked as free and not part of an
 * in-flight job.
 */
static bool mjp_dbuf_is_free(struct mjp_dbuf *dbuf)
{
    return dbuf->alloc_status == MJP_DBUF_FREE 
        && dbuf->dev_status == MJP_DBUF_IDLE;
}

/* Get a free dbuf to give to the user.
 *
 * TODO:
 * Acquire the 'dbuf_alloc' lock for the dbuf array before allocating the
 * buffer to avoid allocating the same buffer twice.
 */
static struct mjp_dbuf *mjp_alloc_dbuf(struct mjp_accel *mjp)
{
    u64 i;
    struct mjp_dbuf *dbuf;

    // acquire lock here

    for (i = 0; i < mjp->dbuf_pool.size; i++) {
        dbuf = &mjp->dbuf_pool.pool[i];

        if (mjp_dbuf_is_free(dbuf)) {
            dbuf->alloc_status = MJP_DBUF_ALLOCATED;
            return dbuf;
        }
    }

    // release lock here

    return NULL;
}

/* Do I need a lock on the dbuf to free it?
 * I think so, to avoid un-mmapping the buffer twice etc.
 * I believe I want to synchronise with userspace here, in case it tries access
 * the buffer during the freeing process.
 */
static void mjp_free_dbuf(struct mjp_accel *mjp, mjp_dbuf_handle handle)
{
    struct mjp_dbuf *dbuf = mjp_get_dbuf(mjp, handle);

    dbuf->alloc_status = MJP_DBUF_FREE;
    // also un-mmap the buffer from userspace
}

static long mjp_ioc_alloc_dbuf(struct mjp_accel *mjp, unsigned long arg)
{
    struct mjp_ioctl_alloc_buf req;
    struct mjp_dbuf *dbuf;

    if (copy_from_user(&req, (void __user *)arg, sizeof(struct mjp_ioctl_alloc_buf))) {
        return -EFAULT;
    }

    dbuf = mjp_alloc_dbuf(mjp);

    if (!dbuf) {
        return -ENOMEM;
    }

    req.handle = dbuf->handle;

    if (copy_to_user((void __user *)arg, &req, sizeof(struct mjp_ioctl_alloc_buf))) {
        return -EFAULT;
    }

    return 0;
}

static long mjp_ioc_free_dbuf(struct mjp_accel *mjp, unsigned long arg)
{
    mjp_dbuf_handle handle = (mjp_dbuf_handle)arg;
    mjp_free_dbuf(mjp, handle);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// MAIN DRIVER CODE

static void mjp_job_tracker_complete(struct mjp_accel *mjp,
                                     struct mjp_cpl_desc *desc)
{
    struct mjp_job_tracker *job;

    job = rhashtable_lookup_fast(&mjp->job_tracker,
                                  &desc->job_id,
                                  job_tracker_params);
    if (!job) {
        return;
    }

    // TODO: get status from the completion descriptor
    WRITE_ONCE(job->status, 0);
    complete_all(&job->done);
}

static struct mjp_cpl_desc *mjp_cpl_ring_get_base_ptr(struct mjp_accel *mjp,
                                                      int queue_id)
{
    struct mjp_queue *queues = mjp->queues;
    return (struct mjp_cpl_desc *)queues[queue_id].cpl_ring.buf.cpu_addr;
}

static struct mjp_sub_desc *mjp_sub_ring_get_base_ptr(struct mjp_accel *mjp,
                                                      int queue_id)
{
    struct mjp_queue *queues = mjp->queues;
    return (struct mjp_sub_desc *)queues[queue_id].sub_ring.buf.cpu_addr;
}


/* TODO:
 * Only call this with the sub_ring lock.
 */
static uint32_t mjp_sub_ring_count_avail_slots(struct mjp_accel *mjp,
                                               int queue_id)
{
    uint32_t prod_idx = ioread32(mjp->regs + MJP_Q_REG(queue_id,
                                                       MJP_REG_SUB_PROD));
    uint32_t cons_idx = ioread32(mjp->regs + MJP_Q_REG(queue_id,
                                                       MJP_REG_SUB_CONS));

    return MJP_SUB_RING_SIZE - (prod_idx - cons_idx);
}

/* Fills the submission ring with the provided job descriptors, in the order
 * provided.
 *
 * Returns the number of jobs actually submitted.
 *
 * TODO:
 * Only call this with the sub_ring lock.
 */
static u32 mjp_sub_ring_fill(struct mjp_accel *mjp,
                             int queue_id,
                             struct mjp_sub_desc *jobs,
                             int count)
{
    struct mjp_sub_desc *ring_base;
    struct mjp_sub_desc *dest;
    struct mjp_sub_desc *src;
    u32 filled = 0;

    uint32_t available = mjp_sub_ring_count_avail_slots(mjp, queue_id);
    ring_base = mjp_sub_ring_get_base_ptr(mjp, queue_id);

    if (!available) {
        return filled; // ring full, return 0
    }

    uint32_t prod_idx = ioread32(mjp->regs + MJP_Q_REG(queue_id,
                                                       MJP_REG_SUB_PROD));

    while ((filled < count) && (filled < available)) {
        src  = &jobs[filled];
        dest = &ring_base[(prod_idx + filled) % MJP_SUB_RING_SIZE];

        memcpy(dest, src, sizeof(struct mjp_sub_desc));

        pr_info("Submitted job: job_id: %llu\n", dest->job_id);

        filled++;
    }

    prod_idx = prod_idx + filled;
    iowrite32(prod_idx, mjp->regs + MJP_Q_REG(queue_id, MJP_REG_SUB_PROD));

    return filled;
}


/* Callback for draining the completion ring when the queue receives an
 * interrupt.
 *
 * TODO:
 * Spin until the the cpl_ring lock is acquired.
 */
static irqreturn_t mjp_cpl_ring_drain(int irq, void *dev_id)
{
    struct mjp_accel *mjp = dev_id;

    struct mjp_cpl_desc *ring_base;
    struct mjp_cpl_desc *desc;

    uint32_t prod_idx = ioread32(mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_PROD));
    uint32_t cons_idx = ioread32(mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_CONS));

    ring_base = mjp_cpl_ring_get_base_ptr(mjp, 0);

    while (cons_idx != prod_idx) {
        desc = &ring_base[cons_idx % MJP_CPL_RING_SIZE];
        mjp_job_tracker_complete(mjp, desc);

        pr_info("Cons: %u, prod: %u\n", cons_idx, prod_idx);
        pr_info("Received completed job: job_id: %llu\n", desc->job_id);

        cons_idx++;
    }

    iowrite32(cons_idx, mjp->regs + MJP_Q_REG(0, MJP_REG_CPL_CONS));

    return IRQ_HANDLED;
}

static int mjp_track_job(struct mjp_accel *mjp, struct mjp_sub_desc *desc)
{
    struct mjp_job_tracker *job;

    job = kzalloc(sizeof(struct mjp_job_tracker), GFP_KERNEL);
    if (!job) {
        return -ENOMEM;
    }

    job->job_id = desc->job_id;
    job->status = -EINPROGRESS;
    init_completion(&job->done);

    int ret = rhashtable_insert_fast(&mjp->job_tracker,
                                     &job->node,
                                     job_tracker_params);
    if (ret) {
        kfree(job);
        return ret;
    }

    return 0;
}

static int mjp_submit_job(struct mjp_accel *mjp, unsigned long long arg)
{
    struct mjp_sub_desc desc;
    struct mjp_job_sub_req req;
    
    if (copy_from_user(&req,
                       (void __user *)arg,
                       sizeof(struct mjp_job_sub_req))) {
        return -EFAULT;
    }

    desc.job_id = alloc_job_id();
    desc.in_handle = req.in_handle;
    desc.out_handle = req.out_handle;
    desc.opcode = req.opcode;

    req.job_id = desc.job_id;
    if (copy_to_user((void __user *)arg, &req, sizeof(struct mjp_job_sub_req))) {
        return -EFAULT;
    }
    
    /* TODO:
     * Loop through all the rings and keep attempting to fill the sub rings
     * until the job is successfully submitted.
     */
    if (!mjp_sub_ring_fill(mjp, 0, &desc, 1)) {
        return 1;
    }

    if (mjp_track_job(mjp, &desc)) {
        return 1;
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

static int mjp_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct mjp_accel *mjp = file->private_data;
    mjp_dbuf_handle handle = vma->vm_pgoff;
    struct mjp_dbuf *dbuf = mjp_get_dbuf(mjp, handle);

    vma->vm_pgoff = 0;
    
    int ret = dma_mmap_coherent(&mjp->pdev->dev,
                                vma,
                                dbuf->cpu_addr,
                                dbuf->dma_addr,
                                dbuf->size_bytes);
    
    if (ret) {
        return ret;
    }
    return 0;
}

static long accel_ioctl(struct file *file,
                        unsigned int cmd,
                        unsigned long arg)
{
    struct mjp_accel *mjp = file->private_data;

    switch (cmd) {
        case MJP_IOC_PING:
            return 0;
        case MJP_IOC_JOB_SUBMIT:
            return mjp_submit_job(mjp, arg);
        case MJP_IOC_ALLOC_BUFFER:
            return mjp_ioc_alloc_dbuf(mjp, arg);
        case MJP_IOC_FREE_BUFFER:
            return mjp_ioc_free_dbuf(mjp, arg);
        default:
            return -ENOTTY;
    }
}

static const struct file_operations accel_fops = {
    .owner = THIS_MODULE,
    .open = accel_open,
    .release = accel_release,
    .unlocked_ioctl = accel_ioctl,
    .mmap = mjp_mmap,
};

/* DEVICE SETUP */

static struct class *mjp_accel_class;

static const struct pci_device_id mjp_ids[] = {
    { PCI_DEVICE(MJP_VENDOR_ID, MJP_DEVICE_ID) },
    { }
};

static int mjp_job_tracker_setup(struct mjp_accel *mjp)
{
    return rhashtable_init(&mjp->job_tracker, &job_tracker_params);
}

static void mjp_job_tracker_cleanup(struct mjp_accel *mjp)
{
    rhashtable_destroy(&mjp->job_tracker);
}

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

static int mjp_msix_setup(struct pci_dev *pdev, struct mjp_accel *mjp)
{
    int res, nvecs;

    nvecs = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX);
    if (nvecs < 0) {
        return nvecs;
    }

    res = devm_request_irq(&pdev->dev,
                           pci_irq_vector(pdev, 0),
                           mjp_cpl_ring_drain,
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

    return 0;
}

static int mjp_dbuf_pool_setup(struct mjp_accel *mjp)
{
    struct mjp_dbuf_pool *pool = &mjp->dbuf_pool;

    pool->size = MJP_DBUF_POOL_SIZE;
    pool->pool = kcalloc(pool->size, sizeof(struct mjp_dbuf), GFP_KERNEL);

    if (!pool->pool) {
        return -ENOMEM;
    }

    struct mjp_dbuf *dbuf;
    u64 i = 0;
    while (i < pool->size) {
        dbuf = &pool->pool[i];

        dbuf->cpu_addr = dma_alloc_coherent(&mjp->pdev->dev,
                                            MJP_DBUF_SIZE_BYTES,
                                            &dbuf->dma_addr,
                                            GFP_KERNEL);
        if (!dbuf->cpu_addr) {
            goto err_allocating_dbuf;
        }

        dbuf->handle = i;
        dbuf->size_bytes = MJP_DBUF_SIZE_BYTES;
        dbuf->alloc_status = MJP_DBUF_FREE;
        dbuf->dev_status = MJP_DBUF_IDLE;

        i++;
    }

    return 0;

err_allocating_dbuf:

    while (i) {
        dbuf = &pool->pool[i - 1];
        dma_free_coherent(&mjp->pdev->dev,
                          dbuf->size_bytes,
                          dbuf->cpu_addr,
                          dbuf->dma_addr);
        i--;
    }

    kfree(pool->pool);

    return -ENOMEM;
}

static void mjp_dbuf_pool_cleanup(struct mjp_accel *mjp)
{
    struct mjp_dbuf_pool *pool = &mjp->dbuf_pool;

    struct mjp_dbuf *dbuf;
    u64 i = 0;
    while (i < pool->size) {
        dbuf = &pool->pool[i];
        dma_free_coherent(&mjp->pdev->dev,
                          dbuf->size_bytes,
                          dbuf->cpu_addr,
                          dbuf->dma_addr);

        i++;
    }

    kfree(pool->pool);
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
    queues[0].sub_ring.size = MJP_SUB_RING_SIZE;

    if ((res = mjp_dma_alloc(mjp->pdev,
                             &queues[0].cpl_ring.buf,
                             cpl_ring_bytes)) != 0) {
        goto err_allocating_cpl_ring;
    }
    queues[0].cpl_ring.size = MJP_CPL_RING_SIZE;

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

    if ((res = mjp_dbuf_pool_setup(mjp)) != 0) {
        return res;
    }

    if ((res = mjp_cdev_setup(mjp, pdev)) != 0) {
        goto err_setting_up_cdev;
    }

    if ((res = mjp_job_tracker_setup(mjp)) != 0) {
        goto err_setting_up_job_tracker;
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
    mjp_job_tracker_cleanup(mjp);
err_setting_up_job_tracker:
err_setting_up_cdev:
    mjp_dbuf_pool_cleanup(mjp);
    mjp_queues_cleanup(mjp);

    return res;
}

static void mjp_remove(struct pci_dev *pdev)
{
    struct mjp_accel *mjp = pci_get_drvdata(pdev);

    mjp_job_tracker_cleanup(mjp);
    mjp_dbuf_pool_cleanup(mjp);
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
