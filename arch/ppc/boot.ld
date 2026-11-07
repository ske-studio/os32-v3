/*
 * Copyright (c) 2013 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* OpenBSD b907757a ld.script:19-33; OS32 adds NOTE and bounded stack. */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(7); note PT_NOTE FLAGS(4); }
SECTIONS
{
    . = PM_LINK_BASE;
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .note.ofw : ALIGN(PM_CELL_BYTES) { KEEP(*(.note.ofw)) } :image :note
    .data : { *(.data .data.*) *(.sdata .sdata.*) } :image
    . = ALIGN(PM_CELL_BYTES);
    _edata = .;
    .bss (NOLOAD) : {
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(PM_STACK_ALIGN);
        __stack_bottom = .;
        . += PM_STACK_SIZE;
        __stack_top = .;
        . = ALIGN(PM_CELL_BYTES);
        _end = .;
    } :image
    __image_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note.GNU-stack) }
    ASSERT(__image_end - PM_LINK_BASE < PM_IMAGE_LIMIT, "boot image exceeds 64 KiB")
}
