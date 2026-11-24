#ifndef _PHY_INGENIC_INNO_H_
#define _PHY_INGENIC_INNO_H_

#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/usb/phy.h>
#include "../../dwc2/core.h"

#define usb_phy_readb(addr)	readb((addr))
#define usb_phy_writeb(val, addr) writeb(val,(addr))

#define CC_WORK_DELAY       2000

#define PHY_REG_100		(0x100)
#define PHY_SUSPEND_LPM		(0x108)

struct ingenic_usb_phy {
	struct usb_phy phy;
	struct device *dev;
	struct clk *gate_clk;
	struct regmap *regmap;
	void __iomem *base;
	char hsotg_sw_switch[8];
	unsigned char sw_switch_mode;
	struct ingenic_usb_phy_priv *iphy_priv;
	int vbus_irq;
	int id_irq;
	bool	vbus_wakeup;
	bool	id_wakeup;

	/*otg phy*/
	spinlock_t phy_lock;
	struct gpio_desc	*gpiod_drvvbus;
	struct gpio_desc	*gpiod_id;
	struct gpio_desc	*gpiod_vbus;
	struct gpio_desc	*gpiod_cc_en;
	struct gpio_desc	*gpiod_charge_en;
	struct gpio_desc	*gpiod_headset_en;

#define CC_STATE_SCHED_BIT 0
#define CC_STATE_RUN_BIT   1
#define CC_STATE_LEVEL_BIT 2
	int cc_state;

#define USB_DETE_VBUS	    0x1
#define USB_DETE_ID	        0x2
#define USB_DETE_HEADSET	0x4
#define USB_DETE_CHARGE     0x8

	unsigned int usb_dete_state;
	unsigned int forced_dev_flags;
	struct delayed_work	work;
	enum usb_device_speed	roothub_port_speed;
	int retry_count;
};

struct inno_phy_tuning {
	u8 val;
	u8 msk;
	u8 bit_off;
	u32 reg_addr;
};

struct ingenic_usb_phy_priv {
	bool phy_remote_wakeup;
	bool phy_vbus_detect_ctl;
	bool phy_ls_fs_driver_ctl;
	int (*phyinit)(struct ingenic_usb_phy*);
	int (*physuspend)(struct ingenic_usb_phy*,int);
	u32 phy_tuning_num;
	struct inno_phy_tuning phy_tuning[];
};

static inline int inno_usbphy_read(struct ingenic_usb_phy *iphy, u32 reg)
{
	u32 val = 0;
	regmap_read(iphy->regmap, reg, &val);
	return val;
}

static inline int inno_usbphy_write(struct ingenic_usb_phy *iphy, u32 val, u32 reg)
{
	return regmap_write(iphy->regmap, reg, val);
}

static inline int inno_usbphy_update_bits(struct ingenic_usb_phy *iphy, u32 reg, u32 mask, u32 bits)
{
	return regmap_update_bits_check(iphy->regmap, reg, mask, bits, NULL);
}

static inline int phy_ioread(struct usb_phy *x, u32 reg)
{
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);
	u32 val = 0;
	regmap_read(iphy->regmap, reg, &val);
	return val;
}

static inline int phy_iowrite(struct usb_phy *x, u32 val, u32 reg)
{
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);
	return regmap_write(iphy->regmap, reg, val);
}

extern struct ingenic_usb_phy_priv usb_phy_common_priv;
extern struct ingenic_usb_phy_priv usb_phy_x1600_priv;
extern struct ingenic_usb_phy_priv usb_phy_x2500_priv;
extern struct ingenic_usb_phy_priv usb_phy_x2580_priv;
extern struct ingenic_usb_phy_priv usb0_phy_x2600_priv;
extern struct ingenic_usb_phy_priv usb1_phy_x2600_priv;
#endif /* _PHY_INGENIC_INNO_H_ */
