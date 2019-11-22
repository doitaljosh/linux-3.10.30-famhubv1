#
# Makefile for the linux vdfs-filesystem routines.
#

ifndef KBUILD_MODULES
# out-of-tree invocation
KSRC = /lib/modules/$(shell uname -r)/build

modules clean:
	$(MAKE) -C "${KSRC}" M="${PWD}" $@

tools:
	$(MAKE) -C ../vdfs-tools/
endif

ifneq ($(KBUILD_EXTMOD),)
# building as external module
CONFIG_VDFS_FS = m
CONFIG_VDFS_DEBUG = y
ccflags-y += -DCONFIG_VDFS_MODULE=1
ccflags-y += -DCONFIG_VDFS_DEBUG=1
ccflags-y += -DCONFIG_VDFS_DEBUG_GET_BNODE=1
ccflags-y += -DCONFIG_VDFS_EXPERIMENTAL=1
ccflags-y += -DCONFIG_VDFS_CRC_CHECK=1
ccflags-y += -DCONFIG_VDFS_META_SANITY_CHECK=1
ccflags-y += -DCONFIG_VDFS_QUOTA=1
ccflags-y += -DCONFIG_VDFS_POSIX_ACL=1
ccflags-y += -DCONFIG_VDFS_DECRYPT_SUPPORT=1
#ccflags-y += -DCONFIG_VDFS_HW2_SUPPORT=1
GIT_REPO_PATH := "${KBUILD_EXTMOD}/../.git"
endif

ifdef VDFS_NO_WARN
EXTRA_CFLAGS+=-Werror
endif

ccflags-$(CONFIG_VDFS_NOOPTIMIZE) += -O0

obj-$(CONFIG_VDFS_FS) += vdfs.o

vdfs-y	:= btree.o bnode.o cattree.o file.o inode.o \
		   options.o super.o fsm.o ioctl.o \
		   extents.o snapshot.o orphan.o data.o \
		   cattree-helper.o xattr.o installed.o\
		   packtree_inode.o packtree.o quota.o

vdfs-$(CONFIG_VDFS_DEBUG)		+= debug.o

GIT_REPO_PATH ?= $(shell echo $(MAKEFILE_FULL_NAME) | sed "s/vdfs-driver\/Makefile/.git/g")
GIT_BRANCH = vdfs3.tizen.0453-2015_03_18
GIT_REV_HASH = af439f08a9824f0706275a830a7e7a9a664c2ae8
VERSION = vdfs3.tizen.0453-2015_03_18

ifneq ($(GIT_BRANCH),)
CFLAGS_super.o				+= -DVDFS_GIT_BRANCH=\"$(GIT_BRANCH)\"
CFLAGS_packtree_inode.o			+= -DVDFS_GIT_BRANCH=\"$(GIT_BRANCH)\"
endif
ifneq ($(GIT_REV_HASH),)
CFLAGS_super.o				+= -DVDFS_GIT_REV_HASH=\"$(GIT_REV_HASH)\"
CFLAGS_packtree_inode.o			+= -DVDFS_GIT_REV_HASH=\"$(GIT_REV_HASH)\"
endif
ifneq ($(VERSION),)
CFLAGS_super.o				+= -DVDFS_VERSION=\"$(VERSION)\"
CFLAGS_packtree_inode.o			+= -DVDFS_VERSION=\"$(VERSION)\"
endif

