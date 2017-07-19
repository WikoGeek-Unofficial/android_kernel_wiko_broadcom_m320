/*
 * fsi-d2153.c - FSI ASoC driver for boards using d2153 codec.
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <mach/common.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/fsi_d2153.h>
#include <linux/d2153/d2153_codec.h>
#include <linux/d2153/d2153_aad.h>
#include <linux/platform_data/fsi_d2153_pdata.h>

#include <sound/sh_fsi.h>
#include <sound/soundpath/soundpath.h>
#include <sound/soundpath/scuw_extern.h>

#if 1 /*** Analog audio dock support ***/

#include <mach/pm.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>

/* DEFINE Definitions */
#define GPIO_DOCK_EN	GPIO_PORT33

#endif

#define MIN_VOL_DB 0x000003FF


/*
 * Marco Definition
 */
#define D2153_SNDP_MCLK_RATE	13000000

#define D2153_PLAYBACK_STREAM_NAME	"Playback"
#define D2153_CAPTURE_STREAM_NAME	"Capture"

struct snd_soc_dapm_widget *playback_widget;
struct snd_soc_dapm_widget *capture_widget;

/* Added for FM */
static bool g_fm_mute_flg;
/* Added for FM */
struct clk *vclk4_clk;
struct clk *main_clk;

static unsigned int mclk;
u_int  g_mode = SNDP_MODE_NORMAL;

static unsigned int path_value[SNDRV_PCM_STREAM_LAST + 1];


static DEFINE_SPINLOCK(fsi_d2153_lock); /* Guards the ignore suspend */

static int vclk4_supply_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event);
static void fsi_d2153_set_active(struct snd_soc_codec *codec,
	const char *stream, int active);

struct fsi_d2153_priv {
	bool hp_spk_path_en;
	bool spk_en;
	bool hp_en;
	bool ep_en;
};

static struct snd_soc_codec *fsi_d2153_codec;
static struct snd_soc_pcm_runtime *fsi_d2153_rtd;
static struct snd_soc_dai_ops fsi_d2153_ops_save;

static void fsi_d2153_set_active(struct snd_soc_codec *codec,
	const char *stream, int active)
{
	struct snd_soc_dapm_widget *w;
	struct fsi_d2153_priv *priv =
			snd_soc_card_get_drvdata(codec->card);
	static int _d2153_set_adc_active;

	/* Add checks to see if the Global Pointers are still intact */
	if (priv == NULL) {
		sndp_log_err("%s: Global Pntrs are NULL.fsi_d2153_priv=%x\n",
			__func__, (unsigned int)priv);
		return;
	}
	if (strstr(stream, D2153_PLAYBACK_STREAM_NAME))
		w = playback_widget;
	else
		w = capture_widget;

	if (capture_widget == w) {
		if (!active) {
			sndp_log_info("adc mute\n");
			snd_soc_update_bits(codec, D2153_ADC_L_CTRL,
				D2153_ADC_MUTE_EN, D2153_ADC_MUTE_EN);
			snd_soc_update_bits(codec, D2153_ADC_R_CTRL,
				D2153_ADC_MUTE_EN, D2153_ADC_MUTE_EN);
		}
		msleep(20);
	}

	mutex_lock_nested(&codec->card->dapm_mutex,
		SND_SOC_DAPM_CLASS_RUNTIME);
	dapm_mark_dirty(w, "fsi_d2153_set_active");
	w->active = active;
	mutex_unlock(&codec->card->dapm_mutex);

	printk(KERN_INFO "w->name[%s] w->active[%d]\n",
		w->name, w->active);
	snd_soc_dapm_sync(&codec->dapm);

	/* Un-mute output device */
	if (playback_widget == w) {
		if (active) {
			if (priv->spk_en) {
				msleep(10);
				snd_soc_update_bits(codec, D2153_SP_CTRL,
					D2153_SP_AMP_MUTE_EN, 0);
				sndp_log_info("spk unmute\n");
			}
			if (priv->ep_en) {
				snd_soc_update_bits(codec, D2153_EP_CTRL,
					D2153_EP_AMP_MUTE_EN, 0);
				sndp_log_info("ep unmute\n");
				msleep(50);
			}
			if (priv->hp_en) {
				snd_soc_update_bits(codec, D2153_HP_L_CTRL,
					D2153_HP_AMP_MUTE_EN, 0);
				snd_soc_update_bits(codec, D2153_HP_R_CTRL,
					D2153_HP_AMP_MUTE_EN, 0);
				sndp_log_info("hp unmute\n");
				msleep(50);
			}
		}
	} else {
		if (active) {
			if (!_d2153_set_adc_active)
				d2153_set_aif_adjust(codec);
			sndp_log_info("adc unmute\n");
			snd_soc_update_bits(codec, D2153_ADC_L_CTRL,
				D2153_ADC_MUTE_EN, 0);
			snd_soc_update_bits(codec, D2153_ADC_R_CTRL,
				D2153_ADC_MUTE_EN, 0);
		}
		msleep(20);
	}
}

void fsi_d2153_set_dac_power(struct snd_kcontrol *kcontrol,
	int status)
{
	struct snd_soc_codec *codec;
	struct snd_card *card;

	if (!kcontrol) {
		sndp_log_err("kcontrol is NULL\n");
		return;
	}
	codec = (struct snd_soc_codec *)kcontrol->private_data;
	card = codec->card->snd_card;

	sndp_log_info("start\n");

	mutex_lock_nested(&codec->card->dapm_mutex,
		SND_SOC_DAPM_CLASS_RUNTIME);
	if (!status) {
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack Left");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack Right");
		snd_soc_dapm_disable_pin(&codec->dapm, "Speaker");
		snd_soc_dapm_disable_pin(&codec->dapm, "Earpiece");
	} else {
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack Left");
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack Right");
		snd_soc_dapm_enable_pin(&codec->dapm, "Speaker");
		snd_soc_dapm_enable_pin(&codec->dapm, "Earpiece");
	}
	mutex_unlock(&codec->card->dapm_mutex);

	fsi_d2153_set_active(codec, D2153_PLAYBACK_STREAM_NAME, status);

	sndp_log_info("end\n");
	return;
}
EXPORT_SYMBOL(fsi_d2153_set_dac_power);

void fsi_d2153_set_adc_power(struct snd_kcontrol *kcontrol,
	int status)
{
	struct snd_soc_codec *codec;
	struct snd_card *card;

	if (!kcontrol) {
		sndp_log_err("kcontrol is NULL\n");
		return;
	}
	codec = (struct snd_soc_codec *)kcontrol->private_data;
	card = codec->card->snd_card;

	sndp_log_info("start\n");

	if (!status) {
		snd_soc_dapm_disable_pin(&codec->dapm, "RECCHL");
		snd_soc_dapm_disable_pin(&codec->dapm, "RECCHR");
	} else {
		snd_soc_dapm_enable_pin(&codec->dapm, "RECCHL");
		snd_soc_dapm_enable_pin(&codec->dapm, "RECCHR");
	}

	fsi_d2153_set_active(codec, D2153_CAPTURE_STREAM_NAME, status);

	sndp_log_info("end\n");
	return;
}
EXPORT_SYMBOL(fsi_d2153_set_adc_power);

int fsi_d2153_set_ignore_suspend(struct snd_soc_card *card,
	unsigned int dev_id, unsigned int status)
{
	struct snd_soc_dapm_widget *w;

	if (!card || dev_id > card->num_rtd)
		return -EINVAL;
	else {
		list_for_each_entry(w, &card->widgets, list) {
			switch (w->id) {
			case snd_soc_dapm_adc:
			case snd_soc_dapm_aif_out:
			case snd_soc_dapm_output:
			case snd_soc_dapm_hp:
			case snd_soc_dapm_spk:
			case snd_soc_dapm_line:
			case snd_soc_dapm_dac:
			case snd_soc_dapm_aif_in:
			case snd_soc_dapm_vmid:
			case snd_soc_dapm_mic:
			case snd_soc_dapm_input:
				w->ignore_suspend = status;
				continue;
			default:
				continue;
			}
		}
		spin_lock(&fsi_d2153_lock);
		card->rtd[dev_id].dai_link->ignore_suspend = status;
		spin_unlock(&fsi_d2153_lock);
		sndp_log_info("dev_id[%d], ignore_suspend[%d]\n",
			dev_id, status);
	}
	return 0;
}

int fsi_d2153_enable_ignore_suspend(struct snd_soc_card *card,
	unsigned int dev_id)
{
	return fsi_d2153_set_ignore_suspend(card, dev_id, 1);
}
EXPORT_SYMBOL(fsi_d2153_enable_ignore_suspend);

int fsi_d2153_disable_ignore_suspend(struct snd_soc_card *card,
	unsigned int dev_id)
{
	return fsi_d2153_set_ignore_suspend(card, dev_id, 0);
}
EXPORT_SYMBOL(fsi_d2153_disable_ignore_suspend);

/*
 * While PT loopback execution, we do not execute
 * cpu_dai->driver->ops->shutdown() and
 * codec_dai->driver->ops->hw_params() callbacks.
 * The reason is shown below.
 * 1. shutdown() calls deactivate_output() on device-change, so loopback
 *    sound is muted unexpectedly.
 * 2. hw_params() sets codec sample rate for playing music (such as 48kHz).
 *    Some codec requires sample rate setting though its setting is slave mode.
 *    So, we have to keep the voice rate (16kHz) while PT loopback.
 */
int fsi_d2153_set_sampling_rate(struct snd_pcm_hw_params *params)
{
	u8 fs;

	/* Set sampling rate */
	switch (params_rate(params)) {
	case 8000:
		fs = D2153_SR_8000;
		break;
	case 11025:
		fs = D2153_SR_11025;
		break;
	case 12000:
		fs = D2153_SR_12000;
		break;
	case 16000:
		fs = D2153_SR_16000;
		break;
	case 22050:
		fs = D2153_SR_22050;
		break;
	case 32000:
		fs = D2153_SR_32000;
		break;
	case 44100:
		fs = D2153_SR_44100;
		break;
	case 48000:
		fs = D2153_SR_48000;
		break;
	case 88200:
		fs = D2153_SR_88200;
		break;
	case 96000:
		fs = D2153_SR_96000;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(fsi_d2153_codec, D2153_SR, fs);
	return 0;
}
EXPORT_SYMBOL(fsi_d2153_set_sampling_rate);

#define exchange_callback(from, to)	\
do {					\
	if ((to) == NULL) {		\
		(to) = (from);		\
		(from) = NULL;		\
	}				\
} while (0)

int fsi_d2153_loopback_notify(int status)
{
	int ret = 0;
	struct snd_soc_dai_ops *cpu_dai_ops
		= fsi_d2153_rtd->cpu_dai->driver->ops;
	struct snd_soc_dai_ops *codec_dai_ops
		= fsi_d2153_rtd->codec_dai->driver->ops;
	struct snd_soc_dai_ops *save = &fsi_d2153_ops_save;

	if (FSI_D2153_LOOPBACK_START == status) {
		sndp_log_info("FSI_D2153_LOOPBACK_START\n");
		exchange_callback(cpu_dai_ops->startup, save->startup);
		exchange_callback(cpu_dai_ops->shutdown, save->shutdown);
		exchange_callback(codec_dai_ops->hw_params, save->hw_params);
		exchange_callback(codec_dai_ops->hw_free, save->hw_free);
	} else if (FSI_D2153_LOOPBACK_STOP == status) {
		sndp_log_info("FSI_D2153_LOOPBACK_STOP\n");
		exchange_callback(save->startup, cpu_dai_ops->startup);
		exchange_callback(save->shutdown, cpu_dai_ops->shutdown);
		exchange_callback(save->hw_params, codec_dai_ops->hw_params);
		exchange_callback(save->hw_free, codec_dai_ops->hw_free);
	} else
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL(fsi_d2153_loopback_notify);

void fsi_d2153_soc_write(int dev)
{
	if (0 == dev) {
		snd_soc_write(fsi_d2153_codec, 0xc6, 0x98);
	} else {
/*		snd_soc_write(fsi_d2153_codec, 0xcd, 0x5F); */
/*		snd_soc_write(fsi_d2153_codec, 0xd0, 0x5F); */
	}
}
EXPORT_SYMBOL(fsi_d2153_soc_write);

#if 1 /*** Analog audio dock support ***/
/*!
   @brief PUT callback function for hooks control(Playback gpio setting)
   @param[-]	kcontrol	Not use
   @param[in]	ucontrol	Element data
   @retval	0		Successful
 */
int fsi_d2153_get_playback_gpio(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return ERROR_NONE;
}

/*!
   @brief PUT callback function for hooks control(Playback gpio setting)
   @param[-]	kcontrol	Not use
   @param[in]	ucontrol	Element data
   @retval	0		Successful
 */
int fsi_d2153_put_playback_gpio(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int state_gpio = 0;

	gpio_set_value(GPIO_DOCK_EN, ucontrol->value.enumerated.item[0]);
	/*** test code start ***/
	state_gpio = gpio_get_value(GPIO_DOCK_EN);
	printk(KERN_INFO "%s gpio_get_value(GPIO_DOCK_EN):%d\n",
		__func__, state_gpio);
	/*** test code end ***/
	return ERROR_NONE;
}
#endif

int fsi_d2153_sndp_soc_info(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

int fsi_d2153_sndp_soc_fmvol_info(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = MIN_VOL_DB;
	uinfo->value.integer.max = 0;

	return 0;
}

int fsi_d2153_sndp_soc_loopback_info(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 8;

	return 0;
}


int fsi_d2153_sndp_soc_get(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0]
		= path_value[SNDRV_PCM_STREAM_CAPTURE] |
		  path_value[SNDRV_PCM_STREAM_PLAYBACK];
	return 0;
}

int fsi_d2153_sndp_soc_sidevol_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = scuw_get_sidetone_val();
	return 0;
}


#define CAPTURE_DEVICE_MASK	0xfff00000
int fsi_d2153_sndp_soc_put(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val = ucontrol->value.integer.value[0];
	struct snd_soc_codec *codec;
	codec = (struct snd_soc_codec *)kcontrol->private_data;

	if (CAPTURE_DEVICE_MASK & val)
		path_value[SNDRV_PCM_STREAM_CAPTURE] = val;
	else
		path_value[SNDRV_PCM_STREAM_PLAYBACK] = val;

    g_mode = SNDP_GET_MODE_VAL(val);

    return sndp_soc_put(kcontrol, ucontrol);
}

int fsi_d2153_sndp_soc_sidevol_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val = ucontrol->value.integer.value[0];
	scuw_sidetone_val(val);

	return 0;
}

int fsi_d2153_snd_soc_get_adc(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

int fsi_d2153_snd_soc_put_dac(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =
		(struct snd_soc_codec *)kcontrol->private_data;
	unsigned int val;

	val = ucontrol->value.integer.value[0];
	fsi_d2153_set_active(codec, D2153_PLAYBACK_STREAM_NAME, val);

	return 0;
}

int fsi_d2153_snd_soc_get_dac(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

int fsi_d2153_snd_soc_get_sr(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

int fsi_d2153_snd_soc_put_sr(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	unsigned int val;
	struct snd_pcm_hw_params params;
	int retVal = 0;

	val = ucontrol->value.integer.value[0];
	params.intervals[SNDRV_PCM_HW_PARAM_RATE
		- SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = val;

	retVal = fsi_d2153_set_sampling_rate(&params);
	if (retVal != 0)
		sndp_log_err("Invalid Sampling Rate\n");
	else
		sndp_log_info("[Sampling Rate]:%d\n", val);
	return retVal;
}

int fsi_d2153_snd_soc_put_adc(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =
		(struct snd_soc_codec *)kcontrol->private_data;
	unsigned int val;

	val = ucontrol->value.integer.value[0];
	fsi_d2153_set_active(codec, D2153_CAPTURE_STREAM_NAME, val);

	if (!val) {
		sndp_log_info("Disable RECCH\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "RECCHL");
		snd_soc_dapm_disable_pin(&codec->dapm, "RECCHR");
	} else {
		sndp_log_info("Enable RECCH\n");
		snd_soc_dapm_enable_pin(&codec->dapm, "RECCHL");
		snd_soc_dapm_enable_pin(&codec->dapm, "RECCHR");
	}
	snd_soc_dapm_sync(&codec->dapm);
	return 0;
}

static int fsi_d2153_sndp_soc_get_voice_out_volume(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return sndp_soc_get_voice_out_volume(kcontrol, ucontrol);
}

static int fsi_d2153_sndp_soc_put_voice_out_volume(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return sndp_soc_put_voice_out_volume(kcontrol, ucontrol);
}

static int fsi_d2153_sndp_soc_get_playback_mute(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return sndp_soc_get_playback_mute(kcontrol, ucontrol);
}

static int fsi_d2153_sndp_soc_put_playback_mute(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return sndp_soc_put_playback_mute(kcontrol, ucontrol);
}

/* Added for FM */
static int fsi_d2153_sndp_soc_get_fmmute(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	/* Return the current settings */
	ucontrol->value.enumerated.item[0] = g_fm_mute_flg;
	return ERROR_NONE;
}

static int fsi_d2153_sndp_soc_put_fmmute(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int	iRet = ERROR_NONE;

	sndp_log_info("fsi_d2153_sndp_soc_put_fmmute ++ \n");

	g_fm_mute_flg = (ucontrol->value.enumerated.item[0]);
	sndp_log_info("sndp_soc_put_fmmute --mute value = %d \n", g_fm_mute_flg);
	scuw_set_fmmute(g_fm_mute_flg);
	return iRet;
}

static int fsi_d2153_sndp_soc_get_fmvolume(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	/* Return the current volume */
	return scuw_get_fmvolume();
}

static int fsi_d2153_sndp_soc_get_loopback(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return ERROR_NONE;
}

static int fsi_d2153_sndp_soc_put_fmvolume(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int	iRet = ERROR_NONE;
	int vol;

	sndp_log_info("fsi_d2153_sndp_soc_put_fmvolume ++ \n");

	vol = (ucontrol->value.enumerated.item[0]);
	sndp_log_info("sndp_soc_put_fmmute --mute value = %d \n", g_fm_mute_flg);
	scuw_set_fmvolume(vol);
	return iRet;
}

static int fsi_d2153_sndp_soc_put_loopback(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	u_int val, dev, en;

	/* SNDP_OUT_EARPIECE = 0x1 */
	/* SNDP_OUT_SPEAKER = 0x2 */
	/* SNDP_OUT_WIRED_HEADSET = 0x4 */
	/* LOOPBACK_ENABLE = 0X8 */

	val = (ucontrol->value.enumerated.item[0]);
	en = (val & 0x8) ? SNDP_ON : SNDP_OFF;
	dev = val & (SNDP_OUT_EARPIECE | SNDP_OUT_SPEAKER |
					SNDP_OUT_WIRED_HEADSET);

	sndp_pt_device_change(dev, en);

	return ERROR_NONE;
}

/* Added for FM */


static int fsi_d2153_sndp_spk_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct fsi_d2153_priv *priv =
			snd_soc_card_get_drvdata(codec->card);

	if (event & SND_SOC_DAPM_POST_PMU)
		priv->spk_en = true;
	else if (event & SND_SOC_DAPM_PRE_PMD) {
		priv->spk_en = false;
		snd_soc_update_bits(codec, D2153_SP_CTRL,
			D2153_SP_AMP_MUTE_EN, D2153_SP_AMP_MUTE_EN);
		msleep(50);
		sndp_log_info("spk mute\n");
	} else {
		/* Nothing to do.*/
	}
	return 0;
}

static int fsi_d2153_sndp_hp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct fsi_d2153_priv *priv =
			snd_soc_card_get_drvdata(codec->card);

	if (event & SND_SOC_DAPM_POST_PMU)
		priv->hp_en = true;
	else if (event & SND_SOC_DAPM_PRE_PMD) {
		priv->hp_en = false;
		snd_soc_update_bits(codec, D2153_HP_L_CTRL,
			D2153_HP_AMP_MUTE_EN, D2153_HP_AMP_MUTE_EN);
		snd_soc_update_bits(codec, D2153_HP_R_CTRL,
			D2153_HP_AMP_MUTE_EN, D2153_HP_AMP_MUTE_EN);
		if (snd_soc_dapm_get_pin_status(&codec->dapm,
			"Headphone Enable") || priv->hp_spk_path_en)
			msleep(25);
		sndp_log_info("hp mute\n");
	} else {
		/* Nothing to do.*/
	}
	return 0;
}

static int fsi_d2153_sndp_ep_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct fsi_d2153_priv *priv =
			snd_soc_card_get_drvdata(codec->card);

	if (event & SND_SOC_DAPM_POST_PMU)
		priv->ep_en = true;
	else if (event & SND_SOC_DAPM_PRE_PMD) {
		priv->ep_en = false;
		snd_soc_update_bits(codec, D2153_EP_CTRL,
			D2153_EP_AMP_MUTE_EN, D2153_EP_AMP_MUTE_EN);
		msleep(50);
		sndp_log_info("ep mute\n");
	} else {
		/* Nothing to do.*/
	}
	return 0;
}

static struct snd_kcontrol_new fsi_d2153_controls[] = {
	SOC_SINGLE_BOOL_EXT("ADC Activate", 0,
		fsi_d2153_snd_soc_get_adc, fsi_d2153_snd_soc_put_adc),
	FSI_SOC_SINGLE_EXT("Path", 0, fsi_d2153_sndp_soc_info,
		fsi_d2153_sndp_soc_get, fsi_d2153_sndp_soc_put),
	FSI_SOC_SINGLE("Earpiece Volume" , 0, 0, 25, 0,
		fsi_d2153_sndp_soc_get_voice_out_volume,
		fsi_d2153_sndp_soc_put_voice_out_volume),
	FSI_SOC_SINGLE("Earpiece Switch" , 0, 0, 1,  0,
		fsi_d2153_sndp_soc_get_playback_mute,
		fsi_d2153_sndp_soc_put_playback_mute),
	SOC_SINGLE_BOOL_EXT("DAC Activate", 0,
		fsi_d2153_snd_soc_get_dac, fsi_d2153_snd_soc_put_dac),
	FSI_SOC_SINGLE("Sampling Rate", 0, 0, 96000, 0,
		fsi_d2153_snd_soc_get_sr, fsi_d2153_snd_soc_put_sr),
#if 1 /*** Analog audio dock support ***/
	SOC_SINGLE_BOOL_EXT("Dock Switch" , 0,
		fsi_d2153_get_playback_gpio, fsi_d2153_put_playback_gpio),
#endif
	/*Added for FM*/
	FSI_SOC_SINGLE_EXT("FM Mute", 0, fsi_d2153_sndp_soc_info,
		fsi_d2153_sndp_soc_get_fmmute, fsi_d2153_sndp_soc_put_fmmute),
	/*Added for FM*/
	FSI_SOC_SINGLE_EXT("FM SetVolume", 0, fsi_d2153_sndp_soc_fmvol_info,
		fsi_d2153_sndp_soc_get_fmvolume, fsi_d2153_sndp_soc_put_fmvolume),
	FSI_SOC_SINGLE_EXT("Loopback Enable", 0,
		fsi_d2153_sndp_soc_loopback_info,
		fsi_d2153_sndp_soc_get_loopback,
		fsi_d2153_sndp_soc_put_loopback),
	FSI_SOC_SINGLE("Sidetone Attenuation", 0, 0, 1023, 0,
		fsi_d2153_sndp_soc_sidevol_get, fsi_d2153_sndp_soc_sidevol_put),
};

static const struct snd_soc_dapm_widget fsi_d2153_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("RECCHL"), /* Dummy widget */
	SND_SOC_DAPM_OUTPUT("RECCHR"), /* Dummy widget */
	SND_SOC_DAPM_SUPPLY("VCLK4", SND_SOC_NOPM, 0, 0, vclk4_supply_event,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SPK("Speaker", fsi_d2153_sndp_spk_event),
	SND_SOC_DAPM_HP("Headphone Jack Left", fsi_d2153_sndp_hp_event),
	SND_SOC_DAPM_HP("Headphone Jack Right", fsi_d2153_sndp_hp_event),
	SND_SOC_DAPM_LINE("Earpiece", fsi_d2153_sndp_ep_event),
	SND_SOC_DAPM_SWITCH("Headphone Enable", SND_SOC_NOPM, 0, 0, NULL),
};

static const struct snd_soc_dapm_route fsi_d2153_audio_map[] = {
	{"RECCHL", NULL, "AIFOUTL"},
	{"RECCHR", NULL, "AIFOUTR"},
	{"AIFINL", NULL, "VCLK4"},
	{"AIFINR", NULL, "VCLK4"},
	{"AIFOUTL", NULL, "VCLK4"},
	{"AIFOUTR", NULL, "VCLK4"},
	{"Headphone Jack Left", NULL, "Headphone Left PGA"},
	{"Headphone Jack Right", NULL, "Headphone Right PGA"},
	{"Speaker", NULL, "Speaker PGA"},
	{"Earpiece", NULL, "Earpiece PGA"},
};

static int vclk4_supply_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	int ret;
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = clk_enable(vclk4_clk);
		/*sndp_log_info("VCLKCR4[0x%x] ret[%d]\n",
			__raw_readl(VCLKCR4), ret);*/
		break;
	case SND_SOC_DAPM_POST_PMD:
        if ((SNDP_MODE_INCOMM != g_mode) && (SNDP_MODE_INCALL != g_mode))
        {
            clk_disable(vclk4_clk);
            sndp_log_info("VCLKCR4[0x%x]\n",
                    __raw_readl(VCLKCR4));
        }
        break;
	}
	return 0;
}

static void fsi_hifi_d2153_shutdown(struct snd_pcm_substream *substream)
{
	path_value[substream->stream] = 0;
}



static int fsi_hifi_d2153_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 16000;
	int ret = 0;

	/* set the cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai,
		SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	snd_soc_dai_set_sysclk(codec_dai, 0,
			mclk, SND_SOC_CLOCK_IN);

	switch (params_rate(params)) {
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 96000:
		pll_out = D2153_PLL_FREQ_OUT_98304000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88100:
		pll_out = D2153_PLL_FREQ_OUT_90316800;
		break;
	default:
		pr_err("Invalid sampling rate for D2153 with PLL\n");
		return -EINVAL;
	}
	snd_soc_dai_set_pll(codec_dai, 0, D2153_SYSCLK_PLL, 48000, pll_out);

	return 0;
}

static struct snd_soc_ops fsi_hifi_d2153_ops = {
	.shutdown = fsi_hifi_d2153_shutdown,
	.hw_params = fsi_hifi_d2153_hw_params,
};

static int fsi_hifi_d2153_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	fsi_d2153_codec = codec;
	fsi_d2153_rtd = codec->card->rtd;

	ret = snd_soc_add_codec_controls(codec, fsi_d2153_controls,
				   ARRAY_SIZE(fsi_d2153_controls));
	if (ret < 0) {
		pr_err("Failed to snd_soc_add_codec_controls(%d)\n", ret);
		return ret;
	}

	snd_soc_dapm_new_controls(dapm, fsi_d2153_dapm_widgets,
		ARRAY_SIZE(fsi_d2153_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, fsi_d2153_audio_map,
		ARRAY_SIZE(fsi_d2153_audio_map));

	snd_soc_dapm_disable_pin(dapm, "RECCHL");
	snd_soc_dapm_disable_pin(dapm, "RECCHR");
	snd_soc_dapm_disable_pin(dapm, "Headphone Jack Left");
	snd_soc_dapm_disable_pin(dapm, "Headphone Jack Right");
	snd_soc_dapm_disable_pin(dapm, "Speaker");
	snd_soc_dapm_disable_pin(dapm, "Earpiece");

	playback_widget = rtd->codec_dai->playback_widget;
	capture_widget = rtd->codec_dai->capture_widget;

	return 0;
}
#ifdef CONFIG_PM
static int fsi_d2153_suspend_pre(struct snd_soc_card *card)
{
	int i, ret;

	spin_lock(&fsi_d2153_lock);
	if (!card) {
		ret = -EINVAL;
	} else {
		ret = 0;
		for (i = 0; i < card->num_rtd; i++) {
			if (card->rtd[i].dai_link->ignore_suspend)
				goto out;
		}
		if (playback_widget) {
			playback_widget->active = 0;
			sndp_log_info("p->active[%d]",
				playback_widget->active);
		}
		if (capture_widget) {
			capture_widget->active = 0;
			sndp_log_info("c->active[%d]",
				capture_widget->active);
		}
	}
out:
	spin_unlock(&fsi_d2153_lock);
	return ret;
}
#else
#define fsi_d2153_suspend_pre NULL
#endif /* CONFIG_PM */

static struct snd_soc_dai_link fsi_dai_link[] = {
	{
		.name = "fsia d2153",
		.stream_name = "Hifi",
		.cpu_dai_name	= "sh_fsi2.0",
		.codec_name = "d2153-codec.0-0018",
		.platform_name = "sh_fsi2.0",
		.codec_dai_name = "d2153-aif1",
		.init = fsi_hifi_d2153_init,
		.ops = &fsi_hifi_d2153_ops,
	},
	{
		.name = "fsib Wireless Transciever",
		.stream_name = "Wireless Transciever",
		.cpu_dai_name	= "sh_fsi2.1",
		.codec_name = "sh_fsi_wireless_transciever.0",
		.platform_name = "sh_fsi2.1",
		.codec_dai_name = "sh_fsi_wireless_transciever",
	},
};

static struct snd_soc_card fsi_soc_card = {
	.name = "FSI",
	.dai_link = fsi_dai_link,
	.num_links = ARRAY_SIZE(fsi_dai_link),
	.suspend_pre = fsi_d2153_suspend_pre,
};

static int fsi_d2153_driver_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &fsi_soc_card;
	struct fsi_d2153_priv *priv = NULL;
	struct device_node *node = pdev->dev.of_node;
	struct fsi_d2153_platform_data *pdata;
	int ret = -ENOMEM;
	struct property *prop;
	struct device_node *dai_node;
	bool headset_polarity;
	card->dev = &pdev->dev;
	priv = kzalloc(sizeof(struct fsi_d2153_priv), GFP_KERNEL);
	if (!priv) {
		sndp_log_err("cannot allocate fsi_d2153 private data\n");
		return ret;
	}
	priv->spk_en = false;
	priv->ep_en = false;
	priv->hp_en = false;

	if (node) {
		pr_debug("%s: device tree\n", __func__);
		/* CPU node and platform node parsing */
		dai_node = of_parse_phandle(node, "renesas,fsia", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "sh_fsi node is not provided\n");
			goto err_mem_free;
		}
		fsi_dai_link[0].cpu_dai_name  = NULL;
		fsi_dai_link[0].platform_name = NULL;
		fsi_dai_link[0].cpu_of_node = dai_node;
		fsi_dai_link[0].platform_of_node = dai_node;
		of_node_put(dai_node);
		dai_node = of_parse_phandle(node, "renesas,fsib", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "sh_fsi2 node is not provided\n");
			goto err_mem_free;
		}
		fsi_dai_link[1].cpu_dai_name  = NULL;
		fsi_dai_link[1].platform_name = NULL;
		fsi_dai_link[1].cpu_of_node = dai_node;
		fsi_dai_link[1].platform_of_node = dai_node;
		of_node_put(dai_node);

		if (IS_ERR(platform_device_register_simple(
				"sh_fsi_wireless_transciever", 0, NULL, 0))) {
			dev_err(&pdev->dev, "Failed to register fsi wireless transceiver\n");
			goto err_mem_free;
		}

		/* Parse mclk and hp_spk_path_en */
		ret = of_property_read_u32_index(node, "renesas,mclk-freq", 0,
						 &mclk);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to get master clock value\n");
			goto err_mem_free;
		}
		priv->hp_spk_path_en = of_property_read_bool(node,
						"renesas,hp_spk_path_en");
		headset_polarity = of_property_read_bool(node, "jack-polarity");
		pr_info("%s : headset_polarity = %d\n", __func__,
				headset_polarity);
		d2153_set_headset_polarity(headset_polarity);
		prop = of_find_property(node, "renesas,audio-routing", NULL);
		if (prop) {
			ret = snd_soc_of_parse_audio_routing(card,
						"renesas,audio-routing");
			if (ret) {
				dev_err(&pdev->dev, "audio-routing is failed");
				goto err_mem_free;
			}
		}
	} else {
		pr_debug("%s: No device tree\n", __func__);
		pdata = pdev->dev.platform_data;
		if (!pdata) {
			sndp_log_err("No platform data supplied\n");
			goto err_mem_free;
		}
		mclk = D2153_SNDP_MCLK_RATE;
		priv->hp_spk_path_en = pdata->hp_spk_path_en;
	}
	vclk4_clk = clk_get(NULL, "vclk4_clk");
	if (IS_ERR(vclk4_clk)) {
		ret = IS_ERR(vclk4_clk);
		sndp_log_err("cannot get vclk4 clock\n");
		goto err_vclk4_clk;
	}
	main_clk = clk_get(NULL, "main_clk");
	if (IS_ERR(main_clk)) {
		ret = IS_ERR(main_clk);
		sndp_log_err("cannot get main clock\n");
		goto err_main_clk;
	}
	ret = clk_set_parent(vclk4_clk, main_clk);
	if (0 != ret) {
		sndp_log_err("clk_set_parent failed (%d)\n", ret);
		goto err_clk_set_parent;
	}

	ret = clk_set_rate(vclk4_clk, mclk);
	if (ret < 0) {
		sndp_log_err("cannot set vclk4 rate\n");
		goto err_clk_set_rate;
	}

	ret = sndp_init(fsi_soc_dai, &fsi_soc_platform, card);
	if (ret) {
		sndp_log_err("sndp_init failed (%d)\n", ret);
		goto err_sndp_init;
	}

	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_register_card(card);
	if (ret) {
		sndp_log_err("snd_soc_register_card failed (%d)\n", ret);
		goto err_snd_soc_register_card;
	}

	/* Added for FM */
	g_fm_mute_flg = false;
	/*Added for FM */

	return 0;

err_snd_soc_register_card:
err_sndp_init:
err_clk_set_rate:
err_clk_set_parent:
	clk_put(main_clk);
err_main_clk:
	clk_put(vclk4_clk);
err_vclk4_clk:
err_mem_free:
	kfree(priv);
	return ret;
}

static void fsi_d2153_driver_shutdown(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct fsi_d2153_priv *priv =
			snd_soc_card_get_drvdata(card);

	sndp_log_info("start\n");
	sndp_exit();
	fsi_d2153_codec = NULL;
	fsi_d2153_rtd = NULL;
	fsi_d2153_ops_save.startup = NULL;
	fsi_d2153_ops_save.shutdown = NULL;
	fsi_d2153_ops_save.hw_params = NULL;
	fsi_d2153_ops_save.hw_free = NULL;
	kfree(priv);
	clk_put(main_clk);
	clk_put(vclk4_clk);
	sndp_log_info("end\n");
}

static const struct of_device_id fsi_d2153_of_match[] = {
	{.compatible = "renesas,fsi-snd-d2153", },
	{},
};
MODULE_DEVICE_TABLE(of, fsi_d2153_of_match);

static struct platform_driver fsi_d2153_driver = {
	.driver = {
		.name = "fsi-snd-d2153",
		.of_match_table = fsi_d2153_of_match,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
	.probe = fsi_d2153_driver_probe,
	.shutdown = fsi_d2153_driver_shutdown,
};

module_platform_driver(fsi_d2153_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Fsi+d2153 machine ASoC driver");
MODULE_AUTHOR("Renesas Mobile Corp");
