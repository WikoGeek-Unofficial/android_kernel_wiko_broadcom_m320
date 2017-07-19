/******************************************************************************/
/*                                                                            */
/* Copyright 2014 Broadcom Corporation                                        */
/*                                                                            */
/* Unless you and Broadcom execute a separate written software license        */
/* agreement governing use of this software, this software is licensed        */
/* to you under the terms of the GNU General Public License version 2         */
/* (the GPL), available at                                                    */
/*                                                                            */
/* http://www.broadcom.com/licenses/GPLv2.php                                 */
/*                                                                            */
/* with the following added to such license:                                  */
/*                                                                            */
/* As a special exception, the copyright holders of this software give        */
/* you permission to link this software with independent modules, and         */
/* to copy and distribute the resulting executable under terms of your        */
/* choice, provided that you also meet, for each linked independent           */
/* module, the terms and conditions of the license of that module.            */
/* An independent module is a module which is not derived from this           */
/* software. The special exception does not apply to any modifications        */
/* of the software.                                                           */
/*                                                                            */
/* Notwithstanding the above, under no circumstances may you combine          */
/* this software in any way with any other Broadcom software provided         */
/* under a license other than the GPL, without Broadcom's express             */
/* prior written consent.                                                     */
/*                                                                            */
/******************************************************************************/
#include <linux/of_address.h>
#include <linux/memblock.h>
#include <mach/memory-r8a7373.h>
#include <mach/memory.h>
#include <mach/ramdump.h>

/* this is used to mark the block that it needs to be removed */
#define MEMLAYOUT_FLAG_REMOVE		BIT(1)
/* this is used to mark the block that it to be split during ramdump loading */
#define MEMLAYOUT_FLAG_REGISTER_SPLIT	BIT(2)
/* this is used to mark the block that it's start address needs to be aligned
 * up to page boundary to allow ioremap for that address to work. This needs
 * to be used only when there is no other block defined above this one. */
#define MEMLAYOUT_FLAG_PAGE_ALIGN_UP	BIT(3)

static struct resource *rmobile_memory;
static unsigned int layout_count;

#define DEFINE_RES_MEM_NAMED_END(_start, _end, _name, _flags)		\
	{								\
		.start = (_start),					\
		.end = (_end),						\
		.name = (_name),					\
		.flags = (_flags),					\
	}

static struct resource mobile_memory[] = {
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MFI_START_ADDR,
			SDRAM_MFI_END_ADDR,
			"mfi",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SH_FIRM_START_ADDR,
			SDRAM_SH_FIRM_END_ADDR,
			"sh_firm",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_BOOTLOG_START_ADDR,
			SDRAM_BOOTLOG_END_ADDR,
			"bootlog",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_BOOT_I2C_LOG_START_ADDR,
			SDRAM_BOOT_I2C_LOG_END_ADDR,
			"i2c_bootlog",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_REGISTER_DUMP_AREA_START_ADDR,
			SDRAM_REGISTER_DUMP_AREA_END_ADDR,
			"ramdump",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_NON_VOLATILE_FLAG_AREA_START_ADDR,
			SDRAM_NON_VOLATILE_FLAG_AREA_END_ADDR,
			"non_volatile_flags",
			MEMLAYOUT_FLAG_REMOVE),
#if !defined(CONFIG_CMA) && defined(CONFIG_MOBICORE_API)
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_DRM_AREA_START_ADDR,
			SDRAM_DRM_AREA_END_ADDR,
			"drm",
			MEMLAYOUT_FLAG_REMOVE),
#endif
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MODEM_RO_START_ADDR,
			SDRAM_MODEM_RO_END_ADDR,
			"modem_ro",
			MEMLAYOUT_FLAG_REMOVE /* | MEMLAYOUT_FLAG_REGISTER_SPLIT */),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MODEM_DATA_START_ADDR,
			SDRAM_MODEM_DATA_END_ADDR,
			"modem_data",
			MEMLAYOUT_FLAG_REMOVE  /* | MEMLAYOUT_FLAG_REGISTER_SPLIT */),

	/* Exceptional case - due to modem crash dump analyzer (CDA) tool limitation */
	/* We have to give them one memdump containing both code & data              */

	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MODEM_RO_START_ADDR,
			SDRAM_MODEM_DATA_END_ADDR,
			"SDRAM_CP",
			MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_DIAMOND_START_ADDR,
			SDRAM_DIAMOND_END_ADDR,
			"diamond",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_STM_TRACE_BUFFER_START_ADDR,
			SDRAM_STM_TRACE_BUFFER_END_ADDR,
			"SDRAM_CP_MODEMTR",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_NON_SECURE_SPINLOCK_START_ADDR,
			SDRAM_NON_SECURE_SPINLOCK_END_ADDR,
			"non_secure_spinlock",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SMC_START_ADDR,
			SDRAM_SMC_END_ADDR,
			"SDRAM_CP_SHARED",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SOFT_SEMAPHORE_TVRF_START_ADDR,
			SDRAM_SOFT_SEMAPHORE_TVRF_END_ADDR,
			"soft_semaphore_tvrf",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_PAGE_ALIGN_UP),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SOFT_SEMAPHORE_FREQ_START_ADDR,
			SDRAM_SOFT_SEMAPHORE_FREQ_END_ADDR,
			"soft_semaphore_freq",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SOFT_SEMAPHORE_E20_START_ADDR,
			SDRAM_SOFT_SEMAPHORE_E20_END_ADDR,
			"soft_semaphore_e20",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_HW_REVISION_NUMBER_START_ADDR,
			SDRAM_HW_REVISION_NUMBER_END_ADDR,
			"hwrev",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MDM_SLEEP_ENTRY_START_ADDR,
			SDRAM_MDM_SLEEP_ENTRY_END_ADDR,
			"mdm_sleep",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SDTOC_START_ADDR,
			SDRAM_SDTOC_END_ADDR,
			"sdtoc",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SECURE_SPINLOCK_AND_DATA_START_ADDR,
			SDRAM_SECURE_SPINLOCK_AND_DATA_END_ADDR,
			"secure_spinlock",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SECURE_OS_START_ADDR,
			SDRAM_SECURE_OS_END_ADDR,
			"secureos",
			0),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_VOCODER_START_ADDR,
			SDRAM_VOCODER_END_ADDR,
			"vocoder",
			MEMLAYOUT_FLAG_REMOVE),

	/* This is not in sdram but it is here to get rid of memmap
	 * header usage. */
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MEMLOG_START_ADDRESS,
			SDRAM_MEMLOG_END_ADDRESS,
			"memlog",
			0),
};

static struct resource mobile_memory_512MB[] = {
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MFI_START_ADDR,
			SDRAM_MFI_END_ADDR,
			"mfi",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SH_FIRM_START_ADDR,
			SDRAM_SH_FIRM_END_ADDR,
			"sh_firm",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_BOOTLOG_START_ADDR,
			SDRAM_BOOTLOG_END_ADDR,
			"bootlog",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_BOOT_I2C_LOG_START_ADDR,
			SDRAM_BOOT_I2C_LOG_END_ADDR,
			"i2c_bootlog",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_REGISTER_DUMP_AREA_START_ADDR,
			SDRAM_REGISTER_DUMP_AREA_END_ADDR,
			"ramdump",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_NON_VOLATILE_FLAG_AREA_START_ADDR,
			SDRAM_NON_VOLATILE_FLAG_AREA_END_ADDR,
			"non_volatile_flags",
			MEMLAYOUT_FLAG_REMOVE),
#if !defined(CONFIG_CMA) && defined(CONFIG_MOBICORE_API)
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_DRM_AREA_START_ADDR,
			SDRAM_DRM_AREA_END_ADDR,
			"drm",
			MEMLAYOUT_FLAG_REMOVE),
#endif
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MODEM_RO_START_ADDR_512MB,
			SDRAM_MODEM_RO_END_ADDR_512MB,
			"modem_ro",
			MEMLAYOUT_FLAG_REMOVE /* | MEMLAYOUT_FLAG_REGISTER_SPLIT */),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MODEM_DATA_START_ADDR_512MB,
			SDRAM_MODEM_DATA_END_ADDR_512MB,
			"modem_data",
			MEMLAYOUT_FLAG_REMOVE  /* | MEMLAYOUT_FLAG_REGISTER_SPLIT */),

	/* Exceptional case - due to modem crash dump analyzer (CDA) tool limitation */
	/* We have to give them one memdump containing both code & data              */

	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MODEM_RO_START_ADDR_512MB,
			SDRAM_MODEM_DATA_END_ADDR_512MB,
			"SDRAM_CP",
			MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_DIAMOND_START_ADDR_512MB,
			SDRAM_DIAMOND_END_ADDR_512MB,
			"diamond",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_STM_TRACE_BUFFER_START_ADDR_512MB,
			SDRAM_STM_TRACE_BUFFER_END_ADDR_512MB,
			"SDRAM_CP_MODEMTR",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_NON_SECURE_SPINLOCK_START_ADDR_512MB,
			SDRAM_NON_SECURE_SPINLOCK_END_ADDR_512MB,
			"non_secure_spinlock",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SMC_START_ADDR_512MB,
			SDRAM_SMC_END_ADDR_512MB,
			"SDRAM_CP_SHARED",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_REGISTER_SPLIT),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SOFT_SEMAPHORE_TVRF_START_ADDR_512MB,
			SDRAM_SOFT_SEMAPHORE_TVRF_END_ADDR_512MB,
			"soft_semaphore_tvrf",
			MEMLAYOUT_FLAG_REMOVE | MEMLAYOUT_FLAG_PAGE_ALIGN_UP),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SOFT_SEMAPHORE_FREQ_START_ADDR_512MB,
			SDRAM_SOFT_SEMAPHORE_FREQ_END_ADDR_512MB,
			"soft_semaphore_freq",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SOFT_SEMAPHORE_E20_START_ADDR_512MB,
			SDRAM_SOFT_SEMAPHORE_E20_END_ADDR_512MB,
			"soft_semaphore_e20",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_HW_REVISION_NUMBER_START_ADDR_512MB,
			SDRAM_HW_REVISION_NUMBER_END_ADDR_512MB,
			"hwrev",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MDM_SLEEP_ENTRY_START_ADDR_512MB,
			SDRAM_MDM_SLEEP_ENTRY_END_ADDR_512MB,
			"mdm_sleep",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SDTOC_START_ADDR_512MB,
			SDRAM_SDTOC_END_ADDR_512MB,
			"sdtoc",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SECURE_SPINLOCK_AND_DATA_START_ADDR_512MB,
			SDRAM_SECURE_SPINLOCK_AND_DATA_END_ADDR_512MB,
			"secure_spinlock",
			MEMLAYOUT_FLAG_REMOVE),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_SECURE_OS_START_ADDR_512MB,
			SDRAM_SECURE_OS_END_ADDR_512MB,
			"secureos",
			0),
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_VOCODER_START_ADDR_512MB,
			SDRAM_VOCODER_END_ADDR_512MB,
			"vocoder",
			MEMLAYOUT_FLAG_REMOVE),

	/* This is not in sdram but it is here to get rid of memmap
	 * header usage. */
	DEFINE_RES_MEM_NAMED_END(
			SDRAM_MEMLOG_START_ADDRESS,
			SDRAM_MEMLOG_END_ADDRESS,
			"memlog",
			0),
};

/*
 * trace buffer size - handler for SDRAM trace buffer size
 *
 * No need to init sdram_trace_small variable, because
 * checkpatch will complain about it. It is initialized
 * to false by default (as it's static).
 *
 */
static bool sdram_trace_small;

static int __init tracebuffersize(char *arg)
{
	/* If we came here, smaller buffer is wanted.*/

	sdram_trace_small = true;
	return 0;
}
early_param("small_sdram_trace", tracebuffersize);

struct resource *get_mem_resource(char *name)
{
	int i;
	struct resource *rp;

	for (i = 0; i < layout_count; i++) {
		rp = &rmobile_memory[i];
		if (!strcmp(rp->name , name)) {
			pr_info("%s found %s 0x%08x-0x%08x\n", __func__, name,
					rp->start,
					rp->end);
			return &rmobile_memory[i];
		}
	}

	pr_alert("%s failed to get %s!!\n", __func__, name);
	return NULL;
}


void __init init_memory_layout(void)
{
	int i;
	struct resource *rp;
	resource_size_t start;

	if (memblock_phys_mem_size() > 512 * 1024 * 1024) {
		rmobile_memory = mobile_memory;
		layout_count = ARRAY_SIZE(mobile_memory);

		/* Update layout based on runtime info */
		if (sdram_trace_small)
			get_mem_resource("SDRAM_CP_MODEMTR")->end =
				SDRAM_SMALL_STM_TRACE_BUFFER_END_ADDR;
	} else {
		rmobile_memory = mobile_memory_512MB;
		layout_count = ARRAY_SIZE(mobile_memory_512MB);

		/* Update layout based on runtime info */
		if (sdram_trace_small)
			get_mem_resource("SDRAM_CP_MODEMTR")->end =
				SDRAM_SMALL_STM_TRACE_BUFFER_END_ADDR_512MB;
	}

	 /*
	 * Register whole sdram as split. We know that in R8A7373 physical ram
	 * always starts at SDRAM_START_ADDR (0x40000000) and the kernel knows
	 * all the ram at this point.
	 */
	register_ramdump_split("SDRAM", SDRAM_START_ADDR,
				memblock_end_of_DRAM() - 1);

	register_ramdump_split("icram0", ICRAM0_START_ADDR, ICRAM0_END_ADDR);
	register_ramdump_split("icram1", ICRAM1_START_ADDR, ICRAM1_END_ADDR);

	/* register splits to ramdump */
	for (i = 0; i < layout_count; i++) {
		rp = &rmobile_memory[i];
		if (rp->flags & MEMLAYOUT_FLAG_REGISTER_SPLIT)
			register_ramdump_split(rp->name, rp->start, rp->end);
	}

	/* carveout holes to memory */
	for (i = 0; i < layout_count; i++) {
		rp = &rmobile_memory[i];
		if (rp->flags & MEMLAYOUT_FLAG_REMOVE) {
			start = rp->start;
			if (rp->flags & MEMLAYOUT_FLAG_PAGE_ALIGN_UP)
				start &= PAGE_MASK;

			pr_info("removing %s 0x%x-0x%x\n", rp->name, start,
					rp->end);
			memblock_remove(start, rp->end - start + 1);
		}
	}
}
