/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) ARM Limited, 2014
 *
 * Authors:
 *  Jean-Philippe Brucker <jean-philippe.brucker@arm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_ASM_DEBUG_SAMSUNG_H
#define _JAILHOUSE_ASM_DEBUG_SAMSUNG_H

#include <asm/debug.h>
#include <asm/io.h>
#include <asm/processor.h>

#define ULCON		0x00
#define UCON		0x04
#define UFCON		0x08
#define UMCON		0x0c
#define UTRSTAT		0x10
#define UFSTAT		0x18
#define UTXH		0x20
#define URXH		0x24
#define UBRDIV		0x28
#define UFRACVAL	0x2c
#define UINTP		0x30
#define UINTSP		0x34
#define UINTM		0x38

#define UFCON_FIFOMODE	(1 << 0)
#define UFSTAT_TXFULL	(1 << 24)
#define UFSTAT_TXMASK	(0xff << 16)
#define UTRSTAT_TEMPTY	(1 << 1)

#ifndef __ASSEMBLY__

static void uart_init(struct uart_chip *chip)
{
}

static void uart_wait(struct uart_chip *chip)
{
	u32 fstat;
	u8 trstat;

	if (chip->fifo_enabled) {
		do {
			fstat = readl_relaxed(chip->virt_base + UFSTAT);
			cpu_relax();
		} while (fstat & UFSTAT_TXMASK);
	} else {
		do {
			trstat = readb_relaxed(chip->virt_base + UTRSTAT);
			cpu_relax();
		} while (!(trstat & UTRSTAT_TEMPTY));
	}
}

static void uart_busy(struct uart_chip *chip)
{
	u32 fstat;
	u8 trstat;

	if (chip->fifo_enabled) {
		do {
			fstat = readl_relaxed(chip->virt_base + UFSTAT);
			cpu_relax();
		} while ((fstat & UFSTAT_TXFULL)); /* FIFO Full */
	} else {
		do {
			trstat = readb_relaxed(chip->virt_base + UTRSTAT);
			cpu_relax();
		} while (!(trstat & UTRSTAT_TEMPTY));
	}
}

static void uart_write(struct uart_chip *chip, char c)
{
	writeb_relaxed(c, chip->virt_base + UTXH);
}

#endif /* !__ASSEMBLY__ */
#endif /* !_JAILHOUSE_ASM_DEBUG_SAMSUNG_H */
