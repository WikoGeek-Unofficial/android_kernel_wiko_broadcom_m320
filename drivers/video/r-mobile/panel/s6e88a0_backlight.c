/*
 * drivers/video/r-mobile/panel/s6e88a0_backlight.c
 *
 * Copyright (C) 2014 Broadcom Corporation
 * All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <rtapi/screen_display.h>
#include <linux/lcd.h>
#include <linux/platform_device.h>
#include "panel_common.h"
#include "panel_info.h"
#include "s6e88a0_param.h"
#include "dynamic_aid_s6e88a0.h"

#define LEVEL_IS_HBM(level)		(level >= 6)
#define MAX_GAMMA			300
#define DEFAULT_GAMMA_LEVEL		GAMMA_143CD
#define LDI_ID_REG			0x04
#define LDI_ID_LEN			3
#define LDI_MTP_REG			0xC8
#define LDI_MTP_LEN			(GAMMA_PARAM_SIZE - 1)
#define LDI_ELVSS_REG			0xB6
#define LDI_ELVSS_LEN			(ELVSS_PARAM_SIZE - 1) /* ELVSS +
						HBM V151 ~ V35 + ELVSS 17th */
#define LDI_HBM_REG			0xB5
#define LDI_HBM_LEN			28	/* HBM V255 + HBM ELVSS +
					white coordination + manufacture date */
#define MIN_BRIGHTNESS		0
#define MAX_BRIGHTNESS		255
#define DEFAULT_BRIGHTNESS		143
#define INIT_BRIGHTNESS	MAX_BRIGHTNESS
#define S6E88A0_INIT_RETRY_COUNT 3

static  unsigned char SEQ_ELVS_CONDITION[] = {
	0xB6,
	0x28, 0x0B,
};
#define SMART_DIMMING_DEBUG 0
#ifdef SMART_DIMMING_DEBUG
#define smtd_dbg(format, arg...)	printk(format, ##arg)
#else
#define smtd_dbg(format, arg...)
#endif

struct lcd_info_bklt {
	enum lcdfreq_level_idx	level;	/* Current level */
	struct device_attribute	*attr;	/* Hold attribute info */
	unsigned int			bl;
	unsigned int			auto_brightness;
	unsigned int			acl_enable;
	unsigned int			siop_enable;
	unsigned int			current_acl;
	unsigned int			current_bl;
	unsigned int			current_elvss;
	unsigned int			ldi_enable;
	unsigned int			power;
	struct mutex		lock;	/* Lock for change frequency */
	struct mutex		bl_lock;
	struct device		*dev;	/* Hold device of LCD */
	struct lcd_device	*ld;	/* LCD device info */
	struct backlight_device		*bd;
	unsigned char			id[LDI_ID_LEN];
	unsigned char			**gamma_table;
	unsigned char			**elvss_table;
	struct dynamic_aid_param_t	daid;
	unsigned char		aor[GAMMA_MAX][ARRAY_SIZE(SEQ_AOR_CONTROL)];
	unsigned int		connected;
	unsigned int		coordinate[2];
};
static struct lcd_info_bklt lcd_info_bklt_data;
static const unsigned int candela_table[GAMMA_MAX] = {
	10,	11,	12,	13,	14,	15,	16,	17,
	19,	20,	21,	22,	24,	25,	27,	29,
	30,	32,	34,	37,	39,	41,	44,	47,
	50,	53,	56,	60,	64,	68,	72,	77,
	82,	87,	93,	98,	105,	111,	119,	126,
	134,	143,	152,	162,	172,	183,	195,	207,
	220,	234,	249,	265,	282,	300,	400
};


unsigned char get_type(int len)
{
	if (len > 2)
		return MIPI_DSI_DCS_LONG_WRITE;
	else if (len == 2)
		return MIPI_DSI_DCS_SHORT_WRITE_PARAM;
	else
		return MIPI_DSI_DCS_SHORT_WRITE;
}

static int s6e88a0_write(unsigned char *seq, int len)
{
	int ret = 0;
	void *screen_handle;
	unsigned char *write_arr;
	screen_disp_delete disp_delete;

	/* +3 for len, type and null at the end */
	write_arr = kzalloc(len + 3, GFP_KERNEL);
	if (!write_arr) {
		pr_err("Unable to allocate memory for sequence\n");
		return -ENOMEM;
	}
	screen_handle = screen_display_new();

	write_arr[0] = len; /* length of the command */
	write_arr[1] = get_type(len); /* tyep of the command */
	memcpy(&write_arr[2], seq, len);

	ret = panel_dsi_write(screen_handle, write_arr, NULL);

	kfree(write_arr);
	disp_delete.handle = screen_handle;
	screen_display_delete(&disp_delete);
	return ret;
}

static int s6e88a0_read(const u8 addr, u8 *buf, u16 count)
{
	return panel_dsi_read(MIPI_DSI_DCS_READ, addr, count, buf, NULL);
}



static int s6e88a0_read_id(u8 *buf);
static int update_brightness(u8 force);

static int s6e88a0_read_id(u8 *buf)
{
	int ret = 0;

	ret = s6e88a0_read(LDI_ID_REG, buf, LDI_ID_LEN);
	if (ret < 0) {
		lcd_info_bklt_data.connected = 0;
		pr_err("panel is not connected well\n");
	}
	return ret;
}

static int s6e88a0_read_mtp(u8 *buf)
{
	int ret;
	ret = s6e88a0_read(LDI_MTP_REG, buf, LDI_MTP_LEN);

	pr_info("s6e88a0_read_mtp %02xh %d\n",
				LDI_MTP_REG, LDI_MTP_LEN);
	return ret;
}

static int s6e88a0_read_elvss(u8 *buf)
{
	int ret;

	ret = s6e88a0_read(LDI_ELVSS_REG, buf, LDI_ELVSS_LEN);

	pr_info("s6e88a0_read_elvss %02xh %d\n",
				LDI_ELVSS_REG, LDI_ELVSS_LEN);
	return ret;
}

static void s6e88a0_read_coordinate(u8 *buf)
{
	int ret = 0;

	ret = s6e88a0_read(LDI_HBM_REG, buf, LDI_HBM_LEN);

	if (ret < 0)
		dev_err(&lcd_info_bklt_data.ld->dev, "%s failed\n", __func__);

	lcd_info_bklt_data.coordinate[0] = buf[19] << 8 | buf[20];	/* X */
	lcd_info_bklt_data.coordinate[1] = buf[21] << 8 | buf[22];	/* Y */
	pr_info("s6e88a0_read_coordinate %02xh %d\n",
						LDI_HBM_REG, LDI_HBM_LEN);

}

static int get_backlight_level_from_brightness(int brightness)
{
	int backlightlevel;

	switch (brightness) {
	case 0 ... 23:
		backlightlevel = GAMMA_22CD;
		break;
	case 24:
		backlightlevel = GAMMA_24CD;
		break;
	case 25 ... 26:
		backlightlevel = GAMMA_25CD;
		break;
	case 27 ... 28:
		backlightlevel = GAMMA_27CD;
		break;
	case 29:
		backlightlevel = GAMMA_29CD;
		break;
	case 30 ... 31:
		backlightlevel = GAMMA_30CD;
		break;
	case 32 ... 33:
		backlightlevel = GAMMA_32CD;
		break;
	case 34 ... 36:
		backlightlevel = GAMMA_34CD;
		break;
	case 37 ... 38:
		backlightlevel = GAMMA_37CD;
		break;
	case 39 ... 40:
		backlightlevel = GAMMA_39CD;
		break;
	case 41 ... 43:
		backlightlevel = GAMMA_41CD;
		break;
	case 44 ... 46:
		backlightlevel = GAMMA_44CD;
		break;
	case 47 ... 49:
		backlightlevel = GAMMA_47CD;
		break;
	case 50 ... 52:
		backlightlevel = GAMMA_50CD;
		break;
	case 53 ... 55:
		backlightlevel = GAMMA_53CD;
		break;
	case 56 ... 59:
		backlightlevel = GAMMA_56CD;
		break;
	case 60 ... 63:
		backlightlevel = GAMMA_60CD;
		break;
	case 64 ... 67:
		backlightlevel = GAMMA_64CD;
		break;
	case 68 ... 71:
		backlightlevel = GAMMA_68CD;
		break;
	case 72 ... 76:
		backlightlevel = GAMMA_72CD;
		break;
	case 77 ... 81:
		backlightlevel = GAMMA_77CD;
		break;
	case 82 ... 86:
		backlightlevel = GAMMA_82CD;
		break;
	case 87 ... 92:
		backlightlevel = GAMMA_87CD;
		break;
	case 93 ... 97:
		backlightlevel = GAMMA_93CD;
		break;
	case 98 ... 104:
		backlightlevel = GAMMA_98CD;
		break;
	case 105 ... 110:
		backlightlevel = GAMMA_105CD;
		break;
	case 111 ... 118:
		backlightlevel = GAMMA_111CD;
		break;
	case 119 ... 125:
		backlightlevel = GAMMA_119CD;
		break;
	case 126 ... 133:
		backlightlevel = GAMMA_126CD;
		break;
	case 134 ... 142:
		backlightlevel = GAMMA_134CD;
		break;
	case 143 ... 151:
		backlightlevel = GAMMA_143CD;
		break;
	case 152 ... 161:
		backlightlevel = GAMMA_152CD;
		break;
	case 162 ... 171:
		backlightlevel = GAMMA_162CD;
		break;
	case 172 ... 182:
		backlightlevel = GAMMA_172CD;
		break;
	case 183 ... 194:
		backlightlevel = GAMMA_183CD;
		break;
	case 195 ... 206:
		backlightlevel = GAMMA_195CD;
		break;
	case 207 ... 219:
		backlightlevel = GAMMA_207CD;
		break;
	case 220 ... 233:
		backlightlevel = GAMMA_220CD;
		break;
	case 234 ... 248:
		backlightlevel = GAMMA_234CD;
		break;
	case 249:
		backlightlevel = GAMMA_249CD;
		break;
	case 250 ... 251:
		backlightlevel = GAMMA_265CD;
		break;
	case 252 ... 253:
		backlightlevel = GAMMA_282CD;
		break;
	case 254 ... 255:
		backlightlevel = GAMMA_300CD;
		break;
	default:
		backlightlevel = DEFAULT_GAMMA_LEVEL;
		break;
	}

	return backlightlevel;
}

static int s6e88a0_gamma_ctl(void)
{
	s6e88a0_write(lcd_info_bklt_data.gamma_table[lcd_info_bklt_data.bl],
							GAMMA_PARAM_SIZE);

	return 0;
}

static int s6e88a0_aid_parameter_ctl(u8 force)
{
	if (force)
		goto aid_update;
	else if (lcd_info_bklt_data.aor[lcd_info_bklt_data.bl][0x04] !=
		lcd_info_bklt_data.aor[lcd_info_bklt_data.current_bl][0x04])
		goto aid_update;
	else if (lcd_info_bklt_data.aor[lcd_info_bklt_data.bl][0x05] !=
		lcd_info_bklt_data.aor[lcd_info_bklt_data.current_bl][0x05])
		goto aid_update;
	else
		goto exit;

aid_update:
	s6e88a0_write(lcd_info_bklt_data.aor[lcd_info_bklt_data.bl],
								AID_PARAM_SIZE);
exit:
	s6e88a0_write(SEQ_GAMMA_UPDATE, ARRAY_SIZE(SEQ_GAMMA_UPDATE));
	return 0;
}

static int s6e88a0_set_acl(u8 force)
{
	int ret = 0, level;
	char *temp;
	level = ACL_STATUS_40P;
	if (lcd_info_bklt_data.siop_enable ||
			LEVEL_IS_HBM(lcd_info_bklt_data.auto_brightness))
		goto acl_update;

	if (!lcd_info_bklt_data.acl_enable)
		level = ACL_STATUS_0P;

acl_update:
	temp = (unsigned char *)ACL_CUTOFF_TABLE[level];
	if (force || lcd_info_bklt_data.current_acl !=
				ACL_CUTOFF_TABLE[level][1]) {
		ret = s6e88a0_write(temp, ACL_PARAM_SIZE);
		lcd_info_bklt_data.current_acl = ACL_CUTOFF_TABLE[level][1];
		pr_info("acl: %d, auto_brightness: %d\n",
				lcd_info_bklt_data.current_acl,
				lcd_info_bklt_data.auto_brightness);
	}

	if (!ret)
		ret = -EPERM;

	return ret;
}

static int s6e88a0_set_elvss(u8 force)
{
	int ret = 0, elvss_level = 0;
	u32 candela = candela_table[lcd_info_bklt_data.bl];

	switch (candela) {
	case 0 ... 105:
		elvss_level = ELVSS_STATUS_105;
		break;
	case 106 ... 111:
		elvss_level = ELVSS_STATUS_111;
		break;
	case 112 ... 119:
		elvss_level = ELVSS_STATUS_119;
		break;
	case 120 ... 126:
		elvss_level = ELVSS_STATUS_126;
		break;
	case 127 ... 134:
		elvss_level = ELVSS_STATUS_134;
		break;
	case 135 ... 143:
		elvss_level = ELVSS_STATUS_143;
		break;
	case 144 ... 152:
		elvss_level = ELVSS_STATUS_152;
		break;
	case 153 ... 162:
		elvss_level = ELVSS_STATUS_162;
		break;
	case 163 ... 172:
		elvss_level = ELVSS_STATUS_172;
		break;
	case 173 ... 183:
		elvss_level = ELVSS_STATUS_183;
		break;
	case 184 ... 195:
		elvss_level = ELVSS_STATUS_195;
		break;
	case 196 ... 207:
		elvss_level = ELVSS_STATUS_207;
		break;
	case 208 ... 220:
		elvss_level = ELVSS_STATUS_220;
		break;
	case 221 ... 234:
		elvss_level = ELVSS_STATUS_234;
		break;
	case 235 ... 249:
		elvss_level = ELVSS_STATUS_249;
		break;
	case 250 ... 265:
		elvss_level = ELVSS_STATUS_265;
		break;
	case 266 ... 282:
		elvss_level = ELVSS_STATUS_282;
		break;
	case 283 ... 299:
		elvss_level = ELVSS_STATUS_300;
		break;
	case 400:
		elvss_level = ELVSS_STATUS_HBM;
		break;
	default:
		elvss_level = ELVSS_STATUS_300;
		break;
	}

	if (force || lcd_info_bklt_data.current_elvss != elvss_level) {
		ret = s6e88a0_write(lcd_info_bklt_data.elvss_table[elvss_level],
						ELVSS_PARAM_SIZE);
		lcd_info_bklt_data.current_elvss = elvss_level;
	}

	if (!ret) {
		ret = -EPERM;
		goto elvss_err;
	}

elvss_err:
	return ret;
}

static void init_dynamic_aid(void)
{
	lcd_info_bklt_data.daid.vreg = VREG_OUT_X1000;
	lcd_info_bklt_data.daid.iv_tbl = index_voltage_table;
	lcd_info_bklt_data.daid.iv_max = IV_MAX;
	lcd_info_bklt_data.daid.mtp = kzalloc(IV_MAX * CI_MAX * sizeof(int),
								GFP_KERNEL);
	lcd_info_bklt_data.daid.gamma_default = gamma_default;
	lcd_info_bklt_data.daid.formular = gamma_formula;
	lcd_info_bklt_data.daid.vt_voltage_value = vt_voltage_value;

	lcd_info_bklt_data.daid.ibr_tbl = index_brightness_table;
	lcd_info_bklt_data.daid.ibr_max = IBRIGHTNESS_MAX;
	lcd_info_bklt_data.daid.br_base = brightness_base_table;
	lcd_info_bklt_data.daid.gc_tbls = gamma_curve_tables;
	lcd_info_bklt_data.daid.gc_lut = gamma_curve_lut;
	lcd_info_bklt_data.daid.offset_gra = offset_gradation;
	lcd_info_bklt_data.daid.offset_color =
				(const struct rgb_t(*)[])offset_color;
}

static void init_mtp_data(const u8 *mtp_data)
{
	int i, c, j;
	int *mtp;

	mtp = lcd_info_bklt_data.daid.mtp;

	for (c = 0, j = 0; c < CI_MAX; c++, j++) {
		if (mtp_data[j++] & 0x01)
			mtp[(IV_MAX-1)*CI_MAX+c] = mtp_data[j] * (-1);
		else
			mtp[(IV_MAX-1)*CI_MAX+c] = mtp_data[j];
	}

	for (i = IV_203; i >= 0; i--) {
		for (c = 0; c < CI_MAX; c++, j++) {
			if (mtp_data[j] & 0x80)
				mtp[CI_MAX*i+c] = (mtp_data[j] & 0x7F) * (-1);
			else
				mtp[CI_MAX*i+c] = mtp_data[j];
		}
	}
}

static int init_gamma_table(const u8 *mtp_data)
{
	int i, c, j, v;
	int ret = 0;
	int *pgamma;
	int **gamma_table;

	/* allocate memory for local gamma table */
	gamma_table = kzalloc(IBRIGHTNESS_MAX * sizeof(int *),
							GFP_KERNEL);
	if (!gamma_table) {
		pr_err("failed to allocate gamma table\n");
		ret = -ENOMEM;
		goto err_alloc_gamma_table;
	}

	for (i = 0; i < IBRIGHTNESS_MAX; i++) {
		gamma_table[i] = kzalloc(IV_MAX*CI_MAX * sizeof(int),
							GFP_KERNEL);
		if (!gamma_table[i]) {
			pr_err("failed to allocate gamma\n");
			ret = -ENOMEM;
			goto err_alloc_gamma;
		}
	}

	/* allocate memory for gamma table */
	lcd_info_bklt_data.gamma_table = kzalloc(GAMMA_MAX * sizeof(u8 *),
							GFP_KERNEL);
	if (!lcd_info_bklt_data.gamma_table) {
		pr_err("failed to allocate gamma table 2\n");
		ret = -ENOMEM;
		goto err_alloc_gamma_table2;
	}

	for (i = 0; i < GAMMA_MAX; i++) {
		lcd_info_bklt_data.gamma_table[i] =
			kzalloc(GAMMA_PARAM_SIZE * sizeof(u8), GFP_KERNEL);
		if (!lcd_info_bklt_data.gamma_table[i]) {
			pr_err("failed to allocate gamma 2\n");
			ret = -ENOMEM;
			goto err_alloc_gamma2;
		}
		lcd_info_bklt_data.gamma_table[i][0] = 0xCA;
	}

	/* calculate gamma table */
	init_mtp_data(mtp_data);
	dynamic_aid(lcd_info_bklt_data.daid, gamma_table);

	/* relocate gamma order */
	for (i = 0; i < GAMMA_MAX; i++) {
		/* Brightness table */
		v = IV_MAX - 1;
		pgamma = &gamma_table[i][v * CI_MAX];
		for (c = 0, j = 1; c < CI_MAX; c++, pgamma++) {
			if (*pgamma & 0x100)
				lcd_info_bklt_data.gamma_table[i][j++] = 1;
			else
				lcd_info_bklt_data.gamma_table[i][j++] = 0;

			lcd_info_bklt_data.gamma_table[i][j++] = *pgamma & 0xff;
		}

		for (v = IV_MAX - 2; v >= 0; v--) {
			pgamma = &gamma_table[i][v * CI_MAX];
			for (c = 0; c < CI_MAX; c++, pgamma++)
				lcd_info_bklt_data.gamma_table[i][j++] =
									*pgamma;
		}
	}

	/* free local gamma table */
	for (i = 0; i < IBRIGHTNESS_MAX; i++)
		kfree(gamma_table[i]);
	kfree(gamma_table);

	return 0;

err_alloc_gamma2:
	while (i > 0) {
		kfree(lcd_info_bklt_data.gamma_table[i-1]);
		i--;
	}
	kfree(lcd_info_bklt_data.gamma_table);
err_alloc_gamma_table2:
	i = IBRIGHTNESS_MAX;
err_alloc_gamma:
	while (i > 0) {
		kfree(gamma_table[i-1]);
		i--;
	}
	kfree(gamma_table);
err_alloc_gamma_table:
	return ret;
}

static int init_aid_dimming_table(const u8 *mtp_data)
{
	int i;

	for (i = 0; i < GAMMA_MAX; i++) {
		memcpy(lcd_info_bklt_data.aor[i],
				SEQ_AOR_CONTROL, ARRAY_SIZE(SEQ_AOR_CONTROL));
		lcd_info_bklt_data.aor[i][4] = aor_cmd[i][0];
		lcd_info_bklt_data.aor[i][5] = aor_cmd[i][1];
	}

	return 0;
}

static int init_elvss_table(u8 *elvss_data)
{
	int i, j, ret;

	lcd_info_bklt_data.elvss_table =
		kzalloc(ELVSS_STATUS_MAX * sizeof(u8 *), GFP_KERNEL);

	if (IS_ERR_OR_NULL(lcd_info_bklt_data.elvss_table)) {
		pr_err("failed to allocate elvss table\n");
		ret = -ENOMEM;
		goto err_alloc_elvss_table;
	}

	for (i = 0; i < ELVSS_STATUS_MAX; i++) {
		lcd_info_bklt_data.elvss_table[i] =
			kzalloc(ELVSS_PARAM_SIZE * sizeof(u8), GFP_KERNEL);
		if (IS_ERR_OR_NULL(lcd_info_bklt_data.elvss_table[i])) {
			pr_err("failed to allocate elvss\n");
			ret = -ENOMEM;
			goto err_alloc_elvss;
		}

		for (j = 0; j < LDI_ELVSS_LEN; j++)
			lcd_info_bklt_data.elvss_table[i][j+1] = elvss_data[j];

		lcd_info_bklt_data.elvss_table[i][0] = 0xB6;
		lcd_info_bklt_data.elvss_table[i][1] = 0x28;
		lcd_info_bklt_data.elvss_table[i][2] = ELVSS_TABLE[i];
	}

	return 0;

err_alloc_elvss:
	/* should be kfree elvss with k */
	while (i > 0) {
		kfree(lcd_info_bklt_data.elvss_table[i-1]);
		i--;
	}
	kfree(lcd_info_bklt_data.elvss_table);
err_alloc_elvss_table:
	return ret;
}

static int init_hbm_parameter(u8 *elvss_data, u8 *hbm_data)
{
	/* CA 1~6 = B5 13~18 */
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][1] = hbm_data[12];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][2] = hbm_data[13];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][3] = hbm_data[14];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][4] = hbm_data[15];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][5] = hbm_data[16];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][6] = hbm_data[17];

	/* CA 7~9 = B5 26~28 */
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][7] = hbm_data[25];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][8] = hbm_data[26];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][9] = hbm_data[27];

	/* CA 10~21 = B6 3~14 */
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][10] = elvss_data[2];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][11] = elvss_data[3];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][12] = elvss_data[4];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][13] = elvss_data[5];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][14] = elvss_data[6];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][15] = elvss_data[7];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][16] = elvss_data[8];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][17] = elvss_data[9];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][18] = elvss_data[10];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][19] = elvss_data[11];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][20] = elvss_data[12];
	lcd_info_bklt_data.gamma_table[GAMMA_HBM][21] = elvss_data[13];

	/* B6 17th = B5 19th */
	lcd_info_bklt_data.elvss_table[ELVSS_STATUS_HBM][17] = hbm_data[18];

	return 0;
}

static int update_brightness(u8 force)
{
	u32 brightness;

	mutex_lock(&lcd_info_bklt_data.bl_lock);

	brightness = lcd_info_bklt_data.bd->props.brightness;
	lcd_info_bklt_data.bl = get_backlight_level_from_brightness(brightness);

	if (LEVEL_IS_HBM(lcd_info_bklt_data.auto_brightness) &&
		(brightness == lcd_info_bklt_data.bd->props.max_brightness))
		lcd_info_bklt_data.bl = GAMMA_HBM;

	if ((force) || ((lcd_info_bklt_data.ldi_enable) &&
		(lcd_info_bklt_data.current_bl != lcd_info_bklt_data.bl))) {
		s6e88a0_gamma_ctl();

		s6e88a0_aid_parameter_ctl(force);

		s6e88a0_set_acl(force);

		s6e88a0_set_elvss(force);

		lcd_info_bklt_data.current_bl = lcd_info_bklt_data.bl;

	}

	mutex_unlock(&lcd_info_bklt_data.bl_lock);

	return 0;
}


int s6e88a0_backlightinit(struct panel_info *panel_data)
{
	int ret = 0;
	unsigned char read_data[60];
	u8 mtp_data[LDI_MTP_LEN] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	u8 elvss_data[LDI_ELVSS_LEN] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0 };
	u8 hbm_data[LDI_HBM_LEN] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	lcd_info_bklt_data.bl = DEFAULT_GAMMA_LEVEL;
	lcd_info_bklt_data.current_bl = lcd_info_bklt_data.bl;
	lcd_info_bklt_data.bd->props.brightness = DEFAULT_BRIGHTNESS;
	lcd_info_bklt_data.acl_enable = 0;
	lcd_info_bklt_data.current_acl = 0;
	lcd_info_bklt_data.ldi_enable = 1;
	lcd_info_bklt_data.auto_brightness = 0;
	lcd_info_bklt_data.connected = 1;
	lcd_info_bklt_data.siop_enable = 0;
	panel_data->pack_send_mode = RT_DISPLAY_SEND_MODE_LP;
	s6e88a0_write(SEQ_TEST_KEY_ON_F0,
				ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	s6e88a0_write(SEQ_TEST_KEY_ON_FC,
				ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));
	s6e88a0_write(SEQ_SRC_LATCH, ARRAY_SIZE(SEQ_SRC_LATCH));
	s6e88a0_write(SEQ_AVDD, ARRAY_SIZE(SEQ_AVDD));
	s6e88a0_write(SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));

	msleep(20);
	s6e88a0_write(SEQ_GAMMA_CONDITION_SET,
				ARRAY_SIZE(SEQ_GAMMA_CONDITION_SET));
	s6e88a0_write(SEQ_AOR_CONTROL,
				ARRAY_SIZE(SEQ_AOR_CONTROL));
	s6e88a0_write(SEQ_ELVS_CONDITION,
				ARRAY_SIZE(SEQ_ELVS_CONDITION));
	s6e88a0_write(SEQ_GAMMA_UPDATE,
				ARRAY_SIZE(SEQ_GAMMA_UPDATE));
	msleep(120);

	ret = s6e88a0_read_id(&read_data[0]);
	if (ret < 0) {
		pr_err("panel is not connected well\n");
		lcd_info_bklt_data.connected = 0;
		return -1;
	}
	pr_info("read_data(RDID0) = %02X\n", read_data[0]);
	pr_info("read_data(RDID1) = %02X\n", read_data[1]);
	pr_info("read_data(RDID2) = %02X\n",	read_data[2]);

	if (read_data[2] == 0x03)
		s6e88a0_write(SEQ_PANEL_CONDITION_SET_03,
			ARRAY_SIZE(SEQ_PANEL_CONDITION_SET_03));
	else
		s6e88a0_write(SEQ_PANEL_CONDITION_SET,
			ARRAY_SIZE(SEQ_PANEL_CONDITION_SET));
	msleep(120);
	s6e88a0_write(SEQ_AVC, ARRAY_SIZE(SEQ_AVC));
	s6e88a0_write(SEQ_TEST_KEY_OFF_FC,
			ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));
	s6e88a0_write(SEQ_DISPLAY_ON,
				ARRAY_SIZE(SEQ_DISPLAY_ON));
	if (lcd_info_bklt_data.connected)
		msleep(20);
	s6e88a0_read_mtp(mtp_data);
	s6e88a0_read_elvss(elvss_data);
	s6e88a0_read_coordinate(hbm_data);
	init_dynamic_aid();
	ret = init_gamma_table(mtp_data);
	ret += init_aid_dimming_table(mtp_data);
	ret += init_elvss_table(elvss_data);
	ret += init_hbm_parameter(elvss_data, hbm_data);
	panel_data->pack_send_mode = RT_DISPLAY_SEND_MODE_HS;
	return 0;
}



static int s6e88a0_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	void *screen_handle;
	screen_disp_delete disp_delete;
	pr_debug("%s: brightness=%d\n", __func__, bd->props.brightness);

	if (bd->props.brightness < MIN_BRIGHTNESS ||
		bd->props.brightness > bd->props.max_brightness) {
		pr_err("lcd brightness should be %d to %d. now %d\n",
							MIN_BRIGHTNESS,
				lcd_info_bklt_data.bd->props.max_brightness,
							bd->props.brightness);
		return -EINVAL;
	}
	if (lcd_info_bklt_data.ldi_enable) {
		screen_handle =  screen_display_new();
		ret = update_brightness(0);
		disp_delete.handle = screen_handle;
		screen_display_delete(&disp_delete);
	if (ret < 0) {
			pr_err("err in %s\n", __func__);
			return -EINVAL;
		}
	}

	return ret;
}

static int s6e88a0_get_brightness(struct backlight_device *bd)
{
	struct lcd_info_bklt *lcd = bl_get_data(bd);
	return candela_table[lcd->bl];
}

static const struct backlight_ops s6e88a0_backlight_ops  = {
	.get_brightness = s6e88a0_get_brightness,
	.update_status = s6e88a0_set_brightness,
};

static ssize_t power_reduce_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", lcd->acl_enable);
	strcpy(buf, temp);

	return strlen(buf);
}

static ssize_t power_reduce_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	int value;
	int rc;
	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0) {
		return rc;
	} else {
		if (lcd->acl_enable != value) {
			pr_info("%s: %d, %d\n", __func__, lcd->acl_enable,
									value);
			mutex_lock(&lcd->bl_lock);
			lcd->acl_enable = value;
			mutex_unlock(&lcd->bl_lock);
			if (lcd->ldi_enable)
				update_brightness(1);
		}
	}
	return size;
}

static ssize_t window_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	char temp[15];

	if (lcd->ldi_enable)
		s6e88a0_read_id(lcd->id);

	sprintf(temp, "%x %x %x\n", lcd->id[0], lcd->id[1], lcd->id[2]);
	strcat(buf, temp);
	return strlen(buf);
}

static ssize_t auto_brightness_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	char temp[3];
	sprintf(temp, "%d\n", lcd->auto_brightness);
	strcpy(buf, temp);
	return strlen(buf);
}

static ssize_t auto_brightness_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	int value;
	int rc;
	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0) {
		return rc;
	} else {
		if (lcd->auto_brightness != value) {
			pr_info("%s: %d, %d\n", __func__,
					lcd->auto_brightness, value);
			mutex_lock(&lcd->bl_lock);
			lcd->auto_brightness = value;
			mutex_unlock(&lcd->bl_lock);
			if (lcd->ldi_enable)
				update_brightness(0);
		}
	}
	return size;
}

static ssize_t siop_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	char temp[3];
	sprintf(temp, "%d\n", lcd->siop_enable);
	strcpy(buf, temp);
	return strlen(buf);
}

static ssize_t siop_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	int value;
	int rc;
	rc = kstrtoul(buf, (unsigned int)0, (unsigned long *)&value);
	if (rc < 0)
		return rc;
	else {
		if (lcd->siop_enable != value) {
			pr_info("%s: %d, %d\n", __func__, lcd->siop_enable,
									value);
			mutex_lock(&lcd->bl_lock);
			lcd->siop_enable = value;
			mutex_unlock(&lcd->bl_lock);
			if (lcd->ldi_enable)
				update_brightness(1);
		}
	}
	return size;
}
static ssize_t manufacture_date_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	u16 year;
	u8 month, manufacture_data[LDI_HBM_LEN] = {0,};

	if (lcd->ldi_enable)
		s6e88a0_read(LDI_HBM_REG, manufacture_data, LDI_HBM_LEN);

	year = ((manufacture_data[23] & 0xF0) >> 4) + 2011;
	month = manufacture_data[23] & 0xF;

	sprintf(buf, "%d, %d, %d\n", year, month, manufacture_data[24]);

	return strlen(buf);
}

static ssize_t parameter_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct lcd_info_bklt *lcd = dev_get_drvdata(dev);
	char *pos = buf;
	unsigned char temp[50] = {0,};
	int i;

	if (!lcd->ldi_enable)
		return -EINVAL;

	s6e88a0_write(SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));

	/* ID */
	s6e88a0_read(LDI_ID_REG, temp, LDI_ID_LEN);
	pos += sprintf(pos, "ID    [04]: ");
	for (i = 0; i < LDI_ID_LEN; i++)
		pos += sprintf(pos, "%02x, ", temp[i]);
	pos += sprintf(pos, "\n");

	/* ACL */
	s6e88a0_read(0x56, temp, ACL_PARAM_SIZE);
	pos += sprintf(pos, "ACL   [56]: ");
	for (i = 0; i < ACL_PARAM_SIZE; i++)
		pos += sprintf(pos, "%02x, ", temp[i]);
	pos += sprintf(pos, "\n");

	/* ACL Parameter */
	s6e88a0_read(0xB5, temp, 5);
	pos += sprintf(pos, "ACL   [B5]: ");
	for (i = 0; i < 5; i++)
		pos += sprintf(pos, "%02x, ", temp[i]);
	pos += sprintf(pos, "\n");

	/* ACL Result */
	s6e88a0_read(0xB3, temp, 13);
	pos += sprintf(pos, "ACL   [B3]: ");
	for (i = 0; i < 13; i++)
		pos += sprintf(pos, "%02x, ", temp[i]);
	pos += sprintf(pos, "\n");

	/* ELVSS */
	s6e88a0_read(LDI_ELVSS_REG, temp, ELVSS_PARAM_SIZE);
	pos += sprintf(pos, "ELVSS [B6]: ");
	for (i = 0; i < ELVSS_PARAM_SIZE; i++)
		pos += sprintf(pos, "%02x, ", temp[i]);
	pos += sprintf(pos, "\n");

	/* GAMMA */
	s6e88a0_read(0xCA, temp, GAMMA_PARAM_SIZE);
	pos += sprintf(pos, "GAMMA [CA]: ");
	for (i = 0; i < GAMMA_PARAM_SIZE; i++)
		pos += sprintf(pos, "%02x, ", temp[i]);
	pos += sprintf(pos, "\n");

	/* MTP */
	s6e88a0_read(0xC8, temp, GAMMA_PARAM_SIZE);
	pos += sprintf(pos, "MTP   [C8]: ");
	for (i = 0; i < GAMMA_PARAM_SIZE; i++)
		pos += sprintf(pos, "%02x, ", temp[i]);
	pos += sprintf(pos, "\n");

	s6e88a0_write(SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));

	return pos - buf;
}

static struct lcd_ops S6E88A0_lcd_ops = {
	.set_power = NULL,
	.get_power = NULL,
};

static DEVICE_ATTR(power_reduce, 0664, power_reduce_show, power_reduce_store);
/* static DEVICE_ATTR(lcd_type, 0444, lcd_type_show, NULL); */
static DEVICE_ATTR(window_type, 0444, window_type_show, NULL);
static DEVICE_ATTR(auto_brightness, 0644, auto_brightness_show,
							auto_brightness_store);
static DEVICE_ATTR(siop_enable, 0664, siop_enable_show, siop_enable_store);
/*static DEVICE_ATTR(color_coordinate, 0444, color_coordinate_show, NULL);*/
static DEVICE_ATTR(manufacture_date, 0444, manufacture_date_show, NULL);
static DEVICE_ATTR(parameter, 0444, parameter_show, NULL);

int s6e88a0_bklt_probe(struct panel_info *panel_data)
{
	int ret = 0;
	/* clear internal info */
	memset(&lcd_info_bklt_data, 0, sizeof(lcd_info_bklt_data));

	lcd_info_bklt_data.ld = lcd_device_register("panel",
		&panel_data->pdev->dev,	&lcd_info_bklt_data, &S6E88A0_lcd_ops);

	if (IS_ERR(lcd_info_bklt_data.ld)) {
		pr_err("bklt_data is NULL\n");
		return PTR_ERR(lcd_info_bklt_data.ld);
	}

	/* register device for backlight control */
	lcd_info_bklt_data.bd = backlight_device_register("panel",
		&panel_data->pdev->dev, NULL, &s6e88a0_backlight_ops, NULL);
	if (IS_ERR(lcd_info_bklt_data.bd)) {
		ret = PTR_ERR(lcd_info_bklt_data.bd);
		pr_err("failed to register backlight device %d\n", ret);
		goto out_free_backlight;
	}

	lcd_info_bklt_data.power = FB_BLANK_UNBLANK;
	lcd_info_bklt_data.connected = 0;
	lcd_info_bklt_data.bd->props.max_brightness = MAX_BRIGHTNESS;
	lcd_info_bklt_data.bd->props.brightness = INIT_BRIGHTNESS;

	ret = device_create_file(&lcd_info_bklt_data.ld->dev,
						&dev_attr_power_reduce);
	if (ret < 0)
		pr_err("failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd_info_bklt_data.ld->dev,
						&dev_attr_window_type);
	if (ret < 0)
		pr_err("failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd_info_bklt_data.bd->dev,
						&dev_attr_auto_brightness);
	if (ret < 0)
		pr_err("failed to add sysfs entries, %d\n", __LINE__);


	ret = device_create_file(&lcd_info_bklt_data.ld->dev,
							&dev_attr_siop_enable);
	if (ret < 0)
		pr_err("failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd_info_bklt_data.ld->dev,
						&dev_attr_manufacture_date);
	if (ret < 0)
		pr_err("failed to add sysfs entries, %d\n", __LINE__);

	ret = device_create_file(&lcd_info_bklt_data.ld->dev,
							&dev_attr_parameter);
	if (ret < 0)
		pr_err("failed to add sysfs entries, %d\n", __LINE__);

	dev_set_drvdata(&lcd_info_bklt_data.bd->dev, &lcd_info_bklt_data);

	mutex_init(&lcd_info_bklt_data.lock);
	mutex_init(&lcd_info_bklt_data.bl_lock);


out_free_backlight:
	lcd_device_unregister(lcd_info_bklt_data.ld);

	return ret;
}

int s6e88a0_demise_sequence(void)
{
	s6e88a0_write(SEQ_DISPLAY_OFF, ARRAY_SIZE(SEQ_DISPLAY_OFF));
	msleep(34);
	s6e88a0_write(SEQ_SLEEP_IN, ARRAY_SIZE(SEQ_SLEEP_IN));
	msleep(120);
	return 0;
}

int s6e88a0_init_sequence(void)
{
	int ret = 0;
	unsigned char read_data[60];
	msleep(20);

	s6e88a0_write(SEQ_TEST_KEY_ON_F0, ARRAY_SIZE(SEQ_TEST_KEY_ON_F0));
	s6e88a0_write(SEQ_TEST_KEY_ON_FC, ARRAY_SIZE(SEQ_TEST_KEY_ON_FC));

	s6e88a0_write(SEQ_SRC_LATCH, ARRAY_SIZE(SEQ_SRC_LATCH));
	s6e88a0_write(SEQ_AVDD, ARRAY_SIZE(SEQ_AVDD));
	s6e88a0_write(SEQ_SLEEP_OUT, ARRAY_SIZE(SEQ_SLEEP_OUT));

	msleep(20);
	ret = s6e88a0_read_id(&read_data[0]);
	if (ret < 0)
		return -1;

	pr_info("read_data(RDID0) = %02X\n", read_data[0]);
	pr_info("read_data(RDID1) = %02X\n", read_data[1]);
	pr_info("read_data(RDID2) = %02X\n",	read_data[2]);
	update_brightness(1);

	if (read_data[2] == 0x03)
		s6e88a0_write(SEQ_PANEL_CONDITION_SET_03,
					ARRAY_SIZE(SEQ_PANEL_CONDITION_SET_03));
	else
		s6e88a0_write(SEQ_PANEL_CONDITION_SET,
			ARRAY_SIZE(SEQ_PANEL_CONDITION_SET));

	msleep(120);

	s6e88a0_write(SEQ_AVC, ARRAY_SIZE(SEQ_AVC));
	s6e88a0_write(SEQ_TEST_KEY_OFF_FC, ARRAY_SIZE(SEQ_TEST_KEY_OFF_FC));
	s6e88a0_write(SEQ_DISPLAY_ON, ARRAY_SIZE(SEQ_DISPLAY_ON));

	return ret;
}

void s6e88a0_bklt_remove(void)
{
	dev_set_drvdata(&lcd_info_bklt_data.bd->dev, NULL);
	backlight_device_unregister(lcd_info_bklt_data.bd);
}

int s6e88a0_bklt_resume(void)
{
	lcd_info_bklt_data.ldi_enable = 1;
	s6e88a0_init_sequence();
	return 0;
}

int s6e88a0_bklt_suspend(void)
{
	lcd_info_bklt_data.ldi_enable = 0;
	s6e88a0_demise_sequence();
	return 0;
}

