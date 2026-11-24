#include "phy-ingenic-inno.h"

#define CPM_USBPCR		(0x3c)
#define CPM_USBRDT		(0x40)
#define CPM_USBVBFIL	(0x44)
#define CPM_USBPCR1		(0x48)
#define CPM_OPCR		(0x24)
#define CPM_SRBC		(0xc4)

#define USBRDT_UTMI_RST	BIT(27)
#define SRBC_USB_SR	BIT(12)
#define OPCR_USB_SPENDN BIT(7)
#define USBPCR_POR      BIT(22)
#define USBPCR_IDPULLUP_MASK    (0x3 << 28)
#define USBPCR_IDPULLUP_OTG     (0x0 << 28)
#define USBPCR_USB_MODE         BIT(31)
#define USBPCR1_PORT_RST    BIT(21)
#define USBPCR1_DPPULLDOWN	BIT(29)
#define USBPCR1_DMPULLDOWN	BIT(28)

#define PHY_RX_SQU_TRI		(0x64)
#define PHY_RX_SQU_TRI_125MV    (0x8)

#define PHY_DETECT      (0x70)
#define PHY_OTG_SESSION (0x78)

static int iphy_init_x2580(struct ingenic_usb_phy *iphy)
{
	u32 usbpcr, usbpcr1;
	unsigned long flags;
	u8 reg;

	spin_lock_irqsave(&iphy->phy_lock, flags);

	usbpcr1 = inno_usbphy_read(iphy, CPM_USBPCR1);
	usbpcr1 |= USBPCR1_DPPULLDOWN | USBPCR1_DMPULLDOWN;
	inno_usbphy_write(iphy, usbpcr1, CPM_USBPCR1);

	usbpcr = inno_usbphy_read(iphy, CPM_USBPCR);
	usbpcr &= ~USBPCR_IDPULLUP_MASK;
#if IS_ENABLED(CONFIG_USB_DWC2_HOST)
	usbpcr |= USBPCR_USB_MODE | USBPCR_IDPULLUP_OTG;
#elif IS_ENABLED(CONFIG_USB_DWC2_PERIPHERAL)
	usbpcr &= ~USBPCR_USB_MODE;
#endif
	inno_usbphy_write(iphy, usbpcr, CPM_USBPCR);

	inno_usbphy_update_bits(iphy, CPM_USBPCR1, USBPCR1_PORT_RST, USBPCR1_PORT_RST);

	inno_usbphy_update_bits(iphy, CPM_USBRDT, USBRDT_UTMI_RST, 0);
	inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, USBPCR_POR);
	inno_usbphy_update_bits(iphy, CPM_SRBC, SRBC_USB_SR, SRBC_USB_SR);
	udelay(5);
	inno_usbphy_update_bits(iphy, CPM_USBPCR, USBPCR_POR, 0);
	udelay(10);
	inno_usbphy_update_bits(iphy, CPM_OPCR, OPCR_USB_SPENDN, OPCR_USB_SPENDN);
	udelay(550);
	inno_usbphy_update_bits(iphy, CPM_USBRDT, USBRDT_UTMI_RST, USBRDT_UTMI_RST);
	udelay(10);
	inno_usbphy_update_bits(iphy, CPM_SRBC, SRBC_USB_SR, 0);

	reg = usb_phy_readb(iphy->base + PHY_RX_SQU_TRI);
	reg &= ~(0xf << 3);
	reg |= PHY_RX_SQU_TRI_125MV << 3;
	usb_phy_writeb(reg,iphy->base + PHY_RX_SQU_TRI);

	reg = usb_phy_readb(iphy->base + PHY_OTG_SESSION);
	reg |=0x20;
	usb_phy_writeb(reg,iphy->base + PHY_OTG_SESSION);
	reg = usb_phy_readb(iphy->base + PHY_DETECT);
	reg &= ~0x8;
	usb_phy_writeb(reg,iphy->base + PHY_DETECT);

	spin_unlock_irqrestore(&iphy->phy_lock, flags);
	return 0;
}

static int iphy_suspend_x2580(struct ingenic_usb_phy *iphy, int suspend)
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

/* for x2580 */
struct ingenic_usb_phy_priv usb_phy_x2580_priv = {
	.phyinit = iphy_init_x2580,
	.physuspend = iphy_suspend_x2580,
	.phy_tuning_num = 1,
	.phy_tuning = {
		/* Tx HS pre_emphasize strength */
		[0].reg_addr = 0x40,
		[0].msk = 0x7,
		[0].bit_off = 3,
		[0].val = 0x7,
	},
	.phy_remote_wakeup = 0,
	.phy_vbus_detect_ctl = 0,
	.phy_ls_fs_driver_ctl = 0,
};
