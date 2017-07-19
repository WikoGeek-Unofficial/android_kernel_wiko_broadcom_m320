#ifndef __ASM_ARCH_VIBRATOR_H
#define __ASM_ARCH_VIBRATOR_H

#include <linux/device.h>
#include <linux/mutex.h>

#define DEFAULT_VIB_VOLTAGE 3000000

void u2_vibrator_init(void);

struct platform_ss_vibrator_data {
	const char *regulator_id;
	int voltage;
	int gpio;
};
extern struct platform_ss_vibrator_data ss_vibrator_data;

#if defined(CONFIG_VIBRATOR)
void __init u2_add_vibrator_device(void);
#else
static inline void u2_add_vibrator_device(void) {}
#endif

#endif /* __ASM_ARCH_VIBRATOR_H */
