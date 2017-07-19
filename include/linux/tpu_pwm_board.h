#ifndef __TPU_PWM_BOARD_H
#define __TPU_PWM_BOARD_H

#ifdef __KERNEL__

/* Enumerating value of TPU modules. */
enum tpu_modules {
	TPU_MODULE_0,  /* This value indicate TPU0 */
	TPU_MODULE_MAX /*< This value is supremum numbers */
};

/*  Enumerating value of TPU channels */
enum tpu_channels {
	TPU_CHANNEL_0,  /* This value indicate Channel0 */
	TPU_CHANNEL_1,  /* This value indicate Channel1 */
	TPU_CHANNEL_2,  /* This value indicate Channel2 */
	TPU_CHANNEL_3,  /* This value indicate Channel3 */
	TPU_CHANNEL_MAX /* This value is supremum numbers */
};

#endif /* __KERNEL__ */

#endif /* __TPU_PWM_BOARD_H */
