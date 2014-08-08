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

#include <asm/io.h>
#include <asm/irqchip.h>
#include <asm/processor.h>
#include <asm/smp.h>
#include <asm/traps.h>

/* Taken from the ARM ARM pseudocode for taking a data abort */
static void arch_inject_dabt(struct trap_context *ctx, unsigned long addr)
{
	unsigned int lr_offset;
	unsigned long vbar;
	bool is_thumb;
	u32 sctlr, ttbcr;

	arm_read_sysreg(SCTLR_EL1, sctlr);
	arm_read_sysreg(TTBCR, ttbcr);

	/* Set cpsr */
	is_thumb = ctx->cpsr & PSR_T_BIT;
	ctx->cpsr &= ~(PSR_MODE_MASK | PSR_IT_MASK(0xff) | PSR_T_BIT
			| PSR_J_BIT | PSR_E_BIT);
	ctx->cpsr |= (PSR_ABT_MODE | PSR_I_BIT | PSR_A_BIT);
	if (sctlr & SCTLR_TE_BIT)
		ctx->cpsr |= PSR_T_BIT;
	if (sctlr & SCTLR_EE_BIT)
		ctx->cpsr |= PSR_E_BIT;

	lr_offset = (is_thumb ? 4 : 0);
	arm_write_banked_reg(LR_abt, ctx->pc + lr_offset);

	/* Branch to dabt vector */
	if (sctlr & SCTLR_V_BIT)
		vbar = 0xffff0000;
	else
		arm_read_sysreg(VBAR, vbar);
	ctx->pc = vbar + 0x10;

	/* Signal a debug fault. DFSR layout depends on the LPAE bit */
	if (ttbcr >> 31)
		arm_write_sysreg(DFSR, (1 << 9) | 0x22);
	else
		arm_write_sysreg(DFSR, 0x2);
	arm_write_sysreg(DFAR, addr);
}

int arch_mmio_access(struct mmio_access *access)
{
	void *addr = (void *)access->addr;

	if (access->is_write) {
		switch (access->size) {
		case 1:
			writeb_relaxed(access->val, addr);
			break;
		case 2:
			writew_relaxed(access->val, addr);
			break;
		case 4:
			writel_relaxed(access->val, addr);
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (access->size) {
		case 1:
			access->val = readb_relaxed(addr);
			break;
		case 2:
			access->val = readw_relaxed(addr);
			break;
		case 4:
			access->val = readl_relaxed(addr);
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

int arch_handle_dabt(struct per_cpu *cpu_data, struct trap_context *ctx)
{
	struct mmio_access access;
	unsigned long hpfar;
	unsigned long hdfar;
	int ret		= TRAP_UNHANDLED;
	/* Decode the syndrome fields */
	u32 icc		= ESR_ICC(ctx->esr);
	u32 isv		= icc >> 24;
	u32 sas		= icc >> 22 & 0x3;
	u32 sse		= icc >> 21 & 0x1;
	u32 srt		= icc >> 16 & 0xf;
	u32 ea		= icc >> 9 & 0x1;
	u32 cm		= icc >> 8 & 0x1;
	u32 s1ptw	= icc >> 7 & 0x1;
	u32 is_write	= icc >> 6 & 0x1;
	u32 size	= 1 << sas;

	arm_read_sysreg(HPFAR, hpfar);
	arm_read_sysreg(HDFAR, hdfar);
	access.addr = hpfar << 8;
	access.addr |= hdfar & 0xfff;

	cpu_data->stats[JAILHOUSE_CPU_STAT_VMEXITS_MMIO]++;

	/*
	 * Invalid instruction syndrome means multiple access or writeback, there
	 * is nothing we can do.
	 */
	if (!isv || size > sizeof(unsigned long))
		goto error_unhandled;

	/* Re-inject abort during page walk, cache maintenance or external */
	if (s1ptw || ea || cm) {
		arch_inject_dabt(ctx, hdfar);
		return TRAP_HANDLED;
	}

	if (is_write) {
		/* Load the value to write from the src register */
		access_cell_reg(ctx, srt, &access.val, true);
		if (sse)
			access.val = sign_extend(access.val, 8 * size);
	} else {
		access.val = 0;
	}
	access.is_write = is_write;
	access.size = size;

	ret = irqchip_mmio_access(cpu_data, &access);
	if (ret == TRAP_UNHANDLED)
		ret = arch_smp_mmio_access(cpu_data, &access);

	if (ret == TRAP_HANDLED) {
		/* Put the read value into the dest register */
		if (!is_write) {
			if (sse)
				access.val = sign_extend(access.val, 8 * size);
			access_cell_reg(ctx, srt, &access.val, false);
		}

		arch_skip_instruction(ctx);
	}

	if (ret != TRAP_UNHANDLED)
		return ret;

error_unhandled:
	panic_printk("Unhandled data %s at 0x%x(%d)\n",
		(is_write ? "write" : "read"), access.addr, size);

	return ret;
}
