SUBDIRS = drivers/brcm/btpcm

# Specify Include folders
EXTRA_CFLAGS := -I$(SUBDIRS)
EXTRA_CFLAGS += -I$(SUBDIRS)/inc
EXTRA_CFLAGS += -I$(SUBDIRS)/src
EXTRA_CFLAGS += -DEXPORT_SYMTAB

# ALSA is the default
PCM_SRC ?= IBIZA

obj-m += btpcm.o

btpcm-objs := src/btpcm_core.o src/btpcm_hrtimer.o src/btpcm_alsa.o
btpcm-objs += src/btpcm_tone.o ibiza/btpcm_ibiza.o
ifeq ($(strip $(PCM_SRC)),ALSA)
EXTRA_CFLAGS += -DBTPCM_ALSA
endif

ifeq ($(strip $(PCM_SRC)),TONE)
EXTRA_CFLAGS += -DBTPCM_TONE
endif

ifeq ($(strip $(PCM_SRC)),IBIZA)
EXTRA_CFLAGS += -DBTPCM_IBIZA_DEBUG -DBTPCM_IBIZA
EXTRA_CFLAGS += -DBTPCM_HW_PCM_SAMPLE_SIZE=4
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_LEFT_ADDR=0x5C7C9000
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_LEFT_ADDR=0x5C7C9000
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_RIGHT_ADDR=0x5C7CD000
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_SIZE=0x4000
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_LEFT_ADDR=0x33420000   # Golf-Tizen
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_LEFT_ADDR=0x33420000   # Golf-Tizen
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_RIGHT_ADDR=0		
#EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_SIZE=0x15000	     #Golf-Tizen
EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_LEFT_ADDR=0x0		#0x13D020000 #Hawk-Tizen
EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_RIGHT_ADDR=0x0		
EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_BUF_SIZE=0x15000	     #Hawk-Tizen
EXTRA_CFLAGS += -DBTPCM_IBIZA_PCM_LEFT_WRITE_ADDR=0x0		#0x13CB8081C #Hawk-Tizen
endif

ifeq ($(strip $(PCM_SRC)),IBIZA_TONE)
EXTRA_CFLAGS += -DBTPCM_IBIZA_DEBUG -DBTPCM_IBIZA
EXTRA_CFLAGS += -DBTPCM_IBIZA_TONE
EXTRA_CFLAGS += -DBTPCM_HW_PCM_SAMPLE_SIZE=2
endif

# specify Kernel build location
KDIR    := $(KERNEL_HEADER_DIR)

PWD     := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
	rm -f Module.markers
	rm -f Module.symvers
	rm -f modules.order
	rm -f ./src/*.o
	rm -f ./src/.*.o.cmd
	rm -f ./src/*.ko
	rm -f ./src/*.mod.c
	rm -f ./ibiza/*.o

help:
	@echo "BTPCM module configurations and targets:"
	@echo "  PCM_SRC=TONE       : Tone Generation"
	@echo "  PCM_SRC=ALSA       : Use Virtual ALSA driver"
	@echo "  PCM_SRC=IBIZA      : PCM Samples come from HW (Ibiza Platform)"
	@echo "  PCM_SRC=IBIZA_TONE : PCM Samples come from SW Tone (Ibiza Platform)"
	@echo "  clean"

