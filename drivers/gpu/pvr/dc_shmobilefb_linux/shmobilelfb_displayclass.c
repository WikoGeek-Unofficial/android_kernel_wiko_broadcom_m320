/*************************************************************************/ /*!
@Title          SHMobile common display driver components
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

/*
 * SHMobile Linux 3rd party display driver.
 * This is based on the Generic PVR Linux Framebuffer 3rd party display
 * driver, with SHMobile specific extensions to support flipping.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/mm.h>

/* IMG services headers */
#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "shmobilelfb.h"

#if defined(CONFIG_MISC_R_MOBILE_COMPOSER)
#include <linux/sh_mobile_composer.h>
#endif

#include <linux/ion.h>

#define SHLFB_COMMAND_COUNT		1

#define	SHLFB_VSYNC_SETTLE_COUNT	5

#define	SHLFB_MAX_NUM_DEVICES		FB_MAX
#if (SHLFB_MAX_NUM_DEVICES > FB_MAX)
#error "SHLFB_MAX_NUM_DEVICES must not be greater than FB_MAX"
#endif

static SHLFB_DEVINFO *gapsDevInfo[SHLFB_MAX_NUM_DEVICES];

/* Top level 'hook ptr' */
static PFN_DC_GET_PVRJTABLE gpfnGetPVRJTable = NULL;

extern struct ion_client *gpsIONClient;

/* Round x up to a multiple of y */
static inline unsigned long RoundUpToMultiple(unsigned long x, unsigned long y)
{
	unsigned long div = x / y;
	unsigned long rem = x % y;

	return (div + ((rem == 0) ? 0 : 1)) * y;
}

/* Greatest common divisor of x and y */
static unsigned long GCD(unsigned long x, unsigned long y)
{
	while (y != 0)
	{
		unsigned long r = x % y;
		x = y;
		y = r;
	}

	return x;
}

/* Least common multiple of x and y */
static unsigned long LCM(unsigned long x, unsigned long y)
{
	unsigned long gcd = GCD(x, y);

	return (gcd == 0) ? 0 : ((x / gcd) * y);
}

unsigned SHLFBMaxFBDevIDPlusOne(void)
{
	return SHLFB_MAX_NUM_DEVICES;
}

/* Returns DevInfo pointer for a given device */
SHLFB_DEVINFO *SHLFBGetDevInfoPtr(unsigned uiFBDevID)
{
	WARN_ON(uiFBDevID >= SHLFBMaxFBDevIDPlusOne());

	if (uiFBDevID >= SHLFB_MAX_NUM_DEVICES)
	{
		return NULL;
	}

	return gapsDevInfo[uiFBDevID];
}

/* Sets the DevInfo pointer for a given device */
static inline void SHLFBSetDevInfoPtr(unsigned uiFBDevID, SHLFB_DEVINFO *psDevInfo)
{
	WARN_ON(uiFBDevID >= SHLFB_MAX_NUM_DEVICES);

	if (uiFBDevID < SHLFB_MAX_NUM_DEVICES)
	{
		gapsDevInfo[uiFBDevID] = psDevInfo;
	}
}

static inline SHLFB_BOOL SwapChainHasChanged(SHLFB_DEVINFO *psDevInfo, SHLFB_SWAPCHAIN *psSwapChain)
{
	return (psDevInfo->psSwapChain != psSwapChain) ||
		(psDevInfo->uiSwapChainID != psSwapChain->uiSwapChainID);
}

/* Don't wait for vertical sync */
static inline SHLFB_BOOL DontWaitForVSync(SHLFB_DEVINFO *psDevInfo)
{
	SHLFB_BOOL bDontWait;

	bDontWait = SHLFBAtomicBoolRead(&psDevInfo->sBlanked) ||
			SHLFBAtomicBoolRead(&psDevInfo->sFlushCommands);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	bDontWait = bDontWait || SHLFBAtomicBoolRead(&psDevInfo->sEarlySuspendFlag);
#endif
#if defined(SUPPORT_DRI_DRM)
	bDontWait = bDontWait || SHLFBAtomicBoolRead(&psDevInfo->sLeaveVT);
#endif
	return bDontWait;
}

/*
 * SetDCState
 * Called from services.
 */
static IMG_VOID SetDCState(IMG_HANDLE hDevice, IMG_UINT32 ui32State)
{
	SHLFB_DEVINFO *psDevInfo = (SHLFB_DEVINFO *)hDevice;

	switch (ui32State)
	{
		case DC_STATE_FLUSH_COMMANDS:
			SHLFBAtomicBoolSet(&psDevInfo->sFlushCommands, SHLFB_TRUE);
			break;
		case DC_STATE_NO_FLUSH_COMMANDS:
			SHLFBAtomicBoolSet(&psDevInfo->sFlushCommands, SHLFB_FALSE);
			break;
/* JC		case DC_STATE_FORCE_SWAP_TO_SYSTEM:
			SHLFBFlip(psDevInfo, &psDevInfo->sSystemBuffer);
			break;*/
		default:
			break;
	}
}

/*
 * OpenDCDevice
 * Called from services.
 */
static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 uiPVRDevID,
                                 IMG_HANDLE *phDevice,
                                 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	SHLFB_DEVINFO *psDevInfo;
	SHLFB_ERROR eError;
	unsigned uiMaxFBDevIDPlusOne = SHLFBMaxFBDevIDPlusOne();
	unsigned i;

	for (i = 0; i < uiMaxFBDevIDPlusOne; i++)
	{
		psDevInfo = SHLFBGetDevInfoPtr(i);
		if (psDevInfo != NULL && psDevInfo->uiPVRDevID == uiPVRDevID)
		{
			break;
		}
	}
	if (i == uiMaxFBDevIDPlusOne)
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": %s: PVR Device %u not found\n", __FUNCTION__, uiPVRDevID));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* store the system surface sync data */
	psDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;

	eError = SHLFBUnblankDisplay(psDevInfo);
	if (eError != SHLFB_OK)
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: SHLFBUnblankDisplay failed (%d)\n", __FUNCTION__, psDevInfo->uiFBDevID, eError));
		return PVRSRV_ERROR_UNBLANK_DISPLAY_FAILED;
	}

	/* return handle to the devinfo */
	*phDevice = (IMG_HANDLE)psDevInfo;

	return PVRSRV_OK;
}

/*
 * CloseDCDevice
 * Called from services.
 */
static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
#if defined(SUPPORT_DRI_DRM)
	SHLFB_DEVINFO *psDevInfo = (SHLFB_DEVINFO *)hDevice;

	SHLFBAtomicBoolSet(&psDevInfo->sLeaveVT, SHLFB_FALSE);
	(void) SHLFBUnblankDisplay(psDevInfo);
#else
	UNREFERENCED_PARAMETER(hDevice);
#endif
	return PVRSRV_OK;
}

/*
 * EnumDCFormats
 * Called from services.
 */
static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE hDevice,
                                  IMG_UINT32 *pui32NumFormats,
                                  DISPLAY_FORMAT *psFormat)
{
	SHLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !pui32NumFormats)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;

	*pui32NumFormats = 1;

	if(psFormat)
	{
		psFormat[0] = psDevInfo->sDisplayFormat;
	}

	return PVRSRV_OK;
}

/*
 * EnumDCDims
 * Called from services.
 */
static PVRSRV_ERROR EnumDCDims(IMG_HANDLE hDevice,
                               DISPLAY_FORMAT *psFormat,
                               IMG_UINT32 *pui32NumDims,
                               DISPLAY_DIMS *psDim)
{
	SHLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !psFormat || !pui32NumDims)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;

	*pui32NumDims = 1;

	/* No need to look at psFormat; there is only one */
	if(psDim)
	{
		psDim[0] = psDevInfo->sDisplayDim;
	}

	return PVRSRV_OK;
}


/*
 * GetDCSystemBuffer
 * Called from services.
 */
static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	SHLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;

	*phBuffer = (IMG_HANDLE)&psDevInfo->sSystemBuffer;

	return PVRSRV_OK;
}


/*
 * GetDCInfo
 * Called from services.
 */
static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	SHLFB_DEVINFO	*psDevInfo;

	if(!hDevice || !psDCInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;

	*psDCInfo = psDevInfo->sDisplayInfo;

	return PVRSRV_OK;
}

/*
 * GetDCBufferAddr
 * Called from services.
 */
static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE        hDevice,
                                    IMG_HANDLE        hBuffer,
                                    IMG_SYS_PHYADDR   **ppsSysAddr,
                                    IMG_UINT32        *pui32ByteSize,
                                    IMG_VOID          **ppvCpuVAddr,
                                    IMG_HANDLE        *phOSMapInfo,
                                    IMG_BOOL          *pbIsContiguous,
	                                IMG_UINT32		  *pui32TilingStride)
{
	SHLFB_DEVINFO	*psDevInfo;
	SHLFB_BUFFER *psSystemBuffer;

	UNREFERENCED_PARAMETER(pui32TilingStride);

	if(!hDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(!hBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (!ppsSysAddr)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (!pui32ByteSize)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;

	psSystemBuffer = (SHLFB_BUFFER *)hBuffer;

	*ppsSysAddr = &psSystemBuffer->sSysAddr;

	*pui32ByteSize = (IMG_UINT32)psDevInfo->sFBInfo.ulBufferSize;

	if (ppvCpuVAddr)
	{
		*ppvCpuVAddr = psSystemBuffer->sCPUVAddr;
	}

	if (phOSMapInfo)
	{
		*phOSMapInfo = (IMG_HANDLE)0;
	}

	if (pbIsContiguous)
	{
		*pbIsContiguous = IMG_TRUE;
	}

	return PVRSRV_OK;
}

/*
 * CreateDCSwapChain
 * Called from services.
 */
static PVRSRV_ERROR CreateDCSwapChain(IMG_HANDLE hDevice,
                                      IMG_UINT32 ui32Flags,
                                      DISPLAY_SURF_ATTRIBUTES *psDstSurfAttrib,
                                      DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
                                      IMG_UINT32 ui32BufferCount,
                                      PVRSRV_SYNC_DATA **ppsSyncData,
                                      IMG_UINT32 ui32OEMFlags,
                                      IMG_HANDLE *phSwapChain,
                                      IMG_UINT32 *pui32SwapChainID)
{
	SHLFB_DEVINFO	*psDevInfo;
	SHLFB_SWAPCHAIN *psSwapChain;
	SHLFB_BUFFER *psBuffer = NULL;
	IMG_UINT32 i;
	PVRSRV_ERROR eError;

	UNREFERENCED_PARAMETER(ui32OEMFlags);
	UNREFERENCED_PARAMETER(ui32Flags);

	/* Check parameters */
	if(!hDevice
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !ppsSyncData
	|| !phSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;

	/* Do we support swap chains? */
	if (psDevInfo->sDisplayInfo.ui32MaxSwapChains == 0)
	{
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	SHLFBCreateSwapChainLock(psDevInfo);

	/* The driver only supports a single swapchain */
	if(psDevInfo->psSwapChain != NULL)
	{
		eError = PVRSRV_ERROR_FLIP_CHAIN_EXISTS;
		goto ExitUnLock;
	}

	/* create a swapchain structure */
	psSwapChain = (SHLFB_SWAPCHAIN*)SHLFBAllocKernelMem(sizeof(SHLFB_SWAPCHAIN));
	if(!psSwapChain)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ExitUnLock;
	}

	if(ui32BufferCount > 0)
	{
		IMG_UINT32 ui32BuffersToSkip;

		/* Check the buffer count */
		if(ui32BufferCount > psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers)
		{
			eError = PVRSRV_ERROR_TOOMANYBUFFERS;
			goto ErrorFreeSwapChain;
		}

		if ((psDevInfo->sFBInfo.ulRoundedBufferSize * (unsigned long)ui32BufferCount) > psDevInfo->sFBInfo.ulFBSize)
		{
			eError = PVRSRV_ERROR_TOOMANYBUFFERS;
			goto ErrorFreeSwapChain;
		}

		/*
		 * We will allocate the swap chain buffers at the back of the frame
		 * buffer area.  This preserves the front portion, which may be being
		 * used by other Linux Framebuffer based applications.
		 */
		ui32BuffersToSkip = psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers - ui32BufferCount;

		/*
		 *	Verify the DST/SRC attributes,
		 *	SRC/DST must match the current display mode config
		*/
		if(psDstSurfAttrib->pixelformat != psDevInfo->sDisplayFormat.pixelformat
		|| psDstSurfAttrib->sDims.ui32ByteStride != psDevInfo->sDisplayDim.ui32ByteStride
		|| psDstSurfAttrib->sDims.ui32Width != psDevInfo->sDisplayDim.ui32Width
		|| psDstSurfAttrib->sDims.ui32Height != psDevInfo->sDisplayDim.ui32Height)
		{
			/* DST doesn't match the current mode */
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto ErrorFreeSwapChain;
		}

		if(psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
		|| psDstSurfAttrib->sDims.ui32ByteStride != psSrcSurfAttrib->sDims.ui32ByteStride
		|| psDstSurfAttrib->sDims.ui32Width != psSrcSurfAttrib->sDims.ui32Width
		|| psDstSurfAttrib->sDims.ui32Height != psSrcSurfAttrib->sDims.ui32Height)
		{
			/* DST doesn't match the SRC */
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto ErrorFreeSwapChain;
		}

		psBuffer = (SHLFB_BUFFER*)SHLFBAllocKernelMem(sizeof(SHLFB_BUFFER) * ui32BufferCount);
		if(!psBuffer)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto ErrorFreeSwapChain;
		}

		psSwapChain->ulBufferCount = (unsigned long)ui32BufferCount;
		psSwapChain->psBuffer = psBuffer;
		psSwapChain->bNotVSynced = SHLFB_TRUE;
		psSwapChain->uiFBDevID = psDevInfo->uiFBDevID;

		/* Link the buffers */
		for(i=0; i<ui32BufferCount-1; i++)
		{
			psBuffer[i].psNext = &psBuffer[i+1];
		}
		/* and link last to first */
		psBuffer[i].psNext = &psBuffer[0];

		/* Configure the swapchain buffers */
		for(i=0; i<ui32BufferCount; i++)
		{
			IMG_UINT32 ui32SwapBuffer = i + ui32BuffersToSkip;
			IMG_UINT32 ui32BufferOffset = ui32SwapBuffer * (IMG_UINT32)psDevInfo->sFBInfo.ulRoundedBufferSize;

			psBuffer[i].psSyncData = ppsSyncData[i];

			psBuffer[i].sSysAddr.uiAddr = psDevInfo->sFBInfo.sSysAddr.uiAddr + ui32BufferOffset;
			psBuffer[i].sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr + ui32BufferOffset;
			psBuffer[i].ulYOffset = ui32BufferOffset / psDevInfo->sFBInfo.ulByteStride;
			psBuffer[i].psDevInfo = psDevInfo;

			SHLFBInitBufferForSwap(&psBuffer[i]);
		}
	}
	else
	{
		psSwapChain->psBuffer = NULL;
	}

	if (SHLFBCreateSwapQueue(psSwapChain) != SHLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": %s: Device %u: Failed to create workqueue\n", __FUNCTION__, psDevInfo->uiFBDevID);
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto ErrorFreeBuffers;
	}

	if (SHLFBEnableLFBEventNotification(psDevInfo)!= SHLFB_OK)
	{
		eError = PVRSRV_ERROR_UNABLE_TO_ENABLE_EVENT;
		printk(KERN_WARNING DRIVER_PREFIX ": %s: Device %u: Couldn't enable framebuffer event notification\n", __FUNCTION__, psDevInfo->uiFBDevID);
		goto ErrorDestroySwapQueue;
	}

	psDevInfo->uiSwapChainID++;
	if (psDevInfo->uiSwapChainID == 0)
	{
		psDevInfo->uiSwapChainID++;
	}

	psSwapChain->uiSwapChainID = psDevInfo->uiSwapChainID;

	psDevInfo->psSwapChain = psSwapChain;

	*pui32SwapChainID = psDevInfo->uiSwapChainID;

	*phSwapChain = (IMG_HANDLE)psSwapChain;

	eError = PVRSRV_OK;
	goto ExitUnLock;

ErrorDestroySwapQueue:
	SHLFBDestroySwapQueue(psSwapChain);
ErrorFreeBuffers:
	SHLFBFreeKernelMem(psBuffer);
ErrorFreeSwapChain:
	SHLFBFreeKernelMem(psSwapChain);
ExitUnLock:
	SHLFBCreateSwapChainUnLock(psDevInfo);
	return eError;
}

/*
 * DestroyDCSwapChain
 * Called from services.
 */
static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
	IMG_HANDLE hSwapChain)
{
	SHLFB_DEVINFO	*psDevInfo;
	SHLFB_SWAPCHAIN *psSwapChain;
	SHLFB_ERROR eError;

	/* Check parameters */
	if(!hDevice || !hSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;
	psSwapChain = (SHLFB_SWAPCHAIN*)hSwapChain;

	SHLFBCreateSwapChainLock(psDevInfo);

	if (SwapChainHasChanged(psDevInfo, psSwapChain))
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Swap chain mismatch\n", __FUNCTION__, psDevInfo->uiFBDevID);

		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto ExitUnLock;
	}

	/* The swap queue is flushed before being destroyed */
	SHLFBDestroySwapQueue(psSwapChain);

	eError = SHLFBDisableLFBEventNotification(psDevInfo);
	if (eError != SHLFB_OK)
	{
		printk(KERN_WARNING DRIVER_PREFIX ": %s: Device %u: Couldn't disable framebuffer event notification\n", __FUNCTION__, psDevInfo->uiFBDevID);
	}

	/* Free resources */
	SHLFBFreeKernelMem(psSwapChain->psBuffer);
	SHLFBFreeKernelMem(psSwapChain);

	psDevInfo->psSwapChain = NULL;

	SHLFBFlip(psDevInfo, &psDevInfo->sSystemBuffer);

	eError = PVRSRV_OK;

ExitUnLock:
	SHLFBCreateSwapChainUnLock(psDevInfo);

	return eError;
}

/*
 * SetDCDstRect
 * Called from services.
 */
static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE hDevice,
	IMG_HANDLE hSwapChain,
	IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	/* Only full display swapchains on this device */

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

/*
 * SetDCSrcRect
 * Called from services.
 */
static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	/* Only full display swapchains on this device */

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

/*
 * SetDCDstColourKey
 * Called from services.
 */
static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	/* Don't support DST CK on this device */

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

/*
 * SetDCSrcColourKey
 * Called from services.
 */
static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	/* Don't support SRC CK on this device */

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

/*
 * GetDCBuffers
 * Called from services.
 */
static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_UINT32 *pui32BufferCount,
                                 IMG_HANDLE *phBuffer)
{
	SHLFB_DEVINFO   *psDevInfo;
	SHLFB_SWAPCHAIN *psSwapChain;
	PVRSRV_ERROR eError;
	unsigned i;

	/* Check parameters */
	if(!hDevice
	|| !hSwapChain
	|| !pui32BufferCount
	|| !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (SHLFB_DEVINFO*)hDevice;
	psSwapChain = (SHLFB_SWAPCHAIN*)hSwapChain;

	SHLFBCreateSwapChainLock(psDevInfo);

	if (SwapChainHasChanged(psDevInfo, psSwapChain))
	{
		printk(KERN_WARNING DRIVER_PREFIX
			": %s: Device %u: Swap chain mismatch\n", __FUNCTION__, psDevInfo->uiFBDevID);

		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	/* Return the buffer count */
	*pui32BufferCount = (IMG_UINT32)psSwapChain->ulBufferCount;

	/* Return the buffers */
	for(i=0; i<psSwapChain->ulBufferCount; i++)
	{
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->psBuffer[i];
	}

	eError = PVRSRV_OK;

Exit:
	SHLFBCreateSwapChainUnLock(psDevInfo);

	return eError;
}

/*
 * SwapToDCBuffer
 * Called from services.
 */
static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE hDevice,
                                   IMG_HANDLE hBuffer,
                                   IMG_UINT32 ui32SwapInterval,
                                   IMG_HANDLE hPrivateTag,
                                   IMG_UINT32 ui32ClipRectCount,
                                   IMG_RECT *psClipRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hBuffer);
	UNREFERENCED_PARAMETER(ui32SwapInterval);
	UNREFERENCED_PARAMETER(hPrivateTag);
	UNREFERENCED_PARAMETER(ui32ClipRectCount);
	UNREFERENCED_PARAMETER(psClipRect);

	/* * Nothing to do since Services common code does the work */

	return PVRSRV_OK;
}

/*
 * Called after the screen has unblanked, or after any other occasion
 * when we didn't wait for vsync, but now need to. Not doing this after
 * unblank leads to screen jitter on some screens.
 * Returns true if the screen has been deemed to have settled.
 */
static SHLFB_BOOL WaitForVSyncSettle(SHLFB_DEVINFO *psDevInfo)
{
		unsigned i;
		for(i = 0; i < SHLFB_VSYNC_SETTLE_COUNT; i++)
		{
			if (DontWaitForVSync(psDevInfo) || !SHLFBWaitForVSync(psDevInfo))
			{
				return SHLFB_FALSE;
			}
		}

		return SHLFB_TRUE;
}

/*
 * Swap handler.
 * Called from the swap chain work queue handler.
 * There is no need to take the swap chain creation lock in here, or use
 * some other method of stopping the swap chain from being destroyed.
 * This is because the swap chain creation lock is taken when queueing work,
 * and the work queue is flushed before the swap chain is destroyed.
 */
void SHLFBSwapHandler(SHLFB_BUFFER *psBuffer)
{
	SHLFB_DEVINFO *psDevInfo = psBuffer->psDevInfo;
	SHLFB_SWAPCHAIN *psSwapChain = psDevInfo->psSwapChain;
	SHLFB_BOOL bPreviouslyNotVSynced;

#if defined(SUPPORT_DRI_DRM)
	if (!SHLFBAtomicBoolRead(&psDevInfo->sLeaveVT))
#endif
	{
		SHLFBFlip(psDevInfo, psBuffer);
	}
	bPreviouslyNotVSynced = psSwapChain->bNotVSynced;
	psSwapChain->bNotVSynced = SHLFB_TRUE;


	if (!DontWaitForVSync(psDevInfo))
	{
		int iBlankEvents = SHLFBAtomicIntRead(&psDevInfo->sBlankEvents);

		psSwapChain->bNotVSynced = SHLFB_FALSE;

		if (bPreviouslyNotVSynced || psSwapChain->iBlankEvents != iBlankEvents)
		{
			psSwapChain->iBlankEvents = iBlankEvents;
			psSwapChain->bNotVSynced = !WaitForVSyncSettle(psDevInfo);
		} else if (psBuffer->ulSwapInterval != 0)
		{
			psSwapChain->bNotVSynced = !SHLFBWaitForVSync(psDevInfo);
		}
	}

	psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete((IMG_HANDLE)psBuffer->hCmdComplete, IMG_TRUE);
}

/* Triggered by PVRSRVSwapToDCBuffer */
static IMG_BOOL ProcessFlipV1(IMG_HANDLE hCmdCookie,
							  SHLFB_DEVINFO *psDevInfo,
							  SHLFB_SWAPCHAIN *psSwapChain,
							  SHLFB_BUFFER *psBuffer,
							  unsigned long ulSwapInterval)
{
	printk("ProcessFlipV1\n");

	SHLFBCreateSwapChainLock(psDevInfo);

	/* The swap chain has been destroyed */
	if (SwapChainHasChanged(psDevInfo, psSwapChain))
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": %s: Device %u (PVR Device ID %u): The swap chain has been destroyed\n",
			__FUNCTION__, psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID));
	}
	else
	{
		psBuffer->hCmdComplete = (SHLFB_HANDLE)hCmdCookie;
		psBuffer->ulSwapInterval = ulSwapInterval;
		{
#if defined(USELINUX_FRAME_BUFFER)
			SHLFBQueueBufferForSwap(psSwapChain, psBuffer);
#else
			SHLFBSwapHandler(psBuffer);
#endif
		}
	}

	SHLFBCreateSwapChainUnLock(psDevInfo);

	return IMG_TRUE;
}

struct gpu_layer_list {
	int num;
	unsigned long rtAddr[];
};

static IMG_BOOL ProcessFlipV2(IMG_HANDLE hCmdCookie,
							  SHLFB_DEVINFO *psDevInfo,
							  PDC_MEM_INFO *ppsMemInfos,
							  IMG_UINT32 ui32NumMemInfos,
							  struct cmp_request_queuedata *psCmpData,
							  IMG_UINT32 ui32CmpDataLength)
{
	IMG_UINT32 i;
	int num_layer = 0;

	struct gpu_layer_list *data = (struct gpu_layer_list*)psCmpData;

	if (data->num > ui32NumMemInfos) {
		WARN(1, "invalid num of infos");
		return IMG_FALSE;
	}
	num_layer = data->num;

	for (i = 0; i < num_layer; i++) {
		IMG_CPU_PHYADDR phyAddr, rtAddr;

		/* try to get the RT logical address */
		psDevInfo->sPVRJTable.pfnPVRSRVDCMemInfoGetCpuRTAddr(
			ppsMemInfos[i], 0, &rtAddr);
		if (rtAddr.uiAddr == 0) {
			/* YUV format: convert the physical address to RT address */
			psDevInfo->sPVRJTable.pfnPVRSRVDCMemInfoGetCpuPAddr(ppsMemInfos[i], 0, &phyAddr);
			rtAddr.uiAddr = (IMG_UINT32)sh_mobile_composer_phy_change_rtaddr(phyAddr.uiAddr);
		}

		data->rtAddr[i] = rtAddr.uiAddr;
	}

	sh_mobile_composer_queue(psCmpData, ui32CmpDataLength,
						  (void *)psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete,
						  (void *)hCmdCookie);

	return IMG_TRUE;
}

/* Command processing flip handler function.  Called from services. */
static IMG_BOOL ProcessFlip(IMG_HANDLE  hCmdCookie,
                            IMG_UINT32  ui32DataSize,
                            IMG_VOID   *pvData)
{
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	SHLFB_DEVINFO *psDevInfo;

	/* Check parameters  */
	if(!hCmdCookie || !pvData)
	{
		return IMG_FALSE;
	}

	/* Validate data packet  */
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;

	psDevInfo = (SHLFB_DEVINFO*)psFlipCmd->hExtDevice;

	if(psFlipCmd->hExtBuffer)
	{
		return ProcessFlipV1(hCmdCookie,
							 psDevInfo,
							 psFlipCmd->hExtSwapChain,
							 psFlipCmd->hExtBuffer,
							 psFlipCmd->ui32SwapInterval);
	}
	else
	{
#if defined(CONFIG_MISC_R_MOBILE_COMPOSER) && defined(CONFIG_MISC_R_MOBILE_COMPOSER_REQUEST_QUEUE)
		DISPLAYCLASS_FLIP_COMMAND2 *psFlipCmd2;
		psFlipCmd2 = (DISPLAYCLASS_FLIP_COMMAND2 *)pvData;
		return ProcessFlipV2(hCmdCookie,
							 psDevInfo,
							 psFlipCmd2->ppsMemInfos,
							 psFlipCmd2->ui32NumMemInfos,
							 psFlipCmd2->pvPrivData,
							 psFlipCmd2->ui32PrivDataLength);
#else
		BUG();
#endif
	}
}

/*!
******************************************************************************

 @Function	SHLFBInitFBDev

 @Description specifies devices in the systems memory map

 @Input    psSysData - sys data

 @Return   SHLFB_ERROR  :

******************************************************************************/
static SHLFB_ERROR SHLFBInitFBDev(SHLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo;
	struct module *psLINFBOwner;
	SHLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
	SHLFB_ERROR eError = SHLFB_ERROR_GENERIC;
	unsigned long FBSize;
	unsigned long ulLCM = 0;
	unsigned uiFBDevID = psDevInfo->uiFBDevID;
	IMG_BOOL bIsRGB888;

	SHLFB_CONSOLE_LOCK();

	psLINFBInfo = registered_fb[uiFBDevID];
	if (psLINFBInfo == NULL)
	{
		eError = SHLFB_ERROR_INVALID_DEVICE;
		goto ErrorRelSem;
	}

	FBSize = (psLINFBInfo->screen_size) != 0 ?
					psLINFBInfo->screen_size :
					psLINFBInfo->fix.smem_len;

	/*
	 * Try and filter out invalid FB info structures.
	 */
	if (FBSize == 0 || psLINFBInfo->fix.line_length == 0)
	{
		eError = SHLFB_ERROR_INVALID_DEVICE;
		goto ErrorRelSem;
	}

	psLINFBOwner = psLINFBInfo->fbops->owner;
	if (!try_module_get(psLINFBOwner))
	{
		printk(KERN_INFO DRIVER_PREFIX
			": %s: Device %u: Couldn't get framebuffer module\n", __FUNCTION__, uiFBDevID);

		goto ErrorRelSem;
	}

	if (psLINFBInfo->fbops->fb_open != NULL)
	{
		int res;

		res = psLINFBInfo->fbops->fb_open(psLINFBInfo, 0);
		if (res != 0)
		{
			printk(KERN_INFO DRIVER_PREFIX
				" %s: Device %u: Couldn't open framebuffer(%d)\n", __FUNCTION__, uiFBDevID, res);

			goto ErrorModPut;
		}
	}

	psDevInfo->psLINFBInfo = psLINFBInfo;

	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer physical address: 0x%lx\n",
			psDevInfo->uiFBDevID, psLINFBInfo->fix.smem_start));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer virtual address: 0x%lx\n",
			psDevInfo->uiFBDevID, (unsigned long)psLINFBInfo->screen_base));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer size: %lu\n",
			psDevInfo->uiFBDevID, FBSize));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer virtual width: %u\n",
			psDevInfo->uiFBDevID, psLINFBInfo->var.xres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer virtual height: %u\n",
			psDevInfo->uiFBDevID, psLINFBInfo->var.yres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer width: %u\n",
			psDevInfo->uiFBDevID, psLINFBInfo->var.xres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer height: %u\n",
			psDevInfo->uiFBDevID, psLINFBInfo->var.yres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer stride: %u\n",
			psDevInfo->uiFBDevID, psLINFBInfo->fix.line_length));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: LCM of stride and page size: %lu\n",
			psDevInfo->uiFBDevID, ulLCM));

	psPVRFBInfo->ulWidth = psLINFBInfo->var.xres;
	psPVRFBInfo->ulHeight = psLINFBInfo->var.yres;
	psPVRFBInfo->ulByteStride =  psLINFBInfo->fix.line_length;
	ulLCM = LCM(psPVRFBInfo->ulByteStride, SHLFB_PAGE_SIZE);

	psPVRFBInfo->ulBufferSize = psPVRFBInfo->ulHeight * psPVRFBInfo->ulByteStride;

	bIsRGB888 = 0;

	if(psLINFBInfo->var.bits_per_pixel == 16)
	{
		if((psLINFBInfo->var.red.length == 5) &&
			(psLINFBInfo->var.green.length == 6) &&
			(psLINFBInfo->var.blue.length == 5) &&
			(psLINFBInfo->var.red.offset == 11) &&
			(psLINFBInfo->var.green.offset == 5) &&
			(psLINFBInfo->var.blue.offset == 0) &&
			(psLINFBInfo->var.red.msb_right == 0))
		{
			psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
		}
		else
		{
			printk(KERN_INFO DRIVER_PREFIX ": %s: Device %u: Unknown FB format\n", __FUNCTION__, uiFBDevID);
		}
	}
	else if(psLINFBInfo->var.bits_per_pixel == 32)
	{
		if((psLINFBInfo->var.red.length == 8) &&
			(psLINFBInfo->var.green.length == 8) &&
			(psLINFBInfo->var.blue.length == 8) &&
			(psLINFBInfo->var.red.offset == 16) &&
			(psLINFBInfo->var.green.offset == 8) &&
			(psLINFBInfo->var.blue.offset == 0) &&
			(psLINFBInfo->var.red.msb_right == 0))
		{
			psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
		}
		else
		{
			printk(KERN_INFO DRIVER_PREFIX ": %s: Device %u: Unknown FB format\n", __FUNCTION__, uiFBDevID);
		}
	}
#if defined(SUPPORT_RGB888_FRAMEBUFFER)
	else if(psLINFBInfo->var.bits_per_pixel == 24)
	{
		if((psLINFBInfo->var.red.length == 8) &&
			(psLINFBInfo->var.green.length == 8) &&
			(psLINFBInfo->var.blue.length == 8) &&
			(psLINFBInfo->var.red.offset == 0) &&
			(psLINFBInfo->var.green.offset == 8) &&
			(psLINFBInfo->var.blue.offset == 16) &&
			(psLINFBInfo->var.red.msb_right == 0))
		{
			psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
			psPVRFBInfo->ulByteStride =  ALIGN(psLINFBInfo->var.xres, 32) * 4;
			psPVRFBInfo->ulBufferSize = psPVRFBInfo->ulHeight * psPVRFBInfo->ulByteStride;
			ulLCM = LCM(psPVRFBInfo->ulByteStride, SHLFB_PAGE_SIZE);
			FBSize = RoundUpToMultiple(psPVRFBInfo->ulBufferSize, ulLCM) * 2;
			bIsRGB888 = 1;
		}
		else
		{
			printk(KERN_INFO DRIVER_PREFIX ": %s: Device %u: Unknown FB format\n", __FUNCTION__, uiFBDevID);
		}
	}
#endif
	else
	{
		printk(KERN_INFO DRIVER_PREFIX ": %s: Device %u: Unknown FB format\n", __FUNCTION__, uiFBDevID);
	}

	psDevInfo->sFBInfo.ulPhysicalWidthmm =
		((int)psLINFBInfo->var.width  > 0) ? psLINFBInfo->var.width  : 90;

	psDevInfo->sFBInfo.ulPhysicalHeightmm =
		((int)psLINFBInfo->var.height > 0) ? psLINFBInfo->var.height : 54;

#if defined(USE_COMPOSITION_BUFFER)
	{
		struct ion_handle *psIONHandle;
		ion_phys_addr_t PhyAddr;
		size_t len;

		/* Allocate ION memory */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
		psIONHandle = ion_alloc(gpsIONClient, FBSize, PAGE_ALIGN(FBSize), 1 << ION_HEAP_GPU_ID, 0);
#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)) */
		psIONHandle = ion_alloc(gpsIONClient, FBSize, PAGE_ALIGN(FBSize), 1 << ION_HEAP_GPU_ID);
#endif /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)) */
		if (IS_ERR_OR_NULL(psIONHandle)){
			printk(KERN_ERR DRIVER_PREFIX
				"%s: Failed to allocate via ion_alloc", __func__);
			goto ErrorModPut;
		}

		/* Get physical address for allocated ION memory */
		if (ion_phys(gpsIONClient, psIONHandle, &PhyAddr, &len) < 0){
			printk(KERN_ERR DRIVER_PREFIX
				"%s: Failed to get ion physical address via ion_phys", __func__);
			goto ErrorModPut;
		}

		/* register GPU buffer to composer driver */
		sh_mobile_composer_register_gpu_buffer(PhyAddr, len);

		/* System Surface */
		psPVRFBInfo->psIONHandle = psIONHandle;
		psPVRFBInfo->sSysAddr.uiAddr = PhyAddr;
		psPVRFBInfo->sCPUVAddr = 0;

		psPVRFBInfo->ulFBSize = len;
	}
#else
	/* System Surface */
	psPVRFBInfo->sSysAddr.uiAddr = psLINFBInfo->fix.smem_start;
	psPVRFBInfo->sCPUVAddr = psLINFBInfo->screen_base;

	psPVRFBInfo->ulFBSize = FBSize;
#endif

	/* Round the buffer size up to a multiple of the number of pages
	 * and the byte stride.
	 * This is used internally, to ensure buffers start on page
	 * boundaries, for the benefit of PVR Services.
	 */
	if ((psLINFBInfo->var.reserved[0] == 0) || (bIsRGB888 == 1))
	{
		psPVRFBInfo->ulRoundedBufferSize = RoundUpToMultiple(psPVRFBInfo->ulBufferSize, ulLCM);
	}
	else
	{
		psPVRFBInfo->ulRoundedBufferSize = psLINFBInfo->var.reserved[0];
		if (psPVRFBInfo->ulRoundedBufferSize < psPVRFBInfo->ulBufferSize)
		{
			printk(KERN_ERR DRIVER_PREFIX
				"%s: ulRoundedBufferSize (%lu) is smaller than BufferSize (%lu)",
				__func__, psPVRFBInfo->ulRoundedBufferSize, psPVRFBInfo->ulBufferSize);

			goto ErrorModPut;
		}
	}

	/* System Surface */
	psDevInfo->sFBInfo.sSysAddr.uiAddr = psPVRFBInfo->sSysAddr.uiAddr;
	psDevInfo->sFBInfo.sCPUVAddr = psPVRFBInfo->sCPUVAddr;

	eError = SHLFB_OK;
	goto ErrorRelSem;

ErrorModPut:
	module_put(psLINFBOwner);
ErrorRelSem:
	SHLFB_CONSOLE_UNLOCK();

	return eError;
}

static void SHLFBDeInitFBDev(SHLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo = psDevInfo->psLINFBInfo;
	struct module *psLINFBOwner;

	SHLFB_CONSOLE_LOCK();

#if defined(USE_COMPOSITION_BUFFER)
	{
		SHLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
		if (psPVRFBInfo->psIONHandle)
		{
			ion_free(gpsIONClient, psPVRFBInfo->psIONHandle);
		}
	}
#endif

	psLINFBOwner = psLINFBInfo->fbops->owner;

	if (psLINFBInfo->fbops->fb_release != NULL)
	{
		(void) psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);
	}

	module_put(psLINFBOwner);

	SHLFB_CONSOLE_UNLOCK();
}

static SHLFB_DEVINFO *SHLFBInitDev(unsigned uiFBDevID)
{
	PFN_CMD_PROC	 	pfnCmdProcList[SHLFB_COMMAND_COUNT];
	IMG_UINT32		aui32SyncCountList[SHLFB_COMMAND_COUNT][2];
	SHLFB_DEVINFO		*psDevInfo = NULL;

	/* Allocate device info. structure */
	psDevInfo = (SHLFB_DEVINFO *)SHLFBAllocKernelMem(sizeof(SHLFB_DEVINFO));

	if(psDevInfo == NULL)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: Couldn't allocate device information structure\n", __FUNCTION__, uiFBDevID);

		goto ErrorExit;
	}

	/* Any fields not set will be zero */
	memset(psDevInfo, 0, sizeof(SHLFB_DEVINFO));

	psDevInfo->uiFBDevID = uiFBDevID;

	/* Get the kernel services function table */
	if(!(*gpfnGetPVRJTable)(&psDevInfo->sPVRJTable))
	{
		goto ErrorFreeDevInfo;
	}

	/* Save private fbdev information structure in the dev. info. */
	if(SHLFBInitFBDev(psDevInfo) != SHLFB_OK)
	{
		/*
		 * Leave it to SHLFBInitFBDev to print an error message, if
		 * required.  The function may have failed because
		 * there is no Linux framebuffer device corresponding
		 * to the device ID.
		 */
		goto ErrorFreeDevInfo;
	}

	psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = (IMG_UINT32)(psDevInfo->sFBInfo.ulFBSize / psDevInfo->sFBInfo.ulRoundedBufferSize);
	if (psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers != 0)
	{
		psDevInfo->sDisplayInfo.ui32MaxSwapChains = 1;
		psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 1;
	}

	psDevInfo->sDisplayInfo.ui32PhysicalWidthmm = psDevInfo->sFBInfo.ulPhysicalWidthmm;
	psDevInfo->sDisplayInfo.ui32PhysicalHeightmm = psDevInfo->sFBInfo.ulPhysicalHeightmm;

	strncpy(psDevInfo->sDisplayInfo.szDisplayName, DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);

	psDevInfo->sDisplayFormat.pixelformat = psDevInfo->sFBInfo.ePixelFormat;
	psDevInfo->sDisplayDim.ui32Width      = (IMG_UINT32)psDevInfo->sFBInfo.ulWidth;
	psDevInfo->sDisplayDim.ui32Height     = (IMG_UINT32)psDevInfo->sFBInfo.ulHeight;
	psDevInfo->sDisplayDim.ui32ByteStride = (IMG_UINT32)psDevInfo->sFBInfo.ulByteStride;

	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		": Device %u: Maximum number of swap chain buffers: %u\n",
		psDevInfo->uiFBDevID, psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers));

	/* Setup system buffer */
	psDevInfo->sSystemBuffer.sSysAddr = psDevInfo->sFBInfo.sSysAddr;
	psDevInfo->sSystemBuffer.sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
	psDevInfo->sSystemBuffer.psDevInfo = psDevInfo;

	SHLFBInitBufferForSwap(&psDevInfo->sSystemBuffer);

	/*
		Setup the DC Jtable so SRVKM can call into this driver
	*/
	psDevInfo->sDCJTable.ui32TableSize = sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE);
	psDevInfo->sDCJTable.pfnOpenDCDevice = OpenDCDevice;
	psDevInfo->sDCJTable.pfnCloseDCDevice = CloseDCDevice;
	psDevInfo->sDCJTable.pfnEnumDCFormats = EnumDCFormats;
	psDevInfo->sDCJTable.pfnEnumDCDims = EnumDCDims;
	psDevInfo->sDCJTable.pfnGetDCSystemBuffer = GetDCSystemBuffer;
	psDevInfo->sDCJTable.pfnGetDCInfo = GetDCInfo;
	psDevInfo->sDCJTable.pfnGetBufferAddr = GetDCBufferAddr;
	psDevInfo->sDCJTable.pfnCreateDCSwapChain = CreateDCSwapChain;
	psDevInfo->sDCJTable.pfnDestroyDCSwapChain = DestroyDCSwapChain;
	psDevInfo->sDCJTable.pfnSetDCDstRect = SetDCDstRect;
	psDevInfo->sDCJTable.pfnSetDCSrcRect = SetDCSrcRect;
	psDevInfo->sDCJTable.pfnSetDCDstColourKey = SetDCDstColourKey;
	psDevInfo->sDCJTable.pfnSetDCSrcColourKey = SetDCSrcColourKey;
	psDevInfo->sDCJTable.pfnGetDCBuffers = GetDCBuffers;
	psDevInfo->sDCJTable.pfnSwapToDCBuffer = SwapToDCBuffer;
	psDevInfo->sDCJTable.pfnSetDCState = SetDCState;

	/* Register device with services and retrieve device index */
	if(psDevInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice(
		&psDevInfo->sDCJTable,
		&psDevInfo->uiPVRDevID) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: PVR Services device registration failed\n", __FUNCTION__, uiFBDevID);

		goto ErrorDeInitFBDev;
	}
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		": Device %u: PVR Device ID: %u\n",
		psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID));

	/* Setup private command processing function table ... */
	pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;

	/* ... and associated sync count(s) */
	aui32SyncCountList[DC_FLIP_COMMAND][0] = 0; /* writes */
	aui32SyncCountList[DC_FLIP_COMMAND][1] = 10; /* reads */

	/*
		Register private command processing functions with
		the Command Queue Manager and setup the general
		command complete function in the devinfo.
	*/
	if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterCmdProcList(psDevInfo->uiPVRDevID,
															&pfnCmdProcList[0],
															aui32SyncCountList,
															SHLFB_COMMAND_COUNT) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: Couldn't register command processing functions with PVR Services\n", __FUNCTION__, uiFBDevID);
		goto ErrorUnregisterDevice;
	}

	SHLFBCreateSwapChainLockInit(psDevInfo);

	SHLFBAtomicBoolInit(&psDevInfo->sBlanked, SHLFB_FALSE);
	SHLFBAtomicIntInit(&psDevInfo->sBlankEvents, 0);
	SHLFBAtomicBoolInit(&psDevInfo->sFlushCommands, SHLFB_FALSE);
#if defined(CONFIG_HAS_EARLYSUSPEND)
	SHLFBAtomicBoolInit(&psDevInfo->sEarlySuspendFlag, SHLFB_FALSE);
#endif
#if defined(SUPPORT_DRI_DRM)
	SHLFBAtomicBoolInit(&psDevInfo->sLeaveVT, SHLFB_FALSE);
#endif
	return psDevInfo;

ErrorUnregisterDevice:
	(void)psDevInfo->sPVRJTable.pfnPVRSRVRemoveDCDevice(psDevInfo->uiPVRDevID);
ErrorDeInitFBDev:
	SHLFBDeInitFBDev(psDevInfo);
ErrorFreeDevInfo:
	SHLFBFreeKernelMem(psDevInfo);
ErrorExit:
	return NULL;
}

SHLFB_ERROR SHLFBInit(void)
{
	unsigned uiMaxFBDevIDPlusOne = SHLFBMaxFBDevIDPlusOne();
	unsigned i;
	unsigned uiDevicesFound = 0;

	if(SHLFBGetLibFuncAddr ("PVRGetDisplayClassJTable", &gpfnGetPVRJTable) != SHLFB_OK)
	{
		return SHLFB_ERROR_INIT_FAILURE;
	}

	/*
	 * We search for frame buffer devices backwards, as the last device
	 * registered with PVR Services will be the first device enumerated
	 * by PVR Services.
	 */
	for(i = uiMaxFBDevIDPlusOne; i-- != 0;)
	{
		SHLFB_DEVINFO *psDevInfo = SHLFBInitDev(i);

		if (psDevInfo != NULL)
		{
			/* Set the top-level anchor */
			SHLFBSetDevInfoPtr(psDevInfo->uiFBDevID, psDevInfo);
			uiDevicesFound++;
		}
	}

	return (uiDevicesFound != 0) ? SHLFB_OK : SHLFB_ERROR_INIT_FAILURE;
}

/*
 *	SHLFBDeInitDev
 *	DeInitialises one device
 */
static SHLFB_BOOL SHLFBDeInitDev(SHLFB_DEVINFO *psDevInfo)
{
	PVRSRV_DC_DISP2SRV_KMJTABLE *psPVRJTable = &psDevInfo->sPVRJTable;

	SHLFBCreateSwapChainLockDeInit(psDevInfo);

	SHLFBAtomicBoolDeInit(&psDevInfo->sBlanked);
	SHLFBAtomicIntDeInit(&psDevInfo->sBlankEvents);
	SHLFBAtomicBoolDeInit(&psDevInfo->sFlushCommands);
#if defined(CONFIG_HAS_EARLYSUSPEND)
	SHLFBAtomicBoolDeInit(&psDevInfo->sEarlySuspendFlag);
#endif
#if defined(SUPPORT_DRI_DRM)
	SHLFBAtomicBoolDeInit(&psDevInfo->sLeaveVT);
#endif
	psPVRJTable = &psDevInfo->sPVRJTable;

	if (psPVRJTable->pfnPVRSRVRemoveCmdProcList (psDevInfo->uiPVRDevID, SHLFB_COMMAND_COUNT) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: PVR Device %u: Couldn't unregister command processing functions\n", __FUNCTION__, psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID);
		return SHLFB_FALSE;
	}

	/*
	 * Remove display class device from kernel services device
	 * register.
	 */
	if (psPVRJTable->pfnPVRSRVRemoveDCDevice(psDevInfo->uiPVRDevID) != PVRSRV_OK)
	{
		printk(KERN_ERR DRIVER_PREFIX
			": %s: Device %u: PVR Device %u: Couldn't remove device from PVR Services\n", __FUNCTION__, psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID);
		return SHLFB_FALSE;
	}

	SHLFBDeInitFBDev(psDevInfo);

	SHLFBSetDevInfoPtr(psDevInfo->uiFBDevID, NULL);

	/* De-allocate data structure */
	SHLFBFreeKernelMem(psDevInfo);

	return SHLFB_TRUE;
}

/*
 *	SHLFBDeInit
 *	Deinitialises the display class device component of the FBDev
 */
SHLFB_ERROR SHLFBDeInit(void)
{
	unsigned uiMaxFBDevIDPlusOne = SHLFBMaxFBDevIDPlusOne();
	unsigned i;
	SHLFB_BOOL bError = SHLFB_FALSE;

	for(i = 0; i < uiMaxFBDevIDPlusOne; i++)
	{
		SHLFB_DEVINFO *psDevInfo = SHLFBGetDevInfoPtr(i);

		if (psDevInfo != NULL)
		{
			bError |= !SHLFBDeInitDev(psDevInfo);
		}
	}

	return (bError) ? SHLFB_ERROR_INIT_FAILURE : SHLFB_OK;
}

/******************************************************************************
 End of file (shmobilelfb_displayclass.c)
******************************************************************************/
