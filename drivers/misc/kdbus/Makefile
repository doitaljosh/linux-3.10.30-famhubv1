obj-$(CONFIG_KDBUS) += kdbus.o

kdbus-objs := \
	bus.o \
	connection.o \
	endpoint.o \
	handle.o \
	memfd.o \
	main.o \
	match.o \
	message.o \
	metadata.o \
	names.o \
	notify.o \
	domain.o \
	policy.o \
	pool.o \
	util.o \
	compatibility/compatibility.o


# EXTRA_CFLAGS doesn't work for -I flag (nor ccflags-y), so this is kind of hack
# $(src) is Kbuild var - path to kdbus module, because PWD shows /usr/src/linux..
LINUXINCLUDE            := -I$(src)/compatibility $(LINUXINCLUDE)

EXTRA_CFLAGS += -Iinclude/linux/kdbus
