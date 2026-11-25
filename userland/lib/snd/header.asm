bits 32
%include "sdk/crt/generations.inc"
section .shlib_hdr alloc align=4
global __os32_shlib_header
extern __shlib_data_start, __shlib_data_pages, __shlib_text_pages
__os32_shlib_header:
    dd 0x42494c53, 1, (entries_end - entries) / 4
    dd __shlib_data_start, __shlib_data_pages, __shlib_text_pages, 0, 0
entries:
%macro SND_ENTRY 1
    extern %1
    dd %1
%endmacro
%include "userland/lib/snd/entries.inc"
entries_end:
