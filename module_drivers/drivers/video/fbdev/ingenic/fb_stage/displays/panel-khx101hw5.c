/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-fw050.c
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
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
#include <linux/fb.h>
#include <linux/backlight.h>

#include "../ingenicfb.h"
#include "../jz_dsim.h"



static char panel_fw050_debug = 1;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_fw050_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL FW050: " msg);		\
	} while(0)

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
	struct board_gpio vdd_en;
	struct board_gpio rst;
	struct board_gpio lcd_te;
	struct board_gpio lcd_pwm;

	struct mipi_dsim_lcd_device *dsim_dev;
};

struct panel_dev *panel;

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

#define PANEL_FW050_ID	(0x98815c)

struct fw050 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};


static void panel_dev_sleep_in(struct panel_dev *lcd)
{
}

static void panel_dev_sleep_out(struct panel_dev *lcd)
{
}

static void panel_dev_display_on(struct panel_dev *lcd)
{
}

static void panel_dev_display_off(struct panel_dev *lcd)
{
}

static void panel_dev_power_on(struct mipi_dsim_lcd_device *dsim_dev, int power)
{
	struct board_gpio *vdd_en = &panel->vdd_en;
	struct board_gpio *rst = &panel->rst;
	struct board_gpio *lcd_te = &panel->lcd_te;
	struct board_gpio *pwm = &panel->lcd_pwm;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	panel->power = power;
}

static int panel_dev_read_id(struct mipi_dsim_lcd_device *dsim_dev)
{
	unsigned char buf[3] = {0};
	struct dsi_master_ops *ops = dsim_dev->master->master_ops;
	return 0;
}


static void panel_dev_panel_init(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_device *dsi = lcd_to_master(lcd);
	struct dsi_cmd_packet data_to_send;
	int  i;
	int ret;
	msleep(50);
}

static void panel_dev_set_sequence(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct fw050 *lcd = dev_get_drvdata(&dsim_dev->dev);
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);


	lcd->power = FB_BLANK_UNBLANK;
}


static struct fb_videomode panel_modes = {
	.name = "fitipower_fw050-lcd",
	//.refresh = 60,
	.xres = 1280,
	.yres = 800,

	.pixclock = KHZ2PICOS(69000),//KHz
	.left_margin = 60,	// hbp
	.right_margin = 60,	//hfp
	.hsync_len = 2,		//hs

	.upper_margin = 10,	//vbp
	.lower_margin = 10,	//vfp
	.vsync_len = 5,		//vs



	.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode = FB_VMODE_NONINTERLACED,
	.flag = 0,

};

struct jzdsi_data jzdsi_pdata = {
	.modes = &panel_modes,
	.video_config.pixel_clock = 69000,		// 69000KHz !!!!!!,must set this clk
	.video_config.virtual_channel = 0,
	.video_config.color_coding = COLOR_CODE_24BIT,

	.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
	//.video_config.video_mode = VIDEO_NON_BURST_WITH_SYNC_PULSES,

	.video_config.receive_ack_packets = 0,	/* enable receiving of ack packets */
	.video_config.is_18_loosely = 0,

	.video_config.h_polarity = 0,
	.video_config.v_polarity = 0,
	.video_config.data_en_polarity = 0,

	.video_config.byte_clock = 0, // driver will auto calculate byte_clock.
	.video_config.byte_clock_coef = MIPI_PHY_BYTE_CLK_COEF_MUL6_DIV5,
	.dsi_config.max_lanes = 4,
	.dsi_config.max_hs_to_lp_cycles = 100,
	.dsi_config.max_lp_to_hs_cycles = 40,
	.dsi_config.max_bta_cycles = 4095,
	.dsi_config.color_mode_polarity = 1,
	.dsi_config.shut_down_polarity = 1,
	.dsi_config.max_bps = 2750,
	.bpp_info = 24,

};

static struct tft_config fw050_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	//.mode = TFT_LCD_MODE_PARALLEL_666,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

struct lcd_panel lcd_panel = {
	.num_modes = 1,
	.modes = &panel_modes,
	.dsi_pdata = &jzdsi_pdata,

	.lcd_type = LCD_TYPE_LVDS_VESA,
	.tft_config = &fw050_cfg,
	.bpp = 24,
	.width = 1280,
	.height = 800,
	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,
};

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
        return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *panel = lcd_get_data(lcd);

	return panel->power;
}

/*
* @ pannel_fw050_lcd_ops, register to kernel common backlight/lcd.c framworks.
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
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	//panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	//if(gpio_is_valid(panel->rst.gpio)) {
	//	panel->rst.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	//	gpio_direction_output(panel->rst.gpio, !panel->rst.active_level);
	//	ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
	//	if(ret < 0) {
	//		dev_err(dev, "Failed to request rst pin!\n");
	//		return ret;
	//	}
	//} else {
	//	dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	//}

	//panel->vdd_en.gpio = of_get_named_gpio_flags(np, "ingenic,vdd-en-gpio", 0, &flags);
	//if(gpio_is_valid(panel->vdd_en.gpio)) {
	//	panel->vdd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	//	gpio_direction_output(panel->vdd_en.gpio, !panel->vdd_en.active_level);
	//	ret = gpio_request_one(panel->vdd_en.gpio, GPIOF_DIR_OUT, "vdd_en");
	//	if(ret < 0) {
	//		dev_err(dev, "Failed to request vdd_en pin!\n");
	//		goto err_request_lcd_vdd;
	//	}
	//} else {
	//	dev_warn(dev, "invalid gpio vdd_en.gpio: %d\n", panel->vdd_en.gpio);
	//}

	 //panel->lcd_pwm.gpio = of_get_named_gpio_flags(np, "ingenic,lcd-pwm-gpio", 0, &flags);
	 //if(gpio_is_valid(panel->lcd_pwm.gpio)) {
	 //	panel->lcd_pwm.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	 //	ret = gpio_request_one(panel->lcd_pwm.gpio, GPIOF_DIR_OUT, "lcd-pwm");
	 //	if(ret < 0) {
	 //		dev_err(dev, "Failed to request lcd-pwm pin!\n");
	 //		goto err_request_lcd_pwm;
	 //	}
	 //} else {
	 //	dev_warn(dev, "invalid gpio lcd-pwm.gpio: %d\n", panel->lcd_pwm.gpio);
	 //}

	return 0;

err_request_lcd_pwm:
	if(gpio_is_valid(panel->vdd_en.gpio))
		gpio_free(panel->vdd_en.gpio);
err_request_lcd_vdd:
	if(gpio_is_valid(panel->rst.gpio))
		gpio_free(panel->rst.gpio);
	return ret;
}

static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct fw050 *lcd;
	char buffer[4];
	int ret;

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct fw050), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate fitipower_fw050 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("fitipower_fw050", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);


	dev_dbg(lcd->dev, "probed fitipower_fw050 panel driver.\n");


	panel->dsim_dev = dsim_dev;


	return 0;

}

static int panel_suspend(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct board_gpio *vdd_en = &panel->vdd_en;

	panel_dev_display_off(panel);
	panel_dev_sleep_in(panel);
	//gpio_direction_output(vdd_en->gpio, !vdd_en->active_level);

	return 0;
}

static int panel_resume(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct board_gpio *vdd_en = &panel->vdd_en;

	//gpio_direction_output(vdd_en->gpio, vdd_en->active_level);

	return 0;
}

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = "khx101hw5-lcd",
	.id = -1,

	.power_on = panel_dev_power_on,
	.set_sequence = panel_dev_set_sequence,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
};


struct mipi_dsim_lcd_device panel_dev_device={
	.name		= "khx101hw5-lcd",
	.id = 0,
};

/*
* @panel_probe

* 	1. Register to ingenicfb.
* 	2. Register to lcd.
* 	3. Register to backlight if possible.

* @pdev

* @Return -
*/
static int panel_probe(struct platform_device *pdev)
{
	int ret = 0;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

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

	//mipi_dsi_register_lcd_device(&panel_dev_device);
	//mipi_dsi_register_lcd_driver(&panel_dev_dsim_ddi_driver);

	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_of_parse;
	}

	return 0;

err_of_parse:
	if(gpio_is_valid(panel->lcd_pwm.gpio))
		gpio_free(panel->lcd_pwm.gpio);
	if(gpio_is_valid(panel->rst.gpio))
		gpio_free(panel->rst.gpio);
	if(gpio_is_valid(panel->vdd_en.gpio))
		gpio_free(panel->vdd_en.gpio);

	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,khx101hw5", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "kh101hw5",
		.of_match_table = panel_of_match,
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
