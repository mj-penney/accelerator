#ifndef MJP_ACCEL_H
#define MJP_ACCEL_H

struct mjp_accel_user_data {
    uint64_t *data;
    uint64_t size;
};

#define MJP_IOC_MAGIC 'm'
#define MJP_IOC_PING  _IO(MJP_IOC_MAGIC, 0)
#define MJP_IOC_COPY  _IOW(MJP_IOC_MAGIC, 1, \
                                 struct mjp_accel_user_data)

#endif
