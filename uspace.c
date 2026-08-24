#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#include "accelerator.h"

static uint64_t buffer[4];

int main(void)
{
    int fd = open("/dev/mjp_accel", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, MJP_ACCEL_IOC_PING) < 0)
        perror("ioctl");

    // fill the buffer
    buffer[0] = 5;
    buffer[1] = 6;
    buffer[2] = 7;
    buffer[3] = 8;

    struct mjp_accel_user_data req_data = {
        .data = &buffer[0],
        .size = 4 * sizeof(uint64_t),
    };

    if (ioctl(fd, MJP_ACCEL_IOC_COPY, &req_data)) {
        perror("copy data");
    }

    close(fd);
    return 0;
}
