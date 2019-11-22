SUBDIRS = drivers/brcm/btsbc

# Specify Include folders
EXTRA_CFLAGS = -I$(SUBDIRS)
EXTRA_CFLAGS += -I$(SUBDIRS)/inc
EXTRA_CFLAGS += -I$(SUBDIRS)/src

EXTRA_CFLAGS += -DEXPORT_SYMTAB

# BTSBC_MEASUREMENT (Debug purpose) is used to measure the SBC Encoding duration
# If this value is defined, the SBC Encoder will print Encoding duration
EXTRA_CFLAGS += -DBTSBC_MEASUREMENT=4000

# SBC_HIGH_PRECISION set the SBC Encoder in High Precision (better quality)
# It, typically, double the CPU usage
#EXTRA_CFLAGS += -DSBC_HIGH_PRECISION

obj-m += btsbc.o

btsbc-objs += src/sbc_module.o
btsbc-objs += src/sbc.o
btsbc-objs += src/sbc_primitives.o

# specify Kernel build location
KDIR    := $(KERNEL_HEADER_DIR)

PWD     := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	@echo Cleaning BTSBC
	@rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
	@rm -f Module.markers
	@rm -f Module.symvers
	@rm -f modules.order
	@rm -f ./src/*.o
	@rm -f ./src/.*.o.cmd
	@rm -f ./src/*.ko
	@rm -f ./src/*.mod.c

help:
	@echo "BTSBC module targets:"
	@echo "  (none)             : BTSBC module doesn't have any compile option"
	@echo "  clean"

