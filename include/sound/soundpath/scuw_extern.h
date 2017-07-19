/* scuw_extern.h
 *
 * Copyright (C) 2012-2013 Renesas Mobile Corp.
 * All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SCUW_EXTERN_H__
#define __SCUW_EXTERN_H__

#include <linux/kernel.h>

#ifdef __SCUW_NO_EXTERN__
#define SCUW_NO_EXTERN
#else
#define SCUW_NO_EXTERN extern
#endif

/* SCUW start */
SCUW_NO_EXTERN int scuw_start(const u_int uiValue, u_int rate);
/* SCUW stop */
SCUW_NO_EXTERN int scuw_stop(void);
/* SCUW registers dump */
SCUW_NO_EXTERN void scuw_reg_dump(void);
/* SCUW FSIIF_FSIIR Setting and dummy read */
SCUW_NO_EXTERN void scuw_set_fsiir(void);

/* Added for FM */
SCUW_NO_EXTERN void scuw_set_fmmute(bool muteflag);
SCUW_NO_EXTERN void scuw_set_fmvolume(int volume);
SCUW_NO_EXTERN int scuw_get_fmvolume(void);
/* Added for FM */

/*Added for Sidetone Volume*/
SCUW_NO_EXTERN void scuw_sidetone_val(int vol);
SCUW_NO_EXTERN int scuw_get_sidetone_val(void);

/* FFD Register */
#define SCUW_FFDIR_FFD		(0xEC748004)
#define SCUW_DEVCR_FFD		(0xEC748020)

/* CPUFIFO2 Register */
#define SCUW_CF2IR		(0xEC768004)
#define SCUW_CF2EVCR		(0xEC76801C)

#ifdef SOUND_TEST
SCUW_NO_EXTERN void scuw_voice_test_start_a(void);
SCUW_NO_EXTERN void scuw_voice_test_stop_a(void);
#endif /* SOUND_TEST */

#endif	/* __SCUW_EXTERN_H__ */
