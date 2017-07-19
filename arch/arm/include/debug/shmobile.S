/*
 * Debugging macro include header
 *
 * Copyright (C) 2011  Renesas Electronics Corporation
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
 */

#define SCFSR		0x14
#ifdef CONFIG_DEBUG_LL_SH_SCIFA
#define SCFTDR		0x20
#elif defined CONFIG_DEBUG_LL_SH_SCIFB
#define SCFTDR		0x40
#endif
#define FSR_TEND	(1 << 6)

/*
 * Map the port at a special virtual location, independent of general
 * device mappings. See <mach/hardware.h> for other mappings. Note that we
 * are initially section mapped, so 1MB alignment is required.
 */
#define DEBUG_UART_BASE_PHY	CONFIG_DEBUG_LL_SH_SCI_BASE
#define DEBUG_UART_BASE_VIRT	0xfec00000 | ((DEBUG_UART_BASE_PHY) & 0xfffff)

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =DEBUG_UART_BASE_PHY
	ldr	\rv, =DEBUG_UART_BASE_VIRT
	.endm

	.macro	senduart, rd, rx
	strb	\rd, [\rx, #SCFTDR]
	.endm

	.macro	busyuart, rd, rx
1001:	ldrh	\rd, [\rx, #SCFSR]
	and	\rd, \rd, #FSR_TEND
	cmp	\rd, #FSR_TEND
	bne	1001b
	.endm

	.macro	waituart, rd, rx
	.endm
