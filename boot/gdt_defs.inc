; Named constants; C mirrors are checked by test_boot_named_constants.py.
%ifndef OS32_BOOT_GDT_DEFS_INC
%define OS32_BOOT_GDT_DEFS_INC
; Canonical: kernel/gdt.h (first two entries of the boot GDT match).
GDT_KERNEL_CS EQU 0x08
GDT_KERNEL_DS EQU 0x10
%endif
