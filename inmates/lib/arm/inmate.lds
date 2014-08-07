SECTIONS {
	. = 0;
	.boot		: { *(.boot) }

	. = ALIGN(4096);
	. = . + 0x1000;
	stack_top = .;
	bss_start = .;
	.bss		: {
		*(.bss)
		*(COMMON)
	}

	. = ALIGN(4);
	.text		: {
		*(.text)
	}

	.rodata		: {
		*(.rodata)
	}

	.data		: {
		*(.data)
	}
}
