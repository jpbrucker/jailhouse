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

#include <jailhouse/paging.h>

static bool arm_entry_valid(pt_entry_t entry)
{
	return *entry & 1;
}

static unsigned long arm_get_entry_flags(pt_entry_t entry)
{
	/* Upper flags (contiguous hint and XN are currently ignored */
	return *entry & 0xfff;
}

static void arm_clear_entry(pt_entry_t entry)
{
	*entry = 0;
}

static bool arm_page_table_empty(page_table_t page_table)
{
	unsigned long n;
	pt_entry_t pte;

	for (n = 0, pte = page_table; n < PAGE_SIZE / sizeof(pt_entry_t); n++, pte++)
		if (arm_entry_valid(pte))
			return false;
	return true;
}

#if MAX_PAGE_DIR_LEVELS > 2
static pt_entry_t arm_get_l1_entry(page_table_t page_table, unsigned long virt)
{
	return &page_table[(virt & L1_VADDR_MASK) >> 30];
}

static void arm_set_l1_block(pt_entry_t pte, unsigned long phys, unsigned long flags)
{
	*pte = ((u64)phys & PTE_L1_BLOCK_ADDR_MASK) | flags;
}

static unsigned long arm_get_l1_phys(pt_entry_t pte, unsigned long virt)
{
	if ((*pte & PTE_TABLE_FLAGS) == PTE_TABLE_FLAGS)
		return INVALID_PHYS_ADDR;
	return (*pte & PTE_L1_BLOCK_ADDR_MASK) | (virt & BLOCK_1G_VADDR_MASK);
}
#endif

static pt_entry_t arm_get_l2_entry(page_table_t page_table, unsigned long virt)
{
	return &page_table[(virt & L2_VADDR_MASK) >> 21];
}

static pt_entry_t arm_get_l3_entry(page_table_t page_table, unsigned long virt)
{
	return &page_table[(virt & L3_VADDR_MASK) >> 12];
}

static void arm_set_l2_block(pt_entry_t pte, unsigned long phys, unsigned long flags)
{
	*pte = ((u64)phys & PTE_L2_BLOCK_ADDR_MASK) | flags;
}

static void arm_set_l3_page(pt_entry_t pte, unsigned long phys, unsigned long flags)
{
	*pte = ((u64)phys & PTE_PAGE_ADDR_MASK) | flags | PTE_FLAG_TERMINAL;
}

static void arm_set_l12_table(pt_entry_t pte, unsigned long next_pt)
{
	*pte = ((u64)next_pt & PTE_TABLE_ADDR_MASK) | PTE_TABLE_FLAGS;
}

static unsigned long arm_get_l12_table(pt_entry_t pte)
{
	return *pte & PTE_TABLE_ADDR_MASK;
}

static unsigned long arm_get_l2_phys(pt_entry_t pte, unsigned long virt)
{
	if ((*pte & PTE_TABLE_FLAGS) == PTE_TABLE_FLAGS)
		return INVALID_PHYS_ADDR;
	return (*pte & PTE_L2_BLOCK_ADDR_MASK) | (virt & BLOCK_2M_VADDR_MASK);
}

static unsigned long arm_get_l3_phys(pt_entry_t pte, unsigned long virt)
{
	if (!(*pte & PTE_FLAG_TERMINAL))
		return INVALID_PHYS_ADDR;
	return (*pte & PTE_PAGE_ADDR_MASK) | (virt & PAGE_MASK);
}

#define ARM_PAGING_COMMON				\
		.entry_valid = arm_entry_valid,		\
		.get_flags = arm_get_entry_flags,	\
		.clear_entry = arm_clear_entry,		\
		.page_table_empty = arm_page_table_empty,

const struct paging arm_paging[] = {
#if MAX_PAGE_DIR_LEVELS > 2
	{
		ARM_PAGING_COMMON
		/* Block entry: 1GB */
		.page_size = 1024 * 1024 * 1024,
		.get_entry = arm_get_l1_entry,
		.set_terminal = arm_set_l1_block,
		.get_phys = arm_get_l1_phys,

		.set_next_pt = arm_set_l12_table,
		.get_next_pt = arm_get_l12_table,
	},
#endif
	{
		ARM_PAGING_COMMON
		/* Block entry: 2MB */
		.page_size = 2 * 1024 * 1024,
		.get_entry = arm_get_l2_entry,
		.set_terminal = arm_set_l2_block,
		.get_phys = arm_get_l2_phys,

		.set_next_pt = arm_set_l12_table,
		.get_next_pt = arm_get_l12_table,
	},
	{
		ARM_PAGING_COMMON
		/* Page entry: 4kB */
		.page_size = 4 * 1024,
		.get_entry = arm_get_l3_entry,
		.set_terminal = arm_set_l3_page,
		.get_phys = arm_get_l3_phys,
	}
};

void arch_paging_init(void)
{
}
