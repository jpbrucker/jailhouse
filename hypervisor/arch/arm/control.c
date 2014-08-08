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
#include <asm/irqchip.h>
#include <asm/processor.h>
#include <asm/sysregs.h>
#include <asm/traps.h>
#include <jailhouse/control.h>
#include <jailhouse/printk.h>
#include <jailhouse/processor.h>
#include <jailhouse/string.h>

static void arch_reset_el1(struct registers *regs)
{
	u32 sctlr;

	/* Wipe all banked and usr regs */
	memset(regs, 0, sizeof(struct registers));

	arm_write_banked_reg(SP_usr, 0);
	arm_write_banked_reg(SP_svc, 0);
	arm_write_banked_reg(SP_abt, 0);
	arm_write_banked_reg(SP_und, 0);
	arm_write_banked_reg(SP_svc, 0);
	arm_write_banked_reg(SP_irq, 0);
	arm_write_banked_reg(SP_fiq, 0);
	arm_write_banked_reg(LR_svc, 0);
	arm_write_banked_reg(LR_abt, 0);
	arm_write_banked_reg(LR_und, 0);
	arm_write_banked_reg(LR_svc, 0);
	arm_write_banked_reg(LR_irq, 0);
	arm_write_banked_reg(LR_fiq, 0);
	arm_write_banked_reg(R8_fiq, 0);
	arm_write_banked_reg(R9_fiq, 0);
	arm_write_banked_reg(R10_fiq, 0);
	arm_write_banked_reg(R11_fiq, 0);
	arm_write_banked_reg(R12_fiq, 0);
	arm_write_banked_reg(SPSR_svc, 0);
	arm_write_banked_reg(SPSR_abt, 0);
	arm_write_banked_reg(SPSR_und, 0);
	arm_write_banked_reg(SPSR_svc, 0);
	arm_write_banked_reg(SPSR_irq, 0);
	arm_write_banked_reg(SPSR_fiq, 0);

	/* Wipe the system registers */
	arm_read_sysreg(SCTLR_EL1, sctlr);
	sctlr = sctlr & ~SCTLR_MASK;
	arm_write_sysreg(SCTLR_EL1, sctlr);
	arm_write_sysreg(ACTLR_EL1, 0);
	arm_write_sysreg(CPACR_EL1, 0);
	arm_write_sysreg(CONTEXTIDR_EL1, 0);
	arm_write_sysreg(PAR_EL1, 0);
	arm_write_sysreg(TTBR0_EL1, 0);
	arm_write_sysreg(TTBR1_EL1, 0);
	arm_write_sysreg(CSSELR_EL1, 0);

	arm_write_sysreg(CNTKCTL_EL1, 0);
	arm_write_sysreg(CNTP_CTL_EL0, 0);
	arm_write_sysreg(CNTP_CVAL_EL0, 0);
	arm_write_sysreg(CNTV_CTL_EL0, 0);
	arm_write_sysreg(CNTV_CVAL_EL0, 0);

	/* AArch32 specific */
	arm_write_sysreg(TTBCR, 0);
	arm_write_sysreg(DACR, 0);
	arm_write_sysreg(VBAR, 0);
	arm_write_sysreg(DFSR, 0);
	arm_write_sysreg(DFAR, 0);
	arm_write_sysreg(IFSR, 0);
	arm_write_sysreg(IFAR, 0);
	arm_write_sysreg(ADFSR, 0);
	arm_write_sysreg(AIFSR, 0);
	arm_write_sysreg(MAIR0, 0);
	arm_write_sysreg(MAIR1, 0);
	arm_write_sysreg(AMAIR0, 0);
	arm_write_sysreg(AMAIR1, 0);
	arm_write_sysreg(TPIDRURW, 0);
	arm_write_sysreg(TPIDRURO, 0);
	arm_write_sysreg(TPIDRPRW, 0);
}

static void arch_reset_self(struct per_cpu *cpu_data)
{
	int err;
	unsigned long reset_address;
	struct cell *cell = cpu_data->cell;
	struct registers *regs = guest_regs(cpu_data);

	err = arch_mmu_cpu_cell_init(cpu_data);
	if (err)
		printk("MMU setup failed\n");
	/*
	 * On the first CPU to reach this, write all cell datas to memory so it
	 * can be started with caches disabled.
	 * On all CPUs, invalidate the instruction caches to take into account
	 * the potential new instructions.
	 */
	arch_cell_caches_flush(cell);

	/*
	 * We come from the IRQ handler, but we won't return there, so the IPI
	 * is deactivated here.
	 */
	irqchip_eoi_irq(SGI_CPU_OFF, true);

	err = irqchip_cpu_reset(cpu_data);
	if (err)
		printk("IRQ setup failed\n");

	if (cpu_data->cell == &root_cell)
		/* Wait for the driver to call cpu_up */
		reset_address = arch_cpu_spin();
	else
		reset_address = 0;

	/* Restore an empty context */
	arch_reset_el1(regs);

	arm_write_banked_reg(ELR_hyp, reset_address);
	arm_write_banked_reg(SPSR_hyp, RESET_PSR);

	vmreturn(regs);
}

static void arch_suspend_self(struct per_cpu *cpu_data)
{
	psci_suspend(cpu_data);

	if (cpu_data->cell_pages_dirty)
		arch_cpu_tlb_flush(cpu_data);
}

static void arch_dump_exit(const char *reason)
{
	unsigned long pc;

	arm_read_banked_reg(ELR_hyp, pc);
	printk("Unhandled HYP %s exit at 0x%x\n", reason, pc);
}

static void arch_dump_abt(bool is_data)
{
	u32 hxfar;
	u32 esr;

	arm_read_sysreg(ESR_EL2, esr);
	if (is_data)
		arm_read_sysreg(HDFAR, hxfar);
	else
		arm_read_sysreg(HIFAR, hxfar);

	printk("  paddr=0x%lx esr=0x%x\n", hxfar, esr);
}

struct registers* arch_handle_exit(struct per_cpu *cpu_data,
				   struct registers *regs)
{
	switch (regs->exit_reason) {
	case EXIT_REASON_IRQ:
		irqchip_handle_irq(cpu_data);
		break;
	case EXIT_REASON_TRAP:
		arch_handle_trap(cpu_data, regs);
		break;

	case EXIT_REASON_UNDEF:
		arch_dump_exit("undef");
		panic_stop(cpu_data);
	case EXIT_REASON_DABT:
		arch_dump_exit("data abort");
		arch_dump_abt(true);
		panic_stop(cpu_data);
	case EXIT_REASON_PABT:
		arch_dump_exit("prefetch abort");
		arch_dump_abt(false);
		panic_stop(cpu_data);
	case EXIT_REASON_HVC:
		arch_dump_exit("hvc");
		panic_stop(cpu_data);
	case EXIT_REASON_FIQ:
		arch_dump_exit("fiq");
		panic_stop(cpu_data);
	default:
		arch_dump_exit("unknown");
		panic_stop(cpu_data);
	}

	return regs;
}

/* CPU must be stopped */
void arch_resume_cpu(unsigned int cpu_id)
{
	/*
	 * Simply get out of the spin loop by returning to handle_sgi
	 * If the CPU is being reset, it already has left the PSCI idle loop.
	 */
	if (psci_cpu_stopped(cpu_id))
		psci_resume(cpu_id);
}

/* CPU must be stopped */
void arch_park_cpu(unsigned int cpu_id)
{
	struct per_cpu *cpu_data = per_cpu(cpu_id);

	/*
	 * Reset always follows park_cpu, so we just need to make sure that the
	 * CPU is suspended
	 */
	if (psci_wait_cpu_stopped(cpu_id) != 0)
		printk("ERROR: CPU%d is supposed to be stopped\n", cpu_id);
	else
		cpu_data->cell->arch.needs_flush = true;
}

/* CPU must be stopped */
void arch_reset_cpu(unsigned int cpu_id)
{
	unsigned long cpu_data = (unsigned long)per_cpu(cpu_id);

	if (psci_cpu_on(cpu_id, (unsigned long)arch_reset_self, cpu_data))
		printk("ERROR: unable to reset CPU%d (was running)\n", cpu_id);
}

void arch_suspend_cpu(unsigned int cpu_id)
{
	struct sgi sgi;

	if (psci_cpu_stopped(cpu_id) != 0)
		return;

	sgi.routing_mode = 0;
	sgi.aff1 = 0;
	sgi.aff2 = 0;
	sgi.aff3 = 0;
	sgi.targets = 1 << cpu_id;
	sgi.id = SGI_CPU_OFF;

	irqchip_send_sgi(&sgi);
}

void arch_handle_sgi(struct per_cpu *cpu_data, u32 irqn)
{
	switch (irqn) {
	case SGI_INJECT:
		irqchip_inject_pending(cpu_data);
		break;
	case SGI_CPU_OFF:
		arch_suspend_self(cpu_data);
		break;
	default:
		printk("WARN: unknown SGI received %d\n", irqn);
	}
}

int arch_cell_create(struct per_cpu *cpu_data, struct cell *cell)
{
	int err;

	err = arch_mmu_cell_init(cell);
	if (err)
		return err;

	return 0;
}

void arch_cell_destroy(struct per_cpu *cpu_data, struct cell *cell)
{
	unsigned int cpu;

	arch_mmu_cell_destroy(cell);

	for_each_cpu(cpu, cell->cpu_set)
		arch_reset_cpu(cpu);
}

void arch_config_commit(struct per_cpu *cpu_data,
			struct cell *cell_added_removed)
{
	unsigned int cpu;

	/*
	 * Reconfiguration of the page tables is done while the cells are
	 * spinning. They will need to flush their TLBs right after they are
	 * resumed.
	 * When init_late calls arch_config_commit, the root cell's bitmap has
	 * not yet been populated by register_root_cpu, so the only invalidated
	 * TLBs are those of the master CPU.
	 */
	for_each_cpu_except(cpu, root_cell.cpu_set, cpu_data->cpu_id)
		per_cpu(cpu)->cell_pages_dirty = true;

	if (cell_added_removed) {
		for_each_cpu_except(cpu, cell_added_removed->cpu_set,
				    cpu_data->cpu_id)
			per_cpu(cpu)->cell_pages_dirty = true;
	}

	arch_cpu_tlb_flush(cpu_data);
}

void arch_panic_stop(struct per_cpu *cpu_data)
{
	psci_cpu_off(cpu_data);
	__builtin_unreachable();
}

void arch_panic_halt(struct per_cpu *cpu_data)
{
	/* Won't return to panic_halt */
	if (phys_processor_id() == panic_cpu)
		panic_in_progress = 0;

	psci_cpu_off(cpu_data);
	__builtin_unreachable();
}
