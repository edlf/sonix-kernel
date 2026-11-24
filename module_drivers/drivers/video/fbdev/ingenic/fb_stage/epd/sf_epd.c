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
#include <linux/firmware.h>
#include <libdmmu.h>
#include "../dpu_reg.h"
#include "../dpu_ctrl.h"
#include "../ingenicfb.h"
#include "epd_lcd_panel.h"
#include "sf_epd.h"
#include "waveform.h"
#include "ebc_pmic.h"

#define USING_32BIT   0
#define SMARTLCD_SIM
#if USING_32BIT
typedef unsigned int DATA_TYPE;
#else
typedef unsigned short DATA_TYPE;
#endif

enum framestate
{
	IDLE,
	FLUSH,
	FLUSHSLOW,
	FINISH,
	POWEROFF,
};

enum update_auto_mode
{
	UPDATE_MODE_GC16 = 1,
	UPDATE_MODE_GU16
};

struct auto_mode
{
	int disp_count;
	int set_disp_count;
	unsigned char *wfm_buf_gc16;
	unsigned char *wfm_buf_gu16;
	int gc16_framecount;
	int gu16_framecount;
	int update_mode;
};

struct sf_epd
{
	void*  parent;
	struct task_struct *updating_thread;
	wait_queue_head_t	wait;
	wait_queue_head_t	wait_epd_sync;
	int update_sync;
	spinlock_t	spinlock_sync;

	struct mutex   stateMtx;
	struct epd_buffer videobuffer[SF_EPD_BUFFER_COUNT];
	struct ingenicfb_sreadesc **sreadesc;
	const struct firmware *waveform;
	struct epd_buffer *top;
	struct epd_buffer *tail;

	unsigned char *wfm_lut_buf;
	int inited;
	int framecount;
	volatile int curframe;
	epd_mode_e cur_mode;
	epd_mode_e prev_mode;

	unsigned char *prev_fb;
	unsigned char *curr_fb;

	volatile enum framestate frame_state; // 中断        更新
	volatile int update_firmwate;
	volatile int fb_update;   // pan display 更新
	int temperature;
	long long update_time_ns;
	int irqcount;
	int framesize;
	struct auto_mode automode;
};

static struct sf_epd epd = {0};

static int sf_epd_updating_thread(void *data);
/**
 * @brief 设置automode
 * @details 当使用部分（GL）刷新时候，当数据没有发生变化的部分不会刷新，刷新次数多时，屏显示会有残影，
 利用自动模式再每个几帧后，发生一次全刷(GC).
 * @param[in] dispcount Description
 * dispcount > 0: 表示GL刷新超过这个值的时候要发出一个GC刷新，用来保证数据屏幕不脏
 * dispcount == -1: 表示立即使用GC刷新，同时清理刷新计数
 * dispcount == -2: 表示立即使用GU刷新，同时清理刷新计数
 * dispcount == 0:  表示关闭自动模式
 */

void sf_epd_set_automode(int dispcount)
{
	int count;
	mutex_lock(&epd.stateMtx);

	if(dispcount > 0) {
		// 自动模式开启
		count = epd.automode.set_disp_count;
		epd.automode.set_disp_count = dispcount;
		epd.automode.disp_count = 0;
		if(count == 0) {
			epd.prev_mode = -1;
			epd.update_firmwate = 1;
			wake_up(&epd.wait);
		}
	}else if(dispcount < 0 && dispcount >= -2){
		// -1: GC16立即更新 -2: GU16 立即更新
		if(dispcount == -1)
			epd.automode.update_mode = UPDATE_MODE_GC16;
		else
			epd.automode.update_mode = UPDATE_MODE_GU16;

		epd.automode.disp_count = 0;
		if(epd.automode.set_disp_count == 0) {
			epd.prev_mode = -1;
			epd.update_firmwate = 1;
		}
		wake_up(&epd.wait);
	} else if(dispcount == 0){
        // 自动模式关闭
		epd.automode.set_disp_count = 0;
	} else {
		printk("Set AutoMode failed: dispcount:%d\n",dispcount);
	}
	mutex_unlock(&epd.stateMtx);
}


/**
 * @brief 设置刷新模式,调试模式使用
 * @details 设置刷新模式，并更新waveform，关闭自动刷新
 * @param[in] mode
 *	              0: MODE_INIT
 *                1: MODE_DU
 *                2: MODE_GC16
 *                3: MODE_GU6
 * @return 0:  成功， < 0,b失败
 */

int set_epd_flush_mode(int mode)
{
	if(mode >= 0 && mode <= 8)
	{
		mutex_lock(&epd.stateMtx);
		epd.cur_mode = mode;
		epd.automode.set_disp_count = 0;
		epd.update_firmwate = 1;
		wake_up(&epd.wait);
		mutex_unlock(&epd.stateMtx);
		return 0;
	} else {
		printk("Set FlushMode %d failed.",mode);
	}
	return -1;
}

static void smartlcd_config(struct dpu_ctrl *dctrl)
{
	//配置通用寄存器，SLCD 模式，关闭DITHER
	{
		uint32_t disp_com = DC_DISP_COM_SLCD;
		reg_write(dctrl, DC_DISP_COM, disp_com);
	}
	// 打开 rdma 并配置
	{
		unsigned com_cfg = 0;

		com_cfg = reg_read(dctrl, DC_COM_CONFIG);
		/*Keep COM_CONFIG reg first bit 0 */
		com_cfg &= ~DC_OUT_SEL;
		com_cfg |= DC_OUT_SEL_RDMA;

		/* set burst length 32*/
		com_cfg &= ~DC_BURST_LEN_BDMA_MASK;
		com_cfg |= DC_BURST_LEN_BDMA_32;
		com_cfg &= ~DC_BURST_LEN_WDMA_MASK;
		com_cfg |= DC_BURST_LEN_WDMA_32;
		com_cfg &= ~DC_BURST_LEN_RDMA_MASK;
		com_cfg |= DC_BURST_LEN_RDMA_32;
		reg_write(dctrl, DC_COM_CONFIG, com_cfg);
	}
	// 配置slcd cfg
#if USING_32BIT
	{
		uint32_t slcd_cfg = 0;
		//slcd_cfg = reg_read(dctrl, DC_SLCD_CFG);
		slcd_cfg |= DC_DBI_TYPE_B_8080 | DC_DATA_FMT_888 | DC_DWIDTH_24BITS | DC_CWIDTH_24BITS | DC_FMT_EN;
		reg_write(dctrl, DC_SLCD_CFG, slcd_cfg);
	}
#else
	{
		uint32_t slcd_cfg = 0;
		//slcd_cfg = reg_read(dctrl, DC_SLCD_CFG);
		slcd_cfg |= DC_DBI_TYPE_B_8080 | DC_DATA_FMT_565 | DC_DWIDTH_16BITS | DC_CWIDTH_16BITS | DC_WR_DP;
		reg_write(dctrl, DC_SLCD_CFG, slcd_cfg);
	}
#endif
	// 配置时序
	{
		reg_write(dctrl, DC_SLCD_WR_DUTY,0);
		reg_write(dctrl, DC_SLCD_TIMING,0);
		reg_write(dctrl, DC_SLCD_SLOW_TIME,0);
	}
	// 配置屏尺寸
	{
		struct lcd_panel *panel = dctrl->panel;
		struct epd_timing *epd_timing = panel->epd_timing;
		uint32_t width = epd_timing->line_length;
		uint32_t height = epd_timing->frame_length;
		/*frm_size*/
		reg_write(dctrl, DC_SLCD_FRM_SIZE,
				  ((width << DC_SLCD_FRM_H_SIZE_LBIT) |
				   (height << DC_SLCD_FRM_V_SIZE_LBIT)));

   	}
	reg_write(dctrl,DC_SLCD_REG_IF,0);
}

static void tftlcd_config(struct dpu_ctrl *dctrl)
{
	//配置通用寄存器，LCD 模式，关闭DITHER
	{
		uint32_t disp_com = DC_DISP_COM_TFT;
		reg_write(dctrl, DC_DISP_COM, disp_com);
	}
	// 打开 rdma 并配置
	{
		unsigned com_cfg = 0;

		com_cfg = reg_read(dctrl, DC_COM_CONFIG);
		/*Keep COM_CONFIG reg first bit 0 */
		com_cfg &= ~DC_OUT_SEL;
		com_cfg |= DC_OUT_SEL_RDMA;

		/* set burst length 32*/
		com_cfg &= ~DC_BURST_LEN_BDMA_MASK;
		com_cfg |= DC_BURST_LEN_BDMA_32;
		com_cfg &= ~DC_BURST_LEN_WDMA_MASK;
		com_cfg |= DC_BURST_LEN_WDMA_32;
		com_cfg &= ~DC_BURST_LEN_RDMA_MASK;
		com_cfg |= DC_BURST_LEN_RDMA_32;
		reg_write(dctrl, DC_COM_CONFIG, com_cfg);
	}
	{
		// 翻转时钟
		/* uint32_t lcd_cgu; */
		/* lcd_cgu = *(volatile unsigned int *)(0xb0000064); */
		/* lcd_cgu |= (0x1 << 26); */
		/* *(volatile unsigned int *)(0xb0000064) = lcd_cgu; */
	}

	// 配置slcd cfg
#if USING_32BIT
	{
		uint32_t tft_cfg = DC_MODE_PARALLEL_888 | DC_PIX_CLK_INV;
		reg_write(dctrl, DC_TFT_CFG, tft_cfg);
	}
#else
	{
		uint32_t tft_cfg = DC_MODE_PARALLEL_565;
		reg_write(dctrl, DC_TFT_CFG, tft_cfg);
	}
#endif
	// 配置屏尺寸
	{
		struct lcd_panel *panel = dctrl->panel;
		struct epd_timing *epd_timing = panel->epd_timing;
		uint32_t width = epd_timing->line_length;
		uint32_t height = epd_timing->frame_length;

		reg_write(dctrl, DC_TFT_HSYNC,
				  (0 << DC_HPS_LBIT) |
				  (width << DC_HPE_LBIT));
		reg_write(dctrl, DC_TFT_VSYNC,
				  (0 << DC_VPS_LBIT) |
				  (height << DC_VPE_LBIT));
		reg_write(dctrl, DC_TFT_HDE,
				  (0 << DC_HDS_LBIT) |
				  (width << DC_HDE_LBIT));
		reg_write(dctrl, DC_TFT_VDE,
				  (0 << DC_VDS_LBIT) |
				  (height << DC_VDE_LBIT));

   	}
}

/**
 * @brief 软件仿真epd刷新初始化
 * @details 1. waveform 初始化
 *          2. 控制器初始化
 *          3. 中断初始化
 *          4. 仿真线程初始化
 * @param[inout] fbdev：ingenicfb的上下文
 * @return 0： 成功，-1 失败
 */

int sf_epd_setup(struct ingenicfb_device *fbdev)
{
	struct dpu_ctrl *dctrl = &fbdev->dctrl;
	int ret;
	int framerate = 0;
	ret = request_firmware(&epd.waveform, "waveform.img",fbdev->dev);
	if(ret < 0) {
		pr_err("load waveform.img error!\n");
		return -1;
	}
	framerate = waveform_framerate(epd.waveform->data);
#ifdef SMARTLCD_SIM
	smartlcd_config(dctrl);
#else
	tftlcd_config(dctrl);
#endif
	{
		unsigned int intc = DC_EOD_MSK | DC_SDA_MSK | DC_UOT_MSK | DC_SOC_MSK | DC_OOW_MSK | DC_EOW_MSK | DC_SOS_MSK | DC_STOP_SRD_ACK;
		reg_write(dctrl, DC_INTC, intc);
		reg_write(dctrl, DC_CLR_ST, 0x01FFFFFE);  // 清除中断
		spin_lock_init(&epd.spinlock_sync);
		init_waitqueue_head(&epd.wait);
		init_waitqueue_head(&epd.wait_epd_sync);
		mutex_init(&epd.stateMtx);
		epd.update_sync = 1;
		/* create epd updating thread(grey to lut) */

		epd.updating_thread = kthread_run(sf_epd_updating_thread,
											 fbdev, "jzepd-updating_thread");
		if (epd.updating_thread == ERR_PTR(-ENOMEM)) {
			dev_err(fbdev->dev, "Failed to create epd updating thread");
		}

		sf_epd_sysfs_init(fbdev);
	}
	return 0;
}

void epd_pan_display(struct ingenicfb_device *fbdev,int next,int sync)
{
	int res = 0;
	if(epd.inited) {
		// 等待上一次完成，一般都是完成的
		wait_event_interruptible_timeout(epd.wait_epd_sync,epd.update_sync,
										 msecs_to_jiffies(500*1000));
		mutex_lock(&epd.stateMtx);
		epd.update_sync = 0;
		epd.curr_fb = fbdev->vidmem[next];
		//printk("pan disp: %d\n",next);
		epd.fb_update = 1;
		fbdev->timestamp.rp = fbdev->timestamp.wp;
		wake_up_interruptible(&epd.wait);
		mutex_unlock(&epd.stateMtx);
		if(sync) {
			res = wait_event_interruptible_timeout(epd.wait_epd_sync,epd.update_sync,
												   msecs_to_jiffies(5*1000));
			if(res <= 0) {
				dump_lcdc_registers(&fbdev->dctrl);
				printk("irqcount: %d\n",epd.irqcount);
			}
		}
	}
}

static void sf_epd_init_ctrl_timing(const struct epd_timing *epd_timing,unsigned char *buffer)
{
	int cols = epd_timing->line_length;
	int rows = epd_timing->frame_length;
	int gates = epd_timing->timing.T.Gate_pin_size;
	int sources = epd_timing->timing.T.Source_pin_size;
	int x,y,i;
	DATA_TYPE *pD = (DATA_TYPE *)buffer;
	memset(pD,0, cols * rows * sizeof(DATA_TYPE));
	for(y = 0;y < rows;y++) {
		for(x = 0;x < cols;x++) {
		    int match;
			DATA_TYPE pixel = 0;
			/* gate timing */
			for(i = 0;i < gates;i++) {
				const struct data_iopin_timing *io = &epd_timing->timing.T.G[i]; /* P is CLK Polarity */
				match = 0;
				if ((y == io->VS) && (x >= io->HS)) {
					match = 1;
				}
				else if ((y == io->VE) && (x < io->HE)) {
					match = 1;
				}
				else if ((y > io->VS) && (y < io->VE)) {
					match = 1;
				}

				if (match) {
					if(io->P == POLARITY_HIGH)
						pixel |= 1 << io->D;
				} else {
					/* else clear bit, pin output low */
					if (io->P == POLARITY_LOW) {
						pixel |= 1 << io->D;
					}
				}
			}
            /* source timing */
			for(i = 0;i < sources;i++) {
				const struct data_iopin_timing *io = &epd_timing->timing.T.S[i]; /* P is CLK Polarity */
				match = 0;
				if ((y >= io->VS) && (y < io->VE)) {
					if ((x >= io->HS) && (x < io->HE)) {
						match = 1;
					}
				}
				if (match) {
					if(io->P == POLARITY_HIGH)
						pixel |= 1 << io->D;
				} else {
					/* else clear bit, pin output low */
					if (io->P == POLARITY_LOW) {
						pixel |= 1 << io->D;
					}
				}
			}

			*pD = pixel;
			pD++;
		}
	}
	if(0){
		DATA_TYPE *pD = (DATA_TYPE *)buffer;
		char s[100];
		int d = 0;
		printk("start---> rows: %d cols: %d\n",rows,cols);
		for(y = 0;y < 14;y++) {
			s[0] = 0;
			for(x = 0;x < 8;x++) {
				if((x % 8) == 0) {
					printk("%s\n",s);
					d = 0;
					s[0] = 0;
					d += sprintf(s,"%03d.%02d: ",y,x);
				}
				d += sprintf(&s[d],"0x%04x ",*(pD + y*cols + x));
			}
			printk("%s\n",s);
			//msleep(100);
		}
		printk("end--->\n");
		d = 0;
		for(y = 758+10;y < rows;y++) {
			s[0] = 0;
			for(x = 256 + 8;x < cols;x++) {
				if((x % 8) == 0) {
					printk("%s\n",s);
					d = 0;
					s[0] = 0;
					d += sprintf(s,"%03d.%02d: ",y,x);
				}
				d += sprintf(&s[d],"0x%04x ",*(pD + y*cols + x));
			}
			printk("%s\n",s);
			//msleep(100);
		}

	}
}

#define load_prefetch(base,offset) asm volatile ("pref 0,%1(%0)\t\n"::"r" (base),"i" (offset));

#if USING_32BIT
static void lut_videoframe_MODE_INIT(struct sf_epd *pepd,struct epd_buffer *buffer,int frame)
{
	unsigned int *lut = (unsigned int*)(pepd->wfm_lut_buf + (frame / 4));
	int bits = (frame % 4) * 2; // data is 2bits
	unsigned int g = ((*lut) >> bits) & 0x3;
	int x,y;
	// 组成32bit数据，方便数据设置
	g = g | (g << 2) | (g << 4) | (g << 6);
	for(y = 0;y < buffer->height;y++) {
		unsigned int *p_dest = (unsigned int *)(buffer->start + y * buffer->stride * sizeof(DATA_TYPE));
		unsigned long p_start = (unsigned long)p_dest;
		for(x = 0;x < buffer->width;x++) {  //每次处理8个点，用32bits来仿真
			unsigned int d = *p_dest;
			d &= 0xffffff00;  // 清楚数据位
		   	d |= g;           // 置位
			*p_dest = d;
			p_dest++;
		}
		dma_cache_wback(p_start, buffer->width * 4);
	}
}

static void lut_videoframe_MODE_NORMAL(struct sf_epd *pepd,struct epd_buffer *buffer,int frame)
{
	unsigned int *lut = (unsigned int*)(pepd->wfm_lut_buf + frame * 16 * 16 * 2 / 8);
	unsigned int *prev = (unsigned int *)pepd->prev_fb;
	unsigned int *curr = (unsigned int *)pepd->curr_fb;
	int x,y;
	long long update_time_ns;
	/* printk("lut: %p\n",lut); */
	/* printk("prev: %p\n",prev); */
	/* printk("curr: %p\n",curr); */
	update_time_ns = ktime_to_ns(ktime_get());

	for(y = 0;y < buffer->height;y++) {
		unsigned int *p_dest = (unsigned int*)(buffer->start + y * buffer->stride * sizeof(DATA_TYPE));
		unsigned long p_start = (unsigned long)p_dest;
		for(x = 0;x < buffer->width / 8;x++) {  //每次处理8个点，用32bits来仿真
			unsigned int g[8] = {0};
			unsigned int *prev_data = prev;
			unsigned int *curr_data = curr;
			if(prev_data != curr_data) {
#define LUT_VALUE(i,bs,bd) do {											\
					unsigned int p0 = (prev_data[i] >> bs) & 0xf;			\
					unsigned int c0 = (curr_data[i] >> bs) & 0xf;			\
					unsigned int lut_rows = lut[c0];					\
					unsigned int lut_cols = (lut_rows >> (p0 * 2)) & 0x3; \
					g[i] |= lut_cols << bd;								\
				}while(0)

				LUT_VALUE(0, 4,0);
				LUT_VALUE(0,12,2);
				LUT_VALUE(0,20,4);
				LUT_VALUE(0,28,6);

				LUT_VALUE(1, 4,0);
				LUT_VALUE(1,12,2);
				LUT_VALUE(1,20,4);
				LUT_VALUE(1,28,6);

				LUT_VALUE(2, 4,0);
				LUT_VALUE(2,12,2);
				LUT_VALUE(2,20,4);
				LUT_VALUE(2,28,6);

				LUT_VALUE(3, 4,0);
				LUT_VALUE(3,12,2);
				LUT_VALUE(3,20,4);
				LUT_VALUE(3,28,6);

				LUT_VALUE(4, 4,0);
				LUT_VALUE(4,12,2);
				LUT_VALUE(4,20,4);
				LUT_VALUE(4,28,6);

				LUT_VALUE(5, 4,0);
				LUT_VALUE(5,12,2);
				LUT_VALUE(5,20,4);
				LUT_VALUE(5,28,6);

				LUT_VALUE(6, 4,0);
				LUT_VALUE(6,12,2);
				LUT_VALUE(6,20,4);
				LUT_VALUE(6,28,6);

				LUT_VALUE(7, 4,0);
				LUT_VALUE(7,12,2);
				LUT_VALUE(7,20,4);
				LUT_VALUE(7,28,6);

#undef LUT_VALUE
			}
			{
				unsigned int *d = p_dest;
				d[0] &= 0xffffff00;  // 清楚数据位
				d[1] &= 0xffffff00;  // 清楚数据位
				d[2] &= 0xffffff00;  // 清楚数据位
				d[3] &= 0xffffff00;  // 清楚数据位
				d[4] &= 0xffffff00;  // 清楚数据位
				d[5] &= 0xffffff00;  // 清楚数据位
				d[6] &= 0xffffff00;  // 清楚数据位
				d[7] &= 0xffffff00;  // 清楚数据位

				load_prefetch(curr,128);
				p_dest[0] = d[0] | g[0];
				p_dest[1] = d[1] | g[1];
				p_dest[2] = d[2] | g[2];
				p_dest[3] = d[3] | g[3];
				load_prefetch(prev,128);
				p_dest[4] = d[4] | g[4];
				p_dest[5] = d[5] | g[5];
				p_dest[6] = d[6] | g[6];
				p_dest[7] = d[7] | g[7];

				p_dest += 8;
			}
			prev += 8;
			curr += 8;
		}
		dma_cache_wback(p_start, buffer->width * 4);
	}
	pepd->update_time_ns += ktime_to_ns(ktime_get()) - update_time_ns;
}
#else
static void lut_videoframe_MODE_INIT(struct sf_epd *pepd,struct epd_buffer *buffer,int frame)
{
	unsigned int *lut = (unsigned int*)(pepd->wfm_lut_buf + (frame / 4));
	int bits = (frame % 4) * 2; // data is 2bits
	unsigned int g = ((*lut) >> bits) & 0x3;
	int x,y;
	// 组成32bit数据，方便数据设置
	g = g | (g << 2) | (g << 4) | (g << 6);
	g = (g << 16) | g;
	for(y = 0;y < buffer->height;y++) {
		unsigned int *p_dest = (unsigned int *)(buffer->start + y * buffer->stride * sizeof(DATA_TYPE));
		unsigned long p_start = (unsigned long)p_dest;
		for(x = 0;x < buffer->width / 2;x++) {  //每次处理8个点，用16bits来仿真
			unsigned int d = *p_dest;
			d &= 0xff00ff00;  // 清楚数据位
			d |= g;           // 置位
			*p_dest = d;
			p_dest++;
		}
		dma_cache_wback(p_start, buffer->width * 2);
	}
}

static void lut_videoframe_MODE_NORMAL(struct sf_epd *pepd,struct epd_buffer *buffer,int frame)
{
	unsigned int *lut = (unsigned int*)(pepd->wfm_lut_buf + frame * SIZE_PER_FRAME);
	unsigned int *prev = (unsigned int *)pepd->prev_fb;
	unsigned int *curr = (unsigned int *)pepd->curr_fb;
	int x,y;
	long long update_time_ns;
	/* printk("lut: %p\n",lut); */
	/* printk("prev: %p\n",prev); */
	/* printk("curr: %p\n",curr); */
	update_time_ns = ktime_to_ns(ktime_get());
	for(y = 0;y < buffer->height;y++) {
		unsigned int *p_dest = (unsigned int*)(buffer->start + y * buffer->stride * sizeof(DATA_TYPE));
		unsigned long p_start = (unsigned long)p_dest;
		for(x = 0;x < buffer->width;x+=8) {  //每次处理8个点，用32bits来仿真
			unsigned int g[4] = {0};
			unsigned int *prev_data = prev;
			unsigned int *curr_data = curr;
			prev+=8;
			curr+=8;
			{

#define LUT_VALUE(id,bs,bd) do {										\
					unsigned int p0 = (prev_data[id] >> bs) & 0xf;		\
					unsigned int c0 = (curr_data[id] >> bs) & 0xf;		\
					unsigned int lut_rows = lut[c0];					\
					unsigned int lut_cols = (lut_rows >> (p0 * 2)) & 0x3; \
					g[id / 2] |= lut_cols << bd;						\
				}while(0)
				LUT_VALUE(0, 4,6);
				LUT_VALUE(0,12,4);
				LUT_VALUE(0,20,2);
				LUT_VALUE(0,28,0);

				LUT_VALUE(1, 4,22);
				LUT_VALUE(1,12,20);
				LUT_VALUE(1,20,18);
				LUT_VALUE(1,28,16);

				LUT_VALUE(2, 4,6);
				LUT_VALUE(2,12,4);
				LUT_VALUE(2,20,2);
				LUT_VALUE(2,28,0);

				LUT_VALUE(3, 4,22);
				LUT_VALUE(3,12,20);
				LUT_VALUE(3,20,18);
				LUT_VALUE(3,28,16);

				LUT_VALUE(4, 4,6);
				LUT_VALUE(4,12,4);
				LUT_VALUE(4,20,2);
				LUT_VALUE(4,28,0);

				LUT_VALUE(5, 4,22);
				LUT_VALUE(5,12,20);
				LUT_VALUE(5,20,18);
				LUT_VALUE(5,28,16);

				LUT_VALUE(6, 4,6);
				LUT_VALUE(6,12,4);
				LUT_VALUE(6,20,2);
				LUT_VALUE(6,28,0);

				LUT_VALUE(7, 4,22);
				LUT_VALUE(7,12,20);
				LUT_VALUE(7,20,18);
				LUT_VALUE(7,28,16);

#undef LUT_VALUE
			}
			{
				unsigned int *d = p_dest;

				d[0] &= 0xff00ff00;  // 清楚数据位
				d[1] &= 0xff00ff00;  // 清楚数据位
				d[2] &= 0xff00ff00;  // 清楚数据位
				d[3] &= 0xff00ff00;  // 清楚数据位

				load_prefetch(curr,128);
				p_dest[0] = d[0] | g[0];           // 置位
				p_dest[1] = d[1] | g[1];           // 置位
				load_prefetch(prev,128);
				p_dest[2] = d[2] | g[2];           // 置位
				p_dest[3] = d[3] | g[3];           // 置位
				p_dest += 4;
			}
		}
		dma_cache_wback(p_start, buffer->width * 2);
	}
	pepd->update_time_ns += ktime_to_ns(ktime_get()) - update_time_ns;
}
#endif

int epd_get_temperature(struct sf_epd *pepd)
{
	return 20;
}

/**
 * @brief 中断处理程序
 * @details 1. 刷新完成检测
 *          2. 每帧查表
 *          3. 当未完成终止后，发出FLUASHSLOW信号，用于线程处理。
 * @param[inout] dctrl 中断上下文
 */

void epd_dc_disp_end_irq(struct dpu_ctrl *dctrl)
{
	int curframe = epd.curframe;
	struct epd_buffer *top = epd.top;
	unsigned long pframe = reg_read(dctrl, DC_RDMA_SITE);
	struct epd_buffer *top_cur = top;
	int no_work = (reg_read(dctrl, DC_ST) & DC_WORKING) == 0;

	do{
		if(pframe >= top_cur->p && pframe < top_cur->pe)
			break;
		top_cur = top_cur->next;
	}while(top_cur != top);
	epd.irqcount++;
	if(epd.tail == top_cur) {
		if(curframe >= epd.framecount) {
			epd.curframe = 0;
			epd.frame_state = POWEROFF;
			wake_up(&epd.wait);
		} else if(curframe > 0 && curframe < epd.framecount){
			epd.frame_state = FLUSHSLOW;
			wake_up(&epd.wait);
		}
	}else if(curframe > 0 && curframe < epd.framecount && top != top_cur) {
		struct epd_buffer *cur = epd.tail;
		while(top != top_cur) {
			if(epd.cur_mode == MODE_INIT) {
				lut_videoframe_MODE_INIT(&epd,cur,curframe);
			} else {
				lut_videoframe_MODE_NORMAL(&epd,cur,curframe);
			}
			curframe++;
			epd.curframe = curframe;
			cur->desc->ChainCfg.b.chain_end = 1;
			cur->prev->desc->ChainCfg.b.chain_end = 0;
			if(curframe >= epd.framecount) {
				break;
			}
			top = top->next;
			cur = cur->next;
		}
		epd.tail = cur;
	}
	epd.top = top_cur;
}

static void update_data(struct sf_epd *pepd,struct dpu_ctrl *dctrl)
{
	struct epd_buffer *cur = pepd->tail;
	int curframe = pepd->curframe;
	do {
		if(pepd->cur_mode == MODE_INIT) {
			lut_videoframe_MODE_INIT(pepd,cur,curframe);
		} else {
			lut_videoframe_MODE_NORMAL(pepd,cur,curframe);
		}
		curframe++;
		cur->desc->ChainCfg.b.chain_end = 1;
		cur->prev->desc->ChainCfg.b.chain_end = 0;
//		printk("fill desc: %p %p %d %d\n",cur,cur->desc,cur->desc->ChainCfg.b.chain_end,curframe);
		cur = cur->next;

	} while(cur != pepd->top && curframe < pepd->framecount);
	pepd->curframe = curframe;
	pepd->tail = cur;
	if(0){
		struct epd_buffer *cur = pepd->top;
		do{
			printk("desc: %p %p %d\n",cur,cur->desc,cur->desc->ChainCfg.b.chain_end);
			cur = cur->next;
		}while(cur != pepd->top && curframe < pepd->framecount);
	}

}

static int sf_epd_updating_thread(void *data)
{
	struct ingenicfb_device *fbdev = (struct ingenicfb_device *)data;
	struct lcd_panel *panel = fbdev->panel;
	struct dpu_ctrl *dctrl = &fbdev->dctrl;
	struct epd_timing *epd_timing = panel->epd_timing;
	struct rdma_setup_info *rdma_info = &fbdev->rdma_info;
	unsigned int* vmem[SF_EPD_BUFFER_COUNT];
	unsigned int *pmem[SF_EPD_BUFFER_COUNT];
	int width = epd_timing->line_length;
	int height = epd_timing->frame_length;
	unsigned char *buffer;
	int i,curframe;
	int temperature;
	enum framestate prev_framestate;
	enum framestate framestate;

	// 初始化framebuffer
	epd.framesize = fbdev->frm_size;
	for(i = 0; i < CONFIG_FB_INGENIC_NR_FRAMES; i++) {
		fbdev->vidmem[i] = (void*)CKSEG0ADDR(fbdev->vidmem[i]);
		memset(fbdev->vidmem[i],0xff,fbdev->frm_size);
	}

	epd.prev_fb = devm_kmalloc(fbdev->dev,fbdev->frm_size, GFP_KERNEL);
	memset(epd.prev_fb,0xff,fbdev->frm_size);

	// 分配,并初始化epd buffer
	buffer = (unsigned char *)devm_kmalloc(fbdev->dev,width * height * SF_EPD_BUFFER_COUNT * sizeof(DATA_TYPE), GFP_KERNEL); //1.5M

	// 初始化epd时序
	sf_epd_init_ctrl_timing(epd_timing,buffer);


	// epd descriptor 初始化
	for(i = 0;i < SF_EPD_BUFFER_COUNT;i++) {
		// 16bit = ctrl + data
		// 8bit data 4bit ctrl data
		epd.videobuffer[i].v = &buffer[width * height * sizeof(DATA_TYPE) * i];
		epd.videobuffer[i].p = (unsigned long)virt_to_phys(epd.videobuffer[i].v);
		epd.videobuffer[i].pe = epd.videobuffer[i].p + width * height * sizeof(DATA_TYPE);
		epd.videobuffer[i].id = i;
		epd.videobuffer[i].start = (unsigned char *)&buffer[width * height * sizeof(DATA_TYPE) * i];

		epd.videobuffer[i].start += (epd_timing->line_data_start +
									 epd_timing->frame_data_start * width) * sizeof(DATA_TYPE);  //使用32bits数据仿真波形
		epd.videobuffer[i].stride = epd_timing->line_length;
		epd.videobuffer[i].width = epd_timing->line_data_length;
		epd.videobuffer[i].height = epd_timing->frame_data_length;
		epd.videobuffer[i].desc = (void*)dctrl->sreadesc[i];
		epd.videobuffer[i].phy_desc = (dma_addr_t)dctrl->sreadesc_phys[i];
		if(i < SF_EPD_BUFFER_COUNT - 1)
			epd.videobuffer[i].next = &epd.videobuffer[i+1];
		else
			epd.videobuffer[i].next = &epd.videobuffer[0];

		if(i == 0)
			epd.videobuffer[i].prev =  &epd.videobuffer[SF_EPD_BUFFER_COUNT - 1];
		else
			epd.videobuffer[i].prev =  &epd.videobuffer[i - 1];
	}
	epd.tail = epd.top = &epd.videobuffer[0];

	for(i = 1;i < SF_EPD_BUFFER_COUNT;i++) {
		memcpy(epd.videobuffer[i].v,epd.videobuffer[0].v,width * height * sizeof(DATA_TYPE));
		dma_cache_wback((unsigned long)epd.videobuffer[i].v,width * height * sizeof(DATA_TYPE));
	}

	dma_cache_wback((unsigned long)epd.videobuffer[0].v,width * height * sizeof(DATA_TYPE));

	// 配置dpu dma descriptor
	rdma_info->nframes = SF_EPD_BUFFER_COUNT;

#if USING_32BIT
	rdma_info->format = RDMA_CHAIN_CFG_FORMAT_888;
#else
	rdma_info->format = RDMA_CHAIN_CFG_FORMAT_565;
#endif

	rdma_info->color = RDMA_CHAIN_CFG_COLOR_RGB;
	rdma_info->stride = epd_timing->line_length;
	rdma_info->continuous = 1;
	rdma_info->vidmem = (unsigned char**)vmem;
	rdma_info->vidmem_phys = pmem;
	for(i = 0;i < SF_EPD_BUFFER_COUNT;i++) {
		rdma_info->vidmem_phys[i] = (unsigned int*)epd.videobuffer[i].p;
		rdma_info->vidmem[i] = (unsigned char*)epd.videobuffer[i].v;
	}
	rdma_info->continuous = 0;
	dpu_ctrl_rdma_setup(dctrl,rdma_info);
	// 把RdmaNextCfgAddr的地址串起来，后边就不处理了
	for(i = 0; i < rdma_info->nframes - 1; i++) {
		dctrl->sreadesc[i]->RdmaNextCfgAddr = dctrl->sreadesc_phys[i + 1];
		dctrl->sreadesc[i]->InterruptControl.d32 = DC_EOD_MSK;
	}
	dctrl->sreadesc[rdma_info->nframes - 1]->RdmaNextCfgAddr = dctrl->sreadesc_phys[0];
	dctrl->sreadesc[rdma_info->nframes - 1]->InterruptControl.d32 = DC_EOD_MSK;


    // 分配waveform lookup table
	epd.automode.wfm_buf_gc16 = devm_kmalloc(fbdev->dev,WFM_LUT_SIZE,GFP_KERNEL);
	epd.automode.wfm_buf_gu16 = devm_kmalloc(fbdev->dev,WFM_LUT_SIZE,GFP_KERNEL);
	epd.automode.set_disp_count = 0;
	epd.automode.disp_count = 0;
	epd.automode.gc16_framecount = 0;
	epd.automode.gu16_framecount = 0;
	epd.automode.update_mode = 0;

	epd.wfm_lut_buf = epd.automode.wfm_buf_gu16;
	temperature = epd_get_temperature(&epd);
	epd.prev_mode = epd.cur_mode = MODE_INIT;

	epd.framecount = load_lut_waveform(epd.waveform->data,
									   epd.wfm_lut_buf,WFM_LUT_SIZE,
									   temperature,
									   MODE_INIT);

	// 初始化运行状态
	epd.temperature = temperature;
	curframe = 0;
	epd.curframe = 0;
	epd.curr_fb = fbdev->vidmem[0];
	epd.fb_update = 1;
	epd.frame_state = IDLE;
	epd.update_firmwate = 0;
	prev_framestate = epd.frame_state;

	//初始化vcom电源
	panel->ops->init(panel);
	//dump_lcdc_registers(dctrl);

	while (!kthread_should_stop()) {
		int res;
		if(epd.frame_state == IDLE)
			res = wait_event_interruptible_timeout(epd.wait,
												   (epd.fb_update || (epd.frame_state != prev_framestate) || epd.update_firmwate || epd.automode.update_mode),
												   msecs_to_jiffies(10 * 1000));
		else
			wait_event_interruptible(epd.wait,epd.frame_state != prev_framestate);

		framestate = epd.frame_state;
		if(res <= 0) {
			//通知状态机进入温度检测
			epd.update_firmwate = 1;
		}
		//printk("framestate: %d %d\n",framestate,prev_framestate);
		switch(framestate)
		{
		case IDLE:
			//刷新帧数等于framecount并lcd刷新完成。
			if(epd.update_firmwate) {

				mutex_lock(&epd.stateMtx);
				temperature = epd_get_temperature(&epd);
				if(temperature / 2 != epd.temperature / 2 || epd.cur_mode != epd.prev_mode) {
					if(epd.automode.set_disp_count) {

						epd.automode.gc16_framecount = load_lut_waveform(epd.waveform->data,
																		 epd.automode.wfm_buf_gc16,WFM_LUT_SIZE,
																		 temperature,
																		 MODE_GC16);

						epd.automode.gu16_framecount = load_lut_waveform(epd.waveform->data,
																		 epd.automode.wfm_buf_gu16,WFM_LUT_SIZE,
																		 temperature,
																		 MODE_GU16);
						if(epd.cur_mode == MODE_GU16) {
							epd.framecount = epd.automode.gu16_framecount;
							epd.wfm_lut_buf = epd.automode.wfm_buf_gu16;
						} else if(epd.cur_mode == MODE_GC16) {
							epd.framecount = epd.automode.gc16_framecount;
							epd.wfm_lut_buf = epd.automode.wfm_buf_gc16;
						} else {
							printk("Error: No support mode[%d] in Auto!\n",epd.cur_mode);
						}

					} else {

						epd.framecount = load_lut_waveform(epd.waveform->data,
														   epd.wfm_lut_buf,WFM_LUT_SIZE,
														   temperature,
														   epd.cur_mode);
					}
					epd.temperature = temperature;
					epd.prev_mode = epd.cur_mode;
//					printk("cur mode: %d %d\n",epd.cur_mode,epd.framecount);
				}
				epd.update_firmwate = 0;
				mutex_unlock(&epd.stateMtx);

			}

			if(epd.automode.update_mode) {
				mutex_lock(&epd.stateMtx);

				if(epd.automode.update_mode == UPDATE_MODE_GU16) {
					epd.framecount = epd.automode.gu16_framecount;
					epd.wfm_lut_buf = epd.automode.wfm_buf_gu16;
					epd.cur_mode = MODE_GU16;
					// printk("set gu16-->\n");
				} else if(epd.automode.update_mode == UPDATE_MODE_GC16) {
					epd.framecount = epd.automode.gc16_framecount;
					epd.wfm_lut_buf = epd.automode.wfm_buf_gc16;
					epd.cur_mode = MODE_GC16;
					// printk("set gc16-->\n");
				}
				epd.automode.update_mode = 0;
				mutex_unlock(&epd.stateMtx);

			}

			if(epd.fb_update) {
				panel->ops->enable(panel);
				epd.fb_update = 0;
				//reg_write(dctrl,DC_CTRL,DC_DES_CNT_RST | DC_GEN_STP_RDMA);
				//udelay(10);
				update_data(&epd,dctrl);
				epd.frame_state = FLUSH;
				epd.update_time_ns = 0LL;
				epd.irqcount = 0;
				/* printk("---> start-%p %d\n",epd.top->desc,epd.irqcount); */
				reg_write(dctrl, DC_CLR_ST, DC_CLR_DISP_END);
				reg_write(dctrl, DC_RDMA_CHAIN_ADDR, (unsigned long)epd.top->phy_desc);
				reg_write(dctrl, DC_RDMA_CHAIN_CTRL, DC_RDMA_START);
				//printk("---> start+\n");
			}

			prev_framestate = framestate;
			break;
		case FLUSH:
			prev_framestate = framestate;
			break;
		case FLUSHSLOW:
			// 刷新帧数小于framecount,lcd也没有刷新完成。原因是填写BUFFER的数据过长导致。
			/* printk("fill buffer too slow! %d %p %p\n",epd.curframe,epd.top,epd.tail); */
			update_data(&epd,dctrl);
			epd.frame_state = FLUSH;
			if(epd.curframe >= epd.framecount) {
				epd.frame_state = POWEROFF;
			} else {
				prev_framestate = framestate;
			}
			reg_write(dctrl, DC_RDMA_CHAIN_ADDR, (unsigned long)epd.top->phy_desc);
			reg_write(dctrl, DC_RDMA_CHAIN_CTRL, DC_RDMA_START);

			break;
		case POWEROFF:
			panel->ops->disable(panel);
			mutex_lock(&epd.stateMtx);
			if(epd.cur_mode == MODE_INIT) {
				epd.cur_mode = MODE_GU16;
				epd.inited = 1;
				epd.update_firmwate = 1;
				memset(epd.prev_fb,0xff,epd.framesize);
			} else {
				memcpy(epd.prev_fb,epd.curr_fb,epd.framesize);
			}

			epd.frame_state = prev_framestate = IDLE;
			epd.update_sync = 1;

			wake_up(&epd.wait_epd_sync);

			if(epd.automode.set_disp_count > 0 && epd.automode.update_mode == 0) {
				epd.automode.disp_count++;
				if(epd.automode.disp_count == epd.automode.set_disp_count)
				{
					// 到达计数转换为GC16
					// 下一次将要转换到GU16
					epd.cur_mode = MODE_GC16;
					epd.wfm_lut_buf = epd.automode.wfm_buf_gc16;
					epd.framecount = epd.automode.gc16_framecount;
					epd.automode.disp_count = 0;

				}else if(epd.cur_mode != MODE_GU16) {
					// 设置立即刷新下一次,设置GU16模式
					// 到达计数后下一次，设置GU16模式
					epd.cur_mode = MODE_GU16;
					epd.wfm_lut_buf = epd.automode.wfm_buf_gu16;
					epd.framecount = epd.automode.gu16_framecount;
				}
			}
			mutex_unlock(&epd.stateMtx);

			dctrl->set_vsync_value(fbdev);
			break;
		case FINISH:
			break;
		}
	}
	return 0;
}
