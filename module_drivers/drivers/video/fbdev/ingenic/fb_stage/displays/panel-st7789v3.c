/*
 * module_drivers/driver/video/fbdev/ingenic/fb_stage/displays/panel-st7789v3.c
 * Note : This is a IPS (SLCD Control) DBI SPI 4lane Driver,
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 *
 * This program is free software, you can redistribute it and/or modify it
 *
 * under the terms of the GNU General Public License version 2 as published by
 *
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/of_gpio.h>

#include "../ingenicfb.h"
#define USE_HORIZONTAL 2  // 320*172 config

#if (USE_HORIZONTAL==0||USE_HORIZONTAL==1)
#define LCD_W 172
#define LCD_H 320
#else
#define LCD_W 320
#define LCD_H 172
#endif
struct board_gpio {
	short gpio;
	short active_level;
};

struct panel_dev {
	/* ingenic frame buffer */
	struct device *dev;
	struct lcd_panel *panel;

	/* common lcd framework */
	struct lcd_device *lcd;
	struct backlight_device *backlight;
	int power;

	struct regulator *vcc;
	struct board_gpio cs;
	struct board_gpio rst;
	struct board_gpio vdd_en;
	struct board_gpio vddio_en;
	struct board_gpio pwm;
	struct board_gpio rd;
};

static struct smart_lcd_data_table st7789v3_data_table[] = {
#if 1
	{SMART_CONFIG_UDELAY,   120000},
	{SMART_CONFIG_CMD,	0x11},

	{SMART_CONFIG_CMD,	0x36},//Display Setting


	#if (USE_HORIZONTAL==0)
	{SMART_CONFIG_PRM,  0x00},
	#elif(USE_HORIZONTAL==1)
	{SMART_CONFIG_PRM,  0xC0},
	#elif(USE_HORIZONTAL==2)
	{SMART_CONFIG_PRM,  0x70},
	#else
	{SMART_CONFIG_PRM,  0xA0},
	#endif
	{SMART_CONFIG_CMD,	 0x3A},
	{SMART_CONFIG_PRM,   0x05},
	{SMART_CONFIG_CMD,	 0xB2},
	{SMART_CONFIG_PRM,   0x0C},
	{SMART_CONFIG_PRM,   0x0C},
	{SMART_CONFIG_PRM,   0x00},
	{SMART_CONFIG_PRM,   0x33},
	{SMART_CONFIG_PRM,   0x33},
	{SMART_CONFIG_CMD,   0xB7},
	{SMART_CONFIG_PRM,   0x35},
	{SMART_CONFIG_CMD,   0xBB},
	{SMART_CONFIG_PRM,   0x35},
	{SMART_CONFIG_CMD,   0xC0},
	{SMART_CONFIG_PRM,   0x2C},
	{SMART_CONFIG_CMD,   0xC2},
	{SMART_CONFIG_PRM,   0x01},
	{SMART_CONFIG_CMD,   0xC3},
	{SMART_CONFIG_PRM,   0x13},
	{SMART_CONFIG_CMD,   0xC4},
	{SMART_CONFIG_PRM,   0x20},
	{SMART_CONFIG_CMD,   0xC6},
	{SMART_CONFIG_PRM,   0x0F},
	{SMART_CONFIG_CMD,   0xD0},
	{SMART_CONFIG_PRM,   0xA4},
	{SMART_CONFIG_PRM,   0xA1},
	{SMART_CONFIG_CMD,   0xD6},
	{SMART_CONFIG_PRM,   0xA1},
	{SMART_CONFIG_CMD,   0xE0},//Gamma setting
	{SMART_CONFIG_PRM,   0xF0},
	{SMART_CONFIG_PRM,   0x00},
	{SMART_CONFIG_PRM,   0x04},
	{SMART_CONFIG_PRM,   0x04},
	{SMART_CONFIG_PRM,   0x04},
	{SMART_CONFIG_PRM,   0x05},
	{SMART_CONFIG_PRM,   0x29},
	{SMART_CONFIG_PRM,   0x33},
	{SMART_CONFIG_PRM,   0x3E},
	{SMART_CONFIG_PRM,   0x38},
	{SMART_CONFIG_PRM,   0x12},
	{SMART_CONFIG_PRM,   0x12},
	{SMART_CONFIG_PRM,   0x28},
	{SMART_CONFIG_PRM,   0x30},
	{SMART_CONFIG_CMD,   0xE1},
	{SMART_CONFIG_PRM,   0xF0},
	{SMART_CONFIG_PRM,   0x07},
	{SMART_CONFIG_PRM,   0x0A},
	{SMART_CONFIG_PRM,   0x0D},
	{SMART_CONFIG_PRM,   0x0B},
	{SMART_CONFIG_PRM,   0x07},
	{SMART_CONFIG_PRM,   0x28},
	{SMART_CONFIG_PRM,   0x33},
	{SMART_CONFIG_PRM,   0x3E},
	{SMART_CONFIG_PRM,   0x36},
	{SMART_CONFIG_PRM,   0x14},
	{SMART_CONFIG_PRM,   0x14},
	{SMART_CONFIG_PRM,   0x29},
	{SMART_CONFIG_PRM,   0x32},

	#if (USE_HORIZONTAL==0)
	{SMART_CONFIG_CMD,   0x2A },
	{SMART_CONFIG_PRM,   0x00 },//Xstart
	{SMART_CONFIG_PRM,   0x22 },
	{SMART_CONFIG_PRM,   0x00 },//Xend
	{SMART_CONFIG_PRM,   0xAC },
	{SMART_CONFIG_CMD,   0x2B},
	{SMART_CONFIG_PRM,   0x00 },//Ystart
	{SMART_CONFIG_PRM,   0x00},
	{SMART_CONFIG_PRM,   0x01 },
	{SMART_CONFIG_PRM,   0x40 },//Yend
	{SMART_CONFIG_CMD,   0x2C},
	#elif(USE_HORIZONTAL==1)
	{SMART_CONFIG_CMD,   0x2A },
	{SMART_CONFIG_PRM,   0x00 },//Xstart
	{SMART_CONFIG_PRM,   0x22 },
	{SMART_CONFIG_PRM,   0x00 },//Xend
	{SMART_CONFIG_PRM,   0xAC },
	{SMART_CONFIG_CMD,   0x2B},
	{SMART_CONFIG_PRM,   0x00 },//Ystart
	{SMART_CONFIG_PRM,   0x00},
	{SMART_CONFIG_PRM,   0x01 },
	{SMART_CONFIG_PRM,   0x40 },//Yend
	{SMART_CONFIG_CMD,   0x2C},
	#elif(USE_HORIZONTAL==2)
        {SMART_CONFIG_CMD,   0x2A},
        {SMART_CONFIG_PRM,   0x0 },//Xstart
        {SMART_CONFIG_PRM,   0x0 },
        {SMART_CONFIG_PRM,   0x01},//Xend     0~319
        {SMART_CONFIG_PRM,   0x3F},
        {SMART_CONFIG_CMD,   0x2B},
	{SMART_CONFIG_PRM,   0x00 },//Ystart :35
	{SMART_CONFIG_PRM,   0x23},
	{SMART_CONFIG_PRM,   0x00 },//
	{SMART_CONFIG_PRM,   0xCE },//Yend 35~206

	#else //Test
	{SMART_CONFIG_CMD,   0x2A },
	{SMART_CONFIG_PRM,   0x00 },//Xstart
	{SMART_CONFIG_PRM,   0x00 },
	{SMART_CONFIG_PRM,   0x01 },//Xend
	{SMART_CONFIG_PRM,   0x40 },
	{SMART_CONFIG_CMD,   0x2B},
	{SMART_CONFIG_PRM,   0x00 },//Ystart
	{SMART_CONFIG_PRM,   0x22},
	{SMART_CONFIG_PRM,   0x00 },
	{SMART_CONFIG_PRM,   0xCE },//Yend
	{SMART_CONFIG_CMD,   0x2C},
	#endif

	{SMART_CONFIG_CMD,   0x21},
	{SMART_CONFIG_CMD,   0x11},
	{SMART_CONFIG_UDELAY,   120000},
	{SMART_CONFIG_CMD,   0x29},
	{SMART_CONFIG_CMD,   0x2C},


#endif
};

static struct fb_videomode panel_modes[] = {
	[0] = {
		#if (USE_HORIZONTAL==0||USE_HORIZONTAL==1)
		.name = "172x320",
		#else
		.name = "320x172",
		#endif
		#if (USE_HORIZONTAL==0||USE_HORIZONTAL==1)
		.xres = 172,
		.yres = 320,
		#else
		.xres = 320,
		.yres = 172,
		#endif

		.refresh = 60,//60fps modex->pixclock: 37850
//		.pixclock = KHZ2PICOS(18432),
		.left_margin = 0,
		.right_margin = 0,
		.upper_margin = 0,
		.lower_margin = 0,
		.hsync_len = 0,
		.vsync_len = 0,
		.sync = 0,
		.vmode = FB_VMODE_NONINTERLACED,
		.flag = 0,
	},
};

struct smart_config st7789v_cfg = {
	 .te_switch = 0,
	 .te_dp = 0, //The TE' default polarity,0: the default(invalid) level is low;(default),1: the default(invalid) level is high;
	 .te_md = 0, //active edge of TE.0: the front edge(default),1: the back edge
	 .te_anti_jit = 0,

	 .dc_md = 0,
	 .wr_md = 1,
	 .smart_type = SMART_LCD_TYPE_SPI_4,
	 .pix_fmt = SMART_LCD_FORMAT_565,
//	 .dwidth = SMART_LCD_DWIDTH_8_BIT,
	 .cwidth = SMART_LCD_CWIDTH_8_BIT,

	.data_table = st7789v3_data_table,
	.length_data_table = ARRAY_SIZE(st7789v3_data_table),


};

struct lcd_panel lcd_panel = {
	.name = "st7789v3",
	.num_modes = ARRAY_SIZE(panel_modes),
	.modes = panel_modes,
	.lcd_type = LCD_TYPE_SLCD,
	#if (USE_HORIZONTAL==0||USE_HORIZONTAL==1)
	.width = 172,
	.height = 320,
	#else
	.width = 320,
	.height = 172,
	#endif
	.smart_config = &st7789v_cfg,

	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,
};

/* SGM3146 supports 16 brightness step */
#define MAX_BRIGHTNESS_STEP     16
/* System support 256 brightness step */
#define CONVERT_FACTOR          (256/MAX_BRIGHTNESS_STEP)


static int panel_update_status(struct backlight_device *bd)
{
	struct panel_dev *panel = dev_get_drvdata(&bd->dev);
	int brightness = bd->props.brightness;
	unsigned int i;
	int pulse_num = MAX_BRIGHTNESS_STEP - brightness / CONVERT_FACTOR - 1;

	if (bd->props.fb_blank == FB_BLANK_POWERDOWN) {
		return 0;
	}

	if (bd->props.state & BL_CORE_SUSPENDED)
		brightness = 0;

	if (brightness && gpio_is_valid(panel->pwm.gpio)) {
		gpio_direction_output(panel->pwm.gpio,0);
		udelay(5000);
		gpio_direction_output(panel->pwm.gpio,1);
		udelay(100);

		for (i = pulse_num; i > 0; i--) {
			gpio_direction_output(panel->pwm.gpio,0);
			udelay(1);
			gpio_direction_output(panel->pwm.gpio,1);
			udelay(3);
		}
	} else if (gpio_is_valid(panel->pwm.gpio))
		gpio_direction_output(panel->pwm.gpio, 0);

	return 0;
}

static struct backlight_ops panel_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = panel_update_status,
};

static void panel_power_reset(struct board_gpio *rst)
{

	gpio_direction_output(rst->gpio, 1);
	mdelay(20);
	gpio_direction_output(rst->gpio, 0);
	mdelay(20);
	gpio_direction_output(rst->gpio, 1);
	mdelay(120);
}

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *panel = lcd_get_data(lcd);
	struct board_gpio *cs = &panel->cs;
	struct board_gpio *rst = &panel->rst;
	struct board_gpio *vdd_en = &panel->vdd_en;
	struct board_gpio *vddio_en = &panel->vddio_en;
	struct board_gpio *rd = &panel->rd;

	if(POWER_IS_ON(power) && !POWER_IS_ON(panel->power)) {
		if(gpio_is_valid(panel->pwm.gpio))
			gpio_direction_output(panel->pwm.gpio, 1);
		gpio_direction_output(vdd_en->gpio, 1);
		if(gpio_is_valid(rd->gpio))
			gpio_direction_output(rd->gpio, 1);
		if(gpio_is_valid(panel->vddio_en.gpio))
			gpio_direction_output(vddio_en->gpio, 1);
		gpio_direction_output(cs->gpio, 1);
		gpio_direction_output(rst->gpio, 1);
		mdelay(10);
		panel_power_reset(rst);
		gpio_direction_output(cs->gpio, 0);
	}
	if(!POWER_IS_ON(power) && POWER_IS_ON(panel->power)) {
		gpio_direction_output(cs->gpio, 0);
		gpio_direction_output(rst->gpio, 0);
		if(gpio_is_valid(panel->vddio_en.gpio))
			gpio_direction_output(vddio_en->gpio, 0);
		if(gpio_is_valid(rd->gpio))
			gpio_direction_output(rd->gpio, 0);
		gpio_direction_output(vdd_en->gpio, 0);
	}

	panel->power = power;
	return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *panel = lcd_get_data(lcd);

	return panel->power;
}

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

	panel->cs.gpio = of_get_named_gpio_flags(np, "ingenic,cs-gpio", 0, &flags);
	if(gpio_is_valid(panel->cs.gpio)) {
		panel->cs.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->cs.gpio, GPIOF_DIR_OUT, "cs");
		if(ret < 0) {
			dev_err(dev, "Failed to request cs pin!\n");
			return ret;
		}
	} else {
		dev_warn(dev, "invalid gpio cs.gpio: %d\n", panel->cs.gpio);
	}

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
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio pwm.gpio: %d\n", panel->pwm.gpio);
	}

	panel->vdd_en.gpio = of_get_named_gpio_flags(np, "ingenic,vdd-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->vdd_en.gpio)) {
		panel->vdd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->vdd_en.gpio, GPIOF_DIR_OUT, "vdd_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request vdd_en pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio vdd_en.gpio: %d\n", panel->vdd_en.gpio);
	}

	panel->rd.gpio = of_get_named_gpio_flags(np, "ingenic,rd-gpio", 0, &flags);
	if(gpio_is_valid(panel->rd.gpio)) {
		panel->rd.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->rd.gpio, GPIOF_DIR_OUT, "rd");
		if(ret < 0) {
			dev_err(dev, "Failed to request rd pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio rd.gpio: %d\n", panel->rd.gpio);
	}

	panel->vddio_en.gpio = of_get_named_gpio_flags(np, "ingenic,vddio-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->vddio_en.gpio)) {
		panel->vddio_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->vddio_en.gpio, GPIOF_DIR_OUT, "vddio_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request vddio_en pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio vddio_en.gpio: %d\n", panel->vddio_en.gpio);
	}

	return 0;
err_request_rst:
	gpio_free(panel->cs.gpio);
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
	struct backlight_properties props;
	memset(&props, 0, sizeof(props));
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

	props.type = BACKLIGHT_RAW;
	props.max_brightness = 255;
	panel->backlight = backlight_device_register("pwm-backlight.0",
						&pdev->dev, panel,
						&panel_backlight_ops,
						&props);
	if (IS_ERR_OR_NULL(panel->backlight)) {
		dev_err(panel->dev, "failed to register 'pwm-backlight.0'.\n");
		goto err_lcd_register;
	}
	panel->backlight->props.brightness = props.max_brightness;
	backlight_update_status(panel->backlight);

	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_lcd_register;
	}
	printk("st7799v3 panel_probe end success\n");
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
	{ .compatible = "ingenic,st7789v3", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "st7789v3",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);
MODULE_LICENSE("GPL");
