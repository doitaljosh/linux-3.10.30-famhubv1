#
# Makefile for Notification service module
#
obj-$(CONFIG_SECURITY_SFD_NOTIFIER) := notifier.o

# Set source files that will be compiled
notifier-y += SfdNotifierEntry.o
notifier-y += SfdNotifier.o
notifier-y += SfdNotifierHookHandlers.o

# Set include directories
ccflags-y += -Isecurity/sfd/include
ccflags-y += -Isecurity/sfd/include/notifier
ccflags-y += -Isecurity/sfd/include/core
ccflags-y += -Isecurity/sfd/include/transport
ccflags-y += -Isecurity/sfd/include/protocol

# Set necessary definitions
ccflags-y += -DSF_TRANSPORT_NETLINK
ccflags-y += -DDEBUG_BUILD
ccflags-y += -DKS_MODULE
ccflags-y += -DSF_MOD_CLASS=SF_DEBUG_CLASS_NTF

# Set compiler options
ccflags-y += -Wall
