#
# Makefile for Dispatcher module
#
obj-$(CONFIG_SECURITY_SFD_DISPATCHER) := dispatcher.o

# Set source files that will be compiled
dispatcher-y += SfdDispatcher.o
dispatcher-y += SfdModuleInterface.o
dispatcher-y += SfdDispatcherHookHandlers.o
dispatcher-y += SfdPlatformInterface.o
dispatcher-y += SfRulesList.o

ccflags-y += -Isecurity/sfd/include
ccflags-y += -Isecurity/sfd/include/dispatcher

ccflags-y += -DSF_TRANSPORT_NETLINK
ccflags-y += -DDEBUG_BUILD
ccflags-y += -DKS_MODULE
ccflags-y += -DSF_MOD_CLASS=SF_DEBUG_CLASS_DSP
