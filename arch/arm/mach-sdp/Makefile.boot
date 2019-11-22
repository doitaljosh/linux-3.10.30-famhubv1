# Do we need these?

zreladdr-y	:= 0x80008000
params_phys-y	:= 0x80000100
initrd_phys-y	:= 0x80800000

ifeq ($(CONFIG_ARCH_SDP1202), y)
zreladdr-y	:= 0x40008000
params_phys-y	:= 0x40000100
initrd_phys-y	:= 0x40800000
endif

ifeq ($(CONFIG_ARCH_SDP1302), y)
zreladdr-y	:= 0xC0008000
params_phys-y	:= 0xC0000100
initrd_phys-y	:= 0xC0800000
endif

ifeq ($(CONFIG_ARCH_SDP1304), y)
zreladdr-y	:= 0x80008000
params_phys-y	:= 0x80000100
initrd_phys-y	:= 0x80800000
endif

ifeq ($(CONFIG_ARCH_SDP1106), y)
zreladdr-y	:= 0x40008000
params_phys-y	:= 0x40000100
initrd_phys-y	:= 0x40800000
endif

