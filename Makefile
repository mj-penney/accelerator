obj-m += accelerator.o

KDIR ?= ../linux
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf initrd/ initrd.cpio.gz uspace
