; Named constants; checked by test_boot_named_constants.py.
%ifndef OS32_BOOT_CRC32_DEFS_INC
%define OS32_BOOT_CRC32_DEFS_INC
; Canonical internal initial state: lib/crc32_core.inc.
CRC32_INIT EQU 0xFFFFFFFF
CRC32_NIBBLE_BITS EQU 4
CRC32_NIBBLE_MASK EQU (1 << CRC32_NIBBLE_BITS) - 1
CRC32_TABLE_WORD_BYTES EQU 4
%endif
