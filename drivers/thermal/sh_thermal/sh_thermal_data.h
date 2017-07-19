/*****************************************************************************
* Copyright 2013 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#ifndef __SH_THERMAL_DATA_H__
#define __SH_THERMAL_DATA_H__

/*
 * Thermal Sensor module
 * Physical address : 0xE61F0000
 */

/* Thermal Sensor Controller Registers (Offset) */
#define STR_RW_32B				0x0000/* Status Register */
#define ENR_RW_32B				0x0004/* Enable Register */
#define PORTRST_MASK_RW_32B		0x0008/* PORT/RESET MASK Register */
#define INT_MASK_RW_32B			0x000C/* Interrupt MASK Register */
/* THS 0 Interrupt polarity setting register */
#define POSNEG0_RW_32B			0x0120
/* THS 1 Interrupt polarity setting register */
#define POSNEG1_RW_32B			0x0220
/* THS 0 Edge/Level setting register */
#define EDGELEVEL0_RW_32B		0x0124
/* THS 1 Edge/Level  setting register */
#define EDGELEVEL1_RW_32B		0x0224
/* THS0 chattering restraint ON/OFF setting register */
#define FILONOFF0_RW_32B		0x0128
/* THS1 chattering restraint ON/OFF setting register */
#define FILONOFF1_RW_32B		0x0228
/* THS0 control register */
#define THSCR0_RW_32B			0x012C
/* THS1 control register */
#define THSCR1_RW_32B			0x022C
/* THS0 status register */
#define THSSR0_R_32B			0x0130
/* THS1 status register */
#define THSSR1_R_32B			0x0230
/* THS0 Interrupt control register */
#define INTCTLR0_RW_32B			0x0134
/* THS1 Interrupt control register */
#define INTCTLR1_RW_32B			0x0234

#define POSNEG(id)		((0 == id) ? POSNEG0_RW_32B : POSNEG1_RW_32B)
#define FILONOFF(id)	((0 == id) ? FILONOFF0_RW_32B : FILONOFF1_RW_32B)
#define THSCR(id)		((0 == id) ? THSCR0_RW_32B : THSCR1_RW_32B)
#define THSSR(id)		((0 == id) ? THSSR0_R_32B : THSSR1_R_32B)
#define INTCTLR(id)		((0 == id) ? INTCTLR0_RW_32B : INTCTLR1_RW_32B)


/* Define setting value */
/* Status Register register - STR bit definition */
#define	TJ00ST		0x1
#define	TJ01ST		(TJ00ST << 1)
#define	TJ02ST		(TJ01ST << 1)
#define	TJ03ST		(TJ02ST << 1)
#define	TJ10ST		(TJ03ST << 5)
#define	TJ11ST		(TJ10ST << 1)
#define	TJ12ST		(TJ11ST << 1)
#define	TJ13ST		(TJ12ST << 1)

#define	PRTFLGCL	(TJ13ST << 17)
#define	RSTFLGCL	(PRTFLGCL << 1)
#define	PRTFLG		(RSTFLGCL << 1)
#define	RSTFLG		(PRTFLG << 1)
#define	TJST_ALL_CLEAR		0x00000000

/* Status register - ENR bit definition */
#define	TJ00_EN		0x1
#define	TJ01_EN		(TJ00ST << 1)
#define	TJ02_EN		(TJ01ST << 1)
#define	TJ03_EN		(TJ02ST << 1)
#define	TJ10_EN		(TJ03ST << 5)
#define	TJ11_EN		(TJ10ST << 1)
#define	TJ12_EN		(TJ11ST << 1)
#define	TJ13_EN		(TJ12ST << 1)

/* PORT/RESET MASK register - PORTRST bit definition */
#define	TJ00PORT_MSK		0x1
#define	TJ01PORT_MSK		(TJ00ST << 1)
#define	TJ02PORT_MSK		(TJ01ST << 1)
#define	TJ03PORT_MSK		(TJ02ST << 1)
#define	TJ10PORT_MSK		(TJ03ST << 5)
#define	TJ11PORT_MSK		(TJ10ST << 1)
#define	TJ12PORT_MSK		(TJ11ST << 1)
#define	TJ13PORT_MSK		(TJ12ST << 1)

#define	TJ00RST_MSK			(TJ13PORT_MSK << 5)
#define	TJ01RST_MSK			(TJ00RST_MSK << 1)
#define	TJ02RST_MSK			(TJ01RST_MSK << 1)
#define	TJ03RST_MSK			(TJ02RST_MSK << 1)
#define	TJ10RST_MSK			(TJ03RST_MSK << 5)
#define	TJ11RST_MSK			(TJ10RST_MSK << 1)
#define	TJ12RST_MSK			(TJ11RST_MSK << 1)
#define	TJ13RST_MSK			(TJ12RST_MSK << 1)

/* Interrupt MASK register - INT bit definition */
#define	TJ00INT_MSK		0x1
#define	TJ01INT_MSK		(TJ00INT_MSK << 1)
#define	TJ02INT_MSK		(TJ01INT_MSK << 1)
#define	TJ03INT_MSK		(TJ02INT_MSK << 1)
#define	TJ10INT_MSK		(TJ03INT_MSK << 5)
#define	TJ11INT_MSK		(TJ10INT_MSK << 1)
#define	TJ12INT_MSK		(TJ11INT_MSK << 1)
#define	TJ13INT_MSK		(TJ12INT_MSK << 1)

/* Decimal temperature values */
#define	CTEMP3_MASK				0x3F000000
#define	CTEMP2_MASK				0x003F0000
#define	CTEMP1_MASK				0x00003F00
#define	CTEMP0_MASK				0x0000003F

#define TRESV_MSK       0x00000008

/*THS0/1 Interrupt polarity setting register - POSNEG0/1 bit definition*/
#define POSNEG_DETECTION	0x01

/* THS0/1 chattering restraint ON/OFF setting - FILONOFF0/1 bit definition*/
/* Enable chattering for Tj105, Tj100, Tj95, Tj65 */
#define FILONOFF_CHATTERING_EN	0x0F
/* Disable chattering for Tj105, Tj100, Tj95, Tj65 */
#define FILONOFF_CHATTERING_DI	0x0

/* THS0/1 control register - THSCR0/1 bit definition */
#define	THIDLE0		(1 << 8)/* Bit THIDLE1 */
#define	THIDLE1		(THIDLE0 << 1)/* Bit THIDLE1 */
#define	CPCTL		(THIDLE1 << 3)/* Bit CPCTL */


/* THS0/1 status register - THSSR0/1 bit definition */
/* Mask pattern */
#define CTEMP_MASK					0x0000003F

/* Set CTEMP to lower than actual value base on formular
 *    ((The_interrupt_temperature + 65) / 5) - 1 */
/* Converting Register value to actual temperature */
#define REG2TEMP(rtemp)		(((rtemp + 1) * 5) - 65)
/* Converting actual temperature to register value */
#define TEMP2REG(temp)		(((temp + 65) / 5) - 1)

/* THS0/1 Interrupt control register - INTCTLR0 bit definition */
/* Bit value definition of CTEM3/2/1/0 */
#define CTEMP3_HEX(x)		(TEMP2REG(x) << (3*8))
#define CTEMP2_HEX(x)		(TEMP2REG(x) << (2*8))
#define CTEMP1_HEX(x)		(TEMP2REG(x) << (1*8))
#define CTEMP0_HEX(x)		(TEMP2REG(x) << (0*8))

/* THS CPG clock supply register */
/*#define SMSTPCR5		0xE6150144
#define MMSTPCR5		0xE6150164*/
#define THS_CLK_SUPPLY_BIT	0x00400000

#endif /* __SH_THERMAL_DATA_H__ */
