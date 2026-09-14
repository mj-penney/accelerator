///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

#define MJP_VENDOR_ID 0x1234
#define MJP_DEVICE_ID 0x11e8

#define MJP_DMA_ADDR_LO 0x00
#define MJP_DMA_ADDR_HI 0x04
#define MJP_DMA_ADDR_SIZE 0x08

///////////////////////////////////////////////////////////////////////////////
// DEVICE
///////////////////////////////////////////////////////////////////////////////

/* vendor & device ids */

#define MJP_VENDOR_ID 0x1234
#define MJP_DEVICE_ID 0x11e8

/* memory region offsets */

#define MJP_BAR0_SIZE    0x10000

#define MJP_REGS_BAR_IDX       0
#define MJP_REGS_OFFSET        0x00000
#define MJP_REGS_SIZE          0x01000

#define MJP_MSIX_TABLE_BAR_IDX 0
#define MJP_MSIX_TABLE_OFFSET  0x01000
#define MJP_MSIX_TABLE_SIZE    0x01000

#define MJP_MSIX_PBA_BAR_IDX   0
#define MJP_MSIX_PBA_OFFSET    0x02000
#define MJP_MSIX_PBA_SIZE      0x01000

#define MJP_MSIX_VEC_NUM 1
#define MJP_MSIX_CAP_POS 0 /* don't manually set msix cap_pos */

/* individual register offsets */

#define MJP_DMA_ADDR_LO 0x00
#define MJP_DMA_ADDR_HI 0x04
#define MJP_DMA_SIZE 0x08
