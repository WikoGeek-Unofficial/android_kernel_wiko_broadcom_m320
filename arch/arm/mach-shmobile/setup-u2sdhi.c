#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/renesas_sdhi.h>
#include <linux/regulator/consumer.h>

#include <mach/setup-u2sdhi.h>
#include <mach/common.h>
#include <mach/r8a7373.h>
#include <mach/irqs.h>

#define VSD_VDCORE_DELAY 50
#define E3_3_V 3300000
#define E1_8_V 1800000
#define SDCLK0DCR               IO_ADDRESS(0xE605811E)
#define SD0DCR                  IO_ADDRESS(0xE605818E)

static void sdhi0_set_pwr(struct platform_device *pdev, int state)
{
	struct regulator *regulator;
	int ret = 0;
	int regulator_voltage = 0;

	switch (state) {
	case RENESAS_SDHI_POWER_ON:
		pr_info("RENESAS_SDHI_POWER_ON:%s\n", __func__);
		regulator = regulator_get(NULL, "vsd");
		if (IS_ERR(regulator)) {
			pr_info("%s:err regulator_get ret = %ld\n",
						__func__ , PTR_ERR(regulator));
			return;
		}

		ret = regulator_enable(regulator);
		if (ret)
			pr_info("%s:err regulator_enable ret = %d\n",
						__func__ , ret);

		regulator_put(regulator);

		regulator = regulator_get(NULL, "vio_sd");
		if (IS_ERR(regulator)) {
			pr_info("%s:err regulator_get ret = %ld\n",
						__func__ , PTR_ERR(regulator));
			return;
		}

		ret = regulator_enable(regulator);
		if (ret)
			pr_info("%s:err regulator_enable ret = %d\n",
						__func__ , ret);

		regulator_put(regulator);

		__raw_writel(__raw_readl(MSEL3CR) | (1<<28), MSEL3CR);
		break;

	case RENESAS_SDHI_POWER_OFF:
		pr_info("RENESAS_SDHI_POWER_OFF:%s\n", __func__);
		__raw_writel(__raw_readl(MSEL3CR) & ~(1<<28), MSEL3CR);

		regulator = regulator_get(NULL, "vio_sd");
		if (IS_ERR(regulator)) {
			pr_info("%s:err regulator_get ret = %ld\n",
						__func__ , PTR_ERR(regulator));
			return;
		}

		ret = regulator_disable(regulator);
		if (ret)
			pr_info("%s:err regulator_disable ret = %d\n",
						__func__ , ret);

		regulator_put(regulator);

		regulator = regulator_get(NULL, "vsd");
		if (IS_ERR(regulator)) {
			pr_info("%s:err regulator_get ret = %ld\n",
						__func__ , PTR_ERR(regulator));
			return;
		}

		ret = regulator_disable(regulator);
		if (ret)
			pr_info("%s:err regulator_disable ret = %d\n",
						__func__ , ret);

		regulator_put(regulator);

		/* Delay of 50ms added between VSD off and VCORE
		off as per SSG specification */
		mdelay(VSD_VDCORE_DELAY);
		break;

	case RENESAS_SDHI_SIGNAL_V330:
		pr_info("RENESAS_SDHI_SIGNAL_V330:%s\n", __func__);

		regulator = regulator_get(NULL, "vsd");
		if (IS_ERR(regulator)) {
			pr_info("%s:err regulator_get ret = %ld\n",
						__func__ , PTR_ERR(regulator));
			return;
		}

		regulator_voltage = regulator_get_voltage(regulator);
		pr_info("vsd voltage = %d\n", regulator_voltage);
		if (regulator_voltage != E3_3_V) {
			pr_info("vsd change as %duV\n", E3_3_V);
			ret = regulator_set_voltage(regulator, E3_3_V, E3_3_V);
			if (ret)
				pr_info("%s: err vsd set voltage ret=%d\n",
								__func__, ret);
		}

		regulator_put(regulator);

		regulator = regulator_get(NULL, "vio_sd");
		if (IS_ERR(regulator)) {
			pr_info("%s:err regulator_get ret = %ld\n",
						__func__ , PTR_ERR(regulator));
			return;
		}

		regulator_voltage = regulator_get_voltage(regulator);
		pr_info("vio_sd voltage= %d\n", regulator_voltage);
		if (regulator_voltage != E3_3_V) {
			pr_info("vio_sd change as %duV\n", E3_3_V);
			ret = regulator_set_voltage(regulator, E3_3_V, E3_3_V);
			if (ret)
				pr_info("%s: err vio_sd set voltage ret=%d\n",
								__func__, ret);
		}

		regulator_put(regulator);

		__raw_writew(__raw_readw(SDCLK0DCR) | (1<<5), SDCLK0DCR);
		__raw_writew(__raw_readw(SD0DCR) | (1<<5), SD0DCR);
		break;
	case RENESAS_SDHI_SIGNAL_V180:
		pr_info("RENESAS_SDHI_SIGNAL_V180:%s\n", __func__);

		regulator = regulator_get(NULL, "vio_sd");
		if (IS_ERR(regulator)) {
			pr_info("%s:err regulator_get ret = %ld\n",
						__func__ , PTR_ERR(regulator));
			return;
		}

		regulator_voltage = regulator_get_voltage(regulator);
		pr_info("vio_sd voltage = %d\n", regulator_voltage);
		if (regulator_voltage != E1_8_V) {
			pr_info("vio_sd change as %duV\n", E1_8_V);
			ret = regulator_set_voltage(regulator, E1_8_V, E1_8_V);
			if (ret)
				pr_info("%s: err vio_sd set voltage ret=%d\n",
								__func__, ret);
		}

		regulator_put(regulator);
		__raw_writew(__raw_readw(SDCLK0DCR) & ~(1<<5), SDCLK0DCR);
		__raw_writew(__raw_readw(SD0DCR) & ~(1<<5), SD0DCR);
		break;
	default:
		pr_info("default:%s\n", __func__);
		break;
	}
}


static int sdhi0_get_cd(struct platform_device *pdev)
{
	struct renesas_sdhi_platdata *pdata = pdev->dev.platform_data;
	return gpio_get_value(pdata->card_detect_gpio) ? 0 : 1;
}

#define SDHI0_EXT_ACC_PHYS	0xEE1000E4
#define SDHI0_DMACR_PHYS	0xEE108000

/* SDHI EXT_ACC */
#define SDHI_DBWSEL_16_BIT      (0 << 0)
#define SDHI_DBWSEL_32_BIT      (1 << 0)

/* SDHI SD_DMACR */
#define SDHI_DMACR_SD1_1        (1 << 5)
#define SDHI_DMACR_SD0_1        (1 << 4)
#define SDHI_DMACR_SD1_0        (1 << 1)
#define SDHI_DMACR_SD0_0        (1 << 0)

/* 32 byte transfer SDx_1 = 1, SDx_0 = 0 */
#define SDHI_DMA_32_BYTE        (SDHI_DMACR_SD1_1 | SDHI_DMACR_SD0_1)
/* 16 byte transfer SDx_1 = 0, SDx_0 = 1 */
#define SDHI_DMA_16_BYTE        (SDHI_DMACR_SD1_0 | SDHI_DMACR_SD0_0)
/* 16 bit transfer SDx_1 = 0, SDx_0 = 0 */
#define SDHI_DMA_16_BIT         (0)

static void sdhi0_set_dma(struct platform_device *pdev, int size)
{
	static void __iomem *dmacr, *ext_acc;
	u32 val, val2;

	if (!dmacr)
		dmacr = ioremap_nocache(SDHI0_DMACR_PHYS, 4);
	if (!ext_acc)
		ext_acc = ioremap_nocache(SDHI0_EXT_ACC_PHYS, 4);

	switch (size) {
	case 32:
		val = SDHI_DMA_32_BYTE;
		val2 = SDHI_DBWSEL_32_BIT;
		break;
	case 16:
		val = SDHI_DMA_16_BYTE;
		val2 = SDHI_DBWSEL_32_BIT;
		break;
	default:
		val = SDHI_DMA_16_BIT;
		val2 = SDHI_DBWSEL_16_BIT;
		break;
	}
	__raw_writew(val, dmacr);
	__raw_writew(val2, ext_acc);
}

struct renesas_sdhi_platdata sdhi0_info = {
	.caps			= /*MMC_CAP_SET_XPC_180 |*/ MMC_CAP_UHS_SDR50,
	.flags			= RENESAS_SDHI_SDCLK_OFFEN |
				  RENESAS_SDHI_WP_DISABLE |
					RENESAS_SDHI_SDCLK_DIV1,
	.slave_id_tx		= SHDMA_SLAVE_SDHI0_TX,
	.slave_id_rx		= SHDMA_SLAVE_SDHI0_RX,
	.set_pwr		= sdhi0_set_pwr,
	.detect_irq		= irq_pin(50),
	.detect_msec		= 0,
	.get_cd			= sdhi0_get_cd,
	.card_detect_gpio	= GPIO_PORT327,
	.card_stat_sysfs	= true,
	.set_dma		= sdhi0_set_dma,
};

static int sdhi1_get_pwr(struct platform_device *pdev)
{
	struct renesas_sdhi_platdata *pdata = pdev->dev.platform_data;
	return gpio_get_value(pdata->pwr_gpio);
}

static void sdhi1_set_pwr(struct platform_device *pdev, int state)
{
	static int power_state;
	struct renesas_sdhi_platdata *pdata = pdev->dev.platform_data;

	pr_alert("%s: %s\n", __func__, (state ? "on" : "off"));

	if (state != power_state) {
		power_state = state;
		gpio_set_value(pdata->pwr_gpio, state);
	}
}

static int sdhi1_get_cd(struct platform_device *pdev)
{
	/*
	In SSG , SDHI1 channel is using wlan .
	For wlan case its non removable card
	thats we dont have external interrupt.
	For that reason we returning  1. */
	return 1;/*return gpio_get_value(GPIO_PORT327) ? 0 : 1;*/
}

struct renesas_sdhi_platdata sdhi1_info = {
	.caps		= MMC_CAP_SDIO_IRQ | MMC_CAP_NONREMOVABLE
					| MMC_CAP_4_BIT_DATA
					| MMC_CAP_POWER_OFF_CARD,
	.pm_caps	= MMC_PM_KEEP_POWER | MMC_PM_IGNORE_PM_NOTIFY,
	.flags		= RENESAS_SDHI_SDCLK_OFFEN,
	.slave_id_tx	= SHDMA_SLAVE_SDHI1_TX,
	.slave_id_rx	= SHDMA_SLAVE_SDHI1_RX,
	.set_pwr	= sdhi1_set_pwr,
	.get_pwr	= sdhi1_get_pwr,
	.pwr_gpio	= GPIO_PORT260,
	.suspend_pwr_ctrl	= true,
	.detect_irq	= 0,
	.detect_msec	= 0,
	.get_cd		= sdhi1_get_cd,
	.ocr		= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.timeout = 1000,
	.dump_timeout = true,
};
