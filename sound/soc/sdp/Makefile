#
# Makefile for the audio device drivers.
#
AUDDVB=../../../drivers/media/platform/sdp/sdp_audio/
ccflags-y += -D__LINUX_OS__

ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common/legacy_common/include/dev
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common/legacy_common/include/sam
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common/legacy_common/include/spi
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common/legacy_common/common_dev/ae
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common/legacy_common/common_dev/aio
#ccflags-y += -Idrivers/media/platform/sdp/sdp_common/osal
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common/legacy_common/platform_dev/ae
ccflags-y += -Idrivers/media/platform/sdp/sdp_audio/common/legacy_common/platform_dev/aio

ccflags-y += -Idrivers/media/dvb-core

ccflags-y += -Idrivers/media/platform/sdp/sdp_sif
ccflags-y += -Idrivers/media/platform/sdp/sdp_sif/common
ccflags-y += -Idrivers/media/platform/sdp/sdp_sif/common/inc
ccflags-y += -Idrivers/media/dvb-core
ccflags-y += -Idrivers/media/dvb-frontends
#sdp_audio_alsa_top-y += sdp_audio_codec.o sdp_audio_machine.o sdp_audio_platform.o sdp_audio_vir_dma.o
#obj-y += sdp_audio_alsa_top.o

obj-$(CONFIG_SDP_AUDIO) += sdp-audio-alsa.o
#sdp-audio-alsa-objs := ../../../drivers/media/platform/sdp/sdp_audio/sdp-audio-dvb.o sdp_audio_machine.o sdp_audio_platform.o sdp_audio_vir_dma.o sdp_audio_codec.o
sdp-audio-alsa-objs := sdp_audio_machine.o sdp_audio_platform.o sdp_audio_vir_dma.o sdp_audio_codec.o sif/sdp_sif_machine.o sif/sdp_sif_platform.o sif/sdp_sif_codec.o \
			${AUDDVB}sdp_audio_dvb.o ${AUDDVB}sdp_audio_spdif.o ${AUDDVB}sdp_audio_sys.o ${AUDDVB}sdp_audio_t2d.o \
                        ${AUDDVB}../sdp_sif/sdp_sif.o ${AUDDVB}../sdp_sif/sdp_sif_ioctl.o ${AUDDVB}../sdp_sif/sdp_sif_sys.o ${AUDDVB}../sdp_sif/sdp_sif_t2d.o\
			${AUDDVB}../sdp_sif/common/devIoSif.o ${AUDDVB}../sdp_sif/common/devSifCommon.o ${AUDDVB}../sdp_sif/common/lldSif.o ${AUDDVB}../sdp_sif/common/devSifDebug.o \
                        ${AUDDVB}common/sdp_audio_callback.o ${AUDDVB}common/sdp_audio_ext.o ${AUDDVB}common/sdp_audio.o \
                        ${AUDDVB}common/legacy_common/platform_dev/ae/devAe.o ${AUDDVB}common/legacy_common/platform_dev/ae/devAeArmComm.o ${AUDDVB}common/legacy_common/platform_dev/ae/devAeDebug.o \
                        ${AUDDVB}common/legacy_common/platform_dev/ae/lldAeArmComm.o ${AUDDVB}common/legacy_common/platform_dev/ae/lldAeArmReg.o ${AUDDVB}common/legacy_common/platform_dev/ae/lldAeArmSbf.o \
                        ${AUDDVB}common/legacy_common/platform_dev/ae/lldAeBoot.o ${AUDDVB}common/legacy_common/platform_dev/ae/lldAeIsr.o \
                        ${AUDDVB}common/legacy_common/platform_dev/aio/AioIsr.o ${AUDDVB}common/legacy_common/platform_dev/aio/devAio.o ${AUDDVB}common/legacy_common/platform_dev/aio/devAioDebug.o \
                        ${AUDDVB}common/legacy_common/platform_dev/aio/devAioDma.o ${AUDDVB}common/legacy_common/platform_dev/aio/lldAioClkGen.o ${AUDDVB}common/legacy_common/platform_dev/aio/lldAioDma.o \
                        ${AUDDVB}common/legacy_common/platform_dev/aio/lldAioI2s.o ${AUDDVB}common/legacy_common/platform_dev/aio/lldAioIsr.o ${AUDDVB}common/legacy_common/platform_dev/aio/lldAioSpdif.o \
                        ${AUDDVB}common/legacy_common/common_dev/ae/devAeCommon.o ${AUDDVB}common/legacy_common/common_dev/ae/devAeQBufferCommon.o \
                        ${AUDDVB}common/legacy_common/common_dev/aio/devAioCommon.o ${AUDDVB}common/legacy_common/common_dev/aio/devAioDmaCommon.o ${AUDDVB}common/legacy_common/common_dev/aio/devAioI2sCommon.o  \
                        ${AUDDVB}common/legacy_common/common_dev/aio/devAioSpdifCommon.o
# add audio amplifier driver
#obj-$(CONFIG_SDP_SND_NTP7412S2) += ntp7412s2.o
obj-$(CONFIG_SDP_SND_TAS5747) += tas5747.o

