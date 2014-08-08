#include <linux/types.h>
#include <jailhouse/cell-config.h>

#define ARRAY_SIZE(a) sizeof(a) / sizeof(a[0])

struct {
	struct jailhouse_system header;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[4];
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
			.name = "Odroid-XU Linux",

			.cpu_set_size = sizeof(config.cpus),
			.num_memory_regions = ARRAY_SIZE(config.mem_regions),
			.num_irqchips = 1,
		},
	},

	.cpus = {
		0xf,
	},

	.mem_regions = {
		/* PM something... */ {
			.phys_start = 0x10040000,
			.virt_start = 0x10040000,
			.size = 0x10000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},

		/* MCT */ {
			.phys_start = 0x101c0000,
			.virt_start = 0x101c0000,
			.size = 0x00001000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_DMA,
		},
		/* Various devices */ {
			.phys_start = 0x12000000,
			.virt_start = 0x12000000,
			.size = 0x03000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
		},
		/* RAM */ {
			.phys_start = 0x40000000,
			.virt_start = 0x40000000,
			.size = 0x80000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE,
		},
	},
	.irqchips = {
		/* GIC */ {
			.address = 0x10480000,
			.pin_bitmap = 0xffffffffffffffff,
		},
	},

};
