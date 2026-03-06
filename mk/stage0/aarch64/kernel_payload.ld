ENTRY(_start)

SECTIONS
{
	. = 0;

	.text.boot : {
		*(.text.boot)
	}

	/* Mirror an entry trampoline at Linux text_offset for MTK bootrom/lk quirks. */
	. = 0x00080000;
	.text.boot.alias : {
		*(.text.boot.alias)
	}

	.text : {
		*(.text*)
	}

	.rodata : {
		*(.rodata*)
	}

	.data : {
		*(.data*)
	}

	.bss : {
		*(.bss*)
		*(COMMON)
	}

	. = ALIGN(16);
	__mk_stack_bottom = .;
	/* Early payload stack before any runtime is set up. */
	. += 0x4000;
	__mk_stack_top = .;

	_image_end = .;
}
