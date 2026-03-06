ENTRY(_start)

SECTIONS
{
	. = 0x48080000;

	.text : {
		*(.text.boot)
		*(.text*)
	}

	.rodata : {
		*(.rodata*)
		*(.mk.note)
	}

	.data : {
		*(.data*)
	}

	.bss : {
		*(.bss*)
		*(COMMON)
	}
}
