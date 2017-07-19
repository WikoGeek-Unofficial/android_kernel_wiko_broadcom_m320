/******************************************************************************/
/*                                                                            */
/* Copyright 2013 Broadcom Corporation                                        */
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


#include "pmdbg_api.h"
#include "pmdbg_hw.h"
#include "mach/hardware.h"
#include <asm/page.h>
#include <linux/io.h>
#include <mach/r8a7373.h>


static int gpio_init(void);
static void gpio_exit(void);
static void gpio_show(char **);

static char *buf_reg;
static int toggle;

LOCAL_DECLARE_MOD_SHOW(gpio, gpio_init, gpio_exit, gpio_show);

struct gpio_table_t {
	int num;
	unsigned long addr;
	unsigned char name[20];
};

const struct gpio_table_t portcr_table[] = {
	{ 0,   0xE6050000, "GPIO0" },
	{ 1,   0xE6050001, "GPIO1" },
	{ 2,   0xE6050002, "GPIO2" },
	{ 3,   0xE6050003, "GPIO3" },
	{ 4,   0xE6050004, "GPIO4" },
	{ 5,   0xE6050005, "GPIO5" },
	{ 6,   0xE6050006, "GPIO6" },
	{ 7,   0xE6050007, "GPIO7" },
	{ 8,   0xE6050008, "GPIO8" },
	{ 9,   0xE6050009, "GPIO9" },
	{ 10,  0xE605000A, "GPIO10" },
	{ 11,  0xE605000B, "GPIO11" },
	{ 12,  0xE605000C, "GPIO12" },
	{ 13,  0xE605000D, "GPIO13" },
	{ 14,  0xE605000E, "GPIO14" },
	{ 15,  0xE605000F, "GPIO15" },
	{ 16,  0xE6050010, "GPIO16" },
	{ 17,  0xE6050011, "GPIO17" },
	{ 18,  0xE6050012, "GPIO18" },
	{ 19,  0xE6050013, "GPIO19" },
	{ 20,  0xE6050014, "GPIO20" },
	{ 21,  0xE6050015, "GPIO21" },
	{ 22,  0xE6050016, "GPIO22" },
	{ 23,  0xE6050017, "GPIO23" },
	{ 24,  0xE6050018, "GPIO24" },
	{ 25,  0xE6050019, "GPIO25" },
	{ 26,  0xE605001A, "GPIO26" },
	{ 27,  0xE605001B, "GPIO27" },
	{ 28,  0xE605001C, "GPIO28" },
	{ 29,  0xE605001D, "GPIO29" },
	{ 30,  0xE605001E, "GPIO30" },
	{ 31,  0xE605001F, "GPIO31" },
	{ 32,  0xE6050020, "GPIO32" },
	{ 33,  0xE6050021, "GPIO33" },
	{ 34,  0xE6050022, "GPIO34" },
	{ 35,  0xE6050023, "GPIO35" },
	{ 36,  0xE6050024, "GPIO36" },
	{ 37,  0xE6050025, "GPIO37" },
	{ 38,  0xE6050026, "GPIO38" },
	{ 39,  0xE6050027, "GPIO39" },
	{ 40,  0xE6050028, "TS_SDAT" },
	{ 41,  0xE6050029, "TS_SPSYNC" },
	{ 42,  0xE605002A, "TS_SCK" },
	{ 43,  0xE605002B, "TS_SDEN" },
	{ 44,  0xE605002C, "KEYIN0" },
	{ 45,  0xE605002D, "KEYIN1" },
	{ 46,  0xE605002E, "KEYIN2" },
	{ 47,  0xE605002F, "KEYIN3" },
	{ 48,  0xE6050030, "KEYIN4" },
	{ 64,  0xE6050040, "SIM1_RST" },
	{ 65,  0xE6050041, "SIM1_CLK" },
	{ 66,  0xE6050042, "SIM1_IO" },
	{ 67,  0xE6050043, "SIM0_RST" },
	{ 68,  0xE6050044, "SIM0_CLK" },
	{ 69,  0xE6050045, "SIM0_IO" },
	{ 70,  0xE6050046, "IC_DP" },
	{ 71,  0xE6050047, "IC_DM" },
	{ 72,  0xE6050048, "MSIOF3_TXD" },
	{ 73,  0xE6050049, "MSIOF3_RXD" },
	{ 74,  0xE605004A, "MSIOF3_SCK" },
	{ 75,  0xE605004B, "MSIOF3_SYNC" },
	{ 76,  0xE605004C, "SCIFB1_RTS" },
	{ 77,  0xE605004D, "SCIFB1_CTS" },
	{ 78,  0xE605004E, "SCIFB1_TXD" },
	{ 79,  0xE605004F, "SCIFB1_RXD" },
	{ 80,  0xE6050050, "MSIOF0_TXD" },
	{ 81,  0xE6050051, "MSIOF0_RXD" },
	{ 82,  0xE6050052, "MSIOF0_SCK" },
	{ 83,  0xE6050053, "MSIOF0_SYNC" },
	{ 84,  0xE6050054, "MSIOF1_TXD" },
	{ 85,  0xE6050055, "MSIOF1_RXD" },
	{ 86,  0xE6050056, "MSIOF1_SCK" },
	{ 87,  0xE6050057, "MSIOF1_SYNC" },
	{ 88,  0xE6050058, "MSIOF0_SS1" },
	{ 89,  0xE6050059, "MSIOF1_SS1" },
	{ 90,  0xE605005A, "MSIOF2_SS1" },
	{ 91,  0xE605005B, "MSIOF3_SS1" },
	{ 96,  0xE6050060, "KEYIN5" },
	{ 97,  0xE6050061, "KEYIN6" },
	{ 98,  0xE6050062, "KEYIN7" },
	{ 99,  0xE6050063, "KEYOUT0" },
	{ 100, 0xE6050064, "KEYOUT1" },
	{ 101, 0xE6050065, "KEYOUT2" },
	{ 102, 0xE6050066, "KEYOUT3" },
	{ 103, 0xE6050067, "KEYOUT4" },
	{ 104, 0xE6050068, "KEYOUT5" },
	{ 105, 0xE6050069, "KEYOUT6" },
	{ 106, 0xE605006A, "KEYOUT7" },
	{ 107, 0xE605006B, "KEYINOUT8" },
	{ 108, 0xE605006C, "KEYINOUT9" },
	{ 109, 0xE605006D, "KEYINOUT10" },
	{ 110, 0xE605006E, "KEYINOUT11" },
	{ 128, 0xE6051080, "SCIFA0_TXD" },
	{ 129, 0xE6051081, "SCIFA0_RXD" },
	{ 130, 0xE6051082, "SCIFA1_TXD" },
	{ 131, 0xE6051083, "SCIFA1_RXD" },
	{ 133, 0xE6051085, "MD3" },
	{ 134, 0xE6051086, "MD4" },
	{ 135, 0xE6051087, "MD6" },
	{ 136, 0xE6051088, "MD7" },
	{ 137, 0xE6051089, "SCIFB0_TXD" },
	{ 138, 0xE605108A, "SCIFB0_RXD" },
	{ 139, 0xE605108B, "DIGRFEN" },
	{ 140, 0xE605108C, "GPS_TIMESTAMP" },
	{ 141, 0xE605108D, "TXP" },
	{ 142, 0xE605108E, "TXP2" },
	{ 198, 0xE60520C6, "COEX_0" },
	{ 199, 0xE60520C7, "COEX_1" },
	{ 200, 0xE60520C8, "COEX_2" },
	{ 201, 0xE60520C9, "COEX_3" },
	{ 202, 0xE60520CA, "BRI" },
	{ 203, 0xE60520CB, "ULPI_DATA0" },
	{ 204, 0xE60520CC, "ULPI_DATA1" },
	{ 205, 0xE60520CD, "ULPI_DATA2" },
	{ 206, 0xE60520CE, "ULPI_DATA3" },
	{ 207, 0xE60520CF, "ULPI_DATA4" },
	{ 208, 0xE60520D0, "ULPI_DATA5" },
	{ 209, 0xE60520D1, "ULPI_DATA6" },
	{ 210, 0xE60520D2, "ULPI_DATA7" },
	{ 211, 0xE60520D3, "ULPI_CLK" },
	{ 212, 0xE60520D4, "ULPI_STP" },
	{ 213, 0xE60520D5, "ULPI_DIR" },
	{ 214, 0xE60520D6, "ULPI_NXT" },
	{ 215, 0xE60520D7, "VIO_CKO1" },
	{ 216, 0xE60520D8, "VIO_CKO2" },
	{ 217, 0xE60520D9, "VIO_CKO3" },
	{ 218, 0xE60520DA, "VIO_CKO4" },
	{ 219, 0xE60520DB, "VIO_CKO5" },
	{ 224, 0xE60520E0, "RD_N" },
	{ 225, 0xE60520E1, "WE0_N" },
	{ 226, 0xE60520E2, "WE1_N" },
	{ 227, 0xE60520E3, "CS0_N" },
	{ 228, 0xE60520E4, "CS2_N" },
	{ 229, 0xE60520E5, "CS4_N" },
	{ 230, 0xE60520E6, "WAIT_N" },
	{ 231, 0xE60520E7, "RDWR" },
	{ 232, 0xE60520E8, "D15" },
	{ 233, 0xE60520E9, "D14" },
	{ 234, 0xE60520EA, "D13" },
	{ 235, 0xE60520EB, "D12" },
	{ 236, 0xE60520EC, "D11" },
	{ 237, 0xE60520ED, "D10" },
	{ 238, 0xE60520EE, "D9" },
	{ 239, 0xE60520EF, "D8" },
	{ 240, 0xE60520F0, "D7" },
	{ 241, 0xE60520F1, "D6" },
	{ 242, 0xE60520F2, "D5" },
	{ 243, 0xE60520F3, "D4" },
	{ 244, 0xE60520F4, "D3" },
	{ 245, 0xE60520F5, "D2" },
	{ 246, 0xE60520F6, "D1" },
	{ 247, 0xE60520F7, "D0" },
	{ 248, 0xE60520F8, "CKO" },
	{ 249, 0xE60520F9, "A10" },
	{ 250, 0xE60520FA, "A9" },
	{ 251, 0xE60520FB, "A8" },
	{ 252, 0xE60520FC, "A7" },
	{ 253, 0xE60520FD, "A6" },
	{ 254, 0xE60520FE, "A5" },
	{ 255, 0xE60520FF, "A4" },
	{ 256, 0xE6052100, "A3" },
	{ 257, 0xE6052101, "A2" },
	{ 258, 0xE6052102, "A1" },
	{ 259, 0xE6052103, "A0" },
	{ 260, 0xE6052104, "FSIACK" },
	{ 261, 0xE6052105, "FSIAISLD" },
	{ 262, 0xE6052106, "FSIAOMC" },
	{ 263, 0xE6052107, "FSIAOLR" },
	{ 264, 0xE6052108, "FSIAOBT" },
	{ 265, 0xE6052109, "FSIAOSLD" },
	{ 266, 0xE605210A, "FSIBISLD" },
	{ 267, 0xE605210B, "FSIBOLR" },
	{ 268, 0xE605210C, "FSIBOMC" },
	{ 269, 0xE605210D, "FSIBOBT" },
	{ 270, 0xE605210E, "FSIBOSLD" },
	{ 271, 0xE605210F, "FSIBCK" },
	{ 272, 0xE6052110, "VINT" },
	{ 273, 0xE6052111, "ISP_IRIS1" },
	{ 274, 0xE6052112, "ISP_IRIS0" },
	{ 275, 0xE6052113, "ISP_SHUTTER1" },
	{ 276, 0xE6052114, "ISP_SHUTTER0" },
	{ 277, 0xE6052115, "ISP_STROBE" },
	{ 288, 0xE6052120, "SDHICLK1" },
	{ 289, 0xE6052121, "SDHID1_0" },
	{ 290, 0xE6052122, "SDHID1_1" },
	{ 291, 0xE6052123, "SDHID1_2" },
	{ 292, 0xE6052124, "SDHID1_3" },
	{ 293, 0xE6052125, "SDHICMD1" },
	{ 294, 0xE6052126, "SDHICLK2" },
	{ 295, 0xE6052127, "SDHID2_0" },
	{ 296, 0xE6052128, "SDHID2_1" },
	{ 297, 0xE6052129, "SDHID2_2" },
	{ 298, 0xE605212A, "SDHID2_3" },
	{ 299, 0xE605212B, "SDHICMD2" },
	{ 300, 0xE605212C, "MMCD0_0" },
	{ 301, 0xE605212D, "MMCD0_1" },
	{ 302, 0xE605212E, "MMCD0_2" },
	{ 303, 0xE605212F, "MMCD0_3" },
	{ 304, 0xE6052130, "MMCD0_4" },
	{ 305, 0xE6052131, "MMCD0_5" },
	{ 306, 0xE6052132, "MMCD0_6" },
	{ 307, 0xE6052133, "MMCD0_7" },
	{ 308, 0xE6052134, "MMCCMD0" },
	{ 309, 0xE6052135, "MMCCLK0" },
	{ 310, 0xE6052136, "MMCRST" },
	{ 311, 0xE6052137, "PDM0_DATA" },
	{ 312, 0xE6052138, "PDM1_DATA" },
	{ 320, 0xE6052140, "SDHID0_0" },
	{ 321, 0xE6052141, "SDHID0_1" },
	{ 322, 0xE6052142, "SDHID0_2" },
	{ 323, 0xE6052143, "SDHID0_3" },
	{ 324, 0xE6052144, "SDHICMD0" },
	{ 325, 0xE6052145, "SDHIWP0" },
	{ 326, 0xE6052146, "SDHICLK0" },
	{ 327, 0xE6052147, "SDHICD0" },
};

#include "mach/hardware.h"

static int _gpio_get(int port_num)
{
	int register_val;
	int bit;

	struct gpio_table_t portd_table[] =	{
		{ 0, 0xE6054000, "PORTL031_000"  },
		{ 0, 0xE6054004, "PORTL063_032"  },
		{ 0, 0xE6054008, "PORTL095_064"  },
		{ 0, 0xE605400C, "PORTL127_096"  },
		{ 0, 0xE6055000, "PORTD159_128"  },
		{ 0, 0,          "NA"            },
		{ 0, 0xE6056000, "PORTR223_192"  },
		{ 0, 0xE6056004, "PORTR255_224"  },
		{ 0, 0xE6056008, "PORTR287_256"  },
		{ 0, 0xE605600C, "PORTR319_288"  },
		{ 0, 0xE6056010, "PORTR351_320"  },
	};

	register_val = portd_table[port_num/32].addr;
	bit = port_num%32;

	return (__raw_readl(IO_ADDRESS(register_val)) >> bit) & 1;
}

#define INOUT(cr) \
		(((cr&7) == 0) ? ((cr&0x20) ? "IN" : \
		((cr&0x10) ? "OUT" : "ERR")) : "FNC")
#define PUPD(cr) \
		((cr&0xc0) == 0x80 ? "PD" : ((cr&0xc0) == 0xc0 ? "PU" : "-"))
#define IS_FUNC(cr) \
		((cr&7) ? 1 : 0)

static void _dump_gpio(char *s, int group)
{
	int i;
	int reg;
	int start = 0, end = 0;

	if (group == 0) {
		start = 0;
		end = 108; /* the maximum size is PAGE_SIZE at sysfs,
					  so we have not to over this size */
	} else if (group == 1) {
		start = 108;
		end = (sizeof(portcr_table)/sizeof(portcr_table[0]));
	} else {
		start = 0;
		end = 0;
	}


	for (i = start; i < end; i++) {
		reg = __raw_readb(IO_ADDRESS(portcr_table[i].addr));
		s += sprintf(s, "%-3d %-15s ",
			portcr_table[i].num,
			portcr_table[i].name);
		s += sprintf(s, "%-3s ", INOUT(reg));
		s += sprintf(s, "%-2s ", PUPD(reg));
		if (IS_FUNC(reg))
			s += sprintf(s, "%s\n", "-");
		else
			s += sprintf(s, "%s\n",
				_gpio_get(portcr_table[i].num) ? "H" : "L");
	}
}

void dump_gpio(void)
{
	int i;
	int reg;

	for (i = 0; i < (sizeof(portcr_table)/sizeof(portcr_table[0])); i++) {
		reg = __raw_readb(IO_ADDRESS(portcr_table[i].addr));
		pr_crit("%-3d %-15s ",
			portcr_table[i].num,
			portcr_table[i].name);
		pr_crit("%-4s ", INOUT(reg));
		pr_crit("%-2s ", PUPD(reg));
		if (IS_FUNC(reg))
			pr_crit("%s\n", "-");
		else
			pr_crit("%s\n",
				_gpio_get(portcr_table[i].num) ? "H" : "L");
	}
}
EXPORT_SYMBOL(dump_gpio);

void gpio_show(char **buf)
{
	FUNC_MSG_IN;
	#define MAX_BUF_SIZE   (PAGE_SIZE)

	if (buf_reg == NULL) {
		buf_reg = kmalloc(MAX_BUF_SIZE, GFP_KERNEL);
		if (NULL == buf_reg)
			return;
	}

	memset(buf_reg, 0, MAX_BUF_SIZE);
	_dump_gpio(buf_reg, toggle%2);
	toggle++;
	*buf = buf_reg;

	FUNC_MSG_OUT;
}


int gpio_init(void)
{
	FUNC_MSG_IN;
	FUNC_MSG_RET(0);
}

void gpio_exit(void)
{
	FUNC_MSG_IN;
	FUNC_MSG_OUT;
}
