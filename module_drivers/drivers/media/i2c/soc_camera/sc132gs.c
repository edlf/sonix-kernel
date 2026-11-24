/*
 * sc132gs Camera Driver
 *
 * Copyright (C) 2010 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 *
 * Based on ov772x, ov9640 drivers and previous non merged implementations.
 *
 * Copyright 2005-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2006, OmniVision
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define DEBUG

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/soc_camera.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-image-sizes.h>

#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#define REG_CHIP_ID_HIGH	0x3107
#define REG_CHIP_ID_LOW		0x3108
#define CHIP_ID_HIGH		0x01
#define CHIP_ID_LOW		0x32
#define SC031_REG_DELAY		0xfe

#define  sc132gs_DEFAULT_WIDTH    1080
#define  sc132gs_DEFAULT_HEIGHT   1280

/*
 * Struct
 */
struct regval_list {
	u16 reg_num;
	u16 value;
};

struct mode_list {
	u16 index;
	const struct regval_list *mode_regs;
};

/* Supported resolutions */
enum sc132gs_width {
	W_VGA	= sc132gs_DEFAULT_WIDTH,
};

enum sc132gs_height {
	H_VGA	= sc132gs_DEFAULT_HEIGHT,
};

struct sc132gs_win_size {
	char *name;
	enum sc132gs_width width;
	enum sc132gs_height height;
	const struct regval_list *regs;
};

struct sc132gs_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	struct v4l2_clk			*clk;
	const struct sc132gs_win_size	*win;

	int				model;
	u16				balance_value;
	u16				effect_value;
	u16				flag_vflip:1;
	u16				flag_hflip:1;

	struct soc_camera_subdev_desc	ssdd_dt;
	struct gpio_desc *avdd_gpio;
	struct gpio_desc *dvdd_gpio;
	struct gpio_desc *dovdd_gpio;
	struct gpio_desc *resetb_gpio;
	struct gpio_desc *pwdn_gpio;
	struct gpio_desc *vcc_en_gpio;

	struct regulator *reg;
	struct regulator *reg1;
};

static int sc132gs_s_power(struct v4l2_subdev *sd, int on);
static inline int sensor_i2c_master_send(struct i2c_client *client,
		const char *buf ,int count)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *)buf;

	ret = i2c_transfer(adap, &msg, 1);

	return (ret == 1) ? count : ret;
}

static inline int sensor_i2c_master_recv(struct i2c_client *client,
		char *buf ,int count)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = buf;
	ret = i2c_transfer(adap, &msg, 1);

	return (ret == 1) ? count : ret;
}


unsigned char sc132gs_read_reg(struct i2c_client *client, u16 reg)
{
	int ret;
	unsigned char retval;
	unsigned short r = cpu_to_be16(reg);

	ret = sensor_i2c_master_send(client,(u8 *)&r,2);

	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	ret = sensor_i2c_master_recv(client, &retval, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;
	return retval;
}

int sc132gs_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	unsigned char msg[3];
	int ret;

	reg = cpu_to_be16(reg);

	memcpy(&msg[0], &reg, 2);
	memcpy(&msg[2], &val, 1);

	ret = sensor_i2c_master_send(client, msg, 3);

	if (ret < 0)
	{
		return ret;
	}
	if (ret < 3)
	{
		return -EIO;
	}

	return 0;
}

/*
 * Registers settings
 */

#define ENDMARKER { 0xffff, 0xff }

static const struct regval_list sc132gs_init_regs[] = {
	//sc132gs_setting_1080x1280
	{0x0103,0x01},
	{0x0100,0x00},
	//PLL bypass
	{0x36e9,0x80},
	{0x36f9,0x80},
	{0x0100,0x00},
	{0x3018,0x32},
	{0x3019,0x0c},
	{0x301a,0xb4},
	{0x3031,0x08}, // raw8
//	{0x3031,0x0a}, // raw10
	{0x3032,0x60},
	////////
//	{0x3037,0x20},//Add xtang ,phy mode:4’h8~ raw8 mode,4’hc~ raw12 mode,others~ raw10 mode
	////////
	{0x3038,0x44},
	{0x3207,0x17},
	{0x320c,0x05},
	{0x320d,0x35},
	{0x320e,0x08}, //0x05
	{0x320f,0x46}, //0x46
	{0x3250,0xcc},
	{0x3251,0x02},
	{0x3252,0x05},
	{0x3253,0x41},
	{0x3254,0x05},
	{0x3255,0x3b},
	{0x3306,0x78},
	{0x330a,0x00},
	{0x330b,0xc8},
	{0x330f,0x24},
	{0x3314,0x80},
	{0x3315,0x40},
	{0x3317,0xf0},
	{0x331f,0x12},
	{0x3361,0x40},
	{0x3364,0x00},
	{0x3385,0x41},
	{0x3387,0x41},
	{0x3389,0x09},
	{0x33ab,0x00},
	{0x33ac,0x00},
	{0x33b1,0x03},
	{0x33b2,0x12},
	{0x33f8,0x02},
	{0x33fa,0x01},
	{0x3409,0x08},
	{0x34f0,0xc0},
	{0x34f1,0x20},
	{0x34f2,0x03},
	{0x3622,0xf5},
	{0x3630,0x5c},
	{0x3631,0x80},
	{0x3632,0xc8},
	{0x3633,0x32},
	{0x3638,0x2a},
	{0x3639,0x07},
	{0x363b,0x48},
	{0x363c,0x83},
	{0x363d,0x10},
	{0x36ea,0x38},
	{0x36fa,0x25},
	{0x36fb,0x05},
	{0x36fd,0x04},
	{0x3900,0x11},
	{0x3901,0x05},
	{0x3902,0xc5},
	{0x3904,0x04},
	{0x3908,0x91},
	{0x391e,0x00},
	{0x3e01,0x53},
	{0x3e02,0xe0},
	{0x3e06,0x00},//0x07//Bit[3:0]: digital gain
	{0x3e08,0x03},//1 gain
	{0x3e09,0x20},//
	{0x3e0e,0xd2},
	{0x3e14,0xb0},
	{0x3e1e,0x7c},
	{0x3e26,0x20},
	{0x4418,0x38},
	//{0x4501,0x08}, // test Bit[3]: incremental pattern
	{0x4503,0x10},
	{0x4837,0x21},
	{0x5000,0x0e},
	{0x540c,0x51},
	{0x550f,0x38},
	{0x5780,0x67},
	{0x5784,0x10},
	{0x5785,0x06},
	{0x5787,0x02},
	{0x5788,0x00},
	{0x5789,0x00},
	{0x578a,0x02},
	{0x578b,0x00},
	{0x578c,0x00},
	{0x5790,0x00},
	{0x5791,0x00},
	{0x5792,0x00},
	{0x5793,0x00},
	{0x5794,0x00},
	{0x5795,0x00},
	{0x5799,0x04},
	//PLL set
	{0x36e9,0x20},
	{0x36f9,0x24},
	{0x0100,0x01},
	{0x33fa,0x02},
	{0x3317,0x0a},
	{0x0100,0x01},
	{0x3221,0x66}, // flip,mirror on
	//{0x3221,0x00}, // flip,mirror off


	{0x3e03,0x0b },//analog gain mode select

//	{SC031_REG_DELAY,10},
	ENDMARKER,
};

static const struct regval_list sc132gs_10bit_regs[] = {
    {0x0103,0x01},
    {0x0100,0x00},
    {0x36e9,0x80},
    {0x36f9,0x80},
    {0x3018,0x32},
    {0x3019,0x0c},
    {0x301a,0xb4},
    {0x301f,0x42},
    {0x3032,0x60},
    {0x3038,0x44},
    {0x3207,0x17},
    {0x320c,0x02},
    {0x320d,0xee},
    {0x320e,0x05},
    {0x320f,0x78},
    {0x3250,0xcc},
    {0x3251,0x02},
    {0x3252,0x05},
    {0x3253,0x73},
    {0x3254,0x05},
    {0x3255,0x3b},
    {0x3306,0x78},
    {0x330a,0x00},
    {0x330b,0xc8},
    {0x330f,0x24},
    {0x3314,0x80},
    {0x3315,0x40},
    {0x3317,0xf0},
    {0x331f,0x12},
    {0x3364,0x00},
    {0x3385,0x41},
    {0x3387,0x41},
    {0x3389,0x09},
    {0x33ab,0x00},
    {0x33ac,0x00},
    {0x33b1,0x03},
    {0x33b2,0x12},
    {0x33f8,0x02},
    {0x33fa,0x01},
    {0x3409,0x08},
    {0x34f0,0xc0},
    {0x34f1,0x20},
    {0x34f2,0x03},
    {0x3622,0xf5},
    {0x3630,0x5c},
    {0x3631,0x80},
    {0x3632,0xc8},
    {0x3633,0x32},
    {0x3638,0x2a},
    {0x3639,0x07},
    {0x363b,0x48},
    {0x363c,0x83},
    {0x363d,0x10},
    {0x36ea,0x36},
    {0x36eb,0x04},
    {0x36ec,0x03},
    {0x36ed,0x24},
    {0x36fa,0x2b},
    {0x36fb,0x0b},
    {0x36fc,0x01},
    {0x36fd,0x34},
    {0x3900,0x11},
    {0x3901,0x05},
    {0x3902,0xc5},
    {0x3904,0x04},
    {0x3908,0x91},
    {0x391e,0x00},
    {0x3e01,0x57},
    {0x3e02,0x00},
    {0x3e09,0x20},
    {0x3e0e,0xd2},
    {0x3e14,0xb0},
    {0x3e1e,0x7c},
    {0x3e26,0x20},
    {0x4418,0x38},
    {0x4503,0x10},
    {0x4837,0x1a},
    {0x5000,0x0e},
    {0x540c,0x51},
    {0x550f,0x38},
    {0x5780,0x67},
    {0x5784,0x10},
    {0x5785,0x06},
    {0x5787,0x02},
    {0x5788,0x00},
    {0x5789,0x00},
    {0x578a,0x02},
    {0x578b,0x00},
    {0x578c,0x00},
    {0x5790,0x00},
    {0x5791,0x00},
    {0x5792,0x00},
    {0x5793,0x00},
    {0x5794,0x00},
    {0x5795,0x00},
    {0x5799,0x04},
    {0x36e9,0x54},
    {0x36f9,0x50},
    {0x0100,0x01},

    
//	[gain<2]
//	{0x33fa,0x01},
//	{0x3317,0xf0},
//
//	[gain>=2]
//	{0x33fa,0x02},
//	{0x3317,0x14},
	ENDMARKER,
};

static const struct regval_list sc132gs_vga_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_720p_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_1080p_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_wb_auto_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_wb_incandescence_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_wb_daylight_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_wb_fluorescent_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_wb_cloud_regs[] = {
	ENDMARKER,
};

static const struct mode_list sc132gs_balance[] = {
	{0, sc132gs_wb_auto_regs}, {1, sc132gs_wb_incandescence_regs},
	{2, sc132gs_wb_daylight_regs}, {3, sc132gs_wb_fluorescent_regs},
	{4, sc132gs_wb_cloud_regs},
};


static const struct regval_list sc132gs_effect_normal_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_effect_grayscale_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_effect_sepia_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_effect_colorinv_regs[] = {
	ENDMARKER,
};

static const struct regval_list sc132gs_effect_sepiabluel_regs[] = {
	ENDMARKER,
};

static const struct mode_list sc132gs_effect[] = {
	{0, sc132gs_effect_normal_regs}, {1, sc132gs_effect_grayscale_regs},
	{2, sc132gs_effect_sepia_regs}, {3, sc132gs_effect_colorinv_regs},
	{4, sc132gs_effect_sepiabluel_regs},
};

#define sc132gs_SIZE(n, w, h, r) \
	{.name = n, .width = w , .height = h, .regs = r }

static struct sc132gs_win_size sc132gs_supported_win_sizes[] = {
	sc132gs_SIZE("VGA", W_VGA, H_VGA, sc132gs_vga_regs),
};

#define N_WIN_SIZES (ARRAY_SIZE(sc132gs_supported_win_sizes))

static u32 sc132gs_codes[] = {
//	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_SBGGR10_1X10,
};

/*
 * General functions
 */
static struct sc132gs_priv *to_sc132gs(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct sc132gs_priv,
			    subdev);
}

static int sc132gs_write_array(struct i2c_client *client,
			      const struct regval_list *vals)
{
	int ret;

	while ((vals->reg_num != 0xffff) || (vals->value != 0xff)) {
		ret = sc132gs_write_reg(client, vals->reg_num, vals->value);
		dev_vdbg(&client->dev, "array: 0x%02x, 0x%02x",
			 vals->reg_num, vals->value);

		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}

static int sc132gs_reset(struct i2c_client *client)
{
	return 0;
}

/*
 * soc_camera_ops functions
 */
static int sc132gs_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);

	if (!enable ) {
		dev_info(&client->dev, "stream down\n");
		sc132gs_write_reg(client, 0x0100, 0x00);
		msleep(50);
		return 0;
	}

	dev_info(&client->dev, "stream on\n");
    sc132gs_write_reg(client, 0x0100, 0x01);
    msleep(50);

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int sc132gs_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	reg->size = 1;
	if (reg->reg > 0xff)
		return -EINVAL;

	ret = sc132gs_read_reg(client, reg->reg);
	if (ret < 0)
		return ret;

	reg->val = ret;

	return 0;
}

static int sc132gs_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff ||
	    reg->val > 0xff)
		return -EINVAL;

	return sc132gs_write_reg(client, reg->reg, reg->val);
}
#endif

/* Select the nearest higher resolution for capture */
static const struct sc132gs_win_size *sc132gs_select_win(u32 *width, u32 *height)
{
	int i, default_size = ARRAY_SIZE(sc132gs_supported_win_sizes) - 1;

	for (i = 0; i < ARRAY_SIZE(sc132gs_supported_win_sizes); i++) {
		if ((*width >= sc132gs_supported_win_sizes[i].width) &&
		    (*height >= sc132gs_supported_win_sizes[i].height)) {
			*width = sc132gs_supported_win_sizes[i].width;
			*height = sc132gs_supported_win_sizes[i].height;
			return &sc132gs_supported_win_sizes[i];
		}
	}

	*width = sc132gs_supported_win_sizes[default_size].width;
	*height = sc132gs_supported_win_sizes[default_size].height;
	return &sc132gs_supported_win_sizes[default_size];
}

static int sc132gs_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct sc132gs_priv *priv = to_sc132gs(client);

	if (format->pad)
		return -EINVAL;

	mf->width = sc132gs_DEFAULT_WIDTH;//priv->win->width;
	mf->height = sc132gs_DEFAULT_HEIGHT;//priv->win->height;
	mf->code = priv->cfmt_code;

	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int sc132gs_set_params(struct i2c_client *client, u32 *width, u32 *height, u32 code)
{
	struct sc132gs_priv       *priv = to_sc132gs(client);
	int ret;

	int bala_index = priv->balance_value;
	int effe_index = priv->effect_value;

	/* select win */
	priv->win = sc132gs_select_win(width, height);

	/* select format */
	priv->cfmt_code = 0;

	/* reset hardware */
	sc132gs_reset(client);

	/* initialize the sensor with default data */
	dev_dbg(&client->dev, "%s: Init default", __func__);
//	ret = sc132gs_write_array(client, sc132gs_init_regs);
	ret = sc132gs_write_array(client, sc132gs_10bit_regs);
	if (ret < 0)
		goto err;

	/* set balance */
	ret = sc132gs_write_array(client, sc132gs_balance[bala_index].mode_regs);
	if (ret < 0)
		goto err;

	/* set effect */
	ret = sc132gs_write_array(client, sc132gs_effect[effe_index].mode_regs);
	if (ret < 0)
		goto err;

	/* set size win */
	ret = sc132gs_write_array(client, priv->win->regs);
	if (ret < 0)
		goto err;

	priv->cfmt_code = code;
	*width = priv->win->width;
	*height = priv->win->height;

	return 0;

err:
	dev_err(&client->dev, "%s: Error %d", __func__, ret);
	sc132gs_reset(client);
	priv->win = NULL;

	return ret;
}

static int sc132gs_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (format->pad)
		return -EINVAL;

	/*
	 * select suitable win, but don't store it
	 */
	sc132gs_select_win(&mf->width, &mf->height);

	mf->field	= V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_JPEG;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return sc132gs_set_params(client, &mf->width,
					 &mf->height, mf->code);
	cfg->try_fmt = *mf;
	return 0;
}

static int sc132gs_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(sc132gs_codes))
		return -EINVAL;

	code->code = sc132gs_codes[code->index];
	return 0;
}

static int sc132gs_enum_frame_size(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_frame_size_enum *fse)
{
	int i, j;
	int num_valid = -1;
	__u32 index = fse->index;

	if(index >= N_WIN_SIZES)
		return -EINVAL;

	j = ARRAY_SIZE(sc132gs_codes);
	while(--j)
		if(fse->code == sc132gs_codes[j])
			break;

	for (i = 0; i < N_WIN_SIZES; i++) {
		if (index == ++num_valid) {
			fse->code = sc132gs_codes[j];
			fse->min_width = sc132gs_supported_win_sizes[index].width;
			fse->max_width = fse->min_width;
			fse->min_height = sc132gs_supported_win_sizes[index].height;
			fse->max_height = fse->min_height;
			return 0;
		}
	}

	return -EINVAL;
}

static int sc132gs_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left	= 0;
	a->c.top	= 0;
	a->c.width	= sc132gs_DEFAULT_WIDTH;
	a->c.height	= sc132gs_DEFAULT_HEIGHT;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int sc132gs_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= sc132gs_DEFAULT_WIDTH;
	a->bounds.height		= sc132gs_DEFAULT_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int sc132gs_video_probe(struct i2c_client *client)
{
	unsigned char retval_high = 0, retval_low = 0;
	struct sc132gs_priv *priv = to_sc132gs(client);
	int ret;

	ret = sc132gs_s_power(&priv->subdev, 1);
	if (ret < 0)
		return ret;
	/*
	 * check and show product ID and manufacturer ID
	 */
//while(1){
	retval_high = sc132gs_read_reg(client, REG_CHIP_ID_HIGH);
	if (retval_high != CHIP_ID_HIGH) {
		dev_err(&client->dev, "read sensor %s chip_id high %x is error\n",
				client->name, retval_high);
		ret = -EINVAL;
		goto done;
	}


	retval_low = sc132gs_read_reg(client, REG_CHIP_ID_LOW);
	if (retval_low != CHIP_ID_LOW) {
		dev_err(&client->dev, "read sensor %s chip_id low %x is error\n",
				client->name, retval_low);
		ret = -EINVAL;
		goto done;
	}

	dev_info(&client->dev, "read sensor %s id high:0x%x,low:%x successed!\n",
			client->name, retval_high, retval_low);
//	msleep(1000);
//}
done:
	sc132gs_s_power(&priv->subdev, 0);
	return ret;
}

static int sc132gs_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct sc132gs_priv *priv = to_sc132gs(client);

	return soc_camera_set_power(&client->dev, ssdd, priv->clk, on);
}

static struct v4l2_subdev_core_ops sc132gs_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= sc132gs_g_register,
	.s_register	= sc132gs_s_register,
#endif
	.s_power	= sc132gs_s_power,
	/* .querymenu	= sc132gs_querymenu, */
};

static int sc132gs_g_mbus_config(struct v4l2_subdev *sd,
		struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_CHANNEL_0 | V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	cfg->flags |= V4L2_MBUS_CSI2_2_LANE;

	return 0;
}

static struct v4l2_subdev_video_ops sc132gs_subdev_video_ops = {
	.s_stream	= sc132gs_s_stream,
	.cropcap	= sc132gs_cropcap,
	.g_crop		= sc132gs_g_crop,
	.g_mbus_config	= sc132gs_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops sc132gs_subdev_pad_ops = {
	.enum_mbus_code = sc132gs_enum_mbus_code,
	.enum_frame_size = sc132gs_enum_frame_size,
	.get_fmt	= sc132gs_get_fmt,
	.set_fmt	= sc132gs_set_fmt,
};

static struct v4l2_subdev_ops sc132gs_subdev_ops = {
	.core	= &sc132gs_subdev_core_ops,
	.video	= &sc132gs_subdev_video_ops,
	.pad	= &sc132gs_subdev_pad_ops,
};

/* OF probe functions */
static int sc132gs_hw_power(struct device *dev, int on)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc132gs_priv *priv = to_sc132gs(client);

	dev_dbg(&client->dev, "%s: %s the camera\n",
			__func__, on ? "ENABLE" : "DISABLE");

	/* thses gpio should be set according to the active level in dt defines */

	if (priv->pwdn_gpio) {
		gpiod_direction_output(priv->pwdn_gpio, on);
	}
	msleep(3);

	return 0;
}

static int sc132gs_hw_reset(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc132gs_priv *priv = to_sc132gs(client);
	int val;
	if (priv->resetb_gpio) {
		/* Active the resetb pin to perform a reset pulse */
		gpiod_direction_output(priv->resetb_gpio, 1);
		val = gpiod_get_value(priv->resetb_gpio);
		usleep_range(3000, 5000);
		gpiod_direction_output(priv->resetb_gpio, 0);
		val = gpiod_get_value(priv->resetb_gpio);
	}

	return 0;
}


static int sc132gs_probe_dt(struct i2c_client *client,
		struct sc132gs_priv *priv)
{

	struct soc_camera_subdev_desc	*ssdd_dt = &priv->ssdd_dt;
	struct v4l2_subdev_platform_data *sd_pdata = &ssdd_dt->sd_pdata;
	struct device_node *np = client->dev.of_node;
	int supplies = 0, index = 0;

	supplies = of_property_count_strings(np, "supplies-name");
	if(supplies <= 0) {
		goto no_supply;
	}

	sd_pdata->num_regulators = supplies;
	sd_pdata->regulators = devm_kzalloc(&client->dev, supplies * sizeof(struct regulator_bulk_data), GFP_KERNEL);
	if(!sd_pdata->regulators) {
		dev_err(&client->dev, "Failed to allocate regulators.!\n");
		goto no_supply;
	}

	for(index = 0; index < sd_pdata->num_regulators; index ++) {
		of_property_read_string_index(np, "supplies-name", index,
				&(sd_pdata->regulators[index].supply));

		dev_dbg(&client->dev, "sd_pdata->regulators[%d].supply: %s\n",
				index, sd_pdata->regulators[index].supply);
	}

	soc_camera_power_init(&client->dev, ssdd_dt);

no_supply:

	/* Request the reset GPIO deasserted */
	priv->resetb_gpio = devm_gpiod_get_optional(&client->dev, "resetb",
			GPIOD_OUT_LOW);
	if (!priv->resetb_gpio)
		dev_dbg(&client->dev, "resetb gpio is not assigned!\n");
	else if (IS_ERR(priv->resetb_gpio))
		return PTR_ERR(priv->resetb_gpio);

	/* Request the power down GPIO asserted */
	priv->pwdn_gpio = devm_gpiod_get_optional(&client->dev, "pwdn",
			GPIOD_OUT_LOW);
	if (!priv->pwdn_gpio)
		dev_dbg(&client->dev, "pwdn gpio is not assigned!\n");
	else if (IS_ERR(priv->pwdn_gpio))
		return PTR_ERR(priv->pwdn_gpio);

	/* Request the power down GPIO asserted */
	priv->vcc_en_gpio = devm_gpiod_get_optional(&client->dev, "vcc-en",
			GPIOD_OUT_HIGH);
	if (!priv->vcc_en_gpio)
		dev_dbg(&client->dev, "vcc_en gpio is not assigned!\n");
	else if (IS_ERR(priv->vcc_en_gpio))
		return PTR_ERR(priv->vcc_en_gpio);

	/* Initialize the soc_camera_subdev_desc */
	priv->ssdd_dt.power = sc132gs_hw_power;
	priv->ssdd_dt.reset = sc132gs_hw_reset;
	client->dev.platform_data = &priv->ssdd_dt;
	return 0;
}

#include <linux/regulator/consumer.h>
/*
 * i2c_driver functions
 */
static int sc132gs_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct sc132gs_priv	*priv;
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int	ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"sc132gs: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct sc132gs_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&adapter->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	priv->clk = v4l2_clk_get(&client->dev, "div_cim");
	if (IS_ERR(priv->clk))
		return -EPROBE_DEFER;

	v4l2_clk_set_rate(priv->clk, 24000000);

	if (!ssdd && !client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!ssdd) {
		ret = sc132gs_probe_dt(client, priv);
		if (ret)
			goto err_clk;
	}
	/* thses gpio should be set according to the active level in dt defines */


	v4l2_i2c_subdev_init(&priv->subdev, client, &sc132gs_subdev_ops);

	ret = sc132gs_video_probe(client);
	if (ret < 0)
		goto err_clk;

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto err_clk;

	dev_info(&adapter->dev, "sc132gs Probed\n");

	return 0;

err_clk:
	v4l2_clk_put(priv->clk);
	return ret;
}

static int sc132gs_remove(struct i2c_client *client)
{
	struct sc132gs_priv       *priv = to_sc132gs(client);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_clk_put(priv->clk);
	v4l2_device_unregister_subdev(&priv->subdev);
	return 0;
}

static const struct i2c_device_id sc132gs_id[] = {
	{ "sc132gs",  0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc132gs_id);
static const struct of_device_id sc132gs_of_match[] = {
	{.compatible = "sc132gs", },
	{},
};
MODULE_DEVICE_TABLE(of, sc132gs_of_match);
static struct i2c_driver sc132gs_i2c_driver = {
	.driver = {
		.name = "sc132gs",
		.of_match_table = of_match_ptr(sc132gs_of_match),
	},
	.probe    = sc132gs_probe,
	.remove   = sc132gs_remove,
	.id_table = sc132gs_id,
};

module_i2c_driver(sc132gs_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for smartsens sc132gs sensor");
MODULE_LICENSE("GPL v2");
