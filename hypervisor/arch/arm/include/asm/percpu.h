/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) Siemens AG, 2013
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_ASM_PERCPU_H
#define _JAILHOUSE_ASM_PERCPU_H

#include <asm/types.h>
#include <asm/paging.h>

#define NUM_ENTRY_REGS			13

/* Keep in sync with struct per_cpu! */
#define PERCPU_SIZE_SHIFT		13
#define PERCPU_STACK_END		PAGE_SIZE
#define PERCPU_LINUX_SP			PERCPU_STACK_END

#ifndef __ASSEMBLY__

#include <asm/cell.h>
#include <asm/psci.h>
#include <asm/spinlock.h>
#include <jailhouse/control.h>

struct pending_irq;

struct per_cpu {
	/* Keep these two in sync with defines above! */
	u8 stack[PAGE_SIZE];
	unsigned long linux_sp;
	unsigned long linux_ret;
	unsigned long linux_flags;
	unsigned long linux_reg[NUM_ENTRY_REGS];

	unsigned int cpu_id;
	unsigned int virt_id;

	/* Other CPUs can insert sgis into the pending array */
	spinlock_t gic_lock;
	struct pending_irq *pending_irqs;
	struct pending_irq *first_pending;
	/* Only GICv3: redistributor base */
	void *gicr_base;

	struct cell *cell;

	u32 stats[JAILHOUSE_NUM_CPU_STATS];

	bool initialized;

	/* The mbox will be accessed with a ldrd, which requires alignment */
	__attribute__((aligned(8))) struct psci_mbox psci_mbox;
	struct psci_mbox guest_mbox;

	bool cpu_stopped;
	bool cell_pages_dirty;
	int shutdown_state;
	bool failed;
} __attribute__((aligned(PAGE_SIZE)));

static inline struct per_cpu *per_cpu(unsigned int cpu)
{
	extern u8 __page_pool[];

	return (struct per_cpu *)(__page_pool + (cpu << PERCPU_SIZE_SHIFT));
}

static inline struct registers *guest_regs(struct per_cpu *cpu_data)
{
	/* Assumes that the trap handler is entered with an empty stack */
	return (struct registers *)(cpu_data->stack + PERCPU_STACK_END
			- sizeof(struct registers));
}

static inline unsigned int cpu_phys2virt(unsigned int cpu_id)
{
	return per_cpu(cpu_id)->virt_id;
}

static inline unsigned int cpu_virt2phys(struct cell *cell, unsigned int virt_id)
{
	unsigned int cpu;

	for_each_cpu(cpu, cell->cpu_set) {
		if (per_cpu(cpu)->virt_id == virt_id)
			return cpu;
	}

	return -1;
}

/* Validate defines */
#define CHECK_ASSUMPTION(assume)	((void)sizeof(char[1 - 2*!(assume)]))

static inline void __check_assumptions(void)
{
	struct per_cpu cpu_data;

	CHECK_ASSUMPTION(sizeof(struct per_cpu) == (1 << PERCPU_SIZE_SHIFT));
	CHECK_ASSUMPTION(sizeof(cpu_data.stack) == PERCPU_STACK_END);
	CHECK_ASSUMPTION(__builtin_offsetof(struct per_cpu, linux_sp) ==
			 PERCPU_LINUX_SP);
}
#endif /* !__ASSEMBLY__ */

#endif /* !_JAILHOUSE_ASM_PERCPU_H */
