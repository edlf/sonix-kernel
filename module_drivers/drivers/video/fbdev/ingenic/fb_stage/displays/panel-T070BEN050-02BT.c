/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-T070BEN050-02BT.c
 *
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 *
 * This program is free software, you can redistribute it and/or modify it
 *
 * under the terms of the GNU General Public License version 2 as published by
 *
 * the Free Software Foundation.
 */
#if 0
 &dpu {
	status = "okay";
	ingenic,disable-rdma-fb = <1>;
	/*Defines the init state of composer fb export infomations.*/
	ingenic,layer-exported = <1 0 0 0>;
	ingenic,layer-frames   = <2 0 0 0>;
	ingenic,layer-framesize = <800 480>, <800 480>, <320 240>, <320 240>;   /*Max framesize for each layer.*/
	layer,color_mode	= <0 0 0 0>;					/*src fmt,*/
	layer,src-size		= <800 480>, <800 480>, <320 240>, <240 200>;	/*Layer src size should smaller than framesize*/
	layer,target-size	= <800 480>, <800 480>, <160 240>, <240 200>;	/*Target Size should smaller than src_size.*/
	layer,target-pos	= <0 0>, <0 0>, <340 480>, <100 980>;	/*target pos , the start point of the target panel.*/
	layer,enable		= <1 0 0 0>;					/*layer enabled or disabled.*/
	ingenic,logo-pan-layer	= <0>;						/*on which layer should init logo pan on.*/
	port {
	dpu_out_ep: endpoint {
		remote-endpoint = <&panel_T070BEN050_02BT_ep>;
	    };
	};
};

display-dbi {
	compatible = "simple-bus";
	#interrupt-cells = <1>;
	#address-cells = <1>;
	#size-cells = <1>;
	ranges = <>;
	panel_T070BEN050_02BT {
		compatible = "ingenic,T070BEN050_02BT";
		status = "okay";
		pinctrl-names = "default";
		pinctrl-0 = <&tft_lcd_pb_rgb888>;
		/*ingenic,vdd-en-gpio = <&gpc 3 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>; */
		ingenic,rst-gpio = <&gpe 4 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
		ingenic,pwm-gpio = <&gpe 3 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
		port {
			panel_T070BEN050_02BT_ep: endpoint {
			remote-endpoint = <&dpu_out_ep>;
			};
		};
	};
};

backlight {
	compatible = "pwm-backlight";
	pwms = <&pwm 5 1000000>; /* arg1: pwm channel id [0~15]. arg2: period in ns. */
	brightness-levels = <0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15>;
	default-brightness-level = <5>;

};
#endif

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/of_gpio.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <soc/gpio.h>
#include "../ingenicfb.h"



struct board_gpio {
	short gpio;
	short active_level; //有效电平
};

struct panel_dev {
	/* ingenic frame buffer */
	struct device *dev;
	struct lcd_panel *panel;//液晶显示屏，控制板

	/* common lcd framework */
	struct lcd_device *lcd;
	struct backlight_device *backlight;
	int power;

	struct regulator *vcc;
	struct board_gpio vdd_en;
	struct board_gpio rst;
	struct board_gpio pwm;
};

static void panel_enable(struct lcd_panel *panel)
{
}

static void panel_disable(struct lcd_panel *panel)
{
}

static struct lcd_panel_ops panel_ops = {
	.enable  = (void*)panel_enable,
	.disable = (void*)panel_disable,
};

static struct fb_videomode panel_modes[] = {
	[0] = {
		.name                   = "800x480",
		.refresh                = 60,
		.xres                   = 800,
		.yres                   = 480,
		.pixclock               = KHZ2PICOS(33264),  //KHz
		.left_margin            = 40,    //hbp--thb--h-Back-Porch
		.right_margin           = 210,    //hfp--thfp--h-Front-Porch
		.upper_margin           = 23,    //vbp--tvb--v-Back-Porch
		.lower_margin           = 22,    //vfp--tvfp--v-Front-Porch
		.hsync_len              = 40,    //hsync--thpw--h-Pulse-Width
		.vsync_len              = 20,     //vsync--tvpw--v-Pulse-Width
		.sync                   = ~FB_SYNC_HOR_HIGH_ACT & ~FB_SYNC_VERT_HIGH_ACT,
		.vmode                  = FB_VMODE_NONINTERLACED,
		.flag                   = 0,
	},
};

static struct tft_config T070BEN050_02BT_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

struct lcd_panel lcd_panel = {
	.name = "T070BEN050_02BT",
	.num_modes = ARRAY_SIZE(panel_modes),
	.modes = panel_modes,
	.bpp = 24,
	.width = 800,
	.height = 480,

	.lcd_type = LCD_TYPE_TFT,

	.tft_config = &T070BEN050_02BT_cfg,

	.dither_enable = 1,
	.dither.dither_red = 1,
	.dither.dither_green = 1,
	.dither.dither_blue = 1,

	.ops = &panel_ops,
};

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *panel = lcd_get_data(lcd);
	struct board_gpio *vdd_en = &panel->vdd_en;
	struct board_gpio *rst = &panel->rst;
	struct board_gpio *pwm = &panel->pwm;
	printk("%s, %d, power = %d, panel power = %d\n", __func__, __LINE__, power, panel->power);
	if(POWER_IS_ON(power) && !POWER_IS_ON(panel->power)) {
		gpio_direction_output(vdd_en->gpio, 1);
		msleep(10);
		gpio_direction_output(rst->gpio, 1);

		gpio_direction_output(pwm->gpio, 1);

		printk("%s, %d, power = %d\n", __func__, __LINE__, power);
	}
	if(!POWER_IS_ON(power) && POWER_IS_ON(panel->power)) {
		gpio_direction_output(vdd_en->gpio, 0);
		printk("%s, %d, power = %d\n", __func__, __LINE__, power);
	}

	panel->power = power;
        return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *panel = lcd_get_data(lcd);
	return panel->power;
}

/**
* @ pannel_T070BEN050-02BT_lcd_ops, register to kernel common backlight/lcd.c framworks.
*/
static struct lcd_ops panel_lcd_ops = {
	.early_set_power = panel_set_power,
	.set_power = panel_set_power,
	.get_power = panel_get_power,
};

static int of_panel_parse(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	enum of_gpio_flags flags;
	int ret = 0;


/*	panel->vdd_en.gpio = of_get_named_gpio_flags(np, "ingenic,vdd-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->vdd_en.gpio)) {
		panel->vdd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->vdd_en.gpio, GPIOF_DIR_OUT, "vdd_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request vdd_en pin!\n");
			return ret;
		}
	} else {
		dev_warn(dev, "invalid gpio vdd_en.gpio: %d\n", panel->vdd_en.gpio);
	}
*/
	panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->rst.gpio)) {
		panel->rst.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request rst pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}
	panel->pwm.gpio = of_get_named_gpio_flags(np, "ingenic,pwm-gpio", 0, &flags);
	if(gpio_is_valid(panel->pwm.gpio)) {
		panel->pwm.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->pwm.gpio, GPIOF_DIR_OUT, "pwm");
		if(ret < 0) {
			dev_err(dev, "Failed to request pwm pin!\n");
			goto err_request_pwm;
		}
	} else {
		dev_warn(dev, "invalid gpio pwm.gpio: %d\n", panel->pwm.gpio);
	}
	return 0;
err_request_pwm:
	if(gpio_is_valid(panel->rst.gpio))
		gpio_free(panel->rst.gpio);
err_request_rst:
	if(gpio_is_valid(panel->vdd_en.gpio))
		gpio_free(panel->vdd_en.gpio);
	return ret;
}
/**
* @panel_probe
*
* 	1. Register to ingenicfb.
* 	2. Register to lcd.
* 	3. Register to backlight if possible.
*
* @pdev
*
* @Return -
*/
static int panel_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct panel_dev *panel;
	panel = kzalloc(sizeof(struct panel_dev), GFP_KERNEL);
	if(panel == NULL) {
		dev_err(&pdev->dev, "Faile to alloc memory!");
		return -ENOMEM;
	}
	panel->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, panel);

	ret = of_panel_parse(&pdev->dev);
	if(ret < 0) {
		goto err_of_parse;
	}

	panel->lcd = lcd_device_register("panel_lcd", &pdev->dev, panel, &panel_lcd_ops);
	if(IS_ERR_OR_NULL(panel->lcd)) {
		dev_err(&pdev->dev, "Error register lcd!\n");
		ret = -EINVAL;
		goto err_of_parse;
	}

	/* TODO: should this power status sync from uboot */
	panel->power = FB_BLANK_POWERDOWN;
	panel_set_power(panel->lcd, FB_BLANK_UNBLANK);

	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_lcd_register;
	}
	printk("============panel_t070ben050_02bt probe ok!\n\n");
	return 0;

err_lcd_register:
	lcd_device_unregister(panel->lcd);
err_of_parse:
	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	struct panel_dev *panel = dev_get_drvdata(&pdev->dev);

	panel_set_power(panel->lcd, FB_BLANK_POWERDOWN);
	return 0;
}

#ifdef CONFIG_PM
static int panel_suspend(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);

	panel_set_power(panel->lcd, FB_BLANK_POWERDOWN);
	return 0;
}

static int panel_resume(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);

	panel_set_power(panel->lcd, FB_BLANK_UNBLANK);
	return 0;
}

static const struct dev_pm_ops panel_pm_ops = {
	.suspend = panel_suspend,
	.resume = panel_resume,
};
#endif
static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,T070BEN050_02BT", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "T070BEN050_02BT",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);
MODULE_LICENSE("GPL");
