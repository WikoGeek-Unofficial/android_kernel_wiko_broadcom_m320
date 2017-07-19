#ifndef __ASM_MACH_MEMORY_H
#define __ASM_MACH_MEMORY_H

#include <linux/ioport.h>

#define PLAT_PHYS_OFFSET	UL(CONFIG_MEMORY_START)
#define MEM_SIZE	UL(CONFIG_MEMORY_SIZE)

#ifndef __ASSEMBLY__
extern struct resource *get_mem_resource(char *name);
extern void init_memory_layout(void);
#endif

#endif /* __ASM_MACH_MEMORY_H */
