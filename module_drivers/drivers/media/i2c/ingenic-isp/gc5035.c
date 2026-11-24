/*
 * A V4L2 driver for OmniVision gc5035 cameras.
 *
 * Copyright 2006 One Laptop Per Child Association, Inc.  Written
 * by Jonathan Corbet with substantial inspiration from Mark
 * McClelland's ovcamchip code.
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <isp-sensor.h>
#include <linux/unistd.h>

#define GC5035_CHIP_ID_H	(0x50)
#define GC5035_CHIP_ID_L	(0x35)
#define GC5035_REG_END		0xff
#define GC5035_REG_DELAY	0x00

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/* SENSOR MIRROR FLIP INFO */
#define GC5035_MIRROR_FLIP_ENABLE         0
#if GC5035_MIRROR_FLIP_ENABLE
#define GC5035_MIRROR                     0x83
#define GC5035_RSTDUMMY1                  0x03
#define GC5035_RSTDUMMY2                  0xfc
#else
#define GC5035_MIRROR                     0x80
#define GC5035_RSTDUMMY1                  0x02
#define GC5035_RSTDUMMY2                  0x7c
#endif

struct gc5035_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;
	unsigned int Dgain_ratio;
};

struct gc5035_gpio {
	int pin;
	int active_level;
};

struct gc5035_supply{
	struct regulator *avdd;
	struct regulator *dvdd;
	struct regulator *dovdd;
};

struct gc5035_info {
	struct gc5035_supply supply;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;

	struct v4l2_clk *clk;

	struct v4l2_ctrl *exposure;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct gc5035_win_size *win;

	struct gc5035_gpio reset;
	struct gc5035_gpio pwdn;
	struct gc5035_gpio led;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	int index;
	unsigned int value;
	unsigned int dgain;
	unsigned int gain;
};

static struct again_lut gc5035_again_lut[] = {
	{1,  0x0,  256,  0},
	{2,  0x1,  302,  15624},
	{3,  0x2,  358,  31707},
	{4,  0x3,  425,  47928},
	{5,  0x8,  502,  63671},
	{6,  0x9,  599,  80374},
	{7,  0xa,  717,  97375},
	{8,  0xb,  845,  112906},
	{9,  0xc,  998,  128640},
	{10, 0xd,  1203, 146304},
	{11, 0xe,  1434, 162911},
	{12, 0xf,  1710, 179554},
	{13, 0x10, 1997, 194224},
	{14, 0x11, 2355, 209814},
	{15, 0x12, 2816, 226717},
	{16, 0x13, 3318, 242227},
	{17, 0x14, 3994, 259760},
};

static inline struct gc5035_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc5035_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct gc5035_info, hdl)->sd;
}
struct regval_list {
	uint8_t reg_num;
	unsigned char value;
};

static struct regval_list gc5035_2048_1944_30fps_mipi_init_regs[] = {
	/* SYSTEM */
	{0xfc, 0x01},
	{0xf4, 0x40},
	{0xf5, 0xe9},
	{0xf6, 0x14},
	{0xf8, 0x49},
	{0xf9, 0x82},
	{0xfa, 0x00},
	{0xfc, 0x81},
	{0xfe, 0x00},
	{0x36, 0x01},
	{0xd3, 0x87},
	{0x36, 0x00},
	{0x33, 0x00},
	{0xfe, 0x03},
	{0x01, 0xe7},
	{0xf7, 0x01},
	{0xfc, 0x8f},
	{0xfc, 0x8f},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0xee, 0x30},
	{0x87, 0x18},
	{0xfe, 0x01},
	{0x8c, 0x90},
	{0xfe, 0x00},
	/* Analog & CISCTL */
	{0xfe, 0x00},
	{0x05, 0x02},
	{0x06, 0xda},
	{0x9d, 0x0c},
	{0x09, 0x00},
	{0x0a, 0x04},
	{0x0b, 0x00},
	{0x0c, 0x03},
	{0x0d, 0x07},
	{0x0e, 0xa8},
	{0x0f, 0x0a},
	{0x10, 0x30},
	{0x11, 0x02},
	{0x41, 0x07},//frame_length
	{0x42, 0xcc},
	{0x17, GC5035_MIRROR},
	{0x19, 0x05},
	{0xfe, 0x02},
	{0x30, 0x03},
	{0x31, 0x03},
	{0xfe, 0x00},
	{0xd9, 0xc0},
	{0x1b, 0x20},
	{0x21, 0x48},
	{0x28, 0x22},
	{0x29, 0x58},
	{0x44, 0x20},
	{0x4b, 0x10},
	{0x4e, 0x1a},
	{0x50, 0x11},
	{0x52, 0x33},
	{0x53, 0x44},
	{0x55, 0x10},
	{0x5b, 0x11},
	{0xc5, 0x02},
	{0x8c, 0x1a},
	{0xfe, 0x02},
	{0x33, 0x05},
	{0x32, 0x38},
	{0xfe, 0x00},
	{0x91, 0x80},
	{0x92, 0x28},
	{0x93, 0x20},
	{0x95, 0xa0},
	{0x96, 0xe0},
	{0xd5, 0xfc},
	{0x97, 0x28},
	{0x16, 0x0c},
	{0x1a, 0x1a},
	{0x1f, 0x11},
	{0x20, 0x10},
	{0x46, 0x83},
	{0x4a, 0x04},
	{0x54, GC5035_RSTDUMMY1},
	{0x62, 0x00},
	{0x72, 0x8f},
	{0x73, 0x89},
	{0x7a, 0x05},
	{0x7d, 0xcc},
	{0x90, 0x00},
	{0xce, 0x18},
	{0xd0, 0xb2},
	{0xd2, 0x40},
	{0xe6, 0xe0},
	{0xfe, 0x02},
	{0x12, 0x01},
	{0x13, 0x01},
	{0x14, 0x01},
	{0x15, 0x02},
	{0x22, GC5035_RSTDUMMY2},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0xfe, 0x00},
	{0xfc, 0x88},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfc, 0x88},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfc, 0x8e},
	/* Gain */
        {0xfe, 0x00},
        {0xb0, 0x6e},
        {0xb1, 0x01},
        {0xb2, 0x00},
        {0xb3, 0x00},
        {0xb4, 0x00},
        {0xb6, 0x00},
	/* ISP */
        {0xfe, 0x01},
        {0x53, 0x00},
        {0x89, 0x03},
        {0x60, 0x40},
	/* BLK */
	{0xfe, 0x01},
	{0x42, 0x21},
	{0x49, 0x03},
	{0x4a, 0xff},
	{0x4b, 0xc0},
	{0x55, 0x00},
	/* Anti_blooming */
	{0xfe, 0x01},
	{0x41, 0x28},
	{0x4c, 0x00},
	{0x4d, 0x00},
	{0x4e, 0x3c},
	{0x44, 0x08},
	{0x48, 0x02},
	/* Crop */
	{0xfe, 0x01},
	{0x91, 0x00},
	{0x92, 0x08},
	{0x93, 0x00},
	{0x94, 0x07},
	{0x95, 0x07},
	{0x96, 0x98},
//	{0x97, 0x0a},
//	{0x98, 0x20},
	{0x97, 0x08},//2048
	{0x98, 0x00},
	{0x99, 0x00},
	/* MIPI */
	{0xfe, 0x03},
	{0x02, 0x57},
	{0x03, 0xb7},
	{0x15, 0x14},
	{0x18, 0x0f},
	{0x21, 0x22},
	{0x22, 0x06},
	{0x23, 0x48},
	{0x24, 0x12},
	{0x25, 0x28},
	{0x26, 0x08},
	{0x29, 0x06},
        {0x2a, 0x58},
        {0x2b, 0x08},
        {0xfe, 0x01},
        {0x8c, 0x10},

        {0xfe, 0x00},
        {0x3e, 0x01},
	{GC5035_REG_END, 0x0},
};

static struct regval_list gc5035_2592_1944_30fps_mipi_init_regs[] = {
	/* SYSTEM */
	{0xfc, 0x01},
	{0xf4, 0x40},
	{0xf5, 0xe9},
	{0xf6, 0x14},
	{0xf8, 0x49},
	{0xf9, 0x82},
	{0xfa, 0x00},
	{0xfc, 0x81},
	{0xfe, 0x00},
	{0x36, 0x01},
	{0xd3, 0x87},
	{0x36, 0x00},
	{0x33, 0x00},
	{0xfe, 0x03},
	{0x01, 0xe7},
	{0xf7, 0x01},
	{0xfc, 0x8f},
	{0xfc, 0x8f},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0xee, 0x30},
	{0x87, 0x18},
	{0xfe, 0x01},
	{0x8c, 0x90},
	{0xfe, 0x00},

	/* Analog & CISCTL */
	{0xfe, 0x00},
	{0x05, 0x02},
	{0x06, 0xda},
	{0x9d, 0x0c},
	{0x09, 0x00},
	{0x0a, 0x04},
	{0x0b, 0x00},
	{0x0c, 0x03},
	{0x0d, 0x07},
	{0x0e, 0xa8},
	{0x0f, 0x0a},
	{0x10, 0x30},
	{0x11, 0x02},
	{0x17, GC5035_MIRROR},
	{0x19, 0x05},
	{0xfe, 0x02},
	{0x30, 0x03},
	{0x31, 0x03},
	{0xfe, 0x00},
	{0xd9, 0xc0},
	{0x1b, 0x20},
	{0x21, 0x48},
	{0x28, 0x22},
	{0x29, 0x58},
	{0x44, 0x20},
	{0x4b, 0x10},
	{0x4e, 0x1a},
	{0x50, 0x11},
	{0x52, 0x33},
	{0x53, 0x44},
	{0x55, 0x10},
	{0x5b, 0x11},
	{0xc5, 0x02},
	{0x8c, 0x1a},
	{0xfe, 0x02},
	{0x33, 0x05},
	{0x32, 0x38},
	{0xfe, 0x00},
	{0x91, 0x80},
	{0x92, 0x28},
	{0x93, 0x20},
	{0x95, 0xa0},
	{0x96, 0xe0},
	{0xd5, 0xfc},
	{0x97, 0x28},
	{0x16, 0x0c},
	{0x1a, 0x1a},
	{0x1f, 0x11},
	{0x20, 0x10},
	{0x46, 0xe3},
	{0x4a, 0x04},
	{0x54, GC5035_RSTDUMMY1},
	{0x62, 0x00},
	{0x72, 0xcf},
	{0x73, 0xc9},
	{0x7a, 0x05},
	{0x7d, 0xcc},
	{0x90, 0x00},
	{0xce, 0x98},
	{0xd0, 0xb2},
	{0xd2, 0x40},
	{0xe6, 0xe0},
	{0xfe, 0x02},
	{0x12, 0x01},
	{0x13, 0x01},
	{0x14, 0x01},
	{0x15, 0x02},
	{0x22, GC5035_RSTDUMMY2},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0xfe, 0x00},
	{0xfc, 0x88},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfc, 0x88},
	{0xfe, 0x10},
	{0xfe, 0x00},
	{0xfc, 0x8e},

	/* Gain */
	{0xfe, 0x00},
	{0xb0, 0x6e},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb3, 0x00},
	{0xb4, 0x00},
	{0xb6, 0x00},

	/* ISP */
	{0xfe, 0x01},
	{0x53, 0x00},
	{0x89, 0x03},
	{0x60, 0x40},
	{0x87, 0x50},

	/* BLK */
	{0xfe, 0x01},
	{0x42, 0x21},
	{0x49, 0x03},
	{0x4a, 0xff},
	{0x4b, 0xc0},
	{0x55, 0x00},

	/* Anti_blooming */
	{0xfe, 0x01},
	{0x41, 0x28},
	{0x4c, 0x00},
	{0x4d, 0x00},
	{0x4e, 0x3c},
	{0x44, 0x08},
	{0x48, 0x01},

	/* Crop */
	{0xfe, 0x01},
	{0x91, 0x00},
	{0x92, 0x08},
	{0x93, 0x00},
	{0x94, 0x07},
	{0x95, 0x07},
	{0x96, 0x98},
//	{0x97, 0x0a},//2952
//	{0x98, 0x20},
	{0x97, 0x08},//2048
	{0x98, 0x00},
	{0x99, 0x00},

	/* MIPI */
	{0xfe, 0x03},
	{0x02, 0x57},
	{0x03, 0xb7},
	{0x15, 0x14},
	{0x18, 0x0f},
	{0x21, 0x22},
	{0x22, 0x06},
	{0x23, 0x48},
	{0x24, 0x12},
	{0x25, 0x28},
	{0x26, 0x08},
	{0x29, 0x06},
	{0x2a, 0x58},
	{0x2b, 0x08},
	{0xfe, 0x01},
	{0x8c, 0x10},

	{0xfe, 0x00},
	{0x3e, 0x01},
	{GC5035_REG_END, 0x0},
};

/*
 * the part of driver was fixed.
 */

static struct regval_list gc5035_stream_on_mipi[] = {
	{0xfe,0x00},
	{0x3e,0x91},
	{GC5035_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list gc5035_stream_off_mipi[] = {
	{0xfe,0x00},
	{0x3e,0x01},
	{GC5035_REG_END, 0x00},	/* END MARKER */
};

static int gc5035_read(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char *value)
{
	struct gc5035_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg[2] = {
		[0] = {
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= (unsigned char *)&reg,
		},
		[1] = {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= value,
		}
	};
	int ret = 0;

	ret = i2c_transfer(client->adapter, msg, 2);
	if(ret > 0)
		ret = 0;

	return ret;
}

static int gc5035_write(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char value)
{
	struct gc5035_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 2,
		.buf	= buf,
	};
	int ret;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int gc5035_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != GC5035_REG_END) {
		if (vals->reg_num == GC5035_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = gc5035_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}

/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int gc5035_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != GC5035_REG_END) {
		if (vals->reg_num == GC5035_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = gc5035_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}

/*
 * Stuff that knows about the sensor.
 */
//static int gc5035_lightup(struct v4l2_subdev *sd, u32 val)
//{
//	struct gc5035_info *info = to_state(sd);
//	if(val) {
//		gpio_direction_output(info->led.pin, info->led.active_level);
//	} else {
//		gpio_direction_output(info->led.pin, !info->led.active_level);
//	}
//	return 0;
//}

static int gc5035_reset(struct v4l2_subdev *sd, u32 val)
{
	struct gc5035_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(10);
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
		msleep(10);
	}
	return 0;
}

static int gc5035_pwdn(struct v4l2_subdev *sd, u32 val)
{
	struct gc5035_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->pwdn.pin, !info->pwdn.active_level);
		msleep(10);
		gpio_direction_output(info->pwdn.pin, info->pwdn.active_level);
		msleep(10);
	}
	return 0;
}

static int gc5035_init(struct v4l2_subdev *sd, u32 val)
{
	struct gc5035_info *info = to_state(sd);
	int ret = 0;

	ret = gc5035_write_array(sd, info->win->regs);

	return ret;
}

static int gc5035_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = gc5035_read(sd, 0xf0, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != GC5035_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = gc5035_read(sd, 0xf1, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != GC5035_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static struct gc5035_win_size gc5035_win_sizes[] = {
	{
		.width					= 2048,
		.height					= 1944,
		.sensor_info.fps			= 30 << 16 | 1,
		.sensor_info.mipi_cfg.twidth		= 2048,
		.sensor_info.mipi_cfg.theight		= 1944,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_OTHER_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en	= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en	= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en	= 0,
		.sensor_info.mipi_cfg.data_type_value	= 0,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode	= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt	= TX_SENSOR_RAW10,
		.sensor_info.total_height		= 0x7cc,
		.mbus_code				= MEDIA_BUS_FMT_SRGGB10_1X10,
		.colorspace				= V4L2_COLORSPACE_SRGB,
//		.regs					= gc5035_2592_1944_30fps_mipi_init_regs,
		.regs					= gc5035_2048_1944_30fps_mipi_init_regs,
		.Dgain_ratio                            = 0,
	},
};

#if 0
static int gc5035_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= N_gc5035_FMTS)
		return -EINVAL;

	code->code = gc5035_formats[code->index].mbus_code;
	return 0;
}
#endif

/*
 * Set a format.
 */
static int gc5035_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct gc5035_format_struct *ovfmt;
	struct gc5035_win_size *wsize;
	struct gc5035_info *info = to_state(sd);
	int ret;

	if (format->pad)
		return -EINVAL;

	return 0;
}

static int gc5035_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct gc5035_info *info = to_state(sd);
	struct gc5035_win_size *wsize = info->win;
	struct v4l2_mbus_framefmt *fmt = &format->format;
	int ret = 0;

	if(!info->win) {
		dev_err(sd->dev, "sensor win_size not set!\n");
		return -EINVAL;
	}

	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->code = wsize->mbus_code;
	fmt->colorspace = wsize->colorspace;
	*(unsigned int *)fmt->reserved = &wsize->sensor_info;

	printk("----%s, %d, width: %d, height: %d, code: %x\n",
			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int gc5035_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5035_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5035_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5035_s_vflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

/*
 * GAIN is split between REG_GAIN and REG_VREF[7:6].  If one believes
 * the data sheet, the VREF parts should be the most significant, but
 * experience shows otherwise.  There seems to be little value in
 * messing with the VREF bits, so we leave them alone.
 */
static int gc5035_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int gc5035_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static unsigned int again_to_regval(int gain)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 16; i >= 0; i--) {
		lut = &gc5035_again_lut[i];
		if(gain >= lut->gain) {
			return lut->index;
		}
	}

	/*last value.*/
	return lut->index;
}
static int gc5035_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	struct gc5035_info *info = to_state(sd);
	unsigned char v = 0;
	unsigned int reg_val = 0;
	int ret = 0;

	return ret;
}
extern uint64_t fix_point_mult2(uint32_t pointpos, uint64_t a, uint64_t b);
extern uint64_t fix_point_div(uint32_t pointpos, uint64_t a, uint64_t b);
extern uint32_t tisp_math_exp2(uint32_t val, const unsigned char shift_in, const unsigned char shift_out);
/*set analog gain db value, map value to sensor register.*/
static int gc5035_s_again(struct v4l2_subdev *sd, int again)
{
	struct gc5035_info *info = to_state(sd);
	struct again_lut *val_lut = gc5035_again_lut;
	unsigned int index = 0;
	int ret = 0;
	int temp_gain = 0,result = 0;
	unsigned int div = fix_point_div(4,again,65536);
	unsigned int pow = tisp_math_exp2(div, 4, 4);
	result = fix_point_mult2(4,pow,256);

	if(again < info->again->minimum || again > info->again->maximum) {
		/* use default value. */
		index = again_to_regval(info->again->default_value);
	} else {
		index = again_to_regval(again);
	}

	ret = gc5035_write(sd, 0xfe, 0x00);
	ret += gc5035_write(sd, 0xb6, val_lut[index].value);

	temp_gain = result;
	temp_gain = result * gc5035_win_sizes[0].Dgain_ratio / val_lut[index].dgain;
	ret += gc5035_write(sd, 0xb2, temp_gain & 0xfc);
	ret += gc5035_write(sd, 0xb1, (temp_gain >> 8) & 0x0f);
	if(ret < 0){
		printk("%s : gc5035_write error %d~~~~\n",__func__,__LINE__);
		return ret;
	}
	return 0;

}

/*
 * Tweak autogain.
 */
static int gc5035_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5035_s_exp(struct v4l2_subdev *sd, int value)
{
	u32 caltime = 0;
	int ret = 0;
	int frame_length = 0;

	caltime = value >> 2;
	caltime = caltime << 2;
	gc5035_win_sizes[0].Dgain_ratio = 256 * value / caltime;
	ret =  gc5035_write(sd, 0xfe, 0x00);
	ret =  gc5035_write(sd, 0x04, caltime&0xff);
	ret += gc5035_write(sd, 0x03, ((caltime>>8)&0x3f));

	return ret;
}

static int gc5035_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc5035_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_AUTOGAIN:
			return gc5035_g_gain(sd, &info->gain->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return gc5035_g_again(sd, &info->again->val);
	}
	return -EINVAL;
}

static int gc5035_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc5035_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			return gc5035_s_brightness(sd, ctrl->val);
		case V4L2_CID_CONTRAST:
			return gc5035_s_contrast(sd, ctrl->val);
		case V4L2_CID_VFLIP:
			return gc5035_s_vflip(sd, ctrl->val);
		case V4L2_CID_HFLIP:
			return gc5035_s_hflip(sd, ctrl->val);
		case V4L2_CID_AUTOGAIN:
			/* Only set manual gain if auto gain is not explicitly
			   turned on. */
			if (!ctrl->val) {
				/* gc5035_s_gain turns off auto gain */
				return gc5035_s_gain(sd, info->gain->val);
			}
			return gc5035_s_autogain(sd, ctrl->val);
		case V4L2_CID_GAIN:
			return gc5035_s_gain(sd, ctrl->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return gc5035_s_again(sd, ctrl->val);
		case V4L2_CID_EXPOSURE:
			return gc5035_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops gc5035_ctrl_ops = {
	.s_ctrl = gc5035_s_ctrl,
	.g_volatile_ctrl = gc5035_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int gc5035_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = gc5035_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int gc5035_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	gc5035_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

static int gc5035_power(struct v4l2_subdev *sd, int on)
{
	struct gc5035_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	info->supply.avdd  = devm_regulator_get_optional(&client->dev, "avdd");
	info->supply.dvdd  = devm_regulator_get_optional(&client->dev, "dvdd");
	info->supply.dovdd = devm_regulator_get_optional(&client->dev, "dovdd");

	if (IS_ERR(info->supply.avdd)||IS_ERR(info->supply.dvdd)
			||IS_ERR(info->supply.dovdd)) {
		if ((PTR_ERR(info->supply.avdd) == -EPROBE_DEFER)
				||(PTR_ERR(info->supply.avdd) == -EPROBE_DEFER)
				||(PTR_ERR(info->supply.avdd) == -EPROBE_DEFER))
			return -EPROBE_DEFER;
		printk("No gc5035 vdd regulator found\n");
	}

	if ((!IS_ERR(info->supply.avdd))&&(!IS_ERR(info->supply.avdd))
			&&(!IS_ERR(info->supply.avdd)))  {
		if(on){
			ret = regulator_enable(info->supply.dovdd);
			msleep(1);
			ret = regulator_enable(info->supply.dvdd);
			msleep(1);
			ret = regulator_enable(info->supply.avdd);
			if (ret)
				dev_err(&client->dev, "gc5035 vdd supply disable failed\n");
		}
		else{
			ret = regulator_disable(info->supply.avdd);
			ret = regulator_disable(info->supply.dvdd);
			ret = regulator_disable(info->supply.dovdd);
			if (ret)
				dev_err(&client->dev, "gc5035 vdd supply disable failed\n");
		}
	}else{
		dev_err(&client->dev, "gc5035 vdd supply IS_ERR failed\n");
	}
	return ret;
}

int gc5035_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	if (enable) {
		ret = gc5035_write_array(sd, gc5035_stream_on_mipi);
		printk("gc5035 stream on\n");

	}
	else {
		ret = gc5035_write_array(sd, gc5035_stream_off_mipi);
		printk("gc5035 stream off\n");
	}
	return ret;
}
/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops gc5035_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = gc5035_g_register,
	.s_register = gc5035_s_register,
#endif
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,

};

static const struct v4l2_subdev_video_ops gc5035_video_ops = {
	.s_stream = gc5035_s_stream,
};

static const struct v4l2_subdev_pad_ops gc5035_pad_ops = {
	//.enum_frame_interval = gc5035_enum_frame_interval,
	//.num_frame_size = gc5035_enum_frame_size,
	//.enum_mbus_code = gc5035_enum_mbus_code,
	.set_fmt = gc5035_set_fmt,
	.get_fmt = gc5035_get_fmt,
};

static const struct v4l2_subdev_ops gc5035_ops = {
	.core = &gc5035_core_ops,
	.video = &gc5035_video_ops,
	.pad = &gc5035_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int gc5035_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct v4l2_fract tpf;
	struct v4l2_subdev *sd;
	struct gc5035_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	unsigned long rate;
	unsigned char value;
	int mclk_index = -1;
	char id_div[9];
	char id_mux[9];
	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->reset.pin = gpio;
		info->reset.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,pwdn-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->pwdn.pin = gpio;
		info->pwdn.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}

	v4l2_i2c_subdev_init(sd, client, &gc5035_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;


	gc5035_power(sd,1);
	gc5035_pwdn(sd, 1);
	gc5035_reset(sd, 1);
	msleep(1);

	/*clk*/
	info->clk = v4l2_clk_get(&client->dev, "div_cim");
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		goto err_clkget;
	}


	ret = v4l2_clk_set_rate(info->clk, 24000000);
	if(ret)
		dev_err(sd->dev, "clk_set_rate err!\n");

	ret = v4l2_clk_enable(info->clk);
	if(ret)
		dev_err(sd->dev, "clk_enable err!\n");

#if 1
	/* Make sure it's an gc5035 */
	ret = gc5035_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
				"chip found @ 0x%x (%s) is not an gc5035 chip.\n",
				client->addr << 1, client->adapter->name);
		return ret;
	}
#endif
#if 1
	/*IRCUT ctl 0:off 1:on*/
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &gc5035_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &gc5035_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &gc5035_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &gc5035_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &gc5035_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &gc5035_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 259760, 1, 1000);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &gc5035_ctrl_ops,
			V4L2_CID_EXPOSURE, 4, 1996-16, 1, 500);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;
		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &gc5035_win_sizes[0];

	gc5035_init(sd, 1);
	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&info->sd.entity, 1, &info->pad, 0);
	if(ret < 0) {
		goto err_entity_init;
	}
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "gc5035 Probed\n");
	return 0;
#endif
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}


static int gc5035_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5035_info *info = to_state(sd);
	int ret;

	if (!IS_ERR(info->supply.avdd)) {
		ret = regulator_disable(info->supply.avdd);
		if (ret) {
			dev_err(dev, "gc5035 avdd supply disable failed\n");
			return ret;
		}
	}
	if (!IS_ERR(info->supply.dvdd)) {
		ret = regulator_disable(info->supply.dvdd);
		if (ret) {
			dev_err(dev, "gc5035 dvdd supply disable failed\n");
			return ret;
		}
	}
	if (!IS_ERR(info->supply.dovdd)) {
		ret = regulator_disable(info->supply.dovdd);
		if (ret) {
			dev_err(dev, "gc5035 dovdd supply disable failed\n");
			return ret;
		}
	}

	v4l2_clk_disable(info->clk);
	return 0;
}

static int gc5035_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5035_info *info = to_state(sd);
	int ret;

	if (!IS_ERR(info->supply.avdd)) {
		ret = regulator_enable(info->supply.avdd);
		if (ret) {
			dev_err(dev, "gc5035 avdd supply enable failed\n");
		}
	}
	if (!IS_ERR(info->supply.dvdd)) {
		ret = regulator_enable(info->supply.dvdd);
		if (ret) {
			dev_err(dev, "gc5035 dvdd supply enable failed\n");
		}
	}
	if (!IS_ERR(info->supply.dovdd)) {
		ret = regulator_enable(info->supply.dovdd);
		if (ret) {
			dev_err(dev, "gc5035 dovdd supply enable failed\n");
		}
	}

	v4l2_clk_enable(info->clk);
	gc5035_reset(sd, 1);
	gc5035_init(sd, 1);
	return 0;
}

static int gc5035_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5035_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id gc5035_id[] = {
	{ "gc5035", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc5035_id);

static const struct of_device_id gc5035_of_match[] = {
	{.compatible = "galaxy,gc5035", },
	{},
};
MODULE_DEVICE_TABLE(of, gc5035_of_match);

const struct dev_pm_ops gc5035_pm =
{
	.suspend = gc5035_suspend,
	.resume = gc5035_resume,
};

static struct i2c_driver gc5035_driver = {
	.driver = {
		.name	= "gc5035",
		.of_match_table = of_match_ptr(gc5035_of_match),
		.pm = &gc5035_pm,
	},
	.probe		= gc5035_probe,
	.remove		= gc5035_remove,
	.id_table	= gc5035_id,
};

module_i2c_driver(gc5035_driver);
MODULE_DESCRIPTION("A low-level driver for gc5035 sensors");
MODULE_LICENSE("GPL");
