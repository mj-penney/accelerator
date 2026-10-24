#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>

#include "include/hw.h"

struct mjp_accel {
    const int fd;
};

struct mjp_float_buf {
    mjp_dbuf_handle handle;
    float *buf;
    uint64_t size; // bytes
    uint64_t len;  // elements
};

static struct mjp_float_buf *mjp_buf_alloc(struct mjp_accel *mjp,
                                           uint64_t size_bytes)
{
    struct mjp_float_buf *buf = malloc(sizeof(struct mjp_float_buf));
    if (!buf) {
        return NULL;
    }

    struct mjp_ioctl_alloc_buf req = {
        .requested_size_bytes = size_bytes,
    };

    if (ioctl(mjp->fd, MJP_IOC_ALLOC_BUFFER, &req)) {
        perror("allocating dbuf");
    }

    printf("Userspace: buffer handle: %u\n", req.handle);

    void *ptr = mmap(NULL, size_bytes,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED, mjp->fd,
                     (off_t)req.handle * getpagesize());

    if (ptr == MAP_FAILED)
        perror("mmap");

    buf->handle = req.handle;
    buf->buf = (float *)ptr;
    buf->size = size_bytes;
    buf->len = size_bytes / sizeof(float);

    return buf;
}

static void mjp_buf_free(struct mjp_accel *mjp, struct mjp_float_buf *buf)
{
    if (munmap(buf->buf, buf->size) < 0) {
        perror("munmap");
    }

    if (ioctl(mjp->fd, MJP_IOC_FREE_BUFFER, buf->handle)) {
        perror("allocating dbuf");
    }

    free(buf);
}

struct mjp_job {
    struct mjp_float_buf *in_buf;
    struct mjp_float_buf *out_buf;
    uint32_t opcode;
    uint64_t exec_id; // kernel writes this back to userspace
};

static struct mjp_job *mjp_job_alloc(struct mjp_accel *mjp)
{
    struct mjp_job *job = malloc(sizeof(struct mjp_job));
    if (!job) {
        return NULL;
    }

    job->in_buf = mjp_buf_alloc(mjp, 256);
    job->out_buf = mjp_buf_alloc(mjp, 256);
    job->opcode = 1;

    if (!job->in_buf || !job->out_buf) {
        return NULL;
    }

    return job;
}

static void mjp_job_free(struct mjp_accel *mjp, struct mjp_job *job)
{
    mjp_buf_free(mjp, job->in_buf);
    mjp_buf_free(mjp, job->out_buf);
    free(job);
}

static int mjp_job_exec(struct mjp_accel *mjp, struct mjp_job *job)
{
    struct mjp_job_exec_req req = {
        .in_handle = job->in_buf->handle,
        .out_handle = job->out_buf->handle,
        .opcode = job->opcode,
    };

    if (ioctl(mjp->fd, MJP_IOC_QUEUE_TEST, &req)) {
        perror("queue test");
        return -1;
    }
    // get back the execution id that the kernel assigned
    job->exec_id = req.exec_id;

    return 0;
}

int main(void)
{

    int fd = open("/dev/mjp_accel", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    struct mjp_accel mjp = {
        .fd = fd,
    };

    struct mjp_job *job1 = mjp_job_alloc(&mjp);
    struct mjp_job *job2 = mjp_job_alloc(&mjp);

    if (mjp_job_exec(&mjp, job1)) {
        fprintf(stderr, "Error in job1 execution\n");
    }
    printf("Submitted job1 with execution ID %u\n", job1->exec_id);

    if (mjp_job_exec(&mjp, job2)) {
        fprintf(stderr, "Error in job2 execution\n");
    }
    printf("Submitted job2 with execution ID %u\n", job2->exec_id);

    mjp_job_free(&mjp, job1);
    mjp_job_free(&mjp, job2);

    close(mjp.fd);
    return 0;
}
