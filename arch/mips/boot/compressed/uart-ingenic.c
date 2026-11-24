#include <soc/base.h>
#include <asm/addrspace.h>
#include <stdarg.h>

/*
 * Add puts func just for debug.
 * If it does not work,check the follows macro define.
 * Have a pleasant debug day.
 */

static volatile int uart_base;

#define OFF_LSR		(0x14)
#define OFF_LCR		(0x0C)
#define OFF_TDR		(0x00)
#define OFF_SPR     (0X1C)
#define UART_OFF	0x1000
#define UART_LSR_TDRQ	(1 << 5)        /* 1: transmit FIFO half "empty" */
#define UART_LSR_TEMT	(1 << 6)	/* 1: transmit FIFO and shift registers empty */

static int is_no_uart = 1;

static void check_uart(void)
{
	volatile unsigned char *base = (volatile unsigned char*)CKSEG1ADDR(UART0_IOBASE);
	int i;
	for(i = 0; i < 10; i++) {
		if(base[OFF_SPR] == 0xa9) {
			is_no_uart = 0;
			break;
		}
		base += UART_OFF;
	}

	uart_base = (int)base;
}

void putc (const char c)
{
    volatile char *uart_lsr;
    volatile char *uart_tdr;

    static int is_check = 0;

    if (!is_check) {
        check_uart();
        is_check = 1;
    }

	if (is_no_uart)
		return;

	uart_lsr = (volatile char *)(uart_base + OFF_LSR);
	uart_tdr = (volatile char *)(uart_base + OFF_TDR);

	if (c == '\n') putc ('\r');

	/* Wait for fifo to shift out some bytes */
	while ( !((*uart_lsr & (UART_LSR_TDRQ | UART_LSR_TEMT)) == 0x60) );

	*uart_tdr = (char)c;
}