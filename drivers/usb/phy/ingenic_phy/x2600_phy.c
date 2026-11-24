#include "phy-ingenic-inno.h"

#define CPM_USBPCR	(0x3c)
#define CPM_USBRDT	(0x40)
#define CPM_USBVBFIL	(0x44)
#define CPM_USBPCR1	(0x48)

#define CPM_USB1PCR	(0x4c)
#define CPM_USB1RDT	(0x50)
#define CPM_USB1VBFIL	(0x58)
#define CPM_USB1PCR1	(0xE8)
#define CPM_OPCR	(0x24)

#define OPCR_USB1_SPENDN	BIT(6)
#define OPCR_USB_SPENDN	BIT(7)

#define USBPCR_UTMI_RST	BIT(20)
#define USBPCR_POR	BIT(22)
#define USBPCR1_DPPULLDOWN	BIT(29)
#define USBPCR1_DMPULLDOWN	BIT(28)
#define USBPCR_IDPULLUP_MASK	(0x3 << 28)
#define USBPCR_IDPULLUP_OTG	(0x0 << 28)
#define USBPCR_USB_MODE		BIT(31)

static int iphy_port0_init_x2600(struct ingenic_usb_phy *iphy)
{
	struct inno_phy_tuning *phy_tuning = iphy->iphy_priv->phy_tuning;
	u32 phy_tuning_num = iphy->iphy_priv->phy_tuning_num;
	u32 usbpcr, usbpcr1;
	unsigned long flags;
	u32 i;
	u8 reg;

	spin_lock_irqsave(&iphy->phy_lock, flags);

	usbpcr = inno_usbphy_read(iphy, CPM_USBPCR);
	usbpcr &= ~USBPCR_IDPULLUP_MASK;
#if (IS_ENABLED(CONFIG_USB_DWC2_HOST) || IS_ENABLED(CONFIG_USB_DWC2_DUAL_ROLE))
	usbpcr |= USBPCR_USB_MODE;
#elif IS_ENABLED(CONFIG_USB_DWC2_PERIPHERAL)
	usbpcr &= ~USBPCR_USB_MODE;
#endif
	inno_usbphy_write(iphy, usbpcr, CPM_USBPCR);

	inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_UTMI_RST, USBPCR_UTMI_RST);
	udelay(11);
	inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB_SPENDN, OPCR_USB_SPENDN);
	udelay(501);
	inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_UTMI_RST, 0);
	udelay(1);

	usbpcr1 = inno_usbphy_read(iphy, CPM_USBPCR1);
	usbpcr1 |= USBPCR1_DPPULLDOWN | USBPCR1_DMPULLDOWN;
	inno_usbphy_write(iphy, usbpcr1, CPM_USBPCR1);

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

static int iphy_port1_init_x2600(struct ingenic_usb_phy *iphy)
{
	struct inno_phy_tuning *phy_tuning = iphy->iphy_priv->phy_tuning;
	u32 phy_tuning_num = iphy->iphy_priv->phy_tuning_num;
	u32 usb1pcr, usb1pcr1;
	unsigned long flags;
	u32 i;
	u8 reg;

	spin_lock_irqsave(&iphy->phy_lock, flags);

	usb1pcr = inno_usbphy_read(iphy, CPM_USB1PCR);
	usb1pcr &= ~USBPCR_IDPULLUP_MASK;
	usb1pcr |= USBPCR_USB_MODE;
	inno_usbphy_write(iphy, usb1pcr, CPM_USB1PCR);

	inno_usbphy_update_bits(iphy, CPM_USB1PCR, USBPCR_UTMI_RST, USBPCR_UTMI_RST);
	udelay(11);
	inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB1_SPENDN, OPCR_USB1_SPENDN);
	udelay(501);
	inno_usbphy_update_bits(iphy, CPM_USB1PCR, USBPCR_UTMI_RST, 0);
	udelay(1);

	usb1pcr1 = inno_usbphy_read(iphy, CPM_USB1PCR1);
	usb1pcr1 |= USBPCR1_DPPULLDOWN | USBPCR1_DMPULLDOWN;
	inno_usbphy_write(iphy, usb1pcr1, CPM_USB1PCR1);

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

static int iphy_port0_suspend_x2600(struct ingenic_usb_phy *iphy, int suspend)
{
	unsigned long flags;

	spin_lock_irqsave(&iphy->phy_lock, flags);

	if(suspend){
		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
		inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB_SPENDN, 0);
		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_UTMI_RST, 0);
	} else {
		/* USB PHY resume. */
		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
		inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB_SPENDN, OPCR_USB_SPENDN);
		udelay(501);

		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, USBPCR_POR);
		udelay(2);

		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
		udelay(1);
	}

	spin_unlock_irqrestore(&iphy->phy_lock, flags);

	return 0;
}

static int iphy_port1_suspend_x2600(struct ingenic_usb_phy *iphy, int suspend)
{
	unsigned long flags;

	spin_lock_irqsave(&iphy->phy_lock, flags);

	if(suspend){
		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
		inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB1_SPENDN, 0);
		inno_usbphy_update_bits(iphy, CPM_USB1PCR, USBPCR_UTMI_RST, 0);
	} else {
		/* USB PHY resume. */
		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
		inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB1_SPENDN, OPCR_USB1_SPENDN);
		udelay(501);

		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, USBPCR_POR);
		udelay(2);

		inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
		udelay(1);
	}

	spin_unlock_irqrestore(&iphy->phy_lock, flags);

	return 0;
}

struct ingenic_usb_phy_priv usb0_phy_x2600_priv = {
	.phyinit = iphy_port0_init_x2600,
	.physuspend = iphy_port0_suspend_x2600,
	.phy_tuning_num = 0,
	.phy_tuning = {
		/* HS eye height tuning */
		[0].reg_addr = 0x30,
		[0].msk = 0x7,
		[0].bit_off = 4,
		[0].val = 0x7,

		[1].reg_addr = 0x18,
		[1].msk = 0x1f,
		[1].bit_off = 3,
		[1].val = 0x1f,
	},
	.phy_remote_wakeup = 0,
	.phy_vbus_detect_ctl = 0,
	.phy_ls_fs_driver_ctl = 0,
};

struct ingenic_usb_phy_priv usb1_phy_x2600_priv = {
	.phyinit = iphy_port1_init_x2600,
	.physuspend = iphy_port1_suspend_x2600,
	.phy_tuning_num = 0,
	.phy_tuning = {
		/* HS eye height tuning */
		[0].reg_addr = 0x30,
		[0].msk = 0x7,
		[0].bit_off = 4,
		[0].val = 0x7,

		[1].reg_addr = 0x18,
		[1].msk = 0x1f,
		[1].bit_off = 3,
		[1].val = 0x1f,
	},
	.phy_remote_wakeup = 0,
	.phy_vbus_detect_ctl = 0,
	.phy_ls_fs_driver_ctl = 0,
};
