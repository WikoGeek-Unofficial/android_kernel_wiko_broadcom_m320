#ifndef __ASM_DEV_BT_RFKILL_H
#define __ASM_DEV_BT_RFKILL_H

void __init add_bcm_bzhw_device(int gpio_bt_wake, int gpio_host_wake);
void __init add_bcmbt_lpm_device(int gpio_bt_wake, int gpio_host_wake);

#endif /* __ASM_DEV_BT_RFKILL_H */
