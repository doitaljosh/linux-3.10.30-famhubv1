#
# Makefile for Unauthorized Execution Prevention module
#
obj-$(CONFIG_SECURITY_SFD_UEP) := uep.o

# Set source files that will be compiled
uep-y += SfdUepEntry.o
uep-y += SfdUep.o
uep-y += SfdUepHookHandlers.o
uep-y += SfdUepKey.o
uep-y += SfdUepSignatureContext.o
uep-y += SfdUepCache.o

# Set include directories
ccflags-y += -Isecurity/sfd/include
ccflags-y += -Isecurity/sfd/include/uep
ccflags-y += -Isecurity/sfd/include/core
ccflags-y += -Isecurity/sfd/include/transport
ccflags-y += -Isecurity/sfd/include/protocol

# Set necessary definitions
ccflags-y += -DSF_TRANSPORT_NETLINK
ccflags-y += -DDEBUG_BUILD
ccflags-y += -DKS_MODULE
ccflags-y += -DSF_MOD_CLASS=SF_DEBUG_CLASS_UEP

# Set compiler options
ccflags-y += -Wall
