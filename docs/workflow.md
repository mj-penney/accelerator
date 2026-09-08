# Development workflow

## Environment overview

- `../qemu/` is the QEMU build with my custom device which I will use to boot
  a dev VM
- `../linux/` is the kernel build that I compile my driver against and that
  QEMU boots
- Minimal BusyBox ramdisk environment with a simple init script for mounting
  filesystems & starting a shell

## Build and run the driver & userspace program

1. Run `./buildrd.sh` to build the ramdisk image containing the compiled
  kernel module, userspace program, init script and BusyBox
2. Run `./qemu.sh` to run the custom QEMU build
3. Once into a shell, run `insmod accelerator.ko`
4. Now run `uspace` to run the userspace program

If developing the QEMU device side, you will need to rebuild QEMU.
