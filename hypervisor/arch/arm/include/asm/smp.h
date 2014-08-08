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

#ifndef JAILHOUSE_ASM_SMP_H_
#define JAILHOUSE_ASM_SMP_H_

#ifndef __ASSEMBLY__

enum smp_type {
	SMP_PSCI,
	SMP_SPIN
};

struct mmio_access;
struct per_cpu;
struct cell;

struct smp_ops {
	enum smp_type type;
	int (*init)(struct cell *cell);

	/*
	 * Uses the MMIO trap interface:
	 * returns TRAP_HANDLED when the mailbox is targeted, or else
	 * TRAP_UNHANDLED.
	 */
	int (*mmio_handler)(struct per_cpu *cpu_data,
			    struct mmio_access *access);
	/* Returns an address */
	unsigned long (*cpu_spin)(struct per_cpu *cpu_data);
};

int arch_generic_smp_init(unsigned long mbox);
int arch_generic_smp_mmio(struct per_cpu *cpu_data, struct mmio_access *access,
			  unsigned long mbox);
unsigned long arch_generic_smp_spin(unsigned long mbox);

int arch_smp_mmio_access(struct per_cpu *cpu_data, struct mmio_access *access);
unsigned long arch_smp_spin(struct per_cpu *cpu_data, struct smp_ops *ops);
void register_smp_ops(struct cell *cell);

#endif /* !__ASSEMBLY__ */
#endif /* !JAILHOUSE_ASM_SMP_H_ */
