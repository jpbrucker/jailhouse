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

#ifndef _JAILHOUSE_ASM_IO_H
#define _JAILHOUSE_ASM_IO_H

#include <asm/types.h>

/* AMBA's biosfood */
#define AMBA_DEVICE	0xb105f00d

#ifndef __ASSEMBLY__

static inline void writeb_relaxed(u8 val, volatile void *addr)
{
	*(volatile u8 *)addr = val;
}

static inline void writew_relaxed(u16 val, volatile void *addr)
{
	*(volatile u16 *)addr = val;
}

static inline void writel_relaxed(u32 val, volatile void *addr)
{
	*(volatile u32 *)addr = val;
}

static inline void writeq_relaxed(u64 val, volatile void *addr)
{
	/* Warning: no guarantee of atomicity */
	*(volatile u64 *)addr = val;
}

static inline u8 readb_relaxed(volatile void *addr)
{
	 return *(volatile u8 *)addr;
}

static inline u16 readw_relaxed(volatile void *addr)
{
	 return *(volatile u16 *)addr;
}

static inline u32 readl_relaxed(volatile void *addr)
{
	 return *(volatile u32 *)addr;
}

static inline u64 readq_relaxed(volatile void *addr)
{
	/* Warning: no guarantee of atomicity */
	return *(volatile u64 *)addr;
}

#endif /* !__ASSEMBLY__ */
#endif /* !_JAILHOUSE_ASM_IO_H */
