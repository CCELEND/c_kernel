
obj-m += ovf.o
CURRENT_PATH := $(shell pwd)

LINUX_KERNEL_SOURCE_PATH := /root/kernel/4.15/linux-4.15
all:
	make -C $(LINUX_KERNEL_SOURCE_PATH) M=$(CURRENT_PATH) modules
clean:
	make -C $(LINUX_KERNEL_SOURCE_PATH) M=$(CURRENT_PATH) clean
