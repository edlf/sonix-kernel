/*
 * wy827 Camera Driver
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

#include <media/v4l2-clk.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-device.h>

#include <linux/clk.h>
#include <linux/regulator/consumer.h>


#define  wy827_DEFAULT_WIDTH    640
#define  wy827_DEFAULT_HEIGHT   512


/*
 * Struct
 */
struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

struct mode_list {
	u16 index;
	const struct regval_list *mode_regs;
};

struct wy827_win_size {
	char *name;
	int width;
	int height;
	const struct regval_list *regs;
	unsigned int mbus_code;
};

struct wy827_gpio {
	int pin;
	int active_level;
};

struct wy827_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	struct v4l2_clk			*clk;
	struct v4l2_clk			*mclk;
	const struct wy827_win_size	*win;

	int				model;
	u16				balance_value;
	u16				effect_value;
	u16				flag_vflip:1;
	u16				flag_hflip:1;

	struct v4l2_subdev_platform_data *sd_pdata;
	struct gpio_desc *resetb_gpio;
	struct gpio_desc *vcc_en_gpio;
	int (*power)(struct i2c_client *client, int on);
	int (*reset)(struct i2c_client *client);

	struct regulator *reg;
};

unsigned short wy827_read_reg(struct i2c_client *client, unsigned char reg, unsigned char *value)
{
	struct i2c_msg msg[2] = {
		[0] = {
			.addr   = client->addr,
			.flags  = 0,
			.len    = 1,
			.buf    = &reg,
		},
		[1] = {
			.addr   = client->addr,
			.flags  = I2C_M_RD,
			.len    = 1,
			.buf    = value,
		}
	};
	int ret;
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int wy827_write_reg(struct i2c_client *client, unsigned char reg, unsigned short value)
{
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr   = client->addr,
		.flags  = 0,
		.len    = 2,
		.buf    = buf,
	};
	int ret;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;

}



static struct wy827_win_size wy827_supported_win_sizes[] = {
	{
		.name = "bt656",
		.width = 640,
		.height = 512,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(wy827_supported_win_sizes))

/*
 * General functions
 */
static struct wy827_priv *to_wy827(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct wy827_priv,
			    subdev);
}

static int wy827_reset(struct i2c_client *client)
{
	struct wy827_priv *priv = to_wy827(client);

	if(priv->resetb_gpio) {
		gpiod_direction_output(priv->resetb_gpio, 1);
		msleep(10);
		gpiod_direction_output(priv->resetb_gpio, 0);
		msleep(10);
	}
	return 0;
}

static struct regval_list wy827_stream_on[] = {
};

static struct regval_list wy827_stream_off[] = {
};

/*video_ops*/
static int wy827_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);

	if (!enable ) {
		dev_info(&client->dev, "stream down\n");
		return 0;
	}

	dev_info(&client->dev, "stream on\n");

	return 0;
}

static int wy827_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= wy827_DEFAULT_WIDTH;
	a->bounds.height		= wy827_DEFAULT_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int wy827_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left	= 0;
	a->c.top	= 0;
	a->c.width	= wy827_DEFAULT_WIDTH;
	a->c.height	= wy827_DEFAULT_HEIGHT;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int wy827_g_mbus_config(struct v4l2_subdev *sd,
		struct v4l2_mbus_config *cfg)
{
#if 1
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_FALLING | V4L2_MBUS_MASTER |
		V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_BT656;
#endif
	return 0;
}

static int wy827_g_queystd(struct v4l2_subdev *sd, v4l2_std_id *std){
	return 0;
}

/*ctrl_ops*/
static int wy827_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd =
		&container_of(ctrl->handler, struct wy827_priv, hdl)->subdev;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct wy827_priv *priv = to_wy827(client);

	switch (ctrl->id) {

	case V4L2_CID_VFLIP:
		ctrl->val = priv->flag_vflip;
		break;
	case V4L2_CID_HFLIP:
		ctrl->val = priv->flag_hflip;
		break;
	default:
		break;
	}
	return 0;
}

static int wy827_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	return ret;
}

static int wy827_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct wy827_priv *priv = to_wy827(client);

	if(priv->sd_pdata) {
		if(on)
			return regulator_bulk_enable(priv->sd_pdata->num_regulators,priv->sd_pdata->regulators);
		else
			return regulator_bulk_disable(priv->sd_pdata->num_regulators,priv->sd_pdata->regulators);
	} else if(priv->power){
		return priv->power(client, on);
	} else {
		dev_err(&client->dev, "wy827_s_power failed");
		return -EINVAL;
	}
}

/* Select the nearest higher resolution for capture */
static const struct wy827_win_size *wy827_select_win(u32 *code, u32 *width, u32 *height)
{
	int i, default_size = ARRAY_SIZE(wy827_supported_win_sizes) - 1;

	for (i = 0; i < ARRAY_SIZE(wy827_supported_win_sizes); i++) {
		if ((*width >= wy827_supported_win_sizes[i].width) &&
		    (*height >= wy827_supported_win_sizes[i].height) &&
		    (*code == wy827_supported_win_sizes[i].mbus_code)) {
			*width = wy827_supported_win_sizes[i].width;
			*height = wy827_supported_win_sizes[i].height;
			return &wy827_supported_win_sizes[i];
		}
	}

	return NULL;
}

static int wy827_set_params(struct i2c_client *client, u32 *width, u32 *height, u32 code)
{
	struct wy827_priv       *priv = to_wy827(client);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	int ret;

	int bala_index = priv->balance_value;
	int effe_index = priv->effect_value;


	/* initialize the sensor with default data */
	dev_dbg(&client->dev, "%s: Init default", __func__);

	/* set size win */
	dev_dbg(&client->dev, "priv->win.width = %d\n", priv->win->width);
	dev_dbg(&client->dev, "priv->win.height = %d\n", priv->win->height);


	priv->cfmt_code = code;
	*width = priv->win->width;
	*height = priv->win->height;

	return 0;

err:
	dev_err(&client->dev, "%s: Error %d", __func__, ret);
	priv->win = NULL;

	return ret;
}

/*pad_ops*/
static int wy827_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct wy827_priv *priv = to_wy827(client);

	if(!priv->win)
		return -EINVAL;

	sel->r.top = 0;
	sel->r.left = 0;
	sel->r.width = priv->win->width;
	sel->r.height = priv->win->height;

	return 0;
}

static int wy827_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct wy827_priv *priv = to_wy827(client);

	if (format->pad)
		return -EINVAL;

	if(priv->win){
		mf->width = priv->win->width;
		mf->height = priv->win->height;
	} else {
		mf->width = wy827_DEFAULT_WIDTH;
		mf->height = wy827_DEFAULT_HEIGHT;
	}
	mf->code = priv->cfmt_code;

	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;//Image 逐行输出

	return 0;
}

static int wy827_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct wy827_priv       *priv = to_wy827(client);

	if (format->pad)
		return -EINVAL;

	priv->win = wy827_select_win(&mf->code, &mf->width, &mf->height);
	if(!priv->win)
		return -EINVAL;

	mf->field	= V4L2_FIELD_NONE;//Image 逐行输出
	mf->colorspace = V4L2_COLORSPACE_JPEG;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		wy827_set_params(client, &mf->width,
					 &mf->height, mf->code);
	if(cfg)
		cfg->try_fmt = *mf;
	return 0;
}

static int wy827_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(wy827_supported_win_sizes))
		return -EINVAL;

	code->code = wy827_supported_win_sizes[code->index].mbus_code;
	return 0;
}

static int wy827_enum_frame_size(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_frame_size_enum *fse)
{
	int i, j;
	int num_valid = -1;
	__u32 index = fse->index;

	if(index >= N_WIN_SIZES)
		return -EINVAL;

	fse->code = wy827_supported_win_sizes[index].mbus_code;
	fse->min_width = wy827_supported_win_sizes[index].width;
	fse->max_width = fse->min_width;
	fse->min_height = wy827_supported_win_sizes[index].height;
	fse->max_height = fse->min_height;
	return 0;

}

static int wy827_detect(struct i2c_client *client, unsigned int *ident)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	unsigned char v;
	int ret;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	return 0;

}

static const struct v4l2_ctrl_ops wy827_ctrl_ops = {
	.s_ctrl = wy827_s_ctrl,
	.g_volatile_ctrl = wy827_g_ctrl,
};

static struct v4l2_subdev_core_ops wy827_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
#endif
};

static struct v4l2_subdev_video_ops wy827_subdev_video_ops = {
	.s_stream	= wy827_s_stream,
	.g_mbus_config	= wy827_g_mbus_config,
	.querystd	= wy827_g_queystd,
};

static const struct v4l2_subdev_pad_ops wy827_subdev_pad_ops = {
	.enum_mbus_code = wy827_enum_mbus_code,
	.enum_frame_size = wy827_enum_frame_size,
	.get_fmt	= wy827_get_fmt,
	.set_fmt	= wy827_set_fmt,
	.get_selection	= wy827_get_selection,
};

static struct v4l2_subdev_ops wy827_subdev_ops = {
	.core	= &wy827_subdev_core_ops,
	.video	= &wy827_subdev_video_ops,
	.pad	= &wy827_subdev_pad_ops,
};

/* OF probe functions */
static int wy827_hw_power(struct i2c_client *client, int on)
{
	struct wy827_priv *priv = to_wy827(client);

	dev_dbg(&client->dev, "%s: %s the camera\n",
			__func__, on ? "ENABLE" : "DISABLE");

	/* thses gpio should be set according to the active level in dt defines */
	if(priv->vcc_en_gpio) {
		gpiod_direction_output(priv->vcc_en_gpio, !on);
	}


	msleep(10);
	return 0;
}

static int wy827_hw_reset(struct device *dev)
{
}

static int wy827_probe_dt(struct i2c_client *client,
		struct wy827_priv *priv)
{
	struct v4l2_subdev_platform_data *sd_pdata = priv->sd_pdata;
	struct device_node *np = client->dev.of_node;
	int supplies = 0, index = 0;

	supplies = of_property_count_strings(np, "supplies-name");
	if(supplies > 0) {

		sd_pdata = devm_kzalloc(&client->dev, sizeof(struct v4l2_subdev_platform_data), GFP_KERNEL);
		sd_pdata->num_regulators = supplies;
		sd_pdata->regulators = devm_kzalloc(&client->dev, supplies * sizeof(struct regulator_bulk_data), GFP_KERNEL);
		if(!sd_pdata->regulators) {
			dev_err(&client->dev, "Failed to allocate regulators.!\n");
			devm_kfree(&client->dev,sd_pdata);
			return -ENOMEM;
		}

		for(index = 0; index < sd_pdata->num_regulators; index ++) {
			of_property_read_string_index(np, "supplies-name", index,
					&(sd_pdata->regulators[index].supply));

			dev_dbg(&client->dev, "sd_pdata->regulators[%d].supply: %s\n",
					index, sd_pdata->regulators[index].supply);
		}

		devm_regulator_bulk_get(&client->dev, sd_pdata->num_regulators, sd_pdata->regulators);

	} else {


		/* Request the power vcc-en GPIO asserted */
		priv->vcc_en_gpio = devm_gpiod_get_optional(&client->dev, "vcc-en",
				GPIOD_OUT_HIGH);
		if (!priv->vcc_en_gpio)
			dev_dbg(&client->dev, "vcc_en gpio is not assigned!\n");
		else if (IS_ERR(priv->vcc_en_gpio))
			return PTR_ERR(priv->vcc_en_gpio);

		/* Request the power resetb GPIO asserted */
		priv->resetb_gpio = devm_gpiod_get_optional(&client->dev, "resetb",
				GPIOD_OUT_LOW);
		if (!priv->resetb_gpio)
			dev_dbg(&client->dev, "vcc_en gpio is not assigned!\n");
		else if (IS_ERR(priv->resetb_gpio))
			return PTR_ERR(priv->resetb_gpio);

	}
	return 0;
}

static int wy827_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct wy827_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int	ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"wy827: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct wy827_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&adapter->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	priv->mclk = v4l2_clk_get(&client->dev, "gate_cim_mclk");
	if (IS_ERR(priv->clk)){
		dev_err(&adapter->dev, "mclk_get err!\n");
		return -EPROBE_DEFER;
	}

	ret = v4l2_clk_enable(priv->mclk);
	if (ret){
		dev_err(&adapter->dev, "mclk_enable err!\n");
		return -EPROBE_DEFER;
	}

	priv->clk = v4l2_clk_get(&client->dev, "div_cim");
	if (IS_ERR(priv->clk))
		return -EPROBE_DEFER;

	v4l2_clk_set_rate(priv->clk, 24000000);


	ret = wy827_probe_dt(client, priv);

	v4l2_i2c_subdev_init(&priv->subdev, client, &wy827_subdev_ops);

	/* add handler */
	v4l2_ctrl_handler_init(&priv->hdl, 2);

	v4l2_ctrl_new_std(&priv->hdl, &wy827_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &wy827_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);

	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error) {
		ret = priv->hdl.error;
		goto err_hdl;
	}

#if 1
	ret = wy827_detect(client, NULL);
	if(ret < 0){
		v4l_err(client,"chip found @ 0x%x (%s) is not an wy827 chip.\n",
				client->addr << 1, client->adapter->name);
		goto err_detect;
	}
#endif

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto err_async_register_subdev;

	dev_info(&adapter->dev, "wy827 probed\n");

	return 0;

err_async_register_subdev:
err_detect:
	v4l2_ctrl_handler_free(&priv->hdl);
err_hdl:
	v4l2_device_unregister_subdev(&priv->subdev);
err_probe_dt:
	v4l2_clk_put(priv->clk);
	v4l2_clk_put(priv->mclk);
	devm_kfree(&client->dev, priv);
	return ret;
}

static int wy827_remove(struct i2c_client *client)
{
	struct wy827_priv       *priv = to_wy827(client);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->subdev);
	v4l2_clk_put(priv->clk);
	devm_kfree(&client->dev, priv);
	return 0;
}

static const struct i2c_device_id wy827_id[] = {
	{ "wy827",  0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wy827_id);
static const struct of_device_id wy827_of_match[] = {
	{.compatible = "bt656,wy827", },
	{},
};
MODULE_DEVICE_TABLE(of, wy827_of_match);
static struct i2c_driver wy827_i2c_driver = {
	.driver = {
		.name = "wy827",
		.of_match_table = of_match_ptr(wy827_of_match),
	},
	.probe    = wy827_probe,
	.remove   = wy827_remove,
	.id_table = wy827_id,
};
module_i2c_driver(wy827_i2c_driver);

MODULE_DESCRIPTION("Camera driver for wy827 sensor");
MODULE_LICENSE("GPL v2");
