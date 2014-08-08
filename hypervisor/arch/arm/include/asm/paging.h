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

#ifndef _JAILHOUSE_ASM_PAGING_H
#define _JAILHOUSE_ASM_PAGING_H

#include <asm/processor.h>
#include <asm/types.h>
#include <jailhouse/utils.h>

#define PAGE_SIZE		4096
#define PAGE_MASK		~(PAGE_SIZE - 1)
#define PAGE_OFFS_MASK		(PAGE_SIZE - 1)

#define MAX_PAGE_DIR_LEVELS	3

/*
 * When T0SZ == 0 and SL0 == 0, the EL2 MMU starts the IPA->PA translation at
 * the level 2 table. The second table is indexed by IPA[31:21], the third one
 * by IPA[20:12].
 * This would allows to cover a 4GB memory map by using 4 concatenated level-2
 * page tables and thus provide better table walk performances.
 * For the moment, the core doesn't allow to use concatenated pages, so we will
 * use three levels instead, starting at level 1.
 *
 * TODO: add a "u32 concatenated" field to the paging struct
 */
#if MAX_PAGE_DIR_LEVELS < 3
#define T0SZ			0
#define SL0			0
#define PADDR_OFF		(14 - T0SZ)
#define L2_VADDR_MASK		BIT_MASK(21, 17 + PADDR_OFF)
#else
#define T0SZ			0
#define SL0			1
#define PADDR_OFF		(5 - T0SZ)
#define L1_VADDR_MASK		BIT_MASK(26 + PADDR_OFF, 30)
#define L2_VADDR_MASK		BIT_MASK(29, 21)
#endif

#define L3_VADDR_MASK		BIT_MASK(20, 12)

/*
 * Stage-1 and Stage-2 lower attributes.
 * FIXME: The upper attributes (contiguous hint and XN) are not currently in
 * use. If needed in the future, they should be shifted towards the lower word,
 * since the core uses unsigned long to pass the flags.
 * An arch-specific typedef for the flags as well as the addresses would be
 * useful.
 * The contiguous bit is a hint that allows the PE to store blocks of 16 pages
 * in the TLB. This may be a useful optimisation.
 */
#define PTE_ACCESS_FLAG		(0x1 << 10)
/*
 * When combining shareability attributes, the stage-1 ones prevail. So we can
 * safely leave everything non-shareable at stage 2.
 */
#define PTE_NON_SHAREABLE	(0x0 << 8)
#define PTE_OUTER_SHAREABLE	(0x2 << 8)
#define PTE_INNER_SHAREABLE	(0x3 << 8)

#define PTE_MEMATTR(val)	((val) << 2)
#define PTE_FLAG_TERMINAL	(0x1 << 1)
#define PTE_FLAG_VALID		(0x1 << 0)

/* These bits differ in stage 1 and 2 translations */
#define S1_PTE_NG		(0x1 << 11)
#define S1_PTE_ACCESS_RW	(0x0 << 7)
#define S1_PTE_ACCESS_RO	(0x1 << 7)
/* Res1 for EL2 stage-1 tables */
#define S1_PTE_ACCESS_EL0	(0x1 << 6)

#define S2_PTE_ACCESS_RO	(0x1 << 6)
#define S2_PTE_ACCESS_WO	(0x2 << 6)
#define S2_PTE_ACCESS_RW	(0x3 << 6)

/*
 * Descriptor pointing to a page table
 * (only for L1 and L2. L3 uses this encoding for terminal entries...)
 */
#define PTE_TABLE_FLAGS		0x3

#define PTE_L1_BLOCK_ADDR_MASK	BIT_MASK(39, 30)
#define PTE_L2_BLOCK_ADDR_MASK	BIT_MASK(39, 21)
#define PTE_TABLE_ADDR_MASK	BIT_MASK(39, 12)
#define PTE_PAGE_ADDR_MASK	BIT_MASK(39, 12)

#define BLOCK_1G_VADDR_MASK	BIT_MASK(29, 0)
#define BLOCK_2M_VADDR_MASK	BIT_MASK(20, 0)

#define TTBR_MASK		BIT_MASK(47, PADDR_OFF)
#define VTTBR_VMID_SHIFT	48

#define HTCR_RES1		((1 << 31) | (1 << 23))
#define VTCR_RES1		((1 << 31))
#define TCR_RGN_NON_CACHEABLE	0x0
#define TCR_RGN_WB_WA		0x1
#define TCR_RGN_WT		0x2
#define TCR_RGN_WB		0x3
#define TCR_NON_SHAREABLE	0x0
#define TCR_OUTER_SHAREABLE	0x2
#define TCR_INNER_SHAREABLE	0x3

#define TCR_SH0_SHIFT		12
#define TCR_ORGN0_SHIFT		10
#define TCR_IRGN0_SHIFT		8
#define TCR_SL0_SHIFT		6
#define TCR_S_SHIFT		4

/*
 * Memory attribute indexes:
 *   0: normal WB, RA, WA, non-transient
 *   1: dev-nGnRE
 *   2: normal non-cacheable
 *   3: normal WT, RA, transient
 *   4: normal WB, WA, non-transient
 *   5: normal WB, RA, non-transient
 *   6: dev-nGnRnE
 *   7: dev-nGnRnE (unused)
 */
#define MEMATTR_WBRAWA		0xff
#define MEMATTR_DEV_nGnRE	0x04
#define MEMATTR_NC		0x44
#define MEMATTR_WTRA		0xaa
#define MEMATTR_WBWA		0x55
#define MEMATTR_WBRA		0xee
#define MEMATTR_DEV_nGnRnE	0x00

#define DEFAULT_HMAIR0		0xaa4404ff
#define DEFAULT_HMAIR1		0x0000ee55
#define HMAIR_IDX_WBRAWA	0
#define HMAIR_IDX_DEV_nGnRE	1
#define HMAIR_IDX_NC		2
#define HMAIR_IDX_WTRA		3
#define HMAIR_IDX_WBWA		4
#define HMAIR_IDX_WBRA		5
#define HMAIR_IDX_DEV_nGnRnE	6


#define S1_PTE_FLAG_NORMAL	PTE_MEMATTR(HMAIR_IDX_WBRAWA)
#define S1_PTE_FLAG_DEVICE	PTE_MEMATTR(HMAIR_IDX_DEV_nGnRE)
#define S1_PTE_FLAG_UNCACHED	PTE_MEMATTR(HMAIR_IDX_NC)

#define S2_PTE_FLAG_NORMAL	PTE_MEMATTR(MEMATTR_WBRAWA)
#define S2_PTE_FLAG_DEVICE	PTE_MEMATTR(MEMATTR_DEV_nGnRE)
#define S2_PTE_FLAG_NC		PTE_MEMATTR(MEMATTR_NC)

#define S1_DEFAULT_FLAGS	(PTE_FLAG_VALID | PTE_ACCESS_FLAG	\
				| S1_PTE_FLAG_NORMAL | PTE_INNER_SHAREABLE\
				| S1_PTE_ACCESS_EL0)

/* Macros used by the core, only for the EL2 stage-1 mappings */
#define PAGE_FLAG_UNCACHED	S1_PTE_FLAG_NC
#define PAGE_DEFAULT_FLAGS	(S1_DEFAULT_FLAGS | S1_PTE_ACCESS_RW)
#define PAGE_READONLY_FLAGS	(S1_DEFAULT_FLAGS | S1_PTE_ACCESS_RO)
#define PAGE_NONPRESENT_FLAGS	0

#define INVALID_PHYS_ADDR	(~0UL)

#define REMAP_BASE		0x00100000UL
#define NUM_REMAP_BITMAP_PAGES	1

#define NUM_TEMPORARY_PAGES	16

#ifndef __ASSEMBLY__

typedef u64 *pt_entry_t;

static inline void arch_tlb_flush_page(unsigned long addr)
{
}

static inline void flush_cache(void *addr, long size)
{
}

#endif /* !__ASSEMBLY__ */

#endif /* !_JAILHOUSE_ASM_PAGING_H */
