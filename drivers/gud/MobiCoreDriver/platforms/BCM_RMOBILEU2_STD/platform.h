/*
 * Header file of MobiCore Driver Kernel Module Platform
 * specific structures
 *
 * Internal structures of the McDrvModule
 *
 * Header file the MobiCore Driver Kernel Module,
 * its internal structures and defines.
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 */

#ifndef _MC_DRV_PLATFORM_H_
#define _MC_DRV_PLATFORM_H_

/* MobiCore Interrupt. */
#define MC_INTR_SSIQ (152)

/* Enable mobicore mem traces */
#define MC_MEM_TRACES

/* Enable Runtime Power Management */
#ifdef CONFIG_PM_RUNTIME
 #define MC_PM_RUNTIME
#endif

#define TBASE_CORE_SWITCHER
#define COUNT_OF_CPUS 2
/* Values of MPIDR regs in  cpu0, cpu1*/
#define CPU_IDS {0x0000, 0x0001}

/* Enabel BL switcher notifier */
/*#define MC_BL_NOTIFIER*/

/*#define MC_VM_UNMAP*/

/* Enable Fastcall worker thread */
#define MC_FASTCALL_WORKER_THREAD

#endif /* _MC_DRV_PLATFORM_H_ */
