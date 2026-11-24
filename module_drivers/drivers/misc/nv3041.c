/*
 * module_drivers/drivers/misc/nv3041.c
 * active panel using spi interface to do init
 */

#if 0

&pinctrl {
    spi0_pb_tx: spi0-pb-tx {
		ingenic,pinmux = <&gpb 2 3>,<&gpb 0 0>;
		ingenic,pinmux-funcsel = <PINCTL_FUNCTION0>;
	};
};

 &spi0 {
   status = "okay";
   pinctrl-names = "default";
   pinctrl-0 = <&spi0_pb_tx>;
   spi-max-frequency = <33000000>;
   num-cs = <1>;
   cs-gpios = <0>;
   /*cs-gpios = <&gpd 5 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>; */
   /* cs-gpios = <0>, <0>; */
   /* cs-gpios = <&gpb 17 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>, <&gpb 16 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>; */
   ingenic,chnl = <0>;
   ingenic,allow_cs_same = <1>;
   ingenic,bus_num = <0>;
   ingenic,has_dma_support = <1>;
   ingenic,spi-src-clk = <1>;/*0.ext; 1.ssi*/

    /* Add SPI interface device */
   nv3041_spirgb@0 {
	   compatible = "ingenic,nv3041_spirgb";
	   status = "okay";
	   spi-max-frequency = <9000000>;
	   reg = <0>;
	   interrupt-parent = <&gpb>; /* INT pin */
	   interrupts = <31>;
	   te-gpio = <&gpb 31 IRQ_TYPE_EDGE_RISING INGENIC_GPIO_NOBIAS>;
	   irq-flags = <1>; /* 1 rising, 2 falling */
	   ingenic,vdd-en-gpio = <&gpc 11 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
	   ingenic,backlight-en-gpio = <&gpc 12 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
	   ingenic,rst-gpio = <&gpb 27 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
	   ingenic,lcd-dcx-gpio = <&gpb 1 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
	   ingenic,cs-gpio = <&gpb 0 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
   };
};

&i2c0 {
	clock-frequency = <100000>;
	pinctrl-names = "default";
	status = "okay";
	pinctrl-0 = <&i2c0_pc>;

	tsc2007@0x4b{
		compatible = "ti,tsc2007"; /* do not modify */
		reg = <0x4b>; /* do not modify */
		interrupt-parent = <&gpb>; /* INT pin */
		interrupts = <20>;
		tsc2007-irq-gpio = <&gpb 20 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>; /* INT pin */
		ti,poll-period = <20>;
		ti,x-plate-ohms = <700>;
		ti,x-max = <480>;
		ti,y-max = <272>;
		ti,x-mirror = <0>;
		ti,y-mirror = <1>;
		irq-flags = <2>; /* 1 rising, 2 falling */
	};
};

#endif


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>

#define DRV_NAME	"nv3041_lcd"
#define PANELTYPE_ACTIVE  0
struct gpio_desc {
	short gpio;
};
#define HOR_RES 480
#define VER_RES 272
struct nv3041_plat_data {
	struct spi_device *spi;
	struct miscdevice mdev;
	/* common lcd framework */
	struct lcd_device *lcd;
	int  irq;
	unsigned long    irq_flags;

	struct gpio_desc *vdd_en;
	struct gpio_desc *backlight_en;
	struct gpio_desc *rst;
	struct gpio_desc *dcx;
	struct gpio_desc *te;
	struct gpio_desc *cs;
	struct completion complete;
	int do_flush;
	int frame_top;
	int frame_right;

	int frame_left;
	int frame_bottom;
	u16 frame_buf[HOR_RES * VER_RES];
	int cur_frame_size;
};



struct lcd_panel {
        /* use node to register to list */
        const char *name;
        struct device *dev;
        int panel_type;
        void *plat_data;
};
#ifndef FB_BLANK_NORMAL
#define FB_BLANK_NORMAL		1
#endif
#ifndef FB_BLANK_POWERDOWN
#define FB_BLANK_POWERDOWN	4
#endif


static unsigned char config_data[] =
{
    0xff,0xa5,
    0xE7,0x10,
    0x35,0x00,
    0x36,0xC0,
    0x3A,0x01,
    0x40,0x01,
    0x41,0x03,
    0x44,0x15,
    0x45,0x15,
    0x7d,0x03,
    0xc1,0xbb,
    0xc2,0x05,
    0xc3,0x10,
    0xc6,0x3e,
    0xc7,0x25,
    0xc8,0x21,
    0x7a,0x51,
    0x6f,0x49,
    0x78,0x57,
    0xc9,0x00,
    0x67,0x11,
    0x51,0x0a,
    0x52,0x7D,
    0x53,0x0a,
    0x54,0x7D,
    0x46,0x0a,
    0x47,0x2a,
    0x48,0x0a,
    0x49,0x1a,
    0x44,0x15,
    0x45,0x15,
    //0x73,0x08,
    //0x74,0x10,
    0x56,0x43,
    0x57,0x42,
    0x58,0x3c,
    0x59,0x64,
    0x5a,0x41,
    0x5b,0x3C,
    0x5c,0x02,
    0x5d,0x3c,
    0x5e,0x1f,
    0x60,0x80,
    0x61,0x3f,
    0x62,0x21,
    0x63,0x07,
    0x64,0xe0,
    0x65,0x02,
    0xca,0x20,
    0xcb,0x52,
    0xcc,0x10,
    0xcD,0x42,
    0xD0,0x20,
    0xD1,0x52,
    0xD2,0x10,
    0xD3,0x42,
    0xD4,0x0a,
    0xD5,0x32,
    0xE5,0x06,
    0x80,0x00,
    0xA0,0x00,
    0x81,0x06,
    0xA1,0x08,
    0x82,0x03,
    0xA2,0x03,
    0x86,0x14,
    0xA6,0x14,
    0x87,0x2C,
    0xA7,0x26,
    0x83,0x37,
    0xA3,0x37,
    0x84,0x35,
    0xA4,0x35,
    0x85,0x3f,
    0xA5,0x3f,
    0x88,0x0A,
    0xA8,0x0A,
    0x89,0x13,
    0xA9,0x12,
    0x8a,0x18,
    0xAa,0x19,
    0x8b,0x0a,
    0xAb,0x0a,
    0x8c,0x17,
    0xAc,0x0B,
    0x8d,0x1A,
    0xAd,0x09,
    0x8e,0x1A,
    0xAe,0x08,
    0x8f,0x1F,
    0xAf,0x00,
    0x90,0x08,
    0xB0,0x00,
    0x91,0x10,
    0xB1,0x06,
    0x92,0x19,
    0xB2,0x15,
    0x6E,0x14,
    0xE5,0x06,
    0xff,0x00,
};


#define DCX(plat,v) gpiod_direction_output(plat->dcx, v);

#define CS(plat, v)
// gpiod_direction_output(plat->cs, v);

#define RESET(plat,v) gpiod_direction_output(plat->rst, v);

/* 写command时DCX切换线为0；写data时DCX切换线为1 */
static int SPI_WriteCommand(struct nv3041_plat_data *plat,int cmd,u8* d,int size)
{
	int ret;
	DCX(plat,0);
	ret = spi_write(plat->spi,&cmd,1);
	DCX(plat,1);
	if(size > 0 && ret == 0) {
		ret = spi_write(plat->spi,d,size);
	}
	//printk("ret=%d\n",ret);
	return ret;
}

static int SPI_WriteDatas(struct nv3041_plat_data *plat,unsigned char *data,int len)
{
	return spi_write(plat->spi,data,len);
}

/*设置局刷命令0x2a控制col，0x2b控制row，0x2c刷新*/
static int SPI_PartWrite(struct nv3041_plat_data *plat,unsigned int Xstart,unsigned int Xend,unsigned int Ystart,unsigned int Yend) //局部刷新
{
	u8 col[4],row[4];
	col[0] = Xstart >> 8;
	col[1] = Xstart & 0xff;
	col[2] = Xend >> 8;
	col[3] = Xend & 0xff;
	row[0] = Ystart >> 8;
	row[1] = Ystart & 0xff;
	row[2] = Yend >> 8;
	row[3] = Yend & 0xff;

	SPI_WriteCommand(plat,0x2a,col,4);
	SPI_WriteCommand(plat,0x2b,row,4);
	SPI_WriteCommand(plat,0x2c,NULL,-1);
	return 0;
}

#if 0
/*刷彩条测试*/
static int color_write(struct nv3041_plat_data *plat,unsigned int Xstart,unsigned int Xend,unsigned int Ystart,unsigned int Yend)
{

		SPI_PartWrite(plat,Xstart,Xend-1,Ystart,Yend-1);
		udelay(1200000); // Delay 120ms // This delay time is necessary
		u16 *framebuf = plat->frame_buf;
		u16 *framedata =NULL; //previous frame
		int i,j;
		for(i = 0;i < Yend;i++) {
			for(j = 0;j < Xend;j++) {

				framedata = framebuf[i * Xend + j] = (0x1f << 5);
			}
		}
		SPI_WriteDatas(plat,framebuf,Xend*Yend*2);
		return 0;

}
#endif

static int Initial_IC(struct nv3041_plat_data *plat)
{
    int i;

    RESET(plat,0);
    msleep(10);
	RESET(plat,1);
    msleep(100);
	RESET(plat,0);
    msleep(10);
	for(i = 0;i < sizeof(config_data);i+=2) {
        int result = SPI_WriteCommand(plat,config_data[i],&config_data[i + 1],1);
        //printk("result;%d\n",result);
        if(result < 0) {
			pr_err("Command send failed! Index: %d\n",i);
			return -1;
		}
	}

	SPI_WriteCommand(plat,0x11,NULL,-1);
    msleep(120);// Delay 120ms
	SPI_WriteCommand(plat,0x29,NULL,-1);
    msleep(120); // Delay 120ms
	SPI_WriteCommand(plat,0x2C,NULL,-1);
#if 0
	memset(plat->frame_buf,255,sizeof(plat->frame_buf));
	SPI_WriteDatas(plat,plat->frame_buf,272 * 480 * 2);
#endif
	return 0;
}

static int panel_set_power(struct nv3041_plat_data *plat, int power)
{
	int ret = 0;
	if(power)
	{
		CS(plat,0);
		gpiod_direction_output(plat->vdd_en, 0);
        msleep(1);
		gpiod_direction_output(plat->vdd_en, 1);
		gpiod_direction_output(plat->rst, 1);
		CS(plat,1);
		ret = Initial_IC(plat);
		CS(plat,0);
		if(ret != 0)
			power = 0;
	}
	if(power == 0)
		gpiod_direction_output(plat->vdd_en, 0);
	return ret;
};


static struct lcd_panel panel_nv3041 = {
	.name = "nv3041",
	.panel_type = PANELTYPE_ACTIVE,

};

/*用做局刷时连接用户层的矩形坐标点*/
struct rect_frame
{
	int left,top;
	int right,bottom;
	u16 buffer[0];
};

/* 可以全屏刷新，给用户层提供接口，对应LVGL demo中 SmartHome/src/nv3041_lcd_drv.c */
static ssize_t lcd_write(struct file *file, const char *buf, size_t size, loff_t *pos)
{
	struct miscdevice *mdev = file->private_data;
	struct nv3041_plat_data *plat = container_of(mdev, struct nv3041_plat_data, mdev);
	int ret = 0;
	int w,y,h;
	struct rect_frame *frame = (struct rect_frame*)buf;
	u16 *framebuf = frame->buffer;


	w = frame->right - frame->left;
	h = frame->bottom - frame->top;
	if(w * h <= 0 || w >= HOR_RES || h >= VER_RES)
		printk("ERROR size no enough ,size:%d ",size);
	else {
		u16 *dst = plat->frame_buf;
        int r;
		w += 1;
		h += 1;
        r = wait_for_completion_interruptible_timeout(&plat->complete,msecs_to_jiffies(600));
        if(r < 0) {
            return size;
        } else if(r == 0) {
            panel_set_power(plat,0);
            panel_set_power(plat,1);
        };

        plat->frame_top = frame->top;
        plat->frame_left = frame->left;
        plat->frame_right = frame->right;
        plat->frame_bottom = frame->bottom;

        //printk("write rect: %d %d %d %d\n",plat->frame_top,plat->frame_left,plat->frame_bottom,plat->frame_right);
        for(y = plat->frame_top;y <= plat->frame_bottom;y++) {
			ret = copy_from_user(dst,&framebuf[y * HOR_RES + plat->frame_left],w * sizeof(u16));
			if (ret) {
				dev_err(&plat->spi->dev, "copy_from_user failed.");
				return -EPERM;
			}
			dst += w;
		}
		plat->cur_frame_size = (w * h * sizeof(u16) + 63) / 64 * 64;
	}
	return size;
}

static struct file_operations lcd_misc_fops = {
	.write		= lcd_write,
};

/*在te线程里做局刷*/
static irqreturn_t te_thread_irq(int irq, void *handle)
{
	struct nv3041_plat_data *plat = handle;
	u16 *framebuf = plat->frame_buf;
    if(plat->cur_frame_size > 0) {
		//printk("te->rect: %d %d %d %d\n",plat->frame_top,plat->frame_left,plat->frame_bottom,plat->frame_right);
		CS(plat,1);
        SPI_PartWrite(plat,plat->frame_left,plat->frame_right,plat->frame_top,plat->frame_bottom);
		SPI_WriteDatas(plat,(u8*)framebuf,plat->cur_frame_size);
		CS(plat,0);
        plat->cur_frame_size = 0;
        complete(&plat->complete);

	}
	return IRQ_HANDLED;
}

static int of_lcd_panel_parse(struct nv3041_plat_data *lcd_panel)
{
	struct device *dev = &lcd_panel->spi->dev;
	struct device_node *np = dev->of_node;
	//struct nv3041_plat_data *plat_data;
	int ret = 0;
	printk("%s %s %d np:%p\n",__FILE__,__func__,__LINE__,np);
	printk("%s %s %d lcd_panel:%p\n",__FILE__,__func__,__LINE__,lcd_panel);
#define PANEL_GPIO_GET(gpiodesc,name) do{								\
		if(ret == 0) {													\
				gpiodesc = devm_gpiod_get_optional(dev,name, GPIOD_ASIS); \
				if(IS_ERR_OR_NULL(gpiodesc))							\
				{														\
					gpiodesc = NULL;									\
					ret = -1;											\
					dev_err(dev,"Failed to request %s!\n",name);		\
				}														\
			}															\
			}while(0)
	PANEL_GPIO_GET(lcd_panel->vdd_en,"ingenic,vdd-en");
	PANEL_GPIO_GET(lcd_panel->backlight_en,"ingenic,backlight-en");
	PANEL_GPIO_GET(lcd_panel->rst,"ingenic,rst");
	PANEL_GPIO_GET(lcd_panel->dcx,"ingenic,lcd-dcx");
    //	PANEL_GPIO_GET(lcd_panel->cs,"ingenic,cs");
	lcd_panel->te = devm_gpiod_get_optional(&lcd_panel->spi->dev,"te", GPIOD_IN);
	if(IS_ERR_OR_NULL(lcd_panel->te))
	{
		lcd_panel->te = NULL;
		ret =-1;
		dev_err(dev, "No valid irq gpio");
	}

	return ret;
}


static int nv3041_probe(struct spi_device *spi)
{
	int ret;
	struct nv3041_plat_data *plat_data;
	printk("nv3041_probe begin!!!!\n");

	/* setup spi related info */
	spi->bits_per_word = 8;
	spi->mode = SPI_3WIRE;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi setup failed %d", ret);
		return ret;
	}

	plat_data = devm_kzalloc(&spi->dev,sizeof(*plat_data), GFP_KERNEL);
	if (plat_data == NULL)
		return -ENOMEM;

	plat_data->spi = spi;
	panel_nv3041.plat_data = plat_data;
	panel_nv3041.dev = &spi->dev;

	plat_data->mdev.minor = MISC_DYNAMIC_MINOR;
	plat_data->mdev.name = DRV_NAME;
	plat_data->mdev.fops = &lcd_misc_fops;

	ret = of_lcd_panel_parse(plat_data);

	if(ret == 0 && plat_data->te){
		plat_data->irq = gpiod_to_irq(plat_data->te);
		ret = devm_request_threaded_irq(&spi->dev, plat_data->irq,
										NULL,te_thread_irq,
										IRQF_ONESHOT | IRQF_TRIGGER_RISING,
										DRV_NAME, plat_data);
		if (ret) {
			dev_err(&spi->dev, "Failed to request irq %d: %d\n",plat_data->irq, ret);
			return ret;
		}
	}

	if(ret == 0) {
		ret = panel_set_power(plat_data,1);
		ret = ret || misc_register(&plat_data->mdev);
		if (ret < 0) {
			dev_err(&spi->dev,"lcd misc_register failed\n");
			ret = -EINVAL;
		}
	}

	init_completion(&plat_data->complete);
    complete(&plat_data->complete);
	plat_data->do_flush = 0;
	printk("nv3041_probe end!!!! %s\n",ret == 0 ? "OK" : "ERROR");
	return ret;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,nv3041_spirgb", },
	{},
};

static struct spi_driver panel_nv3041_driver = {
	.probe		= nv3041_probe,
	.driver		= {
		.name	= DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table	= panel_of_match,
	},
};
module_spi_driver(panel_nv3041_driver);


MODULE_DESCRIPTION("Panel driver for nv3041");
MODULE_LICENSE("GPL");
