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

#include <asm/cell.h>
#include <asm/control.h>
#include <asm/gic_common.h>
#include <asm/io.h>
#include <asm/irqchip.h>
#include <asm/percpu.h>
#include <asm/platform.h>
#include <asm/spinlock.h>
#include <asm/traps.h>
#include <jailhouse/control.h>

#define REG_RANGE(base, n, size)		\
		(base) ... ((base) + (n - 1) * (size))

extern void *gicd_base;
extern unsigned int gicd_size;

static DEFINE_SPINLOCK(dist_lock);

/* The GIC interface numbering does not necessarily match the logical map */
u8 target_cpu_map[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

/*
 * Most of the GIC distributor writes only reconfigure the IRQs corresponding to
 * the bits of the written value, by using separate `set' and `clear' registers.
 * Such registers can be handled by setting the `is_poke' boolean, which allows
 * to simply restrict the access->val with the cell configuration mask.
 * Others, such as the priority registers, will need to be read and written back
 * with a restricted value, by using the distributor lock.
 */
static int restrict_bitmask_access(struct per_cpu *cpu_data,
				struct mmio_access *access,
				unsigned int reg_index,
				unsigned int bits_per_irq,
				bool is_poke)
{
	unsigned int spi;
	unsigned long access_mask = 0;
	/*
	 * In order to avoid division, the number of bits per irq is limited
	 * to powers of 2 for the moment.
	 */
	unsigned long irqs_per_reg = 32 >> ffsl(bits_per_irq);
	unsigned long spi_bits = (1 << bits_per_irq) - 1;
	/* First, extract the first interrupt affected by this access */
	unsigned int first_irq = reg_index * irqs_per_reg;

	/* For SGIs or PPIs, let the caller do the mmio access */
	if (!is_spi(first_irq))
		return TRAP_UNHANDLED;

	/* For SPIs, compare against the cell config mask */
	first_irq -= 32;
	for (spi = first_irq; spi < first_irq + irqs_per_reg; spi++) {
		unsigned int bit_nr = (spi - first_irq) * bits_per_irq;
		if (spi_in_cell(cpu_data->cell, spi))
			access_mask |= spi_bits << bit_nr;
	}

	if (!access->is_write) {
		/* Restrict the read value */
		arch_mmio_access(access);
		access->val &= access_mask;
		return TRAP_HANDLED;
	}

	if (!is_poke) {
		/*
		 * Modify the existing value of this register by first reading
		 * it into access->val
		 * Relies on a spinlock since we need two mmio accesses.
		 */
		unsigned long access_val = access->val;

		spin_lock(&dist_lock);

		access->is_write = false;
		arch_mmio_access(access);
		access->is_write = true;

		/* Clear 0 bits */
		access->val &= ~(access_mask & ~access_val);
		access->val |= access_val;
		arch_mmio_access(access);

		spin_unlock(&dist_lock);

		return TRAP_HANDLED;
	} else {
		access->val &= access_mask;
		/* Do the access */
		return TRAP_UNHANDLED;
	}
}

/*
 * GICv3 uses a 64bit register IROUTER for each IRQ
 */
static int handle_irq_route(struct per_cpu *cpu_data,
			    struct mmio_access *access,
			    unsigned int irq)
{
	struct cell *cell = cpu_data->cell;
	unsigned int cpu;

	/* Ignore aff3 on AArch32 (return 0) */
	if (access->size == 4 && (access->addr % 8))
		return TRAP_HANDLED;

	/* SGIs and PPIs are res0 */
	if (!is_spi(irq))
		return TRAP_HANDLED;

	/*
	 * Ignore accesses to SPIs that do not belong to the cell. This isn't
	 * forbidden, because the guest driver may simply iterate over all
	 * registers at initialisation
	 */
	if (!spi_in_cell(cell, irq - 32))
		return TRAP_HANDLED;

	/* Translate the virtual cpu id into the physical one */
	if (access->is_write) {
		access->val = cpu_virt2phys(cell, access->val);
		if (access->val == -1) {
			printk("Attempt to route IRQ%d outside of cell\n", irq);
			return TRAP_FORBIDDEN;
		}
		/* And do the access */
		return TRAP_UNHANDLED;
	} else {
		cpu = readl_relaxed(gicd_base + GICD_IROUTER + 8 * irq);
		access->val = cpu_phys2virt(cpu);
		return TRAP_HANDLED;
	}
}

/*
 * GICv2 uses 8bit values for each IRQ in the ITARGETRs registers
 */
static int handle_irq_target(struct per_cpu *cpu_data,
				 struct mmio_access *access,
				 unsigned int reg)
{
	/*
	 * ITARGETSR contain one byte per IRQ, so the first one affected by this
	 * access corresponds to the reg index
	 */
	unsigned int i, cpu;
	unsigned int spi = reg - 32;
	unsigned int offset;
	u32 access_mask = 0;
	u8 targets;

	/*
	 * Let the guest freely access its SGIs and PPIs, which may be used to
	 * fill its CPU interface map.
	 */
	if (!is_spi(reg))
		return TRAP_UNHANDLED;

	/*
	 * The registers are byte-accessible, extend the access to a word if
	 * necessary.
	 */
	offset = spi % 4;
	access->val <<= 8 * offset;
	access->size = 4;
	spi -= offset;

	for (i = 0; i < 4; i++, spi++) {
		if (spi_in_cell(cpu_data->cell, spi))
			access_mask |= 0xff << (8 * i);
		else
			continue;

		if (!access->is_write)
			continue;

		targets = (access->val >> (8 * i)) & 0xff;

		/* Check that the targeted interface belongs to the cell */
		for (cpu = 0; cpu < 8; cpu++) {
			if (!(targets & target_cpu_map[cpu]))
				continue;

			if (per_cpu(cpu)->cell == cpu_data->cell)
				continue;

			printk("Attempt to route SPI%d outside of cell\n", spi);
			return TRAP_FORBIDDEN;
		}
	}

	if (access->is_write) {
		spin_lock(&dist_lock);
		u32 itargetsr = readl_relaxed(gicd_base + GICD_ITARGETSR + reg
				+ offset);
		access->val &= access_mask;
		/* Combine with external SPIs */
		access->val |= (itargetsr & ~access_mask);
		/* And do the access */
		arch_mmio_access(access);
		spin_unlock(&dist_lock);
	} else {
		arch_mmio_access(access);
		access->val &= access_mask;
	}

	return TRAP_HANDLED;
}

static int handle_sgir_access(struct per_cpu *cpu_data,
			      struct mmio_access *access)
{
	struct sgi sgi;
	unsigned long val = access->val;

	if (!access->is_write)
		return TRAP_HANDLED;

	sgi.targets = (val >> 16) & 0xff;
	sgi.routing_mode = (val >> 24) & 0x3;
	sgi.aff1 = 0;
	sgi.aff2 = 0;
	sgi.aff3 = 0;
	sgi.id = val & 0xf;

	return gic_handle_sgir_write(cpu_data, &sgi, false);
}

/*
 * Get the CPU interface ID for this cpu. It can be discovered by reading
 * the banked value of the PPI and IPI TARGET registers
 * Patch 2bb3135 in Linux explains why the probe may need to scans the first 8
 * registers: some early implementation returned 0 for the first TARGETS
 * registributor.
 * Since those didn't have virtualization extensions, we can safely ignore that
 * case.
 */
int gic_probe_cpu_id(unsigned int cpu)
{
	if (cpu > 8)
		return -EINVAL;

	target_cpu_map[cpu] = readl_relaxed(gicd_base + GICD_ITARGETSR);

	if (target_cpu_map[cpu] == 0)
		return -ENODEV;

	return 0;
}

int gic_handle_sgir_write(struct per_cpu *cpu_data, struct sgi *sgi,
			  bool virt_input)
{
	unsigned int cpu;
	unsigned long targets;
	unsigned int this_cpu = cpu_data->cpu_id;
	struct cell *cell = cpu_data->cell;
	bool is_target = false;

	cpu_data->stats[JAILHOUSE_CPU_STAT_VMEXITS_VSGI]++;

	targets = sgi->targets;
	sgi->targets = 0;

	/* Filter the targets */
	for_each_cpu_except(cpu, cell->cpu_set, this_cpu) {
		/*
		 * When using a cpu map to target the different CPUs (GICv2),
		 * they are independent from the physical CPU IDs, so there is
		 * no need to translate them to the hypervisor's virtual IDs.
		 */
		if (virt_input)
			is_target = !!test_bit(cpu_phys2virt(cpu), &targets);
		else
			is_target = !!(targets & target_cpu_map[cpu]);

		if (sgi->routing_mode == 0 && !is_target)
			continue;

		irqchip_set_pending(per_cpu(cpu), sgi->id, false);
		sgi->targets |= (1 << cpu);
	}

	/* Let the other CPUS inject their SGIs */
	sgi->id = SGI_INJECT;
	irqchip_send_sgi(sgi);

	return TRAP_HANDLED;
}

int gic_handle_dist_access(struct per_cpu *cpu_data,
			   struct mmio_access *access)
{
	int ret;
	unsigned long reg = access->addr - (unsigned long)gicd_base;

	switch (reg) {
	case REG_RANGE(GICD_IROUTER, 1024, 8):
		ret = handle_irq_route(cpu_data, access,
				(reg - GICD_IROUTER) / 8);
		break;

	case REG_RANGE(GICD_ITARGETSR, 1024, 1):
		ret = handle_irq_target(cpu_data, access, reg - GICD_ITARGETSR);
		break;

	case REG_RANGE(GICD_ICENABLER, 32, 4):
	case REG_RANGE(GICD_ISENABLER, 32, 4):
	case REG_RANGE(GICD_ICPENDR, 32, 4):
	case REG_RANGE(GICD_ISPENDR, 32, 4):
	case REG_RANGE(GICD_ICACTIVER, 32, 4):
	case REG_RANGE(GICD_ISACTIVER, 32, 4):
		ret = restrict_bitmask_access(cpu_data, access,
				(reg & 0x7f) / 4, 1, true);
		break;

	case REG_RANGE(GICD_IGROUPR, 32, 4):
		ret = restrict_bitmask_access(cpu_data, access,
				(reg & 0x7f) / 4, 1, false);
		break;

	case REG_RANGE(GICD_ICFGR, 64, 4):
		ret = restrict_bitmask_access(cpu_data, access,
				(reg & 0xff) / 4, 2, false);
		break;

	case REG_RANGE(GICD_IPRIORITYR, 255, 4):
		ret = restrict_bitmask_access(cpu_data, access,
				(reg & 0x3ff) / 4, 8, false);
		break;

	case GICD_SGIR:
		ret = handle_sgir_access(cpu_data, access);
		break;

	case GICD_CTLR:
	case GICD_TYPER:
	case GICD_IIDR:
	case REG_RANGE(GICD_PIDR0, 4, 4):
	case REG_RANGE(GICD_PIDR4, 4, 4):
	case REG_RANGE(GICD_CIDR0, 4, 4):
		/* Allow read access, ignore write */
		ret = (access->is_write ? TRAP_HANDLED : TRAP_UNHANDLED);
		break;

	default:
		/* Ignore access. */
		ret = TRAP_HANDLED;
	}

	/* The sub-handlers return TRAP_UNHANDLED to allow the access */
	if (ret == TRAP_UNHANDLED) {
		arch_mmio_access(access);
		ret = TRAP_HANDLED;
	}

	return ret;
}

void gic_handle_irq(struct per_cpu *cpu_data)
{
	bool handled = false;
	u32 irq_id;

	while (1) {
		/* Read IAR1: set 'active' state */
		irq_id = gic_read_iar();

		if (irq_id == 0x3ff) /* Spurious IRQ */
			break;

		/* Handle IRQ */
		if (is_sgi(irq_id)) {
			arch_handle_sgi(cpu_data, irq_id);
			handled = true;
		} else {
			handled = arch_handle_phys_irq(cpu_data, irq_id);
		}

		/*
		 * Write EOIR1: drop priority, but stay active if handled is
		 * false.
		 * This allows to not be re-interrupted by a level-triggered
		 * interrupt that needs handling in the guest (e.g. timer)
		 */
		irqchip_eoi_irq(irq_id, handled);
	}
}

void gic_target_spis(struct cell *config_cell, struct cell *dest_cell)
{
	unsigned int i, first_cpu, cpu_itf;
	unsigned int shift = 0;
	void *itargetsr = gicd_base + GICD_ITARGETSR;
	u32 targets;
	u32 mask = 0;
	u32 bits = 0;

	/* Always route to the first logical CPU on reset */
	for_each_cpu(first_cpu, dest_cell->cpu_set)
		break;

	cpu_itf = target_cpu_map[first_cpu];

	/* ITARGETSR0-7 contain the PPIs and SGIs, and are read-only. */
	itargetsr += 4 * 8;

	for (i = 0; i < 64; i++, shift = (shift + 8) % 32) {
		if (spi_in_cell(config_cell, i)) {
			mask |= (0xff << shift);
			bits |= (cpu_itf << shift);
		}

		/* ITARGETRs have 4 IRQ per register */
		if ((i + 1) % 4 == 0) {
			targets = readl_relaxed(itargetsr);
			targets &= ~mask;
			targets |= bits;
			writel_relaxed(targets, itargetsr);
			itargetsr += 4;
			mask = 0;
			bits = 0;
		}
	}
}
