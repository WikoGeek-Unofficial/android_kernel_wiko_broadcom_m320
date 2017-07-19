
/*
 * arch/arm/mach-shmobile/include/mach/setup-u2usb.h
 *
 * Copyright (C) 2012 Renesas Mobile Corporation
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
#ifndef __ASM_ARCH_U2USB_H
#define __ASM_ARCH_U2USB_H __FILE__


#define USB_SPWDAT		IO_ADDRESS(HSUSB_BASE_PHYS + 0x013A)
#define USB_SPCTRL		IO_ADDRESS(HSUSB_BASE_PHYS + 0x013C)
#define USB_SPRDAT		IO_ADDRESS(HSUSB_BASE_PHYS + 0x013E)
#define USB_SPEXADDR		IO_ADDRESS(HSUSB_BASE_PHYS + 0x0140)
#define USB_SPWR		0x0001
#define USB_SPRD		0x0002
#define USB_SPADDR		IO_ADDRESS(HSUSB_BASE_PHYS + 0x0138)
#define PHY_SPADDR_INIT 0x0000
#define PHY_VENDOR_SPECIFIC_ADDR_MASK 0x0020
#define MUIC_IS_PRESENT		muic_is_present()

#define TUSB1211_POWER_CONTROL_REG			0x3D
#define TUSB_VENDOR_SPECIFIC1				0x80
#define TUSB_VENDOR_SPECIFIC1_SET			0x81
#define TUSB_VENDOR_SPECIFIC1_CLR			0x82
#define TUSB_VENDOR_SPECIFIC3				0x85
#define TUSB_VENDOR_SPECIFIC3_SET			0x86
#define TUSB_VENDOR_SPECIFIC3_CLR                       0x87
#define TUSB_VENDOR_SPECIFIC4				0x88
#define TUSB_DEBUG_REG					0x15




#if defined(CONFIG_MACH_U2USB)
extern bool muic_is_present(void);
extern void __init usb_init(bool is_muic_present);
#else
static inline bool muic_is_present(void) { return 0; }
static inline void usb_init(bool is_muic_present) {}
#endif

#if defined(CONFIG_MACH_U2USB) && defined(CONFIG_USB_R8A66597_HCD)
void __init u2_add_usb_host_device(void);
#else
static inline void u2_add_usb_host_device(void) {}
#endif

#if defined(CONFIG_MACH_U2USB) && defined(CONFIG_USB_OTG)
void __init u2_add_usb_otg_device(void);
#else
static inline void u2_add_usb_otg_device(void) {}
#endif

extern struct r8a66597_platdata usbhs_func_data_d2153;
#ifdef CONFIG_USB_R8A66597_HCD
extern struct r8a66597_platdata usb_host_data;
#endif
#ifdef CONFIG_USB_OTG
extern struct r8a66597_platdata tusb1211_data;
#endif
#endif /* __ASM_ARCH_U2USB_H */
