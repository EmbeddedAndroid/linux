/*
 * Debugging macro include header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define UART01x_DR              0x04    /* Data read or write */
#define UART01x_FR              0x14    /* Flag register (Read only) */

#define UART01x_FR_TXFF         0x020


#define UART0_PADDR		0x09405000
#define UART0_VADDR		0xfc705000

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =UART0_PADDR	@ physical
		ldr	\rv, =UART0_VADDR	@ virtual
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #UART01x_DR]
		.endm

		.macro	waituart,rd,rx
		.endm

		.macro  busyuart,rd,rx
1001:		ldr	\rd, [\rx, #UART01x_FR]
		tst	\rd, #UART01x_FR_TXFF
		bne	1001b
		.endm
