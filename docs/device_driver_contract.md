# Device-driver contract

## Device registers

### MJP_REG_DEVICE_STATUS

This register indicates the status of the device (one of the following):

- `MJP_DEVICE_STATUS_RESET`: all state is cleared and queues are flushed
- `MJP_DEVICE_STATUS_ENABLED`: device is set up and ready for work (or already
  working)
- `MJP_DEVICE_STATUS_ERROR`: device is in an error state and needs to be reset
  by the driver

- `r` for the driver
- `rw` for the device

### MJP_REG_CONTROL

This register is for the driver to send control commands to the device (one of
the following):

- `MJP_CONTROL_DEVICE_RESET`: reset the device
- `MJP_CONTROL_DEVICE_ENABLE`: enable the device

- `rw` for the driver
- `r` for the device

## Device initialisation

The driver must follow this process to set the device up:

1. Driver resets the device to ensure a fresh start
2. Driver confirms that `MJP_REG_DEVICE_STATUS` is `MJP_DEVICE_STATUS_RESET`
3. Driver gives the device information about queues (DMA addresses, ring
   indices...)
4. Driver enables the device
5. Driver confirms that `MJP_REG_DEVICE_ENABLE` is `MJP_DEVICE_STATUS_ENABLED`
   before any job submission/completion

The driver must do this when it is loaded (in `probe()`).
It must also follow this process if the device needs a reset at any other time 
(e.g. its status is `MJP_DEVICE_STATUS_ERROR`).
