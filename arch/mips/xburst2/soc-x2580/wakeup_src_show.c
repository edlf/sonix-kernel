#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/printk.h>

struct intc_regs {
	volatile unsigned int ISR;    /* 0x00*/
	volatile unsigned int IMR;	/* 0x04 */
	volatile unsigned int IMSR;	/* 0x08 */
	volatile unsigned int IMCR;	/* 0x0C */
	volatile unsigned int IPR0;	/* 0x10 */
	volatile unsigned int reserved[3];
	volatile unsigned int ISR1;	/* 0x20 */
	volatile unsigned int IMR1;	/* 0x24 */
	volatile unsigned int IMSR1;	/* 0x28 */
	volatile unsigned int IMCR1;	/* 0x2C */
	volatile unsigned int IPR1;	/* 0x30 */
};

#define INTC_IO_BASE					0xB2300000

extern void show_gpio_wakeup_sources(int port);

static const char *intc0_src_name[32] = {
	"JPEG",
	"AIC",
	"SYS LEP",
	"RTC",
	"LZMA",
	"MIPI RX",
	"IPU",
	"SFC0",
	"SSI1",
	"SSI0",
	"PDMA",
	"SSI SLV",
	"RESERVED",
	"IVDC",
	"GPIOD",	/* GPIO3 */
	"GPIOC",	/* GPIO2 */
	"GPIOB",	/* GPIO1 */
	"GPIOA",	/* GPIO0 */
	"SADC",
	"SFC1",
	"LCD",
	"USB",
	"HASH",
	"AES",
	"RSA",
	"TCU2",
	"TCU1",
	"TCU0",
	"RESERVED",
	"RESERVED",
	"VIC",
	"TIZIANO",
};

static const char *intc1_src_name[32] = {
	"RESERVED"
	"AUDIO",
	"DTRNG",		/* software */
	"LDC",
	"MSC1",
	"MSC0",
	"AIP2",
	"AIP1",
	"AIP0",
	"DDR DWC",
	"DRAWBOX",
	"VO",
	"HARB2",
	"I2D",
	"RESERVED",
	"CPM",
	"UART3",
	"UART2",
	"UART1",
	"UART0",
	"DDR",
	"MON",
	"EFUSE",
	"MAC",
	"UART5",
	"UART4",
	"I2C2",
	"I2C1",
	"I2C0",
	"PDMA MCU",
	"ELGENC",
	"PWM",
};


void show_wakeup_sources(void)
{
	struct intc_regs *intc = (void *)INTC_IO_BASE;
	int i;

	unsigned int reg = intc->IPR0;

	printk("-----------------\n");
	/* 当有多个中断时，具体那个中断唤醒请查看 pm_wakeup_irq() 返回的中断号 */
	for (i = 0; i < 32; ++i) {
		if (reg & (1 << i)) {
			printk("WAKE UP by INTC0: bit[%d] -> %s\n", i, intc0_src_name[i]);
			if (i >= 14 && i <= 17)
				show_gpio_wakeup_sources(17 - i);
		}
	}

	reg = intc->IPR1;

	for (i = 0; i < 32; ++i) {
		if (reg & (1 << i)) {
			printk("WAKE UP by INTC1: bit[%d] -> %s\n", i, intc1_src_name[i]);
		}
	}
	printk("-----------------\n");
}
