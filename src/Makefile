# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

DEBFLAGS = -O2 -DMSIX_INTR -DSPC_USER_CTL

ccflags-y += $(DEBFLAGS) -DBAR_64K

ccflags-y += -I$(LDDINC) -I../common

ifneq ($(KERNELRELEASE),)
# call from kernel build system

#dma_drv-objs := dma_test_drv.o 
dma_drv-objs := switchtec.o 

obj-m	:= dma_drv.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD)/../include modules

endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions ../../bin/*.ko

depend .depend dep:
	$(CC) $(CFLAGS) -M *.c > .depend


ifeq (.depend,$(wildcard .depend))
include .depend
endif
