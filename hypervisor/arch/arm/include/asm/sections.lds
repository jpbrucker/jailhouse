
. = ALIGN(0x1000);
.trampoline : {
		trampoline_start = .;
		*(.trampoline)
		trampoline_end = .;
}
