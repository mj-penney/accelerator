# MVP development plan

## 1. Freeze the basic architecture and ABI

Before writing much more code, document the intended contract in docs/:
How much of this do I need to solidify before moving on with development?

- PCI BAR/register map (done, need to define who can read/write/etc. regs)
- MSI-X vector layout
- Submission descriptor format
- Completion descriptor format
- Command/operation formats
- DMA address/length/alignment rules
- Ring sizes and producer/consumer semantics
- ioctl() structures and semantics
- Error/status codes
- Buffer/job ownership state machine

## 2. Replace the global DMA buffer with per-device state (done)

Move device-owned resources into `struct mjp_accel`.

## 3. Define and implement the queue/ring mechanism

Define fixed-size descriptors, for example:

```
submission descriptor:
    job_id
    opcode
    input DMA address
    output DMA address
    input length
    output length
    operation-specific parameters
```

and:

```
completion descriptor:
    job_id
    status
    output length
```

Then implement the ring protocol.

Also implement preliminary `SUBMIT` ioctl.

### 3.1. Migrate existing device model (see hw.h) to the model described in docs/iomem_layout.md

Keep the test_buf for now, as it is a useful debug path (is this possible)?

Add a ring smoke test.
First milestone:

```
driver probe
    ↓
allocate submit + completion rings
    ↓
write ring addresses/sizes to QEMU
    ↓
RESET
    ↓
ENABLE
    ↓
read VERSION/STATUS
    ↓
STATUS == READY
```

Have both sides log this.

Driver:

mjp_accel: submit ring dma=...
mjp_accel: completion ring dma=...
mjp_accel: device version=...
mjp_accel: device ready

QEMU:

mjp-accel: reset
mjp-accel: submit ring = ...
mjp-accel: completion ring = ...
mjp-accel: enabled

Then add a ring-loopback debug operation before implementing real jobs:

```
    Driver writes a known fake submission descriptor into slot 0.

    Driver updates SUB_PROD.

    Rings the doorbell.

    QEMU detects it.

    QEMU logs the descriptor.

    QEMU writes a fake completion.

    QEMU increments CPL_PROD.

    Driver detects/logs the completion.
```

That gives you a complete driver ↔ PCI MMIO ↔ DMA-memory ↔ QEMU debugging path without needing MSI-X, ioctls, or actual accelerator operations.

I'd make that your next concrete milestone: probe → reset → ready → submit one fake descriptor → fake completion.

## 4. Establish DMA semantics properly

E.g.:

- driver allocates DMA buffers
- driver copies userspace data into them
- device owns them while job is in-flight
- driver owns them after completion
- driver copies result back to userspace

## 5. Implement device reset/queue initialization

## 6. Add MSI-X before asynchronous job handling

## 7. Build the asynchronous driver job state machine

## 8. Implement the userspace ioctl ABI

Extremely simple e.g. `SUBMIT` and `REAP`.

## 9. Implement scalar multiplication first

## 10. Test full end-to-end

## 11. Add 1D convolution as a second opcode

## 12. Make concurrency semantics real before hardware parallelism

## 13. Add proper userspace library semantics

## 14. Add robustness/error handling
