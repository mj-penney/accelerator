# I/O memory layout

This is what I need to move my code towards.
If possible, make it a header shared between the device and driver.

```c
/* memory region offsets */

#define MJP_ACCEL_BAR0_SIZE    0x10000

/* device registers */

#define MJP_REGS_BAR_IDX       0
#define MJP_REGS_OFFSET        0x0
#define MJP_REGS_SIZE          0x01000

#define MJP_REG_VERSION    (MJP_REGS_OFFSET + 0x00) // read-only device/ABI version
#define MJP_REG_CONTROL    (MJP_REGS_OFFSET + 0x04) // device-enable
#define MJP_REG_STATUS     (MJP_REGS_OFFSET + 0x08) // e.g. ready/running/error
#define MJP_REG_INT_STATUS (MJP_REGS_OFFSET + 0x0c) // interrupt causes
#define MJP_REG_INT_MASK   (MJP_REGS_OFFSET + 0x10) // interrupt masking
#define MJP_REG_RESET      (MJP_REGS_OFFSET + 0x14) // reset device/queue state

/* queue-specific registers */

// to access the SUB_CONS register on queue q, you would do:
// MJP_REGS_Q_OFFSET + (q * MJP_Q_SIZE) + MJP_REG_SUB_CONS
// make a macro for this calculation

#define MJP_REGS_Q_OFFSET MJP_REGS_OFFSET + 0x100
#define MJP_REGS_Q_SIZE      0x100

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

/* OTHER */

#define MJP_MSIX_VEC_NUM 1
#define MJP_MSIX_CAP_POS 0 /* don't manually set msix cap_pos */
```
