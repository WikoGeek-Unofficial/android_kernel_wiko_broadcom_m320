/*
 * Copyright 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * A copy of the GPL is available at
 * http://www.broadcom.com/licenses/GPLv2.php, or by writing to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/freezer.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/input/mt.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>

#include <linux/regulator/consumer.h>


#include <linux/timer.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/i2c/bcmtch15xxx.h>
#include <mach/bcmtch15xxx_settings.h>

/* -------------------------------------- */
/* - BCM Touch Controller Driver Macros - */
/* -------------------------------------- */

/* -- driver version -- */
#define	BCMTCH_DRIVER_VERSION		"1.4.4.12:C-A.01"
#define	BCMTCH_DRIVER_BUILD_DATE	__DATE__
#define	BCMTCH_DRIVER_BUILD_TIME	__TIME__

/* -- SPM addresses -- */
#define BCMTCH_SPM_REG_REVISIONID   0x40
#define BCMTCH_SPM_REG_CHIPID0      0x41
#define BCMTCH_SPM_REG_CHIPID1      0x42
#define BCMTCH_SPM_REG_CHIPID2      0x43

#define BCMTCH_SPM_REG_SPI_I2C_SEL  0x44
#define BCMTCH_SPM_REG_I2CS_CHIPID  0x45

#define BCMTCH_SPM_REG_PSR          0x48

#define BCMTCH_SPM_REG_MSG_FROM_HOST    0x49
#define BCMTCH_SPM_REG_MSG_FROM_HOST_1  0x4a
#define BCMTCH_SPM_REG_MSG_FROM_HOST_2  0x4b
#define BCMTCH_SPM_REG_MSG_FROM_HOST_3  0x4c
#define BCMTCH_SPM_REG_RQST_FROM_HOST   0x4d
#define BCMTCH_SPM_REG_MSG_TO_HOST      0x4e

#define BCMTCH_SPM_REG_PMU_CONTROL2 0x52

#define BCMTCH_SPM_REG_SOFT_RESETS  0x59
#define BCMTCH_SPM_REG_FLL_STATUS   0x5c

#define BCMTCH_SPM_REG_ALFO_CTRL    0x60
#define BCMTCH_SPM_REG_LPLFO_CTRL   0x61

#define BCMTCH_SPM_REG_DMA_ADDR     0x80
#define BCMTCH_SPM_REG_DMA_STATUS   0x89
#define BCMTCH_SPM_REG_DMA_WFIFO    0x92
#define BCMTCH_SPM_REG_DMA_RFIFO    0xa2

/* -- SYS addresses -- */
#define BCMTCH_ADDR_BASE                    0x30000000

#define BCMTCH_ADDR_SPM_BASE                (BCMTCH_ADDR_BASE + 0x00100000)
#define BCMTCH_ADDR_SPM_PWR_CTRL            (BCMTCH_ADDR_SPM_BASE + 0x1c)
#define BCMTCH_ADDR_SPM_LPLFO_CTRL_RO       (BCMTCH_ADDR_SPM_BASE + 0xa0)
#define BCMTCH_ADDR_SPM_REMAP               (BCMTCH_ADDR_SPM_BASE + 0x100)
#define BCMTCH_ADDR_SPM_STICKY_BITS         (BCMTCH_ADDR_SPM_BASE + 0x144)

#define BCMTCH_ADDR_COMMON_BASE             (BCMTCH_ADDR_BASE + 0x00110000)
#define BCMTCH_ADDR_COMMON_ARM_REMAP        (BCMTCH_ADDR_COMMON_BASE + 0x00)
#define BCMTCH_ADDR_COMMON_SYS_HCLK_CTRL    (BCMTCH_ADDR_COMMON_BASE + 0x20)
#define BCMTCH_ADDR_COMMON_CLOCK_ENABLE     (BCMTCH_ADDR_COMMON_BASE + 0x48)
#define BCMTCH_ADDR_COMMON_OTP_CPU_CTRL0    (BCMTCH_ADDR_COMMON_BASE + 0x4c)
#define BCMTCH_ADDR_COMMON_OTP_CPU_WORD_RD  (BCMTCH_ADDR_COMMON_BASE + 0x54)
#define BCMTCH_ADDR_COMMON_OTP_CPU_BITSEL   (BCMTCH_ADDR_COMMON_BASE + 0x58)
#define BCMTCH_ADDR_COMMON_OTP_CPU_ADDRESS  (BCMTCH_ADDR_COMMON_BASE + 0x5c)
#define BCMTCH_ADDR_COMMON_OTP_CPU_STATUS   (BCMTCH_ADDR_COMMON_BASE + 0x64)
#define BCMTCH_ADDR_COMMON_OTP_CPU_MODE     (BCMTCH_ADDR_COMMON_BASE + 0x68)
#define BCMTCH_ADDR_COMMON_FLL_CTRL0        (BCMTCH_ADDR_COMMON_BASE + 0x104)
#define BCMTCH_ADDR_COMMON_FLL_LPF_CTRL2    (BCMTCH_ADDR_COMMON_BASE + 0x114)
#define BCMTCH_ADDR_COMMON_FLL_TEST_CTRL1   (BCMTCH_ADDR_COMMON_BASE + 0x144)

#define BCMTCH_ADDR_TCH_BASE                (BCMTCH_ADDR_BASE + 0x00300000)
#define BCMTCH_ADDR_TCH_VER                 (BCMTCH_ADDR_TCH_BASE + 0x00)

/* -- SYS MEM addresses -- */
#define BCMTCH_ADDR_VECTORS     0x00000000
#define BCMTCH_ADDR_CODE        0x10000000
#define BCMTCH_ADDR_DATA        0x10009000
#define BCMTCH_ADDR_TOC_BASE    0x0020c000

/* -- constants -- */
#define BCMTCH_MAX_TOUCH         10
#define BCMTCH_MAX_BUTTONS       16
#define BCMTCH_BTN_STATUS_MASK   0x0000ffff

#define BCMTCH_AXIS_MAX	4095

#define BCMTCH_AXIS_SHIFT_BITS	4

#define BCMTCH_MAX_PRESSURE		500
#define BCMTCH_MIN_ORIENTATION	-512
#define BCMTCH_MAX_ORIENTATION	511

#define BCMTCH_DMA_MODE_READ    1
#define BCMTCH_DMA_MODE_WRITE   3

#define BCMTCH_IF_I2C_SEL       0
#define BCMTCH_IF_SPI_SEL       1

#define BCMTCH_DEFAULT_I2C_ADDR_SYS	0x68

#define BCMTCH_IF_I2C_COMMON_CLOCK  0x387B
#define BCMTCH_IF_SPI_COMMON_CLOCK  0x387F

#define BCMTCH_COMMON_CLOCK_USE_FLL (0x1 << 18)

#define BCMTCH_POWER_STATE_SLEEP        0
#define BCMTCH_POWER_STATE_RETENTION    1
#define BCMTCH_POWER_STATE_IDLE         3
#define BCMTCH_POWER_STATE_ACTIVE       4

#define BCMTCH_POWER_MODE_SLEEP     0x01
#define BCMTCH_POWER_MODE_WAKE      0x02
#define BCMTCH_POWER_MODE_NOWAKE    0x00

#define BCMTCH_PMU_CNTL2_DLDO_1_1V  0x08

#define BCMTCH_POWER_ON_DELAY_US_MIN    5000

#define BCMTCH_RESET_MODE_SOFT_CLEAR    0x00
#define BCMTCH_RESET_MODE_SOFT_CHIP     0x01
#define BCMTCH_RESET_MODE_SOFT_ARM      0x02
#define BCMTCH_RESET_MODE_HARD          0x04

#define BCMTCH_MEM_REMAP_ADDR    BCMTCH_ADDR_SPM_REMAP

#define BCMTCH_MEM_RAM_BOOT 0x01

#define BCMTCH_FW_READY_WAIT	1000

#define BCMTCH_SPM_STICKY_BITS_PIN_RESET    0x02

#define BCMTCH_ABI_SIZE_MAX		512

/* development and test */
#define	BCMPFX	"BCMTCH:"

#define BCMTCH_DBG_PRINTF   1

#if BCMTCH_DBG_PRINTF

#ifdef dev_info
#undef dev_info
#endif
#define dev_info(dev, fmt, arg...)    printk(fmt, ##arg)

#ifdef dev_err
#undef dev_err
#endif
#define dev_err(dev, fmt, arg...)    printk(fmt, ##arg)

#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg(dev, fmt, flag, arg...)     \
		{if (bcmtch_debug_flag & flag) printk(fmt, flag, ##arg); }

#endif

/* -------------------------------------- */
/* - Touch Firmware Environment (ToFE)  - */
/* -------------------------------------- */

#define TOFE_MESSAGE_SUCCESS						0x12
#define TOFE_MESSAGE_SOC_REBOOT_PENDING				0x16
#define TOFE_MESSAGE_OTP_READING					0x18
#define TOFE_MESSAGE_COMMAND_ECHO					0x20
#define TOFE_MESSAGE_FW_READY						0x80
#define TOFE_MESSAGE_FW_READY_INTERRUPT				0x81
#define TOFE_MESSAGE_FW_READY_OVERRIDE				0x82
#define TOFE_MESSAGE_FW_READY_INTERRUPT_OVERRIDE	0x83

#define IS_VALID_TOFE_MESSAGE(x) ( \
	(((x) >= TOFE_MESSAGE_SUCCESS) && \
	((x) <= TOFE_MESSAGE_OTP_READING)) || \
	(((x) >= TOFE_MESSAGE_FW_READY) && \
	((x) <= TOFE_MESSAGE_FW_READY_INTERRUPT_OVERRIDE)) \
	)

enum tofe_command {
	TOFE_COMMAND_NO_COMMAND = 0,
	TOFE_COMMAND_INTERRUPT_ACK,
	TOFE_COMMAND_SCAN_START,
	TOFE_COMMAND_SCAN_STOP,
	TOFE_COMMAND_SCAN_SET_RATE,
	TOFE_COMMAND_SET_MODE,
	TOFE_COMMAND_CALIBRATE,
	TOFE_COMMAND_AFEREGREAD,
	TOFE_COMMAND_AFEREGWRITE,
	TOFE_COMMAND_RUN_SEM,
	TOFE_COMMAND_SET_LOG_MASK,
	TOFE_COMMAND_SET_LOG_MASK_FWID_ALL,
	TOFE_COMMAND_GET_LOG_MASK,
	TOFE_COMMAND_INIT_POST_BOOT_PATCHES,
	TOFE_COMMAND_SET_POWER_MODE,
	TOFE_COMMAND_POWER_MODE_SUSPEND,
	TOFE_COMMAND_POWER_MODE_RESUME,
	TOFE_COMMAND_HOST_OVERRIDE_REQ,
	TOFE_COMMAND_HOST_OVERRIDE_REL,
	TOFE_COMMAND_REBOOT_APPROVED,

	TOFE_COMMAND_LAST,
	TOFE_COMMAND_MAX = 0xff
};

enum bcmtch_status {
	BCMTCH_STATUS_SUCCESS	= 0,	/* Success */
	BCMTCH_STATUS_ERR_FAIL,			/* Generic failure */
	BCMTCH_STATUS_ERR_NOMEM,		/* Memory error */
	BCMTCH_STATUS_ERR_NOARG,		/* No proper arguments */
	BCMTCH_STATUS_ERR_BADARG,		/* Bad argument */
	BCMTCH_STATUS_ERR_TOUT,			/* Timeout */
	BCMTCH_STATUS_ERR_IO,			/* I/O error */
	BCMTCH_STATUS_ERR_NOCFG,		/* No configuration */
	BCMTCH_STATUS_ERR_NOCHN,		/* No required channel */
};

/**
	@ struct tofe_command_response
	@ brief Entry structure for command channel.
*/
struct tofe_command_response {
	uint8_t		flags;
	uint8_t		command;
	uint16_t	result;
	uint32_t	data;
};

#define	TOFE_COMMAND_FLAG_COMMAND_PROCESSED	(1 << 0)
#define	TOFE_COMMAND_FLAG_REQUEST_RESULT	(1 << 7)

/* ToFE Signature */

#define	TOFE_SIGNATURE_MAGIC_SIZE	8
#define TOFE_MAGIC {'B', 'C', 'M', 'N', 'A', 'P', 'A', '\0'}

struct tofe_version {
	uint8_t generation;
	uint8_t spin;
	uint8_t major;
	uint8_t minor;
};

enum tofe_chip_variant {
	TOFE_CHIP_VARIANT_INVALID,
	TOFE_CHIP_VARIANT_PHONE,
	TOFE_CHIP_VARIANT_TABLET,
};

/**
    @struct tofe_signature
    @brief Firmware ROM image signature structure.
*/
#pragma pack(push, 1)
struct  tofe_signature {
	char                 magic[TOFE_SIGNATURE_MAGIC_SIZE];
	struct tofe_version  version;
	uint64_t             commit;
	uint32_t             build;
	uint16_t             compatibility;
	uint8_t              variant; /* tofe_chip_variant */
	uint8_t              release_type;
	uint8_t              release_number;
	uint8_t              _pad;
	uint16_t             cust_release_num;
};
#pragma pack(pop)

#define TOFE_SIGNATURE_SIZE sizeof(struct tofe_signature)

/**
    @enum tofe_channel_flag
    @brief Channel flag field bit assignment.
*/
enum tofe_channel_flag {
	TOFE_CHANNEL_FLAG_STATUS_OVERFLOW      = 1 << 0,
	TOFE_CHANNEL_FLAG_STATUS_LEVEL_TRIGGER = 1 << 1,
	TOFE_CHANNEL_FLAG_FWDMA_BUFFER         = 1 << 3,
	TOFE_CHANNEL_FLAG_FWDMA_ENABLE         = 1 << 4,
	TOFE_CHANNEL_FLAG_INTERRUPT_ENABLE     = 1 << 5,
	TOFE_CHANNEL_FLAG_OVERFLOW_STALL       = 1 << 6,
	TOFE_CHANNEL_FLAG_INBOUND              = 1 << 7,
};

enum tofe_toc_index {
	TOFE_TOC_INDEX_CHANNEL = 2,
	TOFE_TOC_INDEX_TCH = 3,
	TOFE_TOC_INDEX_DETECT = 6,
};

enum tofe_channel_id {
	TOFE_CHANNEL_ID_TOUCH,
	TOFE_CHANNEL_ID_COMMAND,
	TOFE_CHANNEL_ID_RESPONSE,
};

struct tofe_dmac_header {
	uint16_t			min_size;
	uint16_t			size;
};

struct tofe_channel_buffer_header {
	struct tofe_dmac_header	dmac;
	uint8_t				channel_id:4;
	uint8_t				flags:4;
	uint8_t				seq_number;
	uint8_t				entry_size;
	uint8_t				entry_count;
};

struct tofe_channel_buffer {
	struct tofe_channel_buffer_header   header;
	uint32_t                            data[256];
};

struct tofe_channel_header {
	/* Channel ID */
	uint8_t		channel_id;
	/* Number of entries.  Limited to 255 entries. */
	uint8_t		entry_num;
	/* Entry size in bytes.  Limited to 255 bytes. */
	uint8_t		entry_size;
	/* Number of entries in channel to trigger notification */
	uint8_t		trig_level;
	/* Bit definitions shared with configuration. */
	uint8_t		flags;
	/* Number of datat buffers for this channel */
	uint8_t		buffer_num;
	/* Select the buffer to write [0 .. buffer_num-1]. */
	uint8_t		buffer_idx;
	/* Count the number of buffer swapped for debug. */
	uint8_t		seq_count;
	struct tofe_channel_buffer	*buffer[2];
};

struct tofe_channel_instance_cfg {
	uint8_t entry_num;	/* Must be > 0. */
	uint8_t entry_size;	/* Range [1..255]. */
	uint8_t trig_level;	/* 0 - entry_num */
	uint8_t flags;
	uint8_t buffer_num; /* Number of buffers for this channel */
	uint8_t _pad8;
	uint16_t offset;
	struct tofe_channel_header *channel_header;
	void *channel_data;
};

#pragma pack(push, 1)
struct mtc_detect_cfg {
	/* compressed structure definition */
	uint16_t    _pad[17];
	int16_t     scaling_x_offset;
	uint16_t    scaling_x_gain;
	uint16_t    scaling_x_range;
	int16_t     scaling_y_offset;
	uint16_t    scaling_y_gain;
	uint16_t    scaling_y_range;
	uint16_t    class_finger_gate;
	uint16_t    _pad0;
	uint16_t    class_stylus_gate;
};
#pragma pack(pop)

struct combi_entry {
	uint32_t offset;
	uint32_t addr;
	uint32_t length;
	uint32_t flags;
};

/* ------------------------------------- */
/* - BCM Touch Controller Driver Enums - */
/* ------------------------------------- */

enum bcmtch_channel_id {
	/* NOTE : see above tofe_channel_id */
	BCMTCH_CHANNEL_TOUCH,
	BCMTCH_CHANNEL_COMMAND,
	BCMTCH_CHANNEL_RESPONSE,

	/* last */
	BCMTCH_CHANNEL_MAX
};

/* event kinds from BCM Touch Controller */
enum bcmtch_event_kind {
	BCMTCH_EVENT_KIND_RESERVED, /* Avoiding zero, but you may use it. */

	BCMTCH_EVENT_KIND_FRAME,
	BCMTCH_EVENT_KIND_TOUCH,
	BCMTCH_EVENT_KIND_TOUCH_END,
	BCMTCH_EVENT_KIND_BUTTON,
	BCMTCH_EVENT_KIND_GESTURE,

	BCMTCH_EVENT_KIND_EXTENSION = 7,
	BCMTCH_EVENT_KIND_MAX = BCMTCH_EVENT_KIND_EXTENSION,
};

enum _bcmtch_touch_status {
	BCMTCH_TOUCH_STATUS_INACTIVE,
	BCMTCH_TOUCH_STATUS_UP,
	BCMTCH_TOUCH_STATUS_MOVE,
	BCMTCH_TOUCH_STATUS_MOVING,
};

/* -------------------------------------- */
/* - BCM Touch Controller Device Tables - */
/* -------------------------------------- */

static const uint32_t const bcmtch_chip_ids[] = {
	0x15200,
	0x15300,
	0x15400,
	0x15500,
};

struct otp_id_mem_cfg {
	uint32_t	chip_id;
	uint32_t	valid_addr;
	uint32_t	data_addr;
	uint8_t		table_size;
};

static const struct otp_id_mem_cfg const bcmtch_hw_id_otp_cfg[] = {
	{
		0x15200,
		0xDA,
		0xDB,
		5
	},
	{
		0x15300,
		0xDA,
		0xDB,
		5
	},
	{
		0x15400,
		0x166,
		0x167,
		8
	},
	{
		0x15500,
		0x166,
		0x167,
		8
	},
};

#define	BCMTCH_OTP_DATA_MASK        0xFFFFFF
#define	BCMTCH_OTP_VALID_MASK       0x0101

/* ------------------------------------------ */
/* - BCM Touch Controller Driver Parameters - */
/* ------------------------------------------ */
#define BCMTCH_BF_AXIS_X_REVERSE     0x00000001
#define BCMTCH_BF_AXIS_Y_REVERSE     0x00000002
#define BCMTCH_BF_AXIS_XY_SWAP       0x00000004
#define BCMTCH_BF_SUSPEND_COLD_BOOT  0x00000008
#define BCMTCH_BF_CHECK_INTERRUPT    0x00000010
#define BCMTCH_BF_DISABLE_POST_BOOT  0x00000020
#define BCMTCH_BF_FW_RESET_ON_WD     0x00000040
#define BCMTCH_BF_VERIFY_CHIP        0x00000080
#define BCMTCH_BF_READ_OTP           0x00000100
#define BCMTCH_BF_STATE_SHORT_SLOT   0x00000200

static int bcmtch_boot_flag =
	(BCMTCH_BF_SUSPEND_COLD_BOOT
	| BCMTCH_BF_AXIS_X_REVERSE
	| BCMTCH_BF_FW_RESET_ON_WD
	| BCMTCH_BF_READ_OTP
	| BCMTCH_BF_STATE_SHORT_SLOT
	);

module_param_named(boot_flag, bcmtch_boot_flag, int, S_IRUGO);
MODULE_PARM_DESC(boot_flag, "Boot bit-fields [RAM|RESET]");

/*-*/

#define BCMTCH_CHANNEL_FLAG_USE_TOUCH       0x00000001
#define BCMTCH_CHANNEL_FLAG_USE_CMD_RESP    0x00000002

static int bcmtch_channel_flag = BCMTCH_CHANNEL_FLAG_USE_TOUCH
			| BCMTCH_CHANNEL_FLAG_USE_CMD_RESP;

module_param_named(channel_flag, bcmtch_channel_flag, int, S_IRUGO);
MODULE_PARM_DESC(channel_flag, "Channels allowed bit-fields [L|C/R|T]");

/*-*/

#define BCMTCH_EVENT_FLAG_TOUCH_SIZE        0x00000001
#define BCMTCH_EVENT_FLAG_PRESSURE          0x00000002
#define BCMTCH_EVENT_FLAG_ORIENTATION       0x00000004

static int bcmtch_event_flag = BCMTCH_EVENT_FLAG_TOUCH_SIZE
		| BCMTCH_EVENT_FLAG_ORIENTATION;

module_param_named(event_flag, bcmtch_event_flag, int, S_IRUGO);
MODULE_PARM_DESC(event_flag, "Extension events bit-fields [ORIENTATION|PRESSURE|SIZE]");

/*- firmware -*/

#define BCMTCH_FIRMWARE_FLAGS_CODE                  0x0
#define BCMTCH_FIRMWARE_FLAGS_CONFIGS               0x01
#define BCMTCH_FIRMWARE_FLAGS_POST_BOOT             0x02
#define BCMTCH_FIRMWARE_FLAGS_POST_BOOT_CODE        0x02
#define BCMTCH_FIRMWARE_FLAGS_POST_BOOT_CONFIGS     0x03
#define BCMTCH_FIRMWARE_FLAGS_ROM_BOOT              0x04
#define BCMTCH_FIRMWARE_FLAGS_POST_BOOT_PATCH       0x08
#define BCMTCH_FIRMWARE_FLAGS_MASK                  0x0b

#define BCMTCH_FIRMWARE_FLAGS_COMBI                 0x10

#define BCMTCH_FIRMWARE_MAX_ENTRIES                 6

static int bcmtch_firmware_flag = BCMTCH_FIRMWARE_FLAGS_COMBI;

module_param_named(firmware_flag, bcmtch_firmware_flag, int, S_IRUGO);
MODULE_PARM_DESC(firmware_flag, "Firmware flag bit-fields (combi = 0x10  config = 0x01");

static char *bcmtch_firmware;

module_param_named(firmware, bcmtch_firmware, charp, S_IRUGO);
MODULE_PARM_DESC(firmware, "Filename of firmware to load");

static int bcmtch_firmware_addr = 0x0;

module_param_named(firmware_addr, bcmtch_firmware_addr, int, S_IRUGO);
MODULE_PARM_DESC(firmware_addr, "Address to load firmware");

/*- post boot -*/

#define	BCMTCH_POST_BOOT_RATE_HIGH	(1<<10)
#define	BCMTCH_POST_BOOT_RATE_LOW	80

static int bcmtch_post_boot_rate_high = BCMTCH_POST_BOOT_RATE_HIGH;

module_param_named(
			pbr_high,
			bcmtch_post_boot_rate_high,
			int,
			S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pbr_high, "Post Boot Download Rate - High");

static int bcmtch_post_boot_rate_low = BCMTCH_POST_BOOT_RATE_LOW;

module_param_named(pbr_low, bcmtch_post_boot_rate_low, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pbr_low, "Post Boot Download Rate - Low");

/*- watch dog duration -*/

/* x milliseconds in jiffies */
#define	BCMTCH_WATCHDOG_NORMAL		5000
#define	BCMTCH_WATCHDOG_POST_BOOT	50

static uint32_t bcmtch_watchdog_normal = BCMTCH_WATCHDOG_NORMAL;

module_param_named(
		wdg_normal,
		bcmtch_watchdog_normal,
		uint,
		S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(wdg_normal, "Watch dog rate in normal operation (ms)");

static uint32_t bcmtch_watchdog_post_boot = BCMTCH_WATCHDOG_POST_BOOT;

module_param_named(
		wdg_post_boot,
		bcmtch_watchdog_post_boot,
		uint,
		S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(wdg_post_boot, "Watch dog rate at post boot download (ms)");


/*- debug flag -*/

#define BCMTCH_DF_MV	0x00000001		/* touch MOVE event */
#define BCMTCH_DF_UP	0x00000002		/* touch UP event */
#define BCMTCH_DF_TE	0x00000004		/* touch extension events */
#define BCMTCH_DF_BT	0x00000008		/* BUTTON event */
#define BCMTCH_DF_CH	0x00000010		/* channel protocol */
#define BCMTCH_DF_ST	0x00000020		/* state protocol */
#define BCMTCH_DF_RC	0x00000040		/* rm */
#define BCMTCH_DF_RF	0x00000080		/* rf */
#define BCMTCH_DF_FR	0x00000100		/* frame events */
#define BCMTCH_DF_FE	0x00000200		/* frame extension events */
#define BCMTCH_DF_PB	0x00000400		/* post-boot */
#define BCMTCH_DF_DT	0x00000800		/* device tree */
#define BCMTCH_DF_HO	0x00001000		/* host override */
#define BCMTCH_DF_PM	0x00002000		/* power management */
#define BCMTCH_DF_WD	0x00004000		/* watch dog */
#define BCMTCH_DF_IH	0x00008000		/* interrupt */
#define BCMTCH_DF_I2C	0x00010000		/* i2c */
#define BCMTCH_DF_INFO	0x00020000		/* info */

static int bcmtch_debug_flag; /* = 0; */

module_param_named(debug_flag, bcmtch_debug_flag, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_flag, "Debug Bit-Flag");

/* ---------------------------------------- */
/* - BCM Touch Controller Firmware Tables - */
/* ---------------------------------------- */

#define	BCMTCHWC	0x9999

struct firmware_load_info_t {
	uint32_t chip_id;
	uint32_t chip_rev;
	uint8_t *filename;
	uint32_t addr;
	uint32_t flags;
};

static const struct firmware_load_info_t bcmtch_binaries[] = {

	/* MUST ALWAYS START WITH WILDCARD */
	{BCMTCHWC, BCMTCHWC, "bcmtchfw_bin", 0, BCMTCH_FIRMWARE_FLAGS_COMBI},

	/*
	* ADD CHIP SPECIFIC BINARIES HERE
	*/

	/*
	** CHIP ID specific
	*{0x15200, BCMTCHWC, "bcmtch15200_bin", 0, BCMTCH_FIRMWARE_FLAGS_COMBI},
	*/

	/*
	** CHIP ID & CHIP REV specific
	*{0x15200, 0xa0, "bcmtch15200a1_bin", 0, BCMTCH_FIRMWARE_FLAGS_COMBI},
	*
	*/
};


/* ------------------------------------------ */
/* - BCM Touch Controller Driver Structures - */
/* ------------------------------------------ */

/*- work queue enum -*/

enum bcmtch_work_process_function {
	BCMTCH_WP_CHANNEL,
	BCMTCH_WP_PATCH_INIT,
	BCMTCH_WP_STATE,
	BCMTCH_WP_NUMBER
};

/*- bcmtch channel structures -*/

struct bcmtch_channel {
	struct tofe_channel_instance_cfg	cfg;
	struct tofe_channel_header		hdr;
	uint16_t					queued;
	uint8_t						active;
	/* intentional pad - may use for i2c tranactions */
	uint8_t						_pad8;
	uint32_t data;
};

/**
	@ struct bcmtch_response_wait
	@ brief storage of the results from response channel.
*/
struct bcmtch_response_wait {
	bool		wait;
	uint32_t	resp_data;
};

struct bcmtch_event {
	uint32_t           event_kind:3;
	uint32_t           _pad:29;
};

enum bcmtch_event_frame_extension_kind {
	BCMTCH_EVENT_FRAME_EXTENSION_KIND_TIMESTAMP,
	BCMTCH_EVENT_FRAME_EXTENSION_KIND_CHECKSUM,
	BCMTCH_EVENT_FRAME_EXTENSION_KIND_HEARTBEAT,

	BCMTCH_EVENT_FRAME_EXTENSION_KIND_MAX = 7  /* 3 bits */
};

struct bcmtch_event_frame {
	uint32_t           event_kind:3;
	uint32_t           _pad:1;
	uint32_t           frame_id:12;
	uint32_t           timestamp:16;
};

struct bcmtch_event_frame_extension {
	uint32_t           event_kind:3;
	uint32_t           frame_kind:3;
	uint32_t           _pad:26;
};

struct bcmtch_event_frame_extension_timestamp {
	uint32_t           event_kind:3;
	uint32_t           frame_kind:3;
	uint32_t           _pad:2;
	uint32_t           scan_end:8;
	uint32_t           mtc_start:8;
	uint32_t           mtc_end:8;
};

struct bcmtch_event_frame_extension_checksum {
	uint32_t           event_kind:3;
	uint32_t           frame_kind:3;
	uint32_t           _pad:10;
	uint32_t           hash:16;
};

struct bcmtch_event_frame_extension_heartbeat {
	uint32_t           event_kind:3;
	uint32_t           frame_kind:3;
	uint32_t           _pad:2;
	uint32_t           timestamp:24; /* 100 us units, free running */
};

enum bcmtch_event_touch_extension_kind {
	BCMTCH_EVENT_TOUCH_EXTENSION_KIND_DETAIL,
	BCMTCH_EVENT_TOUCH_EXTENSION_KIND_BLOB,
	BCMTCH_EVENT_TOUCH_EXTENSION_KIND_SIZE,
	BCMTCH_EVENT_TOUCH_EXTENSION_KIND_HOVER,
	BCMTCH_EVENT_TOUCH_EXTENSION_KIND_TOOL,

	BCMTCH_EVENT_TOUCH_EXTENSION_KIND_MAX = 7  /* 3 bits */
};

enum bcmtch_event_touch_tool {
	BCMTCH_EVENT_TOUCH_TOOL_FINGER,
	BCMTCH_EVENT_TOUCH_TOOL_STYLUS,
};

struct bcmtch_event_touch {
	uint32_t           event_kind:3;
	uint32_t           track_tag:5;
	uint32_t           x:12;
	uint32_t           y:12;
};

struct bcmtch_event_touch_end {
	uint32_t           event_kind:3;
	uint32_t           track_tag:5;
	uint32_t           _pad:24;
};

struct bcmtch_event_touch_extension {
	uint32_t           event_kind:3;
	uint32_t           touch_kind:3;
	uint32_t           _pad:26;
};

struct bcmtch_event_touch_extension_detail {
	uint32_t           event_kind:3;
	uint32_t           touch_kind:3;
	uint32_t           confident:1;
	uint32_t           suppressed:1;
	uint32_t           hover:1;
	uint32_t           tool:1;
	uint32_t           large_touch:1;
	uint32_t           _pad:1;
	uint32_t           pressure:8;
	uint32_t           orientation:12;
};

struct bcmtch_event_touch_extension_blob {
	uint32_t           event_kind:3;
	uint32_t           touch_kind:3;
	uint32_t           area:8;
	uint32_t           total_cap:18;
};

#define BCMTCH_EVENT_TOUCH_EXTENSION_AREA_MAX 255

struct bcmtch_event_touch_extension_size {
	uint32_t           event_kind:3;
	uint32_t           touch_kind:3;
	uint32_t           _pad:6;
	uint32_t           major_axis:10;
	uint32_t           minor_axis:10;
};

struct bcmtch_event_touch_extension_hover {
	uint32_t           event_kind:3;
	uint32_t           touch_kind:3;
	uint32_t           _pad:16;
	uint32_t           height:10;
};

struct bcmtch_event_touch_extension_tool {
	uint32_t           event_kind:3;
	uint32_t           touch_kind:3;
	uint32_t           _pad:6;
	uint32_t           width_major:10;
	uint32_t           width_minor:10;
};

enum bcmtch_event_button_kind {
	BCMTCH_EVENT_BUTTON_KIND_CONTACT,
	BCMTCH_EVENT_BUTTON_KIND_HOVER,

	BCMTCH_EVENT_BUTTON_KIND_MAX = 1
};

struct bcmtch_event_button {
	uint32_t           event_kind:3;
	uint32_t           button_kind:1;
	uint32_t           _pad:12;
	uint32_t           status:16;
};

/* driver structure for a single touch point */
struct bcmtch_touch {
	uint16_t	x;		/* X Coordinate */
	uint16_t	y;		/* Y Coordinate */
	uint16_t	major_axis;
	uint16_t	minor_axis;
	uint16_t	width_major;
	uint16_t	width_minor;
	uint8_t		type;
	uint8_t		pressure;
	int16_t		orientation;

	/* Touch status: Down, Move, Up (Inactive) */
	enum _bcmtch_touch_status	status;

	enum bcmtch_event_kind		event;	/* Touch Event Kind */
};

/* ---------------------------- */
/* - State Protocol Constants - */
/* ---------------------------- */

enum bcmtch_state_protocol_cmd {
	BCMTCH_CMD_LONG = 0x20,
	BCMTCH_CMD_START_SCAN = 0x21,
	BCMTCH_CMD_STOP_SCAN = 0x22,
	BCMTCH_CMD_SUSPEND = 0x23,
	BCMTCH_CMD_RESUME = 0x24,
	BCMTCH_CMD_SLEEP = 0x28,
	BCMTCH_CMD_SINGLE_SCAN = 0x30,
	BCMTCH_CMD_READ_RAM = 0x31,
	BCMTCH_CMD_WRITE_RAM = 0x32,
};

enum bcmtch_state_slot_type {
	STATE_SLOT_TYPE_EMPTY = 0,
	STATE_SLOT_TYPE_BUTTON = 1,
	STATE_SLOT_TYPE_STYLUS = 2,
	STATE_SLOT_TYPE_FINGER = 8,
	STATE_SLOT_TYPE_RESERVED = 96,
};

#define TOFE_HOST_CMD_BUF_SIZE              512
#define TOFE_HOST_CMD_SHORT_SIZE            4
#define TOFE_HOST_MAX_NUM_SLOTS             15
#define TOFE_HOST_TOUCH_SLOT_SMALL_SIZE     4
#define TOFE_HOST_TOUCH_SLOT_BIG_SIZE       8
#define TOFE_HOST_LOG_SLOT_SIZE             12
#define TOFE_HOST_SLOT_AREA_MIN             1

#define TOFE_HOST_EXCEPTION_REGS	24

#define TOFE_HOST_RSP_BUF_SIZE \
			(BCMTCH_MAX_TOUCH \
			* TOFE_HOST_TOUCH_SLOT_BIG_SIZE + 1)

/*- State protocol RAM access configuration */

struct bcmtch_state_cfg_rw {
	uint16_t cfg_size;
	uint16_t max_read_size;
	uint16_t max_write_size;
	uint16_t patch_cfg_max_size;
	uint32_t patch_cfg_addr;
};

#pragma pack(push, 1)
struct bcmtch_state_rw_params {
	uint32_t addr;
	uint16_t len;
};
#pragma pack(pop)

/*- Touch scan command/response format. -*/

union bcmtch_state_cmd_start_scan {
	struct {
		unsigned int touch_slot_format:4;
		unsigned int max_slot_number:4;
	};
	uint8_t reg;
};

union bcmtch_state_ret_start_scan {
	struct {
		unsigned int slots:4;
		unsigned int frame:3;
		unsigned int done:1;
	};
	uint8_t reg;
};

struct bcmtch_state_start_scan {
	union bcmtch_state_cmd_start_scan	cmd;
	union bcmtch_state_ret_start_scan	status;
	uint8_t	touch_slot_size;
	uint8_t	touch_slot_num;
	uint8_t	log_slot_num;
};

union bcmtch_state_resp_slot {
	struct {
		unsigned int type:7;
		unsigned int frame:1;
		unsigned int pad:24;
	} slot_header;

	struct {
		unsigned int type:7;
		unsigned int frame:1;
		unsigned int x:12;
		unsigned int y:12;
		unsigned int pressure:8;
		unsigned int orientation:8;
		unsigned int major:8;
		unsigned int minor:8;
	} long_slot;

	struct {
		unsigned int type:7;
		unsigned int frame:1;
		unsigned int x:12;
		unsigned int y:12;
	} short_slot;

	struct {
		unsigned int type:7;
		unsigned int frame:1;
		unsigned int buttons:24;
	} button_slot;
};

struct bcmtch_data {
	/* core 0S elements */
	/* Work queue structure for defining work queue handler */
	struct work_struct work;
	/* Work queue structure for transaction handling */
	struct workqueue_struct *p_workqueue;

	/* The table of worker process functions */
	void (*bcmtch_dev_process_table[BCMTCH_WP_NUMBER])
		(struct bcmtch_data *);
	/* The index of the process function table */
	uint8_t work_process_index;

	/* Critical Section : Mutex : */
	struct mutex mutex_work;

	/* Pointer to allocated memory for input device */
	struct input_dev *p_input_device;

	struct device *p_device;
	/* I2C 0S elements */

	/* SPM I2C Client structure pointer */
	struct i2c_client *p_i2c_client_spm;

	/* SYS I2C Client structure pointer */
	struct i2c_client *p_i2c_client_sys;

	/* Local copy of platform data structure */
	struct bcmtch_platform_data platform_data;

	/* BCM Touch elements */
	struct bcmtch_channel *p_channels[BCMTCH_CHANNEL_MAX];

	/* BCMTCH touch structure */
	struct bcmtch_touch touch[BCMTCH_MAX_TOUCH];
	uint32_t touch_count;

	uint8_t touch_event_track_id;

	/* Response storage */
	struct bcmtch_response_wait bcmtch_cmd_response[TOFE_COMMAND_LAST];

	/* BCM Button elements */
	uint16_t button_status;
	const int bcmtch_button_map[BCMTCH_MAX_BUTTONS];

	/* DMA transfer mode */
	bool has_dma_channel;
	bool host_override;
	uint32_t fw_dma_buffer_size;
	void *fw_dma_buffer;

	/* Interrupts */
	bool irq_pending;
	bool irq_enabled;

	/* Suspend/Resume Status */
	bool suspend;

	/* Hard Reset Ok */
	bool gpio_hw_reset;

	/* Critical Section : Mutex : */
	spinlock_t	lock_suspend;

	struct regulator *regulator_avdd33;
	struct regulator *regulator_vddo;
	struct regulator *regulator_avdd_adldo;

	/* Watchdog Timer */
	uint32_t watchdog_expires;
	struct timer_list watchdog;

	/* Post Boot Downloads */
	uint8_t	post_boot_pending;
	uint8_t *post_boot_buffer;
	uint8_t *post_boot_data;
	uint32_t post_boot_addr;
	uint16_t post_boot_left;
	uint16_t post_boot_sections;
	uint16_t post_boot_section;
	uint16_t post_boot_patches;
	uint32_t post_boot_cfg_addr;
	uint32_t post_boot_cfg_length;

	/* Axis Limits */
	uint16_t axis_x_max;
	uint16_t axis_x_min;
	uint16_t axis_y_max;
	uint16_t axis_y_min;
	uint16_t axis_h_max;
	uint16_t axis_h_min;

	/* Thresholds */
	uint16_t threshold_gate_finger;
	uint16_t threshold_gate_stylus;

	/* FW Headers */
	uint16_t fw_entry_num;
	struct combi_entry fw_headers[32];
	struct combi_entry fw_screened_headers
		[BCMTCH_FIRMWARE_MAX_ENTRIES];

	/* FW Signature */
	struct tofe_signature fw_signature;

	/* BCMTCH chip ID */
	uint32_t chip_id;

	/* BCMTCH revision ID */
	uint8_t rev_id;

	/* HW Id */
	uint32_t otp_hw_id;
	struct otp_id_mem_cfg otp_hw_id_cfg;

	/* ROM boot */
	bool boot_from_rom;

#ifdef CONFIG_HAS_EARLYSUSPEND
	/* Early suspend */
	struct early_suspend bcmtch_early_suspend_desc;
#endif

	bool state_protocol;
	struct bcmtch_state_cfg_rw ram_rw_cfg;
	struct bcmtch_state_start_scan scan_data;
	uint32_t exception_buffer[TOFE_HOST_EXCEPTION_REGS];
	uint8_t bcmtch_state_cmd_buffer[TOFE_HOST_CMD_BUF_SIZE];
	uint8_t bcmtch_state_resp_buffer[TOFE_HOST_RSP_BUF_SIZE];

	/* sysfs ABI data */
	struct mutex mutex_abi;
	uint8_t abi_spm_addr;
	uint32_t abi_sys_addr;
	uint16_t abi_sys_size;
	unsigned char abi_buffer[BCMTCH_ABI_SIZE_MAX];
};

/* -------------------------------------------- */
/* - BCM Touch Controller Function Prototypes - */
/* -------------------------------------------- */

/*  DEV Prototypes */
static void	   bcmtch_dev_process(
	struct bcmtch_data *);
static void    bcmtch_dev_process_pb_patch_init(
	struct bcmtch_data *);
static int bcmtch_dev_reset(struct bcmtch_data *,
	uint8_t);
static int bcmtch_dev_suspend(
	struct bcmtch_data *);
static int bcmtch_dev_resume(
	struct bcmtch_data *);
static int bcmtch_dev_request_power_mode(
	struct bcmtch_data *,
	uint8_t, enum tofe_command);
static int bcmtch_dev_send_command(
	struct bcmtch_data *,
	enum tofe_command, uint32_t, uint16_t, uint8_t);
static inline bool bcmtch_dev_verify_buffer_header(
	struct bcmtch_data *,
	struct tofe_channel_buffer_header*);
static int bcmtch_dev_post_boot_download(
	struct bcmtch_data *bcmtch_data_ptr,
	int16_t data_rate);
static unsigned int bcmtch_dev_post_boot_get_section(
	struct bcmtch_data *);
static void    bcmtch_dev_watchdog_work(unsigned long int data);
static void    bcmtch_dev_watchdog_start(
	struct bcmtch_data *);
static int bcmtch_dev_watchdog_reset(
	struct bcmtch_data *);
static int bcmtch_dev_watchdog_stop(
	struct bcmtch_data *);
static int bcmtch_dev_watchdog_restart(
	struct bcmtch_data *, uint32_t);
static int bcmtch_dev_watchdog_check(
	struct bcmtch_data *);
static int bcmtch_dev_power_init(
	struct bcmtch_data *);
static int bcmtch_dev_power_enable(
	struct bcmtch_data *,
	bool);
static int bcmtch_dev_power_free(
	struct bcmtch_data *);
static void bcmtch_dev_reset_events(
	struct bcmtch_data *bcmtch_data_ptr);
static int bcmtch_dev_sync_event_frame(
	struct bcmtch_data *bcmtch_data_ptr);
static int bcmtch_dev_read_otp(
	struct bcmtch_data *, uint32_t, uint32_t*);
static void bcmtch_dev_dump_fw_header(
	struct bcmtch_data *, struct combi_entry *, int);

static void bcmtch_dev_init_state(
	struct bcmtch_data *);
static void	bcmtch_dev_process_state_touches(
	struct bcmtch_data *);
static int bcmtch_state_start_scan(
	struct bcmtch_data *);
static int bcmtch_state_stop_scan(
	struct bcmtch_data *);
static int bcmtch_state_sleep(
	struct bcmtch_data *);
static int bcmtch_state_suspend(
	struct bcmtch_data *);
static int bcmtch_state_resume(
	struct bcmtch_data *);
static int bcmtch_state_write_ram(
	struct bcmtch_data *, uint32_t, uint16_t, uint8_t *);
static int bcmtch_state_read_ram(
	struct bcmtch_data *, uint32_t, uint16_t, uint8_t *, uint16_t);
static int bcmtch_state_check_command_status(
	struct bcmtch_data *, int);
static int bcmtch_state_i2c_send_command(
	struct bcmtch_data *, uint8_t, uint8_t*, int);

/*  COM Prototypes */
static int  bcmtch_com_init(struct bcmtch_data *);
static inline int  bcmtch_com_read_spm(
	struct bcmtch_data *, uint8_t, uint8_t*);
static inline int  bcmtch_com_write_spm(
	struct bcmtch_data *, uint8_t, uint8_t);
static inline int  bcmtch_com_read_sys(
	struct bcmtch_data *, uint32_t, uint16_t, uint8_t*);
static inline int  bcmtch_com_write_sys(
	struct bcmtch_data *, uint32_t, uint16_t, uint8_t*);
static inline int bcmtch_com_read_dma(
	struct bcmtch_data *, uint16_t, uint8_t*);
/* COM Helper */
static inline int bcmtch_com_fast_write_spm(
	struct bcmtch_data *, uint8_t, uint8_t*, uint8_t*);
static inline int bcmtch_com_write_sys32(
	struct bcmtch_data *, uint32_t, uint32_t);

/*  OS Prototypes */
static void bcmtch_reset(struct bcmtch_data *);
static int bcmtch_interrupt_enable(struct bcmtch_data *);
static void bcmtch_interrupt_disable(struct bcmtch_data *);
static void bcmtch_deferred_worker(struct work_struct *work);
static void bcmtch_clear_deferred_worker(struct bcmtch_data *);

#ifdef CONFIG_PM
#ifndef CONFIG_HAS_EARLYSUSPEND
static int      bcmtch_suspend(
					struct i2c_client *p_client,
					pm_message_t mesg);
static int      bcmtch_resume(struct i2c_client *p_client);
#endif
#endif

/*  OS I2C Prototypes */
static int  bcmtch_i2c_probe(
					struct i2c_client*,
					const struct i2c_device_id *);
static int  bcmtch_i2c_remove(struct i2c_client *);
static int  bcmtch_i2c_read_spm(struct i2c_client*, uint8_t, uint8_t*);
static int  bcmtch_i2c_write_spm(struct i2c_client*, uint8_t, uint8_t);
static int  bcmtch_i2c_fast_write_spm(
					struct i2c_client*,
					uint8_t,
					uint8_t*,
					uint8_t*);
static int  bcmtch_i2c_read_sys(
					struct i2c_client*,
					uint32_t,
					uint16_t,
					uint8_t*);
static int  bcmtch_i2c_write_sys(
					struct i2c_client*,
					uint32_t,
					uint16_t,
					uint8_t*);
static int  bcmtch_i2c_init_clients(struct i2c_client *);
static void     bcmtch_i2c_free_clients(struct bcmtch_data *);
static int  bcmtch_i2c_read_dma(
					struct i2c_client*,
					uint16_t,
					uint8_t*);
static int bcmtch_i2c_transfer(
					struct i2c_adapter *adap,
					struct i2c_msg *msgs,
					int num);

/* ------------------------------------------------- */
/* - BCM Touch Controller SysFs ABI Implementation - */
/* ------------------------------------------------- */

/* -- SYS peek/poke ABI -- */
static ssize_t bcmtch_os_abi_sys_addr_show(
					struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE,
			"0x%04X %u\n",
			bcmtch_data_ptr->abi_sys_addr,
			bcmtch_data_ptr->abi_sys_size);
}

static ssize_t bcmtch_os_abi_sys_addr_store(
					struct device *dev,
					struct device_attribute *devattr,
					const char *buf,
					size_t count)
{
	int ret_val = 0;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);

	uint32_t addr;
	unsigned int len;

	ret_val = sscanf(buf, "0x%x %u", &addr, &len);
	if (ret_val != 2) {
		dev_err(dev,
			"format: 0x<32bit address> <memory chunk size in bytes>\n");
		return -EINVAL;
	}

	mutex_lock(&bcmtch_data_ptr->mutex_abi);
	bcmtch_data_ptr->abi_sys_addr = addr;
	bcmtch_data_ptr->abi_sys_size = (uint16_t)len;
	mutex_unlock(&bcmtch_data_ptr->mutex_abi);

	return count;
}

static DEVICE_ATTR(sys_addr,
		S_IRUGO|S_IWUSR|S_IWGRP,
		bcmtch_os_abi_sys_addr_show,
		bcmtch_os_abi_sys_addr_store);

static ssize_t bcmtch_os_abi_sys_data_show(
					struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	int count = 0;
	int ret_val = 0;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);

	int i, cnt;

	if (bcmtch_data_ptr->abi_sys_size > BCMTCH_ABI_SIZE_MAX)
		return -EINVAL;

	mutex_lock(&bcmtch_data_ptr->mutex_work);
	if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE)
		ret_val = bcmtch_state_read_ram(
					bcmtch_data_ptr,
					bcmtch_data_ptr->abi_sys_addr,
					bcmtch_data_ptr->abi_sys_size,
					bcmtch_data_ptr->abi_buffer,
					BCMTCH_ABI_SIZE_MAX);
	else
		ret_val = bcmtch_com_read_sys(
					bcmtch_data_ptr,
					bcmtch_data_ptr->abi_sys_addr,
					bcmtch_data_ptr->abi_sys_size,
					bcmtch_data_ptr->abi_buffer);
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	if (!ret_val) {
		for (i = 0; i < bcmtch_data_ptr->abi_sys_size; i++) {
			cnt = snprintf(buf + count, PAGE_SIZE - count,
						"%02X ",
						bcmtch_data_ptr->abi_buffer[i]);
			count += (cnt > 0 ? cnt : 0);
		}
		cnt = snprintf(buf + count,
				PAGE_SIZE - count, "\n");
		count += (cnt > 0 ? cnt : 0);
	}

	return count;
}

static ssize_t bcmtch_os_abi_sys_data_store(
					struct device *dev,
					struct device_attribute *devattr,
					const char *buf,
					size_t count)
{
	int ret_val = 0;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);
	char tok[5] = "0xff";

	unsigned int in_data;
	int i, j, k;

	for (i = 0, j = 0, k = 0; i < count; i++) {
		if (isspace(buf[i])) {
			if (j) {
				tok[j] = '\0';
				ret_val = kstrtouint(tok, 16, &in_data);
				if (!ret_val)
					bcmtch_data_ptr->abi_buffer[k++]
					= (uint8_t)in_data;
			}
			j = 0;
			continue;
		}
		tok[j++] = buf[i];
		j %= ARRAY_SIZE(tok);
	}

	if (k < bcmtch_data_ptr->abi_sys_size) {
		dev_err(dev, "ERROR: only read %u of %u data\n",
				k, bcmtch_data_ptr->abi_sys_size);
		return -EINVAL;
	}

	mutex_lock(&bcmtch_data_ptr->mutex_work);
	if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE)
		ret_val = bcmtch_state_write_ram(
				bcmtch_data_ptr,
				bcmtch_data_ptr->abi_sys_addr,
				bcmtch_data_ptr->abi_sys_size,
				bcmtch_data_ptr->abi_buffer);
	else
		ret_val = bcmtch_com_write_sys(
				bcmtch_data_ptr,
				bcmtch_data_ptr->abi_sys_addr,
				bcmtch_data_ptr->abi_sys_size,
				bcmtch_data_ptr->abi_buffer);
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	if (ret_val) {
		dev_err(dev, "ERROR: write sys(AHB) error! (%d)\n",
				ret_val);
		return -EIO;
	}

	return count;
}

static DEVICE_ATTR(sys_data,
		S_IRUGO|S_IWUSR|S_IWGRP,
		bcmtch_os_abi_sys_data_show,
		bcmtch_os_abi_sys_data_store);


/* -- SPM peek/poke ABI -- */
static ssize_t bcmtch_os_abi_spm_addr_show(
					struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE,
			"0x%02x\n", bcmtch_data_ptr->abi_spm_addr);
}

static ssize_t bcmtch_os_abi_spm_addr_store(
					struct device *dev,
					struct device_attribute *devattr,
					const char *buf,
					size_t count)
{
	int ret_val = 0;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);
	unsigned int addr;

	ret_val = kstrtouint(buf, 16, &addr);
	if (ret_val)
		return ret_val;

	mutex_lock(&bcmtch_data_ptr->mutex_abi);
	bcmtch_data_ptr->abi_spm_addr = (uint8_t)addr;
	mutex_unlock(&bcmtch_data_ptr->mutex_abi);

	return count;
}

static DEVICE_ATTR(spm_addr,
		S_IRUGO|S_IWUSR|S_IWGRP,
		bcmtch_os_abi_spm_addr_show,
		bcmtch_os_abi_spm_addr_store);

static ssize_t bcmtch_os_abi_spm_data_show(
					struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);
	int ret_val;
	uint8_t r8;

	mutex_lock(&bcmtch_data_ptr->mutex_work);
	ret_val = bcmtch_com_read_spm(bcmtch_data_ptr,
			bcmtch_data_ptr->abi_spm_addr, &r8);
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	if (ret_val) {
		dev_err(dev, "ERROR: read spm error! (%d)\n",
				ret_val);
		return -EIO;
	}

	return snprintf(buf, PAGE_SIZE,
				"0x%02x\n", r8);
}

static ssize_t bcmtch_os_abi_spm_data_store(
					struct device *dev,
					struct device_attribute *devattr,
					const char *buf,
					size_t count)
{
	int ret_val;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);
	unsigned int in_data;

	ret_val = kstrtouint(buf, 16, &in_data);
	if (ret_val) {
		dev_err(dev,
			"ERROR: %s() - failed to convert string to uint! (%d)\n",
			__func__,
			ret_val);
		return ret_val;
	}

	mutex_lock(&bcmtch_data_ptr->mutex_work);
	ret_val = bcmtch_com_write_spm(bcmtch_data_ptr,
			bcmtch_data_ptr->abi_spm_addr,
			(uint8_t)in_data);
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	if (ret_val) {
		dev_err(dev,
			"Write spm error! (%d)\n",
			ret_val);
		return -EIO;
	}

	return count;
}

static DEVICE_ATTR(spm_data,
		S_IRUGO|S_IWUSR|S_IWGRP,
		bcmtch_os_abi_spm_data_show,
		bcmtch_os_abi_spm_data_store);

/* -- Finger threshold ABI -- */
static ssize_t bcmtch_os_abi_finger_threshold(
					struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE,
			"%u\n", bcmtch_data_ptr->threshold_gate_finger);
}

static DEVICE_ATTR(
		finger_threshold,
		S_IRUGO,
		bcmtch_os_abi_finger_threshold,
		NULL);

/* -- Stylus threshold ABI -- */
static ssize_t bcmtch_os_abi_stylus_threshold(
					struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE,
			"%u\n", bcmtch_data_ptr->threshold_gate_stylus);
}

static DEVICE_ATTR(
		stylus_threshold,
		S_IRUGO,
		bcmtch_os_abi_stylus_threshold,
		NULL);

/* -- Suspend ABI -- */
static ssize_t bcmtch_os_abi_suspend_show(
					struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE,
			"%u\n",
			bcmtch_data_ptr->suspend ? 1 : 0);
}

static ssize_t bcmtch_os_abi_suspend_store(
					struct device *dev,
					struct device_attribute *devattr,
					const char *buf,
					size_t count)
{
	int ret_val = 0;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);
	unsigned int val;

	ret_val = kstrtouint(buf, 10, &val);
	if (ret_val)
		goto abi_suspend_error;

	switch (val) {
	case 0:
		if (bcmtch_data_ptr->suspend)
			bcmtch_dev_resume(bcmtch_data_ptr);
		break;
	case 1:
		if (!bcmtch_data_ptr->suspend)
			bcmtch_dev_suspend(bcmtch_data_ptr);
		break;
	default:
		goto abi_suspend_error;
	}

	return count;

abi_suspend_error:
	dev_err(dev, "ERROR: suspend ABI - set 0 or 1\n");
	return -EINVAL;
}

static DEVICE_ATTR(suspend,
		S_IRUGO|S_IWUSR|S_IWGRP,
		bcmtch_os_abi_suspend_show,
		bcmtch_os_abi_suspend_store);


static struct attribute *bcmtch_abi_attrs[] = {
	&dev_attr_spm_addr.attr,
	&dev_attr_spm_data.attr,
	&dev_attr_sys_addr.attr,
	&dev_attr_sys_data.attr,
	&dev_attr_finger_threshold.attr,
	&dev_attr_stylus_threshold.attr,
	&dev_attr_suspend.attr,
	NULL
};

static const struct attribute_group bcmtch_attr_group = {
	.attrs = bcmtch_abi_attrs,
};


static int bcmtch_os_init_abi(struct device *p_device)
{
	int ret_val = 0;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(p_device);

	ret_val = sysfs_create_group(&p_device->kobj, &bcmtch_attr_group);
	if (ret_val)
		dev_err(p_device,
			"ERROR: %s() - device_create_file() failed!\n",
			__func__);

	bcmtch_data_ptr->suspend = false;
	bcmtch_data_ptr->abi_spm_addr = BCMTCH_SPM_REG_MSG_TO_HOST;
	bcmtch_data_ptr->abi_sys_addr = BCMTCH_ADDR_SPM_BASE;
	bcmtch_data_ptr->abi_sys_size = 4;

	return ret_val;
}

static inline void bcmtch_os_free_abi(struct device *p_device)
{
	sysfs_remove_group(&p_device->kobj, &bcmtch_attr_group);
}

/* ------------------------------------------- */
/* - BCM Touch Controller CLI Implementation - */
/* ------------------------------------------- */

static void bcmtch_os_cli_dump_fw_headers(
					struct bcmtch_data *bcmtch_data_ptr)
{
	/* Print the firmware signature info */
	dev_info(bcmtch_data_ptr->p_device,
		"Sig: %08x %08x %08x %08x\n",
		bcmtch_data_ptr->fw_headers[0].offset,
		bcmtch_data_ptr->fw_headers[0].addr,
		bcmtch_data_ptr->fw_headers[0].length,
		bcmtch_data_ptr->fw_headers[0].flags);

	/* Dump the firmware header entries */
	bcmtch_dev_dump_fw_header(
			bcmtch_data_ptr,
			(struct combi_entry *)
				&bcmtch_data_ptr->fw_headers[1],
			bcmtch_data_ptr->fw_entry_num);
}

static void bcmtch_os_cli_versions(
					struct bcmtch_data *bcmtch_data_ptr)
{
	/* Return driver and firmware version info */
	dev_info(bcmtch_data_ptr->p_device,
		"Driver: %s : %s : %s\n",
		BCMTCH_DRIVER_VERSION,
		BCMTCH_DRIVER_BUILD_DATE,
		BCMTCH_DRIVER_BUILD_TIME);

	dev_info(bcmtch_data_ptr->p_device,
		"F/W Version: %d.%d.%d.%d\n",
		bcmtch_data_ptr->fw_signature.version.generation,
		bcmtch_data_ptr->fw_signature.version.spin,
		bcmtch_data_ptr->fw_signature.version.major,
		bcmtch_data_ptr->fw_signature.version.minor);

	dev_info(bcmtch_data_ptr->p_device,
		"F/W Commit: %016llx\n",
		bcmtch_data_ptr->fw_signature.commit);

	dev_info(bcmtch_data_ptr->p_device,
		"F/W Build:%d Compatibility:%d Variant:%d\n",
		bcmtch_data_ptr->fw_signature.build,
		bcmtch_data_ptr->fw_signature.compatibility,
		bcmtch_data_ptr->fw_signature.variant);

	dev_info(bcmtch_data_ptr->p_device,
		"F/W ReleaseType:%c ReleaseNum:%d CustReleaseNum:%d\n",
		bcmtch_data_ptr->fw_signature.release_type,
		bcmtch_data_ptr->fw_signature.release_number,
		bcmtch_data_ptr->fw_signature.cust_release_num);

	if (BCMTCH_BF_READ_OTP & bcmtch_boot_flag)
		dev_info(bcmtch_data_ptr->p_device,
			"H/W Id: %0x\n",
			bcmtch_data_ptr->otp_hw_id);
}

static void bcmtch_os_cli_spm_poke(
					struct bcmtch_data *bcmtch_data_ptr,
					uint8_t in_addr,
					uint8_t in_data)
{
	mutex_lock(&bcmtch_data_ptr->mutex_work);

	bcmtch_com_write_spm(bcmtch_data_ptr,
		in_addr, in_data);

	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	dev_info(bcmtch_data_ptr->p_device,
		"poke spm Reg=%08x data=%08x\n",
		in_addr, in_data);
}

static void bcmtch_os_cli_spm_peek(
					struct bcmtch_data *bcmtch_data_ptr,
					uint8_t in_addr)
{
	uint8_t r8;

	mutex_lock(&bcmtch_data_ptr->mutex_work);

	bcmtch_com_read_spm(bcmtch_data_ptr, in_addr, &r8);

	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	dev_info(bcmtch_data_ptr->p_device,
		"peek spm reg=0x%02x data=0x%02x\n",
		in_addr, r8);
}

static void bcmtch_os_cli_sys_poke(
					struct bcmtch_data *bcmtch_data_ptr,
					uint32_t in_addr,
					uint32_t in_data)
{
	mutex_lock(&bcmtch_data_ptr->mutex_work);

	if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE)
		bcmtch_state_write_ram(
				bcmtch_data_ptr,
				in_addr,
				sizeof(uint32_t),
				(uint8_t *)&in_data);
	else
		bcmtch_com_write_sys32(bcmtch_data_ptr, in_addr, in_data);

	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	dev_info(bcmtch_data_ptr->p_device,
		"poke sys addr=0x%08x data=0x%08x\n",
		in_addr, in_data);
}

static void bcmtch_os_cli_sys_peek(
					struct bcmtch_data *bcmtch_data_ptr,
					uint32_t in_addr,
					uint32_t in_count)
{
	uint32_t r_buf[8];
	uint32_t addr = in_addr;
	uint32_t count = in_count;
	char addr_str[16];

	memset(r_buf, 0, sizeof(r_buf));

	snprintf(addr_str, ARRAY_SIZE(addr_str),
			"0x%08x: ", addr);

	dev_info(bcmtch_data_ptr->p_device,
		"peek sys addr=0x%08x len=0x%08x\n",
		in_addr, in_count);

	mutex_lock(&bcmtch_data_ptr->mutex_work);

	while (count) {
		if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE)
			bcmtch_state_read_ram(
					bcmtch_data_ptr,
					addr,
					sizeof(r_buf),
					(uint8_t *)r_buf,
					sizeof(r_buf));
		else
			bcmtch_com_read_sys(bcmtch_data_ptr,
				addr, sizeof(r_buf),
				(uint8_t *)r_buf);

		print_hex_dump(KERN_INFO,
				addr_str,
				DUMP_PREFIX_NONE,
				32, 4,
				(const void *)r_buf,
				sizeof(r_buf),
				false);
		count =
			(count > sizeof(r_buf)) ?
			(count - sizeof(r_buf)) :
			0;
		addr += sizeof(r_buf);
	}

	mutex_unlock(&bcmtch_data_ptr->mutex_work);
}


static ssize_t bcmtch_os_cli(
					struct device *dev,
					struct device_attribute *devattr,
					const char *buf,
					size_t count)
{
	uint32_t in_addr = 0;
	uint32_t in_value_count = 0;
	struct bcmtch_data *bcmtch_data_ptr =
		dev_get_drvdata(dev);


	/* We are now checking for exact number of matches
	 * to the format. Buf and format are const.
	 * sscanf will return 0 on first mismatch.
	 * We don't anticipate buf to be NULL here
	 * Annotate this if coverity flags it
	 */

	/* In future, we should also veriify that address
	 * is valid. We might change this interface in
	 * near future so leave that part as is
	 */
	if ((count > strlen("poke sys 0x 0x")) &&
			sscanf(buf, "poke sys %x %x", &in_addr,
				&in_value_count) == 2) {
		bcmtch_os_cli_sys_poke(bcmtch_data_ptr,
			in_addr, in_value_count);
	} else if ((count > strlen("peek sys 0x 0x")) &&
			sscanf(buf, "peek sys %x %x", &in_addr,
				&in_value_count) == 2) {
		bcmtch_os_cli_sys_peek(bcmtch_data_ptr,
			in_addr, in_value_count);
	} else if ((count > strlen("poke spm 0x 0x")) &&
			sscanf(buf, "poke spm %x %x", &in_addr,
				&in_value_count) == 2) {
		bcmtch_os_cli_spm_poke(bcmtch_data_ptr,
			in_addr & 0xff, in_value_count & 0xff);
	} else if ((count > strlen("peek spm 0x")) &&
			sscanf(buf, "peek spm %x",
				&in_addr) == 1) {
		bcmtch_os_cli_spm_peek(bcmtch_data_ptr,
			in_addr & 0xff);
	} else if (strncmp(buf, "suspend", strlen("suspend")) == 0) {
		if (!bcmtch_data_ptr->suspend)
			bcmtch_dev_suspend(bcmtch_data_ptr);
	} else if (strncmp(buf, "resume", strlen("resume")) == 0) {
		if (bcmtch_data_ptr->suspend)
			bcmtch_dev_resume(bcmtch_data_ptr);
	} else if (strncmp(buf, "threshold finger",
					strlen("threshold finger")) == 0) {
		dev_info(dev, "finger threshold = %u\n",
			bcmtch_data_ptr->threshold_gate_finger);
	} else if (strncmp(buf, "threshold stylus",
					strlen("threshold stylus")) == 0) {
		dev_info(dev, "stylus threshold = %u\n",
			bcmtch_data_ptr->threshold_gate_stylus);
	} else if (strncmp(buf, "versions", strlen("versions")) == 0) {
		bcmtch_os_cli_versions(bcmtch_data_ptr);
	} else if (strncmp(buf, "fw headers", strlen("fw headers")) == 0) {
		bcmtch_os_cli_dump_fw_headers(bcmtch_data_ptr);
	} else {
		dev_info(dev, "Usage:\n");
		dev_info(dev, "poke sys 0x<addr> 0x<data>\n");
		dev_info(dev, "peek sys 0x<addr> 0x<len>\n");
		dev_info(dev, "poke spm 0x<reg> 0x<data>\n");
		dev_info(dev, "peek spm 0x<reg>\n");
		dev_info(dev, "suspend\n");
		dev_info(dev, "resume\n");
		dev_info(dev, "threshold finger\n");
		dev_info(dev, "threshold stylus\n");
		dev_info(dev, "versions\n");
		dev_info(dev, "fw headers\n");
	}

	return count;
}

static struct device_attribute bcmtch_cli_attr =
		__ATTR(cli, S_IWUSR|S_IWGRP, NULL, bcmtch_os_cli);


/* ------------------------------------------- */
/* - BCM Touch Controller Internal Functions - */
/* ------------------------------------------- */

static inline
unsigned bcmtch_channel_num_queued(
		struct tofe_channel_header *channel)
{
	return (unsigned)channel->
		buffer[0]->header.entry_count;
}

/*
    Note: Internal use only function.
*/
static inline char *
_bcmtch_inline_channel_entry(
			struct tofe_channel_header *channel,
			uint32_t byte_index)
{
	return (char *)channel->buffer[0]->data
			+ byte_index;
}

/*
    Note: Internal use only function.
*/
static inline size_t
_bcmtch_inline_channel_byte_index(
		struct tofe_channel_header *channel,
		uint8_t entry_index)
{
	return entry_index * channel->entry_size;
}

/**
    Check if a channel is empty.

    Events are not considered read or writen until the transaction is
    complete.  Therefore, a channel is empty even when in the middle of a
    set of writes.

    @param
	[in] channel Pointer to channel object.

    @retval
	bool True if channel is empty.

*/
static inline bool
bcmtch_inline_channel_is_empty(struct tofe_channel_header *channel)
{
	return (channel->buffer[0]->header.entry_count == 0);
}

/**
    Read a single entry from a channel.  This function must be called during
    a read transaction.

    The pointer returned by this function points into the channel object itself.
    Callers should not modify or reuse this memory.  Callers may not free the
    memory.


    @param
	[in] channel Pointer to channel object.

    @retval
	void * Pointer to returned entry.

*/
static inline void *bcmtch_inline_channel_read(
			struct tofe_channel_header *channel,
			uint16_t index)
{
	size_t byte_index;
	struct tofe_channel_buffer *buff = channel->buffer[0];

	/* Validate that channel has entries. */
	if (buff->header.entry_count == 0)
		return NULL;

	/* Check if buffer data corrupted */
	if (buff->header.entry_size != channel->entry_size)
		return NULL;

	/* Check if read end. */
	if (index >= buff->header.entry_count)
		return NULL;

	/* Find entry in the channel. */
	byte_index = _bcmtch_inline_channel_byte_index(channel, index);
	return (void *)((char *)channel->buffer[0]->data
			+ byte_index);
}

static inline void
tofe_channel_write_begin(struct tofe_channel_header *channel)
{
	struct tofe_channel_buffer_header *buff =
		&channel->buffer[0]->header;
	if (channel->flags & TOFE_CHANNEL_FLAG_INBOUND)
		buff->entry_count = 0;
}

static inline unsigned int
tofe_channel_write(struct tofe_channel_header *channel, void *entry)
{
	struct tofe_channel_buffer_header *buff =
		&channel->buffer[0]->header;
	size_t byte_index =
		channel->entry_size * buff->entry_count;
	char *p_data = _bcmtch_inline_channel_entry(
						channel,
						byte_index);

	if ((channel->flags & TOFE_CHANNEL_FLAG_INBOUND) == 0)
		return -EINVAL;

	if (buff->entry_count >= channel->entry_num)
		return -ENOMEM;

	memcpy(p_data, entry, channel->entry_size);
	buff->entry_count++;

	return 0;
}

static
uint16_t bcmtch_max_h_axis(uint16_t x, uint16_t y)
{
	uint16_t i;

	uint16_t res = 0;
	uint16_t add = 0x8000;

	uint32_t g = (x * x) + (y * y);

	for (i = 0; i < 16; i++) {
		uint16_t temp = res | add;
		uint32_t g2 = temp * temp;

		if (g >= g2)
			res = temp;

		add >>= 1;
	}
	return res;
}

/* ------------------------------------------- */
/* - BCM Touch Controller DEV Functions - */
/* ------------------------------------------- */

static int bcmtch_dev_alloc(struct i2c_client *p_i2c_client)
{
	int ret_val = 0;
	struct bcmtch_data *bcmtch_data_ptr;


	bcmtch_data_ptr =
		vzalloc(sizeof(struct bcmtch_data));

	if (bcmtch_data_ptr == NULL) {
		dev_err(&p_i2c_client->dev,
			"%s: failed to alloc mem.\n", __func__);
		ret_val = -ENOMEM;
	}

	mutex_init(&bcmtch_data_ptr->mutex_work);
	mutex_init(&bcmtch_data_ptr->mutex_abi);
	spin_lock_init(&bcmtch_data_ptr->lock_suspend);

	i2c_set_clientdata(p_i2c_client, (void *)bcmtch_data_ptr);

	bcmtch_data_ptr->p_device = &p_i2c_client->dev;
	return ret_val;
}

static void bcmtch_dev_free(struct i2c_client *p_i2c_client)
{
	struct bcmtch_data *local_bcmtch_data_p =
		(struct bcmtch_data *)
			i2c_get_clientdata(p_i2c_client);

	vfree(local_bcmtch_data_p->post_boot_buffer);

	vfree(local_bcmtch_data_p);
	i2c_set_clientdata(p_i2c_client, NULL);
}

static int bcmtch_dev_init_clocks(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val;
	uint32_t val32;

	/* setup LPLFO - read OTP and set from value */
	ret_val = bcmtch_com_read_sys(
			bcmtch_data_ptr,
			BCMTCH_ADDR_SPM_LPLFO_CTRL_RO,
			4,
			(uint8_t *)&val32);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
			"%s: [%d] read sys error!\n",
			__func__,
			ret_val);
		return ret_val;
	}

	val32 &= 0xF0000000;
	val32 >>= 28;

	ret_val = bcmtch_com_write_spm(
		bcmtch_data_ptr,
		BCMTCH_SPM_REG_LPLFO_CTRL,
		(uint8_t)val32 | 0x10);
	if (ret_val)
		dev_err(bcmtch_data_ptr->p_device,
			"%s: [%d] write spm error!\n",
			__func__,
			ret_val);

	return ret_val;
}

static int bcmtch_dev_init_memory(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	if (!bcmtch_data_ptr->boot_from_rom)
		ret_val =
			bcmtch_com_write_sys32(
				bcmtch_data_ptr,
				BCMTCH_MEM_REMAP_ADDR,
				BCMTCH_MEM_RAM_BOOT);

	if (ret_val)
		dev_err(bcmtch_data_ptr->p_device,
			"%s: [%d] F/W memory remap error!\n",
			__func__,
			ret_val);

	return ret_val;
}

static inline void
tofe_channel_header_init(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_channel *p_channel,
		struct tofe_channel_instance_cfg *p_chan_cfg)
{
	struct tofe_channel_header *hdr = &p_channel->hdr;
	struct tofe_channel_buffer_header *buff;
	hdr->entry_num = p_chan_cfg->entry_num;
	hdr->entry_size = p_chan_cfg->entry_size;
	hdr->trig_level = p_chan_cfg->trig_level;
	hdr->flags = p_chan_cfg->flags;
	hdr->buffer_num = p_chan_cfg->buffer_num;
	hdr->buffer_idx = 0;
	hdr->seq_count = 0;
	hdr->buffer[0] = (struct tofe_channel_buffer *)&p_channel->data;
	hdr->buffer[1] = (struct tofe_channel_buffer *)&p_channel->data;
	if (hdr->flags & TOFE_CHANNEL_FLAG_FWDMA_ENABLE) {
		bcmtch_data_ptr->has_dma_channel = true;
		buff = &hdr->buffer[1]->header;
		buff->channel_id = hdr->channel_id;
		buff->entry_count = 0;
		buff->entry_size = hdr->entry_size;
		bcmtch_data_ptr->fw_dma_buffer_size +=
			sizeof(struct tofe_channel_buffer_header)
			+ (hdr->entry_num * hdr->entry_size);
	}
}

static int bcmtch_dev_init_channel(
		struct bcmtch_data *bcmtch_data_ptr,
		enum bcmtch_channel_id chan_id,
		struct tofe_channel_instance_cfg *p_chan_cfg,
		uint8_t active)
{
	int ret_val = 0;
	uint32_t channel_size;
	struct bcmtch_channel *p_channel;

	if (active) {
		channel_size =
				/* channel data size   */
				sizeof(struct tofe_channel_buffer_header)
				+ (p_chan_cfg->flags &
					TOFE_CHANNEL_FLAG_FWDMA_ENABLE ? 0 :
					p_chan_cfg->entry_num
					* p_chan_cfg->entry_size)
				/* channel header size */
				+ sizeof(struct tofe_channel_header)
				/* channel config size */
				+ sizeof(struct tofe_channel_instance_cfg)
				/* sizes for added elements: queued, pad */
				+ (sizeof(uint16_t) * 2);
	} else {
		channel_size =
				/* channel header size */
				sizeof(struct tofe_channel_header)
				/* channel config size */
				+ sizeof(struct tofe_channel_instance_cfg)
				/* sizes for added elements: queued, pad */
				+ (sizeof(uint16_t) * 2);
	}

	p_channel = vzalloc(channel_size);

	if (p_channel) {
		p_channel->cfg = *p_chan_cfg;

		/* Initialize Header */
		p_channel->hdr.channel_id = (uint8_t)chan_id;
		p_channel->active = active;
		tofe_channel_header_init(bcmtch_data_ptr,
			p_channel, p_chan_cfg);

		bcmtch_data_ptr->p_channels[chan_id] = p_channel;
	} else if (active)
		ret_val = -ENOMEM;

	return ret_val;
}

static void bcmtch_dev_free_channels(
	struct bcmtch_data *bcmtch_data_ptr)
{
	uint32_t chan = 0;

	vfree(bcmtch_data_ptr->fw_dma_buffer);
	bcmtch_data_ptr->fw_dma_buffer = NULL;

	while (chan < BCMTCH_CHANNEL_MAX) {
		vfree(bcmtch_data_ptr->
			p_channels[chan]);
		bcmtch_data_ptr->
			p_channels[chan] = NULL;
		chan++;
	}
}

static int bcmtch_dev_init_channels(
		struct bcmtch_data *bcmtch_data_ptr,
		uint32_t mem_addr,
		uint8_t *mem_data)
{
	int ret_val = 0;
	void *p_buffer = NULL;
	uint32_t *p_cfg = NULL;
	struct tofe_channel_instance_cfg *p_chan_cfg = NULL;

	/* find channel configs */
	p_cfg = (uint32_t *)(mem_data + TOFE_SIGNATURE_SIZE);
	p_chan_cfg =
		(struct tofe_channel_instance_cfg *)
		((uint32_t)mem_data + p_cfg[TOFE_TOC_INDEX_CHANNEL] - mem_addr);

	/* check if processing channel(s) - add */
	ret_val = bcmtch_dev_init_channel(
			bcmtch_data_ptr,
			BCMTCH_CHANNEL_TOUCH,
			&p_chan_cfg[TOFE_CHANNEL_ID_TOUCH],
			bcmtch_channel_flag & BCMTCH_CHANNEL_FLAG_USE_TOUCH);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
			"%s: [%d] Touch Event Channel not initialized!\n",
			__func__, ret_val);
	}

	/* Command & response channels */
	if (!ret_val) {
		ret_val = bcmtch_dev_init_channel(
				bcmtch_data_ptr,
				BCMTCH_CHANNEL_COMMAND,
				&p_chan_cfg[TOFE_CHANNEL_ID_COMMAND],
				bcmtch_channel_flag &
				BCMTCH_CHANNEL_FLAG_USE_CMD_RESP);
		ret_val |= bcmtch_dev_init_channel(
				bcmtch_data_ptr,
				BCMTCH_CHANNEL_RESPONSE,
				&p_chan_cfg[TOFE_CHANNEL_ID_RESPONSE],
				bcmtch_channel_flag &
				BCMTCH_CHANNEL_FLAG_USE_CMD_RESP);
		if (ret_val)
			dev_err(bcmtch_data_ptr->p_device,
				"%s: [%d] C/R Channel initialization failed!\n",
				__func__, ret_val);
	}

	/* Initialize DMA buffer if there is any DMA mode channel */
	if (!ret_val &&
			bcmtch_data_ptr->has_dma_channel) {
		dev_dbg(bcmtch_data_ptr->p_device,
			"CH:[%x]:%s() dma buffer size=%d\n",
			BCMTCH_DF_CH,
			__func__,
			bcmtch_data_ptr->fw_dma_buffer_size);
		p_buffer =
			vzalloc(bcmtch_data_ptr->fw_dma_buffer_size);
		if (p_buffer) {
			bcmtch_data_ptr->fw_dma_buffer = p_buffer;
		} else {
			dev_err(bcmtch_data_ptr->p_device,
				"%s: [%d] DMA buffer allocation failed!\n",
				__func__, ret_val);
			bcmtch_data_ptr->fw_dma_buffer = NULL;
			ret_val = -ENOMEM;
		}
	}

	return ret_val;
}

static int bcmtch_dev_write_channel(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_channel *chan)
{
	int ret_val = 0;
	int16_t write_size;
	uint32_t sys_addr;

	/* read channel header and data all-at-once : need combined size */
	write_size = sizeof(struct tofe_channel_buffer_header)
			+ (chan->cfg.entry_num * chan->cfg.entry_size);

	sys_addr = (uint32_t)chan->cfg.channel_data;

	/* write channel header & channel data buffer */
	ret_val = bcmtch_com_write_sys(
				bcmtch_data_ptr,
				sys_addr,
				write_size,
				(uint8_t *)chan->hdr.buffer[0]);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"BCMTOUCH: %s() write_sys err addr=0x%08x, rv=%d\n",
				__func__,
				sys_addr,
				ret_val);
	}
	return ret_val;
}

static int bcmtch_dev_sync_channel(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_channel *chan)
{
	int ret_val = 0;
	uint16_t read_size;
	uint32_t sys_addr;
	struct tofe_channel_header sync_hdr;

	/* Read channel header from firmware */
	read_size = sizeof(struct tofe_channel_header);
	sys_addr = (uint32_t)chan->cfg.channel_header;
	ret_val = bcmtch_com_read_sys(
				bcmtch_data_ptr,
				(uint32_t)chan->cfg.channel_header,
				read_size,
				(uint8_t *)&sync_hdr);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"BCMTOUCH: %s() read hdr err addr=0x%08x, rv=%d\n",
				__func__,
				sys_addr,
				ret_val);
		return ret_val;
	}


	/* Read channel */
	read_size = sizeof(struct tofe_channel_buffer_header)
			+ (chan->cfg.entry_num * chan->cfg.entry_size);
	sys_addr = (uint32_t)(sync_hdr.buffer_idx > 0 ?
				(char *)chan->cfg.channel_data :
				(char *)chan->cfg.channel_data
					+ chan->cfg.offset);
	ret_val = bcmtch_com_read_sys(
				bcmtch_data_ptr,
				sys_addr,
				read_size,
				(uint8_t *)chan->hdr.buffer[0]);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
			"BCMTOUCH: %s() read buffer err addr=0x%08x, rv=%d\n",
			__func__,
			sys_addr,
			ret_val);
		return ret_val;
	}
	chan->queued = bcmtch_channel_num_queued(&chan->hdr);

	/* Sync the channel header */
	chan->hdr.buffer_idx = sync_hdr.buffer_idx;
	chan->hdr.seq_count = sync_hdr.seq_count;

	return ret_val;
}

static int bcmtch_dev_read_channel(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_channel *chan)
{
	int ret_val = 0;
	uint8_t buffer_idx = chan->hdr.buffer_idx;
	uint8_t seq_count = chan->hdr.seq_count;
	struct tofe_channel_buffer_header *buff =
				&chan->hdr.buffer[1]->header;
	uint16_t read_size;
	uint32_t sys_addr;

	/* channel buffer size: buffer header + entries */
	read_size = sizeof(struct tofe_channel_buffer_header)
			+ (chan->cfg.entry_num * chan->cfg.entry_size);

	sys_addr = (uint32_t)(buffer_idx == 0 ?
				(char *)chan->cfg.channel_data :
				(char *)chan->cfg.channel_data
					+ chan->cfg.offset);

	/* read channel header & channel data buffer */
	ret_val = bcmtch_com_read_sys(
				bcmtch_data_ptr,
				sys_addr,
				read_size,
				(uint8_t *)chan->hdr.buffer[1]);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"BCMTOUCH: %s() read_sys err addr=0x%08x, rv=%d\n",
				__func__,
				sys_addr,
				ret_val);
		return ret_val;
	}

	/* check if data corrupted */
	if (!bcmtch_dev_verify_buffer_header(bcmtch_data_ptr,
			buff)) {
		dev_dbg(bcmtch_data_ptr->p_device,
			"CH:[%x]:%s() ch=%d buffer data corrupted!\n",
			BCMTCH_DF_CH,
			__func__,
			chan->hdr.channel_id);
		return -EIO;
	}

	if (buff->flags & TOFE_CHANNEL_FLAG_STATUS_OVERFLOW)
		dev_dbg(bcmtch_data_ptr->p_device,
			"CH:[%x]:%s() ch=%d channel overflow\n",
			BCMTCH_DF_CH,
			__func__,
			chan->hdr.channel_id);

	if (buff->seq_number != seq_count ||
		buff->entry_count == 0) {
		dev_dbg(bcmtch_data_ptr->p_device,
			"CH:[%x]:%s() ch=%d drv seq=%d, fw seq=%d : channel sync!!\n",
			BCMTCH_DF_CH,
			__func__,
			buff->channel_id,
			seq_count,
			buff->seq_number);

		/* sync channel */
		ret_val =
			bcmtch_dev_sync_channel(bcmtch_data_ptr, chan);

		return ret_val;
	}

	/* Update channel header */
	chan->hdr.seq_count++;
	chan->hdr.buffer_idx = (buffer_idx > 0 ? 0 : 1);

	/* get count */
	chan->queued = bcmtch_channel_num_queued(&chan->hdr);

	return ret_val;
}

static unsigned int bcmtch_dev_read_dma_buffer(
	struct bcmtch_data *bcmtch_data_ptr)
{
	unsigned int read_size = 0;
	unsigned int dma_buff_size = bcmtch_data_ptr->fw_dma_buffer_size;
	uint8_t dma_reg = BCMTCH_SPM_REG_DMA_RFIFO;
	uint8_t *dma_buff = (uint8_t *)bcmtch_data_ptr->fw_dma_buffer;
	struct i2c_client *p_i2c = bcmtch_data_ptr->p_i2c_client_sys;
	struct tofe_dmac_header *p_dmac;

	/* setup I2C messages for DMA read request transaction */
	struct i2c_msg dma_request[2] = {
		/* write the RFIFO address */
		{.addr = p_i2c->addr,
			.flags = 0,
			.len = 1,
			.buf = &dma_reg},
		/* read RFIFO data */
		{.addr = p_i2c->addr,
			.flags = I2C_M_RD,
			.len = (uint32_t)sizeof(struct tofe_dmac_header),
			.buf = dma_buff}
	};

	/* Set I2C master to read from RFIFO */
	if (dma_buff_size && dma_buff) {
		/* 1st I2C read dmac header */
		if (bcmtch_i2c_transfer(p_i2c->adapter, dma_request, 2) != 2) {
			dev_err(bcmtch_data_ptr->p_device,
					"%s: I2C transfer error.\n",
					__func__);
			return 0;
		} else {
			p_dmac = (struct tofe_dmac_header *)dma_buff;
			read_size = (uint32_t)p_dmac->size;
			dev_dbg(bcmtch_data_ptr->p_device,
				"CH:[%x]:DMA buffer read size=%d min_size=%d.\n",
				BCMTCH_DF_CH,
				read_size,
				p_dmac->min_size);

			if (read_size > dma_buff_size) {
				dev_err(bcmtch_data_ptr->p_device,
					"%s: DMA read overflow buffer [%d].\n",
					__func__,
					dma_buff_size);
				return 0;
			} else if (read_size <
					sizeof(struct tofe_dmac_header))
				return 0;

			/* 2nd I2C read entire DMA buffer */
			dma_request[1].len =
				read_size
				- sizeof(struct tofe_dmac_header);
			dma_request[1].buf =
				(uint8_t *)dma_buff
				+ sizeof(struct tofe_dmac_header);
			if (bcmtch_i2c_transfer(p_i2c->adapter, dma_request, 2) != 2) {
				dev_err(bcmtch_data_ptr->p_device,
						"%s: I2C transfer error.\n",
						__func__);
				return 0;
			}
		}
	} else {
		dev_err(bcmtch_data_ptr->p_device,
			"%s: DMA buffer/size is NULL.\n",
			__func__);
	}
	return read_size;
}

static inline bool bcmtch_dev_verify_buffer_header(
		struct bcmtch_data *bcmtch_data_ptr,
		struct tofe_channel_buffer_header *buff)
{
	uint8_t channel;
	struct bcmtch_channel *p_chan;
	bool ret_val = true;

	p_chan = NULL;
	channel = (uint8_t)buff->channel_id;
	if (channel >= BCMTCH_CHANNEL_MAX)
		ret_val = false;
	else {
		p_chan = bcmtch_data_ptr->p_channels[channel];
		if (!p_chan ||
			(buff->entry_size != p_chan->cfg.entry_size))
			ret_val = false;
	}

	if (ret_val == false)
		dev_dbg(bcmtch_data_ptr->p_device,
			"CH:[%x]:ERROR:id=%d entry_size=%d [%d]\n",
			BCMTCH_DF_CH,
			buff->channel_id,
			buff->entry_size,
			(p_chan) ? p_chan->cfg.entry_size : -1);

	return ret_val;
}

static int bcmtch_dev_read_dma_channels(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	uint32_t read_size;
	uint32_t read_head;
	uint32_t offset;
	uint32_t channel;

	uint8_t *p_dma = (uint8_t *)bcmtch_data_ptr->fw_dma_buffer;
	struct tofe_channel_buffer_header *buff;
	struct tofe_channel_header *hdr;

	/* Read DMA buffer via I2C */
	read_size = bcmtch_dev_read_dma_buffer(bcmtch_data_ptr);
	dev_dbg(bcmtch_data_ptr->p_device,
		"CH:[%x]:%s: read DMA buffer %d bytes.\n",
		BCMTCH_DF_CH,
		__func__,
		read_size);

	if (read_size > bcmtch_data_ptr->fw_dma_buffer_size) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s: Invalid DMA data read size %d.\n",
				__func__,
				read_size);
		return -EIO;
	}

	/* Parse DMA buffer for channels */
	read_head = 0;
	while (read_head < read_size) {
		buff = (struct tofe_channel_buffer_header *)p_dma;
		if (!bcmtch_dev_verify_buffer_header(bcmtch_data_ptr,
				buff)) {
			dev_err(bcmtch_data_ptr->p_device,
				"%s: corrupted buffer header in DMA channel!\n",
				__func__);
			return -EIO;
		}

		channel = buff->channel_id;
		dev_dbg(bcmtch_data_ptr->p_device,
			"CH:[%x]:%s: parsing channel [%d] min_size=%d\n",
			BCMTCH_DF_CH,
			__func__,
			channel,
			buff->dmac.min_size);

		hdr = &bcmtch_data_ptr->p_channels[channel]->hdr;
		if (hdr->flags & TOFE_CHANNEL_FLAG_FWDMA_ENABLE)
			hdr->buffer[0] = (struct tofe_channel_buffer *)p_dma;

		offset = (uint32_t)(buff->dmac.min_size ? buff->dmac.min_size :
				(buff->entry_size * buff->entry_count)
				+ sizeof(struct tofe_channel_buffer_header));
		read_head += offset;
		p_dma += offset;
	}

	return ret_val;
}

static int bcmtch_dev_parse_response_data(
			struct bcmtch_data *bcmtch_data_ptr,
			uint8_t *data,
			uint16_t data_size)
{
	int ret_val = 0;
	uint32_t slot_size =
		(uint32_t)
		bcmtch_data_ptr->scan_data.touch_slot_size;
	uint32_t slot_num =
		(uint32_t)
		bcmtch_data_ptr->scan_data.status.slots;

	/* Touch slot format - 0: long slot, 1: short slot */
	uint8_t	slot_format =
				bcmtch_data_ptr->scan_data.cmd
					.touch_slot_format;
	struct input_dev *input_dev_ptr =
				bcmtch_data_ptr->p_input_device;
	uint32_t button_index = 0;
	uint8_t *data_ptr = data;
	uint8_t slot_type;
	uint16_t tmp_axis;
	uint16_t button_check;
	uint16_t evt_status;
	uint16_t btn_status;
	uint32_t touch_index;
	union bcmtch_state_resp_slot *slot;
	struct bcmtch_touch	*touch_ptr;

	int32_t ao_flag =
			bcmtch_data_ptr->platform_data.axis_orientation_flag;

	for (touch_index = 0;
			touch_index < slot_num;
			data_ptr += slot_size,
			touch_index++) {
		slot = (union bcmtch_state_resp_slot *) data_ptr;
		touch_ptr = &bcmtch_data_ptr->touch[touch_index];
		dev_dbg(bcmtch_data_ptr->p_device,
			"ST:[%x]: SLOT %u: %02x %02x %02x %02x\n",
			BCMTCH_DF_ST,
			touch_index,
			data_ptr[0],
			data_ptr[1],
			data_ptr[2],
			data_ptr[3]);

		/* Categorize slot type */
		if (slot->slot_header.type > 7) {
			slot_type = STATE_SLOT_TYPE_FINGER;
			touch_ptr->type = MT_TOOL_FINGER;
		} else if (slot->slot_header.type > 1) {
			slot_type = STATE_SLOT_TYPE_STYLUS;
			touch_ptr->type = MT_TOOL_PEN;
		} else if (slot->slot_header.type == 1)
			slot_type = STATE_SLOT_TYPE_BUTTON;
		else
			slot_type = STATE_SLOT_TYPE_EMPTY;

		/* Process slots */
		switch (slot_type) {
		case STATE_SLOT_TYPE_EMPTY:
			touch_ptr->status = BCMTCH_TOUCH_STATUS_UP;
			dev_dbg(bcmtch_data_ptr->p_device,
				"ST:[%x]: SLOT %u: EMPTY\n",
				BCMTCH_DF_ST,
				touch_index);
			break;
		case STATE_SLOT_TYPE_BUTTON:
			btn_status = bcmtch_data_ptr->button_status;
			evt_status =
				(uint16_t)
				(slot->button_slot.buttons
					& BCMTCH_BTN_STATUS_MASK);
			if (btn_status != evt_status) {
				dev_dbg(bcmtch_data_ptr->p_device,
					"ST:[%x]:BTN: SLOT %u: 0x%0x\n",
					BCMTCH_DF_ST,
					touch_index,
					evt_status);

				while (button_index < bcmtch_data_ptr->
					  platform_data.ext_button_count) {
					button_check = (0x1 << button_index);
					if ((btn_status & button_check) !=
						(evt_status & button_check)) {
						input_report_key(
							input_dev_ptr,
							bcmtch_data_ptr->
							platform_data.
							ext_button_map
							[button_index],
							(evt_status &
							button_check));
					}
					button_index++;
				}

				/* Update status */
				bcmtch_data_ptr->button_status = evt_status;
			}
			touch_ptr->status = BCMTCH_TOUCH_STATUS_UP;
			break;
		case STATE_SLOT_TYPE_STYLUS:
			/* Not support yet */
		case STATE_SLOT_TYPE_FINGER:
			if (slot_format) {
				/* short slot */
				touch_ptr->x = slot->short_slot.x;
				touch_ptr->y = slot->short_slot.y;
				touch_ptr->pressure = 0;
				touch_ptr->orientation = 0;
				touch_ptr->major_axis = 0;
				touch_ptr->minor_axis = 0;
			} else {
				/* long slot */
				touch_ptr->x = slot->long_slot.x;
				touch_ptr->y = slot->long_slot.y;
				touch_ptr->pressure = slot->long_slot.pressure;
				touch_ptr->orientation =
					slot->long_slot.orientation;
				touch_ptr->major_axis =
					slot->long_slot.major
					<< BCMTCH_AXIS_SHIFT_BITS;
				touch_ptr->minor_axis =
					slot->long_slot.minor
					<< BCMTCH_AXIS_SHIFT_BITS;
			}

			/* axis reverse adjust */
			if (ao_flag & BCMTCH_AXIS_FLAG_X_REVERSED_MASK)
				touch_ptr->x =
					bcmtch_data_ptr->axis_x_max
					- touch_ptr->x;

			if (ao_flag & BCMTCH_AXIS_FLAG_Y_REVERSED_MASK)
				touch_ptr->y =
					bcmtch_data_ptr->axis_y_max
					- touch_ptr->y;

			if (ao_flag & BCMTCH_AXIS_FLAG_X_Y_SWAPPED_MASK) {
				tmp_axis = touch_ptr->x;
				touch_ptr->x = touch_ptr->y;
				touch_ptr->y = tmp_axis;
			}

			touch_ptr->status = BCMTCH_TOUCH_STATUS_MOVING;

			dev_dbg(bcmtch_data_ptr->p_device,
				"ST:[%x]:SLOT %d: X=%d Y=%d P=%d O=%d MAJX=%d MINX=%d T=%d\n",
				BCMTCH_DF_ST,
				touch_index,
				touch_ptr->x,
				touch_ptr->y,
				touch_ptr->pressure,
				touch_ptr->orientation,
				touch_ptr->major_axis,
				touch_ptr->minor_axis,
				touch_ptr->type);
			break;
		default:
			dev_err(bcmtch_data_ptr->p_device,
					"ST: SLOT %d: Unknown type %d",
					touch_index, slot_type);
		}
	}

	/* Report touch event to kernel */
	ret_val = bcmtch_dev_sync_event_frame(bcmtch_data_ptr);

	return ret_val;
}

static int bcmtch_dev_read_scan_touches(
			struct bcmtch_data *bcmtch_data_ptr,
			uint8_t slots)
{
	int ret_val = 0;
	uint16_t data_size;
	uint8_t *dma_buff = bcmtch_data_ptr->bcmtch_state_resp_buffer;

	if (!slots)
		return ret_val;

	data_size = slots * bcmtch_data_ptr->scan_data.touch_slot_size;
	if (data_size > TOFE_HOST_RSP_BUF_SIZE) {
		dev_err(bcmtch_data_ptr->p_device,
			"ST: data size to read exceed response buffer.");
		dev_err(bcmtch_data_ptr->p_device,
			"ST:	slots=%d data_size=%d resp_buf_size=%d",
			slots,
			data_size,
			TOFE_HOST_RSP_BUF_SIZE);
		return -EINVAL;
	}

	memset(dma_buff, 0, data_size);
	dev_dbg(bcmtch_data_ptr->p_device,
		"ST:[%x]:%s read %d touch slots size=%d\n",
		BCMTCH_DF_ST,
		__func__,
		slots,
		data_size);

	ret_val =
		bcmtch_com_read_dma(
			bcmtch_data_ptr,
			data_size + 1,
			 dma_buff);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
			"%s: DMA read error.\n", __func__);
		 return ret_val;
	}

    /* parse touch data: Slot 0 ...  Slot N-1 */
	bcmtch_dev_parse_response_data(
		bcmtch_data_ptr,
		&dma_buff[1],
		data_size);

	return ret_val;
}

static int bcmtch_dev_read_channels(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint32_t channel = 0;

	while (channel < BCMTCH_CHANNEL_MAX) {
		if (bcmtch_data_ptr->p_channels[channel]->active &&
			!(bcmtch_data_ptr->
				p_channels[channel]->cfg.flags &
				(TOFE_CHANNEL_FLAG_INBOUND
				 | TOFE_CHANNEL_FLAG_FWDMA_ENABLE))) {
			ret_val = bcmtch_dev_read_channel(
				bcmtch_data_ptr,
				bcmtch_data_ptr->p_channels[channel]);
		}
		channel++;
	}

	return ret_val;
}

static int bcmtch_dev_process_event_frame(
	struct bcmtch_data *bcmtch_data_ptr,
	struct bcmtch_event_frame *p_frame_event)
{
	int ret_val = 0;

	dev_dbg(bcmtch_data_ptr->p_device,
		"FR:[%x]:T=%d ID=%d TS=%d\n",
		BCMTCH_DF_FR,
		bcmtch_data_ptr->touch_count,
		p_frame_event->frame_id,
		p_frame_event->timestamp);

	return ret_val;
}

static int bcmtch_dev_process_event_frame_extension(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_event_frame_extension
		*p_frame_event_extension)
{
	int ret_val = 0;
	struct bcmtch_event_frame_extension_timestamp *timestamp;
	struct bcmtch_event_frame_extension_checksum  *checksum;

	switch (p_frame_event_extension->frame_kind) {
	case BCMTCH_EVENT_FRAME_EXTENSION_KIND_TIMESTAMP:
		timestamp = (struct bcmtch_event_frame_extension_timestamp *)
			p_frame_event_extension;

		dev_dbg(bcmtch_data_ptr->p_device,
			"FE:[%x]:Time offsets. %d %d %d",
			BCMTCH_DF_FE,
			timestamp->scan_end,
			timestamp->mtc_start,
			timestamp->mtc_end);
		break;
	case BCMTCH_EVENT_FRAME_EXTENSION_KIND_CHECKSUM:
		checksum =
			(struct bcmtch_event_frame_extension_checksum *)
			p_frame_event_extension;

		dev_dbg(bcmtch_data_ptr->p_device,
			"FE:[%x]:ERROR: Checksum not supported.  %#x",
			BCMTCH_DF_FE,
			checksum->hash);
		break;
	case BCMTCH_EVENT_FRAME_EXTENSION_KIND_HEARTBEAT:
		dev_dbg(bcmtch_data_ptr->p_device,
			"FE:[%x]:ERROR: Heartbeat not supported.",
			BCMTCH_DF_FE);
		break;
	default:
		dev_err(bcmtch_data_ptr->p_device,
			"FE:[%x]:Invalid frame extension. %d",
			BCMTCH_DF_FE,
			p_frame_event_extension->frame_kind);
		break;
	}

	return ret_val;
}


static int bcmtch_dev_sync_event_frame(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	struct input_dev *input_dev_ptr =
		bcmtch_data_ptr->p_input_device;
	struct bcmtch_touch *touch_ptr;
	uint32_t num_touches = 0;
	uint32_t touch_index = 0;

	for (touch_index = 0; touch_index < ARRAY_SIZE(bcmtch_data_ptr->touch);
			touch_index++) {
		touch_ptr =
			(struct bcmtch_touch *)
			&bcmtch_data_ptr->touch[touch_index];

		input_mt_slot(input_dev_ptr, touch_index);
		input_mt_report_slot_state(
			input_dev_ptr,
			touch_ptr->type,
			(touch_ptr->status > BCMTCH_TOUCH_STATUS_UP));

		if (touch_ptr->status > BCMTCH_TOUCH_STATUS_UP) {
			/* Count both of STATUS_MOVE and STATUS_MOVING */
			num_touches++;

			if (touch_ptr->status > BCMTCH_TOUCH_STATUS_MOVE) {

				input_report_abs(
						input_dev_ptr,
						ABS_MT_POSITION_X,
						touch_ptr->x);

				input_report_abs(
						input_dev_ptr,
						ABS_MT_POSITION_Y,
						touch_ptr->y);

				if (bcmtch_event_flag &
						BCMTCH_EVENT_FLAG_PRESSURE) {
					input_report_abs(
							input_dev_ptr,
							ABS_MT_PRESSURE,
							touch_ptr->pressure);
				}

				if (bcmtch_event_flag &
						BCMTCH_EVENT_FLAG_TOUCH_SIZE) {
					input_report_abs(
							input_dev_ptr,
							ABS_MT_TOUCH_MAJOR,
							touch_ptr->major_axis);

					input_report_abs(
							input_dev_ptr,
							ABS_MT_TOUCH_MINOR,
							touch_ptr->minor_axis);
				}

				if (bcmtch_event_flag &
						BCMTCH_EVENT_FLAG_ORIENTATION) {
					input_report_abs(
							input_dev_ptr,
							ABS_MT_ORIENTATION,
							touch_ptr->orientation);
				}

				if (bcmtch_data_ptr->state_protocol)
					/* reset the status to UP. */
					touch_ptr->status =
						BCMTCH_TOUCH_STATUS_UP;
				else
					/* reset the status to MOVE. */
					touch_ptr->status =
						BCMTCH_TOUCH_STATUS_MOVE;
			}
		}
	}

	input_report_key(input_dev_ptr, BTN_TOUCH, (num_touches > 0));
	input_sync(input_dev_ptr);

	/* remember */
	bcmtch_data_ptr->touch_count = num_touches;

	return ret_val;
}

static void
bcmtch_dev_process_event_touch_extension(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_event_touch_extension *extension,
		uint8_t track_id)
{
	char *tool_str;
	struct bcmtch_event_touch_extension_detail *detail;
	struct bcmtch_event_touch_extension_blob *blob;
	struct bcmtch_event_touch_extension_size *size;
	struct bcmtch_event_touch_extension_tool *tool;
	struct bcmtch_touch *touch_ptr =
		(struct bcmtch_touch *)&bcmtch_data_ptr->touch[track_id];

	switch (extension->touch_kind) {
	case BCMTCH_EVENT_TOUCH_EXTENSION_KIND_DETAIL:
		detail =
			(struct bcmtch_event_touch_extension_detail *)
			extension;

		/* logic is inverted */
		if (detail->tool == BCMTCH_EVENT_TOUCH_TOOL_FINGER) {
			touch_ptr->type = MT_TOOL_PEN;
			tool_str = "stylus";
		} else {
			touch_ptr->type = MT_TOOL_FINGER;
			tool_str = "finger";
		}

		dev_dbg(bcmtch_data_ptr->p_device,
			"TE:[%x]:C%d:S%d:H%d %s(%d) Pres=%d Ornt=%#x",
			BCMTCH_DF_TE,
			detail->confident,
			detail->suppressed,
			detail->hover,
			tool_str,
			detail->tool,
			detail->pressure,
			detail->orientation);
		/**
		 * ABS_MT_TOOL_TYPE
		 * - MT_TOOL_FINGER
		 * - MT_TOOL_PEN
		 **/
		touch_ptr->pressure = detail->pressure;

		/* get orientation
		 * - handle int12 to int16 conversion
		 */
		touch_ptr->orientation = detail->orientation;
		if (touch_ptr->orientation & (1<<11))
			touch_ptr->orientation -= 1<<12;
		break;

	case BCMTCH_EVENT_TOUCH_EXTENSION_KIND_BLOB:
		blob =
			(struct bcmtch_event_touch_extension_blob *)
			extension;
		dev_dbg(bcmtch_data_ptr->p_device,
			"TE:[%x]:Area=%d TCap=%d",
			BCMTCH_DF_TE,
			blob->area, blob->total_cap);
		/**
		 * ABS_MT_BLOB_ID
		 */
		break;

	case BCMTCH_EVENT_TOUCH_EXTENSION_KIND_SIZE:
		size =
			(struct bcmtch_event_touch_extension_size *)
			extension;
		dev_dbg(bcmtch_data_ptr->p_device,
			"TE:[%x]:Track %d:\tMajor=%d Minor=%d",
			BCMTCH_DF_TE,
			track_id,
			size->major_axis,
			size->minor_axis);
		/**
		 * ABS_MT_MAJOR/MINOR_AXIS
		 */
		touch_ptr->major_axis =
			size->major_axis << BCMTCH_AXIS_SHIFT_BITS;
		touch_ptr->minor_axis =
			size->minor_axis << BCMTCH_AXIS_SHIFT_BITS;
		break;

	case BCMTCH_EVENT_TOUCH_EXTENSION_KIND_HOVER:
		dev_dbg(bcmtch_data_ptr->p_device,
			"TE:[%x]:ERROR:Hover not supported.",
			BCMTCH_DF_TE);
		break;

	case BCMTCH_EVENT_TOUCH_EXTENSION_KIND_TOOL:
		tool =
			(struct bcmtch_event_touch_extension_tool *)
			extension;
		dev_dbg(bcmtch_data_ptr->p_device,
			"TE:[%x]:Track %d:\tMajor=%d Minor=%d",
			BCMTCH_DF_TE,
			track_id,
			tool->width_major,
			tool->width_minor);
		/**
		 * ABS_MT_MAJOR/MINOR_AXIS
		 */
		touch_ptr->width_major = tool->width_major;
		touch_ptr->width_minor = tool->width_minor;
		break;
	default:
		dev_err(bcmtch_data_ptr->p_device,
			"TE:[%x]:Invalid touch extension. %d",
			BCMTCH_DF_TE,
			extension->touch_kind);
		break;
	}
}


static int bcmtch_dev_process_event_touch(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_event_touch *p_touch_event)
{
	int axis_orientation_flag;
	int ret_val = 0;
	struct bcmtch_touch *p_touch;
	enum bcmtch_event_kind kind;

	if (p_touch_event->track_tag < BCMTCH_MAX_TOUCH) {
		axis_orientation_flag =
			bcmtch_data_ptr->platform_data.axis_orientation_flag;
		p_touch =
			(struct bcmtch_touch *)
			&bcmtch_data_ptr->touch[p_touch_event->track_tag];

		if (axis_orientation_flag & BCMTCH_AXIS_FLAG_X_REVERSED_MASK)
			p_touch_event->x =
				bcmtch_data_ptr->axis_x_max - p_touch_event->x;

		if (axis_orientation_flag & BCMTCH_AXIS_FLAG_Y_REVERSED_MASK)
			p_touch_event->y =
				bcmtch_data_ptr->axis_y_max - p_touch_event->y;

		if (axis_orientation_flag & BCMTCH_AXIS_FLAG_X_Y_SWAPPED_MASK) {
			p_touch->y = p_touch_event->x;
			p_touch->x = p_touch_event->y;
		} else {
			p_touch->x  = p_touch_event->x;
			p_touch->y  = p_touch_event->y;
		}

		kind = (enum bcmtch_event_kind)p_touch_event->event_kind;

		switch (kind) {
		case BCMTCH_EVENT_KIND_TOUCH:
			p_touch->event = kind;
			p_touch->status = BCMTCH_TOUCH_STATUS_MOVING;

			dev_dbg(bcmtch_data_ptr->p_device,
				"MV:[%x]:T%d: (%04x , %04x)\n",
				BCMTCH_DF_MV,
				p_touch_event->track_tag,
				p_touch->x,
				p_touch->y);
			break;
		case BCMTCH_EVENT_KIND_TOUCH_END:
			p_touch->event = kind;
			p_touch->status = BCMTCH_TOUCH_STATUS_UP;

			dev_dbg(bcmtch_data_ptr->p_device,
				"UP:[%x]:T%d: (%04x , %04x)\n",
				BCMTCH_DF_UP,
				p_touch_event->track_tag,
				p_touch->x,
				p_touch->y);
			break;
		default:
			dev_err(bcmtch_data_ptr->p_device,
				"%s: Invalid touch event", __func__);
			break;
		}
	}

	return ret_val;
}

static int bcmtch_dev_process_event_button(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_event_button *p_button_event)
{
	int ret_val = 0;
	uint16_t evt_status = p_button_event->status;
	uint16_t btn_status = bcmtch_data_ptr->button_status;
	struct input_dev *input_dev_ptr =
		bcmtch_data_ptr->p_input_device;
	enum bcmtch_event_kind kind = p_button_event->button_kind;
	uint32_t button_index = 0;
	uint16_t button_check;

	if (btn_status != evt_status) {
		switch (kind) {
		case BCMTCH_EVENT_BUTTON_KIND_CONTACT:
			while (button_index < bcmtch_data_ptr->
				  platform_data.ext_button_count) {
				button_check = (0x1 << button_index);
				if ((btn_status & button_check) !=
						(evt_status & button_check)) {
					input_report_key(
						input_dev_ptr,
						bcmtch_data_ptr->platform_data.
						  ext_button_map[button_index],
						(evt_status & button_check));
				}
				button_index++;
			}

			dev_dbg(bcmtch_data_ptr->p_device,
				"BT:[%x]:%s %#04x\n", BCMTCH_DF_BT,
				"press",
				evt_status);
			break;
		case BCMTCH_EVENT_BUTTON_KIND_HOVER:
			dev_dbg(bcmtch_data_ptr->p_device,
				"BT:[%x]:%s %#04x\n", BCMTCH_DF_BT,
				"hover",
				evt_status);
			break;
		default:
			dev_err(bcmtch_data_ptr->p_device,
				"%s: Invalid button kind %d\n",
				__func__,
				kind);
			break;
		}

		/* Report SYNC */
		input_sync(input_dev_ptr);

		/* Update status */
		bcmtch_data_ptr->button_status = evt_status;
	} else {
		dev_dbg(bcmtch_data_ptr->p_device,
			"BT:[%x]:unchanged. status=0x%04x\n",
			BCMTCH_DF_BT,
			btn_status);
	}
	return ret_val;
}

/**
 * To process whole frames of data this variable should
 * be made global because one frame can be split across
 * two invocations of the function process_channel_touch().
 */
static	enum bcmtch_event_kind top_level_kind = BCMTCH_EVENT_KIND_EXTENSION;

static int bcmtch_dev_process_channel_touch(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_channel *chan)
{
	int ret_val = 0;
	bool syn_report_pending = false;
	uint16_t read_idx;
	struct bcmtch_event *ptch_event;
	enum bcmtch_event_kind kind;
	struct bcmtch_event_touch *ptouch_event = NULL;
	struct tofe_channel_header *chan_hdr =
		(struct tofe_channel_header *)&chan->hdr;

	uint32_t frames_in = 0;

	read_idx = 0;
	while ((ptch_event = (struct bcmtch_event *)
				bcmtch_inline_channel_read(chan_hdr,
				read_idx++))) {
		kind = (enum bcmtch_event_kind)ptch_event->event_kind;

		if (kind != BCMTCH_EVENT_KIND_EXTENSION) {
			top_level_kind = kind;

			if (syn_report_pending) {
				/**
				 * The end of frame extension events.
				 * Send the SYN_REPORT for the frame.
				 */
				bcmtch_dev_sync_event_frame(bcmtch_data_ptr);
				syn_report_pending = false;

				if (frames_in)
					usleep_range(1000, 1500);
			}
		}

		switch (kind) {
		case BCMTCH_EVENT_KIND_FRAME:
			/**
			 * Only set the flag to wait for the following frame extension events
			 * rather than directly send SYN_REPORT message.
			 */
			frames_in++;
			syn_report_pending = true;
		    bcmtch_dev_process_event_frame(
					bcmtch_data_ptr,
					(struct bcmtch_event_frame *)
					ptch_event);
			break;
		case BCMTCH_EVENT_KIND_TOUCH:
		case BCMTCH_EVENT_KIND_TOUCH_END:
			ptouch_event =
				(struct bcmtch_event_touch *)ptch_event;
			bcmtch_data_ptr->touch_event_track_id =
				ptouch_event->track_tag;
		    bcmtch_dev_process_event_touch(
				bcmtch_data_ptr,
				ptouch_event);
			break;
		case BCMTCH_EVENT_KIND_BUTTON:
			bcmtch_dev_process_event_button(
				bcmtch_data_ptr,
				(struct bcmtch_event_button *)ptch_event);
			break;
		case BCMTCH_EVENT_KIND_GESTURE:
			dev_err(bcmtch_data_ptr->p_device,
				"ERROR:gesture not supported\n");
			break;
		case BCMTCH_EVENT_KIND_EXTENSION:
			switch (top_level_kind) {
			case BCMTCH_EVENT_KIND_FRAME:
				bcmtch_dev_process_event_frame_extension(
					bcmtch_data_ptr,
					(struct bcmtch_event_frame_extension *)
					ptch_event);
				break;
			case BCMTCH_EVENT_KIND_TOUCH:
				bcmtch_dev_process_event_touch_extension(
					bcmtch_data_ptr,
					(struct bcmtch_event_touch_extension *)
					ptch_event,
					bcmtch_data_ptr->touch_event_track_id);
				break;
			default:
				dev_err(bcmtch_data_ptr->p_device,
					"ERROR:unknown event extension for: tlk=%d k=%d\n",
					top_level_kind, kind);
				break;
			}
			break;
		default:
			dev_err(bcmtch_data_ptr->p_device,
				"ERROR:unknown event kind: %d\n", kind);
		}
	}

    /**
     * The last event in the channel is a frame (extension) event.
     * Send the SYN_REPORT for the frame.
     */
	if (syn_report_pending) {
		bcmtch_dev_sync_event_frame(bcmtch_data_ptr);
		syn_report_pending = false;
	}

	dev_dbg(bcmtch_data_ptr->p_device,
		"FR:[%x]:%d",
		BCMTCH_DF_FR,
		frames_in);

	return ret_val;
}

static int bcmtch_dev_process_channel_response(
		struct bcmtch_data *bcmtch_data_ptr,
		struct bcmtch_channel *chan)
{
	int ret_val = 0;
	uint16_t read_idx;
	struct bcmtch_response_wait *p_resp;
	struct tofe_command_response *resp_event;
	struct tofe_channel_header *chan_hdr =
		(struct tofe_channel_header *)&chan->hdr;

	dev_dbg(bcmtch_data_ptr->p_device,
		"CH:[%x]:%s() - swap count=%d response evt count=%d.\n",
		BCMTCH_DF_CH,
		__func__,
		chan_hdr->seq_count,
		chan->queued);

	read_idx = 0;
	/* Process response events */
	while ((resp_event =
				(struct tofe_command_response *)
				bcmtch_inline_channel_read(chan_hdr,
				read_idx++))) {

		if (resp_event->flags &
				TOFE_COMMAND_FLAG_COMMAND_PROCESSED) {
			if (resp_event->command > TOFE_COMMAND_LAST)
				continue;

			/* Save the response result */
			p_resp = (struct bcmtch_response_wait *)
				&(bcmtch_data_ptr->bcmtch_cmd_response
						[resp_event->command]);
			p_resp->wait = 0;
			p_resp->resp_data = resp_event->data;
		}

		dev_dbg(bcmtch_data_ptr->p_device,
			"CH:[%x]:Response - command=0x%02x result=0x%04x data=0x%08x.\n",
			BCMTCH_DF_CH,
			resp_event->command,
			resp_event->result,
			resp_event->data);
	}
	return ret_val;
}

static int bcmtch_dev_process_channels(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint32_t channel = 0;
	struct bcmtch_channel *p_chan = NULL;

	while (channel < BCMTCH_CHANNEL_MAX) {
		p_chan = bcmtch_data_ptr->p_channels[channel];
		if (!p_chan->active) {
			channel++;
			continue;
		}

		switch (channel) {
		case BCMTCH_CHANNEL_TOUCH:
			bcmtch_dev_process_channel_touch(
				bcmtch_data_ptr,
				p_chan);
			break;

		case BCMTCH_CHANNEL_COMMAND:
			break;

		case BCMTCH_CHANNEL_RESPONSE:
			bcmtch_dev_process_channel_response(
				bcmtch_data_ptr,
				p_chan);
			break;

		default:
			break;
		}

		if (p_chan->cfg.flags & TOFE_CHANNEL_FLAG_FWDMA_ENABLE)
			p_chan->hdr.buffer[0] = p_chan->hdr.buffer[1];

		channel++;
	}

	return ret_val;
}

static int bcmtch_dev_wait_for_firmware_ready(
		struct bcmtch_data *bcmtch_data_ptr,
		int32_t count)
{
	int ret_val = 0;
	uint8_t ready;

	do {
		ret_val =
			bcmtch_com_read_spm(
				bcmtch_data_ptr,
				BCMTCH_SPM_REG_MSG_TO_HOST, &ready);
	} while ((!ret_val) && !(ready & TOFE_MESSAGE_FW_READY) && (count--));

	if (count <= 0) {
		dev_err(bcmtch_data_ptr->p_device,
			"ERROR: Failed to communicate with Napa FW. Error: 0x%x\n",
			ready);
		ret_val = -1;
	}

	return ret_val;
}

static int bcmtch_dev_run_firmware(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	ret_val = bcmtch_dev_reset(bcmtch_data_ptr,
		BCMTCH_RESET_MODE_SOFT_CLEAR);

	/* set DLDO output */
	if (!ret_val)
		ret_val = bcmtch_com_write_spm(
					bcmtch_data_ptr,
					BCMTCH_SPM_REG_PMU_CONTROL2,
					BCMTCH_PMU_CNTL2_DLDO_1_1V);

	if (bcmtch_dev_wait_for_firmware_ready(
			bcmtch_data_ptr,
			BCMTCH_FW_READY_WAIT)) {
		uint8_t xaddr = 0x40;
		uint8_t xdata;
		while (xaddr <= 0x61) {
			bcmtch_com_read_spm(bcmtch_data_ptr,
				xaddr, &xdata);
			dev_err(bcmtch_data_ptr->p_device,
				"%s: addr = 0x%02x  data = 0x%02x\n",
				__func__,
				xaddr++,
				xdata);
		}
	}

	return ret_val;
}

static void bcmtch_dev_dump_fw_header(
		struct bcmtch_data *bcmtch_data_ptr,
		struct combi_entry *p_entry,
		int num)
{
	int i;

	dev_info(bcmtch_data_ptr->p_device,
		"\toffset\t\taddr\t\tlen\tflags\n");
	for (i = 0; i < num; i++)
		dev_info(bcmtch_data_ptr->p_device,
			"\t0x%08x\t0x%08x\t%4u\t0x%08x\n",
			p_entry[i].offset,
			p_entry[i].addr,
			p_entry[i].length,
			p_entry[i].flags);
}

static int bcmtch_dev_parse_firmware(
				struct bcmtch_data *bcmtch_data_ptr,
				const struct combi_entry *p_entry)
{
	int ret_val = 0;
	uint32_t entry_id = 1;
	uint32_t *p_cfg = NULL;
	uint8_t *mem_data = NULL;

	struct tofe_signature *p_tofe_sig = NULL;
	struct mtc_detect_cfg *p_mtc_cfg = NULL;

	struct combi_entry *p_pre_cfg = NULL;
	struct combi_entry *p_pre_patch = NULL;
	struct combi_entry *p_post_cfg = NULL;
	struct combi_entry *p_post_code = NULL;
	struct combi_entry *p_post_patch = NULL;

	struct combi_entry *p_headers_tail =
				&bcmtch_data_ptr->fw_screened_headers[0];
	uint16_t otp_hw_id = (uint16_t)
		bcmtch_data_ptr->otp_hw_id & 0xffff;
	uint16_t hw_id;

	bcmtch_data_ptr->fw_headers[0] = p_entry[0];

	while (p_entry[entry_id].length) {

		bcmtch_data_ptr->fw_headers[entry_id] =
				p_entry[entry_id];

		hw_id = (uint16_t)(p_entry[entry_id].flags >> 16);

		switch (p_entry[entry_id].flags & BCMTCH_FIRMWARE_FLAGS_MASK) {
		case BCMTCH_FIRMWARE_FLAGS_POST_BOOT_CONFIGS:
			/* Conditionally clone header */
			if (p_post_cfg) {
				if (hw_id == otp_hw_id)
					*p_post_cfg = p_entry[entry_id];
				else
					break;
			} else {
				if (hw_id == 0 ||
						hw_id == otp_hw_id) {
					p_post_cfg = p_headers_tail;
					*p_headers_tail++ = p_entry[entry_id];
				} else
					break;
			}

			mem_data = (uint8_t *)
						((uint32_t)p_entry
						+ p_entry[entry_id].offset);
			p_cfg = (uint32_t *)(mem_data + TOFE_SIGNATURE_SIZE);
			p_tofe_sig = (struct tofe_signature *)mem_data;

			/* Parse F/W Signature */
			bcmtch_data_ptr->fw_signature = *p_tofe_sig;

			/* Parse MTC parameters */
			p_mtc_cfg =
				(struct mtc_detect_cfg *)
				((uint32_t)mem_data
					+ p_cfg[TOFE_TOC_INDEX_DETECT]
					- BCMTCH_ADDR_TOC_BASE);

			if (p_mtc_cfg) {
				bcmtch_data_ptr->axis_x_max =
					(p_mtc_cfg->scaling_x_range
						> BCMTCH_AXIS_MAX) ?
					BCMTCH_AXIS_MAX :
					p_mtc_cfg->scaling_x_range - 1;

				bcmtch_data_ptr->axis_y_max =
					(p_mtc_cfg->scaling_y_range
						> BCMTCH_AXIS_MAX) ?
					BCMTCH_AXIS_MAX :
					p_mtc_cfg->scaling_y_range - 1;

				bcmtch_data_ptr->axis_h_max =
					bcmtch_max_h_axis(
						bcmtch_data_ptr->axis_x_max,
						bcmtch_data_ptr->axis_y_max);

				bcmtch_data_ptr->threshold_gate_finger =
					p_mtc_cfg->class_finger_gate;
				bcmtch_data_ptr->threshold_gate_stylus =
					p_mtc_cfg->class_stylus_gate;
			}

			/* Check channel/state protocol mode */
			if (!p_cfg[TOFE_TOC_INDEX_CHANNEL])
				bcmtch_data_ptr->state_protocol = true;
			else
				bcmtch_data_ptr->state_protocol = false;
			break;

		case BCMTCH_FIRMWARE_FLAGS_POST_BOOT_PATCH:
			/* Conditionally clone header */
			if (p_post_patch) {
				if (hw_id == otp_hw_id)
					*p_post_patch = p_entry[entry_id];
			} else {
				if (hw_id == 0 ||
						hw_id == otp_hw_id) {
					p_post_patch = p_headers_tail;
					*p_headers_tail++ = p_entry[entry_id];
				}
			}
			break;

		case BCMTCH_FIRMWARE_FLAGS_POST_BOOT_CODE:
			/* Conditionally clone header */
			if (p_post_code) {
				if (hw_id == otp_hw_id)
					*p_post_code = p_entry[entry_id];
			} else {
				if (hw_id == 0 ||
						hw_id == otp_hw_id) {
					p_post_code = p_headers_tail;
					*p_headers_tail++ = p_entry[entry_id];
				}
			}
			break;

		case BCMTCH_FIRMWARE_FLAGS_CONFIGS:
			/* Conditionally clone header */
			if (p_pre_cfg) {
				if (hw_id == otp_hw_id)
					*p_pre_cfg = p_entry[entry_id];
				else
					break;
			} else {
				if (hw_id == 0 ||
						hw_id == otp_hw_id) {
					p_pre_cfg = p_headers_tail;
					*p_headers_tail++ = p_entry[entry_id];
				} else
					break;
			}

			mem_data = (uint8_t *)
						((uint32_t)p_entry
						+ p_entry[entry_id].offset);

			p_cfg = (uint32_t *)(mem_data + TOFE_SIGNATURE_SIZE);
			p_tofe_sig = (struct tofe_signature *)mem_data;

			/* Parse F/W Signature */
			bcmtch_data_ptr->fw_signature = *p_tofe_sig;

			/* Parse MTC parameters */
			p_mtc_cfg =
				(struct mtc_detect_cfg *)
				((uint32_t)mem_data
					+ p_cfg[TOFE_TOC_INDEX_DETECT]
					- p_entry[entry_id].addr);

			if (p_mtc_cfg) {
				bcmtch_data_ptr->axis_x_max =
					(p_mtc_cfg->scaling_x_range
						> BCMTCH_AXIS_MAX) ?
					BCMTCH_AXIS_MAX :
					p_mtc_cfg->scaling_x_range - 1;

				bcmtch_data_ptr->axis_y_max =
					(p_mtc_cfg->scaling_y_range
						> BCMTCH_AXIS_MAX) ?
					BCMTCH_AXIS_MAX :
					p_mtc_cfg->scaling_y_range - 1;

				bcmtch_data_ptr->axis_h_max =
					bcmtch_max_h_axis(
						bcmtch_data_ptr->axis_x_max,
						bcmtch_data_ptr->axis_y_max);

				bcmtch_data_ptr->threshold_gate_finger =
					p_mtc_cfg->class_finger_gate;
				bcmtch_data_ptr->threshold_gate_stylus =
					p_mtc_cfg->class_stylus_gate;
			}

			/* Check channel/state protocol mode */
			if (!p_cfg[TOFE_TOC_INDEX_CHANNEL])
				bcmtch_data_ptr->state_protocol = true;
			else
				bcmtch_data_ptr->state_protocol = false;
			break;

		case BCMTCH_FIRMWARE_FLAGS_CODE:
			/* Conditionally clone header */
			if (p_pre_patch) {
				if (hw_id == otp_hw_id)
					*p_pre_patch = p_entry[entry_id];
			} else {
				if (hw_id == 0 ||
						hw_id == otp_hw_id) {
					p_pre_patch = p_headers_tail;
					*p_headers_tail++ = p_entry[entry_id];
				}
			}
			break;

		default:
			dev_err(bcmtch_data_ptr->p_device,
				"ERROR:unknown firmware flag : %d 0x%x\n",
				entry_id,
				p_entry[entry_id].flags &
				BCMTCH_FIRMWARE_FLAGS_MASK);
			break;
		}

		/* next */
		entry_id++;
	}

	bcmtch_dev_dump_fw_header(
			bcmtch_data_ptr,
			(struct combi_entry *)&p_entry[1],
			entry_id);
	bcmtch_data_ptr->fw_entry_num = entry_id - 1;

	/* Check boot from ROM mode */
	for (entry_id = 0; entry_id <
			BCMTCH_FIRMWARE_MAX_ENTRIES; entry_id++)
		if (bcmtch_data_ptr->
				fw_screened_headers[entry_id].flags &
					BCMTCH_FIRMWARE_FLAGS_ROM_BOOT) {
			bcmtch_data_ptr->boot_from_rom = true;
			break;
		}

	return ret_val;
}

static int bcmtch_dev_download_firmware(
				struct bcmtch_data *bcmtch_data_ptr,
				uint8_t *fw_name,
				uint32_t fw_addr,
				uint32_t fw_flags)
{
	const struct firmware *p_fw;
	int ret_val = 0;

	uint32_t entry_id = 1;
	struct combi_entry *p_entry = NULL;
	struct combi_entry default_entry[] = {
		{.addr = fw_addr, .flags = fw_flags,},
		{0, 0, 0, 0},
	};

	/* request firmware binary from OS */
	ret_val = request_firmware(
				&p_fw, fw_name,
				&bcmtch_data_ptr->p_i2c_client_spm->dev);

	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
			"%s: Firmware request failed (%d) for %s\n",
			__func__,
			ret_val,
			fw_name);
	} else {
		dev_dbg(bcmtch_data_ptr->p_device,
			"INFO:[%x]:FIRMWARE: %s\n",
			BCMTCH_DF_INFO,
			fw_name);
		dev_dbg(bcmtch_data_ptr->p_device,
			"PB:[%x]:f/w size= 0x%x\n",
			BCMTCH_DF_PB,
			p_fw->size);

		/* pre-process binary according to flags */
		if (fw_flags & BCMTCH_FIRMWARE_FLAGS_COMBI) {
			p_entry = (struct combi_entry *) p_fw->data;
		} else {
			p_entry = default_entry;
			p_entry[entry_id].length = p_fw->size;
		}

		memset(bcmtch_data_ptr->fw_screened_headers,
				0,
				sizeof(struct combi_entry)
				* BCMTCH_FIRMWARE_MAX_ENTRIES);

		/* Parse firmware section flags and set mode */
		bcmtch_dev_parse_firmware(
			bcmtch_data_ptr,
			p_entry);
		p_entry = bcmtch_data_ptr->fw_screened_headers;
		bcmtch_dev_dump_fw_header(
				bcmtch_data_ptr,
				p_entry,
				BCMTCH_FIRMWARE_MAX_ENTRIES);

		/* init memory */
		ret_val = bcmtch_dev_init_memory(bcmtch_data_ptr);
		if (ret_val)
			goto download_error;

		/* Iterate through the screened entry table (reset id) */
		entry_id = 0;
		while (p_entry[entry_id].length && !ret_val) {
			switch (p_entry[entry_id].flags &
					BCMTCH_FIRMWARE_FLAGS_MASK) {
			case BCMTCH_FIRMWARE_FLAGS_POST_BOOT_CONFIGS:
				dev_dbg(bcmtch_data_ptr->p_device,
					"PB:[%x]:entry_id=%d PB CONFIG\n",
					BCMTCH_DF_PB,
					entry_id);
				dev_dbg(bcmtch_data_ptr->p_device,
					"PB:[%x]:pb chans init addr=0x%08x\n",
					BCMTCH_DF_PB,
					p_entry[entry_id].addr);

				bcmtch_data_ptr->post_boot_cfg_addr =
					p_entry[entry_id].addr;
				bcmtch_data_ptr->post_boot_cfg_length =
					p_entry[entry_id].length;

			/* Intentional fall thru */

			case BCMTCH_FIRMWARE_FLAGS_POST_BOOT_CODE:
				if (!bcmtch_data_ptr->post_boot_sections) {
					/* Allocate firmware buffer memory */
					if (bcmtch_data_ptr->
						post_boot_buffer == NULL)
						bcmtch_data_ptr->
							post_boot_buffer =
							vzalloc(p_fw->size);

					if (bcmtch_data_ptr->
						post_boot_buffer == NULL) {
						dev_err(
							bcmtch_data_ptr->
							p_device,
							"%s: failed to alloc firmware buffer.\n",
							__func__);
						ret_val = -ENOMEM;
						goto download_error;
					}

					memcpy(
						bcmtch_data_ptr->
						post_boot_buffer,
						(void *) p_fw->data,
						p_fw->size);
				}

				bcmtch_data_ptr->post_boot_sections++;

				if (p_entry[entry_id].flags &
					BCMTCH_FIRMWARE_FLAGS_POST_BOOT_PATCH)
					bcmtch_data_ptr->post_boot_patches++;

				dev_dbg(bcmtch_data_ptr->p_device,
					"PB:[%x]:entry_id=%d PB pb_sec=%d\n",
					BCMTCH_DF_PB,
					entry_id,
					bcmtch_data_ptr->
					post_boot_sections);
				break;

			case BCMTCH_FIRMWARE_FLAGS_CONFIGS:
				if (bcmtch_data_ptr->boot_from_rom ||
					!bcmtch_data_ptr->state_protocol) {
					/* Channel protocol */
					bcmtch_data_ptr->work_process_index =
						BCMTCH_WP_CHANNEL;
					/* Initialize channels */
					dev_dbg(bcmtch_data_ptr->p_device,
						"PB:[%x]:entry_id=%d CONFIG\n",
						BCMTCH_DF_PB,
						entry_id);

					dev_dbg(bcmtch_data_ptr->p_device,
						"PB:[%x]:%s chan init addr=0x%08x\n",
						BCMTCH_DF_PB,
						"rom",
						p_entry[entry_id].addr);

					ret_val =
						bcmtch_dev_init_channels(
							bcmtch_data_ptr,
							p_entry[entry_id].addr,
							(uint8_t *)
							((uint32_t)p_fw->data
							+ p_entry[entry_id].
							offset));
					if (ret_val) {
						dev_err(
							bcmtch_data_ptr->
							p_device,
							"%s: pre-boot %d download error.\n",
							__func__,
							entry_id);
						goto download_error;
					}
				} else {
					/* State protocol */
					bcmtch_data_ptr->work_process_index =
						BCMTCH_WP_STATE;
				}

			/* Intentional fall thru */

			case BCMTCH_FIRMWARE_FLAGS_CODE:
				dev_dbg(bcmtch_data_ptr->p_device,
					"PB:[%x]:entry_id=%d PREBOOT\n",
					BCMTCH_DF_PB,
					entry_id);
				/** download to chip **/
				ret_val = bcmtch_com_write_sys(
					bcmtch_data_ptr,
					p_entry[entry_id].addr,
					p_entry[entry_id].length,
					(uint8_t *)((uint32_t)p_fw->data +
					p_entry[entry_id].offset));
				if (ret_val) {
					dev_err(bcmtch_data_ptr->p_device,
						"%s: pre-boot %d download error.\n",
						__func__,
						entry_id);
					goto download_error;
				}
				break;

			default:
				dev_err(bcmtch_data_ptr->p_device,
						"%s:UNKNOWN BFF!!! entry_id=%d\n",
						__func__,
						entry_id);
			}

			/* next */
			entry_id++;
		}
	}

	if (bcmtch_boot_flag &
			BCMTCH_BF_DISABLE_POST_BOOT)
		bcmtch_data_ptr->post_boot_sections = 0;

	if (bcmtch_data_ptr->post_boot_sections) {
		/* setup first section for download */
		if (!bcmtch_dev_post_boot_get_section(bcmtch_data_ptr))
			bcmtch_data_ptr->post_boot_sections = 0;
	}

	dev_dbg(bcmtch_data_ptr->p_device,
		"INFO:[%x]:FIRMWARE: loaded\n",
		BCMTCH_DF_INFO);

download_error:

	/* free kernel structures */
	release_firmware(p_fw);
	return ret_val;
}

static int bcmtch_dev_find_firmware(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int32_t ret_file = -ENOENT;
	uint8_t file = 0;

	bool found_chip_id = false;

	uint32_t id = bcmtch_data_ptr->chip_id;
	uint32_t rev = bcmtch_data_ptr->rev_id;

	while (file < ARRAY_SIZE(bcmtch_binaries)) {

		/* find matching chip id */
		if (bcmtch_binaries[file].chip_id == id) {
			found_chip_id = true;

			/* if chip id found find matching chip rev */
			if (bcmtch_binaries[file].chip_rev == rev) {
				ret_file = file;
				break;
			} else if (bcmtch_binaries[file].chip_rev == BCMTCHWC) {
				ret_file = file;
			}
		} else if (bcmtch_binaries[file].chip_id == BCMTCHWC) {
			if (!found_chip_id)
				ret_file = file;
		}

		file++;
	}

	if (ret_file < 0)
		dev_err(bcmtch_data_ptr->p_device,
			" firmware not configured:chip=0x%8x rev=0x%x\n",
			id,
			rev);

	return ret_file;
}

static int bcmtch_dev_init_firmware(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint8_t bin_file = 0;

	if (bcmtch_firmware) {
		ret_val =
			bcmtch_dev_download_firmware(
				bcmtch_data_ptr,
				bcmtch_firmware,
				bcmtch_firmware_addr,
				bcmtch_firmware_flag);
	} else {
		bin_file = bcmtch_dev_find_firmware(bcmtch_data_ptr);

		if (bin_file >= 0) {
			ret_val =
				bcmtch_dev_download_firmware(
					bcmtch_data_ptr,
					bcmtch_binaries[bin_file].filename,
					bcmtch_binaries[bin_file].addr,
					bcmtch_binaries[bin_file].flags);
		} else
			ret_val = bin_file;
	}

	/* init state protocol data structures. */
	if (bcmtch_data_ptr->state_protocol
			&& !bcmtch_data_ptr->boot_from_rom)
		bcmtch_dev_init_state(bcmtch_data_ptr);

	if (!ret_val)
		ret_val = bcmtch_dev_run_firmware(bcmtch_data_ptr);

		if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE) {
			if (!ret_val)
				ret_val =
					bcmtch_state_resume(
						bcmtch_data_ptr);

			if (!ret_val) {
					ret_val =
						bcmtch_state_start_scan(
							bcmtch_data_ptr);
			}
		}

	return ret_val;
}

static int bcmtch_dev_init_platform(struct device *p_device)
{
	int ret_val = 0;
	struct bcmtch_data *local_bcmtch_data_p = NULL;
	int32_t	idx;
	int32_t btn_count;
	int32_t of_ret_val;
	struct device_node *np;
	enum of_gpio_flags gpio_flags;
	struct bcmtch_platform_data *p_platform_data = NULL;

	if (p_device) {
		local_bcmtch_data_p =
				dev_get_drvdata(p_device);

		np = p_device->of_node;
		if (!np) {
			dev_err(p_device,
				" Device tree (DT) error! of_node is NULL.\n");
			goto nodt;
		}

		/*
		 * Obtain the address of the SYS/AHB on I2C bus.
		 */
		of_ret_val =
				of_property_read_u32(np, "addr-sys",
					&local_bcmtch_data_p
						->platform_data.i2c_addr_sys);
		if (of_ret_val) {
			dev_err(p_device,
					"DT property addr-sys not found!\n");
			goto of_read_error;
		}

		dev_dbg(p_device,
			"DT:[%x]:addr-sys = 0x%x\n",
			BCMTCH_DF_DT,
			local_bcmtch_data_p->platform_data.i2c_addr_sys);

		/*
		 * Obtain the GPIO reset pin.
		 */
		if (!of_find_property(np, "reset-gpios", NULL)) {
			dev_err(p_device,
				"DT property reset-gpios not found!\n");
			ret_val = of_ret_val;
			local_bcmtch_data_p->
				platform_data.gpio_reset_pin = -1;
			local_bcmtch_data_p->
				platform_data.gpio_reset_polarity = -1;
		} else {

			local_bcmtch_data_p->
				platform_data.gpio_reset_pin =
					of_get_named_gpio_flags(
						np,
						"reset-gpios",
						0,
						&gpio_flags);
			dev_dbg(p_device,
				"DT:[%x]:gpio-reset-pin = 0x%x\n",
				BCMTCH_DF_DT,
				local_bcmtch_data_p->platform_data
					.gpio_reset_pin);

			local_bcmtch_data_p->
				platform_data.gpio_reset_polarity =
					gpio_flags & OF_GPIO_ACTIVE_LOW;
			dev_dbg(p_device,
				"DT:[%x]:gpio-reset-polarity = 0x%x\n",
				BCMTCH_DF_DT,
				local_bcmtch_data_p->platform_data
					.gpio_reset_polarity);

			/*
			 * Obtain the GPIO reset time in ms.
			 */
			of_ret_val = of_property_read_u32(np, "reset-time-ms",
					&local_bcmtch_data_p
						->platform_data
							.gpio_reset_time_ms);
			if (of_ret_val) {
				/* set default value */
				local_bcmtch_data_p->
					platform_data.
						gpio_reset_time_ms = 100;
			}
			dev_dbg(p_device,
				"DT:[%x]:gpio-reset-time = %u\n",
				BCMTCH_DF_DT,
				local_bcmtch_data_p->platform_data
					.gpio_reset_time_ms);
		}

		/*
		 * Obtain the interrupt pin.
		 */
		local_bcmtch_data_p->platform_data.touch_irq =
				irq_of_parse_and_map(np, 0);
		if (local_bcmtch_data_p->platform_data.touch_irq) {
			dev_dbg(p_device,
				"DT:[%x]:irq = 0x%x",
				BCMTCH_DF_DT,
				local_bcmtch_data_p->platform_data.touch_irq);

			local_bcmtch_data_p->
				platform_data.gpio_interrupt_pin = -1;
			local_bcmtch_data_p->
				platform_data.gpio_interrupt_trigger =
					IRQF_TRIGGER_NONE;
		} else {
			dev_err(p_device,
				"DT: interrupts (irq) request failed!\n");
			of_ret_val = -ENOENT;
			goto of_read_error;
		}

		/*
		 * Setup function pointers for axis coordinates.
		 */
		of_ret_val =
				of_property_read_u32(np,
					"axis-orientation-flag",
					&local_bcmtch_data_p->platform_data
						.axis_orientation_flag);
		if (of_ret_val) {
			dev_info(p_device,
				"DT property axis-orientation-flag not found!\n");
			local_bcmtch_data_p->platform_data
				.axis_orientation_flag = 0;
		}

		dev_dbg(p_device,
			"DT:[%x]:axis-orientation-flag = 0x%02x\n",
			BCMTCH_DF_DT,
			local_bcmtch_data_p->platform_data
			.axis_orientation_flag);

		/*
		 * Obtain the key map.
		 */
		of_ret_val =
				of_property_read_u32(np, "ext-button-count",
						&btn_count);
		if (of_ret_val) {
			dev_dbg(p_device,
				"DT:[%x]:ext-button-count not found!\n",
				BCMTCH_DF_DT);
			btn_count = 0;
		}

		if (btn_count) {
			dev_dbg(p_device,
				"DT:[%x]:ext-button-count = %d\n",
				BCMTCH_DF_DT,
				btn_count);

			/* Allocate array */
			local_bcmtch_data_p
				->platform_data.ext_button_map =
					(const int *)local_bcmtch_data_p
					->bcmtch_button_map;

			/* Read array data from device tree */
			of_ret_val =
					of_property_read_u32_array(
						np, "ext-button-map",
						(u32 *)local_bcmtch_data_p
							->platform_data
							.ext_button_map,
						btn_count);
			if (of_ret_val) {
				dev_err(p_device,
					"DT property ext-button-map read failed!\n");
				local_bcmtch_data_p->platform_data
					.ext_button_count = 0;
			} else {
				local_bcmtch_data_p->platform_data
					.ext_button_count = btn_count;

				dev_dbg(p_device,
					"DT:[%x]:ext-button-map =",
					BCMTCH_DF_DT);
				for (idx = 0; idx < btn_count; idx++)
					dev_dbg(p_device,
						"DT:[%x]:%d",
						BCMTCH_DF_DT,
						local_bcmtch_data_p
						->platform_data
						.ext_button_map[idx]);
			}
		}

		/*
		 * Wait for applying power to Napa.
		 * NAPA POR is 4ms == 4000us
		 * - Value should NOT be less than 5000us
		 */
		of_ret_val =
				of_property_read_u32(np,
					"power-on-delay-us",
					&local_bcmtch_data_p->platform_data
						.power_on_delay_us);
		if (of_ret_val) {
			dev_info(p_device,
				"DT property power-on-delay-us not found!\n");
			local_bcmtch_data_p->platform_data
				.power_on_delay_us =
					BCMTCH_POWER_ON_DELAY_US_MIN;
		}

		dev_dbg(p_device,
			"DT:[%x]:power-on-delay-us = %u\n",
			BCMTCH_DF_DT,
			local_bcmtch_data_p->platform_data
				.power_on_delay_us);
	return ret_val;
nodt:

		p_platform_data =
			(struct bcmtch_platform_data *)p_device->platform_data;

		local_bcmtch_data_p->platform_data.i2c_addr_sys =
			p_platform_data->i2c_addr_sys;

		local_bcmtch_data_p->platform_data.i2c_addr_spm =
			p_platform_data->i2c_addr_spm;

		local_bcmtch_data_p->platform_data.gpio_reset_pin =
			p_platform_data->gpio_reset_pin;
		local_bcmtch_data_p->platform_data.gpio_reset_polarity =
			p_platform_data->gpio_reset_polarity;
		local_bcmtch_data_p->platform_data.gpio_reset_time_ms =
			p_platform_data->gpio_reset_time_ms;

		local_bcmtch_data_p->platform_data.gpio_interrupt_pin =
			p_platform_data->gpio_interrupt_pin;
		local_bcmtch_data_p->platform_data.gpio_interrupt_trigger =
			p_platform_data->gpio_interrupt_trigger;
		local_bcmtch_data_p->platform_data.touch_irq = gpio_to_irq(
			p_platform_data->gpio_interrupt_pin);

		local_bcmtch_data_p->platform_data.ext_button_count =
			p_platform_data->ext_button_count;
		local_bcmtch_data_p->platform_data.ext_button_map =
			p_platform_data->ext_button_map;

		local_bcmtch_data_p->platform_data.axis_orientation_flag =
			p_platform_data->axis_orientation_flag;

		local_bcmtch_data_p->platform_data.power_on_delay_us =
			p_platform_data->power_on_delay_us;


		/* check for axis overrides */
		if (bcmtch_boot_flag & BCMTCH_BF_AXIS_X_REVERSE)
			local_bcmtch_data_p->platform_data.axis_orientation_flag
			^= BCMTCH_AXIS_FLAG_X_REVERSED_MASK;

		if (bcmtch_boot_flag & BCMTCH_BF_AXIS_Y_REVERSE)
			local_bcmtch_data_p->platform_data.axis_orientation_flag
			^= BCMTCH_AXIS_FLAG_Y_REVERSED_MASK;

		if (bcmtch_boot_flag & BCMTCH_BF_AXIS_XY_SWAP)
			local_bcmtch_data_p->platform_data.axis_orientation_flag
			^= BCMTCH_AXIS_FLAG_X_Y_SWAPPED_MASK;

		/*
		 * NAPA POR is 4ms == 4000us
		 * - Value should NOT be less than 5000us
		 */
		local_bcmtch_data_p->platform_data.power_on_delay_us =
			max(BCMTCH_POWER_ON_DELAY_US_MIN,
				local_bcmtch_data_p->
				platform_data.power_on_delay_us);

	} else {
		pr_err("%s() error, platform data == NULL\n", __func__);
		ret_val = -ENODATA;
	}

	return ret_val;

of_read_error:
	if (!ret_val)
		ret_val = -ENODEV;

	return ret_val;
}

static int bcmtch_dev_request_power_mode(
		struct bcmtch_data *bcmtch_data_ptr,
		uint8_t mode, enum tofe_command command)
{
	int ret_val = 0;

	uint8_t regs[5];
	uint8_t data[5];

	regs[0] = BCMTCH_SPM_REG_MSG_FROM_HOST;
	data[0] = command;
	regs[1] = BCMTCH_SPM_REG_RQST_FROM_HOST;
	data[1] = 0;

	switch (mode) {
	case BCMTCH_POWER_MODE_SLEEP:
		data[1] = BCMTCH_POWER_MODE_SLEEP;
		break;

	case BCMTCH_POWER_MODE_WAKE:
		data[1] = BCMTCH_POWER_MODE_WAKE;
		break;

	case BCMTCH_POWER_MODE_NOWAKE:
		data[1] = BCMTCH_POWER_MODE_NOWAKE;
		break;

	default:
		dev_err(bcmtch_data_ptr->p_device,
			"%s:unknown mode",
			__func__);
		break;
	}

	ret_val = bcmtch_com_fast_write_spm(bcmtch_data_ptr, 2, regs, data);

	return ret_val;
}

static int bcmtch_dev_get_power_state(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint8_t power_state;

	ret_val = bcmtch_com_read_spm(bcmtch_data_ptr,
		BCMTCH_SPM_REG_PSR, &power_state);

	return (ret_val) ? (ret_val) : ((int)power_state);
}

static int bcmtch_dev_set_power_state(
		struct bcmtch_data *bcmtch_data_ptr,
		uint8_t power_state)
{
	int ret_val = 0;

	switch (power_state) {
	case BCMTCH_POWER_STATE_SLEEP:
		ret_val =
			bcmtch_dev_request_power_mode(
				bcmtch_data_ptr,
				BCMTCH_POWER_MODE_SLEEP,
				TOFE_COMMAND_NO_COMMAND);
		break;

	case BCMTCH_POWER_STATE_RETENTION:
	case BCMTCH_POWER_STATE_IDLE:
	case BCMTCH_POWER_STATE_ACTIVE:
		dev_err(bcmtch_data_ptr->p_device,
			"ERROR:%s:unsupported state",
			__func__);
		break;

	default:
		dev_err(bcmtch_data_ptr->p_device,
			"ERROR:%s:unknown state",
			__func__);
		break;
	}

	return ret_val;
}

static int bcmtch_dev_check_power_state(
				struct bcmtch_data *bcmtch_data_ptr,
				uint8_t power_state,
				uint8_t wait_count)
{
	int ret_val = -EAGAIN;
	int32_t read_state;

	do {
		read_state = bcmtch_dev_get_power_state(bcmtch_data_ptr);
		if (read_state == power_state) {
			ret_val = 0;
			break;
		}
	} while (wait_count--);

	return ret_val;
}

static enum bcmtch_status bcmtch_dev_request_host_override(
		struct bcmtch_data *bcmtch_data_ptr,
		enum tofe_command command)
{
	enum bcmtch_status ret_val = BCMTCH_STATUS_ERR_FAIL;
	int32_t count = 250;
	uint8_t m2h;

	/* Request channel & wakeup the firmware */
	ret_val = bcmtch_dev_request_power_mode(
					bcmtch_data_ptr,
					BCMTCH_POWER_MODE_WAKE,
					command);

	if (!ret_val)
		ret_val = bcmtch_dev_check_power_state(
					bcmtch_data_ptr,
					BCMTCH_POWER_STATE_ACTIVE,
					25);

	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s: [%d] wake firmware failed.\n",
				__func__,
				ret_val);

	} else {

		/* Wait till FW OVERRIDE is ready */
		do {
			ret_val = bcmtch_com_read_spm(
					bcmtch_data_ptr,
					BCMTCH_SPM_REG_MSG_TO_HOST,
					&m2h);

			switch (m2h) {
			case TOFE_MESSAGE_FW_READY_OVERRIDE:
			case TOFE_MESSAGE_FW_READY_INTERRUPT_OVERRIDE:
				bcmtch_data_ptr->host_override = true;
				dev_dbg(bcmtch_data_ptr->p_device,
					"HO:[%x]:Request m=0x%0x  ho=%d",
					BCMTCH_DF_HO,
					m2h,
					bcmtch_data_ptr->host_override);
				break;

			case TOFE_MESSAGE_FW_READY_INTERRUPT:
				dev_dbg(bcmtch_data_ptr->p_device,
					"HO:[%x]:Request m=0x%0x  ho=%d -> interrupt",
					BCMTCH_DF_HO,
					m2h,
					bcmtch_data_ptr->host_override);
				bcmtch_dev_process(bcmtch_data_ptr);
			case TOFE_MESSAGE_FW_READY:
			default:
				dev_dbg(bcmtch_data_ptr->p_device,
					"HO:[%x]:Request m=0x%0x  ho=%d",
					BCMTCH_DF_HO,
					m2h,
					bcmtch_data_ptr->host_override);
				break;
			}
		} while (!bcmtch_data_ptr->host_override && count--);

		if (bcmtch_data_ptr->host_override)
			ret_val = BCMTCH_STATUS_SUCCESS;
	}

	return ret_val;
}

static enum bcmtch_status bcmtch_dev_release_host_override(
		struct bcmtch_data *bcmtch_data_ptr,
		enum tofe_command command)
{
	enum bcmtch_status ret_val = BCMTCH_STATUS_ERR_FAIL;
	int32_t count = 250;
	uint8_t m2h;

	/* this should release hostOverride - do we need to check */
	/* Release channel */
	ret_val = bcmtch_dev_request_power_mode(
				bcmtch_data_ptr,
				BCMTCH_POWER_MODE_NOWAKE,
				command);

	/* Wait till FW is ready */
	do {
		ret_val = bcmtch_com_read_spm(bcmtch_data_ptr,
				BCMTCH_SPM_REG_MSG_TO_HOST, &m2h);
		switch (m2h) {
		case TOFE_MESSAGE_FW_READY_INTERRUPT_OVERRIDE:
			dev_dbg(bcmtch_data_ptr->p_device,
				"HO:[%x]:Release m=0x%0x  ho=%d -> interrupt",
				BCMTCH_DF_HO,
				m2h,
				bcmtch_data_ptr->host_override);
			bcmtch_dev_process(bcmtch_data_ptr);
		case TOFE_MESSAGE_FW_READY_OVERRIDE:
		default:
			dev_dbg(bcmtch_data_ptr->p_device,
				"HO:[%x]:Release m=0x%0x  ho=%d",
				BCMTCH_DF_HO,
				m2h,
				bcmtch_data_ptr->host_override);
			break;

		case TOFE_MESSAGE_FW_READY_INTERRUPT:
		case TOFE_MESSAGE_FW_READY:
			bcmtch_data_ptr->host_override = false;
			dev_dbg(bcmtch_data_ptr->p_device,
				"HO:[%x]:Release m=0x%0x  ho=%d",
				BCMTCH_DF_HO,
				m2h,
				bcmtch_data_ptr->host_override);
			break;
		}
	} while (bcmtch_data_ptr->host_override && count--);

	if (!bcmtch_data_ptr->host_override)
		ret_val = BCMTCH_STATUS_SUCCESS;

	return ret_val;
}

static int bcmtch_dev_send_command(
	struct bcmtch_data *bcmtch_data_ptr,
	enum tofe_command command,
	uint32_t data,
	uint16_t data16,
	uint8_t flags)
{
	int ret_val = 0;
	struct tofe_command_response cmd;
	struct bcmtch_channel *chan;

	if (command == TOFE_COMMAND_NO_COMMAND) {
		dev_err(bcmtch_data_ptr->p_device,
			"%s: no_command.\n",
			__func__);
		return -EINVAL;
	}

	chan =
		bcmtch_data_ptr->p_channels[TOFE_CHANNEL_ID_COMMAND];
	if (chan == NULL) {
		dev_err(bcmtch_data_ptr->p_device,
			"%s: command channel has not initialized!\n",
			__func__);
		return -ENXIO;
	}


	if (bcmtch_dev_request_host_override(
			bcmtch_data_ptr, command) ==
				BCMTCH_STATUS_SUCCESS) {

		/* Setup the command entry */
		memset(
			(void *)(&cmd),
			0,
			sizeof(struct tofe_command_response));
		cmd.flags	= flags;
		cmd.command	= command;
		cmd.data	= data;
		cmd.result	= data16;

		/* Write sys to command channel */
		tofe_channel_write_begin(&chan->hdr);
		ret_val = tofe_channel_write(&chan->hdr, &cmd);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"%s: [%d] cmd channel write failed.\n",
					__func__,
					ret_val);
			goto send_command_exit;
		}

		ret_val = bcmtch_dev_write_channel(bcmtch_data_ptr,
			chan);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
				"%s: [%d] cmd channel write back FW failed.\n",
				__func__,
				ret_val);
			goto send_command_exit;
		}
	}

	bcmtch_dev_release_host_override(bcmtch_data_ptr,
		command);

send_command_exit:
	return ret_val;
}

static int bcmtch_dev_reset(
		struct bcmtch_data *bcmtch_data_ptr,
		uint8_t mode)
{
	int ret_val = 0;

	switch (mode) {
	case BCMTCH_RESET_MODE_HARD:
		bcmtch_reset(bcmtch_data_ptr);
		break;

	case BCMTCH_RESET_MODE_SOFT_CHIP:
		bcmtch_com_write_spm(
			bcmtch_data_ptr,
			BCMTCH_SPM_REG_SOFT_RESETS,
			BCMTCH_RESET_MODE_SOFT_CHIP);
		break;

	case BCMTCH_RESET_MODE_SOFT_ARM:
		bcmtch_com_write_spm(
			bcmtch_data_ptr,
			BCMTCH_SPM_REG_SOFT_RESETS,
			BCMTCH_RESET_MODE_SOFT_ARM);
		break;

	case (BCMTCH_RESET_MODE_SOFT_CHIP | BCMTCH_RESET_MODE_SOFT_ARM):
		bcmtch_com_write_spm(
			bcmtch_data_ptr,
			BCMTCH_SPM_REG_SOFT_RESETS,
			BCMTCH_RESET_MODE_SOFT_CHIP);
		bcmtch_com_write_spm(
			bcmtch_data_ptr,
			BCMTCH_SPM_REG_SOFT_RESETS,
			BCMTCH_RESET_MODE_SOFT_ARM);
		break;

	case BCMTCH_RESET_MODE_SOFT_CLEAR:
		ret_val = bcmtch_com_write_spm(
					bcmtch_data_ptr,
					BCMTCH_SPM_REG_SOFT_RESETS,
					BCMTCH_RESET_MODE_SOFT_CLEAR);
		break;

	default:
		break;
	}

	return ret_val;
}


static int bcmtch_dev_get_rev_id(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = -ENXIO;

	if (bcmtch_data_ptr) {
		ret_val =
			bcmtch_com_read_spm(bcmtch_data_ptr,
				BCMTCH_SPM_REG_REVISIONID,
				&bcmtch_data_ptr->rev_id);
	}
	return ret_val;
}

static int bcmtch_dev_get_chip_id(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = -ENXIO;

	if (bcmtch_data_ptr) {
		uint8_t id[3];

		ret_val =
			bcmtch_com_read_spm(bcmtch_data_ptr,
				BCMTCH_SPM_REG_CHIPID0, &id[0]);
		ret_val |=
			bcmtch_com_read_spm(bcmtch_data_ptr,
				BCMTCH_SPM_REG_CHIPID1, &id[1]);
		ret_val |=
			bcmtch_com_read_spm(bcmtch_data_ptr,
				BCMTCH_SPM_REG_CHIPID2, &id[2]);

		bcmtch_data_ptr->chip_id = ((((uint32_t)id[2]) << 16)
			| (((uint32_t)id[1]) << 8)
			| (uint32_t)id[0]);
	}

	return ret_val;

}

static int bcmtch_dev_verify_chip_version(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint32_t idx = 0;
	uint32_t *chips_ptr = (uint32_t *)bcmtch_chip_ids;

	/* Get Chip ID AFTER Power On due to OTP */
	ret_val = bcmtch_dev_get_chip_id(bcmtch_data_ptr);

	if (!ret_val)
		ret_val = bcmtch_dev_get_rev_id(bcmtch_data_ptr);

	if (!ret_val) {
		ret_val = -ENXIO;
		for (idx = 0;
				idx < ARRAY_SIZE(bcmtch_chip_ids);
				idx++) {
			if (chips_ptr[idx] ==
				bcmtch_data_ptr->chip_id) {
				/* Found a match in above search */
				ret_val = 0;

				dev_dbg(bcmtch_data_ptr->p_device,
					"INFO:[%x]:chip_id = 0x%06X  rev = 0x%2X : %s\n",
					BCMTCH_DF_INFO,
					bcmtch_data_ptr->chip_id,
					bcmtch_data_ptr->rev_id,
					"Verified");
			}
		}
	}

	if (ret_val)
		dev_err(bcmtch_data_ptr->p_device,
			" chip_id = 0x%06X  rev = 0x%2X : %s : 0x%x\n",
			bcmtch_data_ptr->chip_id,
			bcmtch_data_ptr->rev_id,
			"Error - Unknown device",
			ret_val);

	return ret_val;
}

static int bcmtch_dev_get_hw_id(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = -ENXIO;
	uint32_t hw_id = 0;
	uint32_t invalid = 0;
	int i;

	if (bcmtch_data_ptr) {
		ret_val = bcmtch_com_write_sys32(
					bcmtch_data_ptr,
					BCMTCH_ADDR_COMMON_OTP_CPU_MODE,
					0x03);
		if (!ret_val)
			ret_val = bcmtch_dev_read_otp(
						bcmtch_data_ptr,
						bcmtch_data_ptr->
						otp_hw_id_cfg.valid_addr,
						&invalid);
		for (i = 0;
			!ret_val &&
			(i < bcmtch_data_ptr->otp_hw_id_cfg.table_size);
			i++) {
			if ((invalid & (BCMTCH_OTP_VALID_MASK << i)) == 0) {
				ret_val =
					bcmtch_dev_read_otp(
						bcmtch_data_ptr,
						bcmtch_data_ptr->
						otp_hw_id_cfg.data_addr + i,
						&hw_id);
				break;
			}
		}

		if (!ret_val) {
			bcmtch_data_ptr->otp_hw_id = hw_id;
			dev_dbg(bcmtch_data_ptr->p_device,
				"INFO:[%x]:H/W id=0x%08x\n",
				BCMTCH_DF_INFO,
				hw_id);
		} else
			dev_err(bcmtch_data_ptr->p_device,
				"OTP read ID error!\n");
	}
	return ret_val;
}

static void bcmtch_dev_init_worker_process(
		struct bcmtch_data *bcmtch_data_ptr)
{
	/* Channel protocol process function. */
	bcmtch_data_ptr->bcmtch_dev_process_table
			[BCMTCH_WP_CHANNEL] = bcmtch_dev_process;

	/* State protocol process function. */
	bcmtch_data_ptr->bcmtch_dev_process_table
			[BCMTCH_WP_STATE] =
				bcmtch_dev_process_state_touches;

	/* The process function during the post boot patch
	 * switch (boot from ROM to boot from RAM). */
	bcmtch_data_ptr->bcmtch_dev_process_table
			[BCMTCH_WP_PATCH_INIT] =
				bcmtch_dev_process_pb_patch_init;

	/* Set the default work process function. */
	bcmtch_data_ptr->work_process_index =
			BCMTCH_WP_CHANNEL;

}

static void bcmtch_dev_init_state(
		struct bcmtch_data *bcmtch_data_ptr)
{
	bcmtch_data_ptr->scan_data.cmd.max_slot_number =
		BCMTCH_MAX_TOUCH;

	if (bcmtch_boot_flag & BCMTCH_BF_STATE_SHORT_SLOT) {
		bcmtch_data_ptr->
			scan_data.cmd.touch_slot_format = 1;
		bcmtch_data_ptr->
			scan_data.touch_slot_size  =
				TOFE_HOST_TOUCH_SLOT_SMALL_SIZE;
	} else {
		bcmtch_data_ptr->
			scan_data.cmd.touch_slot_format = 0;
		bcmtch_data_ptr->
			scan_data.touch_slot_size  =
				TOFE_HOST_TOUCH_SLOT_BIG_SIZE;
	}
	memset(&bcmtch_data_ptr->ram_rw_cfg, 0,
			sizeof(bcmtch_data_ptr->ram_rw_cfg));
	bcmtch_data_ptr->work_process_index =
			BCMTCH_WP_STATE;
	dev_dbg(bcmtch_data_ptr->p_device,
		"ST:[%x]:%s() p0=0x%x slot_format %d resp_buff_size %d max_slot_number %d\n",
		BCMTCH_DF_ST,
		__func__,
		bcmtch_data_ptr->scan_data.cmd.reg,
		bcmtch_data_ptr->scan_data.cmd.touch_slot_format,
		TOFE_HOST_RSP_BUF_SIZE,
		bcmtch_data_ptr->scan_data.cmd.max_slot_number);
}

static int bcmtch_dev_init(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint32_t idx;

	/* init com */
	ret_val = bcmtch_com_init(bcmtch_data_ptr);

	/* wakeup */
	if (!ret_val)
		ret_val = bcmtch_dev_request_power_mode(
					bcmtch_data_ptr,
					BCMTCH_POWER_MODE_WAKE,
					TOFE_COMMAND_NO_COMMAND);

	if (!ret_val)
		ret_val = bcmtch_dev_check_power_state(
					bcmtch_data_ptr,
					BCMTCH_POWER_STATE_ACTIVE,
					25);

	/* init clocks */
	if (!ret_val)
		ret_val = bcmtch_dev_init_clocks(bcmtch_data_ptr);

	/* read chip version and id with power on */
	if (!ret_val && (BCMTCH_BF_VERIFY_CHIP & bcmtch_boot_flag))
		ret_val = bcmtch_dev_verify_chip_version(bcmtch_data_ptr);

	/* read HW id from OTP on the Napa chip */
	if (!ret_val && (BCMTCH_BF_READ_OTP & bcmtch_boot_flag)) {
		bcmtch_data_ptr->otp_hw_id_cfg =
			bcmtch_hw_id_otp_cfg[0];
		for (idx = 1; idx <
				ARRAY_SIZE(bcmtch_hw_id_otp_cfg); idx++) {
			if (bcmtch_data_ptr->chip_id ==
					bcmtch_hw_id_otp_cfg[idx].chip_id)
				bcmtch_data_ptr->otp_hw_id_cfg =
					bcmtch_hw_id_otp_cfg[idx];
		}
		ret_val = bcmtch_dev_get_hw_id(bcmtch_data_ptr);
	}

	/* download and run */
	if (!ret_val)
		ret_val = bcmtch_dev_init_firmware(bcmtch_data_ptr);

	return ret_val;
}

static void bcmtch_dev_process(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint8_t m2h;

	if (bcmtch_boot_flag & BCMTCH_BF_CHECK_INTERRUPT) {
		/* Check msg2host */
		bcmtch_com_read_spm(bcmtch_data_ptr,
			BCMTCH_SPM_REG_MSG_TO_HOST, &m2h);
		if ((m2h & TOFE_MESSAGE_FW_READY_INTERRUPT) !=
				TOFE_MESSAGE_FW_READY_INTERRUPT) {
			dev_err(bcmtch_data_ptr->p_device,
				"ERROR:false interrupt\n");
			return;
		}
	}

	/* read DMA buffer */
	if (bcmtch_data_ptr->has_dma_channel &&
			!bcmtch_data_ptr->host_override) {
		ret_val =
			bcmtch_dev_read_dma_channels(bcmtch_data_ptr);
	}

	/* read channels */
	ret_val = bcmtch_dev_read_channels(bcmtch_data_ptr);

	/* release memory */
	bcmtch_dev_request_power_mode(
		bcmtch_data_ptr,
		BCMTCH_POWER_MODE_NOWAKE,
		TOFE_COMMAND_NO_COMMAND);

	/* process channels */
	ret_val = bcmtch_dev_process_channels(bcmtch_data_ptr);
}

static void bcmtch_dev_process_pb_patch_init(
	struct bcmtch_data *bcmtch_data_ptr)
{
	uint8_t m2h;

	/* Check post boot init req TOFE_MESSAGE_SOC_REBOOT_PENDING */
	if (bcmtch_data_ptr->post_boot_pending) {
		bcmtch_com_read_spm(bcmtch_data_ptr,
			BCMTCH_SPM_REG_MSG_TO_HOST, &m2h);
		if (m2h == TOFE_MESSAGE_SOC_REBOOT_PENDING) {
			/* Reset post boot pending */
			bcmtch_data_ptr->post_boot_pending = 0;

			/* Send command TOFE_COMMAND_REBOOT_APPROVED */
			bcmtch_dev_request_power_mode(
						bcmtch_data_ptr,
						BCMTCH_POWER_MODE_NOWAKE,
						TOFE_COMMAND_REBOOT_APPROVED);

			dev_dbg(bcmtch_data_ptr->p_device,
				"PB:[%x]:sent REBOOT_APPROVED\n",
				BCMTCH_DF_PB);

			bcmtch_dev_reset_events(bcmtch_data_ptr);

			/* Switch to state protocol work process function.*/
			bcmtch_dev_init_state(bcmtch_data_ptr);
			bcmtch_state_resume(bcmtch_data_ptr);
			bcmtch_state_start_scan(
				bcmtch_data_ptr);
			return;
		}
	}

	/* Process the ROM channel */
	bcmtch_dev_process(bcmtch_data_ptr);
}

static inline int bcmtch_state_start_scan(
					struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint8_t parameters[TOFE_HOST_CMD_SHORT_SIZE];

	parameters[0] = bcmtch_data_ptr->scan_data.cmd.reg;

	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_START_SCAN,
					parameters,
					1);
	dev_dbg(bcmtch_data_ptr->p_device,
		"ST:[%x]:SCAN START: (p0=0x%x) = %d\n",
		BCMTCH_DF_ST,
		bcmtch_data_ptr->scan_data.cmd.reg,
		ret_val);

	return ret_val;
}

static inline int bcmtch_state_stop_scan(
					struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_STOP_SCAN,
					NULL,
					0);
	dev_dbg(bcmtch_data_ptr->p_device,
		"ST:[%x]:SCAN STOP: = %d\n",
		BCMTCH_DF_ST,
		ret_val);

	return ret_val;
}


static inline int bcmtch_state_suspend(
					struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	ret_val = bcmtch_state_stop_scan(bcmtch_data_ptr);
	if (ret_val)
		return ret_val;

	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_SUSPEND,
					NULL,
					0);
	if (!ret_val) {
		/* check command status */
		ret_val = bcmtch_state_check_command_status(
						bcmtch_data_ptr,
						100);
		dev_dbg(bcmtch_data_ptr->p_device,
			"ST:[%x]:CMD SUSPEND - 0x%x\n",
			BCMTCH_DF_ST,
			bcmtch_data_ptr->bcmtch_state_resp_buffer[0]);
	}

	return ret_val;
}

static inline int bcmtch_state_sleep(
					struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	ret_val = bcmtch_state_stop_scan(bcmtch_data_ptr);
	if (ret_val)
		return ret_val;

	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_SLEEP,
					NULL,
					0);
	if (!ret_val) {
		/* check command status */
		ret_val = bcmtch_state_check_command_status(
						bcmtch_data_ptr,
						100);
		dev_dbg(bcmtch_data_ptr->p_device,
			"ST:[%x]:CMD SLEEP - 0x%x\n",
			BCMTCH_DF_ST,
			bcmtch_data_ptr->bcmtch_state_resp_buffer[0]);
	}

	return ret_val;
}

static inline int bcmtch_state_resume(
					struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	ret_val = bcmtch_state_stop_scan(bcmtch_data_ptr);
	if (ret_val)
		return ret_val;

	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_RESUME,
					NULL,
					0);
	if (!ret_val) {
		/* check command status */
		ret_val = bcmtch_state_check_command_status(
						bcmtch_data_ptr,
						100);
		dev_dbg(bcmtch_data_ptr->p_device,
			"ST:[%x]:CMD RESUME - 0x%x\n",
			BCMTCH_DF_ST,
			bcmtch_data_ptr->bcmtch_state_resp_buffer[0]);
	}

	return ret_val;
}

static int bcmtch_state_rw_cfg_init(
				struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint8_t *parameters =
		bcmtch_data_ptr->bcmtch_state_resp_buffer;
	uint8_t *dma_buff =
		bcmtch_data_ptr->bcmtch_state_resp_buffer;
	struct bcmtch_state_rw_params *header =
		(struct bcmtch_state_rw_params *)
			bcmtch_data_ptr->bcmtch_state_resp_buffer;

	/* The Write RAM command returns configuration when
	 * the data size is set to zero.
	 */
	header->len = 0;
	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_WRITE_RAM,
					parameters,
					sizeof(struct bcmtch_state_rw_params));
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
			"ST: %s send cmd error! (%d)\n",
			__func__,
			ret_val);
		return ret_val;
	}

	/* check command status */
	ret_val = bcmtch_state_check_command_status(
					bcmtch_data_ptr,
					5);
	if (!ret_val)
		ret_val = bcmtch_com_read_dma(
					bcmtch_data_ptr,
					sizeof(struct bcmtch_state_cfg_rw) + 1,
					dma_buff);
	if (!ret_val)
		memcpy(&bcmtch_data_ptr->ram_rw_cfg,
				&dma_buff[1],
				sizeof(struct bcmtch_state_cfg_rw));

	dev_dbg(bcmtch_data_ptr->p_device,
		"ST:[%x}:RAM RW cfg init: max_r=%d max_w=%d\n",
		BCMTCH_DF_ST,
		bcmtch_data_ptr->ram_rw_cfg.max_read_size,
		bcmtch_data_ptr->ram_rw_cfg.max_write_size);

	/* Resume the touch scan */
	bcmtch_state_start_scan(bcmtch_data_ptr);

	return ret_val;
}

static int bcmtch_state_read_ram(
				struct bcmtch_data *bcmtch_data_ptr,
				uint32_t addr,
				uint16_t len,
				uint8_t *out,
				uint16_t buf_size)
{
	int ret_val = 0;
	uint8_t *parameters =
		bcmtch_data_ptr->bcmtch_state_resp_buffer;
	uint8_t *dma_buff =
		bcmtch_data_ptr->bcmtch_state_resp_buffer;
	struct bcmtch_state_rw_params *header =
		(struct bcmtch_state_rw_params *)
			bcmtch_data_ptr->bcmtch_state_resp_buffer;

	/* Check the initialization of the read/write config. */
	if (bcmtch_data_ptr->ram_rw_cfg.cfg_size == 0) {
		ret_val =
			bcmtch_state_rw_cfg_init(bcmtch_data_ptr);
		if (ret_val)
			return ret_val;
	}

	if (len > buf_size ||
			len > bcmtch_data_ptr->ram_rw_cfg.max_read_size)
		return -EINVAL;

	ret_val = bcmtch_state_stop_scan(bcmtch_data_ptr);
	if (ret_val)
		return ret_val;

	header->addr = addr;
	header->len = len;
	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_READ_RAM,
					parameters,
					sizeof(struct bcmtch_state_rw_params));
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"ST: %s send cmd error! (%d)\n",
				__func__,
				ret_val);
		return ret_val;
	}

	/* check command status */
	ret_val = bcmtch_state_check_command_status(
					bcmtch_data_ptr,
					5);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"ST: %s chk cmd status error! (%d)\n",
				__func__,
				ret_val);
		return ret_val;
	}

	dev_dbg(bcmtch_data_ptr->p_device,
		"ST:[%x]:CMD R RAM - (%d) addr=0x%08x size=%d\n",
		BCMTCH_DF_ST,
		bcmtch_data_ptr->bcmtch_state_resp_buffer[0],
		addr,
		len);

	/* read response data */
	ret_val = bcmtch_com_read_dma(
				bcmtch_data_ptr,
				len + 1,
				dma_buff);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"ST: %s read DMA error! (%d)\n",
				__func__,
				ret_val);
		return ret_val;
	}

	/* copy the RAM content */
	memcpy(out, &dma_buff[1], len);

	/* Resume the touch scan */
	bcmtch_state_start_scan(bcmtch_data_ptr);

	return ret_val;
}

static int bcmtch_state_write_ram(
				struct bcmtch_data *bcmtch_data_ptr,
				uint32_t addr,
				uint16_t len,
				uint8_t *data)
{
	int ret_val = 0;
	uint8_t *parameters =
		bcmtch_data_ptr->bcmtch_state_resp_buffer;
	struct bcmtch_state_rw_params *header =
		(struct bcmtch_state_rw_params *)
			bcmtch_data_ptr->bcmtch_state_resp_buffer;
	uint16_t data_size = len
				+ sizeof(struct bcmtch_state_rw_params);

	/* Check the initialization of the read/write config. */
	if (bcmtch_data_ptr->ram_rw_cfg.cfg_size == 0) {
		ret_val =
			bcmtch_state_rw_cfg_init(bcmtch_data_ptr);
		if (ret_val)
			return ret_val;
	}

	if (data_size > bcmtch_data_ptr->ram_rw_cfg.max_write_size)
		return -EINVAL;

	header->addr = addr;
	header->len = len;
	memcpy(parameters + sizeof(struct bcmtch_state_rw_params),
			data, len);

	ret_val = bcmtch_state_stop_scan(bcmtch_data_ptr);
	if (ret_val)
		return ret_val;

	ret_val = bcmtch_state_i2c_send_command(
					bcmtch_data_ptr,
					BCMTCH_CMD_WRITE_RAM,
					parameters,
					data_size);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"ST: %s send cmd error! (%d)\n",
				__func__,
				ret_val);
		return ret_val;
	}

	/* check command status */
	ret_val = bcmtch_state_check_command_status(
					bcmtch_data_ptr,
					5);
	dev_dbg(bcmtch_data_ptr->p_device,
		"ST:[%x]:CMD W RAM - (%d) addr=0x%08x size=%d\n",
		BCMTCH_DF_ST,
		bcmtch_data_ptr->bcmtch_state_resp_buffer[0],
		addr,
		len);

	/* Resume the touch scan */
	bcmtch_state_start_scan(bcmtch_data_ptr);

	return ret_val;
}

#define BCMTCH_FW_LIMIT_STACK	24
#define BCMTCH_FW_LIMIT_REGS	15

static const char const *fw_msg[] = {
	"Reset", "Reset", "NMI", "HardFault",
	"MemManage", "BusFault", "UsageFault"
};

static const char const *fw_reg[] = {
	"R0", "R1", "R2", "R3", "R12",
	"LR", "PC", "PSR", "SP",
	"SCB_CCR", "SCB_SHCSR", "SCB_CFS",
	"SCB_HFSR", "SCB_MMFAR", "SCB_BFAR"
};

static void bcmtch_state_check_fw_exception(
				struct bcmtch_data *bcmtch_data_ptr,
				uint8_t fw_exception)
{
	int ret_val;
	unsigned int index;
	unsigned int index_limit;
	unsigned int read_size =
			sizeof(bcmtch_data_ptr->exception_buffer);

	unsigned int *ex_buf = bcmtch_data_ptr->exception_buffer;

	if (fw_exception < ARRAY_SIZE(fw_msg))
		dev_err(bcmtch_data_ptr->p_device,
				"FWE:Critical error %d [%s]\n",
				fw_exception, fw_msg[fw_exception]);
	else
		dev_err(bcmtch_data_ptr->p_device,
				"FWE:Critical error %d\n", fw_exception);

	if (bcmtch_data_ptr->work_process_index < BCMTCH_WP_STATE)
		return;

	memset(ex_buf, 0, read_size);
	ret_val = bcmtch_com_read_dma(
				bcmtch_data_ptr,
				read_size,
				(uint8_t *)ex_buf);

	if (ret_val ||
		((ex_buf[15] != 0xDEADBEEF) && (ex_buf[15] != 0xDEADBEEB))) {
		dev_err(bcmtch_data_ptr->p_device,
				"FWE:%s - read dma failed %d\n",
				__func__,
				ret_val);
		return;
	}

	index = 0;
	index_limit = (ex_buf[15] == 0xDEADBEEB) ?
					BCMTCH_FW_LIMIT_STACK :
					BCMTCH_FW_LIMIT_REGS;
	while (index < index_limit) {
		if (index < BCMTCH_FW_LIMIT_REGS)
			dev_err(bcmtch_data_ptr->p_device,
				"FWE:%s  = 0x%08X\n",
				fw_reg[index],
				ex_buf[index]);
		else if (index > BCMTCH_FW_LIMIT_REGS)
			dev_err(bcmtch_data_ptr->p_device,
				"FWE:Stack[%d]  = 0x%08X\n",
				index - (BCMTCH_FW_LIMIT_REGS+1),
				ex_buf[index]);
		index++;
	}
}

static int bcmtch_state_check_command_status(
				struct bcmtch_data *bcmtch_data_ptr,
				int timeout_ms)
{
	uint8_t m2h;
	int ret_val = 0;

	do {
		ret_val =
			bcmtch_com_read_spm(
					bcmtch_data_ptr,
					BCMTCH_SPM_REG_MSG_TO_HOST,
					&m2h);
		if (ret_val)
			return ret_val;

		if ((m2h > 0x1) && (m2h < TOFE_MESSAGE_SUCCESS)) {
			ret_val = m2h;
			bcmtch_state_check_fw_exception(
				bcmtch_data_ptr,
				m2h);
			break;
		} else if (m2h > TOFE_MESSAGE_COMMAND_ECHO) {
			break;
		}

		usleep_range(800, 1200);
	} while (timeout_ms--
			&& !(m2h & TOFE_MESSAGE_FW_READY));

	bcmtch_data_ptr->bcmtch_state_resp_buffer[0] =
			m2h & ~TOFE_MESSAGE_FW_READY;

	return ret_val;
}

static void bcmtch_dev_process_state_touches(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	int slots;

	/* Check command status */
	ret_val = bcmtch_state_check_command_status(
					bcmtch_data_ptr,
					20);

	if (ret_val)
		return;

	/* Read command response status */
	bcmtch_data_ptr->scan_data.status.reg =
		bcmtch_data_ptr->bcmtch_state_resp_buffer[0];

	slots = bcmtch_data_ptr->scan_data.status.slots;
	/* FIXME: Need to take the log slot case into account. */

	/* Read variable-size response (long) */
	ret_val = bcmtch_dev_read_scan_touches(
					bcmtch_data_ptr,
					slots);

	/* Next touch scan */
	bcmtch_state_start_scan(bcmtch_data_ptr);
}

static void bcmtch_dev_reset_events(
		struct bcmtch_data *bcmtch_data_ptr)
{
	uint32_t touch = 0;

	/* clear active touch structure */
	memset(
		&bcmtch_data_ptr->touch[0],
		0,
		sizeof(struct bcmtch_touch) * BCMTCH_MAX_TOUCH);

	/* clear system touches */
	if (bcmtch_data_ptr->touch_count) {
		for (touch = 0; touch < BCMTCH_MAX_TOUCH; touch++) {
			input_mt_slot(
				bcmtch_data_ptr->p_input_device,
				touch);
			input_mt_report_slot_state(
				bcmtch_data_ptr->p_input_device,
				MT_TOOL_FINGER,
				false);
		}

		input_report_key(
			bcmtch_data_ptr->p_input_device,
			BTN_TOUCH,
			false);

		input_sync(bcmtch_data_ptr->p_input_device);
	}
	bcmtch_data_ptr->touch_count = 0;
}

static void bcmtch_dev_post_boot_reset(
	struct bcmtch_data *bcmtch_data_ptr)
{
	bcmtch_data_ptr->post_boot_section = 0;
	bcmtch_data_ptr->post_boot_sections = 0;

	bcmtch_data_ptr->post_boot_patches = 0;

	bcmtch_data_ptr->post_boot_pending = 0;

	bcmtch_data_ptr->post_boot_cfg_addr = 0;
	bcmtch_data_ptr->post_boot_cfg_length = 0;

	/* free communication channels */
	bcmtch_dev_free_channels(bcmtch_data_ptr);
}

static int bcmtch_dev_suspend(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	/* check suspend status */
	spin_lock(&bcmtch_data_ptr->lock_suspend);
	if (bcmtch_data_ptr->suspend) {
		spin_unlock(&bcmtch_data_ptr->lock_suspend);
		return 0; /* already suspended */
	}

	/* update suspend status */
	bcmtch_data_ptr->suspend = true;
	spin_unlock(&bcmtch_data_ptr->lock_suspend);

	/* disable interrupts */
	bcmtch_interrupt_disable(bcmtch_data_ptr);

	/* free watchdog timer */
	bcmtch_dev_watchdog_stop(bcmtch_data_ptr);

	/* clear worker */
	bcmtch_clear_deferred_worker(bcmtch_data_ptr);

	/* lock */
	mutex_lock(&bcmtch_data_ptr->mutex_work);

	if (bcmtch_boot_flag & BCMTCH_BF_SUSPEND_COLD_BOOT) {
		/* post boot reset */
		bcmtch_dev_post_boot_reset(bcmtch_data_ptr);

		if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE)
			ret_val = bcmtch_state_sleep(bcmtch_data_ptr);
		else
			ret_val = bcmtch_dev_set_power_state(
				bcmtch_data_ptr,
				BCMTCH_POWER_STATE_SLEEP);

		bcmtch_dev_power_enable(bcmtch_data_ptr, false);

		/* process worker reset */
		bcmtch_data_ptr->work_process_index = BCMTCH_WP_CHANNEL;
	} else {
		if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE)
			ret_val = bcmtch_state_suspend(bcmtch_data_ptr);
		else
			/* suspend */
			ret_val =
				bcmtch_dev_request_power_mode(
					bcmtch_data_ptr,
					BCMTCH_POWER_MODE_NOWAKE,
					TOFE_COMMAND_POWER_MODE_SUSPEND);
	}

	/* clear events */
	bcmtch_dev_reset_events(bcmtch_data_ptr);

	/* unlock */
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	dev_dbg(bcmtch_data_ptr->p_device,
		"PM:[%x]:%s() - 0x%x\n",
		BCMTCH_DF_PM,
		__func__,
		ret_val);

	return ret_val;
}

static int bcmtch_dev_resume(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	/* check suspend status */
	spin_lock(&bcmtch_data_ptr->lock_suspend);
	if (!bcmtch_data_ptr->suspend) {
		spin_unlock(&bcmtch_data_ptr->lock_suspend);
		return 0; /* Not suspended */
	}

	/* update suspend status */
	bcmtch_data_ptr->suspend = false;
	spin_unlock(&bcmtch_data_ptr->lock_suspend);

	/* lock */
	mutex_lock(&bcmtch_data_ptr->mutex_work);

	if (bcmtch_boot_flag & BCMTCH_BF_SUSPEND_COLD_BOOT) {

		ret_val = bcmtch_dev_power_enable(
					bcmtch_data_ptr,
					true);
		if (ret_val)
			goto dev_resume_error;

		/* reset the chip on driver load ? */
		if (bcmtch_data_ptr->gpio_hw_reset) {
			bcmtch_dev_reset(
				bcmtch_data_ptr,
				BCMTCH_RESET_MODE_HARD);
		} else {
			ret_val = bcmtch_dev_reset(
				bcmtch_data_ptr,
				BCMTCH_RESET_MODE_SOFT_CHIP |
				BCMTCH_RESET_MODE_SOFT_ARM);
			if (ret_val) {
				dev_err(bcmtch_data_ptr->p_device,
						"%s soft reset error!\n",
						__func__);
				goto dev_resume_error;
			}
		}

		ret_val = bcmtch_dev_init(bcmtch_data_ptr);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"%s() device init error! (%d)\n",
					__func__,
					ret_val);
			goto dev_resume_error;
		}
	} else {
		if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE) {
			ret_val = bcmtch_state_resume(
						bcmtch_data_ptr);
			if (ret_val) {
				dev_err(bcmtch_data_ptr->p_device,
						"%s() state protocol resume failed! (%d)\n",
						__func__,
						ret_val);
				goto dev_resume_error;
			}
		} else {
			ret_val = bcmtch_dev_request_power_mode(
						bcmtch_data_ptr,
						BCMTCH_POWER_MODE_WAKE,
						TOFE_COMMAND_NO_COMMAND);
			if (ret_val) {
				dev_err(bcmtch_data_ptr->p_device,
						"%s() failed to power wake up the chip! (%d)\n",
						__func__,
						ret_val);
				goto dev_resume_error;
			}

			ret_val = bcmtch_dev_check_power_state(
						bcmtch_data_ptr,
						BCMTCH_POWER_STATE_ACTIVE,
						25);
			if (ret_val) {
				dev_err(bcmtch_data_ptr->p_device,
						"%s() failed to set the chip to active mode! (%d)\n",
						__func__,
						ret_val);
				goto dev_resume_error;
			}

			ret_val = bcmtch_dev_wait_for_firmware_ready(
						bcmtch_data_ptr,
						BCMTCH_FW_READY_WAIT);
			if (ret_val) {
				dev_err(bcmtch_data_ptr->p_device,
						"%s() firmware ready timeout! (%d)\n",
						__func__,
						ret_val);
				goto dev_resume_error;
			}
		}
	}

	bcmtch_dev_watchdog_start(bcmtch_data_ptr);

	ret_val = bcmtch_interrupt_enable(bcmtch_data_ptr);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() interrupt enable error! (%d)\n",
				__func__,
				ret_val);
		goto dev_resume_error_exit;
	}

	if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE) {
		ret_val = bcmtch_state_start_scan(bcmtch_data_ptr);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"%s() ST: start SCAN error! (%d)\n",
					__func__,
					ret_val);
			goto dev_resume_error_exit;
		}
	}

	/* unlock */
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	dev_dbg(bcmtch_data_ptr->p_device,
		"PM:[%x]:%s() - 0x%x\n",
		BCMTCH_DF_PM,
		__func__,
		ret_val);

	return ret_val;

dev_resume_error:
	/* start watchdog */
	bcmtch_dev_watchdog_start(bcmtch_data_ptr);

dev_resume_error_exit:
	/* Reset suspend status */
	spin_lock(&bcmtch_data_ptr->lock_suspend);
	bcmtch_data_ptr->suspend = true;
	spin_unlock(&bcmtch_data_ptr->lock_suspend);

	/* unlock */
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

	return ret_val;
}

void bcmtch_dev_watchdog_work(unsigned long int data)
{
	struct bcmtch_data *bcmtch_data_ptr =
		(struct bcmtch_data *)data;

	dev_dbg(bcmtch_data_ptr->p_device,
		"WD:[%x]:\n",
		BCMTCH_DF_WD);

	/* queue the interrupt handler */
	queue_work(
		bcmtch_data_ptr->p_workqueue,
		(struct work_struct *)&bcmtch_data_ptr->work);

	bcmtch_dev_watchdog_reset(bcmtch_data_ptr);
}

static void bcmtch_dev_watchdog_start(
		struct bcmtch_data *bcmtch_data_ptr)
{
	if (timer_pending(&bcmtch_data_ptr->watchdog))
		del_timer_sync(&bcmtch_data_ptr->watchdog);

	init_timer(&bcmtch_data_ptr->watchdog);

	bcmtch_data_ptr->watchdog_expires =
			(bcmtch_data_ptr->post_boot_sections) ?
			msecs_to_jiffies(bcmtch_watchdog_post_boot) :
			msecs_to_jiffies(bcmtch_watchdog_normal);

	bcmtch_data_ptr->watchdog.function = bcmtch_dev_watchdog_work;
	bcmtch_data_ptr->watchdog.data = (unsigned long)bcmtch_data_ptr;
	bcmtch_data_ptr->watchdog.expires =
			jiffies + bcmtch_data_ptr->watchdog_expires;

	add_timer(&bcmtch_data_ptr->watchdog);
}

static int bcmtch_dev_watchdog_restart(
		struct bcmtch_data *bcmtch_data_ptr,
		uint32_t expires)
{
	int ret_val = 0;

	bcmtch_data_ptr->watchdog_expires = expires;

	ret_val = mod_timer(
			&bcmtch_data_ptr->watchdog,
			(jiffies + bcmtch_data_ptr->watchdog_expires));

	return ret_val;
}

static int bcmtch_dev_watchdog_reset(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	ret_val = mod_timer(
			&bcmtch_data_ptr->watchdog,
			(jiffies + bcmtch_data_ptr->watchdog_expires));

	return ret_val;
}

static int bcmtch_dev_watchdog_stop(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	if (bcmtch_data_ptr) {
		ret_val = del_timer_sync(&bcmtch_data_ptr->watchdog);
		bcmtch_data_ptr->watchdog_expires = 0;
	}

	return ret_val;
}

static int bcmtch_dev_reset_fw(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	/* disable interrupts */
	bcmtch_interrupt_disable(bcmtch_data_ptr);

	/* free watchdog timer */
	bcmtch_dev_watchdog_stop(bcmtch_data_ptr);

	/* post boot reset */
	bcmtch_dev_post_boot_reset(bcmtch_data_ptr);

	/* cycle power - off */
	bcmtch_dev_power_enable(
			bcmtch_data_ptr,
			false);

	/* pause */
	msleep(100);

	/* cycle power - on */
	ret_val = bcmtch_dev_power_enable(
			bcmtch_data_ptr,
			true);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"WG: %s power enable error!\n",
				__func__);
		goto wdg_reset_error;
	}

	/* reset the chip on driver load ? */
	if (bcmtch_data_ptr->gpio_hw_reset) {
		bcmtch_dev_reset(
			bcmtch_data_ptr,
			BCMTCH_RESET_MODE_HARD);
	} else {
		ret_val = bcmtch_dev_reset(
			bcmtch_data_ptr,
			BCMTCH_RESET_MODE_SOFT_CHIP |
			BCMTCH_RESET_MODE_SOFT_ARM);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"WG: %s soft reset error!\n",
					__func__);
			goto wdg_reset_error;
		}
	}

	/* clear events */
	bcmtch_dev_reset_events(bcmtch_data_ptr);

	/* init touch controller */
	ret_val = bcmtch_dev_init(bcmtch_data_ptr);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"WG: %s dev_init error!\n",
				__func__);
		/* restart watchdog */
		bcmtch_dev_watchdog_start(bcmtch_data_ptr);
		goto wdg_reset_error;
	}

	/* restart watchdog */
	bcmtch_dev_watchdog_start(bcmtch_data_ptr);

	/* re-enable interrupts */
	ret_val =
		bcmtch_interrupt_enable(bcmtch_data_ptr);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"WG: %s enable interrupt error!\n",
				__func__);
		goto wdg_reset_error;
	}

	/* Clear suspend status */
	spin_lock(&bcmtch_data_ptr->lock_suspend);
	bcmtch_data_ptr->suspend = false;
	spin_unlock(&bcmtch_data_ptr->lock_suspend);

wdg_reset_error:
	return ret_val;
}

static int bcmtch_dev_watchdog_check(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	uint8_t m2h;

	dev_dbg(bcmtch_data_ptr->p_device,
		"WD:[%x]:Watch DOG\n",
		BCMTCH_DF_WD);

	/* If it is non-state protocol */
	if (bcmtch_data_ptr->work_process_index < BCMTCH_WP_STATE) {
		/* Check FW status */
		ret_val = bcmtch_com_read_spm(
					bcmtch_data_ptr,
					BCMTCH_SPM_REG_MSG_TO_HOST,
					&m2h);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"WG:%s SPM read error! [%d]\n",
					__func__,
					ret_val);
			goto watch_dog_reset;
		}

		if (!IS_VALID_TOFE_MESSAGE(m2h)) {
			dev_err(bcmtch_data_ptr->p_device,
					"WG:%s invalid FW status [0x%0x]\n",
					__func__,
					m2h);
			bcmtch_state_check_fw_exception(
				bcmtch_data_ptr,
				m2h);
			goto watch_dog_reset;
		}
		return ret_val;
	}

	/* Test stop command */
	bcmtch_state_stop_scan(bcmtch_data_ptr);
	ret_val =
		bcmtch_state_check_command_status(bcmtch_data_ptr, 25);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"WG:%s check stop failure [%d]\n",
				__func__,
				ret_val);
		goto watch_dog_reset;
	}

	/* Test start command */
	bcmtch_state_start_scan(bcmtch_data_ptr);
	ret_val =
		bcmtch_state_check_command_status(
				bcmtch_data_ptr, 10);
	if (ret_val ||
		(bcmtch_data_ptr->bcmtch_state_resp_buffer[0]
			< TOFE_MESSAGE_SUCCESS)) {
		dev_err(bcmtch_data_ptr->p_device,
				"WG:%s check start failure [%d]\n",
				__func__,
				ret_val);
		goto watch_dog_reset;
	}

	return ret_val;

watch_dog_reset:
	/* Recover FW */
	if (bcmtch_boot_flag & BCMTCH_BF_FW_RESET_ON_WD) {
		dev_err(bcmtch_data_ptr->p_device,
			"WG: watch dog reset [%d]\n",
			ret_val);
		ret_val = bcmtch_dev_reset_fw(bcmtch_data_ptr);
	}

	return ret_val;
}

static int bcmtch_dev_power_init(
		struct bcmtch_data *p_bcmtch_data)
{
	int ret_val = 0;

	p_bcmtch_data->regulator_avdd33 =
			regulator_get(
				p_bcmtch_data->p_device,
				"key_led");
	if (IS_ERR(p_bcmtch_data->regulator_avdd33))
		p_bcmtch_data->regulator_avdd33 = NULL;

	p_bcmtch_data->regulator_vddo =
			regulator_get(
				p_bcmtch_data->p_device,
				"vtsp_3v");
	if (IS_ERR(p_bcmtch_data->regulator_vddo))
		p_bcmtch_data->regulator_vddo = NULL;

	p_bcmtch_data->regulator_avdd_adldo = NULL;
#if 0

	p_bcmtch_data->regulator_avdd_adldo =
			regulator_get(
				p_bcmtch_data->p_device,
				"avdd_adldo");
	if (IS_ERR(p_bcmtch_data->regulator_avdd_adldo))
		p_bcmtch_data->regulator_avdd_adldo = NULL;
#endif
	dev_dbg(p_bcmtch_data->p_device,
		"PM:[%x]:%s() %x %x %x\n",
		BCMTCH_DF_PM,
		__func__,
		(uint32_t)p_bcmtch_data->regulator_avdd33,
		(uint32_t)p_bcmtch_data->regulator_vddo,
		(uint32_t)p_bcmtch_data->regulator_avdd_adldo);

	return ret_val;
}

static int bcmtch_dev_power_enable(
		struct bcmtch_data *p_bcmtch_data,
		bool enable)
{
	int ret_val = 0;

	if (enable) {
		if (p_bcmtch_data->regulator_avdd33) {
			ret_val = regulator_enable(p_bcmtch_data
						->regulator_avdd33);
			if (ret_val) {
				pr_err("BCMTCH: %s() enable regulator avdd33 error %d.\n",
						__func__,
						ret_val);
				goto pwr_switch_error;
			}

			ret_val = regulator_set_voltage(p_bcmtch_data->regulator_avdd33,
					3300000, 3300000);
			dev_info(p_bcmtch_data->p_device,
					"[TSP] 3.3v regulator_set_voltage ret_val = %d\n",
					ret_val);
			if (ret_val) {
				pr_err("[TSP] can't set voltage TSP AVDD 3.3V,ret_val = %d\n", ret_val);
				regulator_put(p_bcmtch_data->regulator_avdd33);
				p_bcmtch_data->regulator_avdd33 = NULL;
				goto pwr_switch_error;
			}
		}

		if (p_bcmtch_data->regulator_vddo) {
			ret_val = regulator_enable(p_bcmtch_data
						->regulator_vddo);
			if (ret_val) {
				pr_err("BCMTCH: %s() enable regulator vddo error %d.\n",
						__func__,
						ret_val);
				goto err_enable_vddo;
			}

			ret_val = regulator_set_voltage(p_bcmtch_data->regulator_vddo,
							3000000, 3000000);
			dev_info(p_bcmtch_data->p_device,
					"[TSP] 3.0v regulator_set_voltage ret_val = %d\n",
					ret_val);
			if (ret_val) {
				pr_err("[TSP] can't set voltage TSP VDDO 3.0V,ret_val = %d\n", ret_val);
				regulator_put(p_bcmtch_data->regulator_vddo);
				p_bcmtch_data->regulator_vddo = NULL;
				goto err_enable_vddo;
			}
		}

		if (p_bcmtch_data->regulator_avdd_adldo) {
			ret_val = regulator_enable(p_bcmtch_data
						->regulator_avdd_adldo);
			if (ret_val) {
				pr_err("BCMTCH: %s() enable regulator adldo error %d.\n",
						__func__,
						ret_val);
				goto err_enable_adldo;
			}
		}

		if (p_bcmtch_data->platform_data.power_on_delay_us)
			usleep_range(
				p_bcmtch_data->
					platform_data.power_on_delay_us - 500,
				p_bcmtch_data->
					platform_data.power_on_delay_us + 500);

		return 0;

err_enable_adldo:
		if (p_bcmtch_data->regulator_vddo)
			regulator_disable(
				p_bcmtch_data->regulator_vddo);
err_enable_vddo:
		if (p_bcmtch_data->regulator_avdd33)
			regulator_disable(
				p_bcmtch_data->regulator_avdd33);

	} else {

		if (p_bcmtch_data->regulator_avdd33 &&
				regulator_is_enabled(p_bcmtch_data->
					regulator_avdd33) > 0) {
			ret_val = regulator_disable(
					p_bcmtch_data->regulator_avdd33);
			if (ret_val) {
				pr_err("BCMTCH: %s() regulator avdd33 disable error %d.\n",
						__func__,
						ret_val);
				goto pwr_switch_error;
			}
		}

		if (p_bcmtch_data->regulator_vddo &&
				regulator_is_enabled(p_bcmtch_data->
					regulator_vddo) > 0) {
			ret_val = regulator_disable(
					p_bcmtch_data->regulator_vddo);
			if (ret_val) {
				pr_err("BCMTCH: %s() regulator vddo disable error %d.\n",
						__func__,
						ret_val);
				goto pwr_switch_error;
			}
		}

		if (p_bcmtch_data->regulator_avdd_adldo &&
				regulator_is_enabled(p_bcmtch_data->
					regulator_avdd_adldo) > 0) {
			ret_val = regulator_disable(
					p_bcmtch_data->regulator_avdd_adldo);
			if (ret_val) {
				pr_err("BCMTCH: %s() regulator adldo disable error %d.\n",
						__func__,
						ret_val);
				goto pwr_switch_error;
			}
		}

		return 0;
	}

pwr_switch_error:
	return ret_val;
}

static int bcmtch_dev_power_free(
		struct bcmtch_data *p_bcmtch_data)
{
	int ret_val = 0;

	if (p_bcmtch_data->regulator_avdd33)
		regulator_put(p_bcmtch_data->regulator_avdd33);

	if (p_bcmtch_data->regulator_vddo)
		regulator_put(p_bcmtch_data->regulator_vddo);

	if (p_bcmtch_data->regulator_avdd_adldo)
		regulator_put(p_bcmtch_data->regulator_avdd_adldo);

	p_bcmtch_data->regulator_avdd33 = NULL;
	p_bcmtch_data->regulator_vddo = NULL;
	p_bcmtch_data->regulator_avdd_adldo = NULL;

	return ret_val;
}

static int bcmtch_dev_read_otp(
		struct bcmtch_data *bcmtch_data_ptr,
		uint32_t in_addr,
		uint32_t *read_data)
{
	int ret_val = 0;
	int i;
	uint32_t otp_reader, done;

	if (!bcmtch_data_ptr) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() error, bcmtch_data_ptr == NULL\n",
				__func__);
		return -EINVAL;
	}

	ret_val = bcmtch_com_write_sys32(
				bcmtch_data_ptr,
				BCMTCH_ADDR_COMMON_OTP_CPU_CTRL0,
				0x0);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() write 0 OTP_CPU_CTRL0 error! (%d)\n",
				__func__,
				ret_val);
		return ret_val;
	}

	ret_val = bcmtch_com_write_sys32(
				bcmtch_data_ptr,
				BCMTCH_ADDR_COMMON_OTP_CPU_ADDRESS,
				in_addr);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() write OTP_CPU_ADDRESS error! (%d)\n",
				__func__,
				ret_val);
		return ret_val;
	}

	ret_val = bcmtch_com_write_sys32(
				bcmtch_data_ptr,
				BCMTCH_ADDR_COMMON_OTP_CPU_CTRL0,
				0x00000001);
	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() write 1 OTP_CPU_CTRL0 error! (%d)\n",
				__func__,
				ret_val);
		return ret_val;
	}

	/* wait until command is done */
	i = 0;
	do {
		ret_val = bcmtch_com_read_sys(
				bcmtch_data_ptr,
				BCMTCH_ADDR_COMMON_OTP_CPU_STATUS,
				4,
				(uint8_t *)&done);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"%s() read OTP_CPU_STATUS error! (%d)\n",
					__func__,
					ret_val);
			return ret_val;
		}

		if (done)
			break;

		usleep_range(1500, 2000);
	} while (i++ < 10);

	if (done) {
		ret_val = bcmtch_com_read_sys(
				bcmtch_data_ptr,
				BCMTCH_ADDR_COMMON_OTP_CPU_WORD_RD,
				4,
				(uint8_t *)&otp_reader);
		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"%s() read OTP_CPU_WORD_RD error! (%d)\n",
					__func__,
					ret_val);
			return ret_val;
		}

		*read_data =
			otp_reader & BCMTCH_OTP_DATA_MASK;
	} else {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() read OTP timeout.\n",
				__func__);
		return -ENODATA;
	}

	return ret_val;
}


/* -------------------------------------------------- */
/* - BCM Touch Controller Com(munication) Functions - */
/* -------------------------------------------------- */

static int bcmtch_com_init(
	struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	if (BCMTCH_DEFAULT_I2C_ADDR_SYS !=
		bcmtch_data_ptr->platform_data.i2c_addr_sys)
		ret_val = bcmtch_com_write_spm(
				bcmtch_data_ptr,
				BCMTCH_SPM_REG_I2CS_CHIPID,
				bcmtch_data_ptr->platform_data.i2c_addr_sys);

	return ret_val;
}

static inline int bcmtch_com_read_spm(
		struct bcmtch_data *bcmtch_data_ptr,
		uint8_t reg, uint8_t *data)
{
	return bcmtch_i2c_read_spm(
				bcmtch_data_ptr->p_i2c_client_spm,
				reg,
				data);
}

static inline int bcmtch_com_write_spm(
		struct bcmtch_data *bcmtch_data_ptr,
		uint8_t reg, uint8_t data)
{
	return bcmtch_i2c_write_spm(
				bcmtch_data_ptr->p_i2c_client_spm,
				reg,
				data);
}

static inline int bcmtch_com_fast_write_spm(
		struct bcmtch_data *bcmtch_data_ptr,
		uint8_t count, uint8_t *regs, uint8_t *data)
{
	return bcmtch_i2c_fast_write_spm(
				bcmtch_data_ptr->p_i2c_client_spm,
				count,
				regs,
				data);
}

static inline int bcmtch_com_read_sys(
		struct bcmtch_data *bcmtch_data_ptr,
		uint32_t sys_addr,
		uint16_t read_len,
		uint8_t *read_data)
{
	return bcmtch_i2c_read_sys(
				bcmtch_data_ptr->p_i2c_client_sys,
				sys_addr,
				read_len,
				read_data);
}

static inline int bcmtch_com_write_sys32(
		struct bcmtch_data *bcmtch_data_ptr,
		uint32_t sys_addr,
		uint32_t write_data)
{
	return bcmtch_com_write_sys(bcmtch_data_ptr,
			sys_addr, 4, (uint8_t *)&write_data);
}

static inline int bcmtch_com_write_sys(
		struct bcmtch_data *bcmtch_data_ptr,
		uint32_t sys_addr,
		uint16_t write_len,
		uint8_t *write_data)
{
	return bcmtch_i2c_write_sys(
				bcmtch_data_ptr->p_i2c_client_sys,
				sys_addr,
				write_len,
				write_data);
}

static inline int bcmtch_com_read_dma(
		struct bcmtch_data *bcmtch_data_ptr,
		uint16_t read_len,
		uint8_t *read_data)
{
	return bcmtch_i2c_read_dma(
				bcmtch_data_ptr->p_i2c_client_sys,
				read_len,
				read_data);
}

/* ------------------------------------- */
/* - BCM Touch Controller OS Functions - */
/* ------------------------------------- */
static irqreturn_t bcmtch_interrupt_handler(int32_t irq, void *dev_id)
{
	struct bcmtch_data *bcmtch_data_ptr =
		(struct bcmtch_data *)dev_id;

	/* track interrupts */
	bcmtch_data_ptr->irq_pending = true;

	dev_dbg(bcmtch_data_ptr->p_device,
		"IH:[%x]:p=%d\n",
		BCMTCH_DF_IH,
		bcmtch_data_ptr->irq_pending);

	/* queue the interrupt handler */
	queue_work(
		bcmtch_data_ptr->p_workqueue,
		(struct work_struct *)&bcmtch_data_ptr->work);

	/* reset watchdog */
	bcmtch_dev_watchdog_reset(bcmtch_data_ptr);

	return IRQ_HANDLED;
}

static int bcmtch_interrupt_enable(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	struct bcmtch_platform_data *p_data;

	if (bcmtch_data_ptr->irq_enabled)
		return ret_val;

	p_data = &bcmtch_data_ptr->platform_data;

	ret_val = request_irq(
				p_data->touch_irq,
				bcmtch_interrupt_handler,
				p_data->gpio_interrupt_trigger,
				BCMTCH15XXX_NAME,
				bcmtch_data_ptr);

	if (ret_val) {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() - Unable to request interrupt irq %d\n",
				__func__,
				p_data->touch_irq);

		/* note :
		* - polling is not enabled in this release
		* - it is an error if an irq is requested
		*	and not granted
		*/
	} else
		bcmtch_data_ptr->irq_enabled = true;

	return ret_val;
}

static void bcmtch_interrupt_disable(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int32_t irq;

	if (bcmtch_data_ptr && bcmtch_data_ptr->irq_enabled) {
		irq = bcmtch_data_ptr->platform_data.touch_irq;
		if (irq) {
			free_irq(irq, bcmtch_data_ptr);
			bcmtch_data_ptr->irq_enabled = false;
		}
	}
}

static int bcmtch_init_input_device(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;
	int32_t button_init = 0;

	if (bcmtch_data_ptr) {
		bcmtch_data_ptr->p_input_device = input_allocate_device();
		if (bcmtch_data_ptr->p_input_device) {

			bcmtch_data_ptr->p_input_device->name =
				"BCMTCH15xxx Touch Screen";

			bcmtch_data_ptr->p_input_device->phys =
				"I2C";
			bcmtch_data_ptr->p_input_device->id.bustype =
				BUS_I2C;
			bcmtch_data_ptr->p_input_device->id.vendor =
				BCMTCH_VENDOR_ID;
			bcmtch_data_ptr->p_input_device->id.product =
				((bcmtch_data_ptr->chip_id
					& BCMTCH_CHIPID_PID_MASK)
				>> BCMTCH_CHIPID_PID_SHIFT);
			bcmtch_data_ptr->p_input_device->id.version =
				bcmtch_data_ptr->rev_id;

			set_bit(EV_SYN, bcmtch_data_ptr->p_input_device->evbit);
			set_bit(EV_ABS, bcmtch_data_ptr->p_input_device->evbit);
			__set_bit(
				INPUT_PROP_DIRECT,
				bcmtch_data_ptr->p_input_device->propbit);

			set_bit(EV_KEY,
				bcmtch_data_ptr->p_input_device->evbit);
			set_bit(
				BTN_TOUCH,
				bcmtch_data_ptr->p_input_device->keybit);

			while (button_init < bcmtch_data_ptr->
					platform_data.ext_button_count) {
				set_bit(
					bcmtch_data_ptr->platform_data.
						ext_button_map[button_init],
					bcmtch_data_ptr->
						p_input_device->keybit);
				button_init++;
			}

			input_set_abs_params(
				bcmtch_data_ptr->p_input_device,
				ABS_MT_POSITION_X,
				bcmtch_data_ptr->axis_x_min,
				bcmtch_data_ptr->axis_x_max,
				0,
				0);

			input_set_abs_params(
				bcmtch_data_ptr->p_input_device,
				ABS_MT_POSITION_Y,
				bcmtch_data_ptr->axis_y_min,
				bcmtch_data_ptr->axis_y_max,
				0,
				0);


			if (bcmtch_event_flag &
					BCMTCH_EVENT_FLAG_TOUCH_SIZE) {
				input_set_abs_params(
					bcmtch_data_ptr->p_input_device,
					ABS_MT_TOUCH_MAJOR,
					bcmtch_data_ptr->axis_h_min,
					bcmtch_data_ptr->axis_h_max,
					0,
					0);

				input_set_abs_params(
					bcmtch_data_ptr->p_input_device,
					ABS_MT_TOUCH_MINOR,
					bcmtch_data_ptr->axis_h_min,
					bcmtch_data_ptr->axis_h_max,
					0,
					0);
			}

			if (bcmtch_event_flag &
					BCMTCH_EVENT_FLAG_PRESSURE) {
				input_set_abs_params(
					bcmtch_data_ptr->p_input_device,
					ABS_MT_PRESSURE,
					0,
					BCMTCH_MAX_PRESSURE,
					0,
					0);
			}

			if (bcmtch_event_flag &
					BCMTCH_EVENT_FLAG_ORIENTATION) {
				input_set_abs_params(
					bcmtch_data_ptr->p_input_device,
					ABS_MT_ORIENTATION,
					BCMTCH_MIN_ORIENTATION,
					BCMTCH_MAX_ORIENTATION,
					0,
					0);
			}

			set_bit(
				BTN_TOOL_FINGER,
				bcmtch_data_ptr->p_input_device->keybit);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
			input_mt_init_slots(
				bcmtch_data_ptr->p_input_device,
				BCMTCH_MAX_TOUCH);
#else
			input_mt_init_slots(
				bcmtch_data_ptr->p_input_device,
				BCMTCH_MAX_TOUCH,
				0);
#endif
			/* request new os input queue size for this device */
			input_set_events_per_packet(
				bcmtch_data_ptr->p_input_device,
				50 * BCMTCH_MAX_TOUCH);

			/* register device */
			ret_val =
				input_register_device(
					bcmtch_data_ptr->p_input_device);

			if (ret_val) {
				dev_err(bcmtch_data_ptr->p_device,
					"ERROR:%s() Unable to register input device\n",
					__func__);

				input_free_device(
					bcmtch_data_ptr->p_input_device);
				bcmtch_data_ptr->p_input_device = NULL;
			}
		} else {
			dev_err(bcmtch_data_ptr->p_device,
					"%s() Unable to create device\n",
					__func__);

			ret_val = -ENODEV;
		}
	} else {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() error, driver data structure == NULL\n",
				__func__);

		ret_val = -ENODATA;
	}

	return ret_val;
}

static inline void bcmtch_free_input_device(
		struct bcmtch_data *bcmtch_data_ptr)
{
	if (bcmtch_data_ptr && bcmtch_data_ptr->p_input_device) {
		input_unregister_device(bcmtch_data_ptr->p_input_device);
		bcmtch_data_ptr->p_input_device = NULL;
	}
}

static bool bcmtch_dev_process_post_boot(
		struct bcmtch_data *bcmtch_data_ptr,
		bool irq_serviced)
{
	int32_t still_downloading;
	int ret_val;

	if (irq_serviced) {
		/* download shorter packet */
		still_downloading =
				bcmtch_dev_post_boot_download(
					bcmtch_data_ptr,
					bcmtch_post_boot_rate_low);
	} else {
		/* download larger packet */
		still_downloading =
				bcmtch_dev_post_boot_download(
					bcmtch_data_ptr,
					bcmtch_post_boot_rate_high);
	}

	if (still_downloading < 0)
		return false;

	if (!still_downloading) {
		dev_dbg(bcmtch_data_ptr->p_device,
			"PB:[%x]:DOWNLOAD COMPLETE\n",
			BCMTCH_DF_PB);


		if (bcmtch_data_ptr->post_boot_cfg_length) {
			/* don't want any stray interrupts */
			bcmtch_interrupt_disable(bcmtch_data_ptr);

			/* Send post boot init command */
			ret_val = bcmtch_dev_send_command(
					bcmtch_data_ptr,
					TOFE_COMMAND_INIT_POST_BOOT_PATCHES,
					bcmtch_data_ptr->post_boot_cfg_addr,
					bcmtch_data_ptr->post_boot_cfg_length,
					0);
			if (ret_val != 0) {
				dev_err(bcmtch_data_ptr->p_device,
					" send_command error [%d] cmd=%x\n",
					ret_val,
					TOFE_COMMAND_INIT_POST_BOOT_PATCHES);
			}

			/* Set to check post boot init request from FW */
			bcmtch_data_ptr->post_boot_pending = 1;

			/* Set the worker process function to the transient
			 * one which handles the ISR workqueue during
			 * the switch from ROM FW to post boot patched FW.
			 */
			bcmtch_data_ptr->work_process_index =
					BCMTCH_WP_PATCH_INIT;

			bcmtch_dev_watchdog_restart(
				bcmtch_data_ptr,
				msecs_to_jiffies(bcmtch_watchdog_normal));

			bcmtch_interrupt_enable(bcmtch_data_ptr);
		}

		/* Free post boot buffer memory */
		vfree(bcmtch_data_ptr->post_boot_buffer);
		bcmtch_data_ptr->post_boot_buffer = NULL;

		return true;
	}

	return false;
}


static void bcmtch_deferred_worker(struct work_struct *work)
{
	struct bcmtch_data *bcmtch_data_ptr =
		container_of(work, struct bcmtch_data, work);
	bool work_done = false;

	mutex_lock(&bcmtch_data_ptr->mutex_work);

	if (bcmtch_data_ptr->irq_pending) {
		bcmtch_data_ptr->irq_pending = false;

		/* Process channels */
		bcmtch_data_ptr->bcmtch_dev_process_table
			[bcmtch_data_ptr->work_process_index]
				(bcmtch_data_ptr);

		work_done = true;
	}

	/* Amortized post boot download */
	if (bcmtch_data_ptr->post_boot_sections) {
		bcmtch_dev_process_post_boot(
			bcmtch_data_ptr,
			work_done);

		work_done = true;
	}

	if (!work_done)
		bcmtch_dev_watchdog_check(bcmtch_data_ptr);

	mutex_unlock(&bcmtch_data_ptr->mutex_work);
}

static unsigned int bcmtch_dev_post_boot_get_section(
	struct bcmtch_data *bcmtch_data_ptr)
{
	struct combi_entry *pb_entry = NULL;

	if (bcmtch_data_ptr->post_boot_sections &&
			bcmtch_data_ptr->post_boot_buffer) {
		pb_entry = (struct combi_entry *)
			bcmtch_data_ptr->fw_screened_headers;

		bcmtch_data_ptr->post_boot_data = NULL;
		bcmtch_data_ptr->post_boot_addr = 0;
		bcmtch_data_ptr->post_boot_left = 0;

		while (pb_entry[bcmtch_data_ptr->post_boot_section].length) {
			if (pb_entry[bcmtch_data_ptr->post_boot_section].flags &
					BCMTCH_FIRMWARE_FLAGS_POST_BOOT) {
				bcmtch_data_ptr->post_boot_data =
					bcmtch_data_ptr->post_boot_buffer
					+ pb_entry[bcmtch_data_ptr->
					post_boot_section].offset;

				bcmtch_data_ptr->post_boot_addr =
					pb_entry[bcmtch_data_ptr->
					post_boot_section].addr;

				bcmtch_data_ptr->post_boot_left =
					pb_entry[bcmtch_data_ptr->
					post_boot_section].length;

				break;
			} else {
				bcmtch_data_ptr->post_boot_section++;
			}
		}

		return bcmtch_data_ptr->post_boot_left;
	}

	return 0;
}

static int bcmtch_dev_post_boot_download(
		struct bcmtch_data *bcmtch_data_ptr,
		int16_t data_rate)
{
	int ret_val;
	int16_t write_length;

	struct combi_entry *pb_entry = NULL;

	if (bcmtch_data_ptr->post_boot_left)	{
		write_length = data_rate;

		if (bcmtch_data_ptr->post_boot_left < write_length)
			write_length = bcmtch_data_ptr->post_boot_left;

		ret_val = bcmtch_com_write_sys(
					bcmtch_data_ptr,
					bcmtch_data_ptr->post_boot_addr,
					write_length,
					bcmtch_data_ptr->post_boot_data);

		if (ret_val) {
			dev_err(bcmtch_data_ptr->p_device,
					"%s() Error - did not download\n",
					__func__);
		} else {
			bcmtch_data_ptr->post_boot_addr += write_length;
			bcmtch_data_ptr->post_boot_left -= write_length;
			bcmtch_data_ptr->post_boot_data += write_length;
		}
	} else {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() Error - no bytes to download\n",
				__func__);
	}

	if (!bcmtch_data_ptr->post_boot_left) {

		pb_entry = (struct combi_entry *)
			bcmtch_data_ptr->post_boot_buffer;

		dev_dbg(bcmtch_data_ptr->p_device,
			"PB:[%x]:Section %d  Addr 0x%08x  Len %d\n",
			BCMTCH_DF_PB,
			bcmtch_data_ptr->post_boot_section,
			pb_entry[bcmtch_data_ptr->
			post_boot_section].addr,
			pb_entry[bcmtch_data_ptr->
			post_boot_section].length);

		if (pb_entry[bcmtch_data_ptr->post_boot_section].flags &
				BCMTCH_FIRMWARE_FLAGS_POST_BOOT_PATCH) {
			if (--bcmtch_data_ptr->post_boot_patches == 0) {
				/* send patch init */
				ret_val = bcmtch_dev_send_command(
					bcmtch_data_ptr,
					TOFE_COMMAND_INIT_POST_BOOT_PATCHES,
					0,
					0,
					0);
				if (ret_val != 0) {
					dev_err(bcmtch_data_ptr->p_device,
						" send_command error [%d] cmd=%x\n",
						ret_val,
					TOFE_COMMAND_INIT_POST_BOOT_PATCHES);
				}
			}
		}

		if (--bcmtch_data_ptr->post_boot_sections) {
			bcmtch_data_ptr->post_boot_section++;
			if (!bcmtch_dev_post_boot_get_section
					(bcmtch_data_ptr)) {
				dev_err(bcmtch_data_ptr->p_device,
					" post_boot_get_section ret=0 : [%d]\n",
					bcmtch_data_ptr->post_boot_sections);
				bcmtch_data_ptr->post_boot_sections = 0;
				return -EINVAL;
			}
		}
	}

	dev_dbg(bcmtch_data_ptr->p_device,
		"PB:[%x]:pb_sec=%d post_boot_left=%d.\n",
		BCMTCH_DF_PB,
		bcmtch_data_ptr->post_boot_sections,
		bcmtch_data_ptr->post_boot_left);

	return bcmtch_data_ptr->post_boot_sections;
}

static int bcmtch_init_deferred_worker(
		struct bcmtch_data *bcmtch_data_ptr)
{
	int ret_val = 0;

	if (bcmtch_data_ptr) {
		bcmtch_data_ptr->p_workqueue =
			create_workqueue("bcmtch_wq");

		if (bcmtch_data_ptr->p_workqueue) {

			INIT_WORK(
				&bcmtch_data_ptr->work,
				bcmtch_deferred_worker);

		} else {
			dev_err(bcmtch_data_ptr->p_device,
					"%s() Unable to create workqueue\n",
					__func__);

			ret_val = -ENOMEM;
		}
	} else {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() error, driver data structure == NULL\n",
				__func__);
		ret_val = -ENODATA;
	}

	return ret_val;
}

static void bcmtch_clear_deferred_worker(
		struct bcmtch_data *bcmtch_data_ptr)
{
	if (bcmtch_data_ptr && bcmtch_data_ptr->p_workqueue)
		flush_workqueue(bcmtch_data_ptr->p_workqueue);
}

static void bcmtch_free_deferred_worker(
		struct bcmtch_data *bcmtch_data_ptr)
{
	if (bcmtch_data_ptr && bcmtch_data_ptr->p_workqueue) {
		cancel_work_sync(&bcmtch_data_ptr->work);
		destroy_workqueue(bcmtch_data_ptr->p_workqueue);

		bcmtch_data_ptr->p_workqueue = NULL;
	}
}

static void bcmtch_reset(
		struct bcmtch_data *bcmtch_data_ptr)
{
	if (bcmtch_data_ptr &&
		bcmtch_data_ptr->gpio_hw_reset) {
		msleep(bcmtch_data_ptr->platform_data.gpio_reset_time_ms);

		gpio_set_value(
			bcmtch_data_ptr->platform_data.gpio_reset_pin,
			bcmtch_data_ptr->platform_data.gpio_reset_polarity);

		msleep(bcmtch_data_ptr->platform_data.gpio_reset_time_ms);

		gpio_set_value(
			bcmtch_data_ptr->platform_data.gpio_reset_pin,
			!bcmtch_data_ptr->platform_data.gpio_reset_polarity);

		msleep(bcmtch_data_ptr->platform_data.gpio_reset_time_ms);
	}
}

static int bcmtch_init_gpio(
		struct bcmtch_data *bcmtch_data_ptr)
{
	struct bcmtch_platform_data *p_platform_data;
	int ret_val = 0;

	p_platform_data =
		(struct bcmtch_platform_data *)&bcmtch_data_ptr->platform_data;

	/*
	*  setup a gpio pin for BCM Touch Controller reset function
	*/
	if (gpio_is_valid(p_platform_data->gpio_reset_pin)) {
		ret_val = gpio_request(
					p_platform_data->gpio_reset_pin,
					"BCMTCH reset");

		if (ret_val < 0) {
			dev_err(bcmtch_data_ptr->p_device,
				"ERROR: %s() - Unable to request reset pin %d\n",
				__func__,
				p_platform_data->gpio_reset_pin);

			/* note :
			* it is an error if a reset pin is requested
			* and not granted --> return
			*/
			return ret_val;
		}

		/*
		* setup reset pin as output
		* - invert reset polarity --> don't want to hold in reset
		*/
		ret_val = gpio_direction_output(
					p_platform_data->gpio_reset_pin,
					!p_platform_data->gpio_reset_polarity);

		if (ret_val < 0) {
			dev_err(bcmtch_data_ptr->p_device,
					"ERROR: %s() - Unable to set reset pin %d\n",
					__func__,
					p_platform_data->gpio_reset_pin);

			/* note :
			* it is an error if a reset pin is requested
			* and not set --> return
			*/
			bcmtch_data_ptr->gpio_hw_reset = false;
			return ret_val;
		} else
			bcmtch_data_ptr->gpio_hw_reset = true;
	} else {
		dev_info(bcmtch_data_ptr->p_device,
			"%s() : no reset pin configured\n",
			__func__);
	}

	/*
	* setup a gpio pin for BCM Touch Controller interrupt function
	*/
	if (gpio_is_valid(p_platform_data->gpio_interrupt_pin)) {
		ret_val = gpio_request(
					p_platform_data->gpio_interrupt_pin,
					"BCMTCH Interrupt");

		if (ret_val < 0) {
			dev_err(bcmtch_data_ptr->p_device,
				"ERROR: %s() - Unable to request interrupt pin %d\n",
				__func__,
				p_platform_data->gpio_interrupt_pin);

			/* note :
			* it is an error if an interrupt pin is requested
			* and not granted --> return
			*/
			return ret_val;
		}

		/* setup interrupt pin as input */
		ret_val = gpio_direction_input(
					p_platform_data->gpio_interrupt_pin);
		if (ret_val < 0) {
			dev_err(bcmtch_data_ptr->p_device,
				"ERROR: %s() - Unable to set interrupt pin %d\n",
				__func__,
				p_platform_data->gpio_interrupt_pin);

			/* note :
			 * it is an error if a interrupt pin is requested
			 * and not set --> return
			 */
			return ret_val;
		}
	} else if (!p_platform_data->touch_irq) {
		dev_err(bcmtch_data_ptr->p_device,
			"ERROR:%s(): no interrupt pin configured\n",
			__func__);
	}

	return ret_val;
}

static void bcmtch_free_gpio(
		struct bcmtch_data *bcmtch_data_ptr)
{
	struct bcmtch_platform_data *p_platform_data;

	if (bcmtch_data_ptr) {

		p_platform_data =
			(struct bcmtch_platform_data *)
				&bcmtch_data_ptr->platform_data;

		if (gpio_is_valid(p_platform_data->gpio_reset_pin))
			gpio_free(p_platform_data->gpio_reset_pin);
		bcmtch_data_ptr->gpio_hw_reset = false;

		if (gpio_is_valid(p_platform_data->gpio_interrupt_pin))
			gpio_free(p_platform_data->gpio_interrupt_pin);

	}
}

static int bcmtch_os_init_cli(struct device *p_device)
{
	int ret_val = 0;

	ret_val = device_create_file(p_device, &bcmtch_cli_attr);
	if (ret_val)
		dev_err(p_device,
				"ERROR: %s() - device_create_file() failed!\n",
				__func__);

	return ret_val;
}

static inline void bcmtch_os_free_cli(struct device *p_device)
{
	device_remove_file(p_device, &bcmtch_cli_attr);
}

/* ----------------------------------------- */
/* --- BCM Touch Controller PM Functions --- */
/* ----------------------------------------- */
#ifdef CONFIG_PM
#ifndef CONFIG_HAS_EARLYSUSPEND
static int bcmtch_suspend(struct i2c_client *p_client, pm_message_t mesg)
{
	if (mesg.event == PM_EVENT_SUSPEND)
		bcmtch_dev_suspend(i2c_get_clientdata(p_client));

	return 0;
}

static int bcmtch_resume(struct i2c_client *p_client)
{
	bcmtch_dev_resume(i2c_get_clientdata(p_client));

	return 0;
}
#endif
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bcmtch_early_suspend(struct early_suspend *h)
{
	struct bcmtch_data *local_bcmtch_data_p =
		container_of(
			h,
			struct bcmtch_data,
			bcmtch_early_suspend_desc);

	bcmtch_dev_suspend(local_bcmtch_data_p);
}

static void bcmtch_late_resume(struct early_suspend *h)
{
	struct bcmtch_data *bcmtch_data_ptr =
		container_of(h, struct bcmtch_data,
			bcmtch_early_suspend_desc);

	bcmtch_dev_resume(bcmtch_data_ptr);
}

static void bcmtch_register_early_suspend(
		struct bcmtch_data *bcmtch_data_ptr)
{
	/* Init early suspend parameters */
	bcmtch_data_ptr->bcmtch_early_suspend_desc.level =
		EARLY_SUSPEND_LEVEL_STOP_DRAWING;
	bcmtch_data_ptr->bcmtch_early_suspend_desc.suspend =
		bcmtch_early_suspend;
	bcmtch_data_ptr->bcmtch_early_suspend_desc.resume =
		bcmtch_late_resume;

	/* Register early suspend parameters */
	register_early_suspend(&bcmtch_data_ptr->
		bcmtch_early_suspend_desc);
}

static inline void bcmtch_unregister_early_suspend(
		struct bcmtch_data *bcmtch_data_ptr)
{

	/* Unregister early suspend parameters */
	unregister_early_suspend(&bcmtch_data_ptr->
		bcmtch_early_suspend_desc);
}
#endif

/* ----------------------------------------- */
/* -- BCM Touch Controller I2C Functions --- */
/* ----------------------------------------- */

static int bcmtch_i2c_transfer(
	struct i2c_adapter *adap,
	struct i2c_msg *msgs,
	int num)
{
	int msg_num = 0;

	while (msg_num < num) {
		if (i2c_transfer(adap, &msgs[msg_num], 1) != 1) {
			dev_err(bcmtch_data_ptr->p_device,
					"ERROR: %s() I2C transfer\n",
					__func__);

			return -EIO;
		}

		msg_num++;
	}

	return msg_num;
}

static int bcmtch_i2c_read_spm(
					struct i2c_client *p_i2c,
					uint8_t reg,
					uint8_t *data)
{
	int ret_val = 0;

	/* setup I2C messages for single byte read transaction */
	struct i2c_msg msg[2] = {
		/* first write register to spm */
		{.addr = p_i2c->addr, .flags = 0, .len = 1, .buf = &reg},
		/* Second read data from spm reg */
		{.addr = p_i2c->addr, .flags = I2C_M_RD, .len = 1, .buf = data}
	};

	if (bcmtch_i2c_transfer(p_i2c->adapter, msg, 2) != 2)
		ret_val = -EIO;

	return ret_val;
}

static int bcmtch_i2c_write_spm(
					struct i2c_client *p_i2c,
					uint8_t reg,
					uint8_t data)
{
	int ret_val = 0;

	/* setup buffer with reg address and data */
	uint8_t buffer[2] = { reg, data };

	/* setup I2C message for single byte write transaction */
	struct i2c_msg msg[1] = {
		/* first write message to spm */
		{.addr = p_i2c->addr, .flags = 0, .len = 2, .buf = buffer}
	};

	if (bcmtch_i2c_transfer(p_i2c->adapter, msg, 1) != 1)
		ret_val = -EIO;

	return ret_val;
}

static int bcmtch_i2c_fast_write_spm(
						struct i2c_client *p_i2c,
						uint8_t count,
						uint8_t *regs,
						uint8_t *data)
{
	int ret_val = 0;

	/*
	 * support hard-coded for a max of 5 spm write messages
	 *
	 * - 1 i2c message uses 2 uint8_t buffers
	 *
	 */
	uint8_t buffer[10];	/* buffers for reg address and data */
	struct i2c_msg msg[5];
	uint32_t n_msg = 0;
	uint8_t *buf_ptr = buffer;

	/* setup I2C message for single byte write transaction */
	while (n_msg < count) {
		msg[n_msg].addr = p_i2c->addr;
		msg[n_msg].flags = 0;
		msg[n_msg].len = 2;
		msg[n_msg].buf = buf_ptr;

		*buf_ptr++ = regs[n_msg];
		*buf_ptr++ = data[n_msg];
		n_msg++;
	}

	if (bcmtch_i2c_transfer(p_i2c->adapter, msg, n_msg) != n_msg)
		ret_val = -EIO;

	return ret_val;
}

static inline int bcmtch_i2c_send(
			struct bcmtch_data *bcmtch_data_ptr,
			struct i2c_adapter *adap,
			uint16_t addr,
			uint16_t len,
			unsigned char *buf)
{
	struct i2c_msg msg;

	msg.addr = addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = buf;

	if (bcmtch_i2c_transfer(adap, &msg, 1) != 1) {
		dev_err(bcmtch_data_ptr->p_device,
				"ERROR: %s() I2C transfer!\n",
				__func__);
		return -EIO;
	}
	return 0;
}

static int bcmtch_state_i2c_send_command(
			struct bcmtch_data *bcmtch_data_ptr,
			uint8_t cmd,
			uint8_t *params,
			int param_num)
{
	int ret_val = 0;

	struct i2c_client *i2c_spm_ptr =
			bcmtch_data_ptr->p_i2c_client_spm;
	struct i2c_client *i2c_sys_ptr =
			bcmtch_data_ptr->p_i2c_client_sys;
	uint8_t *i2c_buffer =
			bcmtch_data_ptr->bcmtch_state_cmd_buffer;

	uint8_t *cmd_buffer = &i2c_buffer[1];
	int	i, dma_len;

	/* set target register */
	i2c_buffer[0] = BCMTCH_SPM_REG_MSG_FROM_HOST;

	/* distinguish short/long command */
	if (param_num < TOFE_HOST_CMD_SHORT_SIZE) {
		/* set command */
		cmd_buffer[0] = cmd;

		/* set parameters */
		for (i = 0; i < param_num; i++)
			cmd_buffer[i+1] = params[i];

		/* padding */
		for (i = param_num; i < TOFE_HOST_CMD_SHORT_SIZE; i++)
			cmd_buffer[i+1] = 0;

		if (cmd == BCMTCH_CMD_RESUME)
			cmd_buffer[TOFE_HOST_CMD_SHORT_SIZE] =
				BCMTCH_POWER_MODE_WAKE;

		/* i2c write transaction */
		ret_val = bcmtch_i2c_send(
						bcmtch_data_ptr,
						i2c_spm_ptr->adapter,
						i2c_spm_ptr->addr,
						TOFE_HOST_CMD_SHORT_SIZE+2,
						i2c_buffer);
		if (ret_val)
			return ret_val;

		/* 500 us delay for FW to process the command */
		usleep_range(500, 1000);

	} else {
		dev_dbg(bcmtch_data_ptr->p_device,
			"ST:[%x]:%s long cmd=0x%0x param_num=%d\n",
			BCMTCH_DF_ST,
			__func__,
			cmd,
			param_num);

		/* Before sending a long command, we need to make sure the chip
		 * is in Active mode (which enables the I2C DMA). The Retention
		 * mode is used only in two modes: StartScan command (for power
		 * saving) and Suspend. Here we terminate the StartScan command
		 * in case it's pending.
		 */
		if (cmd_buffer[0] == BCMTCH_CMD_START_SCAN ||
				cmd_buffer[0] == BCMTCH_CMD_SINGLE_SCAN) {
			cmd_buffer[0] = 0;
			ret_val = bcmtch_state_stop_scan(bcmtch_data_ptr);
			if (ret_val)
				return ret_val;
		}

		cmd_buffer[0] = BCMTCH_CMD_LONG;
		for (i = 0; i < TOFE_HOST_CMD_SHORT_SIZE; i++)
			cmd_buffer[i+1] = 0;

		/* i2c write transaction */
		ret_val = bcmtch_i2c_send(
						bcmtch_data_ptr,
						i2c_spm_ptr->adapter,
						i2c_spm_ptr->addr,
						TOFE_HOST_CMD_SHORT_SIZE+2,
						i2c_buffer);
		if (ret_val)
			return ret_val;

		/* 500 us delay for FW to process the command */
		usleep_range(500, 1000);

		/* setup i2c messages for DMA write request transaction */
		i2c_buffer[0] = BCMTCH_SPM_REG_DMA_WFIFO;
		cmd_buffer[0] = cmd;
		memcpy(&cmd_buffer[1], params, param_num);
		dma_len = (((param_num+1) + 3) & ~0x3) + 1;

		/* padding */
		for (i = param_num + 2; i < dma_len; i++)
			i2c_buffer[i] = 0;

		/* debug */
		dev_dbg(bcmtch_data_ptr->p_device,
			"ST:[%x]:send long cmd params: dma_len=%d param_num=%d",
			BCMTCH_DF_ST,
			dma_len,
			param_num);
		for (i = 0; i < dma_len; i++)
			dev_dbg(bcmtch_data_ptr->p_device,
				"I2C:[%x]:0x%02x",
				BCMTCH_DF_I2C,
				i2c_buffer[i]);

		dev_dbg(bcmtch_data_ptr->p_device,
			"ST:[%x]:END.",
			BCMTCH_DF_ST);

		/* i2c write transaction */
		ret_val = bcmtch_i2c_send(
						bcmtch_data_ptr,
						i2c_spm_ptr->adapter,
						i2c_sys_ptr->addr,
						dma_len,
						i2c_buffer);
		if (ret_val)
			return ret_val;
	}

	return ret_val;
}

static int bcmtch_i2c_read_dma(
						struct i2c_client *p_i2c,
						uint16_t read_len,
						uint8_t *read_data)
{
	uint8_t dma_reg = BCMTCH_SPM_REG_DMA_RFIFO;

	/* setup I2C messages for DMA read transaction */
	struct i2c_msg dma_read[2] = {
		/* next write messages to read the DMA request status */
		{.addr = p_i2c->addr, .flags = 0, .len = 1, .buf = &dma_reg},
		{.addr = p_i2c->addr, .flags = I2C_M_RD,
				.len = read_len, .buf = read_data}
	};

	/* read status */
	if (bcmtch_i2c_transfer(p_i2c->adapter, dma_read, 2) != 2)
		return -EIO;
	else
		return 0;
}

static int bcmtch_i2c_read_sys(
						struct i2c_client *p_i2c,
						uint32_t sys_addr,
						uint16_t read_len,
						uint8_t *read_data)
{
	int ret_val = 0;
	uint8_t dma_reg = BCMTCH_SPM_REG_DMA_RFIFO;

	/* setup the DMA header for this read transaction */
	uint8_t dma_header[8] = {
		/* set dma controller addr */
		BCMTCH_SPM_REG_DMA_ADDR,
		/* setup dma address */
		(sys_addr & 0xFF),
		((sys_addr & 0xFF00) >> 8),
		((sys_addr & 0xFF0000) >> 16),
		((sys_addr & 0xFF000000) >> 24),
		/* setup dma length */
		(read_len & 0xFF),
		((read_len & 0xFF00) >> 8),
		/* setup dma mode */
		BCMTCH_DMA_MODE_READ
	};

	/* setup I2C messages for DMA read request transaction */
	struct i2c_msg dma_request[3] = {
		/* write DMA request header */
		{.addr = p_i2c->addr, .flags = 0, .len = 8, .buf = dma_header},

		/* next write messages to read the DMA request */
		{.addr = p_i2c->addr, .flags = 0, .len = 1, .buf = &dma_reg},
		{.addr = p_i2c->addr, .flags = I2C_M_RD,
				.len = read_len, .buf = read_data}
	};

	/* send complete DMA request */
	if (bcmtch_i2c_transfer(p_i2c->adapter, dma_request, 3) != 3)
		ret_val = -EIO;

	return ret_val;
}

static int bcmtch_i2c_write_sys(
					struct i2c_client *p_i2c,
					uint32_t sys_addr,
					uint16_t write_len,
					uint8_t *write_data)
{
	int ret_val = 0;

	uint16_t dma_len = write_len + 1;
	uint8_t *dma_data = vzalloc(dma_len);

	/* setup the DMA header for this read transaction */
	uint8_t dma_header[8] = {
		/* set dma controller addr */
		BCMTCH_SPM_REG_DMA_ADDR,
		/* setup dma address */
		(sys_addr & 0xFF),
		((sys_addr & 0xFF00) >> 8),
		((sys_addr & 0xFF0000) >> 16),
		((sys_addr & 0xFF000000) >> 24),
		/* setup dma length */
		(write_len & 0xFF),
		((write_len & 0xFF00) >> 8),
		/* setup dma mode */
		BCMTCH_DMA_MODE_WRITE
	};

	/* setup I2C messages for DMA read request transaction */
	struct i2c_msg dma_request[2] = {
		/* write DMA request header */
		{.addr = p_i2c->addr,
			.flags = 0,
			.len = 8,
			.buf = dma_header},
		{.addr = p_i2c->addr,
			.flags = 0,
			.len = dma_len,
			.buf = dma_data}
	};

	if (dma_data) {
		/* setup dma data buffer */
		dma_data[0] = BCMTCH_SPM_REG_DMA_WFIFO;
		memcpy(&dma_data[1], write_data, write_len);

		if (i2c_transfer(p_i2c->adapter, dma_request, 2) != 2)
			ret_val = -EIO;

		/* free dma buffer */
		vfree(dma_data);
	} else {
		ret_val = -ENOMEM;
	}

	return ret_val;
}

static int bcmtch_i2c_init_clients(struct i2c_client *p_i2c_client_spm)
{
	int ret_val = 0;
	struct i2c_client *p_i2c_client_sys;
	struct bcmtch_data *bcmtch_data_ptr =
		i2c_get_clientdata(p_i2c_client_spm);

	if (p_i2c_client_spm->adapter) {
		/* Configure the second I2C slave address. */
		p_i2c_client_sys =
			i2c_new_dummy(
				p_i2c_client_spm->adapter,
				bcmtch_data_ptr->platform_data.i2c_addr_sys);

		if (p_i2c_client_sys) {
			/* assign */
			bcmtch_data_ptr->p_i2c_client_spm = p_i2c_client_spm;
			bcmtch_data_ptr->p_i2c_client_sys = p_i2c_client_sys;
		} else {
			dev_err(bcmtch_data_ptr->p_device,
				"%s() i2c_new_dummy == NULL, slave address: 0x%x\n",
				__func__,
				bcmtch_data_ptr->platform_data.i2c_addr_sys);

			ret_val = -ENODEV;
		}
	} else {
		dev_err(bcmtch_data_ptr->p_device,
				"%s() p_i2c_adapter == NULL\n",
				__func__);

		ret_val = -ENODEV;
	}

	return ret_val;
}

static inline void bcmtch_i2c_free_clients(
		struct bcmtch_data *bcmtch_data_ptr)
{
	if (bcmtch_data_ptr && (bcmtch_data_ptr->p_i2c_client_sys)) {
		i2c_unregister_device(bcmtch_data_ptr->p_i2c_client_sys);
		bcmtch_data_ptr->p_i2c_client_sys = NULL;
	}
}

/* ------------------------------------------------- */
/* -- BCM Touch Controller I2C Driver Structures --- */
/* ------------------------------------------------- */
static const struct i2c_device_id bcmtch_i2c_id[] = {
	{BCMTCH15XXX_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, bcmtch_i2c_id);

static struct i2c_driver bcmtch_i2c_driver = {
	.driver = {
		   .name = BCMTCH15XXX_NAME,
		   .owner = THIS_MODULE,
		   },
	.probe = bcmtch_i2c_probe,
	.remove = bcmtch_i2c_remove,
	.id_table = bcmtch_i2c_id,

#ifdef CONFIG_PM
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = bcmtch_suspend,
	.resume = bcmtch_resume,
#endif
#endif
};

/* ------------------------------------------------ */
/* -- BCM Touch Controller I2C Driver Functions --- */
/* ------------------------------------------------ */
static int bcmtch_i2c_probe(
					struct i2c_client *p_i2c_client,
					const struct i2c_device_id *id)
{
	struct bcmtch_data *bcmtch_data_ptr;
	int ret_val = 0;

	dev_dbg(&p_i2c_client->dev,
		"INFO:[%x]:Driver: %s : %s : %s\n",
		BCMTCH_DF_INFO,
		BCMTCH_DRIVER_VERSION,
		BCMTCH_DRIVER_BUILD_DATE,
		BCMTCH_DRIVER_BUILD_TIME);

	/* print driver probe header */
	if (p_i2c_client)
		dev_dbg(&p_i2c_client->dev,
			"INFO:[%x]:dev=%s addr=0x%x irq=%d\n",
			BCMTCH_DF_INFO,
			p_i2c_client->name,
			p_i2c_client->addr,
			p_i2c_client->irq);

	if (id)
		dev_dbg(&p_i2c_client->dev,
			"INFO:[%x]:match id=%s\n",
			BCMTCH_DF_INFO,
			id->name);

	/* allocate global BCM Touch Controller driver structure */
	ret_val = bcmtch_dev_alloc(p_i2c_client);
	if (ret_val)
		goto probe_error;

	/* setup local platform data from client device structure */
	ret_val = bcmtch_dev_init_platform(&p_i2c_client->dev);
	if (ret_val)
		goto init_platform_error;

	bcmtch_data_ptr = i2c_get_clientdata(p_i2c_client);

	mutex_lock(&bcmtch_data_ptr->mutex_work);

	/* initialize deferred worker (workqueue/tasklet/etc */
	ret_val = bcmtch_init_deferred_worker(bcmtch_data_ptr);
	if (ret_val)
		goto worker_error;

	/* initialize power supplies */
	ret_val = bcmtch_dev_power_init(bcmtch_data_ptr);
	if (ret_val)
		goto power_init_error;

	/* enable power supplies */
	ret_val = bcmtch_dev_power_enable(bcmtch_data_ptr, true);
	if (ret_val)
		goto power_enable_error;

	/* setup the gpio pins
	 * - 1 gpio used for reset control signal to BCM Touch Controller
	 * - 1 gpio used as interrupt signal from BCM Touch Controller
	 */
	ret_val = bcmtch_init_gpio(bcmtch_data_ptr);
	if (ret_val)
		goto gpio_error;

	/*
	* setup the i2c clients and bind (store pointers in global structure)
	* 1. SPM I2C client
	* 2. SYS I2C client
	*/
	ret_val = bcmtch_i2c_init_clients(p_i2c_client);
	if (ret_val)
		goto i2c_client_error;

    /* reset the chip on driver load ? */
	if (bcmtch_data_ptr->gpio_hw_reset) {
		bcmtch_dev_reset(
			bcmtch_data_ptr,
			BCMTCH_RESET_MODE_HARD);
	} else {
		bcmtch_dev_reset(
			bcmtch_data_ptr,
			BCMTCH_RESET_MODE_SOFT_CHIP |
			BCMTCH_RESET_MODE_SOFT_ARM);
	}

	/* init the table of the worker process functions */
	bcmtch_dev_init_worker_process(bcmtch_data_ptr);

	/* perform BCM Touch Controller initialization */
	ret_val = bcmtch_dev_init(bcmtch_data_ptr);
	if (ret_val)
		goto init_dev_error;

	/* setup the os cli */
	ret_val = bcmtch_os_init_cli(&p_i2c_client->dev);
	if (ret_val)
		goto cli_error;

	/* setup the sysfs api */
	ret_val = bcmtch_os_init_abi(&p_i2c_client->dev);
	if (ret_val)
		goto sysfs_api_error;

	/* setup the os input device*/
	ret_val = bcmtch_init_input_device(bcmtch_data_ptr);
	if (ret_val)
		goto input_dev_error;

	bcmtch_dev_watchdog_start(bcmtch_data_ptr);

	ret_val = bcmtch_interrupt_enable(bcmtch_data_ptr);
	if (ret_val)
		goto interrupt_error;

	mutex_unlock(&bcmtch_data_ptr->mutex_work);

#ifdef CONFIG_HAS_EARLYSUSPEND
	bcmtch_register_early_suspend(bcmtch_data_ptr);
#endif /* CONFIG_HAS_EARLYSUSPEND */
	dev_dbg(bcmtch_data_ptr->p_device,
		"PROBE[%x]: success\n",
		BCMTCH_DF_INFO);

	return 0;

interrupt_error:

input_dev_error:
	/* Undo input device init */
	bcmtch_free_input_device(bcmtch_data_ptr);

sysfs_api_error:
	/* Undo sysfs api init */
	bcmtch_os_free_abi(&p_i2c_client->dev);

cli_error:
	/* Undo os cli init */
	bcmtch_os_free_cli(&p_i2c_client->dev);

init_dev_error:
	/* Undo touch controller initialization */

i2c_client_error:
	/* Undo i2c clients init */
	bcmtch_i2c_free_clients(bcmtch_data_ptr);

gpio_error:
	/* Undo gpio init */
	bcmtch_free_gpio(bcmtch_data_ptr);

power_enable_error:
		/* undo power enable */
	bcmtch_dev_power_enable(bcmtch_data_ptr, false);

power_init_error:
	/* undo power init */
	bcmtch_dev_power_free(bcmtch_data_ptr);

	/* Undo worker init */
	bcmtch_free_deferred_worker(bcmtch_data_ptr);

worker_error:
	mutex_unlock(&bcmtch_data_ptr->mutex_work);

init_platform_error:
	/* Undo platform init */
	bcmtch_dev_free(p_i2c_client);

probe_error:
	pr_err("BCMTCH:PROBE: failure\n");

	return ret_val;
}

static int bcmtch_i2c_remove(struct i2c_client *p_i2c_client)
{
	struct bcmtch_data *bcmtch_data_ptr =
		i2c_get_clientdata(p_i2c_client);

    /* disable interrupts */
	bcmtch_interrupt_disable(bcmtch_data_ptr);

	/* state protocol stop scan */
	if (bcmtch_data_ptr->work_process_index >= BCMTCH_WP_STATE)
		bcmtch_state_stop_scan(bcmtch_data_ptr);

	/* free watchdog timer */
	bcmtch_dev_watchdog_stop(bcmtch_data_ptr);

	/* free deferred worker (queue) */
	bcmtch_free_deferred_worker(bcmtch_data_ptr);

	mutex_lock(&bcmtch_data_ptr->mutex_work);

#ifdef CONFIG_HAS_EARLYSUSPEND
	bcmtch_unregister_early_suspend(bcmtch_data_ptr);
#endif /* CONFIG_HAS_EARLYSUSPEND */

    /* force chip to sleep before exiting */
	if (BCMTCH_POWER_STATE_SLEEP !=
			bcmtch_dev_get_power_state(bcmtch_data_ptr)) {
		bcmtch_dev_set_power_state(bcmtch_data_ptr,
			BCMTCH_POWER_STATE_SLEEP);
	}

	/* disable power */
	bcmtch_dev_power_enable(bcmtch_data_ptr, false);

	/* release power */
	bcmtch_dev_power_free(bcmtch_data_ptr);

	/* free communication channels */
	bcmtch_dev_free_channels(bcmtch_data_ptr);

	/* free i2c device clients */
	bcmtch_i2c_free_clients(bcmtch_data_ptr);

	/* Undo sysfs api init */
	bcmtch_os_free_abi(&p_i2c_client->dev);

	/* remove the os cli */
	bcmtch_os_free_cli(&p_i2c_client->dev);

	/* free input device */
	bcmtch_free_input_device(bcmtch_data_ptr);

	/* free used gpio pins */
	bcmtch_free_gpio(bcmtch_data_ptr);

	/* free this mem last */
	bcmtch_dev_free(p_i2c_client);

	return 0;
}

static int __init bcmtch_i2c_init(void)
{
	return i2c_add_driver(&bcmtch_i2c_driver);
}

module_init(bcmtch_i2c_init);

static void __exit bcmtch_i2c_exit(void)
{
	i2c_del_driver(&bcmtch_i2c_driver);
}

module_exit(bcmtch_i2c_exit);

MODULE_DESCRIPTION("I2C support for BCMTCH15XXX Touchscreen");
MODULE_LICENSE("GPL");
MODULE_VERSION(BCMTCH_DRIVER_VERSION);
