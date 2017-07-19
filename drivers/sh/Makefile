#
# Makefile for the SuperH specific drivers.
#
obj-y	:= intc/

obj-$(CONFIG_RENESAS_DUMMY_NETDEV)	+= dummy-netdev/
obj-$(CONFIG_RENESAS_MDMLOADER)		+= mdmloader/
obj-$(CONFIG_HAVE_CLK)		+= clk/
obj-$(CONFIG_MAPLE)		+= maple/
obj-$(CONFIG_SUPERHYWAY)	+= superhyway/

ifneq ($(CONFIG_PDC),y)
obj-y				+= pm_runtime.o
endif
