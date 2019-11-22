#
# Makefile for the Linux proc filesystem routines.
#

obj-y   += proc.o

proc-y			:= nommu.o task_nommu.o
proc-$(CONFIG_MMU)	:= task_mmu.o

proc-y       += inode.o root.o base.o generic.o array.o \
		fd.o
proc-$(CONFIG_TTY)      += proc_tty.o
proc-y	+= cmdline.o
proc-y	+= consoles.o
proc-y	+= cpuinfo.o
proc-y	+= devices.o
proc-y	+= interrupts.o
proc-y	+= loadavg.o
proc-y	+= meminfo.o
proc-y	+= stat.o
proc-y	+= uptime.o
proc-y	+= version.o
proc-y	+= softirqs.o
proc-y	+= namespaces.o
proc-y	+= self.o
proc-$(CONFIG_PROC_SYSCTL)	+= proc_sysctl.o
proc-$(CONFIG_NET)		+= proc_net.o
proc-$(CONFIG_PROC_KCORE)	+= kcore.o
proc-$(CONFIG_PROC_VMCORE)	+= vmcore.o
proc-$(CONFIG_PROC_DEVICETREE)	+= proc_devtree.o
proc-$(CONFIG_PRINTK)	+= kmsg.o
proc-$(CONFIG_PROC_PAGE_MONITOR)	+= page.o
proc-$(CONFIG_FD_PID)	+= fd_pid.o
proc-$(CONFIG_VD_MEMFREE)	+= memfree.o
proc-$(CONFIG_KPI_SYSTEM_SUPPORT)       += kpi_fault_info.o
proc-$(CONFIG_WRITE_PROTECT)	+= protection.o
proc-$(CONFIG_EXECUTE_AUTHULD) += authuld.o
proc-$(CONFIG_PROC_VD_POLICY_LIST)         += vd_policy.o
proc-$(CONFIG_PROC_VD_TASK_LIST)   += vd_task_policy.o
proc-$(CONFIG_PROC_VD_SIGNAL_HANDLER_LIST) += vd_signal_policy.o
proc-y  += vd_proc_coredump.o
proc-$(CONFIG_SDP_MESSAGEBOX) += internal_msgbox.o
