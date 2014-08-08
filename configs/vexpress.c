#include <linux/types.h>
#include <jailhouse/cell-config.h>

#define ARRAY_SIZE(a) sizeof(a) / sizeof(a[0])

struct {
	struct jailhouse_system header;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[6];
	struct jailhouse_irqchip irqchips[1];
} __attribute__((packed)) config = {
	.header = {
		.hypervisor_memory = {
			.phys_start = 0xa0000000,
			.size = 0x4000000 - 0x2000,
		},
		.config_memory = {
			.phys_start = 0xa3ffe000,
			.size = 0x2000,
		},
		.root_cell = {
			.name = "VExpress Linux",

			.cpu_set_size = sizeof(config.cpus),
			.num_memory_regions = ARRAY_SIZE(config.mem_regions),
			.num_irqchips = 1,
		},
	},

	.cpus = {
		0xf,
	},

	.mem_regions = {
		/* SP810 */ {
			.phys_start = 0x1c020000,
			.virt_start = 0x1c020000,
			.size = 0x00010000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},
		/* Mouse */ {
			.phys_start = 0x1c070000,
			.virt_start = 0x1c070000,
			.size = 0x00010000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},
		/* Keyboard */ {
			.phys_start = 0x1c060000,
			.virt_start = 0x1c060000,
			.size = 0x00010000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},
		/* UARTs */ {
			.phys_start = 0x1c090000,
			.virt_start = 0x1c090000,
			.size = 0x00040000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},
		/* Redistributors (ignore the mmio traps)*/ {
			.phys_start = 0x2f100000,
			.virt_start = 0x2f100000,
			.size = 0x04000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},
		/* RAM */ {
			.phys_start = 0x80000000,
			.virt_start = 0x80000000,
			.size = 0x80000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
		},
	},
	.irqchips = {
		/* GIC */ {
			.address = 0x2f000000,
			.pin_bitmap = 0xffffffffffffffff,
		},
	},

};
