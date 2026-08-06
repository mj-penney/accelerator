#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define MJP_ACCEL_IOC_MAGIC  'm'
#define MJP_ACCEL_IOC_PING   _IO(MJP_ACCEL_IOC_MAGIC, 0)

int main(void)
{
    int fd = open("/dev/mjp_accel", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, MJP_ACCEL_IOC_PING) < 0)
        perror("ioctl");

    close(fd);
    return 0;
}
