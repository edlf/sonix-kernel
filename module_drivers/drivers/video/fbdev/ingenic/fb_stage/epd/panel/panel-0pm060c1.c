#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/lcd.h>
#include <linux/regulator/consumer.h>
#include <linux/of_graph.h>
#include "../ebc_pmic.h"
#include "../sf_epd.h"

/* Timing same to JZ4775 EPD
   =============== dump_epd_timing():
   type= 1
   line_length= 308
   frame_length= 770
   Source_pin_size= 3
   Gate_pin_size= 3
   0: Pin[ 0, 0, V(  10,  770):H(  12,  268)]
   1: Pin[11, 0, V(  10,  769):H(  12,  105)]
   2: Pin[ 8, 1, V(  10,  769):H(   0,    6)]
   3: Pin[ 9, 1, V(   5,  770):H(  12,  274)]
   4: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   5: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   6: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   7: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   8: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   9: Pin[10, 0, V(   5,    7):H( 150,  150)]
   10: Pin[13, 1, V(   0, 65535):H(   0, 65535)]
   11: Pin[12, 1, V(   0, 65535):H(   0, 65535)]
   12: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   13: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   14: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   15: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   16: Pin[ 0, 0, V(   0,    0):H(   0,    0)]
   ===============
*/

#define CLK_FREQ (20*1000000)	/* Typical Clock Frequency 20MHz(85Hz Frame Rate) */

/* Source Timing */
#define LSL   (8)			/* Line Sync */
#define LBL   (8)			/* Line Begin */
#define LDL (256)			/* Line Data */
#define LEL  (36)			/* Line End */
#define LT  (LSL+LBL+LDL+LEL)		/* Line Time: 306 */
#define CPV_STA  (4)			/* Gate Clock Start */
#define CPV_HI  (262)			/* Gate On Time */

/* Gate Timing */
#define FSL   (4)			/* Frame Sync */
#define FBL   (4)			/* Frame Begin */
#define FDL (758)			/* Frame Data */
#define FEL   (4)			/* Frame End */
#define FRAME_LENGTH (FSL+FBL+FDL+FEL)

#define PIXEL_ALIGN 4		/* keep sync with jz_fb.h */

struct fb_videomode jzfb0_videomode = {
	.name = "OED-1024x758",
	.refresh = 0,
	.xres = LDL*4,		/* must aligned */
	.yres = FDL,
//	.pixclock = KHZ2PICOS(20158), /* 20MHz*/
	// smartlcd
	.pixclock = KHZ2PICOS(60474), /* 20MHz, WR_CLK = PIXEL_CLOCK/2 */
	//.pixclock = KHZ2PICOS(10000*2), /* 20MHz, WR_CLK = PIXEL_CLOCK/2 */
	.left_margin = 0,
	.right_margin = 0,
	.upper_margin = 0,
	.lower_margin = 0,
	.hsync_len = 0,
	.vsync_len = 0,
	.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode = FB_VMODE_NONINTERLACED,
	.flag = 0,
};


struct epd_timing epd_timing_mode = {
	.color = 0,
	.type = EPD_TIMING_MODE1,
	.line_length = ALIGN(LT, PIXEL_ALIGN),    /* (8+8+256+38)= 306 + 4, must aligned 4 */
	.frame_length = FRAME_LENGTH,             /* 770 */

	/* Actuall visible area: for user pixel buffer interface */
	.line_data_start = (LSL+LBL),             /* Line Data Start: (6+6)*/
	.line_data_length = (LDL),                /* Line Data Length: 256 Bytes = 1024 pixels */
	.frame_data_start = (FSL+FBL),            /* Frame Data Start */
	.frame_data_length = (FDL),               /* Frame Data Length */

	.timing = {
		.M1 = {
			.Source_pin_size=MODE1_SOURCE_IOPIN_SIZE,
			.Gate_pin_size=MODE1_GATE_IOPIN_SIZE,

			/* Timing same to JZ4775 EPD #define FRAME_LINE_MAX (4+4+758+4) */
			/*
			  .line_length = ((6+6+256+38) + 2),
			  .frame_length = FRAME_LINE_MAX+0,
			*/

			/* source timing */
#if 1  // using smartlcd simulate

			.Data   = {.D= 0, .P=POLARITY_LOW,  .VS=(FSL+FBL), .VE=(FSL+FBL+FDL), .HS=(LSL+LBL), .HE = (LSL+LBL+LDL)},
			.SPH    = {.D=12, .P=POLARITY_LOW,  .VS=(FSL+FBL), .VE=FRAME_LENGTH, .HS=(LSL+LBL),  .HE = (LSL+LBL+LDL)},
			.LE     = {.D=10, .P=POLARITY_HIGH, .VS=(FSL+FBL), .VE=FRAME_LENGTH, .HS=0, .HE=LBL},
			.CPV    = {.D=8, .P=POLARITY_HIGH,  .VS=FSL, .VE=FRAME_LENGTH, .HS=(LSL+LBL), .HE=(LSL+LBL+LDL+(LEL/2))},
			/* gate timing */
			.STV    = {.D=13, .P=POLARITY_LOW,  .VS=FSL, .VE=FSL+1, .HS=(LT/2), .HE=(LT/2)}, /* STV.HE must not zero */
			.GMODE1 = {.D=9, .P=POLARITY_LOW,   .VS=(0), .VE=FSL, .HS=(0), .HE=USHORT_MAX},
			.OE     = {.D=11, .P=POLARITY_HIGH, .VS=(FSL+FBL), .VE=(FSL+FBL+FDL+1), .HS=(0), .HE=USHORT_MAX},

#else // using lcd simulate
			.Data   = {.D= 0, .P=POLARITY_LOW,  .VS=(FSL+FBL), .VE=(FSL+FBL+FDL), .HS=(LSL+LBL), .HE=(LSL+LBL+LDL)},
			.SPH    = {.D=20, .P=POLARITY_LOW,  .VS=(FSL+FBL), .VE=FRAME_LENGTH-1, .HS=(LSL+LBL), .HE=(LSL+LBL+LDL)},
			.LE     = {.D=18, .P=POLARITY_HIGH, .VS=(FSL+FBL), .VE=FRAME_LENGTH-1, .HS=0, .HE=LBL},
			.CPV    = {.D=16, .P=POLARITY_HIGH, .VS=FSL, .VE=FRAME_LENGTH, .HS=(LSL+LBL), .HE=(LSL+LBL+LDL+(LEL/2))},
			/* gate timing */
			.STV    = {.D=21, .P=POLARITY_LOW,  .VS=FSL, .VE=FSL+1, .HS=(LT/2), .HE=(LT/2)}, /* STV.HE must not zero */
			.GMODE1 = {.D=17, .P=POLARITY_LOW,  .VS=(0), .VE=FSL, .HS=(0), .HE=USHORT_MAX},
			.OE     = {.D=19, .P=POLARITY_HIGH, .VS=(FSL+FBL), .VE=(FSL+FBL+FDL+1), .HS=(0), .HE=USHORT_MAX},
#endif
		},
	},
};

/**************************************************************************************************/
struct panel_dev {
	/* ingenic frame buffer */
	struct device *dev;
	struct lcd_panel panel;
	struct ebc_pmic *pmic;
	int vcom;
	int inited;
	struct i2c_client *client;
};

static void vcom_enable(void *panel)
{
	struct panel_dev *panel_dev = container_of(panel,struct panel_dev,panel);
	if(panel_dev->inited)
		ebc_pmic_power_on(panel_dev->pmic);
	else
		printk("%s inited: %d\n",__FUNCTION__,panel_dev->inited);
}

static void vcom_disable(void *panel)
{
	struct panel_dev *panel_dev = container_of(panel,struct panel_dev,panel);
	if(panel_dev->inited)
		ebc_pmic_power_off(panel_dev->pmic);
	else
		printk("%s inited: %d\n",__FUNCTION__,panel_dev->inited);
}

static void vcom_init(void *panel)
{
	struct panel_dev *panel_dev = container_of(panel,struct panel_dev,panel);
	int timeout = 100;
	while(timeout != 0 && (panel_dev->pmic == NULL)) {
		panel_dev->pmic = (struct ebc_pmic *)i2c_get_clientdata(panel_dev->client);
		msleep(100);
		printk("pmic: %p\n",panel_dev->pmic);
		timeout--;
	}
	if(timeout) {
		panel_dev->inited = 1;
		ebc_pmic_power_on(panel_dev->pmic);
		ebc_pmic_set_vcom(panel_dev->pmic,1560); // formware = -1
	} else {
		printk("Can't find pmic\n");
	}
}

struct lcd_panel_ops vcom_ops = {
	.init = vcom_init,
	.enable = vcom_enable,
	.disable = vcom_disable,
};

static int of_panel_parse(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node,*endpoint,*device_node;
	struct i2c_client *client;

	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint) {
		dev_dbg(dev, "%s: no endpoint\n", __func__);
		return -ENODEV;
	}

	/* Get panel node from the output port's endpoint data */
	device_node = of_graph_get_remote_port(endpoint);
	if (!device_node) {
		dev_err(dev, "%s: no valid device\n", __func__);
		of_node_put(endpoint);
		return -ENODEV;
	}

	of_node_put(endpoint);
	of_node_put(device_node);

	client = of_find_i2c_device_by_node(device_node->parent);

	if(!client) {
		dev_err(dev, "%s: no find i2c client\n", __func__);
		return -ENODEV;
	}
	panel->client = client;
	return 0;
}

static int panel_probe(struct platform_device *pdev)
{
	struct panel_dev *panel_dev;
	int ret = 0;

	panel_dev = devm_kzalloc(&pdev->dev,sizeof(struct panel_dev), GFP_KERNEL);
	if(panel_dev == NULL) {
		dev_err(&pdev->dev, "Faile to alloc memory!");
		return -ENOMEM;
	}
	panel_dev->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, panel_dev);

	panel_dev->panel.name = "0PM060C1";
	panel_dev->panel.num_modes = 1;
	panel_dev->panel.modes = &jzfb0_videomode;
	panel_dev->panel.epd_timing = &epd_timing_mode;
	panel_dev->panel.bpp    = 16;
	panel_dev->panel.width = 1024;
	panel_dev->panel.height = 758;
	panel_dev->panel.ops = &vcom_ops;

	ret = of_panel_parse(&pdev->dev);
	if(ret < 0) {
		goto err_of_parse;
	}

	ret = ingenicfb_register_panel(&panel_dev->panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_of_parse;
	}

	return 0;

err_of_parse:
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,0PM060C1", },
	{},
};

#ifdef CONFIG_PM
static int panel_suspend(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);
	ebc_pmic_suspend(panel->pmic);
	return 0;
}

static int panel_resume(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);
	ebc_pmic_resume(panel->pmic);
	return 0;
}

static const struct dev_pm_ops panel_pm_ops = {
	.suspend = panel_suspend,
	.resume = panel_resume,
};
#endif

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "0PM060C1",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);
