#
# Makefile Security Filter Driver
#
obj-$(CONFIG_SECURITY_SFD) := sfd.o

# Set source files that will be compiled
sfd-y += SfdEntry.o
sfd-y += SfdContext.o
sfd-y += SfdConfiguration.o
sfd-y += SfdTimer.o

# Set sub directories will be used for compilation
subdir-core += lib/core
subdir-$(CONFIG_SECURITY_SFD_DISPATCHER) += dispatcher
subdir-$(CONFIG_SECURITY_SFD_NOTIFIER) += notifier
subdir-$(CONFIG_SECURITY_SFD_UEP) += uep

# Set objects of submodules to be compiled
obj-$(CONFIG_SECURITY_SFD_DISPATCHER) += dispatcher/built-in.o
obj-$(CONFIG_SECURITY_SFD_NOTIFIER) += notifier/
obj-$(CONFIG_SECURITY_SFD_UEP) += uep/

# Set include directories
ccflags-y += -Isecurity/sfd/include

# Set necessary definitions
ccflags-y += -DSF_TRANSPORT_NETLINK
ccflags-y += -DDEBUG_BUILD
ccflags-y += -DKS_MODULE
ccflags-y += -DSF_MOD_CLASS=SF_DEBUG_CLASS_SFD

# Set compiler options
ccflags-y += -Wall
