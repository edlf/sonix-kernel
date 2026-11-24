#ifndef __INGENIC_EFUSE_H__
#define __INGENIC_EFUSE_H__

#define EFUSE_CTRL		0x0
#define EFUSE_CFG		0x4
#define EFUSE_STATE		0x8
#define EFUSE_DATA(n)		(0xC + (n) * 4)


// efuse ctrl bits
#define EFUSE_CTRL_ADDR              (21)
#define EFUSE_CTRL_ADDR_MASK         (0x3f)
#define EFUSE_CTRL_LEN               (16)
#define EFUSE_CTRL_LEN_MASK          (0x1f)
#define EFUSE_CTRL_PGEN              (1 << 15)
#define EFUSE_CTRL_WREN              (1 << 1)
#define EFUSE_CTRL_RDEN              (1 << 0)


// efuse cfg bits
#define EFUSE_CFG_INT_EN             (1 << 31)
#define EFUSE_CFG_RD_ADJ             (20)
#define EFUSE_CFG_RD_ADJ_MASK        (0x3)
#define EFUSE_CFG_RD_STROBE          (16)
#define EFUSE_CFG_RD_STROBE_MASK     (0x7)
#define EFUSE_CFG_WR_ADJ             (12)
#define EFUSE_CFG_WR_ADJ_MASK        (0x3)
#define EFUSE_CFG_WR_STROBE          (0)
#define EFUSE_CFG_WR_STROBE_MASK     (0x1ff)


// efuse state bits
#define EFUSE_STA_USERKEY_PRT	     (1 << 23)
#define EFUSE_STA_NKU_PRT            (1 << 22)
#define EFUSE_STA_EXKEY_EN           (1 << 21)
#define EFUSE_STA_CUT_PRT	     (1 << 15)
#define EFUSE_STA_CHIPID_PRT         (1 << 14)
#define EFUSE_STA_SCBT_PRT	     (1 << 12)
#define EFUSE_STA_DIS_JTAG           (1 << 11)
#define EFUSE_STA_SCBT_EN            (1 << 8)
#define EFUSE_STA_WR_DONE            (1 << 1)
#define EFUSE_STA_RD_DONE            (1 << 0)

#define CHIPID_OFFSET_ADDR            (0x0)
#define RN_OFFSET_ADDR		      (0x10)
#define CUT_OFFSET_ADDR		      (0x20)
#define PRT_OFFSET_ADDR               (0x3e)
#define RK_OFFSET_ADDR               (0x40)
#define CHIPKEY_OFFSET_ADDR           (0x50)
#define USERKEY_OFFSET_ADDR           (0x60)
#define NKU_OFFSET_ADDR               (0x70)


#define CHIPID_BIT_NUM		    (128)
#define RN_BIT_NUM		    (128)
#define CUT_BIT_NUM		    (240)
#define PRT_BIT_NUM                 (16)
#define RK_BIT_NUM                 (128)
#define CHIPKEY_BIT_NUM             (128)
#define USERKEY_BIT_NUM		    (128)
#define NKU_BIT_NUM                 (128)


enum segment_id {
	CHIPID = 0,
	CUTID,
	PRT,
	CHIPKEY,
	USERKEY,
	NKU
};


struct seg_info {
	char seg_name[32];
	uint32_t seg_id;
	uint32_t offset_address;
	uint32_t bit_num;
	uint32_t prt_bit;
};

static struct seg_info seg_info_array[] = {
	[0] = {
		.seg_name = "CHIPID",
		.seg_id = CHIPID,
		.offset_address = CHIPID_OFFSET_ADDR,
		.bit_num = CHIPID_BIT_NUM,
		.prt_bit = EFUSE_STA_CHIPID_PRT,
	},
//	[1] = {
//
//		.seg_name = "RN",
//		.seg_id = RN,
//		.offset_address = RN_OFFSET_ADDR,
//		.bit_num = RN_BIT_NUM,
//		.prt_bit = EFUSE_STA_RN_PRT,
//	},
	[1] = {
		.seg_name = "CUTID",
		.seg_id = CUTID,
		.offset_address = CUT_OFFSET_ADDR,
		.bit_num = CUT_BIT_NUM,
		.prt_bit = EFUSE_STA_CUT_PRT,
	},
	[2] = {
		.seg_name = "PRT",
		.seg_id = PRT,
		.offset_address = PRT_OFFSET_ADDR,
		.bit_num = PRT_BIT_NUM,
		.prt_bit = 0xffffffff,
	},
//	[4] = {
//		.seg_name = "RK",
//		.seg_id = RK,
//		.offset_address = RK_OFFSET_ADDR,
//		.bit_num = RK_BIT_NUM,
//		.prt_bit = 0xffffffff,
//	},
	[3] = {
		.seg_name = "CHIPKEY",
		.seg_id = CHIPKEY,
		.offset_address = CHIPKEY_OFFSET_ADDR,
		.bit_num = CHIPKEY_BIT_NUM,
//		.prt_bit = EFUSE_STA_CHIPKEY_PRT,
		.prt_bit = 0xffffffff,
	},
	[4] = {
		.seg_name = "USERKEY",
		.seg_id = USERKEY,
		.offset_address = USERKEY_OFFSET_ADDR,
		.bit_num = USERKEY_BIT_NUM,
		.prt_bit = EFUSE_STA_USERKEY_PRT,
	},
	[5] = {
		.seg_name = "NKU",
		.seg_id = NKU,
		.offset_address = NKU_OFFSET_ADDR,
		.bit_num = NKU_BIT_NUM,
		.prt_bit = EFUSE_STA_NKU_PRT,
	},
};

void jz_efuse_id_read(int is_chip_id, uint8_t *buf);

#endif
