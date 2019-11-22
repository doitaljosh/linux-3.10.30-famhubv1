#
# Makefile for the transport component of the Security Framework
#
obj-$(CONFIG_SFLIB_TRANSPORT) := transport.o

# Set source files that will be compiled
transport-y += SfSerialization.o
transport-y += SfProtocolHeaderSerialization.o
transport-y += SfPacketOperationSerialization.o
transport-y += SfPacketEnvironmentSerialization.o

transport-$(CONFIG_SFLIB_TRANSPORT_NETLINK) += netlink/SfNetlinkKernel.o
transport-$(CONFIG_SFLIB_TRANSPORT_NETLINK) += netlink/SfNetlinkSerialization.o

ccflags-y += -DDEBUG_BUILD
ccflags-y += -DSF_TRANSPORT_NETLINK
ccflags-y += -Wall
ccflags-y += -DKS_MODULE
ccflags-y += -DSF_MOD_CLASS=SF_DEBUG_CLASS_TRP
