/*
 * include/memlog/memlog.h
 *
 * Copyright (C) 2013 Renesas Mobile Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef __MEMLOG_H__
#define __MEMLOG_H__

#ifdef CONFIG_MEMLOG
#ifndef __ASSEMBLY__
extern unsigned long memlog_capture;
extern void memory_log_irq(unsigned int irq, int in);
extern void memory_log_func(unsigned long func_id, int in);
extern void memory_log_dump_int(unsigned int dump_id, int dump_data);
extern void memory_log_timestamp(unsigned int id);
extern void __iomem *memory_log_get_pm_area_va(void);
extern phys_addr_t memory_log_get_pm_area_pa(void);
enum timestamps {
	CMT15_TIMESTAMP = 0,
	RWDT_TIMESTAMP,
	FIQ_TIMESTAMP,
	MAX_TIMESTAMP,
};

#endif /* __ASSEMBLY__ */
#else /* CONFIG_MEMLOG */
#ifndef __ASSEMBLY__
static inline void memory_log_irq(unsigned int irq, int in) {}
static inline void memory_log_func(unsigned long func_id, int in) {}
static inline void memory_log_dump_int(unsigned int dump_id, int dump_data) {}
static inline void memory_log_timestamp(unsigned int id) {}
static inline void __iomem *memory_log_get_pm_area_va(void) { return NULL; }
static inline phys_addr_t memory_log_get_pm_area_pa(void) {return 0; }
#endif /* __ASSEMBLY__ */
#endif

#define PM_ENTRY_SIZE		8 /* 32bit timer & 32 bit id */

#define IRQ_LOG_ENTRY_IN					0x01000000
#define FUNC_LOG_ENTRY_IN					0x00000001
#define PM_FUNC_ID_START_WFI					0x00000010
#define PM_FUNC_ID_START_WFI2					0x00000020
#define PM_FUNC_ID_START_CORESTANDBY				0x00000030
#define PM_FUNC_ID_START_CORESTANDBY2				0x00000040
#define PM_FUNC_ID_JUMP_SYSTEMSUSPEND				0x00000050
#define PM_FUNC_ID_HOTPLUG					0x00000060

#define PM_FUNC_ID_EARLY_SUSPEND				0x00001060
#define PM_FUNC_ID_LATE_RESUME					0x00001070

#define PM_FUNC_ID_DPM_PREPARE					0x00002010
#define PM_FUNC_ID_DPM_SUSPEND					0x00002020
#define PM_FUNC_ID_DPM_SUSPEND_NOIRQ				0x00002030
#define PM_FUNC_ID_DPM_RESUME_NOIRQ				0x00002040
#define PM_FUNC_ID_DPM_RESUME					0x00002050
#define PM_FUNC_ID_DPM_COMPLETE					0x00002060

#define PM_FUNC_ID_SHMOBILE_SUSPEND_BEGIN			0x00003000
#define PM_FUNC_ID_SHMOBILE_SUSPEND_END				0x00003010
#define PM_FUNC_ID_SHMOBILE_SUSPEND_ENTER			0x00003020
#define PM_FUNC_ID_SHMOBILE_SUSPEND_PREPARE			0x00003030
#define PM_FUNC_ID_SHMOBILE_SUSPEND_PREPARE_LATE		0x00003040
#define PM_FUNC_ID_SHMOBILE_SUSPEND_WAKE			0x00003050

#define PM_FUNC_ID_SEC_HAL_PM_COMA_ENTRY			0x00004000
#define PM_FUNC_ID_PUB2SEC_DISPATCHER				0x00004010
#define PM_FUNC_ID_HW_SEC_ROM_PUB_BRIDGE			0x00004020

#define PM_DUMP_ID_DFS_FREQ					0x000001
#define PM_DUMP_ID_DFS_MINMAX_FREQ				0x000002
#define PM_DUMP_ID_ZB3DFS_FREQ_REQ				0x000003
#define PM_DUMP_ID_ZB3DFS_FREQ					0x000004
#define PM_DUMP_ID_KICK_FREQ					0x000005
#define RWDT_DUMP_ID						0x000006
#define CMT15_DUMP_ID						0x000007
#define PM_DUMP_ID_DFS_MINMAX_PID				0x000008

#define PM_PM_ID_ARMVECTOR					0x000001
#define PM_PM_ID_SUSPEND_IN					0x000002
#define PM_PM_ID_SUSPEND_OUT					0x000003
#define PM_PM_ID_CORESTANDBY_IN					0x000004
#define PM_PM_ID_CORESTANDBY_OUT				0x000005
#define PM_PM_ID_CORESTANDBY2_IN				0x000006
#define PM_PM_ID_CORESTANDBY2_OUT				0x000007
#define PM_PM_ID_HOTPLUG_IN					0x000008
#define PM_PM_ID_HOTPLUG_OUT					0x000009

#define PM_DUMP_ID_SET_SBSC_FREQ_ZB3_LOCK			0x000010
#define PM_DUMP_ID_SET_SBSC_FREQ_ZB3_UNLOCK			0x000011
#define PM_DUMP_ID_SET_CPU_FREQ_RETRY				0x000020
#define PM_DUMP_ID_SET_CPU_FREQ_ZB3_LOCK			0x000021
#define PM_DUMP_ID_SET_CPU_FREQ_ZB3_UNLOCK_1			0x000022
#define PM_DUMP_ID_SET_CPU_FREQ_ZB3_UNLOCK_2			0x000023
#define PM_DUMP_ID_SET_CPU_FREQ_ZB3_UNLOCK_3			0x000024
#define PM_DUMP_ID_SET_CPU_FREQ_ZB3_UNLOCK_4			0x000025
#define PM_DUMP_ID_SET_CPU_FREQ_ZS_LOCK				0x000026
#define PM_DUMP_ID_SET_CPU_FREQ_ZS_UNLOCK_1			0x000027
#define PM_DUMP_ID_SET_CPU_FREQ_ZS_UNLOCK_2			0x000028

#define PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_1			0x000050
#define PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_2			0x000051
#define PM_DUMP_ID_SET_FRQCRB					0x000052

#ifdef CONFIG_PM_FORCE_SLEEP
#define PM_DUMP_ID_FSLEEP_SET					0x000060
#define PM_DUMP_ID_FSLEEP_PSTR					0x000061
#define PM_DUMP_ID_FSLEEP_PD_DISABLE				0x000062
#define PM_DUMP_ID_FSLEEP_DFS_DISABLE				0x000063
#define PM_DUMP_ID_FSLEEP_DPM_PREPARE				0x000064
#define PM_DUMP_ID_FSLEEP_DPM_SUSP				0x000065
#define PM_DUMP_ID_FSLEEP_DPM_SUSP_LATE				0x000066
#define PM_DUMP_ID_FSLEEP_DPM_SUSP_NOIRQ			0x000067
#define PM_DUMP_ID_FSLEEP_DPM_ASYNC_ERR				0x000068
#endif /* #ifdef CONFIG_PM_FORCE_SLEEP */

#define PM_DUMP_ID_SET_FRQCRB_1					0x000070
#define PM_DUMP_ID_SET_FRQCRB_2					0x000071
#define PM_DUMP_ID_SET_FRQCRA_1					0x000072
#define PM_DUMP_ID_SET_FRQCRA_2					0x000073
#define PM_DUMP_ID_SET_FRQCRA_3					0x000074

#define PM_DUMP_ID_RWDT_COUNTER					0x0000E0

#define PM_DUMP_ID_SET_SBSC_FREQ_ZB3_LOCK_ERR			0x0000F0
#define PM_DUMP_ID_SET_CPU_FREQ_ZB3_LOCK_ERR			0x0000F1
#define PM_DUMP_ID_SET_CPU_FREQ_ZS_LOCK_ERR			0x0000F2
#define DMA_FUNC_ID_LD_CLEANUP 					0x0000F3
#define DMA_FUNC_ID_PENDING					0x0000F4
#define DMA_FUNC_ID_DO_TASKLET 					0x0000F5
#define DMA_FUNC_LD_QUEUE					0x0000F6	
#define DMA_START 						1
#define DMA_END							0
#define PM_DUMP_START						1
#define PM_DUMP_END						0
#endif
