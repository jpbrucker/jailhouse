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
#include <asm/gic_common.h>
#include <asm/gic_v2.h>
#include <inmates/gic.h>
#include <inmates/inmate.h>
#include <mach/gic_v2.h>

void gic_enable(unsigned int irqn)
{
	writel_relaxed(1 << irqn, GICD_BASE + GICD_ISENABLER);
}

int gic_init(void)
{
	writel_relaxed(GICC_CTLR_GRPEN1, GICC_BASE + GICC_CTLR);
	writel_relaxed(GICC_PMR_DEFAULT, GICC_BASE + GICC_PMR);

	return 0;
}

void gic_write_eoi(u32 irqn)
{
	writel_relaxed(irqn, GICC_BASE + GICC_EOIR);
}

u32 gic_read_ack(void)
{
	return readl_relaxed(GICC_BASE + GICC_IAR);
}
