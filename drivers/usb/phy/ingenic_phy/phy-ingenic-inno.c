#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/mfd/syscon.h>
#include <soc/cpm.h>
#include "phy-ingenic-inno.h"

#define RETRY_GET_GPIO_LEVEL_CNT      1

static const struct of_device_id of_matchs[] = {
	{ .compatible = "ingenic,innophy",.data = (void*)&usb_phy_common_priv},
	{ .compatible = "ingenic,innophy-x1600",.data = (void*)&usb_phy_x1600_priv},
	{ .compatible = "ingenic,innophy-x2500",.data = (void*)&usb_phy_x2500_priv},
	{ .compatible = "ingenic,innophy-x2580",.data = (void*)&usb_phy_x2580_priv},
	{ .compatible = "ingenic,innophy0-x2600",.data = (void*)&usb0_phy_x2600_priv},
	{ .compatible = "ingenic,innophy1-x2600",.data = (void*)&usb1_phy_x2600_priv},
	{ },
};

static irqreturn_t usb_dete_irq_handler(int irq, void *data)
{
	struct ingenic_usb_phy *iphy = (struct ingenic_usb_phy *)data;
	cancel_delayed_work_sync(&iphy->work);
	iphy->cc_state &= ~(1 << CC_STATE_SCHED_BIT);
	iphy->retry_count = RETRY_GET_GPIO_LEVEL_CNT;
	schedule_delayed_work(&iphy->work, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

int __weak dwc2_gadget_get_recv_intr_num(struct usb_gadget *gadget) { return 0; }

static void usb_vbus_connect(struct ingenic_usb_phy *iphy)
{
	struct usb_otg *otg = iphy->phy.otg;
	enum usb_phy_events	status;
	status = USB_EVENT_VBUS;
	iphy->phy.last_event = status;

	if(iphy->gpiod_headset_en)
			gpiod_direction_output(iphy->gpiod_headset_en, 0);
	if(iphy->gpiod_drvvbus)
			gpiod_direction_output(iphy->gpiod_drvvbus, 0);

	{
		int usbrdt = cpm_inl(CPM_USBRDT);
		usbrdt |= USBRDT_IDDIG_EN;
		/*ID level is high*/
		usbrdt |= USBRDT_IDDIG_REG;
		cpm_outl(usbrdt, CPM_USBRDT);
	}

	if (otg && otg->gadget) {
		otg->state = OTG_STATE_B_PERIPHERAL;
		dwc2_gadget_get_recv_intr_num(otg->gadget);
		usb_gadget_vbus_connect(otg->gadget);
	}

	atomic_notifier_call_chain(&iphy->phy.notifier, status, otg);
	iphy->usb_dete_state |= USB_DETE_CHARGE;
	iphy->cc_state |= (1 << CC_STATE_SCHED_BIT);
	iphy->cc_state |= (1 << CC_STATE_LEVEL_BIT);
	cancel_delayed_work(&iphy->work);
	schedule_delayed_work(&iphy->work, msecs_to_jiffies(100));
}

static void usb_vbus_disconnect(struct ingenic_usb_phy *iphy)
{
	struct usb_otg* otg = iphy->phy.otg;
	enum usb_phy_events	status;
	status = USB_EVENT_NONE;
	iphy->phy.last_event = status;
	iphy->cc_state |= (1 << CC_STATE_RUN_BIT);
	iphy->usb_dete_state &= ~USB_DETE_CHARGE;

	if (otg && otg->gadget) {
		otg->state = OTG_STATE_B_IDLE;
		usb_gadget_vbus_disconnect(otg->gadget);
	}
	atomic_notifier_call_chain(&iphy->phy.notifier, status, otg);
}

static void usb_id_connect(struct ingenic_usb_phy *iphy)
{
	struct usb_otg* otg = iphy->phy.otg;
	enum usb_phy_events	status;
	status = USB_EVENT_ID;
	iphy->phy.last_event = status;

	if(iphy->gpiod_headset_en)
		gpiod_direction_output(iphy->gpiod_headset_en, 0);

	if(iphy->gpiod_charge_en){
		gpiod_direction_output(iphy->gpiod_charge_en, 0);
	}

	{
		int usbrdt = cpm_inl(CPM_USBRDT);
		usbrdt |= USBRDT_IDDIG_EN;
		/*ID level is low*/
		usbrdt &= ~USBRDT_IDDIG_REG;
		cpm_outl(usbrdt, CPM_USBRDT);
	}

	if(iphy->gpiod_drvvbus)
		gpiod_direction_output(iphy->gpiod_drvvbus, 1);

	if(otg->gadget){
		otg->state = OTG_STATE_A_IDLE;
		usb_gadget_vbus_connect(otg->gadget);
		dwc2_gadget_get_recv_intr_num(otg->gadget);
	}

	atomic_notifier_call_chain(&iphy->phy.notifier, status, otg);

	cancel_delayed_work(&iphy->work);
	iphy->usb_dete_state |= (USB_DETE_HEADSET | USB_DETE_VBUS);
	iphy->cc_state |= (1 << CC_STATE_SCHED_BIT);
	iphy->cc_state &= ~(1 << CC_STATE_LEVEL_BIT);
	schedule_delayed_work(&iphy->work, msecs_to_jiffies(100));
}

static void usb_id_disconnect(struct ingenic_usb_phy *iphy)
{
	struct usb_otg* otg = iphy->phy.otg;
	enum usb_phy_events	status;
	status = USB_EVENT_NONE;
	if(otg)
		otg->state = OTG_STATE_A_IDLE;

	iphy->phy.last_event = status;
	iphy->usb_dete_state&= ~USB_DETE_HEADSET;

	if(iphy->gpiod_charge_en)
		gpiod_direction_output(iphy->gpiod_charge_en, 1);

	{
		int usbrdt = cpm_inl(CPM_USBRDT);
		usbrdt |= USBRDT_IDDIG_EN;
		usbrdt |= USBRDT_IDDIG_REG;
		cpm_outl(usbrdt, CPM_USBRDT);
	}

	atomic_notifier_call_chain(&iphy->phy.notifier, status,otg);
	iphy->cc_state |= (1 << CC_STATE_SCHED_BIT);
	iphy->cc_state |= (1 << CC_STATE_LEVEL_BIT);

	cancel_delayed_work(&iphy->work);
	iphy->usb_dete_state |= USB_DETE_VBUS;
	schedule_delayed_work(&iphy->work, msecs_to_jiffies(100));

}

static void usb_headset_detect(struct ingenic_usb_phy *iphy)
{
	struct usb_otg* otg = iphy->phy.otg;
	enum usb_phy_events	status;
	status = USB_EVENT_HEADSET;
	iphy->usb_dete_state&= ~USB_DETE_HEADSET;

	if(otg && otg->host && otg->gadget) {
		msleep(1000);
		if(dwc2_gadget_get_recv_intr_num(otg->gadget) < 2) {
			iphy->phy.last_event = status;
			if(iphy->gpiod_cc_en) {
				if(iphy->gpiod_drvvbus)
					gpiod_direction_output(iphy->gpiod_drvvbus, 0);
			}
			atomic_notifier_call_chain(&iphy->phy.notifier, status,otg);
		}
	}
}

static void usb_charge_detect(struct ingenic_usb_phy *iphy)
{
	struct usb_otg* otg = iphy->phy.otg;
	enum usb_phy_events	status;
	status = 	USB_EVENT_CHARGER;
	iphy->usb_dete_state &= ~USB_DETE_CHARGE;

	if(otg && otg->gadget) {
		msleep(1000);
		if( dwc2_gadget_get_recv_intr_num(otg->gadget) == 0) {
			iphy->phy.last_event = status;
			atomic_notifier_call_chain(&iphy->phy.notifier, status,otg);
		}
	}
}

static void usb_cc_detect(struct ingenic_usb_phy *iphy)
{
	if(iphy->cc_state & (1 << CC_STATE_RUN_BIT)) {
		int state_n = gpiod_get_value(iphy->gpiod_cc_en) ? 0 : 1;
		cancel_delayed_work(&iphy->work);
		iphy->cc_state |= (1 << CC_STATE_SCHED_BIT);
		gpiod_direction_output(iphy->gpiod_cc_en,state_n);
		schedule_delayed_work(&iphy->work, msecs_to_jiffies(CC_WORK_DELAY));
	} else {
		int state = gpiod_get_value(iphy->gpiod_cc_en) ? 1 : 0;
		int cc_state = iphy->cc_state & (1 << CC_STATE_LEVEL_BIT) ? 1 : 0;
		if(cc_state != state){
			gpiod_direction_output(iphy->gpiod_cc_en,cc_state);
		}
	}
}

void usb_dete_work(struct work_struct *work)
{
	struct ingenic_usb_phy *iphy =
		container_of(work, struct ingenic_usb_phy, work.work);
//	struct usb_otg	*otg = iphy->phy.otg;

	int vbus = 0, id_pin_changed = 0, usb_state = 0;
	int has_cc = 0,cc_state = 0;
	int changed = 0;

	if (iphy->gpiod_vbus)
		vbus = gpiod_get_value_cansleep(iphy->gpiod_vbus);
	if (iphy->gpiod_id)
		id_pin_changed = gpiod_get_value_cansleep(iphy->gpiod_id);/*1:host mode, 0:device mode*/

	if(iphy->gpiod_cc_en) {
		has_cc = 1;
		cc_state = gpiod_get_value(iphy->gpiod_cc_en) ? 1 : 0;
		if (id_pin_changed)
			usb_state |= USB_DETE_ID;
		else if(cc_state && vbus)
			usb_state |= USB_DETE_VBUS;
	} else {
		if(id_pin_changed)
			usb_state |= USB_DETE_ID;
		if (vbus)
			usb_state |= USB_DETE_VBUS;
	}

	changed = usb_state ^ iphy->usb_dete_state;
//	printk("changed: 0x%08x usb_state: 0x%08x\n",changed,usb_state);
	if (changed) {
		iphy->usb_dete_state = usb_state;
		iphy->cc_state = 0;

		if(changed & USB_DETE_VBUS){
			if (usb_state & USB_DETE_VBUS) {
				usb_vbus_connect(iphy);
			}else{
				usb_vbus_disconnect(iphy);

			}
			changed = 0;
		}

		if(changed & USB_DETE_ID){
			if(usb_state & USB_DETE_ID) {
				usb_id_connect(iphy);
			} else {
				usb_id_disconnect(iphy);
			}
			changed = 0;
		}

		if(changed & USB_DETE_HEADSET) {
			usb_headset_detect(iphy);
			changed = 0;
		}
		if(changed & USB_DETE_CHARGE) {
			usb_charge_detect(iphy);
			changed = 0;
		}
	}
#ifdef CONFIG_USB_DWC2_DETECT_CONNECT
	/* make sure get right gpio level firstly after resume */
	else if (iphy->retry_count > 0) {
		schedule_delayed_work(&iphy->work, msecs_to_jiffies(100));
		iphy->retry_count--;
	}
#endif

	if(has_cc) {
		usb_cc_detect(iphy);
	}
}

static int iphy_init(struct usb_phy *x)
{
	int ret = 0;
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);

	/* USB phy clk opened. */
	if (!IS_ERR_OR_NULL(iphy->gate_clk))
		clk_prepare_enable(iphy->gate_clk);

	usleep_range(5000, 5000);

	if (iphy->iphy_priv->phyinit)
		ret = iphy->iphy_priv->phyinit(iphy);

	return ret;
}

static int iphy_set_suspend(struct usb_phy *x, int suspend)
{
	int ret = 0;
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);

	if (iphy->iphy_priv->physuspend)
		ret = iphy->iphy_priv->physuspend(iphy, suspend);

	return ret;
}

static void iphy_shutdown(struct usb_phy *x)
{
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);
	bool vbus_detect_ctl = iphy->iphy_priv->phy_vbus_detect_ctl;
	bool ls_fs_driver_ctl = iphy->iphy_priv->phy_ls_fs_driver_ctl;

	iphy->iphy_priv->physuspend(iphy,1);

	/* VBUS voltage level detection power down. */
	if (vbus_detect_ctl)
		usb_phy_writeb(0x8, iphy->base + PHY_SUSPEND_LPM);

	/* disable full/low speed driver at the receiver */
	/* NOTE: it is used to reducepower consumption. */
	if (ls_fs_driver_ctl) {
		unsigned int reg = usb_phy_readb(iphy->base + PHY_REG_100);
		reg &= ~(1<<6);
		usb_phy_writeb(reg, iphy->base + PHY_REG_100);
	}
}

static int iphy_set_vbus(struct usb_phy *x, int on)
{
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);

	if (iphy->gpiod_drvvbus) {
			gpiod_set_value(iphy->gpiod_drvvbus, on);
	}
	return 0;
}

static int iphy_set_wakeup(struct usb_phy *x, bool enabled)
{
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);
	struct usb_otg *otg = iphy->phy.otg;
	enum usb_device_speed speed;
	unsigned long flags;
	int timeout;
	unsigned int usbrdt;

	spin_lock_irqsave(&iphy->phy_lock, flags);

	if (enabled) {
		/* Enable resume interrupt */
		usbrdt = inno_usbphy_read(iphy, CPM_USBRDT);
		usbrdt &= ~USBRDT_RESUME_SPEED_MSK;
#if IS_ENABLED(CONFIG_USB_DWC2_PERIPHERAL) || IS_ENABLED(CONFIG_USB_DWC2_DUAL_ROLE)
		{
			struct dwc2_hsotg *hsotg = (struct dwc2_hsotg *)container_of(otg->gadget, struct dwc2_hsotg, gadget);
			if (dwc2_is_device_mode(hsotg)) {
				speed = (otg && otg->gadget) ? otg->gadget->speed : USB_SPEED_FULL;
				if(speed == USB_SPEED_HIGH)
					usbrdt |= USBRDT_RESUME_SPEED_HIGH;
				if(speed == USB_SPEED_FULL)
					usbrdt |= USBRDT_RESUME_SPEED_FULL;
				if(speed == USB_SPEED_LOW)
					usbrdt |= USBRDT_RESUME_SPEED_LOW;
			} else {
				/* Remote wakeup device speed. */
				usbrdt |= USBRDT_RESUME_SPEED_LOW;
			}
		}
#else
		usbrdt |= USBRDT_RESUME_SPEED_LOW;
#endif
		inno_usbphy_write(iphy, usbrdt, CPM_USBRDT);

		//TODO: the function be in suspend.
		inno_usbphy_update_bits(iphy, CPM_USBRDT, USBRDT_RESUME_INTEEN, USBRDT_RESUME_INTEEN);

	} else {
		/* Disable usb resume interrupt. */
		inno_usbphy_update_bits(iphy, CPM_USBRDT, USBRDT_RESUME_INTEEN, 0);

		/* Clear usb resume interrupt. */
		usbrdt = inno_usbphy_read(iphy, CPM_USBRDT);
		if(usbrdt & (USBRDT_RESUME_STATUS)){
			usbrdt |= USBRDT_RESUME_INTERCLR;
			inno_usbphy_write(iphy, usbrdt, CPM_USBRDT);
			timeout = 100;
			while(1){
				if(timeout-- < 0){
					printk("%s:%d resume interrupt clear failed\n", __func__, __LINE__);
					return -EAGAIN;
				}
				usbrdt = inno_usbphy_read(iphy, CPM_USBRDT);
				if(!(usbrdt & (USBRDT_RESUME_STATUS)))
					break;
			}
		}

	}
	/* Clear usb resume interrupt bit. */
	inno_usbphy_update_bits(iphy, CPM_USBRDT, USBRDT_RESUME_INTERCLR, 0);

	spin_unlock_irqrestore(&iphy->phy_lock, flags);

	return 0;
}

static int iphy_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	if (!otg)
		return -ENODEV;
	{
		struct usb_phy *x = otg->usb_phy;
		atomic_notifier_call_chain(&x->notifier, x->last_event,otg);
	}
	otg->host = host;
	return 0;
}

#if IS_ENABLED(CONFIG_USB_DWC2_PERIPHERAL) || IS_ENABLED(CONFIG_USB_DWC2_DUAL_ROLE)
static int iphy_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	if (!otg)
		return -ENODEV;
	{
		struct usb_phy *x = otg->usb_phy;
		atomic_notifier_call_chain(&x->notifier, x->last_event,otg);
	}
	otg->gadget = gadget;

	return 0;
}
#else
#define iphy_set_peripheral NULL
#endif

static int iphy_notify_connect(struct usb_phy *x,
		enum usb_device_speed speed)
{
	struct ingenic_usb_phy *iphy = container_of(x, struct ingenic_usb_phy, phy);

	iphy->roothub_port_speed = speed;

	return 0;
}

#if defined (CONFIG_USB_DWC2_DUAL_ROLE) && defined (CONFIG_USB_DWC2_FORCE_CONNECT)
static ssize_t sw_switch_hsotg_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ingenic_usb_phy *iphy = (struct ingenic_usb_phy *)dev->driver_data;
	unsigned int usbrdt;

	if (sscanf(buf, "%s", iphy->hsotg_sw_switch) != 1){
		printk("input %s not invailed\n", iphy->hsotg_sw_switch);
		return count;
	}

	if (!strcmp(iphy->hsotg_sw_switch, "device")) {
		printk("Force ID level High\n");

		usbrdt = cpm_inl(CPM_USBRDT);
		usbrdt |= USBRDT_IDDIG_EN;
		usbrdt |= USBRDT_IDDIG_REG;
		cpm_outl(usbrdt, CPM_USBRDT);

	}else if(!strcmp(iphy->hsotg_sw_switch, "host")){
		printk("Force ID Level Low!\n");

		usbrdt = cpm_inl(CPM_USBRDT);
		usbrdt |= USBRDT_IDDIG_EN;
		usbrdt &= ~USBRDT_IDDIG_REG;
		cpm_outl(usbrdt, CPM_USBRDT);

	} else if (!strcmp(iphy->hsotg_sw_switch, "none")){
		printk("Enabled ID status from pin\n");

		usbrdt = cpm_inl(CPM_USBRDT);
		usbrdt &= ~(USBRDT_IDDIG_EN | USBRDT_IDDIG_REG);
		cpm_outl(usbrdt, CPM_USBRDT);

	}else{
		printk("input %s not invailed\n", iphy->hsotg_sw_switch);
	}

	memset(iphy->hsotg_sw_switch, 0 , sizeof(iphy->hsotg_sw_switch));

	return count;
}
#endif

#if defined (CONFIG_USB_DWC2_DUAL_ROLE) && defined (CONFIG_USB_DWC2_FORCE_CONNECT)
static DEVICE_ATTR(sw_switch_hsotg, S_IWUSR, NULL, sw_switch_hsotg_store);
#endif

static struct device_attribute *usb_phy_attributes[] = {
#if defined (CONFIG_USB_DWC2_DUAL_ROLE) && defined (CONFIG_USB_DWC2_FORCE_CONNECT)
	&dev_attr_sw_switch_hsotg,
#endif
	NULL
};

static int device_create_node(struct device * dev, struct device_attribute **attrs)
{
	struct device_attribute *attr;
	while ((attr = *attrs++)) {
		int err;
		err = device_create_file(dev, attr);
		if (err) {
			printk("%s() device_create_file() failed.\n", __func__);
			return err;
		}
	}
	return 0;
}
static int device_remove_node(struct device * dev, struct device_attribute **attrs)
{
	struct device_attribute *attr;
	while ((attr = *attrs++)) {
		device_remove_file(dev, attr);
	}
	return 0;
}

static int usb_phy_ingenic_probe(struct platform_device *pdev)
{
	struct ingenic_usb_phy *iphy;
	struct resource *res;
	const struct of_device_id *match;
	int ret,need_sched;

	iphy = (struct ingenic_usb_phy *)
		devm_kzalloc(&pdev->dev, sizeof(*iphy), GFP_KERNEL);
	if (!iphy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	iphy->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(iphy->base))
		return PTR_ERR(iphy->base);

	match = of_match_node(of_matchs, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	iphy->iphy_priv = (struct ingenic_usb_phy_priv*)match->data;
	iphy->phy.init = iphy_init;
	iphy->phy.shutdown = iphy_shutdown;
	iphy->phy.dev = &pdev->dev;
	iphy->dev = &pdev->dev;
	iphy->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, NULL);
	if (IS_ERR(iphy->regmap)) {
		dev_err(&pdev->dev, "failed to find regmap for usb phy %ld\n", PTR_ERR(iphy->regmap));
		return PTR_ERR(iphy->regmap);
	}

	iphy->gpiod_drvvbus = devm_gpiod_get_optional(&pdev->dev,"ingenic,drvvbus", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(iphy->gpiod_drvvbus))
		iphy->gpiod_drvvbus = NULL;

	spin_lock_init(&iphy->phy_lock);

	need_sched = 0;

#ifdef CONFIG_USB_DWC2_DETECT_CONNECT
	iphy->retry_count = RETRY_GET_GPIO_LEVEL_CNT;
	INIT_DELAYED_WORK(&iphy->work, usb_dete_work);
	iphy->gpiod_id = devm_gpiod_get_optional(&pdev->dev,"ingenic,id-dete", GPIOD_IN);
	if (IS_ERR_OR_NULL(iphy->gpiod_id)){
		iphy->gpiod_id = NULL;
	}
	iphy->gpiod_vbus = devm_gpiod_get_optional(&pdev->dev,"ingenic,vbus-dete", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(iphy->gpiod_vbus))
		iphy->gpiod_vbus = NULL;


	iphy->gpiod_charge_en = devm_gpiod_get_optional(&pdev->dev,"ingenic,charge-en", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(iphy->gpiod_charge_en))
		iphy->gpiod_charge_en = NULL;

	iphy->gpiod_headset_en = devm_gpiod_get_optional(&pdev->dev,"ingenic,headset-en", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(iphy->gpiod_headset_en)){
		iphy->gpiod_headset_en = NULL;
	}

	device_init_wakeup(&pdev->dev, true);

	if (of_find_property(pdev->dev.of_node, "enable-vbus-wakeup", NULL))
		iphy->vbus_wakeup = true;

	if (of_find_property(pdev->dev.of_node, "enable-id-wakeup", NULL))
		iphy->id_wakeup = true;

	iphy->gpiod_cc_en = devm_gpiod_get_optional(&pdev->dev,"ingenic,cc-rd-en", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(iphy->gpiod_cc_en))
		iphy->gpiod_cc_en = NULL;

	if (iphy->gpiod_vbus) {
		iphy->vbus_irq = gpiod_to_irq(iphy->gpiod_vbus);
		ret = devm_request_threaded_irq(&pdev->dev,iphy->vbus_irq,
				NULL,usb_dete_irq_handler,
				IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
				"vbus_dete",(void *)iphy);
		if (ret)
			return ret;
		{
			int vbus = gpiod_get_value_cansleep(iphy->gpiod_vbus);
			need_sched = need_sched || vbus;
		}
	} else {
		iphy->usb_dete_state &= ~USB_DETE_VBUS;
		iphy->gpiod_vbus = NULL;
	}

	if (!IS_ERR_OR_NULL(iphy->gpiod_id)) {
		iphy->id_irq = gpiod_to_irq(iphy->gpiod_id);
		ret = devm_request_threaded_irq(&pdev->dev,iphy->id_irq,NULL,
				usb_dete_irq_handler,
				IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
				"id_dete",(void *)iphy);
		if (ret)
			return ret;
		{
			int usbid = gpiod_get_value_cansleep(iphy->gpiod_id);
			need_sched = need_sched || usbid;
		}
	} else {
		iphy->usb_dete_state |= USB_DETE_ID;
		iphy->gpiod_id = NULL;
	}

	if(iphy->gpiod_vbus && iphy->gpiod_id){
		need_sched = 1;
	}

#if IS_ENABLED(CONFIG_USB_DWC2_DUAL_ROLE)
	iphy->usb_dete_state |= USB_DETE_VBUS;
#endif

	if(need_sched) {
		cancel_delayed_work_sync(&iphy->work);
		iphy->cc_state &= ~(1 << CC_STATE_SCHED_BIT);
		schedule_delayed_work(&iphy->work, msecs_to_jiffies(2000));
	}else if(iphy->gpiod_cc_en) {
		gpiod_direction_output(iphy->gpiod_cc_en,1);
		iphy->cc_state |= (1 << CC_STATE_SCHED_BIT);
		iphy->cc_state |= (1 << CC_STATE_RUN_BIT);
		schedule_delayed_work(&iphy->work, msecs_to_jiffies(1000));
	}
#else
	iphy->usb_dete_state &= ~USB_DETE_ID;
	iphy->gpiod_id = NULL;
	iphy->usb_dete_state &= ~USB_DETE_VBUS;
	iphy->gpiod_vbus = NULL;
#endif
	iphy->phy.set_vbus = iphy_set_vbus;
	iphy->phy.set_suspend = iphy_set_suspend;
	if (iphy->iphy_priv->phy_remote_wakeup) {
		iphy->phy.set_wakeup = iphy_set_wakeup;
	}

	iphy->phy.notify_connect = iphy_notify_connect;
	iphy->phy.otg = devm_kzalloc(&pdev->dev, sizeof(*iphy->phy.otg),
			GFP_KERNEL);
	iphy->phy.io_ops = devm_kzalloc(&pdev->dev, sizeof(*iphy->phy.io_ops),
			GFP_KERNEL);

	if (!iphy->phy.otg)
		return -ENOMEM;

	iphy->phy.otg->state		= OTG_STATE_UNDEFINED;
	iphy->phy.otg->usb_phy		= &iphy->phy;
	iphy->phy.otg->set_host		= iphy_set_host;
	iphy->phy.otg->set_peripheral	= iphy_set_peripheral;
	iphy->phy.io_ops->read		= phy_ioread;
	iphy->phy.io_ops->write		= phy_iowrite;
	iphy->phy.type = USB_PHY_TYPE_USB2;
	iphy->gate_clk = devm_clk_get(&pdev->dev, "gate_usbphy");
	if (IS_ERR_OR_NULL(iphy->gate_clk)){
		iphy->gate_clk = NULL;
		dev_err(&pdev->dev, "cannot get usbphy clock !\n");
		return -ENODEV;
	}

	ret = usb_add_phy_dev(&iphy->phy);
	if (ret) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
				ret);
		return ret;
	}
	platform_set_drvdata(pdev, iphy);

	device_create_node(&pdev->dev, usb_phy_attributes);
	pr_info("inno phy probe success\n");

	return 0;
}

static int usb_phy_ingenic_remove(struct platform_device *pdev)
{
	struct ingenic_usb_phy *iphy = platform_get_drvdata(pdev);
	device_init_wakeup(&pdev->dev, 0);
	usb_remove_phy(&iphy->phy);
	device_remove_node(iphy->dev, usb_phy_attributes);

	return 0;
}

MODULE_DEVICE_TABLE(of, ingenic_xceiv_dt_ids);

#ifdef CONFIG_PM
static int usb_phy_suspend(struct device *dev)
{
	struct ingenic_usb_phy *iphy = dev_get_drvdata(dev);
#ifdef CONFIG_USB_DWC2_DETECT_CONNECT
	if(iphy->gpiod_cc_en) {
		gpiod_direction_output(iphy->gpiod_cc_en,0);
	}
#endif
	if (iphy->vbus_wakeup && iphy->gpiod_vbus)
		enable_irq_wake(iphy->vbus_irq);

	if (iphy->id_wakeup && iphy->gpiod_id)
		enable_irq_wake(iphy->id_irq);

	return 0;
}

static int usb_phy_resume(struct device *dev)
{
	struct ingenic_usb_phy *iphy = dev_get_drvdata(dev);
#ifdef CONFIG_USB_DWC2_DETECT_CONNECT
	iphy->retry_count = RETRY_GET_GPIO_LEVEL_CNT;
	schedule_delayed_work(&iphy->work, msecs_to_jiffies(100));
#endif
	if (iphy->vbus_wakeup && iphy->gpiod_vbus)
		disable_irq_wake(iphy->vbus_irq);

	if (iphy->id_wakeup && iphy->gpiod_id)
		disable_irq_wake(iphy->id_irq);

	return 0;
}
static const struct dev_pm_ops ingenic_usb_phy_pm_ops = {
	.suspend	= usb_phy_suspend,
	.resume		= usb_phy_resume,
};
#endif

static struct platform_driver usb_phy_ingenic_driver = {
	.probe		= usb_phy_ingenic_probe,
	.remove		= usb_phy_ingenic_remove,
	.driver		= {
		.name	= "usb_phy",
		.of_match_table = of_matchs,
#ifdef CONFIG_PM
		.pm = &ingenic_usb_phy_pm_ops,
#endif
	},
};

static int __init usb_phy_ingenic_init(void)
{
	return platform_driver_register(&usb_phy_ingenic_driver);
}
subsys_initcall(usb_phy_ingenic_init);

static void __exit usb_phy_ingenic_exit(void)
{
	platform_driver_unregister(&usb_phy_ingenic_driver);
}
module_exit(usb_phy_ingenic_exit);
