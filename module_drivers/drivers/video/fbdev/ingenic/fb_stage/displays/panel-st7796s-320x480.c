/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-ST7796S.c
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#if 0
//device tree config:

 &pwm {
 	pinctrl-names = "default";
 	pinctrl-0 = <&pwm1_pc>;
 	status = "okay";
 };
 &dpu {
 	status = "okay";
 	ingenic,disable-rdma-fb = <1>;
 	/*Defines the init state of composer fb export infomations.*/
 	ingenic,layer-exported = <1 0 0 0>;
 	ingenic,layer-frames   = <2 0 0 0>;
 	ingenic,layer-framesize = <320 480>, <320 480>, <320 240>, <320 240>;   /*Max framesize for each layer.*/
 	layer,color_mode	= <0 0 0 0>;					/*src fmt,*/
 	layer,src-size       	= <320 480>, <320 480>, <320 240>, <240 200>;	/*Layer src size should smaller than framesize*/
 	layer,target-size	= <320 480>, <320 480>, <160 240>, <240 200>;	/*Target Size should smaller than src_size.*/
 	layer,target-pos	= <0 0>, <0 0>, <320 480>, <100 480>;	/*target pos , the start point of the target panel.*/
 	layer,enable		= <1 0 0 0>;					/*layer enabled or disabled.*/
 	ingenic,logo-pan-layer	= <0>;						/*on which layer should init logo pan on.*/
 	port {
 		dpu_out_ep: endpoint {
 			remote-endpoint = <&panel_ST7796S_ep>;
 	    };
 	};
 };
 	display-dbi {
 		compatible = "simple-bus";
 		#interrupt-cells = <1>;
 		#address-cells = <1>;
 		#size-cells = <1>;
 		ranges = <>;
 		panel_ST7796S {
 			compatible = "ingenic,ST7796S";
 			status = "okay";
 			pinctrl-names = "default";
 			ingenic,rst-gpio = <&gpc 4 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
 			port {
 				panel_ST7796S_ep: endpoint {
 					remote-endpoint = <&dpu_out_ep>;
 				};
 			};
 		};
 	};
 backlight {
 		compatible = "pwm-backlight";
 		pwms = <&pwm 1 1000000>; /* arg1: pwm channel id [0~15]. arg2: period in ns. */
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

#include "../ingenicfb.h"
#include "../jz_dsim.h"


static char panel_ST7796S_debug = 1;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_ST7796S_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL ST7796S: " msg);		\
	} while(0)

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
	struct board_gpio lcd_te;
	struct board_gpio lcd_pwm;

	struct mipi_dsim_lcd_device *dsim_dev;
};

struct panel_dev *panel;

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

/* #define PANEL_ST7796S_ID	(0x98815c) */

struct ST7796S {
	struct device *dev;
	unsigned int power;
	unsigned int id;
	struct lcd_device *ld;
	struct backlight_device *bd;
	struct mipi_dsim_lcd_device *dsim_dev;//dsim:动态音效改进技术

};


static struct dsi_cmd_packet fitipower_ST7796S_320_480_test_cmd_list[] =
{
	{0x39, 0x02, 0x00, {0xF0, 0xC3}},
	{0x39, 0x02, 0x00, {0xF0, 0x96}}, //VGH = 5*VCI   VGL = -3*VCI
	{0x39, 0x04, 0x00, {0xb6, 0x8A, 0x07, 0x3b}},//4E 51  CD   52
	{0x39, 0x05, 0x00, {0xb5, 0x20, 0x20, 0x00, 0x20 }},
	{0x39, 0x03, 0x00, {0xb1, 0x80, 0x10}},
	{0x39, 0x02, 0x00, {0x36, 0x88}},//MADCTL
	{0x39, 0x02, 0x00, {0x3a, 0x77}},//55 56
	{0x39, 0x02, 0x00, {0xb4, 0x00}},//IPS
	{0x39, 0x02, 0x00, {0xc5, 0x28}},
	{0x39, 0x02, 0x00, {0xc1, 0x13}},
	{0x39, 0x01, 0x00, {0x21}},
	{0x39, 0x09, 0x00, {0xE8, 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}},
	{0x39, 0x15, 0x00, {0xe0, 0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30, 0x33, 0x47, 0x17, 0x13, 0x13, 0x2B, 0x31}},
	{0x39, 0x15, 0x00, {0xe1, 0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2f, 0x33, 0x47, 0x38, 0x15, 0x16, 0x2C, 0x32}},
	{0x39, 0x02, 0x00, {0xF0, 0x3c}},
	{0x39, 0x02, 0x00, {0xF0, 0x69}},
};


static void panel_dev_sleep_in(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x10, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_sleep_out(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x11, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_display_on(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x29, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_display_off(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x28, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_power_on(struct mipi_dsim_lcd_device *dsim_dev, int power)
{
	struct board_gpio *vdd_en = &panel->vdd_en;
	struct board_gpio *rst = &panel->rst;
	struct board_gpio *lcd_te = &panel->lcd_te;
	struct board_gpio *pwm = &panel->lcd_pwm;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

    gpio_direction_output(panel->rst.gpio, !panel->rst.active_level);
    printk("rst-gpio=%d,value=%d\n",panel->rst.gpio,!panel->rst.active_level);
    msleep(100);
	gpio_direction_output(panel->rst.gpio, panel->rst.active_level);
    printk("rst-gpio=%d,value=%d\n",panel->rst.gpio,panel->rst.active_level);
	msleep(150);
	gpio_direction_output(panel->rst.gpio, !panel->rst.active_level);
    printk("rst-gpio=%d,value=%d\n",panel->rst.gpio,!panel->rst.active_level);
	msleep(100);

	panel->power = power;
}

static int panel_dev_read_id(struct mipi_dsim_lcd_device *dsim_dev)
{
#if 0
	struct dsi_master_ops *ops = dsim_dev->master->master_ops;

	struct dsi_cmd_packet data_to_send0 = {0x14, 0xDa, 0x0};
	struct dsi_cmd_packet data_to_send1 = {0x14, 0xDB, 0x0};
	struct dsi_cmd_packet data_to_send2 = {0x14, 0xDC, 0x0};
	int ret = 0;
	int panel_id = 0;

	panel_dev_power_on(dsim_dev, 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send0, &buf[0], 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send1, &buf[1], 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send2, &buf[2], 1);
	panel_dev_power_on(dsim_dev, 0);

	panel_id = (buf[0] << 16) + (buf[1] << 8) + buf[2];
	printk("--read_id--------%s, (%d) id[0]: %x id[1]: %x, id[2]: %x, id:%08x \n", __func__, __LINE__, buf[0], buf[1], buf[2], panel_id);

#endif

//	if(panel_id == PANEL_ST7796S_ID)
//		return 0;
//	else
//		return -1;
	return 0;
}

static void panel_dev_panel_init(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_device *dsi = lcd_to_master(lcd);
	int  i;
	int ret;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);
	for (i = 0; i < ARRAY_SIZE(fitipower_ST7796S_320_480_test_cmd_list); i++)
	{
		ret = ops->cmd_write(dsi,  fitipower_ST7796S_320_480_test_cmd_list[i]);
	}
}

static void panel_dev_set_sequence(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct ST7796S *lcd = dev_get_drvdata(&dsim_dev->dev);
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	panel_dev_panel_init(panel);
	panel_dev_sleep_out(panel);
	msleep(50);
	panel_dev_display_on(panel);
	msleep(20);
	dump_dsi_reg(dsim_dev->master);
	lcd->power = FB_BLANK_UNBLANK;
}


static struct fb_videomode panel_modes = {
	.name = "fitipower_ST7796S-lcd",
	.xres = 320,
	.yres = 480,

	.refresh =40,
	/* .pixclock               = KHZ2PICOS(13519), */
	.left_margin = 80,	//hbp
	.right_margin = 20,	//hfp
	.hsync_len = 10,	//hsync

	.upper_margin = 20,	//vbp
	.lower_margin = 20,	//vfp
	.vsync_len = 4,	//vsync

	.sync                   = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
	.vmode                  = FB_VMODE_NONINTERLACED,
	.flag = 0,
};

struct jzdsi_data jzdsi_pdata = {
	.modes = &panel_modes,
	.video_config.no_of_lanes = 1,
	.video_config.virtual_channel = 0,
	.video_config.color_coding =COLOR_CODE_24BIT,  //COLOR_CODE_24BIT
	.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
	.video_config.receive_ack_packets = 0,	/* enable receiving of ack packets */
	.video_config.is_18_loosely = 0,
	.video_config.data_en_polarity = 1,
	.video_config.byte_clock = 0, // driver will auto calculate byte_clock.
	.video_config.byte_clock_coef = MIPI_PHY_BYTE_CLK_COEF_MUL6_DIV5, // byte_clock *6/5.

	.dsi_config.max_lanes = 2,
	.dsi_config.max_hs_to_lp_cycles = 100,
	.dsi_config.max_lp_to_hs_cycles = 40,
	.dsi_config.max_bta_cycles = 4095,
	.dsi_config.color_mode_polarity = 1,
	.dsi_config.shut_down_polarity = 1,
	.dsi_config.max_bps = 2750,
	.bpp_info = 24,
};

static struct tft_config ST7796S_cfg = {
	.pix_clk_inv = 1,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

struct lcd_panel lcd_panel = {
	.num_modes = 1,
	.modes = &panel_modes,
	.dsi_pdata = &jzdsi_pdata,

	.lcd_type = LCD_TYPE_MIPI_TFT,
	.tft_config = &ST7796S_cfg,
	.bpp = 24,
	.width = 49,
	.height = 73,

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
* @ pannel_ST7796S_lcd_ops, register to kernel common backlight/lcd.c framworks.
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

	panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->rst.gpio)) {
		panel->rst.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		gpio_direction_output(panel->rst.gpio, !panel->rst.active_level);
        printk("rst-gpio=%d,value=%d\n",panel->rst.gpio,!panel->rst.active_level);
		ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request rst pin!\n");
			return ret;
		}
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}
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
	struct ST7796S *lcd;
	char buffer[4];
	int ret;

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct ST7796S), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate fitipower_ST7796S structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("fitipower_ST7796S", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}
	dev_set_drvdata(&dsim_dev->dev, lcd);
	dev_dbg(lcd->dev, "probed fitipower_ST7796S panel driver.\n");
	panel->dsim_dev = dsim_dev;
	return 0;
}

static int panel_suspend(struct mipi_dsim_lcd_device *dsim_dev)
{
	return 0;
}

static int panel_resume(struct mipi_dsim_lcd_device *dsim_dev)
{
	return 0;
}

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = "fitipower_ST7796S-lcd",
	.id = -1,
	.power_on = panel_dev_power_on,
	.set_sequence = panel_dev_set_sequence,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
	.probe_dev_id = panel_dev_read_id,
};


struct mipi_dsim_lcd_device panel_dev_device={
	.name		= "fitipower_ST7796S-lcd",
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
	char buffer[4];
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

	mipi_dsi_register_lcd_device(&panel_dev_device);
	mipi_dsi_register_lcd_driver(&panel_dev_dsim_ddi_driver);

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

	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,ST7796S", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "ST7796S",
		.of_match_table = panel_of_match,
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
