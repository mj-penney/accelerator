# MVP development plan

## 1. Freeze the basic architecture and ABI

Before writing much more code, document the intended contract in docs/:

- PCI BAR/register map
- MSI-X vector layout
- Submission descriptor format
- Completion descriptor format
- Command/operation formats
- DMA address/length/alignment rules
- Ring sizes and producer/consumer semantics
- ioctl() structures and semantics
- Error/status codes
- Buffer/job ownership state machine

## 2. Replace the global DMA buffer with per-device state

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
