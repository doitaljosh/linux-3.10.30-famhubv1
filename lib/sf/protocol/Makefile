#
# Makefile for the protocol component of the Security Framework
#
obj-$(CONFIG_SFLIB_PROTOCOL) := protocol.o

# Set source files that will be compiled
protocol-y += SfEnvironmentFormat.o
protocol-y += SfOperationsFormat.o
protocol-y += SfPacket.o

ccflags-y += -DDEBUG_BUILD
ccflags-y += -Wall
ccflags-y += -DKS_MODULE
ccflags-y += -DSF_MOD_CLASS=SF_DEBUG_CLASS_PRT