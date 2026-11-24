/*
 * A V4L2 driver for Sony IMX385 cameras.
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
#include <linux/clk.h>
#include <isp-sensor.h>

#define MCLK            37125000
#define VMAX		1125
#define HMAX		4400
#define HMAX_50		2200
#define DOL_RHS1	51  //39
#define DOL_RATIO	16
#define Y_SIZE		2204

#define IMX385_CHIP_ID_H	(0xf0)
#define IMX385_CHIP_ID_L	(0x00)
#define IMX385_REG_END		0xffff
#define IMX385_REG_DELAY	0xfffe

#define AGAIN_DGAIN_MAX_DB 720
#define LOG2_GAIN_SHIFT 16

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct imx385_win_size {
	unsigned int width;
	unsigned int height;
	struct sensor_info sensor_info;
	unsigned int mbus_code;
	enum v4l2_colorspace colorspace;
	void *regs;
};

struct imx385_gpio {
	int pin;
	int active_level;
};

struct imx385_info {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;
	struct v4l2_ctrl *again_short;

	struct v4l2_clk *clk;
	struct clk *sclka;

	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *exposure_short;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct imx385_win_size *win;

	struct imx385_gpio reset;
	struct imx385_gpio power;
	struct imx385_gpio ircutp;
	struct imx385_gpio ircutn;
};

static inline struct imx385_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx385_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct imx385_info, hdl)->sd;
}
struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

static struct regval_list LWKJ_imx385_init_regs_2Lane_12bit_HCG[] = {
	/*
	 * Param:
	 *	mipi csi-2 2lane
	 *	37.125MHz
	 *	1080P
	 *	30fps
	 *	12bit
	 *	HCG
	 */


	//Standby
	{0x3000, 0x01},
	{0x3001, 0x00},

	{0x3002, 0x01},
	{0x3005, 0x01},//ADBIT=1,12bit
	{0x3007, 0x00},
	{0x3009, 0x12},//HCG 30fps

	//Black Level 12bit:0F0h;10bit:03Ch
	{0x300A, 0xF0},
	{0x300B, 0x00},

	{0x3012, 0x2C},//GD
	{0x3013, 0x01},//GD

	//gain
	//{0x3014, 0x00},//lxd
	{0x3014, 0x14},
	{0x3015, 0x00},
	{0x3016, 0x08},

	//VMAX P56 0x0465
	{0x3018, 0x65},
	{0x3019, 0x04},
	{0x301A, 0x00},
	//HMAX P56 0x1130
	{0x301B, 0x30},
	{0x301C, 0x11},

	//æ­£å¸¸æå
	{0x3020, 0x00},
	{0x3021, 0x00},
	{0x3022, 0x00},

	{0x3044, 0x01},//CSI-2,12bit

	{0x3046, 0x00},//XVSLNG
	{0x3047, 0x08},//XHSLANG
	{0x3049, 0x00},//XVSOUTSEL XHSOUTSEL VSYNC output

	{0x3054, 0x66},//åºå®,SCDEN=0

	//INCK,CSI-2 INCK=37.125MHz
	{0x305C, 0x18},//INCKSEL1 87é¡µ
	{0x305D, 0x00},//INCKSEL2
	{0x305E, 0x20},//INCKSEL3
	{0x305F, 0x00},//INCKSEL4

	{0x310B, 0x07},//GD
	{0x3110, 0x12},//GD
	{0x31ED, 0x38},//GD

	//åºå®æ å®æ
	{0x3338, 0xD4},
	{0x3339, 0x40},
	{0x333A, 0x10},
	{0x333B, 0x00},
	{0x333C, 0xD4},
	{0x333D, 0x40},
	{0x333E, 0x10},
	{0x333F, 0x00},

	{0x3344, 0x00},//PEPRTITION=0 P45
	{0x3346, 0x01},	//1:2LANE 3:4LANE

	{0x3353, 0x0E},
	{0x3357, 0x49},
	{0x3358, 0x04},

	//57é¡µï¼THSEXIT=3Fhï¼12bitï¼30fps,2lane
	{0x336B, 0x3F},
	{0x336C, 0x1F},//TCLKPRE

	//0C0C:RAW12 0A0A:RAW10
	{0x337D, 0x0C},//CSI_DT_FMT
	{0x337E, 0x0C},//CSI_DT_FMT

	{0x337F, 0x01},//CSI_LANE_MODE 1:2LANE;3:4LANE;

	//2520h:37.125MHz; 4A40h:74.25MHz
	{0x3380, 0x20},//INCK_FREQ1[7:0]
	{0x3381, 0x25},//INCK_FREQ1[15:8]

	//12bit 30fps
	{0x3382, 0x67},//TCLKPOST CSI-2åºå®
	{0x3383, 0x1F},
	{0x3384, 0x3F},
	{0x3385, 0x27},
	{0x3386, 0x1F},
	{0x3387, 0x17},
	{0x3388, 0x77},
	{0x3389, 0x27},

	//1B4h:INCK=37.125MHz; 367h:INCK=74.25MHz
	{0x338D, 0xB4},//INCK_FREQ2[7:0]
	{0x338E, 0x01},//INCK_FREQ2[15:8]

	{0x3000, 0x00},
	//{imx385_REG_DELAY, 30},	/* å»¶æ¶*/
	{0x3002, 0x00},
	//{0x3049, 0x0A},

	{IMX385_REG_END, 0x00},	/* END MARKER */
};

#if 1
static struct regval_list imx385_init_regs_2Lane_10bit_50fps_mipi[] = {
	/*
	 * Param:
	 *	mipi csi-2 2lane
	 *	37.125MHz
	 *	1080P
	 *	50fps
	 *	10bit
	 */

	{0x3000,0x01},  //STANDBY
	{0x3002,0x01},
	{0x3005,0x00},  //ADBIT
	{0x3007,0x00},  //WINMODE
	{0x3009,0x01},  //HCG&FRSEL
	{0x300a,0x3c},
	{0x300b,0x00},
	{0x3012,0x2c},
	{0x3013,0x01},
	{0x3014,0x00},  //Gain setting
	{0x3015,0x00},	//Gain setting
	{0x3016,0x09},
	{0x3018,0x65},  //VMAX
	{0x3019,0x04},  //VMAX
//      {0x3018,0xca},  //VMAX
//      {0x3019,0x08},  //VMAX
	{0x301a,0x00},	//VMAX
	{0x301b,0x50},  //HMAX, 30h->30fps;
	{0x301c,0x0a},  //HMAX, 11h->30fps;
	{0x3020,0x0f},  //SHS1
	{0x3021,0x00},	//SHS1
	{0x3022,0x00},	//SHS1
	{0x3044,0x01},  //OPORTSE&ODBIT
	{0x3049,0x00},  //XVS/XHS output set
	{0x3054,0x66},  //
	{0x305c,0x28},  //INCKSEL1,37.125MHz;
	{0x305d,0x00},  //INCKSEL2
	{0x305e,0x20},	//INCKSEL3
	{0x305f,0x00},	//INCKSEL4
	{0x310b,0x07},
	{0x3110,0x12},
	{0x31ed,0x38},
	{0x3338,0xd4},
	{0x3339,0x40},
	{0x333a,0x10},
	{0x333b,0x00},
	{0x333c,0xd4},
	{0x333d,0x40},
	{0x333e,0x10},
	{0x333f,0x00},
	{0x3344,0x00},
	{0x3346,0x01},
	{0x3353,0x0e},
	{0x3357,0x49},
	{0x3358,0x04},
	{0x336b,0x5f},
	{0x336c,0x1f},
	{0x337d,0x0a},
	{0x337e,0x0a},
	{0x337f,0x01},
	{0x3380,0x20},  //INCK_FREQ1
	{0x3381,0x25},
	{0x3382,0x77},
	{0x3383,0x2f},
	{0x3384,0x5f},
	{0x3385,0x37},
	{0x3386,0x37},
	{0x3387,0x37},
	{0x3388,0xbf},
	{0x3389,0x3f},
	{0x338d,0xb4},
	{0x338e,0x01},
	{0x3000,0x00},  //Standby cancel
	//wait_ms(30)
	{0x3002,0x00},  //Master start
	{0x3049,0x0a}, //XVS/XHS output
	// wait(10mS)
	{IMX385_REG_END, 0x00},
};

#endif

#if 0
static struct regval_list imx385_init_regs_2Lane_10bit_50fps_mipi[] = {
	/*
	   IMX385,All-pixel,37.125MHz;DOL2X;
	   10Bit,50fps,MIPI 2Lane;
	   */
	{0x3000,0x01},  //STANDBY
	{0x3002,0x01},
	{0x3005,0x00},  //ADBIT
	{0x3007,0x10},  //WINMODE
	{0x3009,0x02},  //HCG&FRSEL
	{0x300A,0x3c},
	{0x300C,0x11},
	{0x3012,0x2c},
	{0x3013,0x00},
	{0x3018,0x65},  // VMAX
	{0x3019,0x04},  // VMAX
	{0x301a,0x00},  // VMAX
	{0x301b,0x28},  // HMAX
	{0x301c,0x05},  // HMAX
	{0x3020,0x04},  //SHS1
	{0x3021,0x00},  //SHS1
	{0x3022,0x00},  //SHS1
	{0x3023,0x49},
	{0x3024,0x03},
	{0x302C,0x27},
	{0x3043,0x05},
	{0x3044,0x00},  //OPORTSE&ODBIT
	{0x3049,0x00},  //XVS/XHS output set
	{0x3054,0x66},
	{0x305c,0x28},  //INCKSEL1,37.125MHz;
	{0x305d,0x00},  //INCKSEL2
	{0x305e,0x20},  //INCKSEL3
	{0x305f,0x00},  //INCKSEL4
	{0x3108,0x11},
	{0x3109,0x01},
	{0x310b,0x07},
	{0x3110,0x12},
	{0x31ed,0x38},
	{0x3338,0xd4},
	{0x3339,0x40},
	{0x333a,0x10},
	{0x333b,0x00},
	{0x333c,0xd4},
	{0x333d,0x40},
	{0x333e,0x10},
	{0x333f,0x00},
	{0x3344,0x00},
	{0x3346,0x01},
	{0x3353,0x0e},
	{0x3354,0x00},
	{0x3357,0xB2},
	{0x3358,0x08},
	{0x336b,0x5F},
	{0x337d,0x0A},
	{0x337e,0x0A},
	{0x337f,0x01},
	{0x3380,0x20},  //INCK_FREQ1
	{0x3381,0x25},
	{0x3382,0x77},
	{0x3383,0x2F},
	{0x3384,0x5F},
	{0x3385,0x37},
	{0x3386,0x37},
	{0x3387,0x37},
	{0x3388,0xBF},
	{0x3389,0x3F},
	{0x338d,0xb4},
	{0x338e,0x01},
	{0x3000,0x00},  //standby cancel
	{0x3002,0x00},  //Master start
	{0x3049,0x0a},  //XVS/XHS output
	{IMX385_REG_END, 0x00},
};

#endif

static struct regval_list imx385_init_regs_1920_1080_30fps_mipi[] = {
	{0x3000, 0x01},

	{0x3009, 0x02},
	{0x310B, 0x07},
	{0x3110, 0x12},
	{0x3012, 0x2C},
	{0x3013, 0x01},
	{0x301B, 0x30},//0x1130 ----- 4400
	{0x301C, 0x11},

	{0x3338, 0xD4},
	{0x3339, 0x40},
	{0x333A, 0x10},
	{0x333B, 0x00},
	{0x333C, 0xD4},
	{0x333D, 0x40},
	{0x333E, 0x10},
	{0x333F, 0x00},
	{0x3344, 0x10},

	{0x3049, 0x0A},//HSYNC VSYNC output
	{0x3054, 0x66},//CSI-2 setting
	{0x305D, 0x00},//37.125
	{0x305F, 0x00},
	{0x336B, 0x2F},   //THSEXIT
	{0x336C, 0x1F},   //TCLKPRE
	{0x3380, 0x20},
	{0x3381, 0x25},
	{0x3382, 0x5F},
	{0x3383, 0x17},
	{0x3384, 0x2F},
	{0x3385, 0x37},   //HS-trail
	//{0x3385, 0x17},   //HS-trail
	{0x3386, 0x17},
	{0x3387, 0x0F},
	{0x3388, 0x4F},

	{0x338D, 0xB4},
	{0x338E, 0x01},
	{0x31ED, 0x38},

	{0x3014, 0x58}, //GAIN
	{0x3015, 0x00},

	//{0x3000, 0x00},
	{0x3002, 0x00}, //master mode operation start
	{0x3000, 0x01},

	{IMX385_REG_END, 0x00},/* END MARKER */
};

static struct regval_list imx385_init_regs_1920_1080_30fps_mipi_2dol_lcg[] = {
	{0x3000, 0x01},

	{0x3007, 0x10},
	{0x300C, 0x11},
	{0x3012, 0x2C},
	{0x3013, 0x01},
	{0x301B, HMAX_50&0xFF},//0x98
	{0x301C, HMAX_50>>8},//0x8
	{0x3020, 0x03},
	{0x3023, 0xE9},
	{0x3024, 0x03},
	{0x302C, DOL_RHS1},
	{0x3043, 0x05},
	{0x3049, 0x0A},//HSYNC VSYNC output
	{0x3054, 0x66},
	{0x305D, 0x00},
	{0x305F, 0x00},

	{0x3109, 0x01},
	{0x310B, 0x07},
	{0x3110, 0x12},
	{0x31ED, 0x38},

	{0x3338, 0xD4},
	{0x3339, 0x40},
	{0x333A, 0x10},
	{0x333B, 0x00},
	{0x333C, 0xD4},
	{0x333D, 0x40},
	{0x333E, 0x10},
	{0x333F, 0x00},
	{0x3346, 0x03},//4lane
	{0x3353, 0x0E},
	{0x3354, 0x00},
	{0x3357, 0xB2},//effective pixel width
	{0x3358, 0x08},
	{0x337D, 0x0C},//RAW12
	{0x337E, 0x0C},//RAW12
	{0x3380, 0x20},
	{0x3381, 0x25},
	{0x338D, 0xB4},
	{0x338E, 0x01},

	{0x3014, 0x58}, //GAIN
	{0x3015, 0x00},

	{0x3000, 0x00},
	{0x3002, 0x00}, //master mode operation start


	{IMX385_REG_END, 0x00},/* END MARKER */

};

/*
 * the part of driver was fixed.
 */
static struct regval_list imx385_stream_on_mipi[] = {
	{0x3000, 0x00},
	{IMX385_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list imx385_stream_off_mipi[] = {
	{0x3000, 0x01},
	{IMX385_REG_END, 0x00},	/* END MARKER */
};

static int imx385_read(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char *value)
{
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

static int imx385_write(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char value)
{
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

static int imx385_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != IMX385_REG_END) {
		if (vals->reg_num == IMX385_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = imx385_read(sd, vals->reg_num, &val);
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
static int imx385_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != IMX385_REG_END) {
		if (vals->reg_num == IMX385_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = imx385_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}

static int imx385_ircut(struct v4l2_subdev *sd, u32 val)
{
	struct imx385_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->ircutp.pin, info->ircutp.active_level);
		gpio_direction_output(info->ircutn.pin, info->ircutn.active_level);
		msleep(10);
		gpio_direction_output(info->ircutp.pin, !info->ircutp.active_level);
	} else {
		gpio_direction_output(info->ircutp.pin, !info->ircutp.active_level);
		gpio_direction_output(info->ircutn.pin, !info->ircutn.active_level);
		msleep(10);
		gpio_direction_output(info->ircutp.pin, info->ircutp.active_level);
	}
	return 0;
}

static int imx385_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = imx385_read(sd, 0x3012, &v);
	pr_info("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != IMX385_CHIP_ID_H)
		return -ENODEV;
	*ident = v;
	ret = imx385_read(sd, 0x3013, &v);
	pr_info("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != IMX385_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;
	return 0;
}

static struct imx385_win_size imx385_win_sizes[] = {

	{
		.width				= 1936,
		.height				= 1096,
		.sensor_info.fps				= 30 << 16 | 1,
		.sensor_info.total_width			= 1936,
		.sensor_info.total_height			= 1096,
		.sensor_info.wdr_en				= 1,
		.sensor_info.mipi_cfg.clk			= 891,
		.sensor_info.mipi_cfg.twidth		= 1936,
		.sensor_info.mipi_cfg.theight		= 1096,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_SONY_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en		= 1,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 16,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 12,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 16,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 62,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en		= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en		= 0,
		.sensor_info.mipi_cfg.data_type_value	= RAW12,
		.sensor_info.mipi_cfg.del_start		= 1,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt		= TX_SENSOR_RAW12,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= imx385_init_regs_1920_1080_30fps_mipi,
		//.regs 		= LWKJ_imx385_init_regs_2Lane_12bit_HCG,
	},

	{
		.width              = 1920,
		.height             = 1080,
		.sensor_info.fps                = 50 << 16 | 1,
		.sensor_info.total_width            = 2200,// to be done ?
		.sensor_info.total_height           = 1125,// to be done ?
		//.sensor_info.total_width            = 1952,// to be done ?
		//.sensor_info.total_height           = 1113,// to be done ?
		.sensor_info.wdr_en             = 0,
		.sensor_info.mipi_cfg.clk           = 891,// to be done ?
		.sensor_info.mipi_cfg.twidth        = 1952,// to be done ?
		.sensor_info.mipi_cfg.theight       = 1113,// to be done ?
		.sensor_info.mipi_cfg.mipi_mode     = SENSOR_MIPI_SONY_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en     = 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en     = 1,
		.sensor_info.mipi_cfg.mipi_crop_start0x = 16,
		.sensor_info.mipi_cfg.mipi_crop_start0y = 25,
		.sensor_info.mipi_cfg.mipi_crop_start1x = 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y = 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x = 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y = 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x = 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y = 0,
		.sensor_info.mipi_cfg.hcrop_diff_en     = 0,
		.sensor_info.mipi_cfg.line_sync_mode    = 0,
		.sensor_info.mipi_cfg.work_start_flag   = 0,
		.sensor_info.mipi_cfg.data_type_en      = 0,
		.sensor_info.mipi_cfg.data_type_value   = RAW10,// to be done ?
		.sensor_info.mipi_cfg.del_start     = 1,
		.sensor_info.mipi_cfg.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode   = 0,
		.sensor_info.mipi_cfg.sensor_mode       = TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt        = TX_SENSOR_RAW10,
		.mbus_code  = MEDIA_BUS_FMT_SRGGB10_1X10, //to be done ?
		.colorspace = V4L2_COLORSPACE_SRGB,
		.regs       = imx385_init_regs_2Lane_10bit_50fps_mipi,
	},
};

#if 0
static int imx385_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= N_IMX385_FMTS)
		return -EINVAL;

	code->code = imx385_formats[code->index].mbus_code;
	return 0;
}
#endif

/*
 * Set a format.
 */
static int imx385_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	if (format->pad)
		return -EINVAL;

	return 0;
}

static int imx385_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct imx385_info *info = to_state(sd);
	struct imx385_win_size *wsize = info->win;
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
	*(unsigned int *)fmt->reserved = (unsigned int)&wsize->sensor_info;
	//    	printk("----%s, %d, width: %d, height: %d, code: %x\n",
	//   			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int imx385_s_wdr(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	struct imx385_info *info = to_state(sd);
	if(value){
		//printk("win_size[1]\n");
		//info->win = &imx385_win_sizes[1];
	}
	else{
		printk("win_size[1]\n");
		info->win = &imx385_win_sizes[1];
	}
	return ret;
}

static int imx385_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx385_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx385_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx385_s_vflip(struct v4l2_subdev *sd, int value)
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
static int imx385_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int imx385_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	//  printk("enter %s %d \n",__func__, __LINE__);
#if 0
	unsigned int reg_value = 0;
	printk("enter %s %d \n",__func__, __LINE__);

	printk("again = %d\n", again);
	reg_value = again_to_regval(again);
	ret += imx385_write(sd, 0x3014, (unsigned char)(reg_value & 0xff));
#endif

	return ret;
}

static unsigned int again_to_regval(int gain)
{
	unsigned int again=(gain*20*3)>>LOG2_GAIN_SHIFT /*wdr:2250-131-10-1*/ ;

	// Limit Max gain
	if(again>AGAIN_DGAIN_MAX_DB) again=AGAIN_DGAIN_MAX_DB;
	return again;
}

static int regval_to_again(unsigned int regval)
{
	return (((int32_t)regval)<<LOG2_GAIN_SHIFT)/20/3;
}

static int imx385_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	unsigned char vl = 0;
	unsigned int reg_val = 0;
	int ret = 0;
	unsigned char vh = 0;


	ret += imx385_read(sd, 0x3014, &vl);
	ret += imx385_read(sd, 0x3015, &vh);
	reg_val = vl + (vh << 8);

	*value = regval_to_again(reg_val);

	return ret;
}

static int imx385_g_again_short(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
#if 1
	unsigned char v = 0;
	unsigned int reg_val = 0;

	ret += imx385_read(sd, 0x30f2, &v);
	reg_val = v;

	*value = regval_to_again(reg_val);
#endif
	return ret;
}

/*set analog gain db value, map value to sensor register.*/
static int imx385_s_again(struct v4l2_subdev *sd, int again)
{
	int ret = 0;
#if 1
	unsigned int reg_value = 0;

	reg_value = again_to_regval(again);
	ret += imx385_write(sd, 0x3014, (unsigned char)(reg_value & 0xff));
	ret += imx385_write(sd, 0x3015, (unsigned char)(reg_value >> 8 & 0x3));
#endif
	return ret;
}

static int imx385_s_again_short(struct v4l2_subdev *sd, int again)
{
	int ret = 0;
#if 1
	unsigned int reg_value = 0;

	reg_value = again_to_regval(again);
	ret += imx385_write(sd, 0x30f2, (unsigned char)(reg_value & 0xff));
#endif

	return ret;
}

/*
 * Tweak autogain.
 */
static int imx385_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx385_g_exp(struct v4l2_subdev *sd, __s32 *value)
{
	struct imx385_info *info = to_state(sd);

	int val = 0,ret = 0;
	unsigned char tmp_val = 0,tmp_val1 = 0,tmp_val2 = 0;

	ret = imx385_read(sd, 0x3020, &tmp_val);
	ret += imx385_read(sd, 0x3021, &tmp_val1);
	ret += imx385_read(sd, 0x3022, &tmp_val2);
	*value = (tmp_val + tmp_val1<<8 + (tmp_val2<<16)&0x3);

	return val;
}

static int imx385_s_exp(struct v4l2_subdev *sd, int value)
{
	struct imx385_info *info = to_state(sd);
	int ret = 0;
	int shs = 0;
	unsigned char val = 0;
	int total_height = 0;

#if 1
	ret = imx385_read(sd, 0x3019, &val);
	total_height |= val << 8;
	ret = imx385_read(sd, 0x3018, &val);
	total_height |= val;

	if(info->win->sensor_info.wdr_en) {
		total_height *= 2;
		shs = total_height - value - 1;
		ret = imx385_write(sd, 0x3024, (unsigned char)(shs & 0xff));
		ret += imx385_write(sd, 0x3025, (unsigned char)((shs >> 8) & 0xff));
		ret += imx385_write(sd, 0x3026, (unsigned char)((shs >> 16) & 0x3));
	} else {
		shs = total_height - value - 1;
		ret = imx385_write(sd, 0x3020, (unsigned char)(shs & 0xff));
		ret += imx385_write(sd, 0x3021, (unsigned char)((shs >> 8) & 0xff));
		ret += imx385_write(sd, 0x3022, (unsigned char)((shs >> 16) & 0x3));
	}
#endif
	return ret;
}

static int imx385_s_exp_short(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
#if 1
	int shs = 101 - value - 1;
	ret = imx385_write(sd, 0x3020, (unsigned char)(shs & 0xff));
	ret += imx385_write(sd, 0x3021, (unsigned char)((shs >> 8) & 0xff));
	ret += imx385_write(sd, 0x3022, (unsigned char)((shs >> 16) & 0x3));
#endif

	return ret;
}

static int imx385_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct imx385_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_AUTOGAIN:
			return imx385_g_gain(sd, &info->gain->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return imx385_g_again(sd, &info->again->val);
		case V4L2_CID_USER_ANALOG_GAIN_SHORT:
			return imx385_g_again_short(sd, &info->again_short->val);
		case V4L2_CID_EXPOSURE_ABSOLUTE:
			return imx385_g_exp(sd, &info->exposure->val);
	}
	return -EINVAL;
}

static int imx385_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct imx385_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			return imx385_s_brightness(sd, ctrl->val);
		case V4L2_CID_CONTRAST:
			return imx385_s_contrast(sd, ctrl->val);
		case V4L2_CID_VFLIP:
			return imx385_s_vflip(sd, ctrl->val);
		case V4L2_CID_HFLIP:
			return imx385_s_hflip(sd, ctrl->val);
		case V4L2_CID_AUTOGAIN:
			/* Only set manual gain if auto gain is not explicitly
			   turned on. */
			if (!ctrl->val) {
				/* imx385_s_gain turns off auto gain */
				return imx385_s_gain(sd, info->gain->val);
			}
			return imx385_s_autogain(sd, ctrl->val);
		case V4L2_CID_GAIN:
			return imx385_s_gain(sd, ctrl->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return imx385_s_again(sd, ctrl->val);
		case V4L2_CID_USER_ANALOG_GAIN_SHORT:
			return imx385_s_again_short(sd, ctrl->val);
		case V4L2_CID_EXPOSURE:
			return imx385_s_exp(sd, ctrl->val);
		case V4L2_CID_USER_EXPOSURE_SHORT:
			return imx385_s_exp_short(sd, ctrl->val);
		case V4L2_CID_WIDE_DYNAMIC_RANGE:
			return imx385_s_wdr(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops imx385_ctrl_ops = {
	.s_ctrl = imx385_s_ctrl,
	.g_volatile_ctrl = imx385_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int imx385_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = imx385_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int imx385_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	imx385_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

static int imx385_core_init(struct v4l2_subdev *sd, u32 val)
{
	struct imx385_info *info = to_state(sd);
	int ret = 0;
	ret = imx385_write_array(sd, info->win->regs);
	return ret;
}

static int imx385_core_reset(struct v4l2_subdev *sd, u32 val)
{
	struct imx385_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->power.pin, 1);
		msleep(100);

		printk("--power on!\n", __func__, __LINE__);

		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(10);
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
		msleep(10);
	}
	return 0;
}

static int imx385_power(struct v4l2_subdev *sd, u32 val)
{
	struct imx385_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->power.pin, 1);
		msleep(200);

		printk("----%s, %d on!\n", __func__, __LINE__);
	}
	else {
		gpio_direction_output(info->power.pin, 0);
		msleep(10);
		printk("----%s, %d off!\n", __func__, __LINE__);

	}
	return 0;
}


int imx385_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {

		imx385_core_reset(sd,1);
		imx385_core_init(sd,1);
		ret = imx385_write_array(sd, imx385_stream_on_mipi);
		printk("imx385 stream on\n");

	}
	else {
		ret = imx385_write_array(sd, imx385_stream_off_mipi);
		imx385_power(sd,0);
		printk("imx385 stream off\n");
	}
	return ret;
}
/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops imx385_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = imx385_g_register,
	.s_register = imx385_s_register,
#endif
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
	.init = imx385_core_init,
	.reset = imx385_core_reset,
	.s_power = imx385_power,

};

static const struct v4l2_subdev_video_ops imx385_video_ops = {
	.s_stream = imx385_s_stream,
};

static const struct v4l2_subdev_pad_ops imx385_pad_ops = {
	//.enum_frame_interval = imx385_enum_frame_interval,
	//.num_frame_size = imx385_enum_frame_size,
	//.enum_mbus_code = imx385_enum_mbus_code,
	.set_fmt = imx385_set_fmt,
	.get_fmt = imx385_get_fmt,
};

static const struct v4l2_subdev_ops imx385_ops = {
	.core = &imx385_core_ops,
	.video = &imx385_video_ops,
	.pad = &imx385_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int imx385_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct imx385_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	unsigned long rate;
	struct v4l2_ctrl_config cfg = {0};
	int mclk_index = -1;
	char id_div[9];
	char id_mux[9];

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,power-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->power.pin = gpio;
		info->power.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		printk("imx385 info->power.pin=%d\n",info->power.pin);
		gpio_direction_output(info->power.pin, 1);
		msleep(10);
	}

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->reset.pin = gpio;
		info->reset.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		printk("imx385 info->reset.pin=%d\n",info->reset.pin);
		gpio_direction_output(info->reset.pin, 1);
	}

	v4l2_i2c_subdev_init(sd, client, &imx385_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/*clk*/
	of_property_read_u32(client->dev.of_node, "ingenic,mclk", &mclk_index);
	if(mclk_index == 0) {
		memcpy(id_div, "div_cim0", sizeof(id_div));
		memcpy(id_mux, "mux_cim0", sizeof(id_mux));
	} else if(mclk_index == 1) {
		memcpy(id_div, "div_cim1", sizeof(id_div));
		memcpy(id_mux, "mux_cim1", sizeof(id_mux));
	} else if(mclk_index == 2) {
		memcpy(id_div, "div_cim2", sizeof(id_div));
		memcpy(id_mux, "mux_cim2", sizeof(id_mux));
	} else
		printk("Unkonwn mclk index\n");

	info->clk = v4l2_clk_get(&client->dev, id_div);
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		goto err_clkget;
	}
	info->sclka = devm_clk_get(&client->dev, id_mux);

	rate = v4l2_clk_get_rate(info->clk);
	if (((rate / 1000) % 37125) != 0) {
		ret = clk_set_parent(info->sclka, clk_get(NULL, "epll"));
		info->sclka = devm_clk_get(&client->dev, "epll");
		if (IS_ERR(info->sclka)) {
			pr_err("get sclka failed\n");
		} else {
			rate = clk_get_rate(info->sclka);
			if (((rate / 1000) % 37125) != 0) {
				clk_set_rate(info->sclka, 891000000);
			}
		}
	}

	ret = v4l2_clk_set_rate(info->clk,MCLK );
	if(ret)
		dev_err(sd->dev, "clk_set_rate err!\n");

	ret = v4l2_clk_enable(info->clk);
	if(ret)
		dev_err(sd->dev, "clk_enable err!\n");

	imx385_core_reset(sd, 1);
#if 1
	/* Make sure it's an imx385 */
	/*
	   while(1){
	   ret = imx385_detect(sd, &ident);
	   }
	   */
	ret = imx385_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
				"chip found @ 0x%x (%s) is not an imx385 chip.\n",
				client->addr << 1, client->adapter->name);
		return ret;
	}
#endif

	info->win = &imx385_win_sizes[1];
	//info->win = &imx385_win_sizes[0];

	/*IRCUT ctl 0:off 1:on*/
	imx385_ircut(sd, 0);
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 10);
	v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_WIDE_DYNAMIC_RANGE, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 262144, 1, 10000);
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &imx385_ctrl_ops,
			V4L2_CID_EXPOSURE, 2,1176/*0x8ca-1*/, 1, 1000);

	cfg.ops = &imx385_ctrl_ops;
	cfg.id = V4L2_CID_USER_EXPOSURE_SHORT;
	cfg.name = "expo short";
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.min = 1;
	cfg.max = 98;
	cfg.step = 1;
	cfg.def = 50;
	info->exposure_short = v4l2_ctrl_new_custom(&info->hdl, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.ops = &imx385_ctrl_ops;
	cfg.id = V4L2_CID_USER_ANALOG_GAIN_SHORT;
	cfg.name = "analog gain short";
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.min = 0;
	cfg.max = 589824;
	cfg.step = 1;
	cfg.def = 10000;
	info->again_short = v4l2_ctrl_new_custom(&info->hdl, &cfg, NULL);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;

		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&info->sd.entity, 1, &info->pad, 0);
	if(ret < 0) {
		goto err_entity_init;
	}
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "imx385 Probed\n");
	return 0;
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}


static int imx385_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx385_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	dev_info(&client->dev, "imx385 removed\n");
	return 0;
}

static const struct i2c_device_id imx385_id[] = {
	{ "imx385", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx385_id);

static const struct of_device_id imx385_of_match[] = {
	{.compatible = "ovti,imx385", },
	{},
};
MODULE_DEVICE_TABLE(of, imx385_of_match);


static struct i2c_driver imx385_driver = {
	.driver = {
		.name	= "imx385",
		.of_match_table = of_match_ptr(imx385_of_match),
	},
	.probe		= imx385_probe,
	.remove		= imx385_remove,
	.id_table	= imx385_id,
};

module_i2c_driver(imx385_driver);
MODULE_AUTHOR("qpz <aric.pzqi@ingenic.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision imx385 sensors");
MODULE_LICENSE("GPL");
