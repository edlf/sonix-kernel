/*
 * Reserved partition manager for flash.
 *
 * Copyright (c) 2015 Ingenic
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/list.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "reserved_part.h"
#include "ingenic_sfc_drv.h"
#include "spinor.h"
#include "spinand.h"

static struct reserved_partition *offset_2_reserved_part(struct sfc_flash *flash, uint32_t addr, uint32_t len);
static bool check_partition_is_writable(struct sfc_flash *flash, uint32_t to, uint32_t len);
static int mtd_reserved_part_write_oob(struct mtd_info *master_mtd, loff_t to, struct mtd_oob_ops *ops);
static int mtd_reserved_part_write(struct mtd_info *master_mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf);
static int mtd_reserved_part_erase(struct mtd_info *master_mtd, struct erase_info *instr);

/*
 * Reserved partition informations
 *
 * "show" format of reserved partition information attributes:
 * [index]:[start]-[end]:[R/W]
 * For example:
 *      0:0x10000-0x20000:R
 *      1:0x20000-0x30000:RW
 *
 * "store" format for reserved partition information attributes:
 * [index]:[RW]
 * For example:
 *      1:R
 * */

static ssize_t reserved_part_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sfc_flash *flash = platform_get_drvdata(pdev);
	struct reserved_partition *reserved_part;
	strcpy(buf, "\t[index]:[name]:[start]-[end]:[R/W]\n");
	char tmp[30];
	list_for_each_entry(reserved_part, &flash->reserved_part_mgr->reserved_part_list_head, list)
	{
		snprintf(tmp, 30, "\t  %d:%s:0x%x-0x%x:",
				reserved_part->index,reserved_part->name,
				reserved_part->start,reserved_part->end);
		if(reserved_part->rw_mode == RESERVED_PART_R_ONLY)
			strcat(tmp, "R\n");
		else
			strcat(tmp, "RW\n");
		strcat(buf, tmp);
	}
	return strlen(buf);
}

static ssize_t reserved_part_info_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	u8 index,rw_mode;
	char *str = buf, *tmp;
	char help_print[] ="\n\tInput format:\n\t  [index]:[RW]\n\tFor example:\n\t  1:R\n";

	if (!n || n < 3)
		goto help;

	if(str && str != '\0')
		tmp = strsep(&str, ":");
	else
		goto help;

	int ret = kstrtou8(tmp, 0, &index);
	if(ret){
		printk("kstrtou8 error:%d\n",ret);
		return -EINVAL;
	}
	tmp = strsep(&str, "\n");
	if(str && str != '\0'){
		if(strcmp(tmp, "RW") == 0 || strcmp(tmp, "rw") == 0)
			rw_mode = RESERVED_PART_RW;
		else if(strcmp(tmp, "R") == 0 || strcmp(tmp, "r") == 0)
			rw_mode = RESERVED_PART_R_ONLY;
		else
			goto help;
	}
	else
		goto help;

	struct platform_device *pdev = to_platform_device(dev);
	struct sfc_flash *flash = platform_get_drvdata(pdev);
	struct reserved_partition *reserved_part;
	list_for_each_entry(reserved_part, &flash->reserved_part_mgr->reserved_part_list_head, list)
	{
		if(reserved_part->index == index)
		{
			reserved_part->rw_mode = rw_mode;
			printk("set %d:%s \n", index, tmp);
		}
	}
	return n;
help:
	printk("%s", help_print);
	return -EINVAL;
}

static DEVICE_ATTR(part_info, S_IRUGO|S_IWUSR, reserved_part_info_show, reserved_part_info_store);

static struct attribute *flash_reserved_part_attrs[] = {
	&dev_attr_part_info.attr,
	NULL,
};

static struct attribute_group flash_reserved_part_attr_group = {
	.name   = "flash_reserved_partition",
	.attrs  = flash_reserved_part_attrs,
};

int scan_reserved_part(struct sfc_flash *flash, const struct mtd_partition *parts, int nr_parts)
{
	u8 i, cnt = 1;
	for(i = 0; i < nr_parts; i++)
	{
		if(parts[i].mask_flags & RESERVED_PART_SYMBOL_RO)
		{
			dev_info(flash->dev, "NOTICE: Found reserved partition: %s \n", parts[i].name);
			if(add_reserved_part(flash, cnt, &parts[i]) != 0)
				return -ENOMEM;
			cnt++;
		}
	}
	return 0;
}
int add_reserved_part(struct sfc_flash *flash, int index, const struct mtd_partition *part)
{
	struct reserved_partition *reserved_part = kzalloc(sizeof(struct reserved_partition), GFP_KERNEL);
	if(IS_ERR_OR_NULL(reserved_part)){
		printk("ERROR: %s malloc failed!\n",__func__);
		return -1;
	}
	reserved_part->index = index;
	reserved_part->start = part->offset;
	reserved_part->end = (part->size == MTDPART_SIZ_FULL || part->size == MTDPART_OFS_APPEND)? \
			     (flash->mtd.size):(part->offset + part->size);
	reserved_part->rw_mode = RESERVED_PART_R_ONLY;
	reserved_part->name = part->name;

	printk("add reserved partition: %s %x - %x \n",reserved_part->name, reserved_part->start, reserved_part->end);
	list_add_tail(&reserved_part->list, &flash->reserved_part_mgr->reserved_part_list_head);
	return 0;
}

int reserved_part_init(struct sfc_flash *flash)
{
	flash->reserved_part_mgr = kzalloc(sizeof(struct reserved_part_manager), GFP_KERNEL);
	if(IS_ERR_OR_NULL(flash->reserved_part_mgr)){
		printk("ERROR: %s malloc failed!\n",__func__);
		return -1;
	}
	INIT_LIST_HEAD(&flash->reserved_part_mgr->reserved_part_list_head);
	int ret = sysfs_create_group(&flash->dev->kobj, &flash_reserved_part_attr_group);
	if (ret){
		dev_err(flash->dev, "device create sysfs group failed\n");
		goto free;
		return ret;
	}
	if(flash->mtd._erase){
		flash->reserved_part_mgr->reserved_part_ops._erase = flash->mtd._erase;
		flash->mtd._erase = mtd_reserved_part_erase;
	}
	if(flash->mtd._write){
		flash->reserved_part_mgr->reserved_part_ops._write = flash->mtd._write;
		flash->mtd._write = mtd_reserved_part_write;
	}
	if(flash->mtd._write_oob){
		flash->reserved_part_mgr->reserved_part_ops._write_oob = flash->mtd._write_oob;
		flash->mtd._write_oob = mtd_reserved_part_write_oob;
	}
	return 0;
free:
	kfree(flash->reserved_part_mgr);
	return ret;
}

void reserved_part_exit(struct sfc_flash *flash)
{
	if(!IS_ERR_OR_NULL(flash->reserved_part_mgr))
		kfree(flash->reserved_part_mgr);
}

static struct reserved_partition *offset_2_reserved_part(struct sfc_flash *flash, uint32_t addr, uint32_t len)
{
	struct reserved_partition *reserved_part;
	list_for_each_entry(reserved_part, &flash->reserved_part_mgr->reserved_part_list_head, list)
	{
		/*
		 * Flash operate has addr and len
		 * Reserved partition include offset and size
		 * The following situations are considered as reserved partitions for operation
		 *
		 * ---|---------|---------|----------|---
		 *   addr     offset offset+size addr+len
		 *
		 * ---|---------|---------|----------|---
		 *  offset     addr  offset+size addr+len
		 *
		 * ---|---------|---------|----------|---
		 *   addr     offset   addr+len offset+size
		 *
		 * ---|---------|---------|----------|---
		 *  offset     addr    addr+len offset+size
		 * */
		if((addr <= reserved_part->start && addr+len >= reserved_part->end) \
				||(addr >= reserved_part->start && addr < reserved_part->end) \
				||(addr+len > reserved_part->start && addr+len <= reserved_part->end))
			return reserved_part;
	}
	return NULL;
}
static bool check_partition_is_writable(struct sfc_flash *flash, uint32_t to, uint32_t len)
{
	struct reserved_partition *reserved_part = offset_2_reserved_part(flash, to, len);
	if(reserved_part && reserved_part->rw_mode == RESERVED_PART_R_ONLY)
	{
		printk("ERROR: Unable to operate reserved partition: %s\n",reserved_part->name);
		return false;
	}
	return true;
}
static int mtd_reserved_part_write_oob(struct mtd_info *master_mtd, loff_t to,
		struct mtd_oob_ops *ops)
{
	struct sfc_flash *flash = container_of(master_mtd, struct sfc_flash, mtd);
	if(check_partition_is_writable(flash, (uint32_t)to, (uint32_t)ops->len))
		return flash->reserved_part_mgr->reserved_part_ops._write_oob(master_mtd, to, ops);
	return -EROFS;
}
static int mtd_reserved_part_write(struct mtd_info *master_mtd, loff_t to,
		size_t len, size_t *retlen, const u_char *buf)
{
	struct sfc_flash *flash = container_of(master_mtd, struct sfc_flash, mtd);
	if(check_partition_is_writable(flash, (uint32_t)to, (uint32_t)len))
		return flash->reserved_part_mgr->reserved_part_ops._write(master_mtd, to, len, retlen, buf);
	return -EROFS;
}
static int mtd_reserved_part_erase(struct mtd_info *master_mtd, struct erase_info *instr)
{
	struct sfc_flash *flash = container_of(master_mtd, struct sfc_flash, mtd);
	if(check_partition_is_writable(flash, (uint32_t)instr->addr, (uint32_t)instr->len))
		return flash->reserved_part_mgr->reserved_part_ops._erase(master_mtd, instr);
	return -EROFS;
}

