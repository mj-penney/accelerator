# Project goals

## MVP goals

- Define clear and minimal userspace-driver & driver-device protocols/ABIs e.g.
  MMIO registers, MSI-X interrupts, DMA semantics, ioctls, commands
- Define a couple of relatively simple processing operations (e.g. 1D
  convolution and scalar multiplication) for the device to carry out
- Stick to ioctls for now, but plan for a switch to mmap in the future to avoid
- Clear state machine to determine who owns which buffers/jobs
  unnecessary copying between userspace and driver
- For now just one device instance and one queue per device, although
  developments should account for multiple devices & queues in the future
- Design for multiple in-flight jobs and parallel jobs in the device (e.g. if
  the device gets a big job then a small job, it might do both at once and
  complete the small one first) even if the device doesn't truly run jobs in
  parallel yet

### Device

- Support 2 simple operations (convolution & scalar multiplication)
- The device itself will not be truly parallel for now

### Protocols

- Well-defined and documented MMIO registers and MSI-X interrupts
- Well-defined and documented DMA semantics
- Well-defined and documented ioctls
- well-defined and documented buffer/descriptor ownership state machine

### Driver

- Present ioctls to userspace rather than using mmap for now
- For now have one queue per device, and just a single device
- The queue will have submission and completion descriptor rings
- Allow many outstanding jobs at once
- Support out-of-order completions from the device even if it is not parallel
  yet

### Userspace

- Extremely minimal library allowing userspace programs to submit jobs and
  get processed data back asynchronously
