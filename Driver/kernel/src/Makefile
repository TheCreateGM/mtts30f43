#
# Moore Threads sGPU Kernel Module Makefile
# Adapted for Fedora Linux
#
# Copyright © 2022 Moore Threads Inc. All rights reserved.
#

# Use KVER if passed from DKMS, otherwise detect from running kernel
KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build

PWD := $(shell pwd)

# For DKMS compatibility: if KVER is passed as argument, use it
ifneq ($(KVER),)
KDIR := /lib/modules/$(KVER)/build
endif

obj-m := sgpu_km.o mtgpu_stub.o
sgpu_km-objs := \
	sgpu_linux.o \
	utils.o \
	sgpu_sched.o \
	sgpu_ioctl.o \
	sgpu.o \
	sgpu_procfs.o \
	sgpu_procfs_ioctl.o \
	sgpu_fence.o

DEBUG ?= 0
ifeq ($(DEBUG), 1)
$(info debug mode is on)
CFLAGS_utils.o := -DDEBUG
DEBUG_CFLAGS += -g -DDEBUG
ccflags-y += ${DEBUG_CFLAGS}
CC += ${DEBUG_CFLAGS}
endif

extra-cflags += -Wall -Wstrict-prototypes -Wno-trigraphs

all:
	$(MAKE) -C $(KDIR) M="$(PWD)" modules

modules:
	$(MAKE) -C $(KDIR) M="$(PWD)" modules

clean:
	$(MAKE) -C $(KDIR) M="$(PWD)" clean

install: all
	sudo mkdir -p /lib/modules/$(KVER)/kernel/drivers/gpu/drm/mtt/
	sudo cp sgpu_km.ko mtgpu_stub.ko /lib/modules/$(KVER)/kernel/drivers/gpu/drm/mtt/
	sudo depmod -a

uninstall:
	sudo rm -f /lib/modules/$(KVER)/kernel/drivers/gpu/drm/mtt/sgpu_km.ko
	sudo rm -f /lib/modules/$(KVER)/kernel/drivers/gpu/drm/mtt/mtgpu_stub.ko
	sudo depmod -a

.PHONY: all clean install uninstall
