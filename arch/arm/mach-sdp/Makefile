# Multi Core Support
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o

#Device Tree Support
obj-$(CONFIG_OF)	+= board-sdp-dt.o
ifeq ($(CONFIG_ARCH_SDP1412),y)
obj-$(CONFIG_OF)	+= board-hawka-dt.o
else
obj-$(CONFIG_OF)	+= board-golfs-dt.o
obj-$(CONFIG_OF)	+= board-golfv-dt.o
obj-$(CONFIG_OF)	+= board-hawkp-dt.o
obj-$(CONFIG_OF)	+= board-hawkm-dt.o
obj-$(CONFIG_OF)	+= board-foxb-dt.o
obj-$(CONFIG_OF)	+= board-echop-dt.o
obj-$(CONFIG_OF)	+= board-foxp-dt.o
endif
obj-$(CONFIG_OF)	+= common.o sdp_uart.o sdp_extint.o

obj-$(CONFIG_PM)		+= sdp_micom.o pm.o sleep.o

#CPUIDLE
ifeq ($(CONFIG_MCPM),y)
obj-$(CONFIG_CPU_IDLE)		+= sdpbl_cpuidle.o
else
obj-$(CONFIG_CPU_IDLE)		+= cpuidle.o
endif

obj-$(CONFIG_ARCH_SDP_BL)	+= sdpbl_pm.o sdpbl_pm_setup.o

# Patch Errata

# sdp_mem
obj-y				+= sdp_hwmem.o

#MAC
ifeq ($(CONFIG_SDP_MAC),y)
obj-y				+= sdp_mac_phy.o
endif

ifeq ($(CONFIG_SDP_MAC),m)
obj-y				+= sdp_mac_phy.o
endif

obj-$(CONFIG_SDP_MAC)		+= sdp_mac.o

#MMC
obj-$(CONFIG_SDP_MMC)		+= sdp_mmc.o

#SMC
obj-$(CONFIG_SDP_SMC)		+= sdp_smc.o

#SPI
obj-$(CONFIG_SDP_SPI)		+= sdp_spi.o
obj-$(CONFIG_SDP_SPIDEV)	+= sdp_spidev.o

#DMA
obj-$(CONFIG_SDP_DMA330)	+= sdp_dma330_core.o sdp_dma330.o
ifeq ($(CONFIG_SDP_DMA330_DEBUG),y)
CFLAGS_sdp_dma330_core.o	+= -DDEBUG
CFLAGS_sdp_dma330.o			+= -DDEBUG
endif
obj-$(CONFIG_SDP_GADMA)		+= sdp_gadma.o
obj-$(CONFIG_SDP_GDMA)		+= sdp_gdma.o
obj-$(CONFIG_SDP_DMADEV)	+= sdp_dmadev.o
obj-$(CONFIG_SDP_DMATEST)	+= sdp_dmatest.o

#GZIP
obj-$(CONFIG_SDP_UNZIP)		+= sdp_unzip.o

#THERMAL
obj-$(CONFIG_SDP_THERMAL)	+= sdp_thermal.o sdp1304_thermal.o sdp1404_thermal.o sdp1406_thermal.o
obj-$(CONFIG_SDP_TEMPSENSOR)	+= sdp_tempsensor.o

#HW clock
obj-$(CONFIG_SDP_HW_CLOCK)	+= sdp_hwclock.o

#Wifi-pm
obj-$(CONFIG_SDP_MISC_PM)	+= misc_pm.o
