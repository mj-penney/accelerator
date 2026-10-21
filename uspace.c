#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#include "include/hw.h"

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

    if (ioctl(fd, MJP_IOC_ALLOC_BUFFER)) {
        perror("allocating dbuf");
    }

    if (ioctl(fd, MJP_IOC_FREE_BUFFER, 0)) {
        perror("allocating dbuf");
    }

    close(fd);
    return 0;
}
