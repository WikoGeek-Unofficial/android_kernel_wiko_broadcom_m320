#ifndef __TK_PSENSOR_TMD2771_H
#define __TK_PSENSOR_TMD2771_H

typedef unsigned char           u8;
typedef unsigned short          u16;
typedef unsigned int            u32;

/* ioctl numbers*/
#define TAOS_IOCTL_MAGIC                0XCF
#define TAOS_IOCTL_ALS_ON               _IO(TAOS_IOCTL_MAGIC, 1)
#define TAOS_IOCTL_ALS_OFF              _IO(TAOS_IOCTL_MAGIC, 2)
#define TAOS_IOCTL_ALS_DATA             _IOR(TAOS_IOCTL_MAGIC, 3, short)
#define TAOS_IOCTL_ALS_CALIBRATE        _IO(TAOS_IOCTL_MAGIC, 4)
#define TAOS_IOCTL_CONFIG_GET           _IOR(TAOS_IOCTL_MAGIC, 5, \
						struct taos_cfg)
#define TAOS_IOCTL_CONFIG_SET           _IOW(TAOS_IOCTL_MAGIC, 6, \
						struct taos_cfg)
#define TAOS_IOCTL_PROX_ON              _IO(TAOS_IOCTL_MAGIC, 7)
#define TAOS_IOCTL_PROX_OFF             _IO(TAOS_IOCTL_MAGIC, 8)
#define TAOS_IOCTL_PROX_DATA            _IOR(TAOS_IOCTL_MAGIC, 9, \
						struct taos_prox_info)
#define TAOS_IOCTL_PROX_EVENT           _IO(TAOS_IOCTL_MAGIC, 10)
#define TAOS_IOCTL_PROX_CALIBRATE       _IO(TAOS_IOCTL_MAGIC, 11)
#define TAOS_IOCTL_SENSOR_ON    _IO(TAOS_IOCTL_MAGIC, 12)
#define TAOS_IOCTL_SENSOR_OFF   _IO(TAOS_IOCTL_MAGIC, 13)
#define TAOS_IOCTL_SENSOR_CONFIG        _IOW(TAOS_IOCTL_MAGIC, 14, \
						struct taos_cfg)
#define TAOS_IOCTL_SENSOR_CHECK _IO(TAOS_IOCTL_MAGIC, 15)
#define TAOS_IOCTL_SENSOR_TEST  _IO(TAOS_IOCTL_MAGIC, 16)


#define TAOS_DEVICE_ID		"tmd2771"
#define TAOS_ID_NAME_SIZE	10

/*#define TAOS_DEVICE_ADDR1	0x29
#define TAOS_DEVICE_ADDR2	0x39
#define TAOS_DEVICE_ADDR3	0x49

#define TAOS_INT_GPIO		89
#define ALS_PS_INT		gpio_to_irq(TAOS_INT_GPIO)*/
#define PROXIMITY_SLAVE_ADDRESS	0x39
#define	ALS_INT			0x2F

struct taos_cfg {
	u32 calibrate_target;
	u16 als_time;
	u16 scale_factor;
	u16 gain_trim;
	u8 filter_history;
	u8 filter_count;
	u8 gain;
	u16 prox_threshold_hi;
	u16 prox_threshold_lo;
	u16 als_threshold_hi;
	u16 als_threshold_lo;
	u16 prox_win_sw;
	u8 prox_int_time;
	u8 prox_adc_time;
	u8 prox_wait_time;
	u8 prox_intr_filter;
	u8 prox_config;
	u8 prox_pulse_cnt;
	u8 prox_gain;
};

struct taos_prox_info {
	u16 prox_clear;
	u16 prox_data;
	int prox_event;
};

struct tmd2771_platform_data {
	struct taos_cfg *cfg_data;
	unsigned int tmd2771_port;
};

#endif /* __TK_PSENSOR_TMD2771_H */
