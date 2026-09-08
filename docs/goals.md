# Project goals

## Accelerator device

- Build a PCIe accelerator device (with QEMU for now)
- Capable of many operations e.g. matrix multiplication, signal processing
  tasks etc.

## Linux driver

- Use DMA, MMIO & MSI-X to efficiently communicate with the device and transfer
  data
- Support multiple in-flight jobs
- Implement a queue structure with sumbission & completion descriptor rings

## Userspace tool/library

- Give userspace a simple API for submitting jobs and receiving processed data
