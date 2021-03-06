/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) Siemens AG, 2014
 *
 * Authors:
 *  Ivan Kolchin <ivan.kolchin@siemens.com>
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_ASM_PCI_H
#define _JAILHOUSE_ASM_PCI_H

#include <asm/percpu.h>
#include <asm/types.h>

/* --- PCI configuration ports --- */
#define PCI_REG_ADDR_PORT		0xcf8
#define PCI_REG_DATA_PORT		0xcfc

/* --- Address register fields --- */
#define PCI_ADDR_ENABLE			(1UL << 31)
#define PCI_ADDR_BDF_SHIFT		8
#define PCI_ADDR_REGNUM_MASK		BIT_MASK(7, 2)

int x86_pci_config_handler(struct registers *guest_regs, struct cell *cell,
			   u16 port, bool dir_in, unsigned int size);

#endif /* !_JAILHOUSE_ASM_PCI_H */
