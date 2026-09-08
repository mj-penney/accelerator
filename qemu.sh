../qemu/build/qemu-system-x86_64 \
    -M q35 \
    -cpu max \
    -kernel ../linux/arch/x86/boot/bzImage \
    -initrd initrd.cpio.gz \
    -device mjp-accel \
    -monitor stdio
#    -append "console=ttyS0" \
#    -nographic \

