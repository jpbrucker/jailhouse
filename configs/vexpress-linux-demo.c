#include <linux/types.h>
#include <jailhouse/cell-config.h>

#define ARRAY_SIZE(a) sizeof(a) / sizeof(a[0])

struct {
	struct jailhouse_cell_desc cell;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[2];
	struct jailhouse_irqchip irqchips[1];
} __attribute__((packed)) config = {
	.cell = {
		.name = "linux-demo",
		.flags = JAILHOUSE_CELL_PASSIVE_COMMREG,

		.cpu_set_size = sizeof(config.cpus),
		.num_memory_regions = ARRAY_SIZE(config.mem_regions),
		.num_irqchips = 1,
		.pio_bitmap_size = 0,
		.num_pci_devices = 0,
	},

	.cpus = {
		0xc,
	},

	.mem_regions = {
		/* UART 3 */ {
			.phys_start = 0x1c0c0000,
			.virt_start = 0x1c090000,
			.size = 0x10000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},
		/* RAM load */ {
			.phys_start = 0xa6000000,
			.virt_start = 0x00000000,
			.size = 0x10000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_LOADABLE,
		},
	},

	.irqchips = {
		/* GIC */ {
			.address = 0x2f000000,
			.pin_bitmap = 0x0000000000000100,
		},
	}
};
