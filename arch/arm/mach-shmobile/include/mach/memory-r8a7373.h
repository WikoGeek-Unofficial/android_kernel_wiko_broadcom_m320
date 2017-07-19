/*
 * memory-r8a7373.h
 *
 * Copyright (C) 2014 Broadcom Corporation - all rights reserved.
 *
 */

#ifndef __MEMORY_R8A7373_H__
#define __MEMORY_R8A7373_H__

/* Please refer SDB4.4-Memory-Map.xlsx */

/* These are really r8a7373 specific and can't change between products*/
#define ICRAM0_START_ADDR				0xE63A0000
#define ICRAM0_END_ADDR					0xE63B1FFF

#define ICRAM1_START_ADDR				0xE63C0000
#define ICRAM1_END_ADDR					0xE63C0FFF

#define SECURE_RAM_START_ADDR				0xE6300000
#define SECURE_RAM_END_ADDR				0xE633FFFF

#define SDRAM_MEMLOG_START_ADDRESS			0xE63C0000
#define SDRAM_MEMLOG_END_ADDRESS			0xE63C0FEF

#define SDRAM_RAMDUMP_START_ADDR			0xE63C0FF0
#define SDRAM_RAMDUMP_END_ADDR				0xE63C0FFF

/* Sdram start address is always same 0x40000000 in r8a7373. Sdram
 * end might change per product so that cannot be hardcoded */
#define SDRAM_START_ADDR				0x40000000

#define SDRAM_KERNEL_START_ADDR				0x40000000

/*  MFI reservation: RT/ARM IPC area + RT log area.
 *  Address and size is hard-coded in RTbinary.
 *  Would need a different RTbinary if any change is done here. */
#define SDRAM_MFI_START_ADDR				0x45F00000
#define SDRAM_MFI_END_ADDR				0x45FFFFFF

/*  SH-Firmware reservation: RT code/data/heap etc
 *  Address and size is hard-coded in RTbinary.
 *  Would need a different RTbinary if any change is done here. */
#define SDRAM_SH_FIRM_START_ADDR			0x46000000
#define SDRAM_SH_FIRM_END_ADDR				0x466FFFFF

#define SDRAM_BOOTLOG_START_ADDR			0x46700000
#define SDRAM_BOOTLOG_END_ADDR				0x4670BAEF
#define SDRAM_BOOT_I2C_LOG_START_ADDR			0x4670BAF0
#define SDRAM_BOOT_I2C_LOG_END_ADDR			0x4670BAFF
#define SDRAM_REGISTER_DUMP_AREA_START_ADDR		0x4670BB00
#define SDRAM_REGISTER_DUMP_AREA_END_ADDR		0x46713EFF
#define SDRAM_NON_VOLATILE_FLAG_AREA_START_ADDR		0x46713F00
#define SDRAM_NON_VOLATILE_FLAG_AREA_END_ADDR		0x46713FFF

/*  Secure DRM area: Protected DRM bitstream buffers
 *  The region for this is limited as there is a cross-check in
 *  secure software.
 *  If CMA is used, size would get rounded up to multiple of 4MB. */
#define SDRAM_DRM_AREA_START_ADDR			0x4C000000
#define SDRAM_DRM_AREA_END_ADDR				0x4D1FFFFF

/*  Bootloader area: Area used by bootloader
 *  This region of memory could be overwritten by bootloader and
 *  so the data of this region in ramdump will not be reliable.
 *  Intentionally overwriting MODEM RO area, because that can be
 *  recovered from map/out-files. (Assumes no corruptions).     */
#define SDRAM_BOOTLOADER_START_ADDR_512MB		0x50000000
#define SDRAM_BOOTLOADER_END_ADDR_512MB			0x50FFFFFF

/*  Overloaded boot area - for 3rd party/customer boot SW
 *  This region is not used by RLoader in case we load the
 *  3rd party boot here. RLoader runs from MERAM and loads
 *  the 3rd party boot SW here.                                 */
#define SDRAM_3RDPARTY_BOOTLOADER_START_ADDR_512MB	0x50000000
#define SDRAM_3RDPARTY_BOOTLOADER_END_ADDR_512MB	0x50FFFFFF

/* small memmap 512MB */
#define SDRAM_MODEM_RO_START_ADDR_512MB			0x50000000
#define SDRAM_MODEM_RO_END_ADDR_512MB			0x50FFFFFF
/* 16 MB hole for APE to use in between */
#define SDRAM_MODEM_DATA_START_ADDR_512MB		0x52000000
#define SDRAM_MODEM_DATA_END_ADDR_512MB			0x53FBFFFF

#define SDRAM_DIAMOND_START_ADDR_512MB			0x53FC0000
#define SDRAM_DIAMOND_END_ADDR_512MB			0x53FFFFFF

#define SDRAM_STM_TRACE_BUFFER_START_ADDR_512MB		0x54000000
#define SDRAM_SMALL_STM_TRACE_BUFFER_END_ADDR_512MB	0x543FFFFF
#define SDRAM_STM_TRACE_BUFFER_END_ADDR_512MB		0x54FFFFFF
#define SDRAM_NON_SECURE_SPINLOCK_START_ADDR_512MB	0x55000000
#define SDRAM_NON_SECURE_SPINLOCK_END_ADDR_512MB	0x55000FFF
#define SDRAM_SMC_START_ADDR_512MB			0x55001000
#define SDRAM_SMC_END_ADDR_512MB			0x554FFFFF

#define SDRAM_SOFT_SEMAPHORE_TVRF_START_ADDR_512MB	0x558FFC00
#define SDRAM_SOFT_SEMAPHORE_TVRF_END_ADDR_512MB	0x558FFC7F
#define SDRAM_SOFT_SEMAPHORE_FREQ_START_ADDR_512MB	0x558FFC80
#define SDRAM_SOFT_SEMAPHORE_FREQ_END_ADDR_512MB	0x558FFDFF
#define SDRAM_SOFT_SEMAPHORE_E20_START_ADDR_512MB	0x558FFE00
#define SDRAM_SOFT_SEMAPHORE_E20_END_ADDR_512MB		0x558FFE7F

#define SDRAM_HW_REVISION_NUMBER_START_ADDR_512MB	0x558FFE80
#define SDRAM_HW_REVISION_NUMBER_END_ADDR_512MB		0x558FFE83
#define SDRAM_MDM_SLEEP_ENTRY_START_ADDR_512MB		0x558FFE84
#define SDRAM_MDM_SLEEP_ENTRY_END_ADDR_512MB		0x558FFFFF

#define SDRAM_SDTOC_START_ADDR_512MB			0x55900000
#define SDRAM_SDTOC_END_ADDR_512MB			0x559FDFFF
#define SDRAM_SECURE_SPINLOCK_AND_DATA_START_ADDR_512MB	0x559FE000
#define SDRAM_SECURE_SPINLOCK_AND_DATA_END_ADDR_512MB	0x559FFFFF

#define SDRAM_SECURE_OS_START_ADDR_512MB		0x55A00000
#define SDRAM_SECURE_OS_END_ADDR_512MB			0x55FFFFFF

#define SDRAM_VOCODER_START_ADDR_512MB			0x5F800000
#define SDRAM_VOCODER_END_ADDR_512MB			0x5FABFFFF

/*  Bootloader area: Area used by bootloader
 *  This region of memory could be overwritten by bootloader and
 *  so the data of this region in ramdump will not be reliable.
 *  Intentionally overwriting MODEM RO area, because that can be
 *  recovered from map/out-files. (Assumes no corruptions).     */
#define SDRAM_BOOTLOADER_START_ADDR			0x70000000
#define SDRAM_BOOTLOADER_END_ADDR			0x70FFFFFF

/*  Overloaded boot area - for 3rd party/customer boot SW
 *  This region is not used by RLoader in case we load the
 *  3rd party boot here. RLoader runs from MERAM and loads
 *  the 3rd party boot SW here.                                 */
#define SDRAM_3RDPARTY_BOOTLOADER_START_ADDR		0x70000000
#define SDRAM_3RDPARTY_BOOTLOADER_END_ADDR		0x70FFFFFF

/* normal memmap 1GB*/
#define SDRAM_MODEM_RO_START_ADDR			0x70000000
#define SDRAM_MODEM_RO_END_ADDR				0x70FFFFFF
/* 16 MB hole for APE to use in between */
#define SDRAM_MODEM_DATA_START_ADDR			0x72000000
#define SDRAM_MODEM_DATA_END_ADDR			0x73FBFFFF
#define SDRAM_DIAMOND_START_ADDR			0x73FC0000
#define SDRAM_DIAMOND_END_ADDR				0x73FFFFFF

#define SDRAM_STM_TRACE_BUFFER_START_ADDR		0x74000000
#define SDRAM_SMALL_STM_TRACE_BUFFER_END_ADDR		0x743FFFFF
#define SDRAM_STM_TRACE_BUFFER_END_ADDR			0x74FFFFFF
#define SDRAM_NON_SECURE_SPINLOCK_START_ADDR		0x75000000
#define SDRAM_NON_SECURE_SPINLOCK_END_ADDR		0x75000FFF
#define SDRAM_SMC_START_ADDR				0x75001000
#define SDRAM_SMC_END_ADDR				0x754FFFFF

#define SDRAM_SOFT_SEMAPHORE_TVRF_START_ADDR		0x758FFC00
#define SDRAM_SOFT_SEMAPHORE_TVRF_END_ADDR		0x758FFC7F
#define SDRAM_SOFT_SEMAPHORE_FREQ_START_ADDR		0x758FFC80
#define SDRAM_SOFT_SEMAPHORE_FREQ_END_ADDR		0x758FFDFF
#define SDRAM_SOFT_SEMAPHORE_E20_START_ADDR		0x758FFE00
#define SDRAM_SOFT_SEMAPHORE_E20_END_ADDR		0x758FFE7F

#define SDRAM_HW_REVISION_NUMBER_START_ADDR		0x758FFE80
#define SDRAM_HW_REVISION_NUMBER_END_ADDR		0x758FFE83
#define SDRAM_MDM_SLEEP_ENTRY_START_ADDR		0x758FFE84
#define SDRAM_MDM_SLEEP_ENTRY_END_ADDR			0x758FFFFF

#define SDRAM_SDTOC_START_ADDR				0x75900000
#define SDRAM_SDTOC_END_ADDR				0x759FDFFF
#define SDRAM_SECURE_SPINLOCK_AND_DATA_START_ADDR	0x759FE000
#define SDRAM_SECURE_SPINLOCK_AND_DATA_END_ADDR		0x759FFFFF

#define SDRAM_SECURE_OS_START_ADDR			0x75A00000
#define SDRAM_SECURE_OS_END_ADDR			0x75FFFFFF
#define SDRAM_SECURE_OS_SIZE				0x600000

#define SDRAM_VOCODER_START_ADDR			0x7F800000
#define SDRAM_VOCODER_END_ADDR				0x7FABFFFF
#define SDRAM_VOCODER_SIZE				0x2C0000


#endif /* __MEMORY_R8A7373_H__ */
