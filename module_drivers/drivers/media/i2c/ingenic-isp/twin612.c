/*
 * A V4L2 driver for OmniVision TWIN612 cameras.
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

#include <isp-sensor.h>

#define TWIN612_REG_END		0xff

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct twin612_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;

};

struct twin612_gpio {
	int pin;
	int active_level;
};

struct twin612_supply{
	struct regulator *avdd;
	struct regulator *dvdd;
	struct regulator *dovdd;
};

struct twin612_info {
	struct twin612_supply supply;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;

	struct v4l2_clk *clk;

	struct v4l2_ctrl *exposure;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct twin612_win_size *win;

	struct twin612_gpio reset;
};


/*
 * the part of driver maybe modify about different sensor and different board.
 */

static inline struct twin612_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct twin612_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct twin612_info, hdl)->sd;
}


struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

static struct regval_list twin612_init_dvp[] = {
	{TWIN612_REG_END, 0x00},	/* END MARKER */
};



/*
 * Stuff that knows about the sensor.
 */
static int twin612_reset(struct v4l2_subdev *sd, u32 val)
{
	struct twin612_info *info = to_state(sd);

	return 0;
}

static int twin612_ircut(struct v4l2_subdev *sd, u32 val)
{
	struct twin612_info *info = to_state(sd);
	return 0;
}

static int twin612_init(struct v4l2_subdev *sd, u32 val)
{
	struct ov2735b_info *info = to_state(sd);
	return 0;
}



static int twin612_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	return 0;
}

#if 1
static struct twin612_win_size twin612_win_sizes[] = {
	{
		.sensor_info.fps			= 25 << 16 | 1,

		.width		= 640,
		.height		= 512,
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		//.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		//.colorspace	= V4L2_COLORSPACE_JPEG,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= twin612_init_dvp,
	}
};
#else
static struct twin612_win_size twin612_win_sizes[] = {
	{
		.sensor_info.fps			= 25 << 16 | 1,

		.width		= 640,
		.height		= 512,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= twin612_init_dvp,
	}
};
#endif

static int twin612_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
#if 0
	if (code->pad || code->index >= N_TWIN612_FMTS)
		return -EINVAL;

	code->code = twin612_formats[code->index].mbus_code;
#endif
	return 0;
}

/*
 * Set a format.
 */
static int twin612_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct twin612_format_struct *ovfmt;
	struct twin612_win_size *wsize;
	struct twin612_info *info = to_state(sd);
	int ret;
	if (format->pad)
		return -EINVAL;

	return 0;
}

static int twin612_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format)
{
	struct twin612_info *info = to_state(sd);
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

	/*printk("----%s, %d, width: %d, height: %d, code: %x\n",
			__func__, __LINE__, fmt->width, fmt->height, fmt->code);*/

	return ret;
}

static int twin612_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct twin612_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return 0;
	case V4L2_CID_ANALOGUE_GAIN:
		return 0;
	}
	return -EINVAL;
}

static int twin612_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct twin612_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_GAIN:
	case V4L2_CID_ANALOGUE_GAIN:
	case V4L2_CID_EXPOSURE:
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops twin612_ctrl_ops = {
	.s_ctrl = twin612_s_ctrl,
	.g_volatile_ctrl = twin612_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int twin612_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	return 0;
}

static int twin612_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	return 0;
}
#endif

static int twin612_power(struct v4l2_subdev *sd, int on)
{
	struct twin612_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	return 0;
}

int twin612_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct twin612_info *info = to_state(sd);
	int ret = 0;

	if (enable) {
		pr_debug("twin612 stream on\n");

	}
	else {
		pr_debug("twin612 stream off\n");
	}
	return 0;
}

int twin612_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	struct twin612_info *info = to_state(sd);

	if(info->win->sensor_info.fps){
		param->parm.capture.timeperframe.numerator = info->win->sensor_info.fps & 0xffff;
		param->parm.capture.timeperframe.denominator = info->win->sensor_info.fps >> 16;
		return 0;
	}
	return -EINVAL;
}
/* ----------------------------------------------------------------------- */
static int twin612_g_mbus_config(struct v4l2_subdev *sd,
		struct v4l2_mbus_config *cfg)
{
//	struct i2c_client *client = v4l2_get_subdevdata(sd);
//	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
//
//	printk("###########%s in\n", __FUNCTION__);
//
//	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_MASTER |
//		V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_HIGH |
//		V4L2_MBUS_DATA_ACTIVE_HIGH;
//	cfg->type = V4L2_MBUS_PARALLEL;
//
//	cfg->flags = soc_camera_apply_board_flags(ssdd, cfg);
//	printk("###########%s out\n", __FUNCTION__);
	return 0;
}

static const struct v4l2_subdev_core_ops twin612_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = twin612_g_register,
	.s_register = twin612_s_register,
#endif
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,

};

static const struct v4l2_subdev_video_ops twin612_video_ops = {
	.s_stream = twin612_s_stream,
	.g_parm	= twin612_g_parm,
	.g_mbus_config	= twin612_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops twin612_pad_ops = {
	//.enum_frame_interval = twin612_enum_frame_interval,
	//.num_frame_size = twin612_enum_frame_size,
	//.enum_mbus_code = twin612_enum_mbus_code,
	.set_fmt = twin612_set_fmt,
	.get_fmt = twin612_get_fmt,
};

static const struct v4l2_subdev_ops twin612_ops = {
	.core = &twin612_core_ops,
	.video = &twin612_video_ops,
	.pad = &twin612_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int twin612_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct twin612_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	v4l2_i2c_subdev_init(sd, client, &twin612_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	twin612_power(sd,1);

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
	/* Make sure it's an twin612 */
	ret = twin612_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
			"chip found @ 0x%x (%s) is not an twin612 chip.\n",
			client->addr << 1, client->adapter->name);
		return ret;
	}
#endif
	/*IRCUT ctl 0:off 1:on*/
	twin612_ircut(sd, 0);

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &twin612_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &twin612_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &twin612_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &twin612_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &twin612_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &twin612_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 259142, 1, 10000);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &twin612_ctrl_ops,
			V4L2_CID_EXPOSURE, 4, 1899 - 4, 1, 1500);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;

		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &twin612_win_sizes[0];
	twin612_init(sd, 1);

	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&info->sd.entity, 1, &info->pad, 0);
	if(ret < 0) {
		goto err_entity_init;
	}
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "twin612 Probed\n");
	return 0;
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}


static int twin612_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct twin612_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id twin612_id[] = {
	{ "twin612", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, twin612_id);

static const struct of_device_id twin612_of_match[] = {
	{.compatible = "gd,twin612", },
	{},
};
MODULE_DEVICE_TABLE(of, twin612_of_match);


static int twin612_suspend(struct device *dev)
{
        struct i2c_client *client = container_of(dev, struct i2c_client, dev);
        struct v4l2_subdev *sd = i2c_get_clientdata(client);
        struct twin612_info *info = to_state(sd);
	int ret=0;

	twin612_power(sd,0);
	v4l2_clk_disable(info->clk);
        return 0;
}

static int twin612_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct twin612_info *info = to_state(sd);
	int ret=0;

	twin612_power(sd,1);
	v4l2_clk_enable(info->clk);
	twin612_reset(sd, 1);
	twin612_init(sd, 1);
	return 0;
}

const struct dev_pm_ops twin612_pm =
{
        .suspend = twin612_suspend,
        .resume  = twin612_resume,
};

static struct i2c_driver twin612_driver = {
	.driver = {
		.name	= "twin612",
		.of_match_table = of_match_ptr(twin612_of_match),
		.pm	=  &twin612_pm,
	},
	.probe		= twin612_probe,
	.remove		= twin612_remove,
	.id_table	= twin612_id,
};

module_i2c_driver(twin612_driver);
MODULE_AUTHOR("qpz <aric.pzqi@ingenic.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision twin612 sensors");
MODULE_LICENSE("GPL");
