/*
 * A V4L2 driver for OmniVision IMX586 cameras.
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

#define IMX586_CHIP_ID_H	(0x40)
#define IMX586_CHIP_ID_L	(0x80)
#define IMX586_REG_END		0xffff
#define IMX586_REG_DELAY	0xfffe
#define MCLK			24000000

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct imx586_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;

};

struct imx586_gpio {
	int pin;
	int active_level;
};

struct imx586_supply {
	struct regulator *vdd2v8;	/* Digital Core supply */
	struct regulator *vdd1v8;	/* Digital I/O supply */
};

struct imx586_info {
	struct imx586_supply supply;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;

	struct v4l2_clk *clk;
	struct clk *sclka;

	struct v4l2_ctrl *exposure;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct imx586_win_size *win;

	struct imx586_gpio reset;
	struct imx586_gpio ircutp;
	struct imx586_gpio ircutn;
	struct imx586_gpio pwr;
};


/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;	/*sensor regs value*/
	unsigned int gain;	/*isp gain*/
};
struct again_lut imx586_again_lut[] = {
	{0x70,10968},
	{0x71,11052},
	{0x72,11136},
	{0x73,11304},
	{0x74,11388},
	{0x75,11472},
	{0x76,11555},
	{0x77,11639},
	{0x78,11806},
	{0x79,11890},
	{0x7a,11973},
	{0x7b,12139},
	{0x7c,12222},
	{0x7d,12306},
	{0x7e,12389},
	{0x7f,12554},
	{0x80,12637},
	{0x81,12720},
	{0x82,12802},
	{0x83,12967},
	{0x84,13050},
	{0x85,13132},
	{0x86,13296},
	{0x87,13379},
	{0x88,13461},
	{0x89,13543},
	{0x8a,13706},
	{0x8b,13788},
	{0x8c,13870},
	{0x8d,14033},
	{0x8e,14114},
	{0x8f,14196},
	{0x90,14358},
	{0x91,14440},
	{0x92,14521},
	{0x93,14683},
	{0x94,14764},
	{0x95,14844},
	{0x96,15006},
	{0x97,15087},
	{0x98,15167},
	{0x99,15328},
	{0x9a,15408},
	{0x9b,15489},
	{0x9c,15649},
	{0x9d,15729},
	{0x9e,15809},
	{0x9f,15969},
	{0xa0,16049},
	{0xa1,16208},
	{0xa2,16288},
	{0xa3,16368},
	{0xa4,16526},
	{0xa5,16606},
	{0xa6,16685},
	{0xa7,16843},
	{0xa8,16923},
	{0xa9,17081},
	{0xaa,17159},
	{0xab,17238},
	{0xac,17396},
	{0xad,17474},
	{0xae,17631},
	{0xaf,17710},
	{0xb0,17866},
	{0xb1,17945},
	{0xb2,18023},
	{0xb3,18179},
	{0xb4,18257},
	{0xb5,18413},
	{0xb6,18491},
	{0xb7,18646},
	{0xb8,18724},
	{0xb9,18879},
	{0xba,18956},
	{0xbb,19033},
	{0xbc,19188},
	{0xbd,19265},
	{0xbe,19419},
	{0xbf,19496},
	{0xc0,19650},
	{0xc1,19726},
	{0xc2,19880},
	{0xc3,19956},
	{0xc4,20109},
	{0xc5,20186},
	{0xc6,20338},
	{0xc7,20415},
	{0xc8,20567},
	{0xc9,20643},
	{0xca,20795},
	{0xcb,20871},
	{0xcc,21022},
	{0xcd,21098},
	{0xce,21249},
	{0xcf,21325},
	{0xd0,21475},
	{0xd1,21551},
	{0xd2,21701},
	{0xd3,21851},
	{0xd4,21926},
	{0xd5,22076},
	{0xd6,22151},
	{0xd7,22300},
	{0xd8,22375},
	{0xd9,22524},
	{0xda,22599},
	{0xdb,22747},
	{0xdc,22896},
	{0xdd,22970},
	{0xde,23118},
	{0xdf,23192},
	{0xe0,23340},
	{0xe1,23488},
	{0xe2,23562},
	{0xe3,23709},
	{0xe4,23782},
	{0xe5,23929},
	{0xe6,24076},
	{0xe7,24149},
	{0xe8,24296},
	{0xe9,24442},
	{0xea,24515},
	{0xeb,24661},
	{0xec,24733},
	{0xed,24879},
	{0xee,25024},
	{0xef,25097},
	{0xf0,25242},
	{0xf1,25386},
	{0xf2,25458},
	{0xf3,25603},
	{0xf4,25747},
	{0xf5,25891},
	{0xf6,25963},
	{0xf7,26106},
	{0xf8,26250},
	{0xf9,26321},
	{0xfa,26464},
	{0xfb,26607},
	{0xfc,26678},
	{0xfd,26821},
	{0xfe,26963},
	{0xff,27105},
	{0x100,27176},
	{0x101,27318},
	{0x102,27460},
	{0x103,27601},
	{0x104,27671},
	{0x105,27812},
	{0x106,27953},
	{0x107,28094},
	{0x108,28164},
	{0x109,28304},
	{0x10a,28444},
	{0x10b,28584},
	{0x10c,28654},
	{0x10d,28794},
	{0x10e,28933},
	{0x10f,29072},
	{0x110,29211},
	{0x111,29350},
	{0x112,29419},
	{0x113,29558},
	{0x114,29696},
	{0x115,29834},
	{0x116,29972},
	{0x117,30041},
	{0x118,30178},
	{0x119,30315},
	{0x11a,30452},
	{0x11b,30589},
	{0x11c,30726},
	{0x11d,30863},
	{0x11e,30999},
	{0x11f,31067},
	{0x120,31203},
	{0x121,31339},
	{0x122,31475},
	{0x123,31610},
	{0x124,31745},
	{0x125,31880},
	{0x126,32015},
	{0x127,32150},
	{0x128,32284},
	{0x129,32419},
	{0x12a,32486},
	{0x12b,32620},
	{0x12c,32754},
	{0x12d,32887},
	{0x12e,33021},
	{0x12f,33154},
	{0x130,33287},
	{0x131,33420},
	{0x132,33553},
	{0x133,33685},
	{0x134,33818},
	{0x135,33950},
	{0x136,34082},
	{0x137,34213},
	{0x138,34345},
	{0x139,34476},
	{0x13a,34608},
	{0x13b,34739},
	{0x13c,34870},
	{0x13d,35000},
	{0x13e,35131},
	{0x13f,35261},
	{0x140,35456},
	{0x141,35586},
	{0x142,35716},
	{0x143,35845},
	{0x144,35975},
	{0x145,36104},
	{0x146,36233},
	{0x147,36362},
	{0x148,36490},
	{0x149,36619},
	{0x14a,36811},
	{0x14b,36939},
	{0x14c,37067},
	{0x14d,37195},
	{0x14e,37322},
	{0x14f,37450},
	{0x150,37577},
	{0x151,37767},
	{0x152,37894},
	{0x153,38020},
	{0x154,38147},
	{0x155,38273},
	{0x156,38399},
	{0x157,38588},
	{0x158,38714},
	{0x159,38839},
	{0x15a,38964},
	{0x15b,39152},
	{0x15c,39277},
	{0x15d,39402},
	{0x15e,39526},
	{0x15f,39713},
	{0x160,39837},
	{0x161,39961},
	{0x162,40085},
	{0x163,40270},
	{0x164,40394},
	{0x165,40517},
	{0x166,40701},
	{0x167,40824},
	{0x168,40947},
	{0x169,41070},
	{0x16a,41253},
	{0x16b,41375},
	{0x16c,41558},
	{0x16d,41680},
	{0x16e,41802},
	{0x16f,41984},
	{0x170,42105},
	{0x171,42226},
	{0x172,42407},
	{0x173,42528},
	{0x174,42709},
	{0x175,42829},
	{0x176,42949},
	{0x177,43129},
	{0x178,43249},
	{0x179,43428},
	{0x17a,43548},
	{0x17b,43726},
	{0x17c,43845},
	{0x17d,44024},
	{0x17e,44142},
	{0x17f,44320},
	{0x180,44438},
	{0x181,44615},
	{0x182,44733},
	{0x183,44910},
	{0x184,45027},
	{0x185,45203},
	{0x186,45320},
	{0x187,45496},
	{0x188,45613},
	{0x189,45788},
	{0x18a,45904},
	{0x18b,46078},
	{0x18c,46252},
	{0x18d,46368},
	{0x18e,46542},
	{0x18f,46657},
	{0x190,46830},
	{0x191,47003},
	{0x192,47118},
	{0x193,47290},
	{0x194,47462},
	{0x195,47576},
	{0x196,47748},
	{0x197,47919},
	{0x198,48033},
	{0x199,48203},
	{0x19a,48373},
	{0x19b,48487},
	{0x19c,48656},
	{0x19d,48826},
	{0x19e,48995},
	{0x19f,49107},
	{0x1a0,49276},
	{0x1a1,49444},
	{0x1a2,49612},
	{0x1a3,49780},
	{0x1a4,49892},
	{0x1a5,50059},
	{0x1a6,50226},
	{0x1a7,50392},
	{0x1a8,50559},
	{0x1a9,50725},
	{0x1aa,50835},
	{0x1ab,51001},
	{0x1ac,51166},
	{0x1ad,51331},
	{0x1ae,51496},
	{0x1af,51660},
	{0x1b0,51824},
	{0x1b1,51988},
	{0x1b2,52151},
	{0x1b3,52315},
	{0x1b4,52423},
	{0x1b5,52586},
	{0x1b6,52749},
	{0x1b7,52911},
	{0x1b8,53073},
	{0x1b9,53234},
	{0x1ba,53396},
	{0x1bb,53557},
	{0x1bc,53771},
	{0x1bd,53932},
	{0x1be,54092},
	{0x1bf,54252},
	{0x1c0,54412},
	{0x1c1,54571},
	{0x1c2,54730},
	{0x1c3,54889},
	{0x1c4,55048},
	{0x1c5,55206},
	{0x1c6,55364},
	{0x1c7,55574},
	{0x1c8,55732},
	{0x1c9,55889},
	{0x1ca,56046},
	{0x1cb,56203},
	{0x1cc,56411},
	{0x1cd,56567},
	{0x1ce,56723},
	{0x1cf,56878},
	{0x1d0,57085},
	{0x1d1,57240},
	{0x1d2,57395},
	{0x1d3,57550},
	{0x1d4,57755},
	{0x1d5,57909},
	{0x1d6,58063},
	{0x1d7,58267},
	{0x1d8,58420},
	{0x1d9,58573},
	{0x1da,58776},
	{0x1db,58928},
	{0x1dc,59131},
	{0x1dd,59283},
	{0x1de,59434},
	{0x1df,59635},
	{0x1e0,59786},
	{0x1e1,59987},
	{0x1e2,60137},
	{0x1e3,60337},
	{0x1e4,60487},
	{0x1e5,60686},
	{0x1e6,60835},
	{0x1e7,61034},
	{0x1e8,61183},
	{0x1e9,61380},
	{0x1ea,61578},
	{0x1eb,61726},
	{0x1ec,61922},
	{0x1ed,62069},
	{0x1ee,62265},
	{0x1ef,62461},
	{0x1f0,62607},
	{0x1f1,62802},
	{0x1f2,62997},
	{0x1f3,63142},
	{0x1f4,63336},
	{0x1f5,63529},
	{0x1f6,63722},
	{0x1f7,63867},
	{0x1f8,64059},
	{0x1f9,64251},
	{0x1fa,64442},
	{0x1fb,64633},
	{0x1fc,64777},
	{0x1fd,64967},
	{0x1fe,65157},
	{0x1ff,65347},
	{0x200,65536},
	{0x201,65725},
	{0x202,65913},
	{0x203,66102},
	{0x204,66289},
	{0x205,66477},
	{0x206,66664},
	{0x207,66850},
        {0x208,67037},
        {0x209,67223},
        {0x20a,67408},
        {0x20b,67594},
        {0x20c,67778},
        {0x20d,67963},
        {0x20e,68147},
        {0x20f,68331},
	{0x210,68560},
	{0x211,68743},
	{0x212,68926},
	{0x213,69108},
	{0x214,69290},
	{0x215,69517},
	{0x216,69698},
	{0x217,69879},
	{0x218,70059},
	{0x219,70284},
	{0x21a,70464},
	{0x21b,70643},
	{0x21c,70867},
	{0x21d,71045},
	{0x21e,71223},
	{0x21f,71446},
	{0x220,71623},
	{0x221,71845},
	{0x222,72021},
	{0x223,72242},
	{0x224,72418},
	{0x225,72637},
	{0x226,72813},
	{0x227,73031},
	{0x228,73206},
	{0x229,73423},
	{0x22a,73641},
	{0x22b,73814},
	{0x22c,74030},
	{0x22d,74246},
	{0x22e,74418},
	{0x22f,74633},
	{0x230,74848},
	{0x231,75062},
	{0x232,75233},
	{0x233,75446},
	{0x234,75658},
	{0x235,75870},
	{0x236,76082},
	{0x237,76293},
	{0x238,76504},
	{0x239,76714},
	{0x23a,76924},
	{0x23b,77091},
	{0x23c,77300},
	{0x23d,77551},
	{0x23e,77759},
	{0x23f,77966},
	{0x240,78173},
	{0x241,78380},
	{0x242,78586},
	{0x243,78791},
	{0x244,78997},
	{0x245,79242},
	{0x246,79447},
	{0x247,79650},
	{0x248,79854},
	{0x249,80097},
	{0x24a,80300},
	{0x24b,80502},
	{0x24c,80743},
	{0x24d,80944},
	{0x24e,81145},
	{0x24f,81385},
	{0x250,81585},
	{0x251,81824},
	{0x252,82023},
	{0x253,82261},
	{0x254,82498},
	{0x255,82695},
	{0x256,82932},
	{0x257,83128},
	{0x258,83363},
	{0x259,83598},
	{0x25a,83832},
	{0x25b,84027},
	{0x25c,84260},
	{0x25d,84492},
	{0x25e,84724},
	{0x25f,84955},
	{0x260,85186},
	{0x261,85378},
	{0x262,85607},
	{0x263,85836},
	{0x264,86065},
	{0x265,86293},
	{0x266,86558},
	{0x267,86785},
	{0x268,87011},
	{0x269,87237},
	{0x26a,87462},
	{0x26b,87687},
	{0x26c,87948},
	{0x26d,88172},
	{0x26e,88395},
	{0x26f,88654},
	{0x270,88876},
	{0x271,89098},
	{0x272,89355},
	{0x273,89575},
	{0x274,89832},
	{0x275,90051},
	{0x276,90306},
	{0x277,90560},
	{0x278,90778},
	{0x279,91031},
	{0x27a,91283},
	{0x27b,91499},
	{0x27c,91750},
	{0x27d,92000},
	{0x27e,92250},
	{0x27f,92499},
	{0x280,92748},
	{0x281,92996},
	{0x282,93243},
	{0x283,93489},
	{0x284,93735},
	{0x285,93980},
	{0x286,94225},
	{0x287,94469},
	{0x288,94712},
	{0x289,94990},
	{0x28a,95232},
	{0x28b,95473},
	{0x28c,95748},
	{0x28d,95988},
	{0x28e,96262},
	{0x28f,96501},
	{0x290,96773},
	{0x291,97011},
	{0x292,97281},
	{0x293,97518},
	{0x294,97787},
	{0x295,98055},
	{0x296,98323},
	{0x297,98590},
	{0x298,98823},
	{0x299,99089},
	{0x29a,99354},
	{0x29b,99618},
	{0x29c,99881},
	{0x29d,100176},
	{0x29e,100438},
	{0x29f,100699},
	{0x2a0,100960},
	{0x2a1,101219},
	{0x2a2,101511},
	{0x2a3,101769},
	{0x2a4,102058},
	{0x2a5,102315},
	{0x2a6,102603},
	{0x2a7,102858},
	{0x2a8,103144},
	{0x2a9,103398},
	{0x2aa,103683},
	{0x2ab,103967},
	{0x2ac,104250},
	{0x2ad,104532},
	{0x2ae,104813},
	{0x2af,105093},
	{0x2b0,105373},
	{0x2b1,105652},
	{0x2b2,105930},
	{0x2b3,106207},
	{0x2b4,106483},
	{0x2b5,106789},
	{0x2b6,107064},
	{0x2b7,107338},
	{0x2b8,107641},
	{0x2b9,107913},
	{0x2ba,108215},
	{0x2bb,108515},
	{0x2bc,108785},
	{0x2bd,109084},
	{0x2be,109381},
	{0x2bf,109678},
	{0x2c0,109974},
	{0x2c1,110269},
	{0x2c2,110563},
	{0x2c3,110856},
	{0x2c4,111178},
	{0x2c5,111469},
	{0x2c6,111759},
	{0x2c7,112078},
	{0x2c8,112366},
	{0x2c9,112683},
	{0x2ca,112969},
	{0x2cb,113284},
	{0x2cc,113597},
	{0x2cd,113909},
	{0x2ce,114192},
	{0x2cf,114503},
	{0x2d0,114812},
	{0x2d1,115148},
	{0x2d2,115455},
	{0x2d3,115762},
	{0x2d4,116067},
	{0x2d5,116399},
	{0x2d6,116702},
	{0x2d7,117032},
	{0x2d8,117333},
	{0x2d9,117660},
	{0x2da,117986},
	{0x2db,118312},
	{0x2dc,118636},
	{0x2dd,118959},
	{0x2de,119281},
	{0x2df,119601},
	{0x2e0,119948},
	{0x2e1,120266},
	{0x2e2,120584},
	{0x2e3,120926},
	{0x2e4,121268},
	{0x2e5,121582},
	{0x2e6,121921},
	{0x2e7,122259},
	{0x2e8,122596},
	{0x2e9,122931},
	{0x2ea,123265},
	{0x2eb,123624},
	{0x2ec,123956},
	{0x2ed,124312},
	{0x2ee,124642},
	{0x2ef,124995},
	{0x2f0,125347},
	{0x2f1,125698},
	{0x2f2,126048},
	{0x2f3,126396},
	{0x2f4,126743},
	{0x2f5,127089},
	{0x2f6,127458},
	{0x2f7,127801},
	{0x2f8,128168},
	{0x2f9,128533},
	{0x2fa,128872},
	{0x2fb,129234},
	{0x2fc,129595},
	{0x2fd,129978},
	{0x2fe,130336},
	{0x2ff,130693},
	{0x300,131072},
	{0x301,131449},
	{0x302,131802},
	{0x303,132176},
	{0x304,132550},
	{0x305,132944},
	{0x306,133314},
	{0x307,133683},
	{0x308,134073},
	{0x309,134462},
	{0x30a,134848},
	{0x30b,135234},
	{0x30c,135617},
	{0x30d,136000},
	{0x30e,136380},
	{0x30f,136782},
	{0x310,137181},
	{0x311,137579},
	{0x312,137976},
	{0x313,138370},
	{0x314,138763},
	{0x315,139155},
	{0x316,139566},
	{0x317,139976},
	{0x318,140384},
	{0x319,140790},
	{0x31a,141194},
	{0x31b,141618},
	{0x31c,142019},
	{0x31d,142439},
	{0x31e,142857},
	{0x31f,143274},
	{0x320,143688},
	{0x321,144122},
	{0x322,144553},
	{0x323,144962},
	{0x324,145410},
	{0x325,145836},
	{0x326,146259},
	{0x327,146701},
	{0x328,147141},
	{0x329,147579},
	{0x32a,148014},
	{0x32b,148468},
	{0x32c,148899},
	{0x32d,149348},
	{0x32e,149796},
	{0x32f,150260},
	{0x330,150703},
	{0x331,151162},
	{0x332,151620},
	{0x333,152075},
	{0x334,152547},
	{0x335,152998},
	{0x336,153466},
	{0x337,153949},
	{0x338,154412},
	{0x339,154891},
	{0x33a,155368},
	{0x33b,155842},
	{0x33c,156314},
	{0x33d,156801},
	{0x33e,157286},
	{0x33f,157786},
	{0x340,158266},
	{0x341,158761},
	{0x342,159254},
	{0x343,159761},
	{0x344,160266},
	{0x345,160768},
	{0x346,161267},
	{0x347,161781},
	{0x348,162292},
	{0x349,162817},
	{0x34a,163323},
	{0x34b,163842},
	{0x34c,164376},
	{0x34d,164906},
	{0x34e,165433},
	{0x34f,165958},
	{0x350,166496},
	{0x351,167031},
	{0x352,167578},
	{0x353,168123},
	{0x354,168665},
	{0x355,169219},
	{0x356,169786},
	{0x357,170333},
	{0x358,170893},
	{0x359,171466},
	{0x35a,172034},
	{0x35b,172600},
	{0x35c,173177},
	{0x35d,173751},
	{0x35e,174336},
	{0x35f,174917},
	{0x360,175510},
	{0x361,176099},
	{0x362,176699},
	{0x363,177295},
	{0x364,177902},
	{0x365,178505},
	{0x366,179119},
	{0x367,179743},
	{0x368,180362},
	{0x369,180978},
	{0x36a,181617},
	{0x36b,182238},
	{0x36c,182882},
	{0x36d,183522},
	{0x36e,184172},
	{0x36f,184817},
	{0x370,185470},
	{0x371,186133},
	{0x372,186791},
	{0x373,187457},
	{0x374,188132},
	{0x375,188814},
	{0x376,189492},
	{0x377,190178},
	{0x378,190871},
	{0x379,191572},
	{0x37a,192279},
	{0x37b,192982},
	{0x37c,193704},
	{0x37d,194420},
	{0x37e,195143},
	{0x37f,195872},
	{0x380,196608},
	{0x381,197350},
	{0x382,198097},
	{0x383,198850},
	{0x384,199609},
	{0x385,200373},
	{0x386,201142},
	{0x387,201928},
	{0x388,202706},
	{0x389,203501},
	{0x38a,204299},
	{0x38b,205102},
	{0x38c,205920},
	{0x38d,206730},
	{0x38e,207555},
	{0x38f,208393},
	{0x390,209235},
	{0x391,210079},
	{0x392,210936},
	{0x393,211795},
	{0x394,212667},
	{0x395,213550},
	{0x396,214435},
	{0x397,215332},
	{0x398,216239},
	{0x399,217156},
	{0x39a,218074},
	{0x39b,219011},
	{0x39c,219948},
	{0x39d,220895},
	{0x39e,221859},
	{0x39f,222831},
	{0x3a0,223811},
	{0x3a1,224798},
	{0x3a2,225802},
	{0x3a3,226812},
	{0x3a4,227828},
	{0x3a5,228867},
	{0x3a6,229912},
	{0x3a7,230969},
	{0x3a8,232032},
	{0x3a9,233114},
	{0x3aa,234208},
	{0x3ab,235314},
	{0x3ac,236429},
	{0x3ad,237563},
	{0x3ae,238713},
	{0x3af,239872},
	{0x3b0,241046},
	{0x3b1,242235},
	{0x3b2,243438},
	{0x3b3,244662},
	{0x3b4,245898},
	{0x3b5,247146},
	{0x3b6,248418},
	{0x3b7,249701},
	{0x3b8,251006},
	{0x3b9,252333},
	{0x3ba,253674},
	{0x3bb,255034},
	{0x3bc,256413},
	{0x3bd,257815},
	{0x3be,259234},
	{0x3bf,260679},
	{0x3c0,262144},
	{0x3c1,263633},
	{0x3c2,265145},
	{0x3c3,266684},
	{0x3c4,268248},
	{0x3c5,269835},
	{0x3c6,271450},
	{0x3c7,273096},
	{0x3c8,274771},
	{0x3c9,276472},
	{0x3ca,278208},
	{0x3cb,279976},
	{0x3cc,281775},
	{0x3cd,283610},
	{0x3ce,285484},
	{0x3cf,287395},
	{0x3d0,289342},
	{0x3d1,291333},
	{0x3d2,293368},
	{0x3d3,295448},
	{0x3d4,297572},
	{0x3d5,299744},
	{0x3d6,301969},
	{0x3d7,304249},
	{0x3d8,306582},
	{0x3d9,308974},
	{0x3da,311431},
	{0x3db,313954},
	{0x3dc,316542},
	{0x3dd,319207},
	{0x3de,321949},
	{0x3df,324770},
	{0x3e0,327680},
	{0x3e1,330681},
	{0x3e2,333781},
	{0x3e3,336986},
	{0x3e4,340304},
	{0x3e5,343744},
	{0x3e6,347313},
	{0x3e7,351020},
	{0x3e8,354881},
	{0x3e9,358904},
	{0x3ea,363106},
	{0x3eb,367505},
	{0x3ec,372118},
	{0x3ed,376968},
	{0x3ee,382080},
	{0x3ef,387484},
	{0x3f0,393216},
};

static inline struct imx586_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx586_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct imx586_info, hdl)->sd;
}
struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};


static struct regval_list imx586_init_regs_3840_2160_mipi[] = {
	{0x0136,0x18},
 	{0x0137,0x00},
 	{0x3C7E,0x01},
 	{0x3C7F,0x08},
 	{0x0111,0x02},
 	{0x380C,0x00},
 	{0x3C00,0x10},
	{0x3C01,0x10},
 	{0x3C02,0x10},
 	{0x3C03,0x10},
 	{0x3C04,0x10},
 	{0x3C05,0x01},
 	{0x3C06,0x00},
 	{0x3C07,0x00},
 	{0x3C08,0x03},
 	{0x3C09,0xFF},
 	{0x3C0A,0x01},
 	{0x3C0B,0x00},
 	{0x3C0C,0x00},
 	{0x3C0D,0x03},
 	{0x3C0E,0xFF},
 	{0x3C0F,0x20},
 	{0x3F88,0x00},
 	{0x3F8E,0x00},
 	{0x5282,0x01},
 	{0x9004,0x14},
 	{0x9200,0xF4},
 	{0x9201,0xA7},
 	{0x9202,0xF4},
 	{0x9203,0xAA},
 	{0x9204,0xF4},
 	{0x9205,0xAD},
 	{0x9206,0xF4},
 	{0x9207,0xB0},
 	{0x9208,0xF4},
 	{0x9209,0xB3},
 	{0x920A,0xB7},
 	{0x920B,0x34},
 	{0x920C,0xB7},
 	{0x920D,0x36},
 	{0x920E,0xB7},
 	{0x920F,0x37},
 	{0x9210,0xB7},
 	{0x9211,0x38},
 	{0x9212,0xB7},
 	{0x9213,0x39},
 	{0x9214,0xB7},
 	{0x9215,0x3A},
 	{0x9216,0xB7},
 	{0x9217,0x3C},
 	{0x9218,0xB7},
 	{0x9219,0x3D},
 	{0x921A,0xB7},
 	{0x921B,0x3E},
 	{0x921C,0xB7},
 	{0x921D,0x3F},
 	{0x921E,0x77},
 	{0x921F,0x77},
 	{0x9222,0xC4},
 	{0x9223,0x4B},
 	{0x9224,0xC4},
 	{0x9225,0x4C},
 	{0x9226,0xC4},
 	{0x9227,0x4D},
 	{0x9810,0x14},
 	{0x9814,0x14},
 	{0x99B2,0x20},
 	{0x99B3,0x0F},
 	{0x99B4,0x0F},
 	{0x99B5,0x0F},
 	{0x99B6,0x0F},
 	{0x99E4,0x0F},
 	{0x99E5,0x0F},
 	{0x99E6,0x0F},
 	{0x99E7,0x0F},
 	{0x99E8,0x0F},
 	{0x99E9,0x0F},
 	{0x99EA,0x0F},
 	{0x99EB,0x0F},
 	{0x99EC,0x0F},
 	{0x99ED,0x0F},
 	{0xA569,0x06},
 	{0xA679,0x20},
 	{0xC020,0x01},
 	{0xC61D,0x00},
 	{0xC625,0x00},
 	{0xC638,0x03},
 	{0xC63B,0x01},
 	{0xE286,0x31},
 	{0xE2A6,0x32},
 	{0xE2C6,0x33},
 	{0xBCF1,0x00},
 	{0x0112,0x0A},
 	{0x0113,0x0A},
 	{0x0114,0x03},
 	{0x0342,0x23},
 	{0x0343,0x10},
 	{0x0340,0x0B},
 	{0x0341,0xF4},
 	{0x0344,0x00},
 	{0x0345,0x00},
 	{0x0346,0x00},
 	{0x0347,0x00},
 	{0x0348,0x1F},
 	{0x0349,0x3F},
 	{0x034A,0x17},
 	{0x034B,0x6F},
 	{0x0220,0x62},
 	{0x0222,0x01},
 	{0x0900,0x01},
 	{0x0901,0x22},
 	{0x0902,0x08},
 	{0x3140,0x00},
 	{0x3246,0x81},
 	{0x3247,0x81},
 	{0x3F15,0x00},
 	{0x0401,0x00},
 	{0x0404,0x00},
 	{0x0405,0x10},
 	{0x0408,0x00},
 	{0x0409,0x00},
 	{0x040A,0x00},
 	{0x040B,0x00},
 	{0x040C,0x0F},
 	{0x040D,0xA0},
 	{0x040E,0x0B},
 	{0x040F,0xB8},

 	/*
 	{0x034C,0x0F},
 	{0x034D,0xA0},// width = 4000
 	{0x034E,0x0B},
 	{0x034F,0xB8},// height = 3000
 	*/

	/*
 	{0x034C,0x07},
 	{0x034D,0xD0},// width = 2000
 	{0x034E,0x05},
 	{0x034F,0xDC},// height = 1500
	*/

 	{0x034C,0x0f},
 	{0x034D,0x00},// width = 3840
 	{0x034E,0x08},
 	{0x034F,0x70},// height = 2160

 	{0x0301,0x05},
 	{0x0303,0x04},
 	{0x0305,0x04},
 	{0x0306,0x00},
 	{0x0307,0xEF},
 	{0x030B,0x02},
 	{0x030D,0x06},
 	{0x030E,0x01},
 	{0x030F,0x90},
 	{0x0310,0x01},
 	{0x3620,0x00},
 	{0x3621,0x00},
 	{0x3C11,0x04},
 	{0x3C12,0x03},
 	{0x3C13,0x2D},
 	{0x3F0C,0x01},
 	{0x3F14,0x00},
 	{0x3F80,0x00},
 	{0x3F81,0x14},
 	{0x3F8C,0x00},
 	{0x3F8D,0x14},
 	{0x3FF8,0x00},
 	{0x3FF9,0x3C},
 	{0x3FFE,0x01},
 	{0x3FFF,0x8C},
 	{0x0202,0x0B},
 	{0x0203,0xC4},
 	{0x0224,0x01},
 	{0x0225,0xF4},
 	{0x3FE0,0x01},
 	{0x3FE1,0xF4},
 	{0x0204,0x00},
 	{0x0205,0x70},
 	{0x0216,0x00},
 	{0x0217,0x70},
 	{0x0218,0x01},
 	{0x0219,0x00},
 	{0x020E,0x01},
 	{0x020F,0x00},
 	{0x0210,0x01},
 	{0x0211,0x00},
 	{0x0212,0x01},
 	{0x0213,0x00},
 	{0x0214,0x01},
 	{0x0215,0x00},
 	{0x3FE2,0x00},
 	{0x3FE3,0x70},
 	{0x3FE4,0x01},
 	{0x3FE5,0x00},
 	{0x3E20,0x02},
 	{0x3E3B,0x00},
 	{0x4434,0x01},
 	{0x4435,0xF0},
 	{0x0100,0x01},

	{IMX586_REG_END, 0x00},	/* END MARKER */
};
#if 0
static struct regval_list imx586_init_regs_320_240_mipi[] = {
	{0x3501, 0x00},
	{0x3502, 0x90},
	{0x3674, 0xef},
	{0x373c, 0xe8},
	{0x37a8, 0x02},
	{0x37a9, 0x14},
	{0x3803, 0x00},
	{0x3807, 0xef},
	{0x3808, 0x01},
	{0x3809, 0x40},
	{0x380a, 0x00},
	{0x380b, 0xf0},
	{0x380c, 0x05},
	{0x380d, 0x70},
	{0x380e, 0x02},
	{0x380f, 0x40},
	{0x3813, 0x05},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3820, 0x42},
	{0x3821, 0x01},
	{0x3c0e, 0x01},
	{0x3c0f, 0x30},
	{0x4001, 0x40},
	{0x4004, 0x02},
	{0x0100, 0x01},
	{IMX586_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list imx586_init_regs_160_120_mipi[] = {
	{0x3501, 0x02}, //
	{0x3502, 0x80}, //expo
//	{0x3509, 0x18},
//	{0x3504, 0x03},
//	{0x3505, 0x00}, //mannual again
	{0x3674, 0xff}, //
	{0x373c, 0xe8}, //
	{0x37a8, 0x02}, //
	{0x37a9, 0x14}, //
	{0x3803, 0x00}, //
	{0x3807, 0xef}, //
	{0x3808, 0x00}, //01
	{0x3809, 0xa0}, //40
	{0x380a, 0x00}, //
	{0x380b, 0x78}, //f0
	{0x380c, 0x03}, //
	{0x380d, 0xa0}, //HTS
	{0x380e, 0x02}, //
	{0x380f, 0x0a}, //VTS
	{0x3811, 0x02},
	{0x3813, 0x03}, //05
	{0x3814, 0x44}, //31
	{0x3815, 0x44}, //31
	{0x3820, 0x42}, //
	{0x3821, 0x01}, //
	{0x3c0c, 0x01}, //
	{0x3c0d, 0x82}, //
	{0x3c0e, 0x01}, //
	{0x3c0f, 0x30}, //
	{0x4001, 0x40}, //
	{0x4004, 0x02}, //
	{0x0100, 0x01}, //
	{IMX586_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list imx586_common_init_regs_mipi[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x3005, 0x00},
	{0x3012, 0xc0},
	{0x3013, 0xd2},
	{0x3014, 0x04},
	{0x3016, 0x10},
	{0x3017, 0x00},
	{0x3018, 0x00},
	{0x301a, 0x00},
	{0x301b, 0x00},
	{0x301c, 0x00},
	{0x3023, 0x05},
	{0x3037, 0xf0},
	{0x3098, 0x04},
	{0x3099, 0x28},
	{0x309a, 0x05},
	{0x309b, 0x04},
	{0x30b0, 0x0a},
	{0x30b1, 0x01},
	{0x30b3, 0x64},
	{0x30b4, 0x03},
	{0x30b5, 0x05},
	{0x3106, 0xda},
	{0x3500, 0x00},
	{0x3503, 0x07},
	{0x3509, 0x10},
	{0x350b, 0x10},
	{0x3600, 0x1c},
	{0x3602, 0x62},
	{0x3620, 0xb7},
	{0x3622, 0x04},
	{0x3626, 0x21},
	{0x3627, 0x30},
	{0x3630, 0x44},
	{0x3631, 0x35},
	{0x3634, 0x60},
	{0x3636, 0x00},
	{0x3662, 0x01},
	{0x3663, 0x70},
	{0x3664, 0xf0},
	{0x3666, 0x0a},
	{0x3669, 0x1a},
	{0x366a, 0x00},
	{0x366b, 0x50},
	{0x3673, 0x01},
	{0x3675, 0x03},
	{0x3705, 0x41},
	{0x3709, 0x40},
	{0x3742, 0x00},
	{0x3757, 0xb3},
	{0x3788, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x04},
	{0x3802, 0x00},
	{0x3804, 0x02},
	{0x3805, 0x8b},
	{0x3806, 0x01},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x382f, 0x0e},
	{0x3832, 0x00},
	{0x3833, 0x05},
	{0x3834, 0x00},
	{0x3835, 0x0c},
	{0x3837, 0x00},
	{0x3b80, 0x00},
	{0x3b81, 0xa5},
	{0x3b82, 0x10},
	{0x3b83, 0x00},
	{0x3b84, 0x08},
	{0x3b85, 0x00},
	{0x3b86, 0x01},
	{0x3b87, 0x00},
	{0x3b88, 0x00},
	{0x3b89, 0x00},
	{0x3b8a, 0x00},
	{0x3b8b, 0x05},
	{0x3b8c, 0x00},
	{0x3b8d, 0x00},
	{0x3b8e, 0x00},
	{0x3b8f, 0x1a},
	{0x3b94, 0x05},
	{0x3b95, 0xf2},
	{0x3b96, 0x40},
	{0x3c00, 0x89},
	{0x3c01, 0x63},
	{0x3c02, 0x01},
	{0x3c03, 0x00},
	{0x3c04, 0x00},
	{0x3c05, 0x03},
	{0x3c06, 0x00},
	{0x3c07, 0x06},
	{0x4005, 0x00},
	{0x404e, 0x01},
	{0x4300, 0xff},
	{0x4301, 0x00},
	{0x4501, 0x48},
	{0x4600, 0x00},
	{0x4601, 0x4e},
	{0x4801, 0x0f},
	{0x4806, 0x0f},
	{0x4819, 0xaa},
	{0x4823, 0x3e},
	{0x4837, 0x19},
	{0x4a0d, 0x00},
	{0x4a47, 0x7f},
	{0x4a49, 0xf0},
	{0x4a4b, 0x30},
	{0x5000, 0x85},
	{0x5001, 0x80},
	{IMX586_REG_END, 0x00},	/* END MARKER */
};
#endif

/*
 * the part of driver was fixed.
 */

static struct regval_list imx586_stream_on_mipi[] = {
	{0x0100, 0x01},
	{IMX586_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list imx586_stream_off_mipi[] = {
	{0x0100, 0x00},
	{IMX586_REG_END, 0x00},	/* END MARKER */
};

static int imx586_read(struct v4l2_subdev *sd, unsigned short reg,
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

static int imx586_write(struct v4l2_subdev *sd, unsigned short reg,
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
/*
static int imx586_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != IMX586_REG_END) {
		if (vals->reg_num == IMX586_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = imx586_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}
*/
/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int imx586_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != IMX586_REG_END) {
		if (vals->reg_num == IMX586_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = imx586_write(sd, vals->reg_num, vals->value);
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
static int imx586_reset(struct v4l2_subdev *sd, u32 val)
{
	struct imx586_info *info = to_state(sd);
	printk("%s >>>>>>>>>>> %s >>>>\n",__func__,__func__);
	if(val) {
		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(20);
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
		msleep(20);

	}
	return 0;
}

/*
static int imx586_pwr_set(struct v4l2_subdev *sd, u32 val)
{
	struct imx586_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->pwr.pin, info->pwr.active_level);
	} else {
		gpio_direction_output(info->pwr.pin, !info->pwr.active_level);
	}
	return 0;
}

static int imx586_ircut(struct v4l2_subdev *sd, u32 val)
{
	struct imx586_info *info = to_state(sd);

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
*/
static int imx586_init(struct v4l2_subdev *sd, u32 val)
{
	int ret = 0;

	//ret = imx586_write_array(sd, imx586_common_init_regs_mipi);

	return ret;
}


/*
static int imx586_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v = 0;
	int ret;
//while(1)
	{
	ret = imx586_read(sd, 0x3033, &v);
	pr_info("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	}
	if (ret < 0)
		return ret;
	if (v != IMX586_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = imx586_read(sd, 0x380c, &v);
	pr_info("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);

	if (ret < 0)
		return ret;
	if (v != IMX586_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}
*/

static struct imx586_win_size imx586_win_sizes[] = {
	{
		.width				= 3840,
		.height				= 2160,
		.sensor_info.fps				= 30 << 16 | 1,
//		.sensor_info.total_width			= 2720,
//		.sensor_info.total_height			= 1976,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 824,
		.sensor_info.mipi_cfg.twidth		= 3840,
		.sensor_info.mipi_cfg.theight		= 2160,
		.sensor_info.mipi_cfg.mipi_mode         = SENSOR_MIPI_SONY_MODE,
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
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= imx586_init_regs_3840_2160_mipi,
	},
/*
	{
		.width				= 320,
		.height				= 240,
		.sensor_info.fps				= 60 << 16 | 1,
//		.sensor_info.total_width			= 656,
//		.sensor_info.total_height			= 496,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 600,
		.sensor_info.mipi_cfg.twidth		= 320,
		.sensor_info.mipi_cfg.theight		= 240,
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
		.regs 		= imx586_init_regs_320_240_mipi,
	},
	{
		.width				= 160,
		.height				= 120,
		.sensor_info.fps				= 100 << 16 | 1,
	//	.sensor_info.total_width			= 656,
	//	.sensor_info.total_height			= 496,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 600,
		.sensor_info.mipi_cfg.twidth		= 160,
		.sensor_info.mipi_cfg.theight		= 120,
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
		.regs 		= imx586_init_regs_160_120_mipi,
	},
*/
};

static struct imx586_win_size *imx586_select_win(u32 *width, u32 *height)
{
//	int i=0;
	int  default_size = ARRAY_SIZE(imx586_win_sizes) - 1;
/*
	for (i = 0; i < ARRAY_SIZE(imx586_win_sizes); i++) {
		if ((*width >= imx586_win_sizes[i].width) &&
				(*height >= imx586_win_sizes[i].height)) {
			*width = imx586_win_sizes[i].width;
			*height = imx586_win_sizes[i].height;
			return &imx586_win_sizes[i];
		}
	}
*/
	*width = imx586_win_sizes[default_size].width;
	*height = imx586_win_sizes[default_size].height;
	return &imx586_win_sizes[default_size];
}


#if 0
static int imx586_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= N_IMX586_FMTS)
		return -EINVAL;

	code->code = imx586_formats[code->index].mbus_code;
	return 0;
}
#endif

/*
 * Set a format.
 */
static int imx586_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct imx586_info *info = to_state(sd);
	info->win = imx586_select_win(&format->format.width, &format->format.height);
	return 0;
}

static int imx586_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format)
{
	struct imx586_info *info = to_state(sd);
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
	*(unsigned int *)fmt->reserved = (unsigned int)&info->win->sensor_info;


//	printk("----%s, %d, width: %d, height: %d, code: %x\n",
//			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int imx586_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx586_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx586_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx586_s_vflip(struct v4l2_subdev *sd, int value)
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
static int imx586_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int imx586_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

//	printk("---%s, %d, s_gain: value: %d\n", __func__, __LINE__, value);

	return ret;
}

#if 1 
static unsigned int again_to_regval(int gain)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(imx586_again_lut); i++) {
		lut = &imx586_again_lut[i];

		if(gain <= lut->gain) {
			return lut->value;
		}
	}

	/*last value.*/
	return lut->value;
}

static int regval_to_again(unsigned int regval)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(imx586_again_lut); i++) {
		lut = &imx586_again_lut[i];

		if(regval == lut->value) {
			return lut->gain;
		}
	}
	printk("%s, %d regval not mapped to isp gain\n", __func__, __LINE__);
}
#endif
static int imx586_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
#if 1
	struct imx586_info *info = to_state(sd);
	char v = 0;
	unsigned int reg_val = 0;


	ret += imx586_read(sd, 0x0205, &v);

	reg_val |= v;
	ret += imx586_read(sd, 0x0204, &v);
	reg_val |= v << 8;


	*value = regval_to_again(reg_val);
#endif
	return ret;
}
/*set analog gain db value, map value to sensor register.*/
static int imx586_s_again(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
#if 1 
	struct imx586_info *info = to_state(sd);
	unsigned int reg_value = 0;
	int i;

	if(value < info->again->minimum || value > info->again->maximum) {
		/* use default value. */
		reg_value = again_to_regval(info->again->default_value);
	} else {
		reg_value = again_to_regval(value);
	}

	ret += imx586_write(sd, 0x0205, (unsigned char)(reg_value & 0xff));
	ret += imx586_write(sd, 0x0204, (unsigned char)((reg_value>>8) & 0xff));
	if (ret < 0)
		return ret;

#endif
	return ret;
}

/*
 * Tweak autogain.
 */
static int imx586_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int imx586_s_exp(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	struct imx586_info *info = to_state(sd);

	ret += imx586_write(sd, 0x0202, ((unsigned char)(value & 0xff)) << 8);
	ret += imx586_write(sd, 0x0203, (unsigned char)((value) & 0xff));
//	ret += imx586_write(sd, 0x0104, (unsigned char)(0x1));

	return ret;
}

static int imx586_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct imx586_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return imx586_g_gain(sd, &info->gain->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return imx586_g_again(sd, &info->again->val);
	}
	return -EINVAL;
}

static int imx586_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct imx586_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return imx586_s_brightness(sd, ctrl->val);
	case V4L2_CID_CONTRAST:
		return imx586_s_contrast(sd, ctrl->val);
	case V4L2_CID_VFLIP:
		return imx586_s_vflip(sd, ctrl->val);
	case V4L2_CID_HFLIP:
		return imx586_s_hflip(sd, ctrl->val);
	case V4L2_CID_AUTOGAIN:
		/* Only set manual gain if auto gain is not explicitly
		   turned on. */
		if (!ctrl->val) {
			/* imx586_s_gain turns off auto gain */
			return imx586_s_gain(sd, info->gain->val);
		}
		return imx586_s_autogain(sd, ctrl->val);
	case V4L2_CID_GAIN:
		return imx586_s_gain(sd, ctrl->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return imx586_s_again(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
		return imx586_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops imx586_ctrl_ops = {
	.s_ctrl = imx586_s_ctrl,
	.g_volatile_ctrl = imx586_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int imx586_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = imx586_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int imx586_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	imx586_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

int imx586_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx586_info *info = to_state(sd);
	int ret = 0;

	if (enable) {
		imx586_write_array(sd, info->win->regs);
		imx586_s_again(sd, 100000);
		ret = imx586_write_array(sd, imx586_stream_on_mipi);
		printk("imx586 stream on\n");

	}
	else {
		ret = imx586_write_array(sd, imx586_stream_off_mipi);
		printk("imx586 stream off\n");
	}
	return ret;
}
/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops imx586_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = imx586_g_register,
	.s_register = imx586_s_register,
#endif
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,

};

static const struct v4l2_subdev_video_ops imx586_video_ops = {
	.s_stream = imx586_s_stream,
};

static const struct v4l2_subdev_pad_ops imx586_pad_ops = {
	//.enum_frame_interval = imx586_enum_frame_interval,
	//.num_frame_size = imx586_enum_frame_size,
	//.enum_mbus_code = imx586_enum_mbus_code,
	.set_fmt = imx586_set_fmt,
	.get_fmt = imx586_get_fmt,
};

static const struct v4l2_subdev_ops imx586_ops = {
	.core = &imx586_core_ops,
	.video = &imx586_video_ops,
	.pad = &imx586_pad_ops,
};

#if 0
int imx586_regulator_get_supply(struct device *dev)
{
	int ret;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586_info *info = to_state(sd);

	info->supply.vdd2v8 = devm_regulator_get_optional(dev, "vdd2v8");
	info->supply.vdd1v8 = devm_regulator_get_optional(dev, "vdd1v8");

	if (IS_ERR(info->supply.vdd2v8)) {
		if (PTR_ERR(info->supply.vdd2v8) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		printk("No vdd2v8 regulator found\n");
	}

	if (IS_ERR(info->supply.vdd1v8)) {
		if (PTR_ERR(info->supply.vdd1v8) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		printk("No vdd1v8 regulator found\n");
	}
	if (!IS_ERR(info->supply.vdd2v8)) {
		ret = regulator_enable(info->supply.vdd2v8);
		if (ret)
			dev_err(&client->dev, "imx586 vdd2v8 supply enable failed\n");
	}
	if (!IS_ERR(info->supply.vdd1v8)) {
		ret = regulator_enable(info->supply.vdd1v8);
		if (ret)
			dev_err(&client->dev, "imx586 vdd1v8 supply enable failed\n");
	}

	return 0;
}
#endif
/* ----------------------------------------------------------------------- */

static int imx586_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct imx586_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	int mclk_index;
	unsigned long rate;
	char id_div[9];
	char id_mux[9];
	dev_info(&client->dev, "imx586 probing\n");
	printk("********************************************\n");
	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->reset.pin = gpio;
		info->reset.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
/*
	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,ircutp-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->ircutp.pin = gpio;
		info->ircutp.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,ircutn-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->ircutn.pin = gpio;
		info->ircutn.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,pwr-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->pwr.pin = gpio;
		info->pwr.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
*/
	
	v4l2_i2c_subdev_init(sd, client, &imx586_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* ret = imx586_regulator_get_supply(&client->dev); */
	/* if (ret) */
	/* 	dev_err(&client->dev, "get supply failed!\n"); */

	//*(volatile unsigned int *)(0xb0010100) = 1;	// set gpio a work at 1.8v
//	imx586_pwr_set(sd,1);

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
	if (((rate / 1000) % 24000) != 0) {
		ret = clk_set_parent(info->sclka, clk_get(NULL, "epll"));
		info->sclka = devm_clk_get(&client->dev, "epll");
		if (IS_ERR(info->sclka)) {
			pr_err("get sclka failed\n");
		} else {
			rate = clk_get_rate(info->sclka);
			if (((rate / 1000) % 24000) != 0) {
				clk_set_rate(info->sclka, 120000000);
			}
		}
	}
//	msleep(100);
	ret = v4l2_clk_set_rate(info->clk, MCLK);
	if(ret)
		dev_err(sd->dev, "clk_set_rate err!\n");

	ret = v4l2_clk_enable(info->clk);
	if(ret)
		dev_err(sd->dev, "clk_enable err!\n");
	imx586_reset(sd, 1);
	gpio_direction_output(info->reset.pin, !info->reset.active_level);
#if 1
	/* Make sure it's an imx586 */
//	while(1){
//	ret = imx586_detect(sd, &ident);
//	}
	if (ret) {
		v4l_err(client,
			"chip found @ 0x%x (%s) is not an imx586 chip.\n",
			client->addr << 1, client->adapter->name);
		return ret;
	}
#endif

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &imx586_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &imx586_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &imx586_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &imx586_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &imx586_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &imx586_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 261773, 1, 10000);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &imx586_ctrl_ops,
			V4L2_CID_EXPOSURE, 4, 1520 - 4, 1, 1000);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;

		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	imx586_init(sd, 1);

	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&info->sd.entity, 1, &info->pad, 0);
	if(ret < 0) {
		goto err_entity_init;
	}
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "imx586 Probed\n");
	return 0;
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}


static int imx586_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id imx586_id[] = {
	{ "imx586", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx586_id);

static const struct of_device_id imx586_of_match[] = {
	{.compatible = "ovti,imx586", },
	{},
};
MODULE_DEVICE_TABLE(of, ov2640_of_match);

int imx586_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586_info *info = to_state(sd);
	/*int ret; */
	/* if (!IS_ERR(info->supply.vdd2v8)) { */
	/* 	ret = regulator_disable(info->supply.vdd2v8); */
	/* 	if (ret) { */
	/* 		dev_err(dev, "imx586 vdd2v8 supply disable failed\n"); */
	/* 		return ret; */
	/* 	} */
	/* } */
	/* if (!IS_ERR(info->supply.vdd1v8)) { */
	/* 	ret = regulator_disable(info->supply.vdd1v8); */
	/* 	if (ret) { */
	/* 		dev_err(dev, "imx586 vdd1v8 supply disable failed\n"); */
	/* 		return ret; */
	/* 	} */
	/* } */

	v4l2_clk_disable(info->clk);
	return 0;
}

int imx586_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586_info *info = to_state(sd);
	/*int ret; */
	/* if (!IS_ERR(info->supply.vdd2v8)) { */
	/* 	ret = regulator_enable(info->supply.vdd2v8); */
	/* 	if (ret) { */
	/* 		dev_err(dev, "imx586 vdd2v8 supply enable failed\n"); */
	/* 	} */
	/* } */
	/* if (!IS_ERR(info->supply.vdd1v8)) { */
	/* 	ret = regulator_enable(info->supply.vdd1v8); */
	/* 	if (ret) { */
	/* 		dev_err(dev, "imx586 vdd1v8 supply enable failed\n"); */
	/* 	} */
	/* } */

	v4l2_clk_enable(info->clk);
	imx586_reset(sd, 1);
	imx586_init(sd, 1);
	return 0;
}

const struct dev_pm_ops imx586_pm =
{
	.suspend = imx586_suspend,
	.resume = imx586_resume,
};



static struct i2c_driver imx586_driver = {
	.driver = {
		.name	= "imx586",
		.of_match_table = of_match_ptr(imx586_of_match),
		.pm	= &imx586_pm,
	},
	.probe		= imx586_probe,
	.remove		= imx586_remove,
	.id_table	= imx586_id,
};

module_i2c_driver(imx586_driver);
MODULE_AUTHOR("qpz <aric.pzqi@ingenic.com>");
MODULE_DESCRIPTION("A low-level driver for OmniVision imx586 sensors");
MODULE_LICENSE("GPL");
