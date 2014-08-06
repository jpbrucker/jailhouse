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

#include <asm/control.h>
#include <asm/io.h>
#include <asm/irqchip.h>
#include <asm/paging.h>
#include <asm/platform.h>
#include <asm/smp.h>
#include <asm/traps.h>
#include <jailhouse/processor.h>

static unsigned long hotplug_mbox;

static int smp_init(struct cell *cell)
{
	hotplug_mbox = SYSREGS_BASE + 0x1c;

	/* Map the mailbox page */
	arch_generic_smp_init(hotplug_mbox);

	psci_cell_init(cell);

	return 0;
}

static unsigned long smp_spin(struct per_cpu *cpu_data)
{
	return arch_generic_smp_spin(hotplug_mbox);
}

static int smp_mmio(struct per_cpu *cpu_data, struct mmio_access *access)
{
	return arch_generic_smp_mmio(cpu_data, access, hotplug_mbox);
}

static struct smp_ops exynos_smp_ops = {
	.type = SMP_SPIN,
	.init = smp_init,
	.cpu_spin = smp_spin,
	.mmio_handler = smp_mmio,
};

void register_smp_ops(struct cell *cell)
{
	cell->arch.smp = &exynos_smp_ops;
}
