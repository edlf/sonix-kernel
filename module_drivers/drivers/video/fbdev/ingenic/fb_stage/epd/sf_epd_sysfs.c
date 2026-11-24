#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/suspend.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <asm/cacheflush.h>
#include "../dpu_reg.h"
#include "../dpu_ctrl.h"
#include "../ingenicfb.h"
#include "epd_lcd_panel.h"
#include "sf_epd.h"


static ssize_t
sf_epd_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int d = 0;
	d += sprintf(&buf[d],"0: MODE_INIT\n");
	d += sprintf(&buf[d],"1: MODE_DU\n");
	d += sprintf(&buf[d],"2: MODE_GC16\n");
	d += sprintf(&buf[d],"3: MODE_GU6\n");
	return d;
}
static ssize_t
sf_epd_mode_set(struct device *dev, struct device_attribute *attr,
						  const char *buf, size_t count)
{
	long val;
	if (kstrtol(buf, 10, &val) != 0)
		return count;
	set_epd_flush_mode(val);
	return count;
}

static ssize_t
sf_epd_show_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int d = 0;
	d += sprintf(&buf[d],"0000\n");
	return d;
}


static void draw_colorbar(unsigned char* buffer,int width,int height,int st_color)
{
	int x,y,n;
	int color_w = (width + 15) / 16;
	int color = 0;
	for(y = 0;y < height;y++) {
		x = 0;
		color = st_color;
		while(x < width) {
			for(n = 0;n < color_w && x < width;n++,x++) {
				buffer[y * width + x] = color << 4;
			}
			color += 1;
			color = color % 16;
		}
	}
}

static void draw_slashline(unsigned char* buffer,int width,int height,int color)
{
	int x,y;
	for(y = 0;y < height;y++) {
		for(x = 0;x < width;x++){
			if(x == y || x == (height - y))
				buffer[y * width + x] = color << 4;
			else
				buffer[y * width + x] = (15 - color) << 4;
		}
	}
}



static ssize_t
sf_epd_show_test_set(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ingenicfb_device *fbdev = dev_get_drvdata(dev);
	int width = fbdev->active_video_mode->xres;
	int height = fbdev->active_video_mode->yres;
	unsigned char *buffer;
	long val;

	static int bufferid = 0;
	int id = bufferid;
	buffer = (unsigned char*)fbdev->vidmem[bufferid];
	bufferid++;
	if(bufferid >= CONFIG_FB_INGENIC_NR_FRAMES)
		bufferid = 0;

	if (kstrtol(buf, 10, &val) != 0)
		return count;
	if(val < 16) {
		memset(buffer,(val << 4),width * height);
	}else if(val < 32) {
		draw_colorbar(buffer,width,height,val - 16);
	}else if(val == 32)
		draw_slashline(buffer,width,height,0);
	else if(val == 33)
		draw_slashline(buffer,width,height,15);

	epd_pan_display(fbdev,id,1);
	return count;
}

static ssize_t
sf_epd_automode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ingenicfb_device *fbdev = dev_get_drvdata(dev);
	int d = 0;
	d += sprintf(&buf[d],"automode > 0: gc16 refresh interval.\n");
	d += sprintf(&buf[d],"automode = 0: disable auto refresh mode.\n");
	d += sprintf(&buf[d],"automode = -1: gc16 refresh now.\n");
	d += sprintf(&buf[d],"automode = -2: gu16 refresh now.\n");
	return d;
}

static ssize_t
sf_epd_automode_set(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct ingenicfb_device *fbdev = dev_get_drvdata(dev);
	long val;
	if (kstrtol(buf, 10, &val) != 0)
		return count;
	sf_epd_set_automode(val);
	return count;
}

static DEVICE_ATTR(mode, 0664, sf_epd_mode_show, sf_epd_mode_set);
static DEVICE_ATTR(show_test, 0664, sf_epd_show_test_show, sf_epd_show_test_set);
static DEVICE_ATTR(automode, 0664, sf_epd_automode_show, sf_epd_automode_set);

static struct attribute *sf_epd_attrs[] = {
	&dev_attr_mode.attr,
	&dev_attr_show_test.attr,
	&dev_attr_automode.attr,
	NULL,
};

const char epd_group_name[] = "epd";
static struct attribute_group sf_epd_attr_group = {
	.name   = epd_group_name,
	.attrs  = sf_epd_attrs,
};

int sf_epd_sysfs_init(struct ingenicfb_device *fbdev)
{
	int ret;
	ret = sysfs_create_group(&fbdev->dev->kobj,
							 &sf_epd_attr_group);
	if (ret != 0) {
		dev_err(fbdev->dev,
				"Failed to create attribute group: %d: %s\n", ret,__FUNCTION__);
		return ret;
	}
	return 0;
}

int sf_epd_sysfs_deinit(struct ingenicfb_device* fbdev)
{
	sysfs_remove_group(&fbdev->dev->kobj, &sf_epd_attr_group);
	return 0;
}
