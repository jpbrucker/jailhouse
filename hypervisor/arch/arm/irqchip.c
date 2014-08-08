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
#include <asm/irqchip.h>
#include <asm/io.h>
#include <asm/platform.h>
#include <asm/setup.h>
#include <asm/sysregs.h>
#include <jailhouse/entry.h>
#include <jailhouse/paging.h>
#include <jailhouse/printk.h>
#include <jailhouse/string.h>

void *gicd_base;
unsigned long gicd_size;

/*
 * The init function must be called after the MMU setup, and whilst in the
 * per-cpu setup, which means that a bool must be set by the master CPU
 */
static bool irqchip_is_init;
static struct irqchip_ops irqchip;

static int irqchip_init_pending(struct per_cpu *cpu_data)
{
	struct pending_irq *pend_array = page_alloc(&mem_pool, 1);

	if (pend_array == NULL)
		return -ENOMEM;
	memset(pend_array, 0, PAGE_SIZE);

	cpu_data->pending_irqs = pend_array;
	cpu_data->first_pending = NULL;

	return 0;
}

/*
 * Find the first available pending struct for insertion. The `prev' pointer is
 * set to the previous pending interrupt, if any, to help inserting the new one
 * into the list.
 * Returns NULL when no slot is available
 */
static struct pending_irq* get_pending_slot(struct per_cpu *cpu_data,
					    struct pending_irq **prev)
{
	u32 i, pending_idx;
	struct pending_irq *pending = cpu_data->first_pending;

	*prev = NULL;

	for (i = 0; i < MAX_PENDING_IRQS; i++) {
		pending_idx = pending - cpu_data->pending_irqs;
		if (pending == NULL || i < pending_idx)
			return cpu_data->pending_irqs + i;

		*prev = pending;
		pending = pending->next;
	}

	return NULL;
}

int irqchip_insert_pending(struct per_cpu *cpu_data, struct pending_irq *irq)
{
	struct pending_irq *prev = NULL;
	struct pending_irq *slot;

	spin_lock(&cpu_data->gic_lock);

	slot = get_pending_slot(cpu_data, &prev);
	if (slot == NULL) {
		spin_unlock(&cpu_data->gic_lock);
		return -ENOMEM;
	}

	/*
	 * Don't override the pointers yet, they may be read by the injection
	 * loop. Odds are astronomically low, but hey.
	 */
	memcpy(slot, irq, sizeof(struct pending_irq) - 2 * sizeof(void *));
	slot->prev = prev;
	if (prev) {
		slot->next = prev->next;
		prev->next = slot;
	} else {
		slot->next = cpu_data->first_pending;
		cpu_data->first_pending = slot;
	}
	if (slot->next)
		slot->next->prev = slot;

	spin_unlock(&cpu_data->gic_lock);

	return 0;
}

/*
 * Only executed by `irqchip_inject_pending' on a CPU to inject its own stuff.
 */
int irqchip_remove_pending(struct per_cpu *cpu_data, struct pending_irq *irq)
{
	spin_lock(&cpu_data->gic_lock);

	if (cpu_data->first_pending == irq)
		cpu_data->first_pending = irq->next;
	if (irq->prev)
		irq->prev->next = irq->next;
	if (irq->next)
		irq->next->prev = irq->prev;

	spin_unlock(&cpu_data->gic_lock);

	return 0;
}

int irqchip_inject_pending(struct per_cpu *cpu_data)
{
	int err;
	struct pending_irq *pending = cpu_data->first_pending;

	while (pending != NULL) {
		err = irqchip.inject_irq(cpu_data, pending);
		if (err == -EBUSY)
			/* The list registers are full. */
			break;
		else
			/*
			 * Removal only changes the pointers, but does not
			 * deallocate anything.
			 * Concurrent accesses are avoided with the spinlock,
			 * but the `next' pointer of the current pending object
			 * may be rewritten by an external insert before or
			 * after this removal, which isn't an issue.
			 */
			irqchip_remove_pending(cpu_data, pending);

		pending = pending->next;
	}

	return 0;
}

void irqchip_handle_irq(struct per_cpu *cpu_data)
{
	irqchip.handle_irq(cpu_data);
}

int irqchip_send_sgi(struct sgi *sgi)
{
	return irqchip.send_sgi(sgi);
}

int irqchip_cpu_init(struct per_cpu *cpu_data)
{
	int err;

	err = irqchip_init_pending(cpu_data);
	if (err)
		return err;

	if (irqchip.cpu_init)
		return irqchip.cpu_init(cpu_data);

	return 0;
}

/* Only the GIC is implemented */
extern struct irqchip_ops gic_irqchip;

int irqchip_init(void)
{
	int i, err;
	u32 pidr2, cidr;
	u32 dev_id = 0;

	/* Only executed on master CPU */
	if (irqchip_is_init)
		return 0;

	/* FIXME: parse device tree */
	gicd_base = GICD_BASE;
	gicd_size = GICD_SIZE;

	if ((err = arch_map_device(gicd_base, gicd_base, gicd_size)) != 0)
		return err;

	for (i = 3; i >= 0; i--) {
		cidr = readl_relaxed(gicd_base + GICD_CIDR0 + i * 4);
		dev_id |= cidr << i * 8;
	}
	if (dev_id != AMBA_DEVICE)
		goto err_no_distributor;

	/* Probe the GIC version */
	pidr2 = readl_relaxed(gicd_base + GICD_PIDR2);
	switch (GICD_PIDR2_ARCH(pidr2)) {
	case 0x2:
		break;
	case 0x3:
	case 0x4:
		memcpy(&irqchip, &gic_irqchip, sizeof(struct irqchip_ops));
		break;
	}

	if (irqchip.init) {
		err = irqchip.init();
		irqchip_is_init = true;

		return err;
	}

err_no_distributor:
	printk("GIC: no distributor found\n");
	arch_unmap_device(gicd_base, gicd_size);

	return -ENODEV;
}
