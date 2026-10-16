#ifndef MJP_HW_H
#define MJP_HW_H

///////////////////////////////////////////////////////////////////////////////
// VENDOR & DEVICE IDS

#define MJP_VENDOR_ID 0x1234
#define MJP_DEVICE_ID 0x11e8

///////////////////////////////////////////////////////////////////////////////
// MEMORY REGION OFFSETS

#define MJP_ACCEL_BAR0_SIZE    0x10000

/* device registers */

#define MJP_REGS_BAR_IDX       0
#define MJP_REGS_OFFSET        0x0
#define MJP_REGS_SIZE          0x01000

#define MJP_REG_VERSION       (MJP_REGS_OFFSET + 0x000) // read-only device/ABI version
#define MJP_REG_DEVICE_STATUS (MJP_REGS_OFFSET + 0x004) // device status
#define MJP_REG_INT_STATUS    (MJP_REGS_OFFSET + 0x00c) // interrupt causes
#define MJP_REG_INT_MASK      (MJP_REGS_OFFSET + 0x010) // interrupt masking
#define MJP_REG_CONTROL       (MJP_REGS_OFFSET + 0x014) // reset device/queue state

/* queue-specific registers */

#define MJP_REGS_Q_OFFSET MJP_REGS_OFFSET + 0x100
#define MJP_REGS_Q_SIZE      0x100

// To access the SUB_CONS register on queue q, you would do:
// MJP_REGS_Q_OFFSET + (q * MJP_Q_SIZE) + MJP_REG_SUB_CONS
//
// Here are macros for this:

#define MJP_REG_Q_BASE(q) \
    (MJP_REGS_Q_OFFSET + ((q) * MJP_REGS_Q_SIZE))

#define MJP_Q_REG(q, reg) \
    (MJP_REG_Q_BASE(q) + (reg))

#define MJP_REG_SUB_BASE_LO  0x000
#define MJP_REG_SUB_BASE_HI  0x004
#define MJP_REG_SUB_SIZE     0x008
#define MJP_REG_SUB_PROD     0x00c
#define MJP_REG_SUB_CONS     0x010
#define MJP_REG_SUB_DOORBELL 0x014

#define MJP_REG_CPL_BASE_LO  0x018
#define MJP_REG_CPL_BASE_HI  0x01c
#define MJP_REG_CPL_SIZE     0x020
#define MJP_REG_CPL_PROD     0x024
#define MJP_REG_CPL_CONS     0x028

/* MSIX table */

#define MJP_MSIX_TABLE_BAR_IDX 0
#define MJP_MSIX_TABLE_OFFSET  0x01000
#define MJP_MSIX_TABLE_SIZE    0x01000

/* MSIX PBA */

#define MJP_MSIX_PBA_BAR_IDX   0
#define MJP_MSIX_PBA_OFFSET    0x02000
#define MJP_MSIX_PBA_SIZE      0x01000

/* other */

#define MJP_MSIX_VEC_NUM 1
#define MJP_MSIX_CAP_POS 0 /* 0: don't manually set msix cap_pos */

///////////////////////////////////////////////////////////////////////////////
// DEVICE STATE MACHINE

// control commands the driver can send to the device
typedef enum {
    MJP_CONTROL_DEVICE_RESET,
    MJP_CONTROL_DEVICE_ENABLE,
} mjp_control;

typedef enum {
    MJP_DEVICE_STATUS_RESET,   // dev sets this once reset
    MJP_DEVICE_STATUS_ENABLED, // dev sets this once enabled
    MJP_DEVICE_STATUS_ERROR,   // dev sets this if error
} mjp_device_status;

///////////////////////////////////////////////////////////////////////////////
// RINGS & DESCRIPTORS

// size refers to the number of descriptors, not bytes
#define MJP_SUB_RING_SIZE 4
#define MJP_CPL_RING_SIZE 16

/* submission/completion descriptors */

struct mjp_sub_desc {
    uint64_t job_id;
    uint32_t opcode;
    uint32_t flags;

    uint64_t input_addr;
    uint64_t output_addr;

    uint32_t input_len;
    uint32_t output_len;

    uint32_t op_param0;
    uint32_t op_param1;
};

struct mjp_cpl_desc {
    uint64_t job_id;
    uint32_t status;
    uint32_t flags;
    uint32_t output_len;
    uint32_t reserved;
};

#endif
