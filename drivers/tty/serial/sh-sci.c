/*
 * SuperH on-chip serial module support.  (SCI with no FIFO / with FIFO)
 *
 *  Copyright (C) 2002 - 2011  Paul Mundt
 *  Modified to support SH7720 SCIF. Markus Brunner, Mark Jonas (Jul 2007).
 *
 * based off of the old drivers/char/sh-sci.c by:
 *
 *   Copyright (C) 1999, 2000  Niibe Yutaka
 *   Copyright (C) 2000  Sugioka Toshinobu
 *   Modified to support multiple serial ports. Stuart Menefy (May 2000).
 *   Modified to support SecureEdge. David McCullough (2002)
 *   Modified to support SH7300 SCIF. Takashi Kusuda (Jun 2003).
 *   Removed SH7300 support (Jul 2007).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#if defined(CONFIG_SERIAL_SH_SCI_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#undef DEBUG

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sh_dma.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/platform_device.h>
#include <linux/serial_sci.h>
#include <linux/notifier.h>
#include <linux/pm_runtime.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/nmi.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/time.h>
#include <linux/shdma.h>

#ifdef CONFIG_SUPERH
#include <asm/sh_bios.h>
#endif

#include "sh-sci.h"

#define SCIFB1_SCxSR_TEND_BIT_READ_ATTEMPT	 100

/* CMT count equivalent to 10s, CMT0 is running at 13MHz*/
#define SCI_STATUS_BIT_POLL_TIMEOUT     (13000000*10)
#define CMCNT0                          (0xE6130014)
static void __iomem *cmt10_virt;

/*
 * Frequency of the SCIFA interrupts. These values
 * are defined after observing #2 memlogs, where
 * the issue has happened.
 */
#define SCIFA_TIME_USEC	6
#define PASS_LIMIT	8

/* Making all variables as globals.
 * Unfortunately if we dont get prints
 * we can have the data from ramdump.
 * If the freeze happen in other than
 * interrupt handler, the the prev and
 * current valurs are same.
 */
unsigned long curr_usec;
unsigned long prev_usec;
int curr_intr_flag;
int prev_intr_flag;
int pass_counter;

#ifdef CONFIG_SERIAL_CORE_CONSOLE
#define sci_uart_console(port)	\
	((port)->cons && (port)->cons->index == (port)->line)
#else
#define sci_uart_console(port)	(0)
#endif
struct sci_port {
	struct uart_port	port;

	/* Platform configuration */
	struct plat_sci_port	*cfg;

	/* Break timer */
	struct timer_list	break_timer;
	int			break_flag;

	/* Interface clock */
	struct clk		*iclk;
	/* Function clock */
	struct clk		*fclk;

	char			*irqstr[SCIx_NR_IRQS];
	char			*gpiostr[SCIx_NR_FNS];

	struct dma_chan			*chan_tx;
	struct dma_chan			*chan_rx;

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	struct dma_async_tx_descriptor	*desc_tx;
	struct dma_async_tx_descriptor	*desc_rx[2];
	dma_cookie_t			cookie_tx;
	dma_cookie_t			cookie_rx[2];
	dma_cookie_t			active_rx;
	struct scatterlist		sg_tx;
	unsigned int			sg_len_tx;
	struct scatterlist		sg_rx[2];
	size_t				buf_len_rx;
	struct sh_dmae_slave		param_tx;
	struct sh_dmae_slave		param_rx;
	struct work_struct		work_tx;
	struct work_struct		work_rx;
	struct timer_list		rx_timer;
	unsigned int			rx_timeout;
#endif

	struct notifier_block		freq_transition;
	/* PCP# TT12102320062
	Save & retain termios->c_cflag value */
	int	cons_cflag ;
};

/* Function prototypes */
static void sci_start_tx(struct uart_port *port);
static void sci_stop_tx(struct uart_port *port);
static void sci_start_rx(struct uart_port *port);

#define SCI_NPORTS CONFIG_SERIAL_SH_SCI_NR_UARTS

static struct sci_port sci_ports[SCI_NPORTS];
static struct uart_driver sci_uart_driver;

static inline struct sci_port *
to_sci_port(struct uart_port *uart)
{
	return container_of(uart, struct sci_port, port);
}

struct plat_sci_reg {
	u8 offset, size;
};

/* Helper for invalidating specific entries of an inherited map. */
#define sci_reg_invalid	{ .offset = 0, .size = 0 }

static struct plat_sci_reg sci_regmap[SCIx_NR_REGTYPES][SCIx_NR_REGS] = {
	[SCIx_PROBE_REGTYPE] = {
		[0 ... SCIx_NR_REGS - 1] = sci_reg_invalid,
	},

	/*
	 * Common SCI definitions, dependent on the port's regshift
	 * value.
	 */
	[SCIx_SCI_REGTYPE] = {
		[SCSMR]		= { 0x00,  8 },
		[SCBRR]		= { 0x01,  8 },
		[SCSCR]		= { 0x02,  8 },
		[SCxTDR]	= { 0x03,  8 },
		[SCxSR]		= { 0x04,  8 },
		[SCxRDR]	= { 0x05,  8 },
		[SCFCR]		= sci_reg_invalid,
		[SCFDR]		= sci_reg_invalid,
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common definitions for legacy IrDA ports, dependent on
	 * regshift value.
	 */
	[SCIx_IRDA_REGTYPE] = {
		[SCSMR]		= { 0x00,  8 },
		[SCBRR]		= { 0x01,  8 },
		[SCSCR]		= { 0x02,  8 },
		[SCxTDR]	= { 0x03,  8 },
		[SCxSR]		= { 0x04,  8 },
		[SCxRDR]	= { 0x05,  8 },
		[SCFCR]		= { 0x06,  8 },
		[SCFDR]		= { 0x07, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SCIFA definitions.
	 */
	[SCIx_SCIFA_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x20,  8 },
		[SCxSR]		= { 0x14, 16 },
		[SCxRDR]	= { 0x24,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[SCPCR]		= { 0x30, 16 },
		[SCPDR]		= { 0x34, 16 },
	},

	/*
	 * Common SCIFB definitions.
	 */
	[SCIx_SCIFB_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x40,  8 },
		[SCxSR]		= { 0x14, 16 },
		[SCxRDR]	= { 0x60,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= sci_reg_invalid,
		[SCTFDR]	= { 0x38, 16 },
		[SCRFDR]	= { 0x3c, 16 },
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[SCPCR]		= { 0x30, 16 },
		[SCPDR]		= { 0x34, 16 },
	},

	/*
	 * Common SH-2(A) SCIF definitions for ports with FIFO data
	 * count registers.
	 */
	[SCIx_SH2_SCIF_FIFODATA_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= { 0x20, 16 },
		[SCLSR]		= { 0x24, 16 },
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-3 SCIF definitions.
	 */
	[SCIx_SH3_SCIF_REGTYPE] = {
		[SCSMR]		= { 0x00,  8 },
		[SCBRR]		= { 0x02,  8 },
		[SCSCR]		= { 0x04,  8 },
		[SCxTDR]	= { 0x06,  8 },
		[SCxSR]		= { 0x08, 16 },
		[SCxRDR]	= { 0x0a,  8 },
		[SCFCR]		= { 0x0c,  8 },
		[SCFDR]		= { 0x0e, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-4(A) SCIF(B) definitions.
	 */
	[SCIx_SH4_SCIF_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= { 0x20, 16 },
		[SCLSR]		= { 0x24, 16 },
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-4(A) SCIF(B) definitions for ports without an SCSPTR
	 * register.
	 */
	[SCIx_SH4_SCIF_NO_SCSPTR_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= { 0x24, 16 },
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-4(A) SCIF(B) definitions for ports with FIFO data
	 * count registers.
	 */
	[SCIx_SH4_SCIF_FIFODATA_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= { 0x1c, 16 },	/* aliased to SCFDR */
		[SCRFDR]	= { 0x20, 16 },
		[SCSPTR]	= { 0x24, 16 },
		[SCLSR]		= { 0x28, 16 },
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * SH7705-style SCIF(B) ports, lacking both SCSPTR and SCLSR
	 * registers.
	 */
	[SCIx_SH7705_SCIF_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x20,  8 },
		[SCxSR]		= { 0x14, 16 },
		[SCxRDR]	= { 0x24,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},
};

#define sci_getreg(up, offset)		(sci_regmap[to_sci_port(up)->cfg->regtype] + offset)

/*
 * The "offset" here is rather misleading, in that it refers to an enum
 * value relative to the port mapping rather than the fixed offset
 * itself, which needs to be manually retrieved from the platform's
 * register map for the given port.
 */
static unsigned int sci_serial_in(struct uart_port *p, int offset)
{
	struct plat_sci_reg *reg = sci_getreg(p, offset);

	if (reg->size == 8)
		return ioread8(p->membase + (reg->offset << p->regshift));
	else if (reg->size == 16)
		return ioread16(p->membase + (reg->offset << p->regshift));
	else
		WARN(1, "Invalid register access\n");

	return 0;
}

static void sci_serial_out(struct uart_port *p, int offset, int value)
{
	struct plat_sci_reg *reg = sci_getreg(p, offset);

	if (reg->size == 8)
		iowrite8(value, p->membase + (reg->offset << p->regshift));
	else if (reg->size == 16)
		iowrite16(value, p->membase + (reg->offset << p->regshift));
	else
		WARN(1, "Invalid register access\n");
}

static int sci_probe_regmap(struct plat_sci_port *cfg)
{
	switch (cfg->type) {
	case PORT_SCI:
		cfg->regtype = SCIx_SCI_REGTYPE;
		break;
	case PORT_IRDA:
		cfg->regtype = SCIx_IRDA_REGTYPE;
		break;
	case PORT_SCIFA:
		cfg->regtype = SCIx_SCIFA_REGTYPE;
		break;
	case PORT_SCIFB:
		cfg->regtype = SCIx_SCIFB_REGTYPE;
		break;
	case PORT_SCIF:
		/*
		 * The SH-4 is a bit of a misnomer here, although that's
		 * where this particular port layout originated. This
		 * configuration (or some slight variation thereof)
		 * remains the dominant model for all SCIFs.
		 */
		cfg->regtype = SCIx_SH4_SCIF_REGTYPE;
		break;
	default:
		printk(KERN_ERR "Can't probe register map for given port\n");
		return -EINVAL;
	}

	return 0;
}

static void sci_port_enable(struct sci_port *sci_port)
{
	if (!sci_port->port.dev)
		return;

	pm_runtime_get_sync(sci_port->port.dev);

	clk_enable(sci_port->iclk);
	sci_port->port.uartclk = clk_get_rate(sci_port->iclk);
	clk_enable(sci_port->fclk);
}

static void sci_port_disable(struct sci_port *sci_port)
{
	if (!sci_port->port.dev)
		return;

	clk_disable(sci_port->fclk);
	clk_disable(sci_port->iclk);

	pm_runtime_put_sync(sci_port->port.dev);
}

#if defined(CONFIG_CONSOLE_POLL) || defined(CONFIG_SERIAL_SH_SCI_CONSOLE)

#ifdef CONFIG_CONSOLE_POLL
static int sci_poll_get_char(struct uart_port *port)
{
	unsigned short status;
	int c;

	do {
		status = serial_port_in(port, SCxSR);
		if (status & SCxSR_ERRORS(port)) {
			serial_port_out(port, SCxSR, SCxSR_ERROR_CLEAR(port));
			continue;
		}
		break;
	} while (1);

	if (!(status & SCxSR_RDxF(port)))
		return NO_POLL_CHAR;

	c = serial_port_in(port, SCxRDR);

	/* Dummy read */
	serial_port_in(port, SCxSR);
	serial_port_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));

	return c;
}
#endif

static void sci_poll_put_char(struct uart_port *port, unsigned char c)
{
	unsigned short status;
	unsigned long timeout = (__raw_readl(cmt10_virt) +
				SCI_STATUS_BIT_POLL_TIMEOUT);
	bool tout = 0;

	/* There are some scenerios where one of the cpu gets stuck
	 * at serial_in and wdt overflow happens.So add a timeout to find out
	 * if there is any bug in updating status bits */
	do {
		status = serial_port_in(port, SCxSR);
	} while (!(status & (SCxSR_TDxE(port) | SCxSR_TEND(port))) &&
		!(tout = time_after(((unsigned long)__raw_readl(cmt10_virt)),
								timeout)));

	BUG_ON(tout);

	serial_port_out(port, SCxTDR, c);
	serial_port_out(port, SCxSR, SCxSR_TDxE_CLEAR(port) & ~SCxSR_TEND(port));
}
#endif /* CONFIG_CONSOLE_POLL || CONFIG_SERIAL_SH_SCI_CONSOLE */

static void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	struct sci_port *s = to_sci_port(port);
	struct plat_sci_reg *reg = sci_regmap[s->cfg->regtype] + SCSPTR;
	struct uart_state *state = port->state;
	struct tty_port *tty_port = &state->port;

	/*
	 * Use port-specific handler if provided.
	 */
	if (s->cfg->ops && s->cfg->ops->init_pins) {
		/* PCP# VA13022646818 -
		Check to achieve device wakeup functionality
		in BT keyboard use case avoiding frame loss due
		to RTS pin set to low before SCIF ports get resumed
		after deep sleep */
		if (!(tty_port->flags & ASYNC_SUSPENDED))
			s->cfg->ops->init_pins(port, cflag);
		return;
	}

	/*
	 * For the generic path SCSPTR is necessary. Bail out if that's
	 * unavailable, too.
	 */
	if (!reg->size)
		return;

	if ((s->cfg->capabilities & SCIx_HAVE_RTSCTS) &&
	    ((!(cflag & CRTSCTS)))) {
		unsigned short status;

		status = serial_port_in(port, SCSPTR);
		status &= ~SCSPTR_CTSIO;
		status |= SCSPTR_RTSIO;
		serial_port_out(port, SCSPTR, status); /* Set RTS = 1 */
	}
}

static int sci_txfill(struct uart_port *port)
{
	struct plat_sci_reg *reg;

	reg = sci_getreg(port, SCTFDR);
	if (reg->size)
		return serial_port_in(port, SCTFDR) & ((port->fifosize << 1) - 1);

	reg = sci_getreg(port, SCFDR);
	if (reg->size)
		return serial_port_in(port, SCFDR) >> 8;

	return !(serial_port_in(port, SCxSR) & SCI_TDRE);
}

static int sci_txroom(struct uart_port *port)
{
	return port->fifosize - sci_txfill(port);
}

static int sci_rxfill(struct uart_port *port)
{
	struct plat_sci_reg *reg;

	reg = sci_getreg(port, SCRFDR);
	if (reg->size)
		return serial_port_in(port, SCRFDR) & ((port->fifosize << 1) - 1);

	reg = sci_getreg(port, SCFDR);
	if (reg->size)
		return serial_port_in(port, SCFDR) & ((port->fifosize << 1) - 1);

	return (serial_port_in(port, SCxSR) & SCxSR_RDxF(port)) != 0;
}

/*
 * SCI helper for checking the state of the muxed port/RXD pins.
 */
static inline int sci_rxd_in(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);

	if (s->cfg->port_reg <= 0)
		return 1;

	/* Cast for ARM damage */
	return !!__raw_readb((void __iomem *)s->cfg->port_reg);
}

/* ********************************************************************** *
 *                   the interrupt related routines                       *
 * ********************************************************************** */

static void sci_transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int stopped = uart_tx_stopped(port);
	unsigned short status;
	unsigned short ctrl;
	int count;

	status = serial_port_in(port, SCxSR);
	if (!(status & SCxSR_TDxE(port))) {
		ctrl = serial_port_in(port, SCSCR);
		if (uart_circ_empty(xmit))
			ctrl &= ~SCSCR_TIE;
		else
			ctrl |= SCSCR_TIE;
		serial_port_out(port, SCSCR, ctrl);
		return;
	}

	count = sci_txroom(port);

	if (count > 0) {
		do {
			unsigned char c;

			if (port->x_char) {
				c = port->x_char;
				port->x_char = 0;
			} else if (!uart_circ_empty(xmit) && !stopped) {
				c = xmit->buf[xmit->tail];
				xmit->tail = (xmit->tail + 1) &
						(UART_XMIT_SIZE - 1);
			} else {
				break;
			}

			serial_port_out(port, SCxTDR, c);

			port->icount.tx++;
		} while (--count > 0);
	}

	serial_port_out(port, SCxSR, SCxSR_TDxE_CLEAR(port));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	if (uart_circ_empty(xmit)) {
		sci_stop_tx(port);
	} else {
		ctrl = serial_port_in(port, SCSCR);

		if (port->type != PORT_SCI) {
			serial_port_in(port, SCxSR); /* Dummy read */
			serial_port_out(port, SCxSR, SCxSR_TDxE_CLEAR(port));
		}

		ctrl |= SCSCR_TIE;
		serial_port_out(port, SCSCR, ctrl);
	}
}

/* On SH3, SCIF may read end-of-break as a space->mark char */
#define STEPFN(c)  ({int __c = (c); (((__c-1)|(__c)) == -1); })

static void sci_receive_chars(struct uart_port *port)
{
	struct sci_port *sci_port = to_sci_port(port);
	struct tty_port *tport = &port->state->port;
	int i, count, copied = 0;
	unsigned short status;
	unsigned char flag;

	status = serial_port_in(port, SCxSR);
	if (!(status & SCxSR_RDxF(port)))
		return;

	while (1) {
		/* Don't copy more bytes than there is room for in the buffer */
		count = tty_buffer_request_room(tport, sci_rxfill(port));

		/* If for any reason we can't copy more data, we're done! */
		if (count == 0)
			break;

		if (port->type == PORT_SCI) {
			char c = serial_port_in(port, SCxRDR);
			if (uart_handle_sysrq_char(port, c) ||
			    sci_port->break_flag)
				count = 0;
			else
				tty_insert_flip_char(tport, c, TTY_NORMAL);
		} else {
			for (i = 0; i < count; i++) {
				char c = serial_port_in(port, SCxRDR);

				status = serial_port_in(port, SCxSR);
#if defined(CONFIG_CPU_SH3)
				/* Skip "chars" during break */
				if (sci_port->break_flag) {
					if ((c == 0) &&
					    (status & SCxSR_FER(port))) {
						count--; i--;
						continue;
					}

					/* Nonzero => end-of-break */
					sci_port->break_flag = 0;

					if (STEPFN(c)) {
						count--; i--;
						continue;
					}
				}
#endif /* CONFIG_CPU_SH3 */
				if (uart_handle_sysrq_char(port, c)) {
					count--; i--;
					continue;
				}

				/* Store data and status */
				if (status & SCxSR_FER(port)) {
					flag = TTY_FRAME;
					port->icount.frame++;
				} else if (status & SCxSR_PER(port)) {
					flag = TTY_PARITY;
					port->icount.parity++;
				} else
					flag = TTY_NORMAL;

				tty_insert_flip_char(tport, c, flag);
			}
		}

		serial_port_in(port, SCxSR); /* dummy read */
		serial_port_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));

		copied += count;
		port->icount.rx += count;
	}

	if (copied) {
		/* Tell the rest of the system the news. New characters! */
		tty_flip_buffer_push(tport);
	} else {
		serial_port_in(port, SCxSR); /* dummy read */
		serial_port_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));
	}
}

#define SCI_BREAK_JIFFIES (HZ/20)

/*
 * The sci generates interrupts during the break,
 * 1 per millisecond or so during the break period, for 9600 baud.
 * So dont bother disabling interrupts.
 * But dont want more than 1 break event.
 * Use a kernel timer to periodically poll the rx line until
 * the break is finished.
 */
static inline void sci_schedule_break_timer(struct sci_port *port)
{
	mod_timer(&port->break_timer, jiffies + SCI_BREAK_JIFFIES);
}

/* Ensure that two consecutive samples find the break over. */
static void sci_break_timer(unsigned long data)
{
	struct sci_port *port = (struct sci_port *)data;

	sci_port_enable(port);

	if (sci_rxd_in(&port->port) == 0) {
		port->break_flag = 1;
		sci_schedule_break_timer(port);
	} else if (port->break_flag == 1) {
		/* break is over. */
		port->break_flag = 2;
		sci_schedule_break_timer(port);
	} else
		port->break_flag = 0;

	sci_port_disable(port);
}

static int sci_handle_errors(struct uart_port *port)
{
	int copied = 0;
	unsigned short status = serial_port_in(port, SCxSR);
	struct tty_port *tport = &port->state->port;
	struct sci_port *s = to_sci_port(port);

	/*
	 * Handle overruns, if supported.
	 */
	if (s->cfg->overrun_bit != SCIx_NOT_SUPPORTED) {
		if (status & (1 << s->cfg->overrun_bit)) {
			port->icount.overrun++;

			/* overrun error */
			if (tty_insert_flip_char(tport, 0, TTY_OVERRUN))
				copied++;

		}
	}

	if (status & SCxSR_FER(port)) {
		if (sci_rxd_in(port) == 0) {
			/* Notify of BREAK */
			struct sci_port *sci_port = to_sci_port(port);

			if (!sci_port->break_flag) {
				port->icount.brk++;

				sci_port->break_flag = 1;
				sci_schedule_break_timer(sci_port);

				/* Do sysrq handling. */
				if (uart_handle_break(port))
					return 0;


				if (tty_insert_flip_char(tport, 0, TTY_BREAK))
					copied++;
			}

		} else {
			/* frame error */
			port->icount.frame++;

			if (tty_insert_flip_char(tport, 0, TTY_FRAME))
				copied++;

		}
	}

	if (status & SCxSR_PER(port)) {
		/* parity error */
		port->icount.parity++;

		if (tty_insert_flip_char(tport, 0, TTY_PARITY))
			copied++;

	}

	if (copied)
		tty_flip_buffer_push(tport);

	return copied;
}

static int sci_handle_fifo_overrun(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	struct sci_port *s = to_sci_port(port);
	struct plat_sci_reg *reg;
	int copied = 0;

	reg = sci_getreg(port, SCLSR);
	if (!reg->size)
		return 0;

	if ((serial_port_in(port, SCLSR) & (1 << s->cfg->overrun_bit))) {
		serial_port_out(port, SCLSR, 0);

		port->icount.overrun++;

		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
		tty_flip_buffer_push(tport);

		copied++;
	}

	return copied;
}

static int sci_handle_breaks(struct uart_port *port)
{
	int copied = 0;
	unsigned short status = serial_port_in(port, SCxSR);
	struct tty_port *tport = &port->state->port;
	struct sci_port *s = to_sci_port(port);

	if (uart_handle_break(port))
		return 0;

	if (!s->break_flag && status & SCxSR_BRK(port)) {
#if defined(CONFIG_CPU_SH3)
		/* Debounce break */
		s->break_flag = 1;
#endif

		port->icount.brk++;

		/* Notify of BREAK */
		if (tty_insert_flip_char(tport, 0, TTY_BREAK))
			copied++;

	}

	if (copied)
		tty_flip_buffer_push(tport);

	copied += sci_handle_fifo_overrun(port);

	return copied;
}

static irqreturn_t sci_rx_interrupt(int irq, void *ptr)
{
	unsigned long flags = 0 ;
	struct uart_port *port = ptr;
	struct sci_port *s = to_sci_port(port);

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	if (s->chan_rx) {
		u16 scr = serial_port_in(port, SCSCR);
		u16 ssr = serial_port_in(port, SCxSR);

		/* Disable future Rx interrupts */
		if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
			disable_irq_nosync(irq);
			scr |= 0x4000;
		} else {
			scr &= ~SCSCR_RIE;
		}
		serial_port_out(port, SCSCR, scr);
		/* Clear current interrupt */
		serial_port_out(port, SCxSR, ssr & ~(1 | SCxSR_RDxF(port)));
		dev_dbg(port->dev, "Rx IRQ %lu: setup t-out in %u jiffies\n",
			jiffies, s->rx_timeout);
		mod_timer(&s->rx_timer, jiffies + s->rx_timeout);

		return IRQ_HANDLED;
	}
#endif

	if (s->cfg->irqs[0] != s->cfg->irqs[1])
		spin_lock_irqsave(&port->lock, flags);
	/* I think sci_receive_chars has to be called irrespective
	 * of whether the I_IXOFF is set, otherwise, how is the interrupt
	 * to be disabled?
	 */
	sci_receive_chars(ptr);

	if (s->cfg->irqs[0] != s->cfg->irqs[1])
		spin_unlock_irqrestore(&port->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t sci_tx_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;
	unsigned long flags = 0 ;

	struct sci_port *s = to_sci_port(port);
	if (s->cfg->irqs[0] != s->cfg->irqs[1])
		spin_lock_irqsave(&port->lock, flags);
	sci_transmit_chars(port);
	if (s->cfg->irqs[0] != s->cfg->irqs[1])
		spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t sci_er_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;

	/* Handle errors */
	if (port->type == PORT_SCI) {
		if (sci_handle_errors(port)) {
			/* discard character in rx buffer */
			serial_port_in(port, SCxSR);
			serial_port_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));
		}
	} else {
		sci_handle_fifo_overrun(port);
		sci_rx_interrupt(irq, ptr);
	}

	serial_port_out(port, SCxSR, SCxSR_ERROR_CLEAR(port));

	/* Kick the transmission */
	sci_tx_interrupt(irq, ptr);

	return IRQ_HANDLED;
}

static irqreturn_t sci_br_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;

	/* Handle BREAKs */
	sci_handle_breaks(port);
	serial_port_out(port, SCxSR, SCxSR_BREAK_CLEAR(port));

	return IRQ_HANDLED;
}

static inline unsigned long port_rx_irq_mask(struct uart_port *port)
{
	/*
	 * Not all ports (such as SCIFA) will support REIE. Rather than
	 * special-casing the port type, we check the port initialization
	 * IRQ enable mask to see whether the IRQ is desired at all. If
	 * it's unset, it's logically inferred that there's no point in
	 * testing for it.
	 */
	return SCSCR_RIE | (to_sci_port(port)->cfg->scscr & SCSCR_REIE);
}

static irqreturn_t sci_mpxed_interrupt(int irq, void *ptr)
{
	unsigned short ssr_status, scr_status, err_enabled;
	struct uart_port *port = ptr;
	struct sci_port *s = to_sci_port(port);
	irqreturn_t ret = IRQ_NONE;
	unsigned long flags = 0 ;
	struct timeval t;
	int reg_type = s->cfg->regtype;

	if (reg_type == SCIx_SCIFA_REGTYPE)
		curr_intr_flag = 0;

	spin_lock_irqsave(&port->lock, flags);
	ssr_status = serial_port_in(port, SCxSR);
	scr_status = serial_port_in(port, SCSCR);
	err_enabled = scr_status & port_rx_irq_mask(port);

	/* Tx Interrupt */
	if ((ssr_status & SCxSR_TDxE(port)) && (scr_status & SCSCR_TIE) &&
	    !s->chan_tx) {
		if (reg_type == SCIx_SCIFA_REGTYPE)
			curr_intr_flag = 1;
		ret = sci_tx_interrupt(irq, ptr);
	}

	/*
	 * Rx Interrupt: if we're using DMA, the DMA controller clears RDF /
	 * DR flags
	 */
	if (((ssr_status & SCxSR_RDxF(port)) || s->chan_rx) &&
	    (scr_status & SCSCR_RIE)) {
		if (reg_type == SCIx_SCIFA_REGTYPE)
			curr_intr_flag = 2;
		ret = sci_rx_interrupt(irq, ptr);
	}

	/* Error Interrupt */
	if ((ssr_status & SCxSR_ERRORS(port)) && err_enabled) {
		if (reg_type == SCIx_SCIFA_REGTYPE)
			curr_intr_flag = 4;
		ret = sci_er_interrupt(irq, ptr);
	}

	/* Break Interrupt */
	if ((ssr_status & SCxSR_BRK(port)) && err_enabled) {
		if (reg_type == SCIx_SCIFA_REGTYPE)
			curr_intr_flag = 8;
		ret = sci_br_interrupt(irq, ptr);
	}

	spin_unlock_irqrestore(&port->lock, flags) ;

	/*
	 * Observing continuous UART console interrupts
	 * in some rare occassions. This is to debug the issue.
	 * Dumping all the UART registers.
	 * This is restricted to SCIFA port. Not observed on SCIFB port.
	 */
	if (s->cfg->regtype == SCIx_SCIFA_REGTYPE) {
		do_gettimeofday(&t);

		curr_usec = ((t.tv_sec * 1000000) + t.tv_usec);
		if ((curr_usec < prev_usec) ? false :
				((curr_usec - prev_usec) <
				 (unsigned long)SCIFA_TIME_USEC)) {

			/* Once we are here, dont want to go further. */
			if (curr_intr_flag & prev_intr_flag) {
				if (pass_counter > PASS_LIMIT) {
					/*
					 * If we hit this condition, we are
					 * DEAD. Dumping the UART registers
					 * for debugging.
					 */
					pr_err("ssr_status = 0x%04x\n",
							ssr_status);
					pr_err("scr_status = 0x%04x\n",
							scr_status);
					pr_err("err_enabled = 0x%04x\n",
							err_enabled);
					pr_err("too much work for irq%d\n",
							irq);
					pr_err("Tx circ chars pending = %ld\n",
						uart_circ_chars_pending(
						&port->state->xmit));
					pr_err("prev_intr_flag = %d\n",
							prev_intr_flag);
					pr_err("curr_intr_flag = %d\n",
							curr_intr_flag);

					pr_err("Dumping UART registers\n");
					pr_err("SCSMR = 0x%04x\n",
						serial_port_in(port, SCSMR));
					pr_err("SCBRR = 0x%04x\n",
						serial_port_in(port, SCBRR));
					pr_err("SCSCR = 0x%04x\n",
						serial_port_in(port, SCSCR));
					pr_err("SCxSR = 0x%04x\n",
						serial_port_in(port, SCxSR));
					pr_err("SCFCR = 0x%04x\n",
						serial_port_in(port, SCFCR));
					if (s->cfg->regtype ==
							SCIx_SCIFA_REGTYPE) {
						pr_err("SCFDR = 0x%04x\n",
						serial_port_in(port, SCFDR));
					}
					if (s->cfg->regtype ==
							SCIx_SCIFB_REGTYPE) {
						pr_err("SCTFDR = 0x%04x\n",
						serial_port_in(port, SCTFDR));
						pr_err("SCRFDR = 0x%04x\n",
						serial_port_in(port, SCRFDR));
					}
					pr_err("SCPCR = 0x%04x\n",
						serial_port_in(port, SCPCR));
					pr_err("SCPDR = 0x%04x\n",
						serial_port_in(port, SCPDR));
					pr_err("SCxTDR = 0x%04x\n",
						serial_port_in(port, SCxTDR));
					pr_err("SCxRDR = 0x%04x\n",
						serial_port_in(port, SCxRDR));
					BUG_ON(1);
				} /* End of (pass_counter > PASS_LIMIT)  */
				pass_counter++;
			} /* End of (curr_intr_flag == prev_intr_flag) */
			else
				pass_counter = 0;
		}
		prev_intr_flag = curr_intr_flag;
		prev_usec = curr_usec;
	}

	return ret;
}

/*
 * Here we define a transition notifier so that we can update all of our
 * ports' baud rate when the peripheral clock changes.
 */
static int sci_notifier(struct notifier_block *self,
			unsigned long phase, void *p)
{
	struct sci_port *sci_port;
	unsigned long flags;

	sci_port = container_of(self, struct sci_port, freq_transition);

	if ((phase == CPUFREQ_POSTCHANGE) ||
	    (phase == CPUFREQ_RESUMECHANGE)) {
		struct uart_port *port = &sci_port->port;

		spin_lock_irqsave(&port->lock, flags);
		port->uartclk = clk_get_rate(sci_port->iclk);
		spin_unlock_irqrestore(&port->lock, flags);
	}

	return NOTIFY_OK;
}

static struct sci_irq_desc {
	const char	*desc;
	irq_handler_t	handler;
} sci_irq_desc[] = {
	/*
	 * Split out handlers, the default case.
	 */
	[SCIx_ERI_IRQ] = {
		.desc = "rx err",
		.handler = sci_er_interrupt,
	},

	[SCIx_RXI_IRQ] = {
		.desc = "rx full",
		.handler = sci_rx_interrupt,
	},

	[SCIx_TXI_IRQ] = {
		.desc = "tx empty",
		.handler = sci_tx_interrupt,
	},

	[SCIx_BRI_IRQ] = {
		.desc = "break",
		.handler = sci_br_interrupt,
	},

	/*
	 * Special muxed handler.
	 */
	[SCIx_MUX_IRQ] = {
		.desc = "mux",
		.handler = sci_mpxed_interrupt,
	},
};

static int sci_request_irq(struct sci_port *port)
{
	struct uart_port *up = &port->port;
	int i, j, ret = 0;

	for (i = j = 0; i < SCIx_NR_IRQS; i++, j++) {
		struct sci_irq_desc *desc;
		unsigned int irq;

		if (SCIx_IRQ_IS_MUXED(port)) {
			i = SCIx_MUX_IRQ;
			irq = up->irq;
		} else {
			irq = port->cfg->irqs[i];

			/*
			 * Certain port types won't support all of the
			 * available interrupt sources.
			 */
			if (unlikely(!irq))
				continue;
		}

		desc = sci_irq_desc + i;
		port->irqstr[j] = kasprintf(GFP_KERNEL, "%s:%s",
					    dev_name(up->dev), desc->desc);
		if (!port->irqstr[j]) {
			dev_err(up->dev, "Failed to allocate %s IRQ string\n",
				desc->desc);
			goto out_nomem;
		}

		ret = request_irq(irq, desc->handler, up->irqflags,
				  port->irqstr[j], port);
		if (unlikely(ret)) {
			dev_err(up->dev, "Can't allocate %s IRQ\n", desc->desc);
			goto out_noirq;
		}
	}

	return 0;

out_noirq:
	while (--i >= 0)
		free_irq(port->cfg->irqs[i], port);

out_nomem:
	while (--j >= 0)
		kfree(port->irqstr[j]);

	return ret;
}

static void sci_free_irq(struct sci_port *port)
{
	int i;

	/*
	 * Intentionally in reverse order so we iterate over the muxed
	 * IRQ first.
	 */
	for (i = 0; i < SCIx_NR_IRQS; i++) {
		unsigned int irq = port->cfg->irqs[i];

		/*
		 * Certain port types won't support all of the available
		 * interrupt sources.
		 */
		if (unlikely(!irq))
			continue;

		free_irq(port->cfg->irqs[i], port);
		kfree(port->irqstr[i]);

		if (SCIx_IRQ_IS_MUXED(port)) {
			/* If there's only one IRQ, we're done. */
			return;
		}
	}
}

static const char *sci_gpio_names[SCIx_NR_FNS] = {
	"sck", "rxd", "txd", "cts", "rts",
};

static const char *sci_gpio_str(unsigned int index)
{
	return sci_gpio_names[index];
}

static void sci_init_gpios(struct sci_port *port)
{
	struct uart_port *up = &port->port;
	int i;

	if (!port->cfg)
		return;

	for (i = 0; i < SCIx_NR_FNS; i++) {
		const char *desc;
		int ret;

		if (!port->cfg->gpios[i])
			continue;

		desc = sci_gpio_str(i);

		port->gpiostr[i] = kasprintf(GFP_KERNEL, "%s:%s",
					     dev_name(up->dev), desc);

		/*
		 * If we've failed the allocation, we can still continue
		 * on with a NULL string.
		 */
		if (!port->gpiostr[i])
			dev_notice(up->dev, "%s string allocation failure\n",
				   desc);

		ret = gpio_request(port->cfg->gpios[i], port->gpiostr[i]);
		if (unlikely(ret != 0)) {
			dev_notice(up->dev, "failed %s gpio request\n", desc);

			/*
			 * If we can't get the GPIO for whatever reason,
			 * no point in keeping the verbose string around.
			 */
			kfree(port->gpiostr[i]);
		}
	}
}

static void sci_free_gpios(struct sci_port *port)
{
	int i;

	for (i = 0; i < SCIx_NR_FNS; i++)
		if (port->cfg->gpios[i]) {
			gpio_free(port->cfg->gpios[i]);
			kfree(port->gpiostr[i]);
		}
}

static unsigned int sci_tx_empty(struct uart_port *port)
{
	unsigned short status = serial_port_in(port, SCxSR);
	unsigned short in_tx_fifo = sci_txfill(port);

	return (status & SCxSR_TEND(port)) && !in_tx_fifo ? TIOCSER_TEMT : 0;
}

/*
 * Modem control is a bit of a mixed bag for SCI(F) ports. Generally
 * CTS/RTS is supported in hardware by at least one port and controlled
 * via SCSPTR (SCxPCR for SCIFA/B parts), or external pins (presently
 * handled via the ->init_pins() op, which is a bit of a one-way street,
 * lacking any ability to defer pin control -- this will later be
 * converted over to the GPIO framework).
 *
 * Other modes (such as loopback) are supported generically on certain
 * port types, but not others. For these it's sufficient to test for the
 * existence of the support register and simply ignore the port type.
 */
static void sci_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	if (mctrl & TIOCM_LOOP) {
		struct plat_sci_reg *reg;

		/*
		 * Standard loopback mode for SCFCR ports.
		 */
		reg = sci_getreg(port, SCFCR);
		if (reg->size)
			serial_port_out(port, SCFCR, serial_port_in(port, SCFCR) | 1);
	}
}

static unsigned int sci_get_mctrl(struct uart_port *port)
{
	/* This routine is used for getting signals of: DTR, DCD, DSR, RI,
	 * and CTS/RTS */
	return  TIOCM_DTR | TIOCM_RTS | TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

#ifdef CONFIG_SERIAL_SH_SCI_DMA
static void sci_dma_tx_complete(void *arg)
{
	struct sci_port *s = arg;
	struct uart_port *port = &s->port;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned long flags;

	dev_dbg(port->dev, "%s(%d)\n", __func__, port->line);

	spin_lock_irqsave(&port->lock, flags);

	xmit->tail += sg_dma_len(&s->sg_tx);
	xmit->tail &= UART_XMIT_SIZE - 1;

	port->icount.tx += sg_dma_len(&s->sg_tx);

	async_tx_ack(s->desc_tx);
	s->desc_tx = NULL;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (!uart_circ_empty(xmit)) {
		s->cookie_tx = 0;
		schedule_work(&s->work_tx);
	} else {
		s->cookie_tx = -EINVAL;
		if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
			u16 ctrl = serial_port_in(port, SCSCR);
			serial_port_out(port, SCSCR, ctrl & ~SCSCR_TIE);
		}
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/* Locking: called with port lock held */
static int sci_dma_rx_push(struct sci_port *s, size_t count)
{
	struct uart_port *port = &s->port;
	struct tty_port *tport = &port->state->port;
	int i, active, room;

	room = tty_buffer_request_room(tport, count);

	if (s->active_rx == s->cookie_rx[0]) {
		active = 0;
	} else if (s->active_rx == s->cookie_rx[1]) {
		active = 1;
	} else {
		dev_err(port->dev, "cookie %d not found!\n", s->active_rx);
		return 0;
	}

	if (room < count)
		dev_warn(port->dev, "Rx overrun: dropping %u bytes\n",
			 count - room);
	if (!room)
		return room;

	for (i = 0; i < room; i++)
		tty_insert_flip_char(tport, ((u8 *)sg_virt(&s->sg_rx[active]))[i],
				     TTY_NORMAL);

	port->icount.rx += room;

	return room;
}

static void sci_dma_rx_complete(void *arg)
{
	struct sci_port *s = arg;
	struct uart_port *port = &s->port;
	unsigned long flags;
	int count;

	dev_dbg(port->dev, "%s(%d) active #%d\n", __func__, port->line, s->active_rx);

	spin_lock_irqsave(&port->lock, flags);

	count = sci_dma_rx_push(s, s->buf_len_rx);

	mod_timer(&s->rx_timer, jiffies + s->rx_timeout);

	spin_unlock_irqrestore(&port->lock, flags);

	if (count)
		tty_flip_buffer_push(&port->state->port);

	schedule_work(&s->work_rx);
}

static void sci_rx_dma_release(struct sci_port *s, bool enable_pio)
{
	struct dma_chan *chan = s->chan_rx;
	struct uart_port *port = &s->port;

	s->chan_rx = NULL;
	s->cookie_rx[0] = s->cookie_rx[1] = -EINVAL;
	dma_release_channel(chan);
	if (sg_dma_address(&s->sg_rx[0]))
		dma_free_coherent(port->dev, s->buf_len_rx * 2,
				  sg_virt(&s->sg_rx[0]), sg_dma_address(&s->sg_rx[0]));
	if (enable_pio)
		sci_start_rx(port);
}

static void sci_tx_dma_release(struct sci_port *s, bool enable_pio)
{
	struct dma_chan *chan = s->chan_tx;
	struct uart_port *port = &s->port;

	s->chan_tx = NULL;
	s->cookie_tx = -EINVAL;
	dma_release_channel(chan);
	if (enable_pio)
		sci_start_tx(port);
}

static void sci_submit_rx(struct sci_port *s)
{
	struct dma_chan *chan = s->chan_rx;
	int i;

	for (i = 0; i < 2; i++) {
		struct scatterlist *sg = &s->sg_rx[i];
		struct dma_async_tx_descriptor *desc;

		desc = dmaengine_prep_slave_sg(chan,
			sg, 1, DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);

		if (desc) {
			s->desc_rx[i] = desc;
			desc->callback = sci_dma_rx_complete;
			desc->callback_param = s;
			s->cookie_rx[i] = desc->tx_submit(desc);
		}

		if (!desc || s->cookie_rx[i] < 0) {
			if (i) {
				async_tx_ack(s->desc_rx[0]);
				s->cookie_rx[0] = -EINVAL;
			}
			if (desc) {
				async_tx_ack(desc);
				s->cookie_rx[i] = -EINVAL;
			}
			dev_warn(s->port.dev,
				 "failed to re-start DMA, using PIO\n");
			sci_rx_dma_release(s, true);
			return;
		}
		dev_dbg(s->port.dev, "%s(): cookie %d to #%d\n", __func__,
			s->cookie_rx[i], i);
	}

	s->active_rx = s->cookie_rx[0];

	dma_async_issue_pending(chan);
}

static void work_fn_rx(struct work_struct *work)
{
	struct sci_port *s = container_of(work, struct sci_port, work_rx);
	struct uart_port *port = &s->port;
	struct dma_async_tx_descriptor *desc;
	int new;

	if (s->active_rx == s->cookie_rx[0]) {
		new = 0;
	} else if (s->active_rx == s->cookie_rx[1]) {
		new = 1;
	} else {
		dev_err(port->dev, "cookie %d not found!\n", s->active_rx);
		return;
	}
	desc = s->desc_rx[new];

	if (dma_async_is_tx_complete(s->chan_rx, s->active_rx, NULL, NULL) !=
	    DMA_SUCCESS) {
		/* Handle incomplete DMA receive */
		struct dma_chan *chan = s->chan_rx;
		struct sh_desc *sh_desc = container_of(desc,
					struct sh_desc, async_tx);
		unsigned long flags;
		int count;

		chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
		dev_dbg(port->dev, "Read %u bytes with cookie %d\n",
			sh_desc->partial, sh_desc->cookie);

		spin_lock_irqsave(&port->lock, flags);
		count = sci_dma_rx_push(s, sh_desc->partial);
		spin_unlock_irqrestore(&port->lock, flags);

		if (count)
			tty_flip_buffer_push(&port->state->port);

		sci_submit_rx(s);

		return;
	}

	s->cookie_rx[new] = desc->tx_submit(desc);
	if (s->cookie_rx[new] < 0) {
		dev_warn(port->dev, "Failed submitting Rx DMA descriptor\n");
		sci_rx_dma_release(s, true);
		return;
	}

	s->active_rx = s->cookie_rx[!new];

	dev_dbg(port->dev, "%s: cookie %d #%d, new active #%d\n", __func__,
		s->cookie_rx[new], new, s->active_rx);
}

static void work_fn_tx(struct work_struct *work)
{
	struct sci_port *s = container_of(work, struct sci_port, work_tx);
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan = s->chan_tx;
	struct uart_port *port = &s->port;
	struct circ_buf *xmit = &port->state->xmit;
	struct scatterlist *sg = &s->sg_tx;

	/*
	 * DMA is idle now.
	 * Port xmit buffer is already mapped, and it is one page... Just adjust
	 * offsets and lengths. Since it is a circular buffer, we have to
	 * transmit till the end, and then the rest. Take the port lock to get a
	 * consistent xmit buffer state.
	 */
	spin_lock_irq(&port->lock);
	sg->offset = xmit->tail & (UART_XMIT_SIZE - 1);
	sg_dma_address(sg) = (sg_dma_address(sg) & ~(UART_XMIT_SIZE - 1)) +
		sg->offset;
	sg_dma_len(sg) = min((int)CIRC_CNT(xmit->head, xmit->tail, UART_XMIT_SIZE),
		CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE));
	spin_unlock_irq(&port->lock);

	BUG_ON(!sg_dma_len(sg));

	desc = dmaengine_prep_slave_sg(chan,
			sg, s->sg_len_tx, DMA_MEM_TO_DEV,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		/* switch to PIO */
		sci_tx_dma_release(s, true);
		return;
	}

	dma_sync_sg_for_device(port->dev, sg, 1, DMA_TO_DEVICE);

	spin_lock_irq(&port->lock);
	s->desc_tx = desc;
	desc->callback = sci_dma_tx_complete;
	desc->callback_param = s;
	spin_unlock_irq(&port->lock);
	s->cookie_tx = desc->tx_submit(desc);
	if (s->cookie_tx < 0) {
		dev_warn(port->dev, "Failed submitting Tx DMA descriptor\n");
		/* switch to PIO */
		sci_tx_dma_release(s, true);
		return;
	}

	dev_dbg(port->dev, "%s: %p: %d...%d, cookie %d\n", __func__,
		xmit->buf, xmit->tail, xmit->head, s->cookie_tx);

	dma_async_issue_pending(chan);
}
#endif

static void sci_start_tx(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	unsigned short ctrl;

	/* Wake BT chip before sending a frame */
	if (s->cfg->exit_lpm_cb)
		s->cfg->exit_lpm_cb(port);
#ifdef CONFIG_SERIAL_SH_SCI_DMA
	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
		u16 new, scr = serial_port_in(port, SCSCR);
		if (s->chan_tx)
			new = scr | 0x8000;
		else
			new = scr & ~0x8000;
		if (new != scr)
			serial_port_out(port, SCSCR, new);
	}

	if (s->chan_tx && !uart_circ_empty(&s->port.state->xmit) &&
	    s->cookie_tx < 0) {
		s->cookie_tx = 0;
		schedule_work(&s->work_tx);
	}
#endif

	if (!s->chan_tx || port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
		/* Set TIE (Transmit Interrupt Enable) bit in SCSCR */
		ctrl = serial_port_in(port, SCSCR);
		serial_port_out(port, SCSCR, ctrl | SCSCR_TIE);
	}
}

static void sci_stop_tx(struct uart_port *port)
{
	unsigned short ctrl;

	/* Clear TIE (Transmit Interrupt Enable) bit in SCSCR */
	ctrl = serial_port_in(port, SCSCR);

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
		ctrl &= ~0x8000;

	ctrl &= ~SCSCR_TIE;

	serial_port_out(port, SCSCR, ctrl);
}

static void sci_start_rx(struct uart_port *port)
{
	unsigned short ctrl;

	ctrl = serial_port_in(port, SCSCR) | port_rx_irq_mask(port);

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
		ctrl &= ~0x4000;

	serial_port_out(port, SCSCR, ctrl);
}

static void sci_stop_rx(struct uart_port *port)
{
	unsigned short ctrl;

	ctrl = serial_port_in(port, SCSCR);

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
		ctrl &= ~0x4000;

	ctrl &= ~port_rx_irq_mask(port);

	serial_port_out(port, SCSCR, ctrl);
}

static void sci_enable_ms(struct uart_port *port)
{
	/*
	 * Not supported by hardware, always a nop.
	 */
}

static void sci_break_ctl(struct uart_port *port, int break_state)
{
	struct sci_port *s = to_sci_port(port);
	struct plat_sci_reg *reg = sci_regmap[s->cfg->regtype] + SCSPTR;
	unsigned short scscr, scsptr;

	/* check wheter the port has SCSPTR */
	if (!reg->size) {
		/*
		 * Not supported by hardware. Most parts couple break and rx
		 * interrupts together, with break detection always enabled.
		 */
		return;
	}

	scsptr = serial_port_in(port, SCSPTR);
	scscr = serial_port_in(port, SCSCR);

	if (break_state == -1) {
		scsptr = (scsptr | SCSPTR_SPB2IO) & ~SCSPTR_SPB2DT;
		scscr &= ~SCSCR_TE;
	} else {
		scsptr = (scsptr | SCSPTR_SPB2DT) & ~SCSPTR_SPB2IO;
		scscr |= SCSCR_TE;
	}

	serial_port_out(port, SCSPTR, scsptr);
	serial_port_out(port, SCSCR, scscr);
}

#ifdef CONFIG_SERIAL_SH_SCI_DMA
static bool filter(struct dma_chan *chan, void *slave)
{
	struct sh_dmae_slave *param = slave;

	dev_dbg(chan->device->dev, "%s: slave ID %d\n", __func__,
		param->slave_id);

	chan->private = slave;
	return true;
}

static void rx_timer_fn(unsigned long arg)
{
	struct sci_port *s = (struct sci_port *)arg;
	struct uart_port *port = &s->port;
	u16 scr = serial_port_in(port, SCSCR);

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
		scr &= ~0x4000;
		enable_irq(s->cfg->irqs[1]);
	}
	serial_port_out(port, SCSCR, scr | SCSCR_RIE);
	dev_dbg(port->dev, "DMA Rx timed out\n");
	schedule_work(&s->work_rx);
}

static void sci_request_dma(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	struct sh_dmae_slave *param;
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	int nent;

	dev_dbg(port->dev, "%s: port %d\n", __func__,
		port->line);

	if (s->cfg->dma_slave_tx <= 0 || s->cfg->dma_slave_rx <= 0)
		return;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	param = &s->param_tx;

	/* Slave ID, e.g., SHDMA_SLAVE_SCIF0_TX */
	param->slave_id = s->cfg->dma_slave_tx;

	s->cookie_tx = -EINVAL;
	chan = dma_request_channel(mask, filter, param);
	dev_dbg(port->dev, "%s: TX: got channel %p\n", __func__, chan);
	if (chan) {
		s->chan_tx = chan;
		sg_init_table(&s->sg_tx, 1);
		/* UART circular tx buffer is an aligned page. */
		BUG_ON((int)port->state->xmit.buf & ~PAGE_MASK);
		sg_set_page(&s->sg_tx, virt_to_page(port->state->xmit.buf),
			    UART_XMIT_SIZE, (int)port->state->xmit.buf & ~PAGE_MASK);
		nent = dma_map_sg(port->dev, &s->sg_tx, 1, DMA_TO_DEVICE);
		if (!nent)
			sci_tx_dma_release(s, false);
		else
			dev_dbg(port->dev, "%s: mapped %d@%p to %x\n", __func__,
				sg_dma_len(&s->sg_tx),
				port->state->xmit.buf, sg_dma_address(&s->sg_tx));

		s->sg_len_tx = nent;

		INIT_WORK(&s->work_tx, work_fn_tx);
	}

	param = &s->param_rx;

	/* Slave ID, e.g., SHDMA_SLAVE_SCIF0_RX */
	param->slave_id = s->cfg->dma_slave_rx;

	chan = dma_request_channel(mask, filter, param);
	dev_dbg(port->dev, "%s: RX: got channel %p\n", __func__, chan);
	if (chan) {
		dma_addr_t dma[2];
		void *buf[2];
		int i;

		s->chan_rx = chan;

		s->buf_len_rx = 2 * max(16, (int)port->fifosize);
		buf[0] = dma_alloc_coherent(NULL, s->buf_len_rx * 2,
					    &dma[0], GFP_KERNEL);

		if (!buf[0]) {
			dev_warn(port->dev,
				 "failed to allocate dma buffer, using PIO\n");
			sci_rx_dma_release(s, true);
			return;
		}

		buf[1] = buf[0] + s->buf_len_rx;
		dma[1] = dma[0] + s->buf_len_rx;

		for (i = 0; i < 2; i++) {
			struct scatterlist *sg = &s->sg_rx[i];

			sg_init_table(sg, 1);
			sg_set_page(sg, virt_to_page(buf[i]), s->buf_len_rx,
				    (int)buf[i] & ~PAGE_MASK);
			sg_dma_address(sg) = dma[i];
		}

		INIT_WORK(&s->work_rx, work_fn_rx);
		setup_timer(&s->rx_timer, rx_timer_fn, (unsigned long)s);

		sci_submit_rx(s);
	}
}

static void sci_free_dma(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);

	if (s->cfg->dma_slave_tx <= 0 || s->cfg->dma_slave_rx <= 0)
		return;

	del_timer_sync(&s->rx_timer);

	if (s->chan_tx) {
		cancel_work_sync(&s->work_tx);
		sci_tx_dma_release(s, false);
	}
	if (s->chan_rx) {
		cancel_work_sync(&s->work_rx);
		sci_rx_dma_release(s, false);
	}
}
#else
static inline void sci_request_dma(struct uart_port *port)
{
}

static inline void sci_free_dma(struct uart_port *port)
{
}
#endif

static int sci_startup(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	unsigned long flags;
	int ret;

	dev_dbg(port->dev, "%s(%d)\n", __func__, port->line);

	ret = sci_request_irq(s);
	if (unlikely(ret < 0))
		return ret;

	sci_request_dma(port);

	spin_lock_irqsave(&port->lock, flags);
	sci_start_tx(port);
	sci_start_rx(port);
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void sci_shutdown(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	unsigned long flags;

	dev_dbg(port->dev, "%s(%d)\n", __func__, port->line);

	spin_lock_irqsave(&port->lock, flags);
	sci_stop_rx(port);
	sci_stop_tx(port);
	spin_unlock_irqrestore(&port->lock, flags);

	sci_free_dma(port);
	sci_free_irq(s);
}

static unsigned int sci_scbrr_calc(unsigned int algo_id, unsigned int bps,
				   unsigned long freq)
{
	switch (algo_id) {
	case SCBRR_ALGO_1:
		return ((freq + 16 * bps) / (16 * bps) - 1);
	case SCBRR_ALGO_2:
		return ((freq + 16 * bps) / (32 * bps) - 1);
	case SCBRR_ALGO_3:
		return (((freq * 2) + 16 * bps) / (16 * bps) - 1);
	case SCBRR_ALGO_4:
		return (((freq * 2) + 16 * bps) / (32 * bps) - 1);
	case SCBRR_ALGO_4_BIS:
		return (((freq * 2) + 13 * bps) / (26 * bps) - 1);
	case SCBRR_ALGO_5:
		return (((freq * 1000 / 32) / bps) - 1);
	}

	/* Warn, but use a safe default */
	WARN_ON(1);

	return ((freq + 16 * bps) / (32 * bps) - 1);
}

static int sci_reset(struct uart_port *port)
{
	struct plat_sci_reg *reg;
	unsigned int status;
	int idx = 0;

	/* JIRA ID 1911 (PCP# JK12122071879).
	Propagation PCP# VA13022823537
	TEND bit in SCxSR register is a read only bit
	set by hardware on transmission end.
	At times, when there is no ACK from GPS chip for
	previous transmission, infinite busy loop is observed for
	SCIFB1 port as TEND bit is NOT read as 1 for long time.
	Possible solution to avoid busy loop.
	Note:
	1. TEND bit is always read as 1 on first attempt and
	so read attempt is limited to 100 attempts.
	2. SCIFB port could be reset to read TEND bit as 1.
	However, due to lack of reproduciblity, this cannot be verified.
	3. TTY framework does not permit to return error code
	on this failure case.
	*/
	do {
		idx++;
		status = serial_port_in(port, SCxSR);
		if (idx > SCIFB1_SCxSR_TEND_BIT_READ_ATTEMPT)
			break;
	} while (!(status & SCxSR_TEND(port)));

	if (idx > SCIFB1_SCxSR_TEND_BIT_READ_ATTEMPT) {
		dev_dbg(port->dev, \
			"%s(%d) Possible busy loop, exit gracefully\n", \
					__func__, port->line) ;
		return -EPERM ;
	}
	serial_port_out(port, SCSCR, 0x00);	/* TE=0, RE=0, CKE1=0 */

	reg = sci_getreg(port, SCFCR);
	if (reg->size)
		serial_port_out(port, SCFCR, SCFCR_RFRST | SCFCR_TFRST);
	return 0;
}

static void sci_set_termios(struct uart_port *port, struct ktermios *termios,
			    struct ktermios *old)
{
	struct sci_port *s = to_sci_port(port);
	struct plat_sci_reg *reg;
	unsigned int baud, smr_val, max_baud, cks;
	int t = -1;
	int ret = 0;
	unsigned long flags = 0 ;

	/* PCP# TT12102320062 - Save & retain termios->c_cflag value
	for TTY close use case.	This change is applicable only for
	SCIFA port to retain all register values as C4 power area
	might get turned OFF and reset register values
	in TTY close use case */
	if (sci_uart_console(port) && s && termios) {
		if (termios->c_cflag == 0)
			termios->c_cflag = s->cons_cflag ;
		else
			s->cons_cflag = termios->c_cflag;
	}
	/*
	 * earlyprintk comes here early on with port->uartclk set to zero.
	 * the clock framework is not up and running at this point so here
	 * we assume that 115200 is the maximum baud rate. please note that
	 * the baud rate is not programmed during earlyprintk - it is assumed
	 * that the previous boot loader has enabled required clocks and
	 * setup the baud rate generator hardware for us already.
	 */
	max_baud = port->uartclk ? port->uartclk / 16 : 115200;

	baud = uart_get_baud_rate(port, termios, old, 0, max_baud);
	if (likely(baud && port->uartclk))
		t = sci_scbrr_calc(s->cfg->scbrr_algo_id, baud, port->uartclk);

	sci_port_enable(s);
	/* Propagation PCP# VA13022823537 -
	To avoid possible race condition occurrence between
	sci_mpxed_interrupt() & serial_console_write()/sci_set_termios
	*/
	spin_lock_irqsave(&port->lock, flags);

	ret = sci_reset(port);
	if (ret < 0) {
		/* spin_unlock_* should be invoked here as spin_lock_*
		is invoked before for PCP# JK13012548863 */
		spin_unlock_irqrestore(&port->lock, flags);
		sci_port_disable(s);
		return ;
	}
	smr_val = serial_port_in(port, SCSMR) & 3;

	if ((termios->c_cflag & CSIZE) == CS7)
		smr_val |= 0x40;
	if (termios->c_cflag & PARENB)
		smr_val |= 0x20;
	if (termios->c_cflag & PARODD)
		smr_val |= 0x30;
	if (termios->c_cflag & CSTOPB)
		smr_val |= 0x08;

	if(s->cfg->scbrr_algo_id == SCBRR_ALGO_4_BIS)
	{
		smr_val &= ~(7 << 8);
		smr_val |= 4 << 8;
	}

	uart_update_timeout(port, termios->c_cflag, baud);

	for (cks = 0; t >= 256 && cks <= 3; cks++)
		t >>= 2;

	dev_dbg(port->dev, "%s: SMR %x, cks %x, t %x, SCSCR %x\n",
		__func__, smr_val, cks, t, s->cfg->scscr);

	if (t >= 0) {
		serial_port_out(port, SCSMR, (smr_val & ~3) | cks);
		serial_port_out(port, SCBRR, t);
		udelay((1000000+(baud-1)) / baud); /* Wait one bit interval */
	} else
		serial_port_out(port, SCSMR, smr_val);

	sci_init_pins(port, termios->c_cflag);

	reg = sci_getreg(port, SCFCR);
	if (reg->size) {
		unsigned short ctrl = serial_port_in(port, SCFCR);

		if (s->cfg->capabilities & SCIx_HAVE_RTSCTS) {
			/* Set rts_ctrl to control RTS pin in
			suspend & resume only when CRTSCTS is set */
			if (termios->c_cflag & CRTSCTS) {
				ctrl |= SCFCR_MCE;
				s->cfg->rts_ctrl = 1 ;
			}
			else
				ctrl &= ~SCFCR_MCE;
		}

		/*
		 * As we've done a sci_reset() above, ensure we don't
		 * interfere with the FIFOs while toggling MCE. As the
		 * reset values could still be set, simply mask them out.
		 */
		ctrl &= ~(SCFCR_RFRST | SCFCR_TFRST);

		serial_port_out(port, SCFCR, ctrl);
	}

	serial_port_out(port, SCSCR, s->cfg->scscr);

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	/*
	 * Calculate delay for 1.5 DMA buffers: see
	 * drivers/serial/serial_core.c::uart_update_timeout(). With 10 bits
	 * (CS8), 250Hz, 115200 baud and 64 bytes FIFO, the above function
	 * calculates 1 jiffie for the data plus 5 jiffies for the "slop(e)."
	 * Then below we calculate 3 jiffies (12ms) for 1.5 DMA buffers (3 FIFO
	 * sizes), but it has been found out experimentally, that this is not
	 * enough: the driver too often needlessly runs on a DMA timeout. 20ms
	 * as a minimum seem to work perfectly.
	 */
	if (s->chan_rx) {
		s->rx_timeout = (port->timeout - HZ / 50) * s->buf_len_rx * 3 /
			port->fifosize / 2;
		dev_dbg(port->dev,
			"DMA Rx t-out %ums, tty t-out %u jiffies\n",
			s->rx_timeout * 1000 / HZ, port->timeout);
		if (s->rx_timeout < msecs_to_jiffies(20))
			s->rx_timeout = msecs_to_jiffies(20);
	}
#endif

	if ((termios->c_cflag & CREAD) != 0)
		sci_start_rx(port);
	spin_unlock_irqrestore(&port->lock, flags);

	sci_port_disable(s);
}

static void sci_pm(struct uart_port *port, unsigned int state,
		   unsigned int oldstate)
{
	struct sci_port *sci_port = to_sci_port(port);

	switch (state) {
	case 3:
		sci_port_disable(sci_port);
		if (port->dev)
			pinctrl_pm_select_sleep_state(port->dev);
		break;
	default:
		if (port->dev)
			pinctrl_pm_select_default_state(port->dev);
		sci_port_enable(sci_port);
		break;
	}
}

static const char *sci_type(struct uart_port *port)
{
	switch (port->type) {
	case PORT_IRDA:
		return "irda";
	case PORT_SCI:
		return "sci";
	case PORT_SCIF:
		return "scif";
	case PORT_SCIFA:
		return "scifa";
	case PORT_SCIFB:
		return "scifb";
	}

	return NULL;
}

static inline unsigned long sci_port_size(struct uart_port *port)
{
	/*
	 * Pick an arbitrary size that encapsulates all of the base
	 * registers by default. This can be optimized later, or derived
	 * from platform resource data at such a time that ports begin to
	 * behave more erratically.
	 */
	return 64;
}

static int sci_remap_port(struct uart_port *port)
{
	unsigned long size = sci_port_size(port);

	/*
	 * Nothing to do if there's already an established membase.
	 */
	if (port->membase)
		return 0;

	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap_nocache(port->mapbase, size);
		if (unlikely(!port->membase)) {
			dev_err(port->dev, "can't remap port#%d\n", port->line);
			return -ENXIO;
		}
	} else {
		/*
		 * For the simple (and majority of) cases where we don't
		 * need to do any remapping, just cast the cookie
		 * directly.
		 */
		port->membase = (void __iomem *)port->mapbase;
	}

	return 0;
}

static void sci_release_port(struct uart_port *port)
{
	if (port->flags & UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}

	release_mem_region(port->mapbase, sci_port_size(port));
}

static int sci_request_port(struct uart_port *port)
{
	unsigned long size = sci_port_size(port);
	struct resource *res;
	int ret;

	res = request_mem_region(port->mapbase, size, dev_name(port->dev));
	if (unlikely(res == NULL))
		return -EBUSY;

	ret = sci_remap_port(port);
	if (unlikely(ret != 0)) {
		release_resource(res);
		return ret;
	}

	return 0;
}

static void sci_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		struct sci_port *sport = to_sci_port(port);

		port->type = sport->cfg->type;
		sci_request_port(port);
	}
}

static int sci_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	struct sci_port *s = to_sci_port(port);

	if (ser->irq != s->cfg->irqs[SCIx_TXI_IRQ] || ser->irq > nr_irqs)
		return -EINVAL;
	if (ser->baud_base < 2400)
		/* No paper tape reader for Mitch.. */
		return -EINVAL;

	return 0;
}

static struct uart_ops sci_uart_ops = {
	.tx_empty	= sci_tx_empty,
	.set_mctrl	= sci_set_mctrl,
	.get_mctrl	= sci_get_mctrl,
	.start_tx	= sci_start_tx,
	.stop_tx	= sci_stop_tx,
	.stop_rx	= sci_stop_rx,
	.enable_ms	= sci_enable_ms,
	.break_ctl	= sci_break_ctl,
	.startup	= sci_startup,
	.shutdown	= sci_shutdown,
	.set_termios	= sci_set_termios,
	.pm		= sci_pm,
	.type		= sci_type,
	.release_port	= sci_release_port,
	.request_port	= sci_request_port,
	.config_port	= sci_config_port,
	.verify_port	= sci_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= sci_poll_get_char,
	.poll_put_char	= sci_poll_put_char,
#endif
};

static int sci_init_single(struct platform_device *dev,
				     struct sci_port *sci_port,
				     unsigned int index,
				     struct plat_sci_port *p)
{
	struct uart_port *port = &sci_port->port;
	int ret;

	sci_port->cfg	= p;

	port->ops	= &sci_uart_ops;
	port->iotype	= UPIO_MEM;
	port->line	= index;

	switch (p->type) {
	case PORT_SCIFB:
		port->fifosize = 256;
		break;
	case PORT_SCIFA:
		port->fifosize = 64;
		break;
	case PORT_SCIF:
		port->fifosize = 16;
		break;
	default:
		port->fifosize = 1;
		break;
	}

	if (p->regtype == SCIx_PROBE_REGTYPE) {
		ret = sci_probe_regmap(p);
		if (unlikely(ret))
			return ret;
	}

	if (dev) {
		sci_port->iclk = clk_get(&dev->dev, "sci_ick");
		if (IS_ERR(sci_port->iclk)) {
			sci_port->iclk = clk_get(&dev->dev, "peripheral_clk");
			if (IS_ERR(sci_port->iclk)) {
				dev_err(&dev->dev, "can't get iclk\n");
				return PTR_ERR(sci_port->iclk);
			}
		}

		/*
		 * The function clock is optional, ignore it if we can't
		 * find it.
		 */
		sci_port->fclk = clk_get(&dev->dev, "sci_fck");
		if (IS_ERR(sci_port->fclk))
			sci_port->fclk = NULL;

		port->dev = &dev->dev;

		sci_init_gpios(sci_port);

		pm_runtime_enable(&dev->dev);
	}

	sci_port->break_timer.data = (unsigned long)sci_port;
	sci_port->break_timer.function = sci_break_timer;
	init_timer(&sci_port->break_timer);

	/*
	 * Establish some sensible defaults for the error detection.
	 */
	if (!p->error_mask)
		p->error_mask = (p->type == PORT_SCI) ?
			SCI_DEFAULT_ERROR_MASK : SCIF_DEFAULT_ERROR_MASK;

	/*
	 * Establish sensible defaults for the overrun detection, unless
	 * the part has explicitly disabled support for it.
	 */
	if (p->overrun_bit != SCIx_NOT_SUPPORTED) {
		if (p->type == PORT_SCI)
			p->overrun_bit = 5;
		else if (p->scbrr_algo_id == SCBRR_ALGO_4)
			p->overrun_bit = 9;
		else
			p->overrun_bit = 0;

		/*
		 * Make the error mask inclusive of overrun detection, if
		 * supported.
		 */
		p->error_mask |= (1 << p->overrun_bit);
	}

	port->mapbase		= p->mapbase;
	port->type		= p->type;
	port->flags		= p->flags;
	port->regshift		= p->regshift;

	/*
	 * The UART port needs an IRQ value, so we peg this to the RX IRQ
	 * for the multi-IRQ ports, which is where we are primarily
	 * concerned with the shutdown path synchronization.
	 *
	 * For the muxed case there's nothing more to do.
	 */
	port->irq		= p->irqs[SCIx_RXI_IRQ];
	port->irqflags		= 0;

	port->serial_in		= sci_serial_in;
	port->serial_out	= sci_serial_out;

	if (p->dma_slave_tx > 0 && p->dma_slave_rx > 0)
		dev_dbg(port->dev, "DMA tx %d, rx %d\n",
			p->dma_slave_tx, p->dma_slave_rx);

	return 0;
}

static void sci_cleanup_single(struct sci_port *port)
{
	sci_free_gpios(port);

	clk_put(port->iclk);
	clk_put(port->fclk);

	pm_runtime_disable(port->port.dev);
}

#ifdef CONFIG_SERIAL_SH_SCI_CONSOLE
static void serial_console_putchar(struct uart_port *port, int ch)
{
	sci_poll_put_char(port, ch);
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 */
static void serial_console_write(struct console *co, const char *s,
				 unsigned count)
{
if (get_kernel_log_status()) {
	struct sci_port *sci_port = &sci_ports[co->index];
	struct uart_port *port = &sci_port->port;
	unsigned short bits, ctrl;
	unsigned long flags;
	int locked = 1;
	unsigned long timeout;
	bool tout = 0;

	touch_nmi_watchdog();

	local_irq_save(flags);
	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&port->lock);
	else
		spin_lock(&port->lock);

	/* first save the SCSCR then disable the interrupts */
	ctrl = serial_port_in(port, SCSCR);
	serial_port_out(port, SCSCR, sci_port->cfg->scscr);

	uart_console_write(port, s, count, serial_console_putchar);

	/* wait until fifo is empty and last bit has been transmitted */
	bits = SCxSR_TDxE(port) | SCxSR_TEND(port);

	timeout = (__raw_readl(cmt10_virt) + SCI_STATUS_BIT_POLL_TIMEOUT);

	/* There are some scenerios where one of the cpu gets stuck
	 * at serial_in and wdt overflow happens.So add a timeout to find out
	 * if there is any bug in updating status bits */
	while (((serial_port_in(port, SCxSR) & bits) != bits) &&
		(tout = time_after(((unsigned long)__raw_readl(cmt10_virt)),
								timeout)))
		cpu_relax();
	BUG_ON(tout);

	/* restore the SCSCR */
	serial_port_out(port, SCSCR, ctrl);

	if (locked)
		spin_unlock(&port->lock);
	local_irq_restore(flags);
	}
}

static int serial_console_setup(struct console *co, char *options)
{
	struct sci_port *sci_port;
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	/*
	 * Refuse to handle any bogus ports.
	 */
	if (co->index < 0 || co->index >= SCI_NPORTS)
		return -ENODEV;

	sci_port = &sci_ports[co->index];
	port = &sci_port->port;

	/*
	 * Refuse to handle uninitialized ports.
	 */
	if (!port->ops)
		return -ENODEV;

	ret = sci_remap_port(port);
	if (unlikely(ret != 0))
		return ret;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	ret = uart_set_options(port, co, baud, parity, bits, flow);
	/* PCP# TT12102320062 - Save & retain termios->c_cflag
	value for TTY close use case */
	if (sci_port && port->cons && port->cons->cflag != 0)
		sci_port->cons_cflag = port->cons->cflag ;
	return ret;
}

static struct console serial_console = {
	.name		= "ttySC",
	.device		= uart_console_device,
	.write		= serial_console_write,
	.setup		= serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sci_uart_driver,
};

static struct console early_serial_console = {
	.name           = "early_ttySC",
	.write          = serial_console_write,
	.flags          = CON_PRINTBUFFER,
	.index		= -1,
};

static char early_serial_buf[32];

static int sci_probe_earlyprintk(struct platform_device *pdev)
{
	struct plat_sci_port *cfg = pdev->dev.platform_data;

	if (early_serial_console.data)
		return -EEXIST;

	early_serial_console.index = pdev->id;

	sci_init_single(NULL, &sci_ports[pdev->id], pdev->id, cfg);

	serial_console_setup(&early_serial_console, early_serial_buf);

	if (!strstr(early_serial_buf, "keep"))
		early_serial_console.flags |= CON_BOOT;

	register_console(&early_serial_console);
	return 0;
}

#define SCI_CONSOLE	(&serial_console)

#else
static inline int sci_probe_earlyprintk(struct platform_device *pdev)
{
	return -EINVAL;
}

#define SCI_CONSOLE	NULL

#endif /* CONFIG_SERIAL_SH_SCI_CONSOLE */

static char banner[] __initdata =
	KERN_INFO "SuperH SCI(F) driver initialized\n";

static struct uart_driver sci_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "sci",
	.dev_name	= "ttySC",
	.major		= SCI_MAJOR,
	.minor		= SCI_MINOR_START,
	.nr		= SCI_NPORTS,
	.cons		= SCI_CONSOLE,
};

static int sci_remove(struct platform_device *dev)
{
	struct sci_port *port = platform_get_drvdata(dev);

	cpufreq_unregister_notifier(&port->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);

	uart_remove_one_port(&sci_uart_driver, &port->port);

	sci_cleanup_single(port);

	if (cmt10_virt)
		iounmap(cmt10_virt);

	return 0;
}

static int sci_probe_single(struct platform_device *dev,
				      unsigned int index,
				      struct plat_sci_port *p,
				      struct sci_port *sciport)
{
	int ret;

	/* Sanity check */
	if (unlikely(index >= SCI_NPORTS)) {
		dev_notice(&dev->dev, "Attempting to register port "
			   "%d when only %d are available.\n",
			   index+1, SCI_NPORTS);
		dev_notice(&dev->dev, "Consider bumping "
			   "CONFIG_SERIAL_SH_SCI_NR_UARTS!\n");
		return -EINVAL;
	}

	ret = sci_init_single(dev, sciport, index, p);
	if (ret)
		return ret;

	ret = uart_add_one_port(&sci_uart_driver, &sciport->port);
	if (ret) {
		sci_cleanup_single(sciport);
		return ret;
	}

	return 0;
}

static int sci_probe(struct platform_device *dev)
{
	struct plat_sci_port *p = dev->dev.platform_data;
	struct sci_port *sp = &sci_ports[dev->id];
	int ret;

	cmt10_virt = ioremap_nocache(CMCNT0, 4);
	if (!cmt10_virt) {
		dev_err(&dev->dev, "ioremap failed for CMCNT0\n");
		return -ENXIO;
	}

	/*
	 * If we've come here via earlyprintk initialization, head off to
	 * the special early probe. We don't have sufficient device state
	 * to make it beyond this yet.
	 */
	if (is_early_platform_device(dev))
		return sci_probe_earlyprintk(dev);

	platform_set_drvdata(dev, sp);

	ret = sci_probe_single(dev, dev->id, p, sp);
	if (ret)
		return ret;

	sp->freq_transition.notifier_call = sci_notifier;

	ret = cpufreq_register_notifier(&sp->freq_transition,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (unlikely(ret < 0)) {
		sci_cleanup_single(sp);
		return ret;
	}

#ifdef CONFIG_SH_STANDARD_BIOS
	sh_bios_gdb_detach();
#endif

	return 0;
}

static int sci_suspend(struct device *dev)
{
	struct sci_port *sport = dev_get_drvdata(dev);
	struct uart_port *port = &sport->port;
	u16 data = 0 ;

	/* GPIO settings review comments for PCP# NK12120730341 */
	if (sport && sport->cfg) {
		if (sport->cfg->rts_ctrl) {
			sci_port_enable(sport);

			/* Set RTS to high before going in deep sleep mode */
			data = serial_port_in(port, SCPCR);
			serial_port_out(port, SCPCR,  data | 0x0010);
			data = serial_port_in(port, SCPDR);
			serial_port_out(port, SCPDR,  data | 0x0010);

			sci_port_disable(sport);
		}
		uart_suspend_port(&sci_uart_driver, &sport->port);
	}
	return 0;
}

static int sci_resume(struct device *dev)
{
	struct sci_port *sport = dev_get_drvdata(dev);
	struct uart_port *port = &sport->port;
	u16 data = 0 ;

	/* GPIO settings review comments for PCP# NK12120730341 */
	if (sport && sport->cfg) {
		uart_resume_port(&sci_uart_driver, &sport->port);
		if (sport->cfg->rts_ctrl) {
			sci_port_enable(sport);

			/* Set RTS to low after the resume */
			data = serial_port_in(port, SCPCR);
			serial_port_out(port, SCPCR,  data & 0xFFEF);
			data = serial_port_in(port, SCPDR);
			serial_port_out(port, SCPDR,  data & 0xFFEF);

			sci_port_disable(sport);
		}
	}
	return 0;
}

static const struct dev_pm_ops sci_dev_pm_ops = {
	.suspend	= sci_suspend,
	.resume		= sci_resume,
};

static struct platform_driver sci_driver = {
	.probe		= sci_probe,
	.remove		= sci_remove,
	.driver		= {
		.name	= "sh-sci",
		.owner	= THIS_MODULE,
		.pm	= &sci_dev_pm_ops,
	},
};

static int __init sci_init(void)
{
	int ret;
	printk(banner);

	ret = uart_register_driver(&sci_uart_driver);
	if (likely(ret == 0)) {
		ret = platform_driver_register(&sci_driver);
		if (unlikely(ret))
			uart_unregister_driver(&sci_uart_driver);
	}

	return ret;
}

static void __exit sci_exit(void)
{
	platform_driver_unregister(&sci_driver);
	uart_unregister_driver(&sci_uart_driver);
}

#ifdef CONFIG_SERIAL_SH_SCI_CONSOLE
early_platform_init_buffer("earlyprintk", &sci_driver,
			   early_serial_buf, ARRAY_SIZE(early_serial_buf));
#endif
subsys_initcall(sci_init);
module_exit(sci_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sh-sci");
MODULE_AUTHOR("Paul Mundt");
MODULE_DESCRIPTION("SuperH SCI(F) serial driver");
