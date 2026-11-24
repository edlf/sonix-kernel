#ifndef __SFC_FLASH_H
#define __SFC_FLASH_H
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include "sfc.h"

#ifdef CONFIG_FLASH_RESERVED_PART
#include "reserved_part.h"
#endif

struct sfc_flash {

	struct sfc *sfc;
	void *flash_info;
	struct device* dev;
	struct mtd_info mtd;
	struct mtd_part_parser_data ppdata;
	struct mutex	lock;
	unsigned long sfc_init_frequency;
	unsigned long sfc_max_frequency;
	int param_offset;	//param_offset.
	struct ingenic_sfc_info *pdata_params;
	void (*create_cdt_table)(struct sfc *,void *, uint32_t);
	struct attribute_group *attr_group;
#ifdef CONFIG_FLASH_RESERVED_PART
	struct reserved_part_manager *reserved_part_mgr;
#endif
};

struct ingenic_sfc_info {

	uint8_t use_ofpart_info;	/* use device tree partiton flag */
	int param_offset;		/* param_offset */
	struct device_node *part_node;  /* flash node */
};

#endif
