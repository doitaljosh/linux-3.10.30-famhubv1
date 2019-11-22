#
# Makefile for the core component of the Security Framework
#
obj-$(CONFIG_SFLIB_CORE) := core.o

# Set source files that will be compiled
core-y += SfDebug.o
core-y += SfDebugLinux.o
core-y += SfMemoryCommon.o
core-y += SfMemoryKernel.o
core-y += SfTime.o
core-y += SfValidator.o

ccflags-y += -DDEBUG_BUILD
ccflags-y += -DKS_MODULE
ccflags-y += -DSF_MOD_CLASS=SF_DEBUG_CLASS_CORE