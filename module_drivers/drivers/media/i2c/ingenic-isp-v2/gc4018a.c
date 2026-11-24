/*
 * A V4L2 driver for OmniVision GC4018A cameras.
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
#include <linux/notifier.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <isp-sensor.h>
#include <riscv.h>
#include <isp-drv.h>


#define GC4018A_CHIP_ID_H	(0x42)
#define GC4018A_CHIP_ID_L	(0x10)
#define GC4018A_REG_END		0xff
#define GC4018A_REG_DELAY	0x00
#define GC4018A_PAGE_REG	0xfd


static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct gc4018a_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_field field;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	void *regs;

};

struct gc4018a_gpio {
	int pin;
	int active_level;
};

struct gc4018a_info {
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
	struct gc4018a_win_size *win;

	struct gc4018a_gpio reset;
	struct gc4018a_gpio pwdn;

	struct notifier_block nb;
	int init_flag;
};


/*
 * the part of driver maybe modify about different sensor and different board.
 */

struct again_lut {
	unsigned int value;
	unsigned int fine_value;
	unsigned int gain;
};

struct again_lut gc4018a_again_lut[] = {
	{0x03, 0x40, 0},
	{0x03, 0x41, 1501},
	{0x03, 0x42, 2886},
	{0x03, 0x43, 4343},
	{0x03, 0x44, 5776},
	{0x03, 0x45, 7101},
	{0x03, 0x46, 8494},
	{0x03, 0x47, 9782},
	{0x03, 0x48, 11136},
	{0x03, 0x49, 12471},
	{0x03, 0x4a, 13706},
	{0x03, 0x4b, 15006},
	{0x03, 0x4c, 16288},
	{0x03, 0x4d, 17474},
	{0x03, 0x4e, 18724},
	{0x03, 0x4f, 19880},
	{0x03, 0x50, 21098},
	{0x03, 0x51, 22300},
	{0x03, 0x52, 23414},
	{0x03, 0x53, 24588},
	{0x03, 0x54, 25747},
	{0x03, 0x55, 26821},
	{0x03, 0x56, 27953},
	{0x03, 0x57, 29003},
	{0x03, 0x58, 30109},
	{0x03, 0x59, 31203},
	{0x03, 0x5a, 32217},
	{0x03, 0x5b, 33287},
	{0x03, 0x5c, 34345},
	{0x03, 0x5d, 35326},
	{0x03, 0x5e, 36362},
	{0x03, 0x5f, 37322},
	{0x03, 0x60, 38336},
	{0x03, 0x61, 39339},
	{0x03, 0x62, 40270},
	{0x03, 0x63, 41253},
	{0x03, 0x64, 42226},
	{0x03, 0x65, 43129},
	{0x03, 0x66, 44083},
	{0x03, 0x67, 44968},
	{0x03, 0x68, 45904},
	{0x03, 0x69, 46830},
	{0x03, 0x6a, 47691},
	{0x03, 0x6b, 48600},
	{0x03, 0x6c, 49500},
	{0x03, 0x6d, 50337},
	{0x03, 0x6e, 51221},
	{0x03, 0x6f, 52042},
	{0x03, 0x70, 52911},
	{0x03, 0x71, 53771},
	{0x03, 0x72, 54571},
	{0x03, 0x73, 55417},
	{0x03, 0x74, 56255},
	{0x03, 0x75, 57034},
	{0x03, 0x76, 57858},
	{0x03, 0x77, 58624},
	{0x03, 0x78, 59434},
	{0x03, 0x79, 60237},
	{0x03, 0x7a, 60984},
	{0x03, 0x7b, 61775},
	{0x03, 0x7c, 62559},
	{0x03, 0x7d, 63288},
	{0x03, 0x7e, 64059},
	{0x03, 0x7f, 64777},
	{0x07, 0x40, 65536},
	{0x07, 0x41, 66990},
	{0x07, 0x42, 68468},
	{0x07, 0x43, 69879},
	{0x07, 0x44, 71268},
	{0x07, 0x45, 72637},
	{0x07, 0x46, 74030},
	{0x07, 0x47, 75360},
	{0x07, 0x48, 76672},
	{0x07, 0x49, 77966},
	{0x07, 0x4a, 79283},
	{0x07, 0x4b, 80542},
	{0x07, 0x4c, 81784},
	{0x07, 0x4d, 83010},
	{0x07, 0x4e, 84260},
	{0x07, 0x4f, 85454},
	{0x07, 0x50, 86634},
	{0x07, 0x51, 87799},
	{0x07, 0x52, 88987},
	{0x07, 0x53, 90124},
	{0x07, 0x54, 91247},
	{0x07, 0x55, 92357},
	{0x07, 0x56, 93489},
	{0x07, 0x57, 94573},
	{0x23, 0x40, 95645},
	{0x23, 0x41, 97112},
	{0x23, 0x42, 98557},
	{0x23, 0x43, 99980},
	{0x23, 0x44, 101381},
	{0x23, 0x45, 102763},
	{0x23, 0x46, 104124},
	{0x23, 0x47, 105466},
	{0x23, 0x48, 106789},
	{0x23, 0x49, 108094},
	{0x23, 0x4a, 109381},
	{0x23, 0x4b, 110651},
	{0x23, 0x4c, 111904},
	{0x23, 0x4d, 113141},
	{0x23, 0x4e, 114362},
	{0x23, 0x4f, 115567},
	{0x23, 0x50, 116757},
	{0x23, 0x51, 117905},
	{0x23, 0x52, 119066},
	{0x23, 0x53, 120213},
	{0x23, 0x54, 121346},
	{0x23, 0x55, 122466},
	{0x23, 0x56, 123573},
	{0x23, 0x57, 124667},
	{0x23, 0x58, 125748},
	{0x23, 0x59, 126818},
	{0x23, 0x5a, 127875},
	{0x23, 0x5b, 128920},
	{0x23, 0x5c, 129954},
	{0x23, 0x5d, 130977},
	{0x23, 0x5e, 131989},
	{0x23, 0x5f, 132991},
	{0x23, 0x60, 133981},
	{0x23, 0x61, 134962},
	{0x23, 0x62, 135932},
	{0x23, 0x63, 136893},
	{0x23, 0x64, 137844},
	{0x23, 0x65, 138785},
	{0x23, 0x66, 139717},
	{0x23, 0x67, 140640},
	{0x23, 0x68, 141555},
	{0x23, 0x69, 142460},
	{0x23, 0x6a, 143357},
	{0x23, 0x6b, 144245},
	{0x23, 0x6c, 145125},
	{0x23, 0x6d, 145997},
	{0x23, 0x6e, 146861},
	{0x23, 0x6f, 147717},
	{0x23, 0x70, 148566},
	{0x23, 0x71, 149387},
	{0x23, 0x72, 150221},
	{0x23, 0x73, 151048},
	{0x23, 0x74, 151867},
	{0x23, 0x75, 152679},
	{0x23, 0x76, 153484},
	{0x23, 0x77, 154283},
	{0x23, 0x78, 155075},
	{0x23, 0x79, 155860},
	{0x23, 0x7a, 156639},
	{0x23, 0x7b, 157411},
	{0x23, 0x7c, 158177},
	{0x23, 0x7d, 158937},
	{0x23, 0x7e, 159691},
	{0x23, 0x7f, 160439},
	{0x27, 0x40, 161181},
	{0x27, 0x41, 162648},
	{0x27, 0x42, 164093},
	{0x27, 0x43, 165516},
	{0x27, 0x44, 166917},
	{0x27, 0x45, 168299},
	{0x27, 0x46, 169660},
	{0x27, 0x47, 171002},
	{0x27, 0x48, 172325},
	{0x27, 0x49, 173615},
	{0x27, 0x4a, 174902},
	{0x27, 0x4b, 176173},
	{0x27, 0x4c, 177426},
	{0x27, 0x4d, 178663},
	{0x27, 0x4e, 179884},
	{0x27, 0x4f, 181089},
	{0x27, 0x50, 182279},
	{0x27, 0x51, 183455},
	{0x27, 0x52, 184616},
	{0x27, 0x53, 185762},
	{0x27, 0x54, 186895},
	{0x27, 0x55, 188015},
	{0x27, 0x56, 189122},
	{0x27, 0x57, 190216},
	{0x27, 0x58, 191297},
	{0x27, 0x59, 192354},
	{0x27, 0x5a, 193411},
	{0x27, 0x5b, 194456},
	{0x27, 0x5c, 195490},
	{0x27, 0x5d, 196513},
	{0x27, 0x5e, 197525},
	{0x27, 0x5f, 198527},
	{0x27, 0x60, 199517},
	{0x27, 0x61, 200498},
	{0x27, 0x62, 201468},
	{0x27, 0x63, 202429},
	{0x27, 0x64, 203380},
	{0x27, 0x65, 204321},
	{0x27, 0x66, 205253},
	{0x27, 0x67, 206176},
	{0x27, 0x68, 207091},
	{0x27, 0x69, 207986},
	{0x27, 0x6a, 208882},
	{0x27, 0x6b, 209771},
	{0x27, 0x6c, 210651},
	{0x27, 0x6d, 211523},
	{0x27, 0x6e, 212387},
	{0x27, 0x6f, 213244},
	{0x27, 0x70, 214092},
	{0x27, 0x71, 214933},
	{0x27, 0x72, 215767},
	{0x27, 0x73, 216593},
	{0x27, 0x74, 217412},
	{0x27, 0x75, 218224},
	{0x27, 0x76, 219030},
	{0x27, 0x77, 219828},
	{0x27, 0x78, 220620},
	{0x27, 0x79, 221396},
	{0x27, 0x7a, 222175},
	{0x27, 0x7b, 222947},
	{0x27, 0x7c, 223713},
	{0x27, 0x7d, 224473},
	{0x27, 0x7e, 225227},
	{0x27, 0x7f, 225975},
	{0x2f, 0x40, 226717},
	{0x2f, 0x41, 228184},
	{0x2f, 0x42, 229629},
	{0x2f, 0x43, 231052},
	{0x2f, 0x44, 232453},
	{0x2f, 0x45, 233827},
	{0x2f, 0x46, 235188},
	{0x2f, 0x47, 236530},
	{0x2f, 0x48, 237854},
	{0x2f, 0x49, 239159},
	{0x2f, 0x4a, 240446},
	{0x2f, 0x4b, 241716},
	{0x2f, 0x4c, 242969},
	{0x2f, 0x4d, 244199},
	{0x2f, 0x4e, 245420},
	{0x2f, 0x4f, 246625},
	{0x2f, 0x50, 247815},
	{0x2f, 0x51, 248991},
	{0x2f, 0x52, 250152},
	{0x2f, 0x53, 251298},
	{0x2f, 0x54, 252431},
	{0x2f, 0x55, 253545},
	{0x2f, 0x56, 254651},
	{0x2f, 0x57, 255745},
	{0x2f, 0x58, 256827},
	{0x2f, 0x59, 257896},
	{0x2f, 0x5a, 258953},
	{0x2f, 0x5b, 259998},
	{0x2f, 0x5c, 261032},
	{0x2f, 0x5d, 262049},
	{0x2f, 0x5e, 263061},
	{0x2f, 0x5f, 264063},
	{0x2f, 0x60, 265053},
	{0x2f, 0x61, 266034},
	{0x2f, 0x62, 267004},
	{0x2f, 0x63, 267965},
	{0x2f, 0x64, 268916},
	{0x2f, 0x65, 269852},
	{0x2f, 0x66, 270784},
	{0x2f, 0x67, 271707},
	{0x2f, 0x68, 272621},
	{0x2f, 0x69, 273527},
	{0x2f, 0x6a, 274424},
	{0x2f, 0x6b, 275312},
	{0x2f, 0x6c, 276192},
	{0x2f, 0x6d, 277059},
	{0x2f, 0x6e, 277923},
	{0x2f, 0x6f, 278780},
	{0x2f, 0x70, 279628},
	{0x2f, 0x71, 280469},
	{0x2f, 0x72, 281303},
	{0x2f, 0x73, 282129},
	{0x2f, 0x74, 282948},
	{0x2f, 0x75, 283756},
	{0x2f, 0x76, 284561},
	{0x2f, 0x77, 285360},
	{0x2f, 0x78, 286151},
	{0x2f, 0x79, 286936},
	{0x2f, 0x7a, 287715},
	{0x2f, 0x7b, 288488},
	{0x2f, 0x7c, 289254},
	{0x2f, 0x7d, 290009},
	{0x2f, 0x7e, 290763},
	{0x2f, 0x7f, 291511},
	{0x3f, 0x40, 292253},
	{0x3f, 0x41, 293720},
	{0x3f, 0x42, 295165},
	{0x3f, 0x43, 296584},
	{0x3f, 0x44, 297985},
	{0x3f, 0x45, 299367},
	{0x3f, 0x46, 300728},
	{0x3f, 0x47, 302066},
	{0x3f, 0x48, 303390},
	{0x3f, 0x49, 304695},
	{0x3f, 0x4a, 305982},
	{0x3f, 0x4b, 307248},
	{0x3f, 0x4c, 308501},
	{0x3f, 0x4d, 309738},
	{0x3f, 0x4e, 310959},
	{0x3f, 0x4f, 312161},
	{0x3f, 0x50, 313351},
	{0x3f, 0x51, 314527},
	{0x3f, 0x52, 315688},
	{0x3f, 0x53, 316831},
	{0x3f, 0x54, 317964},
	{0x3f, 0x55, 319084},
	{0x3f, 0x56, 320191},
	{0x3f, 0x57, 321281},
	{0x3f, 0x58, 322363},
	{0x3f, 0x59, 323432},
	{0x3f, 0x5a, 324489},
	{0x3f, 0x5b, 325531},
	{0x3f, 0x5c, 326565},
	{0x3f, 0x5d, 327588},
	{0x3f, 0x5e, 328600},
	{0x3f, 0x5f, 329599},
	{0x3f, 0x60, 330589},
	{0x3f, 0x61, 331570},
	{0x3f, 0x62, 332540},
	{0x3f, 0x63, 333498},
	{0x3f, 0x64, 334449},
	{0x3f, 0x65, 335391},
	{0x3f, 0x66, 336323},
	{0x3f, 0x67, 337243},
	{0x3f, 0x68, 338157},
	{0x3f, 0x69, 339063},
	{0x3f, 0x6a, 339960},
	{0x3f, 0x6b, 340845},
	{0x3f, 0x6c, 341726},
	{0x3f, 0x6d, 342598},
	{0x3f, 0x6e, 343462},
	{0x3f, 0x6f, 344316},
	{0x3f, 0x70, 345164},
	{0x3f, 0x71, 346005},
	{0x3f, 0x72, 346839},
	{0x3f, 0x73, 347663},
	{0x3f, 0x74, 348482},
	{0x3f, 0x75, 349294},
	{0x3f, 0x76, 350099},
	{0x3f, 0x77, 350896},
	{0x3f, 0x78, 351687},
	{0x3f, 0x79, 352472},
	{0x3f, 0x7a, 353251},
	{0x3f, 0x7b, 354021},
	{0x3f, 0x7c, 354788},
	{0x3f, 0x7d, 355547},
	{0x3f, 0x7e, 356301},
	{0x3f, 0x7f, 357047},
};

static inline struct gc4018a_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc4018a_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct gc4018a_info, hdl)->sd;
}

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

static struct regval_list gc4018a_init_regs_2560_1440_30fps_mipi[] = {
    {0x0103,0x01},//30fps
    {0x0100,0x00},
    {0x36e9,0x80},
    {0x36f9,0x80},
    {0x3000,0x00},
    {0x3001,0x07},
    {0x3002,0xc0},
    {0x300a,0x2c},
    {0x300f,0x00},
    {0x3018,0x73},
    {0x3031,0x0c},//RAW12
    {0x3038,0x22},
    {0x320c,0x05},
    {0x320d,0x46},
    {0x3220,0x10},
    {0x3225,0x01},
    {0x3227,0x03},
    {0x3229,0x08},
    {0x3231,0x01},
    {0x3241,0x02},
    {0x3243,0x03},
    {0x3249,0x17},
    {0x3251,0x08},
    {0x3253,0x08},
    {0x325e,0x00},
    {0x325f,0x00},
    {0x3273,0x01},
    {0x3301,0x28},
    {0x3302,0x18},
    {0x3304,0x20},
    {0x3305,0x00},
    {0x3306,0x74},
    {0x3308,0x10},
    {0x3309,0x40},
    {0x330a,0x00},
    {0x330b,0xe8},
    {0x330e,0x18},
    {0x3312,0x02},
    {0x3314,0x84},
    {0x331e,0x19},
    {0x331f,0x39},
    {0x3320,0x05},
    {0x3338,0x10},
    {0x334c,0x10},
    {0x335d,0x20},
    {0x3366,0x92},
    {0x3367,0x08},
    {0x3368,0x05},
    {0x3369,0xdc},
    {0x336a,0x0b},
    {0x336b,0xb8},
    {0x336c,0xc2},
    {0x337a,0x08},
    {0x337b,0x10},
    {0x337e,0x40},
    {0x33a3,0x0c},
    {0x33e0,0xa0},
    {0x33e1,0x08},
    {0x33e2,0x00},
    {0x33e3,0x10},
    {0x33e4,0x10},
    {0x33e5,0x00},
    {0x33e6,0x10},
    {0x33e7,0x10},
    {0x33e8,0x00},
    {0x33e9,0x10},
    {0x33ea,0x16},
    {0x33eb,0x00},
    {0x33ec,0x10},
    {0x33ed,0x18},
    {0x33ee,0xa0},
    {0x33ef,0x08},
    {0x33f4,0x00},
    {0x33f5,0x10},
    {0x33f6,0x10},
    {0x33f7,0x00},
    {0x33f8,0x10},
    {0x33f9,0x10},
    {0x33fa,0x00},
    {0x33fb,0x10},
    {0x33fc,0x16},
    {0x33fd,0x00},
    {0x33fe,0x10},
    {0x33ff,0x18},
    {0x360f,0x05},
    {0x3622,0xff},
    {0x3624,0x07},
    {0x3625,0x02},
    {0x3630,0xc4},
    {0x3631,0x80},
    {0x3632,0x88},
    {0x3633,0x22},
    {0x3634,0x64},
    {0x3635,0x20},
    {0x3636,0x20},
    {0x3638,0x28},
    {0x363b,0x03},
    {0x363c,0x06},
    {0x363d,0x06},
    {0x366e,0x04},
    {0x3670,0x48},
    {0x3671,0xff},
    {0x3672,0x1f},
    {0x3673,0x1f},
    {0x367a,0x40},
    {0x367b,0x40},
    {0x3690,0x42},
    {0x3691,0x44},
    {0x3692,0x44},
    {0x3699,0x80},
    {0x369a,0x9f},
    {0x369b,0x9f},
    {0x369c,0x40},
    {0x369d,0x40},
    {0x36a2,0x40},
    {0x36a3,0x40},
    {0x36cc,0x2c},
    {0x36cd,0x30},
    {0x36ce,0x30},
    {0x36d0,0x20},
    {0x36d1,0x40},
    {0x36d2,0x40},
    {0x36ea,0x37},
    {0x36eb,0x16},
    {0x36ec,0x03},
    {0x36ed,0x0c},
    {0x36fa,0x37},
    {0x36fb,0x14},
    {0x36fc,0x00},
    {0x36fd,0x2c},
    {0x3817,0x20},
    {0x3905,0xd8},
    {0x3908,0x11},
    {0x391b,0x80},
    {0x391c,0x0f},
    {0x391d,0x01},
    {0x3933,0x24},
    {0x3934,0xb0},
    {0x3935,0x80},
    {0x3936,0x1f},
    {0x3940,0x68},
    {0x3942,0x04},
    {0x3943,0xc0},
    {0x3980,0x00},
    {0x3981,0x50},
    {0x3982,0x00},
    {0x3983,0x40},
    {0x3984,0x00},
    {0x3985,0x20},
    {0x3986,0x00},
    {0x3987,0x10},
    {0x3988,0x00},
    {0x3989,0x20},
    {0x398a,0x00},
    {0x398b,0x30},
    {0x398c,0x00},
    {0x398d,0x50},
    {0x398e,0x00},
    {0x398f,0x60},
    {0x3990,0x00},
    {0x3991,0x70},
    {0x3992,0x00},
    {0x3993,0x36},
    {0x3994,0x00},
    {0x3995,0x20},
    {0x3996,0x00},
    {0x3997,0x14},
    {0x3998,0x00},
    {0x3999,0x20},
    {0x399a,0x00},
    {0x399b,0x50},
    {0x399c,0x00},
    {0x399d,0x90},
    {0x399e,0x00},
    {0x399f,0xf0},
    {0x39a0,0x08},
    {0x39a1,0x10},
    {0x39a2,0x20},
    {0x39a3,0x40},
    {0x39a4,0x20},
    {0x39a5,0x10},
    {0x39a6,0x08},
    {0x39a7,0x04},
    {0x39a8,0x18},
    {0x39a9,0x30},
    {0x39aa,0x40},
    {0x39ab,0x60},
    {0x39ac,0x38},
    {0x39ad,0x20},
    {0x39ae,0x10},
    {0x39af,0x08},
    {0x39b9,0x00},
    {0x39ba,0xa0},
    {0x39bb,0x80},
    {0x39bc,0x00},
    {0x39bd,0x44},
    {0x39be,0x00},
    {0x39bf,0x00},
    {0x39c0,0x00},
    {0x39c5,0x41},
    {0x3e00,0x00},
    {0x3e01,0xbb},
    {0x3e02,0x40},
    {0x3e03,0x0b},
    {0x3e06,0x00},
    {0x3e07,0x80},
    {0x3e08,0x03},
    {0x3e09,0x40},
    {0x3e0e,0x6a},
    {0x3e26,0x40},
    {0x4407,0xb0},
    {0x4418,0x0b},
    {0x4501,0xb4},
    {0x4509,0x10},
    {0x4603,0x00},
    {0x4837,0x2c},
    {0x5000,0x0e},
    {0x550f,0x20},
    {0x5784,0x10},
    {0x5785,0x08},
    {0x5787,0x06},
    {0x5788,0x06},
    {0x5789,0x00},
    {0x578a,0x06},
    {0x578b,0x06},
    {0x578c,0x00},
    {0x5790,0x10},
    {0x5791,0x10},
    {0x5792,0x00},
    {0x5793,0x10},
    {0x5794,0x10},
    {0x5795,0x00},
    {0x57c4,0x10},
    {0x57c5,0x08},
    {0x57c7,0x06},
    {0x57c8,0x06},
    {0x57c9,0x00},
    {0x57ca,0x06},
    {0x57cb,0x06},
    {0x57cc,0x00},
    {0x57d0,0x10},
    {0x57d1,0x10},
    {0x57d2,0x00},
    {0x57d3,0x10},
    {0x57d4,0x10},
    {0x57d5,0x00},
    {0x36e9,0x27},
    {0x36f9,0x20},

    //defect pixel correct
    {0x5780,0xe7},
    {0x5787,0x00},
    {0x5790,0x00},
//    {0x0100,0x01},

//    {GC4018A_REG_DELAY, 10},
    {GC4018A_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list gc4018a_init_regs_2560_1440_60fps_mipi[] = {
    {0x0103,0x01}, //60fps
    {0x0100,0x00}, 
    {0x3001,0x07}, 
    {0x3002,0xc0}, 
    {0x300a,0x2c}, 
    {0x300f,0x00}, 
    {0x3018,0x73}, 
    {0x3031,0x0a}, //RAW10
    {0x3038,0x22}, 
    {0x320c,0x05}, 
    {0x320d,0x46},     
    {0x3220,0x10}, 
    {0x3225,0x01}, 
    {0x3227,0x03}, 
    {0x3229,0x08}, 
    {0x3231,0x01}, 
    {0x3241,0x02}, 
    {0x3243,0x03}, 
    {0x3249,0x17}, 
    {0x3251,0x08}, 
    {0x3253,0x10}, 
    {0x325e,0x00}, 
    {0x325f,0x00}, 
    {0x3273,0x01}, 
    {0x3301,0x24}, 
    {0x3302,0x18}, 
    {0x3304,0x20}, 
    {0x3305,0x00}, 
    {0x3000,0x00}, 
    {0x3306,0x78}, 
    {0x3308,0x10}, 
    {0x3309,0x50}, 
    {0x330a,0x00}, 
    {0x330b,0xe0}, 
    {0x330e,0x20}, 
    {0x3312,0x02}, 
    {0x3314,0x84}, 
    {0x331e,0x19}, 
    {0x331f,0x49}, 
    {0x3320,0x05}, 
    {0x3338,0x10}, 
    {0x334c,0x10}, 
    {0x335d,0x20}, 
    {0x3364,0x1e}, 
    {0x3366,0x92}, 
    {0x3367,0x08}, 
    {0x3368,0x05}, 
    {0x3369,0xdc}, 
    {0x336a,0x0b}, 
    {0x336b,0xb8}, 
    {0x336c,0xc2}, 
    {0x337a,0x08}, 
    {0x337b,0x10}, 
    {0x337e,0x40}, 
    {0x3390,0x08}, 
    {0x3391,0x08}, 
    {0x3392,0x08}, 
    {0x3393,0x24}, 
    {0x3394,0x24}, 
    {0x3395,0x24}, 
    {0x3396,0x08}, 
    {0x3397,0x38}, 
    {0x3398,0x3c}, 
    {0x3399,0x1c}, 
    {0x339a,0x26}, 
    {0x339b,0x1d}, 
    {0x339c,0x26}, 
    {0x33a3,0x0c}, 
    {0x33e0,0xa0}, 
    {0x33e1,0x08}, 
    {0x33e2,0x00}, 
    {0x33e3,0x10}, 
    {0x33e4,0x10}, 
    {0x33e5,0x00}, 
    {0x33e6,0x10}, 
    {0x33e7,0x10}, 
    {0x33e8,0x00}, 
    {0x33e9,0x10}, 
    {0x33ea,0x16}, 
    {0x33eb,0x00}, 
    {0x33ec,0x10}, 
    {0x33ed,0x18}, 
    {0x33ee,0xa0}, 
    {0x33ef,0x08}, 
    {0x33f4,0x00}, 
    {0x33f5,0x10}, 
    {0x33f6,0x10}, 
    {0x33f7,0x00}, 
    {0x33f8,0x10}, 
    {0x33f9,0x10}, 
    {0x33fa,0x00}, 
    {0x33fb,0x10}, 
    {0x33fc,0x16}, 
    {0x33fd,0x00}, 
    {0x33fe,0x10}, 
    {0x33ff,0x18}, 
    {0x360f,0x05}, 
    {0x3622,0xff}, 
    {0x3624,0x07}, 
    {0x3625,0x02}, 
    {0x3630,0xc4}, 
    {0x3631,0x80}, 
    {0x3632,0x88}, 
    {0x3633,0x42}, 
    {0x3634,0x64}, 
    {0x3635,0x20}, 
    {0x3636,0x20}, 
    {0x3638,0x28}, 
    {0x363b,0x03}, 
    {0x363c,0x06}, 
    {0x363d,0x06}, 
    {0x366e,0x04}, 
    {0x3670,0x48}, 
    {0x3671,0xff}, 
    {0x3672,0x9f}, 
    {0x3673,0x9f}, 
    {0x367a,0x40}, 
    {0x367b,0x48}, 
    {0x3690,0x43}, 
    {0x3691,0x54}, 
    {0x3692,0x66}, 
    {0x3699,0x8c}, 
    {0x369a,0x96}, 
    {0x369b,0x9f}, 
    {0x369c,0x40}, 
    {0x369d,0x48}, 
    {0x36a2,0x40}, 
    {0x36a3,0x48}, 
    {0x36cc,0x2c}, 
    {0x36cd,0x30}, 
    {0x36ce,0x30}, 
    {0x36d0,0x20}, 
    {0x36d1,0x40}, 
    {0x36d2,0x40}, 
    {0x36e9,0x34}, 
    {0x36ea,0x23}, 
    {0x36eb,0x06}, 
    {0x36ec,0x03}, 
    {0x36ed,0x0c}, 
    {0x36f9,0x20}, 
    {0x36fa,0x37}, 
    {0x36fb,0x04}, 
    {0x36fc,0x00}, 
    {0x36fd,0x2c}, 
    {0x3817,0x20}, 
    {0x3905,0xd8}, 
    {0x3908,0x11}, 
    {0x391b,0x80}, 
    {0x391c,0x0f}, 
    {0x391d,0x21}, 
    {0x3933,0x1f}, 
    {0x3934,0xff}, 
    {0x3935,0x80}, 
    {0x3936,0x1f}, 
    {0x393e,0x01}, 
    {0x3940,0x60}, 
    {0x3942,0x04}, 
    {0x3943,0xd0}, 
    {0x3980,0x00}, 
    {0x3981,0x30}, 
    {0x3982,0x00}, 
    {0x3983,0x2c}, 
    {0x3984,0x00}, 
    {0x3985,0x15}, 
    {0x3986,0x00}, 
    {0x3987,0x10}, 
    {0x3988,0x00}, 
    {0x3989,0x30}, 
    {0x398a,0x00}, 
    {0x398b,0x28}, 
    {0x398c,0x00}, 
    {0x398d,0x30}, 
    {0x398e,0x00}, 
    {0x398f,0x70}, 
    {0x3990,0x0a}, 
    {0x3991,0x00}, 
    {0x3992,0x00}, 
    {0x3993,0x60}, 
    {0x3994,0x00}, 
    {0x3995,0x30}, 
    {0x3996,0x00}, 
    {0x3997,0x10}, 
    {0x3998,0x00}, 
    {0x3999,0x1c}, 
    {0x399a,0x00}, 
    {0x399b,0x48}, 
    {0x399c,0x00}, 
    {0x399d,0x90}, 
    {0x399e,0x00}, 
    {0x399f,0xc0}, 
    {0x39a0,0x14}, 
    {0x39a1,0x28}, 
    {0x39a2,0x48}, 
    {0x39a3,0x70}, 
    {0x39a4,0x18}, 
    {0x39a5,0x04}, 
    {0x39a6,0x08}, 
    {0x39a7,0x04}, 
    {0x39a8,0x01}, 
    {0x39a9,0x14}, 
    {0x39aa,0x28}, 
    {0x39ab,0x50}, 
    {0x39ac,0x30}, 
    {0x39ad,0x20}, 
    {0x39ae,0x10}, 
    {0x39af,0x08}, 
    {0x39b9,0x00}, 
    {0x39ba,0x00}, 
    {0x39bb,0x00}, 
    {0x39bc,0x00}, 
    {0x39bd,0x58}, 
    {0x39be,0xc0}, 
    {0x39bf,0x00}, 
    {0x39c0,0x00}, 
    {0x39c5,0x41}, 
    {0x3e00,0x00}, 
    {0x3e01,0xbb}, 
    {0x3e02,0x40}, 
    {0x3e03,0x0b}, 
    {0x3e06,0x00}, 
    {0x3e07,0x80}, 
    {0x3e08,0x03}, 
    {0x3e09,0x40}, 
    {0x3e0e,0x6a}, 
    {0x3e26,0x40}, 
    {0x4407,0xb0}, 
    {0x4418,0x16}, 
    {0x4501,0xa4}, 
    {0x4509,0x08}, 
    {0x4603,0x00}, 
    {0x4819,0x40}, 
    {0x4829,0x01}, 
    {0x4837,0x1b}, 
    {0x5000,0x06}, 
    {0x550f,0x20}, 
    {0x5784,0x10}, 
    {0x5785,0x08}, 
    {0x5787,0x06}, 
    {0x5788,0x06}, 
    {0x5789,0x00}, 
    {0x578a,0x06}, 
    {0x578b,0x06}, 
    {0x578c,0x00}, 
    {0x5790,0x10}, 
    {0x5791,0x10}, 
    {0x5792,0x00}, 
    {0x5793,0x10}, 
    {0x5794,0x10}, 
    {0x5795,0x00}, 
    {0x57c4,0x10}, 
    {0x57c5,0x08}, 
    {0x57c7,0x06}, 
    {0x57c8,0x06}, 
    {0x57c9,0x00}, 
    {0x57ca,0x06}, 
    {0x57cb,0x06}, 
    {0x57cc,0x00}, 
    {0x57d0,0x10}, 
    {0x57d1,0x10}, 
    {0x57d2,0x00}, 
    {0x57d3,0x10}, 
    {0x57d4,0x10}, 
    {0x57d5,0x00}, 
//    {0x0100,0x01}, 

//	{GC4018A_REG_DELAY, 10},
	{GC4018A_REG_END, 0x00},	/* END MARKER */
};
static struct regval_list gc4018a_stream_on[] = {
	{0x0100,0x01},
	{GC4018A_REG_END, 0x00},
};

static struct regval_list gc4018a_stream_off[] = {
	{0x0100,0x00},
	{GC4018A_REG_END, 0x00},
};


int gc4018a_read(struct v4l2_subdev *sd, unsigned short reg,
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
	int ret;
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int gc4018a_write(struct v4l2_subdev *sd, unsigned short reg,
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
	unsigned int timeout  = 100;
	while(timeout--){
		ret = i2c_transfer(client->adapter, &msg, 1);
		if(ret == -EAGAIN){
			msleep(100);
			continue;
		}
		else
			break;
	}
	if (ret > 0)
		ret = 0;
	return ret;
}

static int gc4018a_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != GC4018A_REG_END) {
		if (vals->reg_num == GC4018A_REG_DELAY) {
				msleep(vals->value);
		} else {
			ret = gc4018a_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
			if (vals->reg_num == GC4018A_PAGE_REG){
				val &= 0xf8;
				val |= (vals->value & 0x07);
				ret = gc4018a_write(sd, vals->reg_num, val);
				ret = gc4018a_read(sd, vals->reg_num, &val);
			}
		}
		vals++;
	}
	return 0;
}

static int gc4018a_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != GC4018A_REG_END) {
		if (vals->reg_num == GC4018A_REG_DELAY) {
				msleep(vals->value);
		} else {
			ret = gc4018a_write(sd, vals->reg_num, vals->value);
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

void gc4018a_power_init_seq(struct gc4018a_info *info)
{
	*(volatile unsigned int*)0xb0010100 = 0x1;  //set gpio power domain 1.8v
	gpio_direction_output(4,0);
	udelay(50);

	gpio_direction_output(info->reset.pin,!info->reset.active_level);
	msleep(10);//40
	gpio_direction_output(info->reset.pin,info->reset.active_level);
	msleep(10);//40
	gpio_direction_output(info->reset.pin,!info->reset.active_level);
	msleep(10);//40
}

static int gc4018a_pwdn(struct v4l2_subdev *sd, u32 val)
{
	struct gc4018a_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->pwdn.pin, info->pwdn.active_level);
		msleep(10);
	} else {
		gpio_direction_output(info->pwdn.pin, !info->pwdn.active_level);
	}
	return 0;
}

static int gc4018a_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;

	ret = gc4018a_read(sd, 0x3107, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != GC4018A_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = gc4018a_read(sd, 0x3108, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != GC4018A_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;
	return 0;
}

static struct gc4018a_win_size gc4018a_win_sizes[] = {
	{
		.width				= 2560,
		.height				= 1440,
		.sensor_info.fps				= 30 << 16 | 1,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 720,
		.sensor_info.mipi_cfg.twidth		= 2560,
		.sensor_info.mipi_cfg.theight		= 1440,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_OTHER_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en		= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en		= 0,
		.sensor_info.mipi_cfg.data_type_value	= RAW12,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt		= TX_SENSOR_RAW12,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.xfer_func   	= V4L2_XFER_FUNC_SRGB,
		.field          = V4L2_FIELD_NONE,
		.ycbcr_enc      = V4L2_YCBCR_ENC_DEFAULT,
		.quantization   = V4L2_QUANTIZATION_FULL_RANGE,
		.regs 		= gc4018a_init_regs_2560_1440_30fps_mipi,
	},
	{
		.width				= 2560,
		.height				= 1440,
		.sensor_info.fps				= 60 << 16 | 1,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 720,
		.sensor_info.mipi_cfg.twidth		= 2560,
		.sensor_info.mipi_cfg.theight		= 1440,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_OTHER_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en		= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en		= 0,
		.sensor_info.mipi_cfg.data_type_value	= RAW10,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt		= TX_SENSOR_RAW10,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.xfer_func   	= V4L2_XFER_FUNC_SRGB,
		.field          = V4L2_FIELD_NONE,
		.ycbcr_enc      = V4L2_YCBCR_ENC_DEFAULT,
		.quantization   = V4L2_QUANTIZATION_FULL_RANGE,
		.regs 		= gc4018a_init_regs_2560_1440_60fps_mipi,
	},
};

#if 0
static int gc4018a_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= N_OV2735a_FMTS)
		return -EINVAL;

	code->code = gc4018a_formats[code->index].mbus_code;
	return 0;
}
#endif

/*
 * Set a format.
 */
static int gc4018a_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	if (format->pad)
		return -EINVAL;

	return 0;
}

static int gc4018a_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format)
{
	struct gc4018a_info *info = to_state(sd);
	struct gc4018a_win_size *wsize = info->win;
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
	fmt->xfer_func = wsize->xfer_func;
	fmt->field = wsize->field;
	fmt->ycbcr_enc = wsize->ycbcr_enc;
	fmt->quantization = wsize->quantization;

	*(unsigned int *)fmt->reserved = (unsigned int)&wsize->sensor_info; /*reserved[0] reserved[1]*/

//	printk("----%s, %d, width: %d, height: %d, code: %x\n",
//			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int gc4018a_s_wdr(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	struct gc4018a_info *info = to_state(sd);
	if(value)
		info->win = &gc4018a_win_sizes[0];
	else
		info->win = &gc4018a_win_sizes[0];
	return ret;
}

static int gc4018a_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc4018a_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc4018a_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc4018a_s_vflip(struct v4l2_subdev *sd, int value)
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
static int gc4018a_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int gc4018a_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

//	printk("---%s, %d, s_gain: value: %d\n", __func__, __LINE__, value);

	return ret;
}

static unsigned int again_to_regval(int gain, unsigned int *value, unsigned int *fine_value)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(gc4018a_again_lut); i++) {
		lut = &gc4018a_again_lut[i];

		if(gain <= lut->gain) {
			*value = lut->value;
			*fine_value = lut->fine_value;
			return lut->value;
		}
	}
	/*last value.*/
	*value = lut->value;
	*fine_value = lut->fine_value;
	return lut->value;
}

static int regval_to_again(unsigned int regval, unsigned int fine_reg_val)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(gc4018a_again_lut); i++) {
		lut = &gc4018a_again_lut[i];

		if(regval == lut->value && fine_reg_val == lut->fine_value) {
			return lut->gain;
		}
	}
	printk("%s, %d regval not mapped to isp gain\n", __func__, __LINE__);
	return -EINVAL;
}

static int gc4018a_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	char v = 0;
	unsigned int reg_val = 0;
	unsigned int fine_reg_val = 0;
	int ret = 0;

	ret = gc4018a_read(sd, 0x3e08, &v);
	reg_val = v ;
	ret = gc4018a_read(sd, 0x3e09, &v);
	fine_reg_val = v ;

	*value = regval_to_again(reg_val, fine_reg_val);

	return ret;

}
static int gc4018a_g_again_short(struct v4l2_subdev *sd, __s32 *value)
{
	char v = 0;
	unsigned int reg_val = 0;
	unsigned int fine_reg_val = 0;
	int ret = 0;

#if 0
	ret = gc4018a_read(sd, 0x3e13, &v);
	reg_val = v ;
	ret = gc4018a_read(sd, 0x3e11, &v);
	fine_reg_val = v ;

	*value = regval_to_again(reg_val, fine_reg_val);

#endif
	return ret;

}
/*set analog gain db value, map value to sensor register.*/
static int gc4018a_s_again(struct v4l2_subdev *sd, int value)
{
	struct gc4018a_info *info = to_state(sd);
	unsigned int reg_value = 0;
	unsigned int reg_fine_value;
	int ret = 0;

	if(value < info->again->minimum || value > info->again->maximum) {
		/* use default value. */
		again_to_regval(info->again->default_value, &reg_value, &reg_fine_value);
	} else {
		again_to_regval(value, &reg_value, &reg_fine_value);
	}

	ret += gc4018a_write(sd, 0x3e08, (unsigned char)(reg_value));
	ret += gc4018a_write(sd, 0x3e09, (unsigned char)(reg_fine_value));
	if (ret < 0){
		printk("gc4018a_write error  %d\n" ,__LINE__ );
		return ret;
	}

	return 0;
}
static int gc4018a_s_again_short(struct v4l2_subdev *sd, int value)
{
	struct gc4018a_info *info = to_state(sd);
	unsigned int reg_value;
	unsigned int reg_fine_value;
	int ret = 0;

#if 0
	if(value < info->again->minimum || value > info->again->maximum) {
		/* use default value. */
		again_to_regval(info->again->default_value, &reg_value, &reg_fine_value);
	} else {
		again_to_regval(value, &reg_value, &reg_fine_value);
	}

	ret += gc4018a_write(sd, 0x3e13, (unsigned char)(reg_value));
	ret += gc4018a_write(sd, 0x3e11, (unsigned char)(reg_fine_value));
	if (ret < 0){
		printk("gc4018a_write error  %d\n" ,__LINE__ );
		return ret;
	}

#endif
	return 0;
}
/*
 * Tweak autogain.
 */
static int gc4018a_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc4018a_s_exp(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

//	value *= 2; /*unit in half line*/
	ret += gc4018a_write(sd, 0x3e00, (unsigned char)((value >> 12) & 0xf));
	ret += gc4018a_write(sd, 0x3e01, (unsigned char)((value >> 4) & 0xff));
	ret += gc4018a_write(sd, 0x3e02, (unsigned char)(value & 0xf) << 4);

	if (ret < 0) {
		printk("gc4018a_write error  %d\n" ,__LINE__);
		return ret;
	}

	return 0;
}
static int gc4018a_s_exp_short(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

#if 0
	value *= 2; /*unit in half line*/
	ret += gc4018a_write(sd, 0x3e22, (unsigned char)((value >> 12) & 0xf));
	ret += gc4018a_write(sd, 0x3e04, (unsigned char)((value >> 4) & 0xff));
	ret += gc4018a_write(sd, 0x3e05, (unsigned char)(value & 0xf) << 4);

	if (ret < 0) {
		printk("gc4018a_write error  %d\n" ,__LINE__);
		return ret;
	}

#endif
	return 0;
}

static int gc4018a_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc4018a_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return gc4018a_g_gain(sd, &info->gain->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return gc4018a_g_again(sd, &info->again->val);
	case V4L2_CID_USER_ANALOG_GAIN_SHORT:
		return gc4018a_g_again_short(sd, &info->again_short->val);
	}
	return -EINVAL;
}

static int gc4018a_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc4018a_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return gc4018a_s_brightness(sd, ctrl->val);
	case V4L2_CID_CONTRAST:
		return gc4018a_s_contrast(sd, ctrl->val);
	case V4L2_CID_VFLIP:
		return gc4018a_s_vflip(sd, ctrl->val);
	case V4L2_CID_HFLIP:
		return gc4018a_s_hflip(sd, ctrl->val);
	case V4L2_CID_AUTOGAIN:
	      /* Only set manual gain if auto gain is not explicitly
	         turned on. */
		if (!ctrl->val) {
	      	/* gc4018a_s_gain turns off auto gain */
			return gc4018a_s_gain(sd, info->gain->val);
		}
			return gc4018a_s_autogain(sd, ctrl->val);
	case V4L2_CID_GAIN:
		return gc4018a_s_gain(sd, ctrl->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return gc4018a_s_again(sd, ctrl->val);
	case V4L2_CID_USER_ANALOG_GAIN_SHORT:
		return gc4018a_s_again_short(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
		return gc4018a_s_exp(sd, ctrl->val);
	case V4L2_CID_USER_EXPOSURE_SHORT:
		return gc4018a_s_exp_short(sd, ctrl->val);
	case V4L2_CID_WIDE_DYNAMIC_RANGE:
		return gc4018a_s_wdr(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops gc4018a_ctrl_ops = {
	.s_ctrl = gc4018a_s_ctrl,
	.g_volatile_ctrl = gc4018a_g_volatile_ctrl,
};


#ifdef CONFIG_VIDEO_ADV_DEBUG
static int gc4018a_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = gc4018a_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int gc4018a_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	gc4018a_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

static int gc4018a_core_reset(struct v4l2_subdev *sd, u32 val)
{
	struct gc4018a_info *info = to_state(sd);
	unsigned char v;
	int ret;

	/*software reset*/
	if(info->init_flag)
		return 0;

	ret = gc4018a_read(sd, 0x0103, &v);

	if(val) {
		v |= 1;
		ret += gc4018a_write(sd, 0x0103, v);
	}
	return 0;
}

static int gc4018a_core_init(struct v4l2_subdev *sd, u32 val)
{
	struct gc4018a_info *info = to_state(sd);
	int ret = 0;

	if(!info->init_flag) {
		ret = gc4018a_write_array(sd, info->win->regs);
	}

	return ret;
}

int gc4018a_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;

	if (enable) {
		ret = gc4018a_write_array(sd, gc4018a_stream_on);
		printk("gc4018a stream on\n");

	}
	else {
		ret = gc4018a_write_array(sd, gc4018a_stream_off);
		printk("gc4018a stream off\n");
	}

	return ret;
}

int gc4018a_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	struct gc4018a_info *info = to_state(sd);
	if(info->win->sensor_info.fps){
		param->parm.capture.timeperframe.numerator = info->win->sensor_info.fps & 0xffff;
		param->parm.capture.timeperframe.denominator = info->win->sensor_info.fps >> 16;
		return 0;
	}
	return -EINVAL;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops gc4018a_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = gc4018a_g_register,
	.s_register = gc4018a_s_register,
#endif
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
	.init = gc4018a_core_init,

};

static const struct v4l2_subdev_video_ops gc4018a_video_ops = {
	.s_stream = gc4018a_s_stream,
	.g_parm	= gc4018a_g_parm,
};

static const struct v4l2_subdev_pad_ops gc4018a_pad_ops = {
	//.enum_mbus_code = gc4018a_enum_mbus_code,
	.set_fmt = gc4018a_set_fmt,
	.get_fmt = gc4018a_get_fmt,
};

static const struct v4l2_subdev_ops gc4018a_ops = {
	.core = &gc4018a_core_ops,
	.video = &gc4018a_video_ops,
	.pad = &gc4018a_pad_ops,
};

/* ----------------------------------------------------------------------- */

#ifdef CONFIG_X2500_RISCV
extern int register_riscv_notifier(struct notifier_block *nb);
int gc4018a_riscv_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	struct gc4018a_info *info = container_of(nb, struct gc4018a_info, nb);
	struct v4l2_subdev *sd = &info->sd;
	struct ispcam_device *ispcam = container_of(sd->v4l2_dev, struct ispcam_device, v4l2_dev);
	struct message *message = (unsigned int)action | 0x80000000;
	unsigned int *data_paddr;
	unsigned int *vaddr;

	while(message->type != MESSAGE_END){
		if((message->type == MESSAGE_INIT_SENSOR0 && ispcam->dev_nr == 0) ||
		   (message->type == MESSAGE_INIT_SENSOR1 && ispcam->dev_nr == 1) ||
		   (message->type == MESSAGE_INIT_SENSOR2 && ispcam->dev_nr == 2)){
			data_paddr = message->data;
			vaddr = (unsigned int)data_paddr | 0x80000000;
			info->init_flag = *vaddr;
			break;
		}
		message++;
	}
	return 0;
}
#endif


static int gc4018a_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct gc4018a_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	int mclk_index = -1;
	char id_div[9];
	struct v4l2_ctrl_config cfg = {0};

	dev_info(&client->dev, "gc4018a Probe !!!\n");

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->reset.pin = gpio;
		info->reset.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}

	v4l2_i2c_subdev_init(sd, client, &gc4018a_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	gc4018a_power_init_seq(info);
	msleep(5);

	/*clk*/
	of_property_read_u32(client->dev.of_node, "ingenic,mclk", &mclk_index);
	if(mclk_index == 0) {
		memcpy(id_div, "div_cim0", sizeof(id_div));
	} else if(mclk_index == 1) {
		memcpy(id_div, "div_cim1", sizeof(id_div));
	} else if(mclk_index == 2) {
		memcpy(id_div, "div_cim2", sizeof(id_div));
	} else
		printk("Unkonwn mclk index\n");

	info->clk = v4l2_clk_get(&client->dev, id_div);
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

	/* Make sure it's an gc4018a */
	ret = gc4018a_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
			"chip found @ 0x%x (%s) is not an gc4018a chip.\n",
			client->addr << 1, client->adapter->name);
		return ret;
	}

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_WIDE_DYNAMIC_RANGE, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 357047, 1, 1000);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &gc4018a_ctrl_ops,
			V4L2_CID_EXPOSURE, 1, 2996, 2, 600);

#if 0
	cfg.ops = &gc4018a_ctrl_ops;
	cfg.id = V4L2_CID_USER_EXPOSURE_SHORT;
	cfg.name = "expo short";
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.min = 1;
	cfg.max = 131;
	cfg.step = 4;
	cfg.def = 50;
	info->exposure_short = v4l2_ctrl_new_custom(&info->hdl, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.ops = &gc4018a_ctrl_ops;
	cfg.id = V4L2_CID_USER_ANALOG_GAIN_SHORT;
	cfg.name = "analog gain short";
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.min = 0;
	cfg.max = 589824;
	cfg.step = 1;
	cfg.def = 10000;
	info->again_short = v4l2_ctrl_new_custom(&info->hdl, &cfg, NULL);
#endif

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;

		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &gc4018a_win_sizes[0];
	gc4018a_write_array(sd, info->win->regs);


	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	info->sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&info->sd.entity, 1, &info->pad, 0);
	if(ret < 0) {
		goto err_entity_init;
	}
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

#ifdef CONFIG_X2500_RISCV
	info->nb.notifier_call = gc4018a_riscv_notifier;
	register_riscv_notifier(&info->nb);
#endif

	dev_info(&client->dev, "gc4018a Probed\n");
	return 0;
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}


static int gc4018a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4018a_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id gc4018a_id[] = {
	{ "gc4018a", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc4018a_id);

static const struct of_device_id gc4018a_of_match[] = {
	{.compatible = "smartsens,gc4018a", },
	{},
};
MODULE_DEVICE_TABLE(of, gc4018a_of_match);


static struct i2c_driver gc4018a_driver = {
	.driver = {
		.name	= "gc4018a",
		.of_match_table = of_match_ptr(gc4018a_of_match),
	},
	.probe		= gc4018a_probe,
	.remove		= gc4018a_remove,
	.id_table	= gc4018a_id,
};

module_i2c_driver(gc4018a_driver);
MODULE_AUTHOR("qpz <aric.pzqi@ingenic.com>");
MODULE_DESCRIPTION("A low-level driver for SmartSens gc4018a sensors");
MODULE_LICENSE("GPL");
