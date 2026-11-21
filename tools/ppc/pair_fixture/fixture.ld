ENTRY(ppc_kernel_entry)
PHDRS { low PT_LOAD FLAGS(5); high PT_LOAD FLAGS(4); }
SECTIONS {
    .pair_low 0x100000 : { *(.pair_low) } :low
    .bss (NOLOAD) : { . += 8; } :low
    .pair_high staging_base : { *(.pair_high) } :high
}
