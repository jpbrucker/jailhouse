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
#include <asm/gic_common.h>
#include <asm/io.h>
#include <asm/irqchip.h>
#include <asm/platform.h>
#include <asm/setup.h>

static unsigned int gic_num_lr;

extern void *gicd_base;
extern unsigned int gicd_size;
void *gicc_base;
unsigned int gicc_size;
void *gicv_base;
void *gich_base;
unsigned int gich_size;

static int gic_init(void)
{
	int err;

	/* FIXME: parse device tree */
	gicc_base = GICC_BASE;
	gicc_size = GICC_SIZE;
	gich_base = GICH_BASE;
	gich_size = GICH_SIZE;
	gicv_base = GICV_BASE;

	err = arch_map_device(gicc_base, gicc_base, gicc_size);
	if (err)
		return err;

	err = arch_map_device(gich_base, gich_base, gich_size);

	return err;
}

static int gic_cpu_reset(struct per_cpu *cpu_data, bool is_shutdown)
{
	unsigned int i;
	bool root_shutdown = is_shutdown && (cpu_data->cell == &root_cell);
	u32 active;
	u32 gich_vmcr = 0;
	u32 gicc_ctlr, gicc_pmr;

	/* Clear list registers */
	for (i = 0; i < gic_num_lr; i++)
		gic_write_lr(i, 0);

	/* Deactivate all PPIs */
	active = readl_relaxed(gicd_base + GICD_ISACTIVER);
	for (i = 16; i < 32; i++) {
		if (test_bit(i, (unsigned long *)&active))
			writel_relaxed(i, gicc_base + GICC_DIR);
	}

	/* Disable PPIs if necessary */
	if (!root_shutdown)
		writel_relaxed(0xffff0000, gicd_base + GICD_ICENABLER);
	/* Ensure IPIs are enabled */
	writel_relaxed(0x0000ffff, gicd_base + GICD_ISENABLER);

	writel_relaxed(0, gich_base + GICH_APR);

	if (is_shutdown)
		writel_relaxed(0, gich_base + GICH_HCR);

	if (root_shutdown) {
		gich_vmcr = readl_relaxed(gich_base + GICH_VMCR);
		gicc_ctlr = 0;
		gicc_pmr = (gich_vmcr >> GICH_VMCR_PMR_SHIFT) << GICV_PMR_SHIFT;

		if (gich_vmcr & GICH_VMCR_EN0)
			gicc_ctlr |= GICC_CTLR_GRPEN1;
		if (gich_vmcr & GICH_VMCR_EOImode)
			gicc_ctlr |= GICC_CTLR_EOImode;

		writel_relaxed(gicc_ctlr, gicc_base + GICC_CTLR);
		writel_relaxed(gicc_pmr, gicc_base + GICC_PMR);

		gich_vmcr = 0;
	}
	writel_relaxed(gich_vmcr, gich_base + GICH_VMCR);

	return 0;
}

static int gic_cpu_init(struct per_cpu *cpu_data)
{
	u32 vtr, vmcr;
	u32 cell_gicc_ctlr, cell_gicc_pmr;

	/* Ensure all IPIs are enabled */
	writel_relaxed(0x0000ffff, gicd_base + GICD_ISENABLER);

	cell_gicc_ctlr = readl_relaxed(gicc_base + GICC_CTLR);
	cell_gicc_pmr = readl_relaxed(gicc_base + GICC_PMR);

	writel_relaxed(GICC_CTLR_GRPEN1 | GICC_CTLR_EOImode,
		       gicc_base + GICC_CTLR);
	writel_relaxed(GICC_PMR_DEFAULT, gicc_base + GICC_PMR);

	vtr = readl_relaxed(gich_base + GICH_VTR);
	gic_num_lr = (vtr & 0x3f) + 1;

	/* VMCR only contains 5 bits of priority */
	vmcr = (cell_gicc_pmr >> GICV_PMR_SHIFT) << GICH_VMCR_PMR_SHIFT;
	/*
	 * All virtual interrupts are group 0 in this driver since the GICV
	 * layout seen by the guest corresponds to GICC without security
	 * extensions:
	 * - A read from GICV_IAR doesn't acknowledge group 1 interrupts
	 *   (GICV_AIAR does it, but the guest never attempts to accesses it)
	 * - A write to GICV_CTLR.GRP0EN corresponds to the GICC_CTLR.GRP1EN bit
	 *   Since the guest's driver thinks that it is accessing a GIC with
	 *   security extensions, a write to GPR1EN will enable group 0
	 *   interrups.
	 * - Group 0 interrupts are presented as virtual IRQs (FIQEn = 0)
	 */
	if (cell_gicc_ctlr & GICC_CTLR_GRPEN1)
		vmcr |= GICH_VMCR_EN0;
	if (cell_gicc_ctlr & GICC_CTLR_EOImode)
		vmcr |= GICH_VMCR_EOImode;

	writel_relaxed(vmcr, gich_base + GICH_VMCR);
	writel_relaxed(GICH_HCR_EN, gich_base + GICH_HCR);

	/* Register ourselves into the CPU itf map */
	gic_probe_cpu_id(cpu_data->cpu_id);

	return 0;
}

static void gic_eoi_irq(u32 irq_id, bool deactivate)
{
	/*
	 * The GIC doesn't seem to care about the CPUID value written to EOIR,
	 * which is rather convenient...
	 */
	writel_relaxed(irq_id, gicc_base + GICC_EOIR);
	if (deactivate)
		writel_relaxed(irq_id, gicc_base + GICC_DIR);
}

static void gic_cell_init(struct cell *cell)
{
	struct jailhouse_memory gicv_region;

	/*
	 * target_cpu_map has not been populated by all available CPUs when the
	 * setup code initialises the root cell. It is assumed that the kernel
	 * already has configured all its SPIs anyway, and that it will redirect
	 * them when unplugging a CPU.
	 */
	if (cell != &root_cell)
		gic_target_spis(cell, cell);

	gicv_region.phys_start = (unsigned long)gicv_base;
	/*
	 * WARN: some SoCs (EXYNOS4) use a modified GIC which doesn't have any
	 * banked CPU interface, so we should map per-CPU physical addresses
	 * here.
	 * As for now, none of them seem to have virtualization extensions.
	 */
	gicv_region.virt_start = (unsigned long)gicc_base;
	gicv_region.size = gicc_size;
	gicv_region.flags = JAILHOUSE_MEM_DMA | JAILHOUSE_MEM_READ
			  | JAILHOUSE_MEM_WRITE;

	/*
	 * Let the guest access the virtual CPU interface instead of the
	 * physical one
	 */
	arch_map_memory_region(cell, &gicv_region);
}

static void gic_cell_exit(struct cell *cell)
{
	/* Reset interrupt routing of the cell's spis */
	gic_target_spis(cell, &root_cell);
}

static int gic_send_sgi(struct sgi *sgi)
{
	u32 val;

	if (!is_sgi(sgi->id))
		return -EINVAL;

	val = (sgi->routing_mode & 0x3) << 24
		| (sgi->targets & 0xff) << 16
		| (sgi->id & 0xf);

	writel_relaxed(val, gicd_base + GICD_SGIR);

	return 0;
}

static int gic_inject_irq(struct per_cpu *cpu_data, struct pending_irq *irq)
{
	int i;
	int first_free = -1;
	u32 lr;
	u64 elsr;

	elsr = readl_relaxed(gich_base + GICH_ELSR0);
	elsr |= (u64)readl_relaxed(gich_base + GICH_ELSR1) << 32;
	for (i = 0; i < gic_num_lr; i++) {
		if (test_bit(i, (unsigned long *)&elsr)) {
			/* Entry is available */
			if (first_free == -1)
				first_free = i;
			continue;
		}

		/* Check that there is no overlapping */
		lr = gic_read_lr(i);
		if ((lr & GICH_LR_VIRT_ID_MASK) == irq->virt_id)
			return -EINVAL;
	}

	if (first_free == -1) {
		/* Enable maintenance IRQ */
		u32 hcr;
		hcr = readl_relaxed(gich_base + GICH_HCR);
		hcr |= GICH_HCR_UIE;
		writel_relaxed(hcr, gich_base + GICH_HCR);

		return -EBUSY;
	}

	/* Inject group 0 interrupt (seen as IRQ by the guest) */
	lr = irq->virt_id;
	lr |= GICH_LR_PENDING_BIT;

	if (irq->hw) {
		lr |= GICH_LR_HW_BIT;
		lr |= irq->type.irq << GICH_LR_PHYS_ID_SHIFT;
	} else {
		lr |= irq->type.sgi.cpuid << GICH_LR_CPUID_SHIFT;
		if (irq->type.sgi.maintenance)
			lr |= GICH_LR_SGI_EOI_BIT;
	}

	gic_write_lr(first_free, lr);

	return 0;
}

static int gic_mmio_access(struct per_cpu *cpu_data, struct mmio_access *access)
{
	void *address = (void *)access->addr;

	if (address >= gicd_base && address < gicd_base + gicd_size)
		return gic_handle_dist_access(cpu_data, access);

	return TRAP_UNHANDLED;
}

struct irqchip_ops gic_irqchip = {
	.init = gic_init,
	.cpu_init = gic_cpu_init,
	.cpu_reset = gic_cpu_reset,
	.cell_init = gic_cell_init,
	.cell_exit = gic_cell_exit,

	.send_sgi = gic_send_sgi,
	.handle_irq = gic_handle_irq,
	.inject_irq = gic_inject_irq,
	.eoi_irq = gic_eoi_irq,
	.mmio_access = gic_mmio_access,
};
