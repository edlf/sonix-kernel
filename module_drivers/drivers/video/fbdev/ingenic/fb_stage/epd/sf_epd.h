#ifndef __SF_EPD_H__
#define __SF_EPD_H__
#include "epd_lcd_panel.h"
#include "../ingenicfb.h"
#define	MAX_FRAME_COUNT	256
#define	SIZE_PER_FRAME	64
#define WFM_LUT_SIZE	(MAX_FRAME_COUNT * SIZE_PER_FRAME) /* 16KB */
#define SF_EPD_BUFFER_COUNT 6

struct epd_buffer
{
	unsigned long p;
	unsigned long pe;
	unsigned char *v;
	unsigned char *start;
	int width;
	int height;
	int stride;
	int id;
	struct ingenicfb_sreadesc *desc;
	dma_addr_t phy_desc;
	struct epd_buffer *next;
	struct epd_buffer *prev;
};
void epd_pan_display(struct ingenicfb_device *fbdev,int next,int sync);
void epd_dc_disp_end_irq(struct dpu_ctrl *dctrl);
int sf_epd_setup(struct ingenicfb_device *fbdev);
int set_epd_flush_mode(int mode);
void sf_epd_set_automode(int dispcount);
int sf_epd_sysfs_init(struct ingenicfb_device *fbdev);
#endif /* __SF_EPD_H__ */
