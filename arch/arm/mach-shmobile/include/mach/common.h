#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

#include <mach/hardware.h>

extern unsigned int shmobile_rev(void);

#define U2_PRODUCT		0x00003E00 /* aka R8A7373 */
#define U2_VERSION_1_0		0x00003E00
#define U2_VERSION_2_0		0x00003E10
#define U2_VERSION_2_1		0x00003E11
#define U2_VERSION_2_2		0x00003E12
#define U2_VERSION_2_3		0x00003E13

#define APE6_PRODUCT		0x00003F00 /* aka R8A73A4 */

#define U3_PRODUCT		0x00004400 /* aka R8A73724 */

#define U2B_PRODUCT		0x00004D00 /* aka R8A73A7 */

#define shmobile_product() (shmobile_rev() & CCCR_PRODUCT_MASK)
#define shmobile_is_u2() (shmobile_product() == U2_PRODUCT)
#define shmobile_is_u3() (shmobile_product() == U3_PRODUCT)
#define shmobile_is_ape6() (shmobile_product() == APE6_PRODUCT)
#define shmobile_is_u2b() (shmobile_product() == U2B_PRODUCT)

/* Handy test for old versions of a specific product that we need some sort
 * of workaround for - better than doing a straight "if (shmobile_rev() < x)".
 *
 * Returns true if we're on an older version of the specified product than
 * the specified version.
 * Returns false if we're on the specified version or newer, or it's a
 * different product.
 */
static inline int shmobile_is_older(unsigned int than)
{
	unsigned int rev = shmobile_rev();
	return rev < than
		&& (rev & CCCR_PRODUCT_MASK) == (than & CCCR_PRODUCT_MASK);
}

extern void shmobile_earlytimer_init(void);
extern void shmobile_timer_init(void);
extern void shmobile_setup_delay(unsigned int max_cpu_core_mhz,
			 unsigned int mult, unsigned int div);
struct twd_local_timer;
extern void shmobile_setup_console(void);
extern void shmobile_secondary_vector(void);
extern void shmobile_secondary_vector_scu(void);
extern void shmobile_invalidate_start(void);
#ifdef CONFIG_SMP
extern void __init shmobile_dt_smp_map_io(void);
#else
static void __init shmobile_dt_smp_map_io(void)
{
}
#endif
struct clk;
extern int shmobile_clk_init(void);
extern void shmobile_handle_irq_intc(struct pt_regs *);
extern struct platform_suspend_ops shmobile_suspend_ops;
struct cpuidle_driver;
extern void shmobile_cpuidle_set_driver(struct cpuidle_driver *drv);

#ifdef CONFIG_SUSPEND
int shmobile_suspend_init(void);
#else
static inline int shmobile_suspend_init(void) { return 0; }
#endif

#ifdef CONFIG_CPU_IDLE
int shmobile_cpuidle_init(void);
#else
static inline int shmobile_cpuidle_init(void) { return 0; }
#endif

extern void __iomem *shmobile_scu_base;
extern void shmobile_smp_init_cpus(unsigned int ncores);

static inline void __init shmobile_init_late(void)
{
	shmobile_suspend_init();
	shmobile_cpuidle_init();
}

extern void sh_modify_register8(void __iomem *, u8, u8);
extern void sh_modify_register16(void __iomem *, u16, u16);
extern void sh_modify_register32(void __iomem *, u32, u32);

extern void cmt10_start(bool clear);
extern void cmt10_stop(void);

/* A temporary solution until everybody fixes their dependencies on
 * system_rev */
typedef enum {
	BOARD_REV_000 = 0x000,       /* Rev0.00 */
	BOARD_REV_002 = 0x002,       /* Rev0.02 */
	BOARD_REV_003 = 0x003,       /* Rev0.03 */
	BOARD_REV_010 = 0x010,       /* Rev0.10 */
	BOARD_REV_012 = 0x012,       /* Rev0.12 */
	BOARD_REV_013 = 0x013,       /* Rev0.13 */
	BOARD_REV_020 = 0x020,       /* Rev0.20 */
	BOARD_REV_030 = 0x030,       /* Rev0.30 */
	BOARD_REV_040 = 0x040,       /* Rev0.40 */
	BOARD_REV_041 = 0x041,       /* Rev0.41 */
	BOARD_REV_050 = 0x050,       /* Rev0.50 */
	BOARD_REV_060 = 0x060,       /* Rev0.60 */
	BOARD_REV_110 = 0x110,       /* Rev1.10 */
	BOARD_REV_130 = 0x130,       /* Rev1.30 */
	BOARD_REV_131 = 0x131,       /* Rev1.31 */
} BOARD_REV;

#define PRINT_HW_REV(REV) pr_info("%s HW rev:%x.%02x\n", __FILE__, REV >> 8, REV);

#endif /* __ARCH_MACH_COMMON_H */
