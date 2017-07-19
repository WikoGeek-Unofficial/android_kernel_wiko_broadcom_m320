/*************************************************************************/ /*!
@Title          SHMobile linux display driver components
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/ /**************************************************************************/

/**************************************************************************
 The 3rd party driver is a specification of an API to integrate the IMG POWERVR
 Services driver with 3rd Party display hardware.  It is NOT a specification for
 a display controller driver, rather a specification to extend the API for a
 pre-existing driver for the display hardware.

 The 3rd party driver interface provides IMG POWERVR client drivers (e.g. PVR2D)
 with an API abstraction of the system's underlying display hardware, allowing
 the client drivers to indirectly control the display hardware and access its
 associated memory.

 Functions of the API include
 - query primary surface attributes (width, height, stride, pixel format, CPU
     physical and virtual address)
 - swap/flip chain creation and subsequent query of surface attributes
 - asynchronous display surface flipping, taking account of asynchronous read
 (flip) and write (render) operations to the display surface

 Note: having queried surface attributes the client drivers are able to map the
 display memory to any IMG POWERVR Services device by calling
 PVRSRVMapDeviceClassMemory with the display surface handle.

 This code is intended to be an example of how a pre-existing display driver may
 be extended to support the 3rd Party Display interface to POWERVR Services
 - IMG is not providing a display driver implementation.
 **************************************************************************/

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#include <asm/atomic.h>

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#else
#include <linux/module.h>
#endif

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/mutex.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "shmobilelfb.h"
#include "pvrmodule.h"
#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#include "3rdparty_dc_drm_shared.h"
#endif

#if !defined(PVR_LINUX_USING_WORKQUEUES)
#error "PVR_LINUX_USING_WORKQUEUES must be defined"
#endif

MODULE_SUPPORTED_DEVICE(DEVNAME);

void *SHLFBAllocKernelMem(unsigned long ulSize)
{
	return kmalloc(ulSize, GFP_KERNEL);
}

void SHLFBFreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}

void SHLFBCreateSwapChainLockInit(SHLFB_DEVINFO *psDevInfo)
{
	mutex_init(&psDevInfo->sCreateSwapChainMutex);
}

void SHLFBCreateSwapChainLockDeInit(SHLFB_DEVINFO *psDevInfo)
{
	mutex_destroy(&psDevInfo->sCreateSwapChainMutex);
}

void SHLFBCreateSwapChainLock(SHLFB_DEVINFO *psDevInfo)
{
	mutex_lock(&psDevInfo->sCreateSwapChainMutex);
}

void SHLFBCreateSwapChainUnLock(SHLFB_DEVINFO *psDevInfo)
{
	mutex_unlock(&psDevInfo->sCreateSwapChainMutex);
}

void SHLFBAtomicBoolInit(SHLFB_ATOMIC_BOOL *psAtomic, SHLFB_BOOL bVal)
{
	atomic_set(psAtomic, (int)bVal);
}

void SHLFBAtomicBoolDeInit(SHLFB_ATOMIC_BOOL *psAtomic)
{
}

void SHLFBAtomicBoolSet(SHLFB_ATOMIC_BOOL *psAtomic, SHLFB_BOOL bVal)
{
	atomic_set(psAtomic, (int)bVal);
}

SHLFB_BOOL SHLFBAtomicBoolRead(SHLFB_ATOMIC_BOOL *psAtomic)
{
	return (SHLFB_BOOL)atomic_read(psAtomic);
}

void SHLFBAtomicIntInit(SHLFB_ATOMIC_INT *psAtomic, int iVal)
{
	atomic_set(psAtomic, iVal);
}

void SHLFBAtomicIntDeInit(SHLFB_ATOMIC_INT *psAtomic)
{
}

void SHLFBAtomicIntSet(SHLFB_ATOMIC_INT *psAtomic, int iVal)
{
	atomic_set(psAtomic, iVal);
}

int SHLFBAtomicIntRead(SHLFB_ATOMIC_INT *psAtomic)
{
	return atomic_read(psAtomic);
}

void SHLFBAtomicIntInc(SHLFB_ATOMIC_INT *psAtomic)
{
	atomic_inc(psAtomic);
}

SHLFB_ERROR SHLFBGetLibFuncAddr (char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		return (SHLFB_ERROR_INVALID_PARAMS);
	}

	/* Nothing to do - should be exported from pvrsrv.ko */
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return (SHLFB_OK);
}

/* Inset a swap buffer into the swap chain work queue */
void SHLFBQueueBufferForSwap(SHLFB_SWAPCHAIN *psSwapChain, SHLFB_BUFFER *psBuffer)
{
	int res = queue_work(psSwapChain->psWorkQueue, &psBuffer->sWork);

	if (res == 0)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": %s: Device %u: Buffer already on work queue\n", __FUNCTION__, psSwapChain->uiFBDevID);
	}
}

/* Process an item on a swap chain work queue */
static void WorkQueueHandler(struct work_struct *psWork)
{
	SHLFB_BUFFER *psBuffer = container_of(psWork, SHLFB_BUFFER, sWork);

	SHLFBSwapHandler(psBuffer);
}

/* Create a swap chain work queue */
SHLFB_ERROR SHLFBCreateSwapQueue(SHLFB_SWAPCHAIN *psSwapChain)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	/*
	 * Calling alloc_ordered_workqueue with the WQ_FREEZABLE and
	 * WQ_MEM_RECLAIM flags set, (currently) has the same effect as
	 * calling create_freezable_workqueue. None of the other WQ
	 * flags are valid. Setting WQ_MEM_RECLAIM should allow the
	 * workqueue to continue to service the swap chain in low memory
	 * conditions, preventing the driver from holding on to
	 * resources longer than it needs to.
	 */
	psSwapChain->psWorkQueue = alloc_ordered_workqueue(DEVNAME, WQ_FREEZABLE | WQ_MEM_RECLAIM);
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
	psSwapChain->psWorkQueue = create_freezable_workqueue(DEVNAME);
#else
	/*
	 * Create a single-threaded, freezable, rt-prio workqueue.
	 * Such workqueues are frozen with user threads when a system
	 * suspends, before driver suspend entry points are called.
	 * This ensures this driver will not call into the Linux
	 * framebuffer driver after the latter is suspended.
	 */
	psSwapChain->psWorkQueue = __create_workqueue(DEVNAME, 1, 1, 1);
#endif
#endif
	if (psSwapChain->psWorkQueue == NULL)
	{
		printk(KERN_ERR DRIVER_PREFIX ": %s: Device %u: Couldn't create workqueue\n", __FUNCTION__, psSwapChain->uiFBDevID);

		return (SHLFB_ERROR_INIT_FAILURE);
	}

	return (SHLFB_OK);
}

/* Prepare buffer for insertion into a swap chain work queue */
void SHLFBInitBufferForSwap(SHLFB_BUFFER *psBuffer)
{
	INIT_WORK(&psBuffer->sWork, WorkQueueHandler);
}

/* Destroy a swap chain work queue */
void SHLFBDestroySwapQueue(SHLFB_SWAPCHAIN *psSwapChain)
{
	destroy_workqueue(psSwapChain->psWorkQueue);
}

/* Flip display to given buffer */
void SHLFBFlip(SHLFB_DEVINFO *psDevInfo, SHLFB_BUFFER *psBuffer)
{
#if defined(USE_LINUX_FRAME_BUFFER)
	struct fb_var_screeninfo sFBVar;
	int res;
	unsigned long ulYResVirtual;

	SHLFB_CONSOLE_LOCK();

	sFBVar = psDevInfo->psLINFBInfo->var;

	sFBVar.xoffset = 0;
	sFBVar.yoffset = psBuffer->ulYOffset;

	ulYResVirtual = psBuffer->ulYOffset + sFBVar.yres;

#if !defined(PVR_SHLFB_DONT_USE_FB_PAN_DISPLAY)
	/*
	 * Attempt to change the virtual screen resolution if it is too
	 * small.  Note that fb_set_var also pans the display.
	 */
	if (sFBVar.xres_virtual != sFBVar.xres || sFBVar.yres_virtual < ulYResVirtual)
#endif /* !defined(PVR_SHLFB_DONT_USE_FB_PAN_DISPLAY) */
	{
		sFBVar.xres_virtual = sFBVar.xres;
		sFBVar.yres_virtual = ulYResVirtual;

		sFBVar.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

		res = fb_set_var(psDevInfo->psLINFBInfo, &sFBVar);
		if (res != 0)
		{
			printk(KERN_ERR DRIVER_PREFIX ": %s: Device %u: fb_set_var failed (Y Offset: %lu, Error: %d)\n", __FUNCTION__, psDevInfo->uiFBDevID, psBuffer->ulYOffset, res);
		}
	}
#if !defined(PVR_SHLFB_DONT_USE_FB_PAN_DISPLAY)
	else
	{
		res = fb_pan_display(psDevInfo->psLINFBInfo, &sFBVar);
		if (res != 0)
		{
			printk(KERN_ERR DRIVER_PREFIX ": %s: Device %u: fb_pan_display failed (Y Offset: %lu, Error: %d)\n", __FUNCTION__, psDevInfo->uiFBDevID, psBuffer->ulYOffset, res);
		}
	}
#endif /* !defined(PVR_SHLFB_DONT_USE_FB_PAN_DISPLAY) */

	SHLFB_CONSOLE_UNLOCK();
#endif
}

/* Wait for VSync */
SHLFB_BOOL SHLFBWaitForVSync(SHLFB_DEVINFO *psDevInfo)
{
	if(psDevInfo->psLINFBInfo->fbops->fb_ioctl != NULL)
		psDevInfo->psLINFBInfo->fbops->fb_ioctl(psDevInfo->psLINFBInfo, FBIO_WAITFORVSYNC, 0);

	return SHLFB_TRUE;
}

/* Linux Framebuffer event notification handler */
static int SHLFBFrameBufferEvents(struct notifier_block *psNotif,
                             unsigned long event, void *data)
{
	SHLFB_DEVINFO *psDevInfo;
	struct fb_event *psFBEvent = (struct fb_event *)data;
	struct fb_info *psFBInfo = psFBEvent->info;
	SHLFB_BOOL bBlanked;

	/* Only interested in blanking events */
	if (event != FB_EVENT_BLANK)
	{
		return 0;
	}

	bBlanked = (*(IMG_INT *)psFBEvent->data != 0) ? SHLFB_TRUE: SHLFB_FALSE;

	psDevInfo = SHLFBGetDevInfoPtr(psFBInfo->node);

#if 0
	if (psDevInfo != NULL)
	{
		if (bBlanked)
		{
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: Device %u: Blank event received\n", __FUNCTION__, psDevInfo->uiFBDevID));
		}
		else
		{
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: Device %u: Unblank event received\n", __FUNCTION__, psDevInfo->uiFBDevID));
		}
	}
	else
	{
		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX ": %s: Device %u: Blank/Unblank event for unknown framebuffer\n", __FUNCTION__, psFBInfo->node));
	}
#endif

	if (psDevInfo != NULL)
	{
		SHLFBAtomicBoolSet(&psDevInfo->sBlanked, bBlanked);
		SHLFBAtomicIntInc(&psDevInfo->sBlankEvents);
	}

	return 0;
}

/* Unblank the screen */
SHLFB_ERROR SHLFBUnblankDisplay(SHLFB_DEVINFO *psDevInfo)
{
	int res;

	SHLFB_CONSOLE_LOCK();
	res = fb_blank(psDevInfo->psLINFBInfo, 0);
	SHLFB_CONSOLE_UNLOCK();
	if (res != 0 && res != -EINVAL)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: fb_blank failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, res);
		return (SHLFB_ERROR_GENERIC);
	}

	return (SHLFB_OK);
}

#ifdef CONFIG_HAS_EARLYSUSPEND

static void SHLFBEarlySuspendHandler(struct early_suspend *h)
{
	unsigned uiMaxFBDevIDPlusOne = SHLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i=0; i < uiMaxFBDevIDPlusOne; i++)
	{
		SHLFB_DEVINFO *psDevInfo = SHLFBGetDevInfoPtr(i);

		if (psDevInfo != NULL)
		{
			SHLFBAtomicBoolSet(&psDevInfo->sEarlySuspendFlag, SHLFB_TRUE);
		}
	}
}

static void SHLFBEarlyResumeHandler(struct early_suspend *h)
{
	unsigned uiMaxFBDevIDPlusOne = SHLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i=0; i < uiMaxFBDevIDPlusOne; i++)
	{
		SHLFB_DEVINFO *psDevInfo = SHLFBGetDevInfoPtr(i);

		if (psDevInfo != NULL)
		{
			SHLFBAtomicBoolSet(&psDevInfo->sEarlySuspendFlag, SHLFB_FALSE);
		}
	}
}

#endif /* CONFIG_HAS_EARLYSUSPEND */

/* Set up Linux Framebuffer event notification */
SHLFB_ERROR SHLFBEnableLFBEventNotification(SHLFB_DEVINFO *psDevInfo)
{
	int                res;
	SHLFB_ERROR         eError;

	/* Set up Linux Framebuffer event notification */
	memset(&psDevInfo->sLINNotifBlock, 0, sizeof(psDevInfo->sLINNotifBlock));

	psDevInfo->sLINNotifBlock.notifier_call = SHLFBFrameBufferEvents;

	SHLFBAtomicBoolSet(&psDevInfo->sBlanked, SHLFB_FALSE);
	SHLFBAtomicIntSet(&psDevInfo->sBlankEvents, 0);

	res = fb_register_client(&psDevInfo->sLINNotifBlock);
	if (res != 0)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: fb_register_client failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, res);

		return (SHLFB_ERROR_GENERIC);
	}

	eError = SHLFBUnblankDisplay(psDevInfo);
	if (eError != SHLFB_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: UnblankDisplay failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, eError);
		return eError;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	psDevInfo->sEarlySuspend.suspend = SHLFBEarlySuspendHandler;
	psDevInfo->sEarlySuspend.resume = SHLFBEarlyResumeHandler;
	psDevInfo->sEarlySuspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	register_early_suspend(&psDevInfo->sEarlySuspend);
#endif

	return (SHLFB_OK);
}

/* Disable Linux Framebuffer event notification */
SHLFB_ERROR SHLFBDisableLFBEventNotification(SHLFB_DEVINFO *psDevInfo)
{
	int res;

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&psDevInfo->sEarlySuspend);
#endif

	/* Unregister for Framebuffer events */
	res = fb_unregister_client(&psDevInfo->sLINNotifBlock);
	if (res != 0)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: fb_unregister_client failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, res);
		return (SHLFB_ERROR_GENERIC);
	}

	SHLFBAtomicBoolSet(&psDevInfo->sBlanked, SHLFB_FALSE);

	return (SHLFB_OK);
}

#if defined(SUPPORT_DRI_DRM) && defined(PVR_DISPLAY_CONTROLLER_DRM_IOCTL)
static SHLFB_DEVINFO *SHLFBPVRDevIDToDevInfo(unsigned uiPVRDevID)
{
	unsigned uiMaxFBDevIDPlusOne = SHLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i=0; i < uiMaxFBDevIDPlusOne; i++)
	{
		SHLFB_DEVINFO *psDevInfo = SHLFBGetDevInfoPtr(i);

		if (psDevInfo->uiPVRDevID == uiPVRDevID)
		{
			return psDevInfo;
		}
	}

	printk(KERN_ERR DRIVER_PREFIX
		": %s: PVR Device %u: Couldn't find device\n", __FUNCTION__, uiPVRDevID);

	return NULL;
}

int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Ioctl)(struct drm_device unref__ *dev, void *arg, struct drm_file unref__ *pFile)
{
	uint32_t *puiArgs;
	uint32_t uiCmd;
	unsigned uiPVRDevID;
	int ret = 0;
	SHLFB_DEVINFO *psDevInfo;

	if (arg == NULL)
	{
		return -EFAULT;
	}

	puiArgs = (uint32_t *)arg;
	uiCmd = puiArgs[PVR_DRM_DISP_ARG_CMD];
	uiPVRDevID = puiArgs[PVR_DRM_DISP_ARG_DEV];

	psDevInfo = SHLFBPVRDevIDToDevInfo(uiPVRDevID);
	if (psDevInfo == NULL)
	{
		return -EINVAL;
	}


	switch (uiCmd)
	{
		case PVR_DRM_DISP_CMD_LEAVE_VT:
		case PVR_DRM_DISP_CMD_ENTER_VT:
		{
			SHLFB_BOOL bLeaveVT = (uiCmd == PVR_DRM_DISP_CMD_LEAVE_VT);
			DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX ": %s: PVR Device %u: %s\n",
				__FUNCTION__, uiPVRDevID,
				bLeaveVT ? "Leave VT" : "Enter VT"));

			SHLFBCreateSwapChainLock(psDevInfo);

			SHLFBAtomicBoolSet(&psDevInfo->sLeaveVT, bLeaveVT);
			if (psDevInfo->psSwapChain != NULL)
			{
				flush_workqueue(psDevInfo->psSwapChain->psWorkQueue);

				if (bLeaveVT)
				{
					SHLFBFlip(psDevInfo, &psDevInfo->sSystemBuffer);
				}
			}

			SHLFBCreateSwapChainUnLock(psDevInfo);
			(void) SHLFBUnblankDisplay(psDevInfo);
			break;
		}
		case PVR_DRM_DISP_CMD_ON:
		case PVR_DRM_DISP_CMD_STANDBY:
		case PVR_DRM_DISP_CMD_SUSPEND:
		case PVR_DRM_DISP_CMD_OFF:
		{
			int iFBMode;
#if defined(DEBUG)
			{
				const char *pszMode;
				switch(uiCmd)
				{
					case PVR_DRM_DISP_CMD_ON:
						pszMode = "On";
						break;
					case PVR_DRM_DISP_CMD_STANDBY:
						pszMode = "Standby";
						break;
					case PVR_DRM_DISP_CMD_SUSPEND:
						pszMode = "Suspend";
						break;
					case PVR_DRM_DISP_CMD_OFF:
						pszMode = "Off";
						break;
					default:
						pszMode = "(Unknown Mode)";
						break;
				}
				printk(KERN_WARNING DRIVER_PREFIX ": %s: PVR Device %u: Display %s\n",
				__FUNCTION__, uiPVRDevID, pszMode);
			}
#endif
			switch(uiCmd)
			{
				case PVR_DRM_DISP_CMD_ON:
					iFBMode = FB_BLANK_UNBLANK;
					break;
				case PVR_DRM_DISP_CMD_STANDBY:
					iFBMode = FB_BLANK_HSYNC_SUSPEND;
					break;
				case PVR_DRM_DISP_CMD_SUSPEND:
					iFBMode = FB_BLANK_VSYNC_SUSPEND;
					break;
				case PVR_DRM_DISP_CMD_OFF:
					iFBMode = FB_BLANK_POWERDOWN;
					break;
				default:
					return -EINVAL;
			}

			SHLFBCreateSwapChainLock(psDevInfo);

			if (psDevInfo->psSwapChain != NULL)
			{
				flush_workqueue(psDevInfo->psSwapChain->psWorkQueue);
			}

			SHLFB_CONSOLE_LOCK();
			ret = fb_blank(psDevInfo->psLINFBInfo, iFBMode);
			SHLFB_CONSOLE_UNLOCK();

			SHLFBCreateSwapChainUnLock(psDevInfo);

			break;
		}
		default:
		{
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}
#endif

/* Insert the driver into the kernel */
#if defined(SUPPORT_DRI_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device unref__ *dev)
#else
static int __init SHLFB_Init(void)
#endif
{

	if(SHLFBInit() != SHLFB_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX ": %s: SHLFBInit failed\n", __FUNCTION__);
		return -ENODEV;
	}

	return 0;

}

/* Remove the driver from the kernel */
#if defined(SUPPORT_DRI_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device unref__ *dev)
#else
static void __exit SHLFB_Cleanup(void)
#endif
{
	if(SHLFBDeInit() != SHLFB_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX ": %s: SHLFBDeInit failed\n", __FUNCTION__);
	}
}

#if !defined(SUPPORT_DRI_DRM)
/*
 These macro calls define the initialisation and removal functions of the
 driver.  Although they are prefixed `module_', they apply when compiling
 statically as well; in both cases they define the function the kernel will
 run to start/stop the driver.
*/
late_initcall(SHLFB_Init);
module_exit(SHLFB_Cleanup);
#endif
