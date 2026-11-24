/*
 * psd030k Camera Driver
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

#define PSD030K_REG_END         0xff
#define PSD030K_REG_DELAY       0x00

#define  psd030k_DEFAULT_WIDTH    640
#define  psd030k_DEFAULT_HEIGHT   480


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

struct psd030k_win_size {
	char *name;
	int width;
	int height;
	const struct regval_list *regs;
	unsigned int mbus_code;
};

struct psd030k_gpio {
	int pin;
	int active_level;
};

struct psd030k_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	struct v4l2_clk			*clk;
	struct v4l2_clk			*mclk;
	const struct psd030k_win_size	*win;

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

unsigned short psd030k_read_reg(struct i2c_client *client, unsigned char reg, unsigned char *value)
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

int psd030k_write_reg(struct i2c_client *client, unsigned char reg, unsigned short value)
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

static struct regval_list psd030k_1080p_regs[] = {
	{0x03,0x00}, // bank
	{0x06,0x03}, // framewidth_h
	{0x07,0x59}, // framewidth_l
	{0x08,0x02}, // fheight_a_h
	{0x09,0x0C}, // fheight_a_l
	{0x03,0x01}, // bank
	{0x04,0x80}, // bayer_control_01
	{0x1B,0x00}, // dmyfwa_h
	{0x1C,0x00}, // dmyfwa_l
	{0x1D,0x00}, // dmyfwb_h
	{0x1E,0x00}, // dmyfwb_l
	{0x1F,0x00}, // retrace_h
	{0x20,0x00}, // retrace_l
	{0x24,0x00}, // tgvstart_h
	{0x25,0x08}, // tgvstart_l
	{0x26,0x01}, // tgvstop_h
	{0x27,0xEC}, // tgvstop_l
	{0x2C,0x00}, // blvstart_0_h
	{0x2D,0x02}, // blvstart_0_l
	{0x2E,0x00}, // blvstop_0_h
	{0x2F,0x08}, // blvstop_0_l
	{0x03,0x02}, // bank
	{0x48,0x0F}, // illuminance_h
	{0x49,0xFF}, // illuminance_l

	//# mirror setting
	{0x03,0x00}, // bank
	{0x05,0x00}, // mirror
	{0x03,0x01}, // bank
	{0x0E,0x01}, // bayer_control_11
	{0x0F,0x00}, // bayer_control_12


	//###############################################################################
	//# Format Timing Setting
	//###############################################################################

	//# Window setting
	{0x03,0x00}, // bank
	{0x0C,0x00}, // windowx1_h
	{0x0D,0x01}, // windowx1_l
	{0x10,0x01}, // windowx2_h
	{0x11,0x41}, // windowx2_l
	{0x0E,0x00}, // windowy1_h
	{0x0F,0x02}, // windowy1_l
	{0x12,0x01}, // windowy2_h
	{0x13,0xE2}, // windowy2_l

	//# Format fifo setting
	{0x03,0x00}, // bank
	{0xAC,0x01}, // rd_bit_sel
	{0x97,0x02}, // rd_cnt_init_h
	{0x98,0x50}, // rd_cnt_init_l
	{0x99,0x00}, // rd_x_start0_h
	{0x9A,0x30}, // rd_x_start0_l
	{0x9B,0x02}, // rd_x_stop0_h
	{0x9C,0xB0}, // rd_x_stop0_l
	{0x9D,0x00}, // rd_y_start_h
	{0x9E,0x0B}, // rd_y_start_l
	{0x9F,0x01}, // rd_y_stop_h
	{0xA0,0xEB}, // rd_y_stop_l

	//# vsync setting
	{0x03,0x00}, // bank
	{0x74,0x00}, // vsyncstartrow0_h
	{0x75,0x0B}, // vsyncstartrow0_l
	{0x76,0x01}, // vsyncstoprow0_h
	{0x77,0xEB}, // vsyncstoprow0_l


	//###############################################################################
	//# TG Data Setting
	//###############################################################################

	//# ADC setting
	{0x03,0x01}, // bank
	{0x8B,0x06}, // dat_max_h
	{0x8C,0xCC}, // dat_max_l
	{0x8D,0x01}, // dat_ofst_h
	{0x8E,0x00}, // dat_ofst_l
	{0x8F,0x01}, // max_robp_h
	{0x90,0xCD}, // max_robp_l

	//# DBLC setting
	{0x03,0x01}, // bank
	{0x05,0xBE}, // bayer_control_02

	//# ABLC setting
	{0x03,0x01}, // bank
	{0x06,0x80}, // bayer_control_03
	{0x07,0xE0}, // bayer_control_04
	{0xDB,0x01}, // blc_top_th_h
	{0xDC,0xB0}, // blc_top_th_l
	{0xDD,0x01}, // blc_bot_th_h
	{0xDE,0x40}, // blc_bot_th_l

	//# Column inttime setting
	{0x03,0x01}, // bank
	{0x0D,0x86}, // bayer_control_10
	{0x03,0x03}, // bank
	{0x4A,0x0E}, // lineth
	{0x4B,0xFF}, // expcorr
	{0x4D,0x80}, // prestop



	//###############################################################################
	//# Format Data Setting
	//###############################################################################

	//# Data max/min setting
	{0x03,0x00}, // bank
	{0xB2,0x02}, // sync_control_1 //0x22 HSync_all_line
	{0xB4,0x00}, // data_min_h
	{0xB5,0x04}, // data_min_l
	{0xB6,0x03}, // data_max_h
	{0xB7,0xF8}, // data_max_l

	//###############################################################################
	//# MIPI Setting
	//###############################################################################
	//# MIPI setting

	{0x03,0x05}, // bank
	{0x04,0x30}, // mipi_control_0
	{0x05,0x9B}, // mipi_control_1
	{0x06,0x99}, // mipi_control_2
	{0x07,0x99}, // mipi_control_3
	{0x08,0x01}, // mipi_control_4

	//# MIPI packet setting
	//# 640/4*5=800
	{0x36,0x03}, // mipi_pkt_size0_h
	{0x37,0x20}, // mipi_pkt_size0_l

	//# MIPI timing setting
	{0x1C,0x00}, // mipi_T_lpx
	{0x1D,0x00}, // mipi_T_clk_prepare
	{0x1E,0x00}, // mipi_T_hs_prepare
	{0x1F,0x00}, // mipi_T_hs_zero
	{0x20,0x00}, // mipi_T_hs_trail
	{0x21,0x00}, // mipi_T_clk_zero
	{0x22,0x00}, // mipi_T_clk_trail
	{0x23,0x00}, // mipi_T_clk_pre
	{0x24,0x00}, // mipi_T_clk_post
	{0x25,0x00}, // mipi_T_wakeup
	{0x26,0x00}, // mipi_T_hsexit
	{0x27,0x00}, // mipi_T_clk_hsexit

	//# MIPI datatype setting
	{0x42,0x00}, // mipi_data_id0

	//###############################################################################
	//# DVP Setting
	//###############################################################################

	//# PAD drivability setting
	{0x03,0x00}, // bank
	{0x24,0xF0}, // pad_control2

	//# PAD setting
	{0x03,0x00}, // bank
	{0x25,0x98}, // pad_control3
	{0x26,0x0F}, // pad_control4
	{0x27,0xF0}, // pad_control5
	{0x28,0x00}, // pad_control6


	//###############################################################################
	//# Clock Setting
	//###############################################################################

	//# pll bandwidth setting
	{0x03,0x00}, // bank
	{0x4E,0x7A}, // pll_control2
	{0x4F,0x7D}, // pll_control3

	//# pll & }clock divider setting
	{0x03,0x00}, // bank
	{0x1A,0xF0}, // i2c_control_1
	{0x51,0x20}, // pll_tg_n_cnt
	{0x52,0x02}, // pll_tg_r_cnt
	{0x53,0x20}, // pll_mp_n_cnt
	{0x54,0x04}, // pll_mp_r_cnt
	{0x56,0x9E}, // clkdiv1
	{0x57,0x18}, // clkdiv2
	{0x58,0xA0}, // clkdiv3
	{0x1A,0x50}, // i2c_control_1

	//# PLL power & bypass setting
	{0x4E,0x52}, // pll_control2
	//$0200 //	wait for 200 us;
//	{0xffff,0x01},	//for dothinkey delay
	{0x4E,0x42}, // pll_control2


	/////! PSD030K_ANALOG_230303 (PSD030K_Analog_global_640x480_DVP_Bayer_30_60fps_230303.ccf)

	//0x03,0x00, W0220 W0223 W0203 W0203

	//## Analog Setting

	//------------------------------------ for pd to xcp sequency
	{0x03,0x00},	// A bank
	{0x3E,0x7F},	// xcp & cp pd disable

	{0x03,0x01},	// B bank
	{0x0B,0x54},	// xcp_clk_en[4]=1 

	{0x03,0x00},	// A bank
	{0x3E,0x03},	// xcp & cp pd disable
	//--------}----------------------------

	{0x03,0x00},	// A bank
	{0x38,0x27},   // Pixelbias1 0111
	{0x3B,0x67},	// Ramprange 1.35V --> 1.40625V
	{0x3D,0x65},	// S2(VDD/NCP_L), S1(VDD/NCP_L) ==>S2 NCP 
	{0x3F,0x8A},   // NCP_T -0.9 --> -1.1V
	{0x40,0x81},	// NCP_LS -0.7-->-0.2V
	{0x41,0x88},	// PCP_RT 3.3V-->3.5V
	{0x42,0x82},	// PCP_L 4.3V-->3.0V
	{0x43,0x07},	// VPC 0.7V 

	{0x03,0x01},	// B bank
	{0x7A,0x10},	// globalgain x2(global shutter x1)

	{0x03,0x02},	// C bank
	{0xC7,0x10},	// blacksun[6:0] 60h-->20h-->10h
	{0xC8,0x20},   // blacksun x4 20h

	{0x03,0x03},	// D bank
	{0x4D,0x00},	// prestop 80h-->31h(top h-line remove@x8)


	//## Timing Setting

	{0x03,0x02},	// C bank
	
	{0x13,0x00},	// row_ls_en_start_h (1)
	{0x14,0x01},	// row_ls_en_start_l 
	{0x15,0x03},	// row_ls_en_stop_h (856)
	{0x16,0x58},	// row_ls_en_stop_l 
	
	{0x1F,0x00},	// row_r_ls_rst_start_h (20)
	{0x20,0x14},	// row_r_ls_rst_start_l 
	{0x21,0x00},	// row_r_ls_rst_stop_h (20)
	{0x22,0x14},	// row_r_ls_rst_stop_l 
	
	{0x34,0x00},	// row_r_rs_tx_start_h (0)
	{0x35,0x00},	// row_r_rs_tx_start_l 
	{0x36,0x00},	// row_r_rs_tx_stop_h (0)
	{0x37,0x00},	// row_r_rs_tx_stop_l 
	
	{0x38,0x01},	// row_r_ls_tx_start_h (350)
	{0x39,0x5E},	// row_r_ls_tx_start_l 
	{0x3A,0x01},	// row_r_ls_tx_stop_h (350)
	{0x3B,0x5E},	// row_r_ls_tx_stop_l 
	
	{0x1B,0x00},	// row_g_pr_rst_stop_rcnt_h (0)
	{0x1C,0x00},	// row_g_pr_rst_stop_rcnt_l 
	{0x17,0x00},	// row_g_pr_rst_start_rcnt_h (0)
	{0x18,0x00},	// row_g_pr_rst_start_rcnt_l 
	
	{0x2F,0x02},	// row_g_pr_tx_stop1_rcnt_h (512)
	{0x30,0x00},	// row_g_pr_tx_stop1_rcnt_l 
	{0x2B,0x00},	// row_g_pr_tx_start1_rcnt_h (0)
	{0x2C,0x00},	// row_g_pr_tx_start1_rcnt_l 
	
	{0x27,0x00},	// row_g_pr_tx_stop0_rcnt_h (0)
	{0x28,0x00},	// row_g_pr_tx_stop0_rcnt_l 
	{0x23,0x00},	// row_g_pr_tx_start0_rcnt_h (0)
	{0x24,0x00},	// row_g_pr_tx_start0_rcnt_l 
	
	
	{0x03,0x01},	// B bank
	
	{0x6C,0x01},	// inttime_h (511)
	{0x6D,0xFF},	// inttime_m 
	
	
	{0x03,0x02},	// C bank
	
	{0x40,0x00},	// row_g_pr_s2_stop_rcnt_h (0)
	{0x41,0x00},	// row_g_pr_s2_stop_rcnt_l 
	{0x3C,0x00},	// row_g_pr_s2_start_rcnt_h (0)
	{0x3D,0x00},	// row_g_pr_s2_start_rcnt_l 
	
	{0x5C,0x00},	// row_g_pr_s1_stop_rcnt_h (0)
	{0x5D,0x00},	// row_g_pr_s1_stop_rcnt_l 
	{0x58,0x00},	// row_g_pr_s1_start_rcnt_h (0)
	{0x59,0x00},	// row_g_pr_s1_start_rcnt_l 
	
	{0x6C,0x00},	// row_g_pr_vpc_stop1_rcnt_h (0)
	{0x6D,0x00},	// row_g_pr_vpc_stop1_rcnt_l 
	{0x68,0x00},	// row_g_pr_vpc_start1_rcnt_h (0)
	{0x69,0x00},	// row_g_pr_vpc_start1_rcnt_l 
	
	{0x64,0x00},	// row_g_pr_vpc_stop0_rcnt_h (0)
	{0x65,0x00},	// row_g_pr_vpc_stop0_rcnt_l 
	{0x60,0x00},	// row_g_pr_vpc_start0_rcnt_h (0)
	{0x61,0x00},	// row_g_pr_vpc_start0_rcnt_l 
	
	{0x19,0x00},	// row_g_pr_rst_start_ccnt_h (250)
	{0x1A,0xFA},	// row_g_pr_rst_start_ccnt_l 
	{0x1D,0x03},	// row_g_pr_rst_stop_ccnt_h (800)
	{0x1E,0x20},	// row_g_pr_rst_stop_ccnt_l 
	
	{0x29,0x01},	// row_g_pr_tx_stop0_ccnt_h (380)
	{0x2A,0x7C},	// row_g_pr_tx_stop0_ccnt_l 
	{0x2D,0x01},	// row_g_pr_tx_start1_ccnt_h (430)
	{0x2E,0xAE},	// row_g_pr_tx_start1_ccnt_l 
	{0x31,0x03},	// row_g_pr_tx_stop1_ccnt_h (831)
	{0x32,0x3F},	// row_g_pr_tx_stop1_ccnt_l 
	
	{0x3E,0x01},	// row_g_pr_s2_start_ccnt_h (270)
	{0x3F,0x0E},	// row_g_pr_s2_start_ccnt_l 
	{0x42,0x01},	// row_g_pr_s2_stop_ccnt_h (368)
	{0x43,0x70},	// row_g_pr_s2_stop_ccnt_l 
	
	{0x5A,0x01},	// row_g_pr_s1_start_ccnt_h (270)
	{0x5B,0x0E},	// row_g_pr_s1_start_ccnt_l 
	{0x5E,0x03},	// row_g_pr_s1_stop_ccnt_h (785)
	{0x5F,0x11},	// row_g_pr_s1_stop_ccnt_l 
	
	{0x6A,0x01},	// row_g_pr_vpc_start1_ccnt_h (340)
	{0x6B,0x54},	// row_g_pr_vpc_start1_ccnt_l 
	{0x6E,0x01},	// row_g_pr_vpc_stop1_ccnt_h (360)
	{0x6F,0x68},	// row_g_pr_vpc_stop1_ccnt_l 
	
	{0x62,0x01},	// row_g_pr_vpc_start0_ccnt_h (418)
	{0x63,0xA2},	// row_g_pr_vpc_start0_ccnt_l 
	{0x66,0x01},	// row_g_pr_vpc_stop0_ccnt_h (445)
	{0x67,0xBD},	// row_g_pr_vpc_stop0_ccnt_l 
	
	{0x48,0x02},	// row_s2_pcp_sel_stop_rcnt_h (600)
	{0x49,0x58},	// row_s2_pcp_sel_stop_rcnt_l 
	{0x44,0x00},	// row_s2_pcp_sel_start_rcnt_h (0)
	{0x45,0x00},	// row_s2_pcp_sel_start_rcnt_l 
	
	{0x46,0x00},	// row_s2_pcp_sel_start_ccnt_h (1)
	{0x47,0x01},	// row_s2_pcp_sel_start_ccnt_l 
	{0x4A,0x03},	// row_s2_pcp_sel_stop_ccnt_h (802)
	{0x4B,0x22},	// row_s2_pcp_sel_stop_ccnt_l 
	
	{0x50,0x02},	// row_s2_ncp_sel_stop_rcnt_h (600)
	{0x51,0x58},	// row_s2_ncp_sel_stop_rcnt_l 
	{0x4C,0x00},	// row_s2_ncp_sel_start_rcnt_h (0)
	{0x4D,0x00},	// row_s2_ncp_sel_start_rcnt_l 
	
	{0x4E,0x00},	// row_s2_ncp_sel_start_ccnt_h (1)
	{0x4F,0x01},	// row_s2_ncp_sel_start_ccnt_l 
	{0x52,0x03},	// row_s2_ncp_sel_stop_ccnt_h (802)
	{0x53,0x22},	// row_s2_ncp_sel_stop_ccnt_l 
	
	{0x54,0x01},	// row_r_ls_s2_start_h (280)
	{0x55,0x18},	// row_r_ls_s2_start_l 
	{0x56,0x03},	// row_r_ls_s2_stop_h (780)
	{0x57,0x0C},	// row_r_ls_s2_stop_l 
	
	{0x05,0x03},	// row_flush_start_start_h (855)
	{0x06,0x57},	// row_flush_start_start_l 
	{0x07,0x03},	// row_flush_start_stop_h (855)
	{0x08,0x57},	// row_flush_start_stop_l 
	
	{0x09,0x00},	// row_flush_stop_start_h (21)
	{0x0A,0x15},	// row_flush_stop_start_l 
	{0x0B,0x00},	// row_flush_stop_stop_h (21)
	{0x0C,0x15},	// row_flush_stop_stop_l 
	
	{0x0D,0x03},	// row_latch_rst_start_h (855)
	{0x0E,0x57},	// row_latch_rst_start_l 
	{0x0F,0x03},	// row_latch_rst_stop_h (855)
	{0x10,0x57},	// row_latch_rst_stop_l 
	
	{0x88,0x01},	// cds_bls_en_start_h (300)
	{0x89,0x2C},	// cds_bls_en_start_l 
	
	{0x80,0x00},	// cds_store1_start_h (10)
	{0x81,0x0A},	// cds_store1_start_l 
	{0x82,0x00},	// cds_store1_stop_h (80)
	{0x83,0x50},	// cds_store1_stop_l 
	
	{0x84,0x00},	// cds_store2_start_h (10)
	{0x85,0x0A},	// cds_store2_start_l 
	{0x86,0x00},	// cds_store2_stop_h (110)
	{0x87,0x6E},	// cds_store2_stop_l 
	
	{0x8E,0x00},	// cds_pxl_track_start1_h (0)
	{0x8F,0x00},	// cds_pxl_track_start1_l 
	{0x90,0x03},	// cds_pxl_track_stop1_h (857)
	{0x91,0x59},	// cds_pxl_track_stop1_l 
	{0x92,0x03},	// cds_pxl_track_start2_h (857)
	{0x93,0x59},	// cds_pxl_track_start2_l 
	{0x94,0x03},	// cds_pxl_track_stop2_h (857)
	{0x95,0x59},	// cds_pxl_track_stop2_l 
	
	{0x9A,0x00},	// cds_amp1_rst_start1_h (155)
	{0x9B,0x9B},	// cds_amp1_rst_start1_l 
	{0x9C,0x00},	// cds_amp1_rst_stop1_h (185)
	{0x9D,0xB9},	// cds_amp1_rst_stop1_l 
	{0x9E,0x01},	// cds_amp1_rst_start2_h (330)
	{0x9F,0x4A},	// cds_amp1_rst_start2_l 
	{0xA0,0x01},	// cds_amp1_rst_stop2_h (360)
	{0xA1,0x68},	// cds_amp1_rst_stop2_l 
	
	{0xA2,0x00},	// cds_amp2_clamp_en_start1_h (155)
	{0xA3,0x9B},	// cds_amp2_clamp_en_start1_l 
	{0xA4,0x03},	// cds_amp2_clamp_en_stop1_h (857)
	{0xA5,0x59},	// cds_amp2_clamp_en_stop1_l 
	{0xA6,0x03},	// cds_amp2_clamp_en_start2_h (857)
	{0xA7,0x59},	// cds_amp2_clamp_en_start2_l 
	{0xA8,0x03},	// cds_amp2_clamp_en_stop2_h (857)
	{0xA9,0x59},	// cds_amp2_clamp_en_stop2_l 
	
	{0xAA,0x03},	// cds_amp2_rst_start1_h (857)
	{0xAB,0x59},	// cds_amp2_rst_start1_l 
	{0xAC,0x03},	// cds_amp2_rst_stop1_h (857)
	{0xAD,0x59},	// cds_amp2_rst_stop1_l 
	{0xAE,0x03},	// cds_amp2_rst_start2_h (857)
	{0xAF,0x59},	// cds_amp2_rst_start2_l 
	{0xB0,0x03},	// cds_amp2_rst_stop2_h (857)
	{0xB1,0x59},	// cds_amp2_rst_stop2_l 
	
	{0xB2,0x00},	// cds_sf_rst_start1_h (157)
	{0xB3,0x9D},	// cds_sf_rst_start1_l 
	{0xB4,0x00},	// cds_sf_rst_stop1_h (187)
	{0xB5,0xBB},	// cds_sf_rst_stop1_l 
	{0xB6,0x01},	// cds_sf_rst_start2_h (332)
	{0xB7,0x4C},	// cds_sf_rst_start2_l 
	{0xB8,0x01},	// cds_sf_rst_stop2_h (362)
	{0xB9,0x6A},	// cds_sf_rst_stop2_l 
	
	
	{0x03,0x03},	// D bank
	
	{0x06,0x01},	// ramp_ablc_sw_en_start1_h (280)
	{0x07,0x18},	// ramp_ablc_sw_en_start1_l 
	{0x08,0x01},	// ramp_ablc_sw_en_stop1_h (282)
	{0x09,0x1A},	// ramp_ablc_sw_en_stop1_l 
	
	{0x0A,0x03},	// ramp_ablc_sw_en_start2_h (805)
	{0x0B,0x25},	// ramp_ablc_sw_en_start2_l 
	{0x0C,0x03},	// ramp_ablc_sw_en_stop2_h (807)
	{0x0D,0x27},	// ramp_ablc_sw_en_stop2_l 
	
	{0x16,0x00},	// ramp_state_lsb_start1_h (115)
	{0x17,0x73},	// ramp_state_lsb_start1_l 
	{0x18,0x00},	// ramp_state_lsb_stop1_h (117)
	{0x19,0x75},	// ramp_state_lsb_stop1_l 
	
	{0x1A,0x01},	// ramp_state_lsb_start2_h (280)
	{0x1B,0x18},	// ramp_state_lsb_start2_l 
	{0x1C,0x01},	// ramp_state_lsb_stop2_h (282)
	{0x1D,0x1A},	// ramp_state_lsb_stop2_l 
	
	{0x1E,0x03},	// ramp_state_lsb_start3_h (805)
	{0x1F,0x25},	// ramp_state_lsb_start3_l 
	{0x20,0x03},	// ramp_state_lsb_stop3_h (807)
	{0x21,0x27},	// ramp_state_lsb_stop3_l 
	
	{0x12,0x00},	// ramp_state_msb_start_h (118)
	{0x13,0x76},	// ramp_state_msb_start_l 
	{0x14,0x00},	// ramp_state_msb_stop_h (118)
	{0x15,0x76},	// ramp_state_msb_stop_l 
	
	{0x32,0x00},	// ramp_clk_start1_h (118)
	{0x33,0x76},	// ramp_clk_start1_l 
	{0x24,0x00},	// ramp_clk_stop1_y0_h (122)
	{0x25,0x7A},	// ramp_clk_stop1_y0_l 
	
	{0x34,0x00},	// ramp_clk_start2_h (195)
	{0x35,0xC3},	// ramp_clk_start2_l 
	{0x36,0x01},	// ramp_clk_stop2_h (300)
	{0x37,0x2C},	// ramp_clk_stop2_l 
	
	{0x38,0x01},	// ramp_clk_start3_h (405)
	{0x39,0x95},	// ramp_clk_start3_l 
	{0x3A,0x03},	// ramp_clk_stop3_h (777)
	{0x3B,0x09},	// ramp_clk_stop3_l 
	
	{0x3C,0x03},	// ramp_clk_start4_h (807)
	{0x3D,0x27},	// ramp_clk_start4_l 
	{0x2C,0x03},	// ramp_clk_stop4_y0_h (837)
	{0x2D,0x45},	// ramp_clk_stop4_y0_l 
	
	
	{0x03,0x03},	// D bank
	
	{0x0E,0x03},	// ramp_ref_sample_start_h (810)
	{0x0F,0x2A},	// ramp_ref_sample_start_l 
	{0x10,0x03},	// ramp_ref_sample_stop_h (857)
	{0x11,0x59},	// ramp_ref_sample_stop_l 
	
	{0x3E,0x03},	// ramp_dvdd_sample_start_h (810)
	{0x3F,0x2A},	// ramp_dvdd_sample_start_l 
	{0x40,0x03},	// ramp_dvdd_sample_stop_h (857)
	{0x41,0x59},	// ramp_dvdd_sample_stop_l 
	
	{0x42,0x03},	// ramp_ablc_ref_sample_start_h (810)
	{0x43,0x2A},	// ramp_ablc_ref_sample_start_l 
	{0x44,0x03},	// ramp_ablc_ref_sample_stop_h (857)
	{0x45,0x59},	// ramp_ablc_ref_sample_stop_l 
	
	
	{0x03,0x02},	// C bank
	
	{0xCA,0x00},	// adc_clk_en_start1_h (195)
	{0xCB,0xC3},	// adc_clk_en_start1_l 
	{0xCC,0x01},	// adc_clk_en_stop1_h (275)
	{0xCD,0x13},	// adc_clk_en_stop1_l 
	{0xCE,0x01},	// adc_clk_en_start2_h (405)
	{0xCF,0x95},	// adc_clk_en_start2_l 
	{0xD0,0x03},	// adc_clk_en_stop2_h (777)
	{0xD1,0x09},	// adc_clk_en_stop2_l 
	
	{0xD2,0x01},	// cntr_pol_start_h (280)
	{0xD3,0x18},	// cntr_pol_start_l 
	{0xD4,0x03},	// cntr_pol_stop_h (805)
	{0xD5,0x25},	// cntr_pol_stop_l 
	
	{0xDA,0x00},	// cntr_load_en_start1_h (191)
	{0xDB,0xBF},	// cntr_load_en_start1_l 
	{0xDC,0x01},	// cntr_load_en_stop1_h (279)
	{0xDD,0x17},	// cntr_load_en_stop1_l 
	{0xDE,0x01},	// cntr_load_en_start2_h (401)
	{0xDF,0x91},	// cntr_load_en_start2_l 
	{0xE0,0x03},	// cntr_load_en_stop2_h (781)
	{0xE1,0x0D},	// cntr_load_en_stop2_l 
	
	{0xE2,0x00},	// cntr_latch_en1_start1_h (193)
	{0xE3,0xC1},	// cntr_latch_en1_start1_l 
	{0xE4,0x01},	// cntr_latch_en1_stop1_h (277)
	{0xE5,0x15},	// cntr_latch_en1_stop1_l 
	{0xE6,0x01},	// cntr_latch_en1_start2_h (403)
	{0xE7,0x93},	// cntr_latch_en1_start2_l 
	{0xE8,0x03},	// cntr_latch_en1_stop2_h (779)
	{0xE9,0x0B},	// cntr_latch_en1_stop2_l 
	
	{0xEA,0x00},	// cntr_latch_en2_start1_h (193)
	{0xEB,0xC1},	// cntr_latch_en2_start1_l 
	{0xEC,0x01},	// cntr_latch_en2_stop1_h (277)
	{0xED,0x15},	// cntr_latch_en2_stop1_l 
	{0xEE,0x01},	// cntr_latch_en2_start2_h (403)
	{0xEF,0x93},	// cntr_latch_en2_start2_l 
	{0xF0,0x03},	// cntr_latch_en2_stop2_h (779)
	{0xF1,0x0B},	// cntr_latch_en2_stop2_l 
	
	{0xD6,0x03},	// cntr_rstb_start_h (855)
	{0xD7,0x57},	// cntr_rstb_start_l 
	{0xD8,0x03},	// cntr_rstb_stop_h (857)
	{0xD9,0x59},	// cntr_rstb_stop_l 
	
	{0xF2,0x03},	// cntr_transfer_start_h (852)
	{0xF3,0x54},	// cntr_transfer_start_l 
	{0xF4,0x03},	// cntr_transfer_stop_h (854)
	{0xF5,0x56},	// cntr_transfer_stop_l 
	
	{0x8A,0x03},	// cds_pxl_bias1_track_start_h (810)
	{0x8B,0x2A},	// cds_pxl_bias1_track_start_l 
	{0x8C,0x03},	// cds_pxl_bias1_track_stop_h (857)
	{0x8D,0x59},	// cds_pxl_bias1_track_stop_l 
	
	{0x96,0x03},	// cds_amp1_bias1_track_start_h (810)
	{0x97,0x2A},	// cds_amp1_bias1_track_start_l 
	{0x98,0x03},	// cds_amp1_bias1_track_stop_h (857)
	{0x99,0x59},	// cds_amp1_bias1_track_stop_l 
	
	{0xBA,0x00},	// cds_amp2_clamp_sample_start_h (0)
	{0xBB,0x00},	// cds_amp2_clamp_sample_start_l 
	{0xBC,0x03},	// cds_amp2_clamp_sample_stop_h (857)
	{0xBD,0x59},	// cds_amp2_clamp_sample_stop_l 
	
	
	{0x03,0x01},	// B bank
	
	{0x28,0x02},	// tghstart_h (528)
	{0x29,0x10},	// tghstart_l 
	{0x2A,0x03},	// tghstop_h (850)
	{0x2B,0x52},	// tghstop_l 
	
	{0x3C,0x07},	// coffset_h (1874)
	{0x3D,0x52},	// coffset_l 
	
	{0x38,0x02},	// blhstart_h (528)
	{0x39,0x10},	// blhstart_l 
	{0x3A,0x03},	// blhstop_h (850)
	{0x3B,0x52},	// blhstop_l

	//############################################################
	//########### Don't change setting value stop ################
	//############################################################



	//#### TO_END_START ##################

	/////! PSD030K_DIGITAL_230105 (PGD030K__60fps_640x480_DVP_Bayer_230105.ccf)

	//0x03,0x00, W0211 W0223 W0201 W0205
	{0x03,0x05},
	{0x04,0x30},

	{PSD030K_REG_END, 0x00},        /* END MARKER */
};

#if 0

static const struct regval_list psd030k_wb_auto_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_wb_incandescence_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_wb_daylight_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_wb_fluorescent_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_wb_cloud_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct mode_list psd030k_balance[] = {
	{0, psd030k_wb_auto_regs}, {1, psd030k_wb_incandescence_regs},
	{2, psd030k_wb_daylight_regs}, {3, psd030k_wb_fluorescent_regs},
	{4, psd030k_wb_cloud_regs},
};


static const struct regval_list psd030k_effect_normal_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_effect_grayscale_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_effect_sepia_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_effect_colorinv_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct regval_list psd030k_effect_sepiabluel_regs[] = {
	{PSD030K_REG_END, 0x00},/* END MARKER */
};

static const struct mode_list psd030k_effect[] = {
	{0, psd030k_effect_normal_regs}, {1, psd030k_effect_grayscale_regs},
	{2, psd030k_effect_sepia_regs}, {3, psd030k_effect_colorinv_regs},
	{4, psd030k_effect_sepiabluel_regs},
};

#endif

static struct psd030k_win_size psd030k_supported_win_sizes[] = {
	{
		.name = "480p",
		.width = 640,
		.height = 480,
		.regs = psd030k_1080p_regs,
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(psd030k_supported_win_sizes))

/*
 * General functions
 */
static struct psd030k_priv *to_psd030k(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct psd030k_priv,
			    subdev);
}

static int psd030k_write_array(struct i2c_client *client,
			      const struct regval_list *vals)
{
	int ret;

	while (vals->reg_num != PSD030K_REG_END) {
		if (vals->reg_num == PSD030K_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = psd030k_write_reg(client, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}
#if 0
static int psd030k_mask_set(struct i2c_client *client,
			   u16  reg, u16  mask, u16  set)
{
	int ret = 0;
	unsigned char value[2] = {0};
	ret = psd030k_read_reg(client, reg, value);
	if (ret < 0)
		return ret;

	val &= ~mask;
	val |= set & mask;

	dev_vdbg(&client->dev, "masks: 0x%02x, 0x%02x", reg, val);

	return psd030k_write_reg(client, reg, val);
}
#endif
static int psd030k_reset(struct i2c_client *client)
{
	struct psd030k_priv *priv = to_psd030k(client);

	if(priv->resetb_gpio) {
		gpiod_direction_output(priv->resetb_gpio, 1);
		msleep(10);
		gpiod_direction_output(priv->resetb_gpio, 0);
		msleep(10);
	}
	return 0;
}

static struct regval_list psd030k_stream_on[] = {
	{0x4e,0x28},
	{PSD030K_REG_END, 0x00},
};

static struct regval_list psd030k_stream_off[] = {
	{0x4e,0x00},
	{PSD030K_REG_END, 0x00},
};

/*video_ops*/
static int psd030k_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);

	if (!enable ) {
		dev_info(&client->dev, "stream down\n");
		psd030k_write_array(client, psd030k_stream_off);
		return 0;
	}

	dev_info(&client->dev, "stream on\n");
	psd030k_write_array(client, psd030k_stream_on);

	return 0;
}

static int psd030k_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= psd030k_DEFAULT_WIDTH;
	a->bounds.height		= psd030k_DEFAULT_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int psd030k_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left	= 0;
	a->c.top	= 0;
	a->c.width	= psd030k_DEFAULT_WIDTH;
	a->c.height	= psd030k_DEFAULT_HEIGHT;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int psd030k_g_mbus_config(struct v4l2_subdev *sd,
		struct v4l2_mbus_config *cfg)
{
#if 1
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_FALLING | V4L2_MBUS_MASTER |
		V4L2_MBUS_VSYNC_ACTIVE_LOW | V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_PARALLEL;
#endif
	return 0;
}

/*ctrl_ops*/
static int psd030k_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd =
		&container_of(ctrl->handler, struct psd030k_priv, hdl)->subdev;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct psd030k_priv *priv = to_psd030k(client);

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

static int psd030k_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
#if 0
	struct v4l2_subdev *sd =
		&container_of(ctrl->handler, struct psd030k_priv, hdl)->subdev;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct psd030k_priv *priv = to_psd030k(client);
	int i = 0;
	u16 value;

	int balance_count = ARRAY_SIZE(psd030k_balance);
	int effect_count = ARRAY_SIZE(psd030k_effect);

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		value = ctrl->val ? psd030k_FLIP_VAL : 0x00;
		priv->flag_vflip = ctrl->val ? 1 : 0;
//		ret = psd030k_mask_set(client, 0x301D, psd030k_FLIP_MASK, value);
		break;

	case V4L2_CID_HFLIP:
		value = ctrl->val ? psd030k_FLIP_VAL : 0x00;
		priv->flag_hflip = ctrl->val ? 1 : 0;
//		ret = psd030k_mask_set(client, 0x301D, psd030k_FLIP_MASK, value);
		break;

	default:
		dev_err(&client->dev, "no V4L2 CID: 0x%x ", ctrl->id);
		return -EINVAL;
	}

#endif
	return ret;
}

/*core_ops*/
#ifdef CONFIG_VIDEO_ADV_DEBUG
static int psd030k_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	reg->size = 1;
	if (reg->reg > 0xff)
		return -EINVAL;

	ret = psd030k_read_reg(client, reg->reg, reg->val);

	return 0;
}

static int psd030k_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff ||
	    reg->val > 0xff)
		return -EINVAL;

	return psd030k_write_reg(client, reg->reg, reg->val);
}
#endif

static int psd030k_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct psd030k_priv *priv = to_psd030k(client);

	if(priv->sd_pdata) {
		if(on)
			return regulator_bulk_enable(priv->sd_pdata->num_regulators,priv->sd_pdata->regulators);
		else
			return regulator_bulk_disable(priv->sd_pdata->num_regulators,priv->sd_pdata->regulators);
	} else if(priv->power){
		return priv->power(client, on);
	} else {
		dev_err(&client->dev, "psd030k_s_power failde");
		return -EINVAL;
	}
}

/* Select the nearest higher resolution for capture */
static const struct psd030k_win_size *psd030k_select_win(u32 *code, u32 *width, u32 *height)
{
	int i, default_size = ARRAY_SIZE(psd030k_supported_win_sizes) - 1;

	for (i = 0; i < ARRAY_SIZE(psd030k_supported_win_sizes); i++) {
		if ((*width >= psd030k_supported_win_sizes[i].width) &&
		    (*height >= psd030k_supported_win_sizes[i].height) &&
		    (*code == psd030k_supported_win_sizes[i].mbus_code)) {
			*width = psd030k_supported_win_sizes[i].width;
			*height = psd030k_supported_win_sizes[i].height;
			return &psd030k_supported_win_sizes[i];
		}
	}

	//*width = psd030k_supported_win_sizes[default_size].width;
	//*height = psd030k_supported_win_sizes[default_size].height;
	//return &psd030k_supported_win_sizes[default_size];
	return NULL;
}

static int psd030k_set_params(struct i2c_client *client, u32 *width, u32 *height, u32 code)
{
	struct psd030k_priv       *priv = to_psd030k(client);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	int ret;

	int bala_index = priv->balance_value;
	int effe_index = priv->effect_value;

	/* reset hardware */
	psd030k_reset(client);

	/* initialize the sensor with default data */
	dev_dbg(&client->dev, "%s: Init default", __func__);
#if 0
	ret = psd030k_write_array(client, psd030k_init_regs);
	if (ret < 0)
		goto err;

	/* set balance */
	ret = psd030k_write_array(client, psd030k_balance[bala_index].mode_regs);
	if (ret < 0)
		goto err;

	/* set effect */
	ret = psd030k_write_array(client, psd030k_effect[effe_index].mode_regs);
	if (ret < 0)
		goto err;
#endif
	/* set size win */
	dev_dbg(&client->dev, "priv->win.width = %d\n", priv->win->width);
	dev_dbg(&client->dev, "priv->win.height = %d\n", priv->win->height);

	ret = psd030k_write_array(client, priv->win->regs);
	if (ret < 0)
		goto err;

	priv->cfmt_code = code;
	*width = priv->win->width;
	*height = priv->win->height;

	return 0;

err:
	dev_err(&client->dev, "%s: Error %d", __func__, ret);
	psd030k_reset(client);
	priv->win = NULL;

	return ret;
}

/*pad_ops*/
static int psd030k_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct psd030k_priv *priv = to_psd030k(client);

	if(!priv->win)
		return -EINVAL;

	sel->r.top = 0;
	sel->r.left = 0;
	sel->r.width = priv->win->width;
	sel->r.height = priv->win->height;

	return 0;
}

static int psd030k_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct psd030k_priv *priv = to_psd030k(client);

	if (format->pad)
		return -EINVAL;

	if(priv->win){
		mf->width = priv->win->width;
		mf->height = priv->win->height;
	} else {
		mf->width = psd030k_DEFAULT_WIDTH;
		mf->height = psd030k_DEFAULT_HEIGHT;
	}
	mf->code = priv->cfmt_code;

	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int psd030k_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct psd030k_priv       *priv = to_psd030k(client);

	if (format->pad)
		return -EINVAL;

	priv->win = psd030k_select_win(&mf->code, &mf->width, &mf->height);
	if(!priv->win)
		return -EINVAL;

	mf->field	= V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_JPEG;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		psd030k_set_params(client, &mf->width,
					 &mf->height, mf->code);
	if(cfg)
		cfg->try_fmt = *mf;
	return 0;
}

static int psd030k_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(psd030k_supported_win_sizes))
		return -EINVAL;

	code->code = psd030k_supported_win_sizes[code->index].mbus_code;
	return 0;
}

static int psd030k_enum_frame_size(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_frame_size_enum *fse)
{
	int i, j;
	int num_valid = -1;
	__u32 index = fse->index;

	if(index >= N_WIN_SIZES)
		return -EINVAL;

	fse->code = psd030k_supported_win_sizes[index].mbus_code;
	fse->min_width = psd030k_supported_win_sizes[index].width;
	fse->max_width = fse->min_width;
	fse->min_height = psd030k_supported_win_sizes[index].height;
	fse->max_height = fse->min_height;
	return 0;

}

static int psd030k_detect(struct i2c_client *client, unsigned int *ident)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	unsigned char v;
	int ret;

	psd030k_s_power(sd, 1);
/*
	ret = psd030k_read_reg(client, 0x02, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != PSD030K_CHIP_ID_H)
		return -ENODEV;

	ret = psd030k_read_reg(client, 0x03, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != PSD030K_CHIP_ID_L)
		return -ENODEV;

	ret = psd030k_read_reg(client, 0x04, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != 0x05 && v != 0x06 && v != 0x07)
		return -ENODEV;
*/
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	return 0;

}

static const struct v4l2_ctrl_ops psd030k_ctrl_ops = {
	.s_ctrl = psd030k_s_ctrl,
	.g_volatile_ctrl = psd030k_g_ctrl,
};

static struct v4l2_subdev_core_ops psd030k_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= psd030k_g_register,
	.s_register	= psd030k_s_register,
#endif
	.s_power	= psd030k_s_power,
};

static struct v4l2_subdev_video_ops psd030k_subdev_video_ops = {
	.s_stream	= psd030k_s_stream,
	.cropcap	= psd030k_cropcap,
	.g_crop		= psd030k_g_crop,
	.g_mbus_config	= psd030k_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops psd030k_subdev_pad_ops = {
	.enum_mbus_code = psd030k_enum_mbus_code,
	.enum_frame_size = psd030k_enum_frame_size,
	.get_fmt	= psd030k_get_fmt,
	.set_fmt	= psd030k_set_fmt,
	.get_selection	= psd030k_get_selection,
};

static struct v4l2_subdev_ops psd030k_subdev_ops = {
	.core	= &psd030k_subdev_core_ops,
	.video	= &psd030k_subdev_video_ops,
	.pad	= &psd030k_subdev_pad_ops,
};

/* OF probe functions */
static int psd030k_hw_power(struct i2c_client *client, int on)
{
	struct psd030k_priv *priv = to_psd030k(client);

	dev_dbg(&client->dev, "%s: %s the camera\n",
			__func__, on ? "ENABLE" : "DISABLE");

	/* thses gpio should be set according to the active level in dt defines */
	if(priv->vcc_en_gpio) {
		gpiod_direction_output(priv->vcc_en_gpio, !on);
	}


	msleep(10);
	return 0;
}

static int psd030k_hw_reset(struct device *dev)
{
	/*
	struct i2c_client *client = to_i2c_client(dev);
	struct psd030k_priv *priv = to_psd030k(client);

	gpio_direction_output(priv->reset.pin, priv->reset.active_level);
	msleep(10);
	gpio_direction_output(priv->reset.pin, !priv->reset.active_level);
	msleep(10);
	return 0;
	*/
}

static int psd030k_probe_dt(struct i2c_client *client,
		struct psd030k_priv *priv)
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

		//if(!client->dev.platform_data) /*TODO*/
		//	client->dev.platform_data = &priv->sd_pdata;
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

		priv->power = psd030k_hw_power;
	//	priv->reset = psd030k_hw_reset; /*TODO*/
	}
	return 0;
}

static int psd030k_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct psd030k_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int	ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"psd030k: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct psd030k_priv), GFP_KERNEL);
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

	/*mclk gate close*/
//	*(volatile unsigned int *)0xb0000024 &= ~(1 << 5); 

	ret = psd030k_probe_dt(client, priv);
	if (ret)
		goto err_probe_dt;

	v4l2_i2c_subdev_init(&priv->subdev, client, &psd030k_subdev_ops);

	/* add handler */
	v4l2_ctrl_handler_init(&priv->hdl, 2);

	v4l2_ctrl_new_std(&priv->hdl, &psd030k_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &psd030k_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);

	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error) {
		ret = priv->hdl.error;
		goto err_hdl;
	}

#if 1
	ret = psd030k_detect(client, NULL);
	if(ret < 0){
		v4l_err(client,"chip found @ 0x%x (%s) is not an psd030k chip.\n",
				client->addr << 1, client->adapter->name);
		goto err_detect;
	}
#endif

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto err_async_register_subdev;

	dev_info(&adapter->dev, "psd030k probed\n");

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

static int psd030k_remove(struct i2c_client *client)
{
	struct psd030k_priv       *priv = to_psd030k(client);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->subdev);
	v4l2_clk_put(priv->clk);
	devm_kfree(&client->dev, priv);
	return 0;
}

static const struct i2c_device_id psd030k_id[] = {
	{ "psd030k",  0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, psd030k_id);
static const struct of_device_id psd030k_of_match[] = {
	{.compatible = "PSD030K", },
	{},
};
MODULE_DEVICE_TABLE(of, psd030k_of_match);
static struct i2c_driver psd030k_i2c_driver = {
	.driver = {
		.name = "psd030k",
		.of_match_table = of_match_ptr(psd030k_of_match),
	},
	.probe    = psd030k_probe,
	.remove   = psd030k_remove,
	.id_table = psd030k_id,
};
module_i2c_driver(psd030k_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for onsemi psd030k sensor");
MODULE_LICENSE("GPL v2");
