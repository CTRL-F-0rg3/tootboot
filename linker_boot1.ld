OUTPUT_FORMAT(binary)
ENTRY(boot1_entry_point)

SECTIONS {
    . = 0x7E00;
    .boot1_entry  : { KEEP(*(.boot1_entry)) }
    .text         : { *(.text*)             }
    .rodata       : { *(.rodata*)           }
    .data         : { *(.data*)             }
    .bss          : { *(.bss*) *(COMMON)    }

    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame*) *(.gcc_except_table*)
    }
}
