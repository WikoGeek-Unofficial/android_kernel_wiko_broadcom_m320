/*
 * MMCIF eMMC driver.
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 * Yusuke Goda <yusuke.goda.sx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *
 * TODO
 *  1. DMA
 *  2. Power management
 *  3. Handle MMC errors better
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/sh_dma.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/renesas_mmcif.h>
#include <linux/of.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#define DRIVER_NAME	"renesas_mmcif"
#define DRIVER_VERSION	"2010-04-28"

/* #define SHMMCIF_LOG */
#ifdef SHMMCIF_LOG
#define shmmcif_log(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
#define shmmcif_log(fmt, ...)
#endif

/* CE_CMD_SET */
#define CMD_MASK		0x3f000000
#define CMD_SET_RTYP_NO		((0 << 23) | (0 << 22))
#define CMD_SET_RTYP_6B		((0 << 23) | (1 << 22)) /* R1/R1b/R3/R4/R5 */
#define CMD_SET_RTYP_17B	((1 << 23) | (0 << 22)) /* R2 */
#define CMD_SET_RBSY		(1 << 21) /* R1b */
#define CMD_SET_CCSEN		(1 << 20)
#define CMD_SET_WDAT		(1 << 19) /* 1: on data, 0: no data */
#define CMD_SET_DWEN		(1 << 18) /* 1: write, 0: read */
#define CMD_SET_CMLTE		(1 << 17) /* 1: multi block trans, 0: single */
#define CMD_SET_CMD12EN		(1 << 16) /* 1: CMD12 auto issue */
#define CMD_SET_RIDXC_INDEX	((0 << 15) | (0 << 14)) /* index check */
#define CMD_SET_RIDXC_BITS	((0 << 15) | (1 << 14)) /* check bits check */
#define CMD_SET_RIDXC_NO	((1 << 15) | (0 << 14)) /* no check */
#define CMD_SET_CRC7C		((0 << 13) | (0 << 12)) /* CRC7 check*/
#define CMD_SET_CRC7C_BITS	((0 << 13) | (1 << 12)) /* check bits check*/
#define CMD_SET_CRC7C_INTERNAL	((1 << 13) | (0 << 12)) /* internal CRC7 check*/
#define CMD_SET_CRC16C		(1 << 10) /* 0: CRC16 check*/
#define CMD_SET_CRCSTE		(1 << 8) /* 1: not receive CRC status */
#define CMD_SET_TBIT		(1 << 7) /* 1: tran mission bit "Low" */
#define CMD_SET_OPDM		(1 << 6) /* 1: open/drain */
#define CMD_SET_CCSH		(1 << 5)
#define CMD_SET_DARS		(1 << 2) /* Dual Data Rate */
#define CMD_SET_DATW_1		((0 << 1) | (0 << 0)) /* 1bit */
#define CMD_SET_DATW_4		((0 << 1) | (1 << 0)) /* 4bit */
#define CMD_SET_DATW_8		((1 << 1) | (0 << 0)) /* 8bit */

/* CE_CMD_CTRL */
#define CMD_CTRL_BREAK		(1 << 0)

/* CE_BLOCK_SET */
#define BLOCK_SIZE_MASK		0x0000ffff

/* CE_INT */
#define INT_CCSDE		(1 << 29)
#define INT_CMD12DRE		(1 << 26)
#define INT_CMD12RBE		(1 << 25)
#define INT_CMD12CRE		(1 << 24)
#define INT_DTRANE		(1 << 23)
#define INT_BUFRE		(1 << 22)
#define INT_BUFWEN		(1 << 21)
#define INT_BUFREN		(1 << 20)
#define INT_CCSRCV		(1 << 19)
#define INT_RBSYE		(1 << 17)
#define INT_CRSPE		(1 << 16)
#define INT_CMDVIO		(1 << 15)
#define INT_BUFVIO		(1 << 14)
#define INT_WDATERR		(1 << 11)
#define INT_RDATERR		(1 << 10)
#define INT_RIDXERR		(1 << 9)
#define INT_RSPERR		(1 << 8)
#define INT_CCSTO		(1 << 5)
#define INT_CRCSTO		(1 << 4)
#define INT_WDATTO		(1 << 3)
#define INT_RDATTO		(1 << 2)
#define INT_RBSYTO		(1 << 1)
#define INT_RSPTO		(1 << 0)
#define INT_ERR_STS		(INT_CMDVIO | INT_BUFVIO | INT_WDATERR |  \
				 INT_RDATERR | INT_RIDXERR | INT_RSPERR | \
				 INT_CCSTO | INT_CRCSTO | INT_WDATTO |	  \
				 INT_RDATTO | INT_RBSYTO | INT_RSPTO)

/* CE_INT_MASK */
#define MASK_ALL		0x00000000
#define MASK_MCCSDE		(1 << 29)
#define MASK_MCMD12DRE		(1 << 26)
#define MASK_MCMD12RBE		(1 << 25)
#define MASK_MCMD12CRE		(1 << 24)
#define MASK_MDTRANE		(1 << 23)
#define MASK_MBUFRE		(1 << 22)
#define MASK_MBUFWEN		(1 << 21)
#define MASK_MBUFREN		(1 << 20)
#define MASK_MCCSRCV		(1 << 19)
#define MASK_MRBSYE		(1 << 17)
#define MASK_MCRSPE		(1 << 16)
#define MASK_MCMDVIO		(1 << 15)
#define MASK_MBUFVIO		(1 << 14)
#define MASK_MWDATERR		(1 << 11)
#define MASK_MRDATERR		(1 << 10)
#define MASK_MRIDXERR		(1 << 9)
#define MASK_MRSPERR		(1 << 8)
#define MASK_MCCSTO		(1 << 5)
#define MASK_MCRCSTO		(1 << 4)
#define MASK_MWDATTO		(1 << 3)
#define MASK_MRDATTO		(1 << 2)
#define MASK_MRBSYTO		(1 << 1)
#define MASK_MRSPTO		(1 << 0)

#define MASK_START_CMD		(MASK_MCMDVIO | MASK_MBUFVIO | MASK_MWDATERR | \
				 MASK_MRDATERR | MASK_MRIDXERR | MASK_MRSPERR | \
				 MASK_MCCSTO | MASK_MCRCSTO | MASK_MWDATTO | \
				 MASK_MRDATTO | MASK_MRBSYTO | MASK_MRSPTO)

/* CE_HOST_STS1 */
#define STS1_CMDSEQ		(1 << 31)

/* CE_HOST_STS2 */
#define STS2_CRCSTE		(1 << 31)
#define STS2_CRC16E		(1 << 30)
#define STS2_AC12CRCE		(1 << 29)
#define STS2_RSPCRC7E		(1 << 28)
#define STS2_CRCSTEBE		(1 << 27)
#define STS2_RDATEBE		(1 << 26)
#define STS2_AC12REBE		(1 << 25)
#define STS2_RSPEBE		(1 << 24)
#define STS2_AC12IDXE		(1 << 23)
#define STS2_RSPIDXE		(1 << 22)
#define STS2_CRC16E2    (1 << 19)
#define STS2_CCSTO		(1 << 15)
#define STS2_RDATTO		(1 << 14)
#define STS2_DATBSYTO		(1 << 13)
#define STS2_CRCSTTO		(1 << 12)
#define STS2_AC12BSYTO		(1 << 11)
#define STS2_RSPBSYTO		(1 << 10)
#define STS2_AC12RSPTO		(1 << 9)
#define STS2_RSPTO		(1 << 8)
#define STS2_CRC_ERR		(STS2_CRCSTE | STS2_CRC16E |		\
				 STS2_AC12CRCE | STS2_RSPCRC7E|		\
				STS2_CRCSTEBE | STS2_CRC16E2)
#define STS2_TIMEOUT_ERR	(STS2_CCSTO | STS2_RDATTO |		\
				 STS2_DATBSYTO | STS2_CRCSTTO |		\
				 STS2_AC12BSYTO | STS2_RSPBSYTO |	\
				 STS2_AC12RSPTO | STS2_RSPTO)

#define CLKDEV_EMMC_DATA	52000000 /* 52MHz */
#define CLKDEV_MMC_DATA		20000000 /* 20MHz */
#define CLKDEV_INIT		400000   /* 400 KHz */

#define EMMC_CLK_CMD_DELAY	362
#define HOST_MAX_WAIT_TIME (50 * HZ)

unsigned int wakeup_from_suspend_emmc;

enum mmcif_state {
	STATE_IDLE,
	STATE_REQUEST,
	STATE_IOS,
};

struct sh_mmcif_host {
	struct mmc_host *mmc;
	struct mmc_data *data;
	struct platform_device *pd;
	struct sh_mmcif_plat_data *pdata;
	struct sh_dmae_slave dma_slave_tx;
	struct sh_dmae_slave dma_slave_rx;
	struct clk *hclk;
	unsigned int clk;
	int bus_width;
	int timing;
	bool sd_error;
	long timeout;
	void __iomem *addr;
	struct completion intr_wait;
	spinlock_t lock;		/* protect sh_mmcif_host::state */
	enum mmcif_state state;
	bool power;
	bool card_present;
	struct sg_mapping_iter sg_miter;        /* SG state for PIO */

	/* DMA support */
	struct dma_chan		*chan_rx;
	struct dma_chan		*chan_tx;
	struct completion	dma_complete;
	bool			dma_active;

	u64 dma_mask; /* custom DMA mask */

	u32 buf_acc;
};

static inline void sh_mmcif_bitset(struct sh_mmcif_host *host,
					unsigned int reg, u32 val)
{
	__raw_writel(val | __raw_readl(host->addr + reg), host->addr + reg);
}

static inline void sh_mmcif_bitclr(struct sh_mmcif_host *host,
					unsigned int reg, u32 val)
{
	__raw_writel(~val & __raw_readl(host->addr + reg), host->addr + reg);
}

static void mmcif_dma_complete(void *arg)
{
	struct sh_mmcif_host *host = arg;
	dev_dbg(&host->pd->dev, "Command completed\n");

	if (WARN(!host->data, "%s: NULL data in DMA completion!\n",
		 dev_name(&host->pd->dev)))
		return;

	if (host->data->flags & MMC_DATA_READ)
		dma_unmap_sg(host->chan_rx->device->dev,
			     host->data->sg, host->data->sg_len,
			     DMA_FROM_DEVICE);
	else
		dma_unmap_sg(host->chan_tx->device->dev,
			     host->data->sg, host->data->sg_len,
			     DMA_TO_DEVICE);

	complete(&host->dma_complete);
}

static void sh_mmcif_start_dma_rx(struct sh_mmcif_host *host)
{
	struct scatterlist *sg = host->data->sg;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_rx;
	dma_cookie_t cookie = -EINVAL;
	int ret;

	ret = dma_map_sg(chan->device->dev, sg, host->data->sg_len,
			 DMA_FROM_DEVICE);
	if (ret > 0) {
		host->dma_active = true;
		desc = dmaengine_prep_slave_sg(chan, sg, ret,
			DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = mmcif_dma_complete;
		desc->callback_param = host;
		cookie = dmaengine_submit(desc);
		if (cookie < 0) {
			dev_err(&host->pd->dev,
			"%s(): dmaengine_submit error\n", __func__);
		} else {
			sh_mmcif_bitset(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN);
			dma_async_issue_pending(chan);
		}
	}
	dev_dbg(&host->pd->dev, "%s(): mapped %d -> %d, cookie %d\n",
		__func__, host->data->sg_len, ret, cookie);

	if (!desc || cookie < 0) {
		/* DMA failed, fall back to PIO */
		if (ret >= 0)
			ret = -EIO;

		host->dma_active = false;
		dev_warn(&host->pd->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
		sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN | BUF_ACC_DMAWEN);
	}

	dev_dbg(&host->pd->dev, "%s(): desc %p, cookie %d, sg[%d]\n", __func__,
		desc, cookie, host->data->sg_len);
}

static void sh_mmcif_dma_terminate(struct sh_mmcif_host *host)
{
	if (!host->data)
		return;

	if (host->data->flags & MMC_DATA_READ)
		dmaengine_terminate_all(host->chan_rx);
	else
		dmaengine_terminate_all(host->chan_tx);

	host->dma_active = false;
}

static void sh_mmcif_start_dma_tx(struct sh_mmcif_host *host)
{
	struct scatterlist *sg = host->data->sg;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_tx;
	dma_cookie_t cookie = -EINVAL;
	int ret;

	ret = dma_map_sg(chan->device->dev, sg, host->data->sg_len,
			 DMA_TO_DEVICE);
	if (ret > 0) {
		host->dma_active = true;
		desc = dmaengine_prep_slave_sg(chan, sg, ret,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = mmcif_dma_complete;
		desc->callback_param = host;
		cookie = dmaengine_submit(desc);
		if (cookie < 0) {
			dev_err(&host->pd->dev,
			"%s(): dmaengine_submit error\n", __func__);
		} else {
			sh_mmcif_bitset(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAWEN);
			dma_async_issue_pending(chan);
		}
	}
	dev_dbg(&host->pd->dev, "%s(): mapped %d -> %d, cookie %d\n",
		__func__, host->data->sg_len, ret, cookie);

	if (!desc || cookie < 0) {
		/* DMA failed, fall back to PIO */
		if (ret >= 0)
			ret = -EIO;
		host->dma_active = false;

		dev_warn(&host->pd->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
		sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN | BUF_ACC_DMAWEN);
	}

	dev_dbg(&host->pd->dev, "%s(): desc %p, cookie %d\n", __func__,
		desc, cookie);
}

static bool sh_mmcif_filter(struct dma_chan *chan, void *arg)
{
	dev_dbg(chan->device->dev, "%s: slave data %p\n", __func__, arg);
	chan->private = arg;
	return true;
}

static void sh_mmcif_request_dma(struct sh_mmcif_host *host,
				 struct sh_mmcif_plat_data *pdata)
{
	struct sh_dmae_slave *tx, *rx;
	host->dma_active = false;

	/* We can only either use DMA for both Tx and Rx or not use it at all */
	tx = &host->dma_slave_tx;
	tx->slave_id = pdata->slave_id_tx;
	rx = &host->dma_slave_rx;
	rx->slave_id = pdata->slave_id_rx;

	if (tx->slave_id > 0 && rx->slave_id > 0) {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		host->chan_tx = dma_request_channel(mask, sh_mmcif_filter, tx);
		dev_dbg(&host->pd->dev, "%s: TX: got channel %p\n", __func__,
			host->chan_tx);

		if (!host->chan_tx)
			return;

		host->chan_rx = dma_request_channel(mask, sh_mmcif_filter, rx);
		dev_dbg(&host->pd->dev, "%s: RX: got channel %p\n", __func__,
			host->chan_rx);

		if (!host->chan_rx) {
			dma_release_channel(host->chan_tx);
			host->chan_tx = NULL;
			return;
		}

		init_completion(&host->dma_complete);
	}
}

static void sh_mmcif_release_dma(struct sh_mmcif_host *host)
{
	sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN | BUF_ACC_DMAWEN);
	/* Descriptors are freed automatically */
	if (host->chan_tx) {
		struct dma_chan *chan = host->chan_tx;
		host->chan_tx = NULL;
		dma_release_channel(chan);
	}
	if (host->chan_rx) {
		struct dma_chan *chan = host->chan_rx;
		host->chan_rx = NULL;
		dma_release_channel(chan);
	}

	host->dma_active = false;
}

static void sh_mmcif_clock_control(struct sh_mmcif_host *host, unsigned int clk)
{
	struct sh_mmcif_plat_data *p = host->pdata;

	sh_mmcif_bitclr(host, MMCIF_CE_CLK_CTRL, CLK_ENABLE);
	sh_mmcif_bitclr(host, MMCIF_CE_CLK_CTRL, CLK_CLEAR);

	if (!clk)
		return;
	if (p->sup_pclk && clk == host->clk)
		sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_SUP_PCLK);
	else
		sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_CLEAR &
				((fls(DIV_ROUND_UP(host->clk,
						   clk) - 1) - 1) << 16));

	sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_ENABLE);
}

static void sh_mmcif_sync_reset(struct sh_mmcif_host *host)
{
	u32 tmp;
	struct sh_mmcif_plat_data *p = host->pdata;

	tmp = 0x010f0000 & sh_mmcif_readl(host->addr, MMCIF_CE_CLK_CTRL);

	sh_mmcif_writel(host->addr, MMCIF_CE_VERSION, SOFT_RST_ON);
	sh_mmcif_writel(host->addr, MMCIF_CE_VERSION, SOFT_RST_OFF);
	sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, tmp |
		SRSPTO_256 | SRBSYTO_29 | SRWDTO_29 | SCCSTO_29);
	sh_mmcif_bitset(host, MMCIF_CE_BUF_ACC, host->buf_acc);

	/* Not checking p for NULL, it's consistently not checked else either */

	if (p->data_delay_ps > 2000)
		pr_alert("WARNING: %s data delay max 2ns, %d ps requested!\n",
			  __func__, p->data_delay_ps);
	tmp = (p->data_delay_ps+250)/500;	/* From picosec to 0.5 ns */
	if (tmp > 4) tmp = 4;		/* Max delay 4 = 2.0 ns   */
	sh_mmcif_writel(host->addr, MMCIF_CE_DELAY_SEL, tmp);
}

static int sh_mmcif_error_manage(struct sh_mmcif_host *host)
{
	u32 state1, state2;
	int ret, timeout;

	host->sd_error = false;

	state1 = sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS1);
	state2 = sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS2);
	dev_err(&host->pd->dev, "ERR HOST_STS1 = %08x\n", state1);
	dev_err(&host->pd->dev, "ERR HOST_STS2 = %08x\n", state2);

	if (state1 & STS1_CMDSEQ) {
		sh_mmcif_bitset(host, MMCIF_CE_CMD_CTRL, CMD_CTRL_BREAK);
		sh_mmcif_bitclr(host, MMCIF_CE_CMD_CTRL, CMD_CTRL_BREAK);
		for (timeout = 10000000; timeout; timeout--) {
			if (!(sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS1)
			      & STS1_CMDSEQ))
				break;
			mdelay(1);
		}
		if (!timeout) {
			dev_err(&host->pd->dev,
				"Forced end of command sequence timeout err\n");
			return -EIO;
		}
		sh_mmcif_sync_reset(host);
		dev_err(&host->pd->dev, "Forced end of command sequence\n");
		return -EIO;
	}

	if (state2 & STS2_CRC_ERR) {
		dev_err(&host->pd->dev, ": CRC error\n");
		ret = -EIO;
	} else if (state2 & STS2_TIMEOUT_ERR) {
		dev_err(&host->pd->dev, ": Timeout\n");
		ret = -ETIMEDOUT;
	} else {
		dev_err(&host->pd->dev, ": End/Index error\n");
		ret = -EIO;
	}
	return ret;
}

static int sh_mmcif_single_read(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, *p, len;

	sg_miter_start(&host->sg_miter, data->sg, data->sg_len,
				SG_MITER_ATOMIC | SG_MITER_TO_SG);
	/* buf read enable */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	blocksize = (BLOCK_SIZE_MASK &
			sh_mmcif_readl(host->addr, MMCIF_CE_BLOCK_SET)) + 3;

	preempt_disable();
	if (!sg_miter_next(&host->sg_miter))
		BUG();

	p = host->sg_miter.addr;
	len = host->sg_miter.consumed =
		min(host->sg_miter.length, blocksize);

	BUG_ON(len % 4);
	for (i = 0; i < blocksize / 4; i++)
		*p++ = sh_mmcif_readl(host->addr, MMCIF_CE_DATA);

	sg_miter_stop(&host->sg_miter);
	preempt_enable();
	/* buffer read end */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFRE);
	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	return 0;
}

static int sh_mmcif_multi_read(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, j, *p, len;

	sg_miter_start(&host->sg_miter, data->sg, data->sg_len,
				SG_MITER_ATOMIC | SG_MITER_TO_SG);
	blocksize = BLOCK_SIZE_MASK & sh_mmcif_readl(host->addr,
						     MMCIF_CE_BLOCK_SET);
	for (j = 0; j < host->data->blocks; ++j) {
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
		/* buf read enable */
		time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);

		if (time <= 0 || host->sd_error)
			return sh_mmcif_error_manage(host);

		preempt_disable();
		if (!sg_miter_next(&host->sg_miter)) {
			pr_info("Index is %d\n", j);
			BUG();
		}

		p = host->sg_miter.addr;
		len = host->sg_miter.consumed =
			min(host->sg_miter.length, blocksize);

		BUG_ON(len % 4);
		for (i = 0; i < len / 4; i++)
			*p++ = sh_mmcif_readl(host->addr,
					      MMCIF_CE_DATA);
		sg_miter_stop(&host->sg_miter);
		preempt_enable();
	}

	return 0;
}

static int sh_mmcif_single_write(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, *p, len;

	sg_miter_start(&host->sg_miter, data->sg, data->sg_len,
				SG_MITER_ATOMIC | SG_MITER_FROM_SG);
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);

	/* buf write enable */
	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	blocksize = (BLOCK_SIZE_MASK &
			sh_mmcif_readl(host->addr, MMCIF_CE_BLOCK_SET)) + 3;

	preempt_disable();
	if (!sg_miter_next(&host->sg_miter))
		BUG();

	p = host->sg_miter.addr;
	len = host->sg_miter.consumed =
			min(host->sg_miter.length, blocksize);

	BUG_ON(len % 4);
	for (i = 0; i < blocksize / 4; i++)
		sh_mmcif_writel(host->addr, MMCIF_CE_DATA, *p++);

	sg_miter_stop(&host->sg_miter);
	preempt_enable();
	/* buffer write end */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MDTRANE);

	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	return 0;
}

static int sh_mmcif_multi_write(struct sh_mmcif_host *host,
						struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 i, j, blocksize, *p, len;

	sg_miter_start(&host->sg_miter, data->sg, data->sg_len,
				SG_MITER_ATOMIC | SG_MITER_FROM_SG);
	blocksize = BLOCK_SIZE_MASK & sh_mmcif_readl(host->addr,
						     MMCIF_CE_BLOCK_SET);
	for (j = 0; j < host->data->blocks; ++j) {
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);
		/* buf read enable */
		time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);

		if (time <= 0 || host->sd_error)
			return sh_mmcif_error_manage(host);

		preempt_disable();
		if (!sg_miter_next(&host->sg_miter)) {
			pr_info("Index is %d\n", j);
			BUG();
		}

		p = host->sg_miter.addr;
		len = host->sg_miter.consumed =
			min(host->sg_miter.length, blocksize);

		BUG_ON(len % 4);
		for (i = 0; i < blocksize / 4; i++)
			sh_mmcif_writel(host->addr,
					MMCIF_CE_DATA, *p++);
		sg_miter_stop(&host->sg_miter);
		preempt_enable();
	}

	return 0;
}

static void sh_mmcif_get_response(struct sh_mmcif_host *host,
						struct mmc_command *cmd)
{
	if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP3);
		cmd->resp[1] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP2);
		cmd->resp[2] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP1);
		cmd->resp[3] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP0);
	} else
		cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP0);
}

static void sh_mmcif_get_cmd12response(struct sh_mmcif_host *host,
						struct mmc_command *cmd)
{
	cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP_CMD12);
}

static u32 sh_mmcif_set_cmd(struct sh_mmcif_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd, u32 opc)
{
	u32 tmp = 0;

	/* Response Type check */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		tmp |= CMD_SET_RTYP_NO;
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
	case MMC_RSP_R3:
		tmp |= CMD_SET_RTYP_6B;
		break;
	case MMC_RSP_R2:
		tmp |= CMD_SET_RTYP_17B;
		break;
	default:
		dev_err(&host->pd->dev, "Unsupported response type.\n");
		break;
	}
	switch (opc) {
	/* RBSY */
	case MMC_SLEEP_AWAKE:
	case MMC_SWITCH:
	case MMC_STOP_TRANSMISSION:
	case MMC_SET_WRITE_PROT:
	case MMC_CLR_WRITE_PROT:
	case MMC_ERASE:
		tmp |= CMD_SET_RBSY;
		break;
	}
	/* WDAT / DATW */
	if (host->data) {
		tmp |= CMD_SET_WDAT;
		switch (host->bus_width) {
		case MMC_BUS_WIDTH_1:
			tmp |= CMD_SET_DATW_1;
			break;
		case MMC_BUS_WIDTH_4:
			tmp |= CMD_SET_DATW_4;
			break;
		case MMC_BUS_WIDTH_8:
			tmp |= CMD_SET_DATW_8;
			break;
		default:
			dev_err(&host->pd->dev, "Unsupported bus width.\n");
			break;
		}

		switch (host->timing) {
		case MMC_TIMING_UHS_DDR50:
			tmp |= CMD_SET_DARS;
			break;
		}
	}
	/* DWEN */
	if (opc == MMC_WRITE_BLOCK || opc == MMC_WRITE_MULTIPLE_BLOCK)
		tmp |= CMD_SET_DWEN;
	/* CMLTE/CMD12EN */
	if (opc == MMC_READ_MULTIPLE_BLOCK || opc == MMC_WRITE_MULTIPLE_BLOCK) {
		tmp |= CMD_SET_CMLTE | CMD_SET_CMD12EN;
		sh_mmcif_bitset(host, MMCIF_CE_BLOCK_SET,
					mrq->data->blocks << 16);
	}
	/* RIDXC[1:0] check bits */
	if (opc == MMC_SEND_OP_COND || opc == MMC_ALL_SEND_CID ||
	    opc == MMC_SEND_CSD || opc == MMC_SEND_CID)
		tmp |= CMD_SET_RIDXC_BITS;
	/* RCRC7C[1:0] check bits */
	if (opc == MMC_SEND_OP_COND)
		tmp |= CMD_SET_CRC7C_BITS;
	/* RCRC7C[1:0] internal CRC7 */
	if (opc == MMC_ALL_SEND_CID ||
		opc == MMC_SEND_CSD || opc == MMC_SEND_CID)
		tmp |= CMD_SET_CRC7C_INTERNAL;

	return opc = ((opc << 24) | tmp);
}

static int sh_mmcif_data_trans(struct sh_mmcif_host *host,
				struct mmc_request *mrq, u32 opc)
{
	switch (opc) {
	case MMC_READ_MULTIPLE_BLOCK:
		return sh_mmcif_multi_read(host, mrq);
	case MMC_WRITE_MULTIPLE_BLOCK:
		return sh_mmcif_multi_write(host, mrq);
	case MMC_WRITE_BLOCK:
		return sh_mmcif_single_write(host, mrq);
	case MMC_READ_SINGLE_BLOCK:
	case MMC_SEND_EXT_CSD:
		return sh_mmcif_single_read(host, mrq);
	default:
		dev_err(&host->pd->dev, "UNSUPPORTED CMD = d'%08d\n", opc);
		return -EINVAL;
	}
}

static void sh_mmcif_start_cmd(struct sh_mmcif_host *host,
			       struct mmc_request *mrq)
{
	struct mmc_command *cmd = mrq->cmd;
	long time;
	int ret = 0;
	u32 mask, opc = cmd->opcode;
	ulong timeout = host->timeout;

	switch (opc) {
	/* response busy check */
	case MMC_SWITCH:
		if (cmd->cmd_timeout_ms == 0) {
			/* extend the timeout time for sanitize function*/
			mask = (MASK_START_CMD | MASK_MRBSYE) & ~MASK_MRBSYTO;
			timeout = HOST_MAX_WAIT_TIME;
			break;
		}
	case MMC_SLEEP_AWAKE:
	case MMC_STOP_TRANSMISSION:
	case MMC_SET_WRITE_PROT:
	case MMC_CLR_WRITE_PROT:
	case MMC_ERASE:
		mask = MASK_START_CMD | MASK_MRBSYE;
		break;
	default:
		mask = MASK_START_CMD | MASK_MCRSPE;
		break;
	}

	if (host->data) {
		sh_mmcif_writel(host->addr, MMCIF_CE_BLOCK_SET, 0);
		sh_mmcif_writel(host->addr, MMCIF_CE_BLOCK_SET,
				mrq->data->blksz);
	}
	opc = sh_mmcif_set_cmd(host, mrq, cmd, opc);

	sh_mmcif_writel(host->addr, MMCIF_CE_INT, 0xD80430C0);
	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, mask);
	/* set arg */
	sh_mmcif_writel(host->addr, MMCIF_CE_ARG, cmd->arg);
	/* set cmd */
	sh_mmcif_writel(host->addr, MMCIF_CE_CMD_SET, opc);

	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
		timeout);
	if (time <= 0) {
		dev_err(&host->pd->dev, "Timeout Cmd(0x%x),Arg(0x%x) err\n",
					cmd->opcode, cmd->arg);
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}
	if (host->sd_error) {
		switch (cmd->opcode) {
		case MMC_ALL_SEND_CID:
		case MMC_SELECT_CARD:
		case MMC_APP_CMD:
			cmd->error = -ETIMEDOUT;
			break;
		default:
			dev_err(&host->pd->dev, "Cmd(d'%d) err\n",
					cmd->opcode);
			cmd->error = sh_mmcif_error_manage(host);
			break;
		}
		if (host->dma_active)
			sh_mmcif_dma_terminate(host);
		host->sd_error = false;
		return;
	}
	if (!(cmd->flags & MMC_RSP_PRESENT)) {
		cmd->error = 0;
		return;
	}
	sh_mmcif_get_response(host, cmd);
	if (host->data) {
		if (!host->dma_active) {
			ret = sh_mmcif_data_trans(host, mrq, cmd->opcode);
		} else {
			long time =
				wait_for_completion_interruptible_timeout(&host->dma_complete,
									  host->timeout);
			if (!time || host->sd_error)
				ret = -ETIMEDOUT;
			else if (time < 0)
				ret = time;

			sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC,
					BUF_ACC_DMAREN | BUF_ACC_DMAWEN);

			if (host->sd_error || time <= 0) {
				dev_err(&host->pd->dev, "Cmd%d err\n",
						cmd->opcode);
				sh_mmcif_dma_terminate(host);
				sh_mmcif_error_manage(host);
			}
			host->dma_active = false;
		}
		if (ret < 0)
			mrq->data->bytes_xfered = 0;
		else
			mrq->data->bytes_xfered =
				mrq->data->blocks * mrq->data->blksz;
	}
	cmd->error = ret;
}

static void sh_mmcif_wait_transfer_end(struct sh_mmcif_host *host,
				       struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;

	if (data->flags == MMC_DATA_READ)
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFRE);
	else
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MDTRANE);

	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
							 host->timeout);
	if (time <= 0 || host->sd_error) {
		data->error = sh_mmcif_error_manage(host);
		data->bytes_xfered = 0;
	}
}

static void sh_mmcif_stop_cmd(struct sh_mmcif_host *host,
			      struct mmc_request *mrq)
{
	struct mmc_command *cmd = mrq->stop;
	long time;

	if (mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK)
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MCMD12DRE);
	else if (mrq->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MCMD12RBE);
	else {
		dev_err(&host->pd->dev, "unsupported stop cmd\n");
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}

	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error) {
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}
	sh_mmcif_get_cmd12response(host, cmd);
	cmd->error = 0;
}

static void sh_mmcif_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);
	struct sh_mmcif_plat_data *pdata;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (host->state != STATE_IDLE) {
		spin_unlock_irqrestore(&host->lock, flags);
		mrq->cmd->error = -EAGAIN;
		mmc_request_done(mmc, mrq);
		return;
	}

	host->state = STATE_REQUEST;
	spin_unlock_irqrestore(&host->lock, flags);

	switch (mrq->cmd->opcode) {
	/* MMCIF does not support SD/SDIO command */
	case SD_IO_SEND_OP_COND:
		/*
		 * MMC_SLEEP_AWAKE happens to be given an idential number
		 * with SD_IO_SEND_OP_COND, but we need to send it out here
		 * to put MMC to sleep.
		 */
		if ((mrq->cmd->flags & MMC_CMD_MASK) == MMC_CMD_AC)
			break;
	case MMC_APP_CMD:
	case SD_IO_RW_DIRECT:
		host->state = STATE_IDLE;
		mrq->cmd->error = -ETIMEDOUT;
		mmc_request_done(mmc, mrq);
		return;
	case MMC_SEND_EXT_CSD: /* = SD_SEND_IF_COND (8) */
		if (!mrq->data) {
			/* send_if_cond cmd (not support) */
			host->state = STATE_IDLE;
			mrq->cmd->error = -ETIMEDOUT;
			mmc_request_done(mmc, mrq);
			return;
		}
		break;
	default:
		break;
	}

	clk_enable(host->hclk);

	if (wakeup_from_suspend_emmc == 1) {
		udelay(EMMC_CLK_CMD_DELAY);
		wakeup_from_suspend_emmc = 0;
	}

	host->data = mrq->data;
	if (mrq->data) {
		pdata = host->pdata;
		if (mrq->data->sg->length < pdata->dma_min_size)
			host->dma_active = false;
		else if (mrq->data->flags & MMC_DATA_READ) {
			if (host->chan_rx)
				sh_mmcif_start_dma_rx(host);
		} else {
			if (host->chan_tx)
				sh_mmcif_start_dma_tx(host);
		}
	}
	sh_mmcif_start_cmd(host, mrq);
	host->data = NULL;

	if (!mrq->cmd->error) {
		if (mrq->stop)
			sh_mmcif_stop_cmd(host, mrq);
		else if (mrq->data)
			sh_mmcif_wait_transfer_end(host, mrq);
	}

	clk_disable(host->hclk);

	host->state = STATE_IDLE;
	mmc_request_done(mmc, mrq);
}

static void sh_mmcif_set_power(struct sh_mmcif_host *host, struct mmc_ios *ios)
{
	struct mmc_host *mmc = host->mmc;

	if (!IS_ERR(mmc->supply.vmmc)) {
		if (mmc_regulator_set_ocr(mmc, mmc->supply.vmmc,
					ios->power_mode ? ios->vdd : 0))
			dev_err(mmc_dev(mmc), "Error setting mmc regulator power\n");
	}
}

static void sh_mmcif_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (host->state != STATE_IDLE) {
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	host->state = STATE_IOS;
	spin_unlock_irqrestore(&host->lock, flags);

	if (ios->power_mode == MMC_POWER_UP) {
		if (!host->card_present) {
			/* See if we also get DMA */
			sh_mmcif_request_dma(host, host->pdata);
			host->card_present = true;
		}
		sh_mmcif_set_power(host, ios);
	} else if (ios->power_mode == MMC_POWER_OFF || !ios->clock) {
		/* clock stop */
		clk_enable(host->hclk);
		sh_mmcif_clock_control(host, 0);
		if (ios->power_mode == MMC_POWER_OFF) {
			if (host->card_present) {
				sh_mmcif_release_dma(host);
				host->card_present = false;
			}
		}
		clk_disable(host->hclk);
		if (host->power) {
			shmmcif_log("%s: power down\n", __func__);
			pm_runtime_put_sync(&host->pd->dev);
			host->power = false;
			if (ios->power_mode == MMC_POWER_OFF)
				sh_mmcif_set_power(host, ios);
		}
		host->state = STATE_IDLE;
		shmmcif_log("sh_mmcif_set_ios end\n");
		return;
	}

	if (!host->power) {
		shmmcif_log("%s: power up\n", __func__);
		pm_runtime_get_sync(&host->pd->dev);
		host->power = true;
	}

	if (ios->clock) {
		clk_enable(host->hclk);
		sh_mmcif_sync_reset(host);
		sh_mmcif_clock_control(host, ios->clock);
		clk_disable(host->hclk);
	}

	host->timing = ios->timing;
	host->bus_width = ios->bus_width;
	host->state = STATE_IDLE;
}

static int sh_mmcif_get_cd(struct mmc_host *mmc)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);
	struct sh_mmcif_plat_data *p = host->pdata;

	if (!p->get_cd)
		return -ENOSYS;
	else
		return p->get_cd(host->pd);
}

static struct mmc_host_ops sh_mmcif_ops = {
	.request	= sh_mmcif_request,
	.set_ios	= sh_mmcif_set_ios,
	.get_cd		= sh_mmcif_get_cd,
};

static irqreturn_t sh_mmcif_intr(int irq, void *dev_id)
{
	struct sh_mmcif_host *host = dev_id;
	u32 state;
	int err = 0;

	state = sh_mmcif_readl(host->addr, MMCIF_CE_INT)
		& sh_mmcif_readl(host->addr, MMCIF_CE_INT_MASK);

	if (state & INT_ERR_STS) {
		/* error interrupts - process first */
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~state);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, state);
		err = 1;
	} else if (state & INT_RBSYE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
				~(INT_RBSYE | INT_CRSPE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MRBSYE);
	} else if (state & INT_CRSPE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_CRSPE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCRSPE);
	} else if (state & INT_BUFREN) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFREN);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
	} else if (state & INT_BUFWEN) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFWEN);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);
	} else if (state & INT_CMD12DRE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
			~(INT_CMD12DRE | INT_CMD12RBE |
			  INT_CMD12CRE | INT_BUFRE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCMD12DRE);
	} else if (state & INT_BUFRE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFRE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFRE);
	} else if (state & INT_DTRANE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
			 ~(INT_CMD12DRE | INT_CMD12RBE |
                          INT_CMD12CRE | INT_DTRANE ));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MDTRANE);
	} else if (state & INT_CMD12RBE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
				~(INT_CMD12RBE | INT_CMD12CRE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCMD12RBE);
	}

	if (err) {
		host->sd_error = true;
		dev_err(&host->pd->dev, "int err state = %08x\n", state);
	}
	if (state) {
		if (host->dma_active &&
		    (state & (INT_BUFVIO | INT_CRCSTO |
				INT_WDATTO | INT_RDATTO)))
			complete(&host->dma_complete);
		else
			complete(&host->intr_wait);
	} else {
		dev_dbg(&host->pd->dev, "Unexpected IRQ 0x%x\n", state);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static struct sh_mmcif_plat_data*
renesas_mmcif_of_init(struct platform_device *pdev)
{
	struct sh_mmcif_plat_data *pdata, *aux_pdata;
	struct device_node *np = pdev->dev.of_node;

	if (!np) {
		dev_err(&pdev->dev, "device node not found\n");
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Non-existent properties will be left as 0 */
	of_property_read_u8(np, "renesas,sup-pclk", &pdata->sup_pclk);
	of_property_read_u32(np, "max-frequency", &pdata->max_clk);
	of_property_read_u32(np, "renesas,dma-min-size", &pdata->dma_min_size);
	of_property_read_u32(np, "renesas,buf-acc", &pdata->buf_acc);
	of_property_read_u32(np, "renesas,data-delay-ps",
							&pdata->data_delay_ps);

	/* if platform data is supplied using AUXDATA, then get the callbacks
	 * and other params from there.
	 * TODO:This is a temporary solution until we move all the dependencies
	 * from board file */
	if (pdev->dev.platform_data) {
		aux_pdata = pdev->dev.platform_data;
		pdata->get_cd = aux_pdata->get_cd;
		pdata->slave_id_tx = aux_pdata->slave_id_tx;
		pdata->slave_id_rx = aux_pdata->slave_id_rx;
		pdata->ocr = aux_pdata->ocr;
		pdata->caps = aux_pdata->caps;
	}
	return pdata;
}

static const struct of_device_id renesas_mmcif_of_match[] = {
	{ .compatible = "renesas,renesas-mmcif" },
	{ }
};
MODULE_DEVICE_TABLE(of, renesas_mmcif_of_match);
#else /*CONFIG_OF*/
static inline struct sh_mmcif_plat_data*
renesas_mmcif_of_init(struct platform_device *pedv)
{
	return NULL;
}
#endif /*CONFIG_OF*/

static void renesas_mmcif_init_ocr(struct sh_mmcif_host *host)
{
	struct sh_mmcif_plat_data *pdata = host->pdata;
	struct mmc_host *mmc = host->mmc;

	mmc_regulator_get_supply(mmc);
	if (!mmc->ocr_avail) {
		if (pdata->ocr)
			mmc->ocr_avail = pdata->ocr;
		else /* Default OCR values*/
			mmc->ocr_avail = MMC_VDD_165_195 | MMC_VDD_32_33
						| MMC_VDD_33_34;
	} else if (pdata->ocr)
		dev_warn(mmc_dev(mmc), "Platform OCR mask is ignored\n");
}

static int sh_mmcif_probe(struct platform_device *pdev)
{
	int ret = 0, irq[2];
	struct mmc_host *mmc;
	struct sh_mmcif_host *host;
	struct sh_mmcif_plat_data *pd;
	struct resource *res;
	void __iomem *reg;
	char clk_name[8];
	const char *name;

	/* If device tree node is present then parse and populate pdata
	 * Or fallback in platform_data */
	if (pdev->dev.of_node) {
		dev_dbg(&pdev->dev, "found device tree node\n");
		pd = renesas_mmcif_of_init(pdev);
		if (IS_ERR(pd)) {
			dev_err(&pdev->dev, "platform data not available\n");
			return PTR_ERR(pd);
		}
	} else if (pdev->dev.platform_data) {
		dev_err(&pdev->dev, "found platform_data\n");
		pd = pdev->dev.platform_data;
	} else {
		dev_err(&pdev->dev, "platform data not available\n");
		return -ENODEV;
	}

	irq[0] = platform_get_irq(pdev, 0);
	irq[1] = platform_get_irq(pdev, 1);
	if (irq[0] < 0) {
		dev_err(&pdev->dev, "Get irq error\n");
		return -ENXIO;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "platform_get_resource error.\n");
		return -ENXIO;
	}
	reg = ioremap(res->start, resource_size(res));
	if (!reg) {
		dev_err(&pdev->dev, "ioremap error.\n");
		return -ENOMEM;
	}

	mmc = mmc_alloc_host(sizeof(struct sh_mmcif_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto clean_up;
	}
	host		= mmc_priv(mmc);
	host->mmc	= mmc;
	host->addr	= reg;
	host->timeout	= msecs_to_jiffies(10000);

	wakeup_from_suspend_emmc = 0;

	snprintf(clk_name, sizeof(clk_name), "mmc%d", pdev->id);
	host->hclk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(host->hclk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(host->hclk);
		goto clean_up1;
	}
	clk_enable(host->hclk);
	host->clk = clk_get_rate(host->hclk);
	host->pd = pdev;
	host->pdata = pd;

	init_completion(&host->intr_wait);
	spin_lock_init(&host->lock);

	mmc->ops = &sh_mmcif_ops;
	if (pd->max_clk)
		mmc->f_max = pd->max_clk;
	else
		mmc->f_max = host->clk / 2;
	/* close to 400KHz */
	if (host->clk < 51200000)
		mmc->f_min = host->clk / 128;
	else if (host->clk < 102400000)
		mmc->f_min = host->clk / 256;
	else
		mmc->f_min = host->clk / 512;

	renesas_mmcif_init_ocr(host);

	mmc->caps = MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_ERASE |
			MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR;
	if (pd->caps)
		mmc->caps |= pd->caps;
	mmc->max_segs = 128;
	mmc->max_blk_size = 512;
	mmc->max_req_size = PAGE_CACHE_SIZE * mmc->max_segs;
	mmc->max_blk_count = mmc->max_req_size / mmc->max_blk_size;
	mmc->max_seg_size = mmc->max_req_size;

	host->dma_mask = DMA_BIT_MASK(64);
	mmc_dev(host->mmc)->dma_mask = &host->dma_mask;
	mmc_of_parse(mmc);

	if (pd->buf_acc)
		host->buf_acc = pd->buf_acc;
	else
		host->buf_acc = BUF_ACC_ATYP; /* with swapped byte-wise */

	platform_set_drvdata(pdev, host);

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		goto clean_up2;
	host->power = true;
	sh_mmcif_sync_reset(host);

	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, MASK_ALL);

	name = (irq[1] < 0) ? dev_name(&pdev->dev) : "sh_mmc:error";
	ret = request_irq(irq[0], sh_mmcif_intr, 0, name, host);
	if (ret) {
		dev_err(&pdev->dev, "request_irq error (%s)\n", name);
		goto clean_up3;
	}

	if (irq[1] < 0)
		goto skip;

	ret = request_irq(irq[1], sh_mmcif_intr, 0, "sh_mmc:int", host);
	if (ret) {
		dev_err(&pdev->dev, "request_irq error (sh_mmc:int)\n");
		goto clean_up4;
	}
 skip:

	ret = mmc_add_host(mmc);
	if (ret < 0)
		goto clean_up5;

	dev_info(&pdev->dev, "driver version %s\n", DRIVER_VERSION);
	dev_dbg(&pdev->dev, "chip ver H'%04x\n",
		sh_mmcif_readl(host->addr, MMCIF_CE_VERSION) & 0x0000ffff);

	pm_runtime_put_sync(&pdev->dev);
	host->power = false;
	clk_disable(host->hclk);
	return ret;

clean_up5:
	if (irq[1] >= 0)
		free_irq(irq[1], host);
clean_up4:
	free_irq(irq[0], host);
clean_up3:
	pm_runtime_put_sync(&pdev->dev);
	host->power = false;
clean_up2:
	pm_runtime_disable(&pdev->dev);
	clk_disable(host->hclk);
clean_up1:
	mmc_free_host(mmc);
clean_up:
	if (reg)
		iounmap(reg);
	return ret;
}

static int sh_mmcif_remove(struct platform_device *pdev)
{
	struct sh_mmcif_host *host = platform_get_drvdata(pdev);
	int irq[2];

	pm_runtime_get_sync(&pdev->dev);
	clk_enable(host->hclk);

	mmc_remove_host(host->mmc);
	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, MASK_ALL);

	if (host->addr)
		iounmap(host->addr);

	irq[0] = platform_get_irq(pdev, 0);
	irq[1] = platform_get_irq(pdev, 1);

	free_irq(irq[0], host);
	if (irq[1] >= 0)
		free_irq(irq[1], host);

	platform_set_drvdata(pdev, NULL);

	clk_disable(host->hclk);
	mmc_free_host(host->mmc);
	shmmcif_log("%s: power down\n", __func__);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int sh_mmcif_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_mmcif_host *host = platform_get_drvdata(pdev);
	shmmcif_log("%s: In\n", __func__);
	return mmc_suspend_host(host->mmc);
}

static int sh_mmcif_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_mmcif_host *host = platform_get_drvdata(pdev);

	wakeup_from_suspend_emmc = 1;
	shmmcif_log("%s: In\n", __func__);
	return mmc_resume_host(host->mmc);
}
#else
#define sh_mmcif_suspend	NULL
#define sh_mmcif_resume		NULL
#endif	/* CONFIG_PM */

static const struct dev_pm_ops sh_mmcif_dev_pm_ops = {
	.suspend = sh_mmcif_suspend,
	.resume = sh_mmcif_resume,
};

static struct platform_driver sh_mmcif_driver = {
	.probe		= sh_mmcif_probe,
	.remove		= sh_mmcif_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.pm	= &sh_mmcif_dev_pm_ops,
		.of_match_table = of_match_ptr(renesas_mmcif_of_match),
	},
};

module_platform_driver(sh_mmcif_driver);

MODULE_DESCRIPTION("SuperH on-chip MMC/eMMC interface driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Yusuke Goda <yusuke.goda.sx@renesas.com>");
