#!/bin/bash

if [ -d initrd ]; then
    rm -rf initrd
fi

mkdir -p initrd/{dev,bin,sbin,etc,proc,sys,usr/{bin,sbin}}
cp -av ../busybox/_install/* initrd/

## Use busybox init
#ln -sf bin/busybox init

## Use custom init
cp init initrd/init
sudo chmod +x initrd/init

sudo mknod -m 660 initrd/dev/mem c 1 1
sudo mknod -m 660 initrd/dev/tty2 c 4 2
sudo mknod -m 660 initrd/dev/tty3 c 4 3
sudo mknod -m 660 initrd/dev/tty4 c 4 4

## Build and copy userspace tool into initrd
gcc -static -o2 -o uspace uspace.c
cp uspace initrd/bin/

## Build and copy driver into initrd
make
cp accelerator.ko initrd/

cd initrd
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initrd.cpio.gz
cd ..
