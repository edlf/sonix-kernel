#ifndef RESERVED_PART_H
#define RESERVED_PART_H
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include "sfc.h"
#include "sfc_flash.h"

enum {
	RESERVED_PART_RW = 0,
	RESERVED_PART_R_ONLY,
};

struct flash_reserved_part_ops {
	int (*_write) (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf);
	int (*_write_oob) (struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops);
	int (*_erase) (struct mtd_info *mtd, struct erase_info *instr);
};

struct reserved_partition{
	int index;
	const char *name;
	u32 start;
	u32 end;
	u32 rw_mode;
	u32 reserved;
	struct list_head list;
};

struct reserved_part_manager{
	struct list_head reserved_part_list_head;
	struct flash_reserved_part_ops reserved_part_ops;
};

struct sfc_flash;
#define RESERVED_PART_SYMBOL_RO 0x30000000
/* Be careful not to conflict with the identification of "mask_flags" in struct mtd_info*/

int reserved_part_init(struct sfc_flash *flash);
void reserved_part_exit(struct sfc_flash *flash);
int scan_reserved_part(struct sfc_flash *flash, const struct mtd_partition *parts, int nr_parts);
int add_reserved_part(struct sfc_flash *flash, int index, const struct mtd_partition *part);

#endif /*RESERVED_PART_H*/
