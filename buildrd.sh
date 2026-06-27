#!/bin/bash

if [ -d initrd ]; then
    rm -rf initrd
fi

mkdir -p initrd/{dev,bin,sbin,etc,proc,sys,usr/{bin,sbin}}
cp -av ../busybox/_install/* initrd/
cd initrd

## Use busybox init
#ln -sf bin/busybox init

## Use custom init
cp ../init init
sudo chmod +x init

sudo mknod -m 660 dev/mem c 1 1
sudo mknod -m 660 dev/tty2 c 4 2
sudo mknod -m 660 dev/tty3 c 4 3
sudo mknod -m 660 dev/tty4 c 4 4
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initrd.cpio.gz
cd ..
