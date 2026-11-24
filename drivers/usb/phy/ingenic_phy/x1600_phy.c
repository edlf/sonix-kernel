#include "phy-ingenic-inno.h"

#define CPM_USBPCR		(0x3C)
#define CPM_USBRDT		(0x40)
#define CPM_USBVBFIL		(0x44)
#define CPM_USBPCR1		(0x48)
#define CPM_OPCR		(0x24)

#define PHY_RX_SQU_TRI		(0x64)
#define PHY_RX_SQU_TRI_125MV    (0x8)

#define OPCR_USB_SPENDN		BIT(7)
#define USBPCR1_PORT_RST	BIT(21)
#define USBPCR1_DPPULLDOWN	BIT(29)
#define USBPCR1_DMPULLDOWN	BIT(28)
#define USBPCR_IDPULLUP_MASK	(0x3 << 28)
#define USBPCR_IDPULLUP_OTG	(0x0 << 28)
#define USBPCR_USB_MODE	BIT(31)
#define USBPCR_POR	BIT(22)

static int iphy_init_x1600(struct ingenic_usb_phy *iphy)
{
	struct inno_phy_tuning *phy_tuning = iphy->iphy_priv->phy_tuning;
	u32 phy_tuning_num = iphy->iphy_priv->phy_tuning_num;
	u32 usbpcr, usbpcr1;
	unsigned long flags;
	u32 i;
	u8 reg;

	spin_lock_irqsave(&iphy->phy_lock, flags);
	usbpcr1 = inno_usbphy_read(iphy, CPM_USBPCR1);
	usbpcr1 &= ~(USBPCR1_DPPULLDOWN | USBPCR1_DMPULLDOWN);
	inno_usbphy_write(iphy, usbpcr1, CPM_USBPCR1);

	usbpcr = inno_usbphy_read(iphy, CPM_USBPCR);
	usbpcr &= ~USBPCR_IDPULLUP_MASK;
#if (IS_ENABLED(CONFIG_USB_DWC2_HOST) || IS_ENABLED(CONFIG_USB_DWC2_DUAL_ROLE))
	usbpcr |= USBPCR_USB_MODE | USBPCR_IDPULLUP_OTG;
#elif IS_ENABLED(CONFIG_USB_DWC2_PERIPHERAL)
	usbpcr &= ~USBPCR_USB_MODE;
#endif
	inno_usbphy_write(iphy, usbpcr, CPM_USBPCR);

	inno_usbphy_update_bits(iphy, CPM_USBPCR1, USBPCR1_PORT_RST, USBPCR1_PORT_RST);
	inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, USBPCR_POR);
	udelay(11);
	inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
	udelay(11);
	inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB_SPENDN, OPCR_USB_SPENDN);
	udelay(501);
	inno_usbphy_update_bits(iphy, CPM_USBPCR1, USBPCR1_PORT_RST, 0);
	udelay(1);

	usbpcr1 = inno_usbphy_read(iphy, CPM_USBPCR1);
	usbpcr1 |= USBPCR1_DPPULLDOWN | USBPCR1_DMPULLDOWN;
	inno_usbphy_write(iphy, usbpcr1, CPM_USBPCR1);

	/* set squelch trigger point */
	reg = usb_phy_readb(iphy->base + PHY_RX_SQU_TRI);
	reg &= ~(0xf << 3);
	reg |= PHY_RX_SQU_TRI_125MV << 3;
	usb_phy_writeb(reg,iphy->base + PHY_RX_SQU_TRI);

	/* set inno usb phy tuning */
	for (i = 0; i < phy_tuning_num; i++) {
		reg = usb_phy_readb(iphy->base + phy_tuning[i].reg_addr);
		reg &= ~(phy_tuning[i].msk << phy_tuning[i].bit_off);
		reg |= (phy_tuning[i].val << phy_tuning[i].bit_off);
		usb_phy_writeb(reg, iphy->base + phy_tuning[i].reg_addr);
	}

	spin_unlock_irqrestore(&iphy->phy_lock, flags);
	return 0;
}

static int iphy_suspend_x1600(struct ingenic_usb_phy *iphy, int suspend)
{
	unsigned long flags;

	spin_lock_irqsave(&iphy->phy_lock, flags);

	if(suspend){
		/* USB phy port reset is low. */
		inno_usbphy_update_bits(iphy, CPM_OPCR, USBPCR1_PORT_RST, 0);

		/* USB phy enter suspend. */
		inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB_SPENDN, 0);

		udelay(10);
	} else {
		/* USB PHY resume. */
		inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB_SPENDN, OPCR_USB_SPENDN);
		udelay(501);

		inno_usbphy_update_bits(iphy, CPM_USBPCR1, USBPCR1_PORT_RST, USBPCR1_PORT_RST);
		udelay(2);

		inno_usbphy_update_bits(iphy, CPM_USBPCR1, USBPCR1_PORT_RST, 0);
		udelay(1);

	}

	spin_unlock_irqrestore(&iphy->phy_lock, flags);

	return 0;
}

/* for x1600 */
struct ingenic_usb_phy_priv usb_phy_x1600_priv = {
	.phyinit = iphy_init_x1600,
	.physuspend = iphy_suspend_x1600,
	.phy_tuning_num = 7,
	.phy_tuning = {
		/* set 45ohm HS ODT value */
		[0].reg_addr = 0x11c,
		[0].msk = 0x1f,
		[0].bit_off = 0,
		[0].val = 0x1c,

		/* always enable pre-emphasis */
		[1].reg_addr = 0x30,
		[1].msk = 0x7,
		[1].bit_off = 0,
		[1].val = 0x7,

		/* Tx HS pre_emphasize strength */
		[2].reg_addr = 0x40,
		[2].msk = 0x7,
		[2].bit_off = 3,
		[2].val = 0x7,

		/* set A session valid reference tuning */
		[3].reg_addr = 0x10c,
		[3].msk = 0x7,
		[3].bit_off = 0,
		[3].val = 0x5,

		/* set B session valid reference tuning */
		[4].reg_addr = 0x10c,
		[4].msk = 0x7,
		[4].bit_off = 3,
		[4].val = 0x5,

		/* set vbus valid reference tuning */
		[5].reg_addr = 0x110,
		[5].msk = 0x7,
		[5].bit_off = 0,
		[5].val = 0x5,

		/* set session end reference tuning */
		[6].reg_addr = 0x110,
		[6].msk = 0x7,
		[6].bit_off = 3,
		[6].val = 0x5,

		/* enable control of full/low speed driver at the receiver */
		/* NOTE: Set to 0 during suspend, it is used to reducepower consumption. Restore here. */
		[7].reg_addr = 0x100,
		[7].msk = 0x1,
		[7].bit_off = 6,
		[7].val = 0x1,

		/* HS eye height tuning */
		[8].reg_addr = 0x124,
		[8].msk = 0x7,
		[8].bit_off = 2,
		[8].val = 0x1,
	},
	.phy_remote_wakeup = 1,
	.phy_vbus_detect_ctl = 1,
	.phy_ls_fs_driver_ctl = 1,
};
