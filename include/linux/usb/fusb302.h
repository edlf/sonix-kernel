#ifndef __LINUX_USB_FUSB302_H
#define __LINUX_USB_FUSB302_H

#include <linux/types.h>

/*
 * Board data for the FUSB302 Type-C controller, passed as the i2c client's
 * platform_data. The layout is fixed by the vendor's fusb302b_add module,
 * which builds this structure from its module parameters and registers the
 * i2c device: fields may only ever be added at the end.
 *
 * Without platform data the driver falls back to the "fcs,*" device
 * properties.
 */
struct fusb302_platform_data {
	u32 max_sink_microvolt;
	u32 max_sink_microamp;
	u32 max_sink_microwatt;
	u32 operating_sink_microwatt;
	/*
	 * Name of the charger power supply that has to follow the port:
	 * POWER_SUPPLY_PROP_TYPEC_POWER_ROLE when the role changes and
	 * POWER_SUPPLY_PROP_USB_OTG when VBUS has to be sourced. Empty for none.
	 */
	char charger_psy[20];
	/*
	 * GPIO driving the USB controller's ID input: low while the port is the
	 * host, high otherwise. A negative number when the board has none.
	 */
	int otg_id_gpio;
};

#endif /* __LINUX_USB_FUSB302_H */
