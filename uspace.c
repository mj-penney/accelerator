#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

#include "include/hw.h"

struct mjp_float_buf {
    mjp_dbuf_handle handle;
    float *buf;
    uint64_t len; // elements
};

static struct mjp_float_buf mjp_alloc(int fd, uint64_t size_bytes)
{
    struct mjp_ioctl_alloc_buf req = {
        .requested_size_bytes = size_bytes,
    };

    if (ioctl(fd, MJP_IOC_ALLOC_BUFFER, &req)) {
        perror("allocating dbuf");
    }

    printf("Userspace: buffer handle: %u\n", req.handle);

    void *ptr = mmap(NULL, size_bytes,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd,
                     (off_t)req.handle * getpagesize());

    if (ptr == MAP_FAILED)
        perror("mmap");

    struct mjp_float_buf buf = {
        .handle = req.handle,
        .buf = (float *)ptr,
        .len = size_bytes / sizeof(float),
    };

    return buf;
}

void mjp_free(int fd, struct mjp_float_buf *buf)
{
    if (ioctl(fd, MJP_IOC_FREE_BUFFER, buf->handle)) {
        perror("allocating dbuf");
    }
}

int main(void)
{
    int fd = open("/dev/mjp_accel", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, MJP_IOC_PING) < 0)
        perror("ioctl");

    struct mjp_user_job job = {
        .opcode = 1,
    };

    //if (ioctl(fd, MJP_IOC_COPY, &req_data)) {
    //    perror("copy data");
    //}

    if (ioctl(fd, MJP_IOC_QUEUE_TEST, &job)) {
        perror("queue test");
    }

    struct mjp_float_buf buf = mjp_alloc(fd, 256);

    buf.buf[0] = 5.3;
    buf.buf[1] = 165.2340;
    buf.buf[2] = 72.6666667;

    mjp_free(fd, &buf);

    close(fd);
    return 0;
}
