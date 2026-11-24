/*
 * A V4L2 driver for OmniVision SC035HGS cameras.
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
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>
#include <linux/of_gpio.h>
#include <isp-sensor.h>

#define SC035HGS_CHIP_ID_H       (0x00)
#define SC035HGS_CHIP_ID_L       (0x31)
#define SC035HGS_REG_END		0xffff
#define SC035HGS_REG_DELAY	0xfffe

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct sc035hgs_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;

};

struct sc035hgs_gpio {
	int pin;
	int active_level;
};

struct sc035hgs_info {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;

	struct v4l2_clk *clk;

	struct v4l2_ctrl *exposure;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct sc035hgs_win_size *win;

	struct sc035hgs_gpio reset;
	struct sc035hgs_gpio vcc_en;
	struct sc035hgs_gpio ircutp;
	struct sc035hgs_gpio ircutn;
};


/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;	/*sensor regs value*/
	unsigned int gain;	/*isp gain*/
};

struct again_lut sc035hgs_again_lut[] = {
	{0x310, 0},
	{0x311, 5731},
	{0x312, 11136},
	{0x313, 16248},
	{0x314, 21097},
	{0x315, 25710},
	{0x316, 30109},
	{0x317, 34312},
	{0x318, 38336},
	{0x319, 42195},
	{0x31a, 45904},
	{0x31b, 49472},
	{0x31c, 52910},
	{0x31d, 56228},
	{0x31e, 59433},
	{0x31f, 62534},
	{0x710, 65536},
	{0x711, 71267},
	{0x712, 76672},
	{0x713, 81784},
	{0x714, 86633},
	{0x715, 91246},
	{0x716, 95645},
	{0x717, 99848},
	{0x718, 103872},
	{0x719, 107731},
	{0x71a, 111440},
	{0x71b, 115008},
	{0x71c, 118446},
	{0x71e, 121764},
	{0x71d, 124969},
	{0x71f, 128070},
	{0xf10, 131072},
	{0xf11, 136803},
	{0xf12, 142208},
	{0xf13, 147320},
	{0xf14, 152169},
	{0xf15, 156782},
	{0xf16, 161181},
	{0xf17, 165384},
	{0xf18, 169408},
	{0xf19, 173267},
	{0xf1a, 176976},
	{0xf1b, 180544},
	{0xf1c, 183982},
	{0xf1d, 187300},
	{0xf1e, 190505},
	{0xf1f, 193606},
	{0x1f10, 196608},
	{0x1f11, 202339},
	{0x1f12, 207744},
	{0x1f13, 212856},
	{0x1f14, 217705},
	{0x1f15, 222318},
	{0x1f16, 226717},
	{0x1f17, 230920},
	{0x1f18, 234944},
	{0x1f19, 238803},
	{0x1f1a, 242512},
	{0x1f1b, 246080},
	{0x1f1c, 249518},
	{0x1f1d, 252836},
	{0x1f1e, 256041},
	{0x1f1f, 259142},
};

static inline struct sc035hgs_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sc035hgs_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct sc035hgs_info, hdl)->sd;
}

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

static struct regval_list sc035hgs_init_regs_640_480_9fps_mipi[] = {
	{0x0103, 0x01},//软复位
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x300f, 0x0f},
	{0x3018, 0x13},//1 lane
	//{0x3018, 0x33},//2 lane
	{0x3019, 0xfc},
	{0x301c, 0x78},
	{0x301f, 0x87},
	{0x3031, 0x0a}, //raw10
	{0x3037, 0x20},
	{0x303f, 0x01},
	{0x320c, 0x03},
	{0x320d, 0x54},
	//{0x320e, 0x02},//240fps
	//{0x320f, 0x10},
//	{0x320e, 0x31},//9fps
//	{0x320f, 0x80},
//	{0x320e, 0x03},//160fps
//	{0x320f, 0x18},
	{0x320e, 0x0e},//33fps
	{0x320f, 0xa6},
	{0x3217, 0x00},
	{0x3218, 0x00},
	{0x3220, 0x10},
	{0x3223, 0x48},
	{0x3226, 0x74},
	{0x3227, 0x07},
	{0x323b, 0x00},
	{0x3250, 0xf0},
	{0x3251, 0x02},
	{0x3252, 0x02},
	{0x3253, 0x08},
	{0x3254, 0x02},
	{0x3255, 0x07},
	{0x3304, 0x48},
	{0x3305, 0x00},
	{0x3306, 0x60},
	{0x3309, 0x50},
	{0x330a, 0x00},
	{0x330b, 0xc0},
	{0x330c, 0x18},
	{0x330f, 0x40},
	{0x3310, 0x10},
	{0x3314, 0x1e},
	{0x3315, 0x30},
	{0x3316, 0x68},
	{0x3317, 0x1b},
	{0x3329, 0x5c},
	{0x332d, 0x5c},
	{0x332f, 0x60},
	{0x3335, 0x64},
	{0x3344, 0x64},
	{0x335b, 0x80},
	{0x335f, 0x80},
	{0x3366, 0x06},
	{0x3385, 0x31},
	{0x3387, 0x39},
	{0x3389, 0x01},
	{0x33b1, 0x03},
	{0x33b2, 0x06},
	{0x33bd, 0xe0},
	{0x33bf, 0x10},
	{0x3621, 0xa4},
	{0x3622, 0x05},
	{0x3624, 0x47},
	{0x3630, 0x4a},
	{0x3631, 0x58},
	{0x3633, 0x52},
	{0x3635, 0x03},
	{0x3636, 0x25},
	{0x3637, 0x8a},
	{0x3638, 0x0f},
	{0x3639, 0x08},
	{0x363a, 0x00},
	{0x363b, 0x48},
	{0x363c, 0x86},
	{0x363d, 0x10},
	{0x363e, 0xf8},
	{0x3640, 0x00},
	{0x3641, 0x01},
	{0x36ea, 0x37},
	{0x36eb, 0x0e},
	{0x36ec, 0x0e},
	{0x36ed, 0x23},
	{0x36fa, 0x37},
	{0x36fb, 0x00},
	{0x36fc, 0x02},
	{0x36fd, 0x03},
	{0x3908, 0x91},
	{0x391b, 0x81},
	{0x3d08, 0x01},
	{0x3e01, 0x31},
	{0x3e02, 0x80},
	{0x3e03, 0x2b},
	{0x3e06, 0x0c},
	{0x3f04, 0x03},
	{0x3f05, 0x34},
	{0x4500, 0x59},
	{0x4501, 0xc4},
	{0x4603, 0x00},
	{0x4800, 0x64},
	{0x4809, 0x01},
	{0x4810, 0x00},
	{0x4811, 0x01},
	{0x4837, 0x13},
	{0x5011, 0x00},
	{0x5988, 0x02},
	{0x598e, 0x03},
	{0x598f, 0x10},
	{0x36e9, 0x24},
	{0x36f9, 0x20},
	{0x0100, 0x01},
	{0x4418, 0x0a},
	{0x4419, 0x80},

	/*
	{0x3314,0x1e},
	{0x3317,0x1b},
	{0x3631,0x58},
	{0x3630,0x4a},

	{0x3314,0x6f},
	{0x3317,0x10},
	{0x3631,0x48},
	{0x3630,0x4c},
	*/

	{0x3314, 0x52},
	{0x3317, 0x14},
	{0x3631, 0x48},
	{0x3630, 0x4c},
	{SC035HGS_REG_END, 0x00},	/* END MARKER */
};


static struct regval_list sc035hgs_init_regs_640_480_240fps_mipi[] = {
	{0x0103, 0x01},//软复位
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x300f, 0x0f},
	{0x3018, 0x13},
	{0x3019, 0xfc},
	{0x301c, 0x78},
	{0x301f, 0x87},
	{0x3031, 0x0a}, //raw10
	{0x3037, 0x20},
	{0x303f, 0x01},
	{0x320c, 0x03},
	{0x320d, 0x54},
	{0x320e, 0x02},
	{0x320f, 0x10},
	{0x3217, 0x00},
	{0x3218, 0x00},
	{0x3220, 0x10},
	{0x3223, 0x48},
	{0x3226, 0x74},
	{0x3227, 0x07},
	{0x323b, 0x00},
	{0x3250, 0xf0},
	{0x3251, 0x02},
	{0x3252, 0x02},
	{0x3253, 0x08},
	{0x3254, 0x02},
	{0x3255, 0x07},
	{0x3304, 0x48},
	{0x3305, 0x00},
	{0x3306, 0x60},
	{0x3309, 0x50},
	{0x330a, 0x00},
	{0x330b, 0xc0},
	{0x330c, 0x18},
	{0x330f, 0x40},
	{0x3310, 0x10},
	{0x3314, 0x1e},
	{0x3315, 0x30},
	{0x3316, 0x68},
	{0x3317, 0x1b},
	{0x3329, 0x5c},
	{0x332d, 0x5c},
	{0x332f, 0x60},
	{0x3335, 0x64},
	{0x3344, 0x64},
	{0x335b, 0x80},
	{0x335f, 0x80},
	{0x3366, 0x06},
	{0x3385, 0x31},
	{0x3387, 0x39},
	{0x3389, 0x01},
	{0x33b1, 0x03},
	{0x33b2, 0x06},
	{0x33bd, 0xe0},
	{0x33bf, 0x10},
	{0x3621, 0xa4},
	{0x3622, 0x05},
	{0x3624, 0x47},
	{0x3630, 0x4a},
	{0x3631, 0x58},
	{0x3633, 0x52},
	{0x3635, 0x03},
	{0x3636, 0x25},
	{0x3637, 0x8a},
	{0x3638, 0x0f},
	{0x3639, 0x08},
	{0x363a, 0x00},
	{0x363b, 0x48},
	{0x363c, 0x86},
	{0x363d, 0x10},
	{0x363e, 0xf8},
	{0x3640, 0x00},
	{0x3641, 0x01},
	{0x36ea, 0x37},
	{0x36eb, 0x0e},
	{0x36ec, 0x0e},
	{0x36ed, 0x23},
	{0x36fa, 0x37},
	{0x36fb, 0x00},
	{0x36fc, 0x02},
	{0x36fd, 0x03},
	{0x3908, 0x91},
	{0x391b, 0x81},
	{0x3d08, 0x01},
	{0x3e01, 0x14},
	{0x3e02, 0x80},
	{0x3e03, 0x2b},
	{0x3e06, 0x0c},
	{0x3f04, 0x03},
	{0x3f05, 0x34},
	{0x4500, 0x59},
	{0x4501, 0xc4},
	{0x4603, 0x00},
	{0x4800, 0x64},
	{0x4809, 0x01},
	{0x4810, 0x00},
	{0x4811, 0x01},
	{0x4837, 0x13},
	{0x5011, 0x00},
	{0x5988, 0x02},
	{0x598e, 0x03},
	{0x598f, 0x10},
	{0x36e9, 0x24},
	{0x36f9, 0x20},
	{0x0100, 0x01},
	{0x4418, 0x0a},
	{0x4419, 0x80},

	//[gain<2 ]
	{0x3314, 0x1e},
	{0x3317, 0x1b},
	{0x3631, 0x58},
	{0x3630, 0x4a},

	//[4>gain>=2]
	{0x3314, 0x6f},
	{0x3317, 0x10},
	{0x3631, 0x48},
	{0x3630, 0x4c},

	//[gain>=4]
	{0x3314, 0x52},
	{0x3317, 0x14},
	{0x3631, 0x48},
	{0x3630, 0x4c},
	{SC035HGS_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc035hgs_init_regs_640_480_10fps_mipi[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x300f, 0x0f},
	{0x3018, 0x13},
	{0x3019, 0xfe},
	{0x301c, 0x78},
	{0x301f, 0xac},
	{0x3031, 0x0a},
	{0x3037, 0x20},
	{0x303f, 0x01},
	{0x320c, 0x06},
	{0x320d, 0x40},
	{0x320e, 0x0e},
	{0x320f, 0xa6},
	{0x3217, 0x00},
	{0x3218, 0x00},
	{0x3220, 0x10},
	{0x3223, 0x48},
	{0x3226, 0x74},
	{0x3227, 0x07},
	{0x323b, 0x00},
	{0x3250, 0xf0},
	{0x3251, 0x02},
	{0x3252, 0x0e},
	{0x3253, 0x9e},
	{0x3254, 0x02},
	{0x3255, 0x07},
	{0x3304, 0x48},
	{0x3305, 0x00},
	{0x3306, 0x98},
	{0x3309, 0x50},
	{0x330a, 0x01},
	{0x330b, 0x18},
	{0x330c, 0x18},
	{0x330f, 0x40},
	{0x3310, 0x10},
	{0x3314, 0x68},
	{0x3315, 0x30},
	{0x3316, 0x68},
	{0x3317, 0x14},
	{0x3329, 0x5c},
	{0x332d, 0x5c},
	{0x332f, 0x60},
	{0x3335, 0x64},
	{0x3344, 0x64},
	{0x335b, 0x80},
	{0x335f, 0x80},
	{0x3366, 0x06},
	{0x3385, 0x41},
	{0x3387, 0x49},
	{0x3389, 0x01},
	{0x33b1, 0x03},
	{0x33b2, 0x06},
	{0x33bd, 0xe0},
	{0x33bf, 0x10},
	{0x3621, 0xa4},
	{0x3622, 0x05},
	{0x3624, 0x47},
	{0x3630, 0x4a},
	{0x3631, 0x58},
	{0x3633, 0x52},
	{0x3635, 0x03},
	{0x3636, 0x25},
	{0x3637, 0x8a},
	{0x3638, 0x0f},
	{0x3639, 0x08},
	{0x363a, 0x00},
	{0x363b, 0x48},
	{0x363c, 0x86},
	{0x363e, 0xf8},
	{0x3640, 0x00},
	{0x3641, 0x01},
	{0x36ea, 0x3b},
	{0x36eb, 0x0e},
	{0x36ec, 0x0e},
	{0x36ed, 0x20},
	{0x36fa, 0x3b},
	{0x36fb, 0x10},
	{0x36fc, 0x02},
	{0x36fd, 0x00},
	{0x3908, 0x91},
	{0x391b, 0x81},
	{0x3d08, 0x01},
	{0x3e01, 0xea},
	{0x3e02, 0x00},
	{0x3e03, 0x2b},
	{0x3e06, 0x0c},
	{0x3f04, 0x06},
	{0x3f05, 0x20},
	{0x4500, 0x59},
	{0x4501, 0xc4},
	{0x4603, 0x00},
	{0x4800, 0x64},
	{0x4809, 0x01},
	{0x4810, 0x00},
	{0x4811, 0x01},
	{0x4837, 0x21},
	{0x5011, 0x00},
	{0x5988, 0x02},
	{0x598e, 0x06},
	{0x598f, 0x18},
	{0x36e9, 0x24},
	{0x36f9, 0x24},
	{0x0100, 0x01},
	{0x4418, 0x0a},
	{0x363d, 0x10},
	{0x4419, 0x80},

	//[gain<2]
	{0x3631, 0x58},
	{0x3630, 0x4a},

	//[gain>=2]
	{0x3631, 0x48},
	{0x3630, 0x4c},
	{SC035HGS_REG_END, 0x00},	/* END MARKER */
};

/*
 * the part of driver was fixed.
 */

static struct regval_list sc035hgs_stream_on_mipi[] = {
	{0x0100, 0x01},
	{SC035HGS_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list sc035hgs_stream_off_mipi[] = {
	{0x0100, 0x00},
	{SC035HGS_REG_END, 0x00},	/* END MARKER */
};

static int sc035hgs_read(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char *value)
{
	struct sc035hgs_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[2] = {(reg>>8)&0xff, reg&0xff};
	struct i2c_msg msg[2] = {
		[0] = {
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= buf,
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

static int sc035hgs_write(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char value)
{
	struct sc035hgs_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[3] = {(reg>>8)&0xff, reg&0xff, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= buf,
	};
	int ret;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int sc035hgs_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != SC035HGS_REG_END) {
		if (vals->reg_num == SC035HGS_REG_DELAY) {
			msleep(vals->value);
		}
		else if(vals->reg_num == 0x0100 && vals->value == 0x01)
		{
			msleep(7);
		}
		else{
			ret = sc035hgs_read(sd, vals->reg_num, &val);
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
static int sc035hgs_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != SC035HGS_REG_END) {
		if (vals->reg_num == SC035HGS_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = sc035hgs_write(sd, vals->reg_num, vals->value);
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
static int sc035hgs_power(struct v4l2_subdev *sd, u32 val)
{
	struct sc035hgs_info *info = to_state(sd);

	if(val){
		gpio_direction_output(info->reset.pin, info->reset.active_level);
	}else{
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
	}
	return 0;
}

static int sc035hgs_reset(struct v4l2_subdev *sd, u32 val)
{
	struct sc035hgs_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(20);
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
		msleep(20);
	}
	return 0;
}

static int sc035hgs_init(struct v4l2_subdev *sd, u32 val)
{
	struct sc035hgs_info *info = to_state(sd);
	int ret = 0;

	ret = sc035hgs_write_array(sd, info->win->regs);

	return ret;
}

static int sc035hgs_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v = 0;
	int ret;

	ret = sc035hgs_read(sd, 0x3107, &v);
	// pr_info("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;

	if (v != SC035HGS_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = sc035hgs_read(sd, 0x3108, &v);
	// pr_info("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;

	if (v != SC035HGS_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}


static struct sc035hgs_win_size sc035hgs_win_sizes[] = {
	{
		.sensor_info.mipi_cfg.twidth		= 640,
		.sensor_info.mipi_cfg.theight		= 480,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.fps			= 10 << 16 | 1,

		.width		= 640,
		.height		= 480,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
//		.regs 		= sc035hgs_init_regs_640_480_240fps_mipi,
		.regs 		= sc035hgs_init_regs_640_480_9fps_mipi,
//		.regs 		= sc035hgs_init_regs_640_480_10fps_mipi,
	},
};

static int sc035hgs_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
#if 0
	if (code->pad || code->index >= N_SC035HGS_FMTS)
		return -EINVAL;

	code->code = sc035hgs_formats[code->index].mbus_code;
#endif
	return 0;
}

/*
 * Set a format.
 */
static int sc035hgs_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct sc035hgs_format_struct *ovfmt;
	struct sc035hgs_win_size *wsize;
	struct sc035hgs_info *info = to_state(sd);
	int ret;

	if (format->pad)
		return -EINVAL;

	return 0;
}

static int sc035hgs_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct sc035hgs_info *info = to_state(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;
	int ret = 0;

	if(!info->win) {
		dev_err(sd->dev, "sensor win_size not set!\n");
		return -EINVAL;
	}

	fmt->width = info->win->width;
	fmt->height = info->win->height;
	fmt->code = info->win->mbus_code;
	fmt->colorspace = info->win->colorspace;
	*(unsigned int *)fmt->reserved = &info->win->sensor_info;


	//	printk("----%s, %d, width: %d, height: %d, code: %x\n",
	//			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int sc035hgs_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int sc035hgs_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int sc035hgs_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int sc035hgs_s_vflip(struct v4l2_subdev *sd, int value)
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
static int sc035hgs_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int sc035hgs_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	// printk("---%s, %d, s_gain: value: %d\n", __func__, __LINE__, value);

	return ret;
}

static unsigned int again_to_regval(int gain)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(sc035hgs_again_lut); i++) {
		lut = &sc035hgs_again_lut[i];

		if(gain <= lut->gain) {
			return lut->value;
		}
	}

	/*last value.*/
	return lut->value;
}

static int regval_to_again(unsigned int regval)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(sc035hgs_again_lut); i++) {
		lut = &sc035hgs_again_lut[i];

		if(regval == lut->value) {
			return lut->gain;
		}
	}
	// printk("%s, %d regval not mapped to isp gain\n", __func__, __LINE__);
}

static int sc035hgs_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;

	struct sc035hgs_info *info = to_state(sd);
	char v = 0;
	unsigned int reg_val = 0;


	ret += sc035hgs_read(sd, 0x3e09, &v);

	reg_val |= v;
	ret += sc035hgs_read(sd, 0x3e08, &v);
	reg_val |= v << 8;


	*value = regval_to_again(reg_val);
	printk("!!!  reg_value = [%#x]  value = [%d] \n",reg_val,value);

	return ret;
}
/*set analog gain db value, map value to sensor register.*/
static int sc035hgs_s_again(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
#if 1
	struct sc035hgs_info *info = to_state(sd);
	unsigned int reg_value = 0;
	int i;

//	printk("##### %s:%d  again=[%d] \n",__func__,__LINE__,value);
	if(value < info->again->minimum || value > info->again->maximum) {
		/* use default value. */
		reg_value = again_to_regval(info->again->default_value);
	} else {
		reg_value = again_to_regval(value);
	}

	ret += sc035hgs_write(sd, 0x3e09, (unsigned char)(reg_value & 0xff));
	ret += sc035hgs_write(sd, 0x3e08, (unsigned char)((reg_value>>8) & 0xff));
	if (ret < 0)
		return ret;

#endif
	return ret;
}

/*
 * Tweak autogain.
 */
static int sc035hgs_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int sc035hgs_s_exp(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
#if 1
//	printk("##### %s:%d  exp=[%d] \n",__func__,__LINE__,value);
	struct sc035hgs_info *info = to_state(sd);

	ret += sc035hgs_write(sd, 0x3e01, (unsigned char)((value >> 4) & 0xff));
	ret += sc035hgs_write(sd, 0x3e02, ((unsigned char)(value & 0xf)) << 4);
#endif

	return ret;
}

static int sc035hgs_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct sc035hgs_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_AUTOGAIN:
			return sc035hgs_g_gain(sd, &info->gain->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return sc035hgs_g_again(sd, &info->again->val);
	}
	return -EINVAL;
}

static int sc035hgs_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct sc035hgs_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			return sc035hgs_s_brightness(sd, ctrl->val);
		case V4L2_CID_CONTRAST:
			return sc035hgs_s_contrast(sd, ctrl->val);
		case V4L2_CID_VFLIP:
			return sc035hgs_s_vflip(sd, ctrl->val);
		case V4L2_CID_HFLIP:
			return sc035hgs_s_hflip(sd, ctrl->val);
		case V4L2_CID_AUTOGAIN:
			/* Only set manual gain if auto gain is not explicitly
			   turned on. */
			if (!ctrl->val) {
				/* sc035hgs_s_gain turns off auto gain */
				return sc035hgs_s_gain(sd, info->gain->val);
			}
			return sc035hgs_s_autogain(sd, ctrl->val);
		case V4L2_CID_GAIN:
			return sc035hgs_s_gain(sd, ctrl->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return sc035hgs_s_again(sd, ctrl->val);
		case V4L2_CID_EXPOSURE:
			return sc035hgs_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops sc035hgs_ctrl_ops = {
	.s_ctrl = sc035hgs_s_ctrl,
	.g_volatile_ctrl = sc035hgs_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int sc035hgs_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = sc035hgs_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int sc035hgs_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	sc035hgs_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

int sc035hgs_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sc035hgs_info *info = to_state(sd);
	int ret = 0;

	if (enable) {

		sc035hgs_write_array(sd, info->win->regs);
		ret = sc035hgs_write_array(sd, sc035hgs_stream_on_mipi);
		pr_debug("sc035hgs stream on\n");

	}
	else {
		ret = sc035hgs_write_array(sd, sc035hgs_stream_off_mipi);
		pr_debug("sc035hgs stream off\n");
	}
	return ret;
}
/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops sc035hgs_core_ops = {
	.reset = sc035hgs_reset,
	.init = sc035hgs_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = sc035hgs_g_register,
	.s_register = sc035hgs_s_register,
#endif
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,

};

static const struct v4l2_subdev_video_ops sc035hgs_video_ops = {
	.s_stream = sc035hgs_s_stream,
};

static const struct v4l2_subdev_pad_ops sc035hgs_pad_ops = {
	//.enum_frame_interval = sc035hgs_enum_frame_interval,
	//.num_frame_size = sc035hgs_enum_frame_size,
	//.enum_mbus_code = sc035hgs_enum_mbus_code,
	.set_fmt = sc035hgs_set_fmt,
	.get_fmt = sc035hgs_get_fmt,
};

static const struct v4l2_subdev_ops sc035hgs_ops = {
	.core = &sc035hgs_core_ops,
	.video = &sc035hgs_video_ops,
	.pad = &sc035hgs_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int sc035hgs_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct v4l2_fract tpf;
	struct v4l2_subdev *sd;
	struct sc035hgs_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	int tmp;

	dev_info(&client->dev, "sc035hgs probing\n");

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->reset.pin = gpio;
		info->reset.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
	v4l2_i2c_subdev_init(sd, client, &sc035hgs_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

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

	sc035hgs_power(sd, 1);
	msleep(10);
	sc035hgs_reset(sd, 1);
#if 1
	/* Make sure it's an sc035hgs */
	ret = sc035hgs_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
				"chip found @ 0x%x (%s) is not an sc035hgs chip.\n",
				client->addr << 1, client->adapter->name);
		return ret;
	}
#endif

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &sc035hgs_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &sc035hgs_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &sc035hgs_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &sc035hgs_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &sc035hgs_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &sc035hgs_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 259142, 1, 65536);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &sc035hgs_ctrl_ops,
			V4L2_CID_EXPOSURE, 1, 0xea6-6, 1, 522);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;

		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &sc035hgs_win_sizes[0];

	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&info->sd.entity, 1, &info->pad, 0);
	if(ret < 0) {
		goto err_entity_init;
	}
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "sc035hgs Probed\n");
	return 0;
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}


static int sc035hgs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc035hgs_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id sc035hgs_id[] = {
	{ "sc035hgs", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc035hgs_id);

static const struct of_device_id sc035hgs_of_match[] = {
	{.compatible = "ovti,sc035hgs", },
	{},
};
MODULE_DEVICE_TABLE(of, ov2640_of_match);


static struct i2c_driver sc035hgs_driver = {
	.driver = {
		.name	= "sc035hgs",
		.of_match_table = of_match_ptr(sc035hgs_of_match),
	},
	.probe		= sc035hgs_probe,
	.remove		= sc035hgs_remove,
	.id_table	= sc035hgs_id,
};

module_i2c_driver(sc035hgs_driver);
MODULE_AUTHOR("qpz <aric.pzqi@ingenic.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision sc035hgs sensors");
MODULE_LICENSE("GPL");
