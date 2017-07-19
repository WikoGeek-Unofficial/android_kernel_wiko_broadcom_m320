/*************************************************************************/ /*!
@Title          Environmental Data header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Linux-specific part of system data.
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
#ifndef _ENV_DATA_
#define _ENV_DATA_

#include <linux/interrupt.h>
#include <linux/pci.h>

#if defined(PVR_LINUX_MISR_USING_WORKQUEUE) || defined(PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE)
#include <linux/workqueue.h>
#endif

/*
 *	Env data specific to linux - convenient place to put this
 */

/* Fairly arbitrary sizes - hopefully enough for all bridge calls */
#define PVRSRV_MAX_BRIDGE_IN_SIZE	0x1000
#define PVRSRV_MAX_BRIDGE_OUT_SIZE	0x1000

typedef	struct _PVR_PCI_DEV_TAG
{
	struct pci_dev		*psPCIDev;
	HOST_PCI_INIT_FLAGS	ePCIFlags;
	IMG_BOOL abPCIResourceInUse[DEVICE_COUNT_RESOURCE];
} PVR_PCI_DEV;

typedef struct _ENV_DATA_TAG
{
	IMG_VOID		*pvBridgeData;
	struct pm_dev		*psPowerDevice;
	IMG_BOOL		bLISRInstalled;
	IMG_BOOL		bMISRInstalled;
	IMG_UINT32		ui32IRQ;
	IMG_VOID		*pvISRCookie;
#if defined(PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE)
	struct workqueue_struct	*psWorkQueue;
#endif
#if defined(PVR_LINUX_MISR_USING_WORKQUEUE) || defined(PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE)
	struct work_struct	sMISRWork;
	IMG_VOID		*pvMISRData;
#else
	struct tasklet_struct	sMISRTasklet;
#endif
#if defined (SUPPORT_ION)
	IMG_HANDLE		hIonHeaps;
	IMG_HANDLE		hIonDev;
#endif
} ENV_DATA;

#endif /* _ENV_DATA_ */
/*****************************************************************************
 End of file (env_data.h)
*****************************************************************************/
