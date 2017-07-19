/*************************************************************************/ /*!
@Title          SHMOBILE Linux display driver structures and prototypes
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
#ifndef __SHMOBILELFB_H__
#define __SHMOBILELFB_H__

#include <linux/version.h>

#include <asm/atomic.h>

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/mutex.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
#define	SHLFB_CONSOLE_LOCK()		console_lock()
#define	SHLFB_CONSOLE_UNLOCK()	console_unlock()
#else
#define	SHLFB_CONSOLE_LOCK()		acquire_console_sem()
#define	SHLFB_CONSOLE_UNLOCK()	release_console_sem()
#endif

#define unref__ __attribute__ ((unused))

typedef void *       SHLFB_HANDLE;

typedef bool SHLFB_BOOL, *SHLFB_PBOOL;
#define	SHLFB_FALSE false
#define SHLFB_TRUE true

typedef	atomic_t	SHLFB_ATOMIC_BOOL;

typedef atomic_t	SHLFB_ATOMIC_INT;

typedef struct SHLFB_BUFFER_TAG
{
	struct SHLFB_BUFFER_TAG	*psNext;
	struct SHLFB_DEVINFO_TAG	*psDevInfo;

	struct work_struct sWork;

	/* Position of this buffer in the virtual framebuffer */
	unsigned long		     	ulYOffset;

	/* IMG structures used, to minimise API function code */
	/* replace with own structures where necessary */
	IMG_SYS_PHYADDR              	sSysAddr;
	IMG_CPU_VIRTADDR             	sCPUVAddr;
	PVRSRV_SYNC_DATA            	*psSyncData;

	SHLFB_HANDLE      		hCmdComplete;
	unsigned long    		ulSwapInterval;
} SHLFB_BUFFER;

/* SHLFB swapchain structure */
typedef struct SHLFB_SWAPCHAIN_TAG
{
	/* Swap chain ID */
	unsigned int			uiSwapChainID;

	/* number of buffers in swapchain */
	unsigned long       		ulBufferCount;

	/* list of buffers in the swapchain */
	SHLFB_BUFFER     		*psBuffer;

	/* Swap chain work queue */
	struct workqueue_struct   	*psWorkQueue;

	/*
	 * Set if we didn't manage to wait for VSync on last swap,
	 * or if we think we need to wait for VSync on the next flip.
	 * The flag helps to avoid jitter when the screen is
	 * unblanked, by forcing an extended wait for VSync before
	 * attempting the next flip.
	 */
	SHLFB_BOOL			bNotVSynced;

	/* Previous number of blank events */
	int				iBlankEvents;

	/* Framebuffer Device ID for messages (e.g. printk) */
	unsigned int            	uiFBDevID;
} SHLFB_SWAPCHAIN;

typedef struct SHLFB_FBINFO_TAG
{
	unsigned long       ulFBSize;
	unsigned long       ulBufferSize;
	unsigned long       ulRoundedBufferSize;
	unsigned long       ulWidth;
	unsigned long       ulHeight;
	unsigned long       ulByteStride;
	unsigned long       ulPhysicalWidthmm;
	unsigned long       ulPhysicalHeightmm;

	/* IMG structures used, to minimise API function code */
	/* replace with own structures where necessary */
	IMG_SYS_PHYADDR     sSysAddr;
	IMG_CPU_VIRTADDR    sCPUVAddr;

	/* pixelformat of system/primary surface */
	PVRSRV_PIXEL_FORMAT ePixelFormat;

#if defined(USE_COMPOSITION_BUFFER)
	SHLFB_BOOL		bIs2D;
	IMG_SYS_PHYADDR		*psPageList;
	struct ion_handle	*psIONHandle;
	IMG_UINT32			uiBytesPerPixel;
#endif
} SHLFB_FBINFO;

/* kernel device information structure */
typedef struct SHLFB_DEVINFO_TAG
{
	/* Framebuffer Device ID */
	unsigned int            uiFBDevID;

	/* PVR Device ID */
	unsigned int            uiPVRDevID;

	/* Swapchain create/destroy mutex */
	struct mutex		sCreateSwapChainMutex;

	/* system surface info */
	SHLFB_BUFFER          sSystemBuffer;

	/* jump table into PVR services */
	PVRSRV_DC_DISP2SRV_KMJTABLE	sPVRJTable;

	/* jump table into DC */
	PVRSRV_DC_SRV2DISP_KMJTABLE	sDCJTable;

	/* fb info structure */
	SHLFB_FBINFO          sFBInfo;

	/* Only one swapchain supported by this device so hang it here */
	SHLFB_SWAPCHAIN      *psSwapChain;

	/* Swap chain ID */
	unsigned int		uiSwapChainID;

	/* True if PVR Services is flushing its command queues */
	SHLFB_ATOMIC_BOOL     sFlushCommands;

	/* pointer to linux frame buffer information structure */
	struct fb_info         *psLINFBInfo;

	/* Linux Framebuffer event notification block */
	struct notifier_block   sLINNotifBlock;

	/* IMG structures used, to minimise API function code */
	/* replace with own structures where necessary */

	/* Address of the surface being displayed */
	IMG_DEV_VIRTADDR	sDisplayDevVAddr;

	DISPLAY_INFO            sDisplayInfo;

	/* Display format */
	DISPLAY_FORMAT          sDisplayFormat;

	/* Display dimensions */
	DISPLAY_DIMS            sDisplayDim;

	/* True if screen is blanked */
	SHLFB_ATOMIC_BOOL	sBlanked;

	/* Number of blank/unblank events */
	SHLFB_ATOMIC_INT	sBlankEvents;

#ifdef CONFIG_HAS_EARLYSUSPEND
	/* Set by early suspend */
	SHLFB_ATOMIC_BOOL	sEarlySuspendFlag;

	struct early_suspend    sEarlySuspend;
#endif

#if defined(SUPPORT_DRI_DRM)
	SHLFB_ATOMIC_BOOL     sLeaveVT;
#endif

}  SHLFB_DEVINFO;

#define	SHLFB_PAGE_SIZE 4096

/* DEBUG only printk */
#ifdef	DEBUG
#define	DEBUG_PRINTK(x) printk x
#else
#define	DEBUG_PRINTK(x)
#endif

#define DISPLAY_DEVICE_NAME "SH-Mobile Linux Display Driver"
#define	DRVNAME	"shmobilelfb"
#define	DEVNAME	DRVNAME
#define	DRIVER_PREFIX DRVNAME

typedef enum _SHLFB_ERROR_
{
	SHLFB_OK                             =  0,
	SHLFB_ERROR_GENERIC                  =  1,
	SHLFB_ERROR_OUT_OF_MEMORY            =  2,
	SHLFB_ERROR_TOO_FEW_BUFFERS          =  3,
	SHLFB_ERROR_INVALID_PARAMS           =  4,
	SHLFB_ERROR_INIT_FAILURE             =  5,
	SHLFB_ERROR_CANT_REGISTER_CALLBACK   =  6,
	SHLFB_ERROR_INVALID_DEVICE           =  7,
	SHLFB_ERROR_DEVICE_REGISTER_FAILED   =  8,
	SHLFB_ERROR_SET_UPDATE_MODE_FAILED   =  9
} SHLFB_ERROR;

typedef enum _SHLFB_UPDATE_MODE_
{
	SHLFB_UPDATE_MODE_UNDEFINED			= 0,
	SHLFB_UPDATE_MODE_MANUAL			= 1,
	SHLFB_UPDATE_MODE_AUTO			= 2,
	SHLFB_UPDATE_MODE_DISABLED			= 3
} SHLFB_UPDATE_MODE;

#ifndef UNREFERENCED_PARAMETER
#define	UNREFERENCED_PARAMETER(param) (param) = (param)
#endif

SHLFB_ERROR SHLFBInit(void);
SHLFB_ERROR SHLFBDeInit(void);

/* OS Specific APIs */
SHLFB_DEVINFO *SHLFBGetDevInfoPtr(unsigned uiFBDevID);
unsigned SHLFBMaxFBDevIDPlusOne(void);
void *SHLFBAllocKernelMem(unsigned long ulSize);
void SHLFBFreeKernelMem(void *pvMem);
SHLFB_ERROR SHLFBGetLibFuncAddr(char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable);
SHLFB_ERROR SHLFBCreateSwapQueue (SHLFB_SWAPCHAIN *psSwapChain);
void SHLFBDestroySwapQueue(SHLFB_SWAPCHAIN *psSwapChain);
void SHLFBInitBufferForSwap(SHLFB_BUFFER *psBuffer);
void SHLFBSwapHandler(SHLFB_BUFFER *psBuffer);
void SHLFBQueueBufferForSwap(SHLFB_SWAPCHAIN *psSwapChain, SHLFB_BUFFER *psBuffer);
void SHLFBFlip(SHLFB_DEVINFO *psDevInfo, SHLFB_BUFFER *psBuffer);
SHLFB_UPDATE_MODE SHLFBGetUpdateMode(SHLFB_DEVINFO *psDevInfo);
SHLFB_BOOL SHLFBSetUpdateMode(SHLFB_DEVINFO *psDevInfo, SHLFB_UPDATE_MODE eMode);
SHLFB_BOOL SHLFBWaitForVSync(SHLFB_DEVINFO *psDevInfo);
SHLFB_BOOL SHLFBManualSync(SHLFB_DEVINFO *psDevInfo);
SHLFB_BOOL SHLFBCheckModeAndSync(SHLFB_DEVINFO *psDevInfo);
SHLFB_ERROR SHLFBUnblankDisplay(SHLFB_DEVINFO *psDevInfo);
SHLFB_ERROR SHLFBEnableLFBEventNotification(SHLFB_DEVINFO *psDevInfo);
SHLFB_ERROR SHLFBDisableLFBEventNotification(SHLFB_DEVINFO *psDevInfo);
void SHLFBCreateSwapChainLockInit(SHLFB_DEVINFO *psDevInfo);
void SHLFBCreateSwapChainLockDeInit(SHLFB_DEVINFO *psDevInfo);
void SHLFBCreateSwapChainLock(SHLFB_DEVINFO *psDevInfo);
void SHLFBCreateSwapChainUnLock(SHLFB_DEVINFO *psDevInfo);
void SHLFBAtomicBoolInit(SHLFB_ATOMIC_BOOL *psAtomic, SHLFB_BOOL bVal);
void SHLFBAtomicBoolDeInit(SHLFB_ATOMIC_BOOL *psAtomic);
void SHLFBAtomicBoolSet(SHLFB_ATOMIC_BOOL *psAtomic, SHLFB_BOOL bVal);
SHLFB_BOOL SHLFBAtomicBoolRead(SHLFB_ATOMIC_BOOL *psAtomic);
void SHLFBAtomicIntInit(SHLFB_ATOMIC_INT *psAtomic, int iVal);
void SHLFBAtomicIntDeInit(SHLFB_ATOMIC_INT *psAtomic);
void SHLFBAtomicIntSet(SHLFB_ATOMIC_INT *psAtomic, int iVal);
int SHLFBAtomicIntRead(SHLFB_ATOMIC_INT *psAtomic);
void SHLFBAtomicIntInc(SHLFB_ATOMIC_INT *psAtomic);

#if defined(DEBUG)
void SHLFBPrintInfo(SHLFB_DEVINFO *psDevInfo);
#else
#define	SHLFBPrintInfo(psDevInfo)
#endif

#endif /* __SHMOBILELFB_H__ */

/******************************************************************************
 End of file (shmobilelfb.h)
******************************************************************************/
