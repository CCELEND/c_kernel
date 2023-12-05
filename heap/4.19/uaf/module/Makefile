
obj-m += kuaf.o
CURRENT_PATH := $(shell pwd)

LINUX_KERNEL_SOURCE_PATH := /root/kernel/4.19/linux-4.19
all:
	make -C $(LINUX_KERNEL_SOURCE_PATH) M=$(CURRENT_PATH) modules
clean:
	make -C $(LINUX_KERNEL_SOURCE_PATH) M=$(CURRENT_PATH) clean
