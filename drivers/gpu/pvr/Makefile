#
# Makefile for the sgx driver
#

ifeq ($(KBUILD_SRC),)
PVR_KER_DIR := drivers/gpu/pvr
else
PVR_KER_DIR := $(KBUILD_SRC)/drivers/gpu/pvr
endif

include drivers/gpu/pvr/config_kernel.mk

obj-$(CONFIG_POWERVR_SGX) += pvrsrvkm.o
obj-$(CONFIG_POWERVR_SGX) += shmobilelfb.o

ccflags-y += -include $(PVR_KER_DIR)/config_kernel.h \
 -Idrivers/gpu/pvr \
 -Idrivers/gpu/pvr/hwdefs \
 -Idrivers/gpu/pvr/system/include \
 -Idrivers/gpu/pvr/system/r8a73734

pvrsrvkm-y += \
 osfunc.o \
 mutils.o \
 mmap.o \
 module.o \
 pdump.o \
 proc.o \
 pvr_bridge_k.o \
 pvr_debug.o \
 mm.o \
 mutex.o \
 event.o \
 osperproc.o \
 buffer_manager.o \
 devicemem.o \
 handle.o \
 hash.o \
 lists.o \
 mem.o \
 mem_debug.o \
 metrics.o \
 osfunc_common.o \
 pdump_common.o \
 perproc.o \
 power.o \
 pvrsrv.o \
 ra.o \
 refcount.o \
 resman.o \
 bridged_support.o \
 bridged_pvr_bridge.o \
 system/$(PVR_SYSTEM)/sysconfig.o \
 system/$(PVR_SYSTEM)/sysutils.o

ifeq ($(SUPPORT_PVRSRV_DEVICE_CLASS),1)
pvrsrvkm-y += deviceclass.o queue.o
endif

ifeq ($(SUPPORT_ION),1)
pvrsrvkm-y += ion.o
endif

ifeq ($(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC),1)
pvrsrvkm-y += pvr_sync.o
endif

ifeq ($(TTRACE),1)
pvrsrvkm-y += ttrace.o
endif

ifeq ($(SUPPORT_PVRSRV_ANDROID_SYSTRACE),1)
pvrsrvkm-y += systrace.o
endif

pvrsrvkm-y += \
 bridged_sgx_bridge.o \
 sgxinit.o \
 sgxpower.o \
 sgxreset.o \
 sgxutils.o \
 sgxkick.o \
 sgxtransfer.o \
 mmu.o \
 pb.o

ifeq ($(SUPPORT_DRI_DRM),1)

pvrsrvkm-y += pvr_drm.o
ccflags-y += -Iinclude/drm

ifeq ($(PVR_DRI_DRM_NOT_PCI),1)
ccflags-y += linux_drm
endif

endif # SUPPORT_DRI_DRM

ccflags-y += \
 -Idrivers/gpu/pvr/dc_shmobilefb_linux \
 -Idrivers/video

shmobilelfb-y += \
	dc_shmobilefb_linux/shmobilelfb_displayclass.o \
	dc_shmobilefb_linux/shmobilelfb_linux.o

dbgdrv-y += \
 dbgdriv.o \
 ioctl.o \
 handle.o \
 hotkey.o \
 main.o \
 hostfunc.o
