; Named constants; checked by test_boot_named_constants.py.
%ifndef OS32_BOOT_LZ4_DEFS_INC
%define OS32_BOOT_LZ4_DEFS_INC
; LZ4 block token/offset format; boot/lz4_mini.c uses the same format.
LZ4_TOKEN_BITS EQU 4
LZ4_LENGTH_MASK EQU (1 << LZ4_TOKEN_BITS) - 1
LZ4_OFFSET_BYTES EQU 2
BOOT_LZ4_ERR_OUTPUT EQU -2
%endif
