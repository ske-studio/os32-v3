; Named constants; C mirrors are checked by test_boot_named_constants.py.
%ifndef OS32_BOOT_HEX_DEFS_INC
%define OS32_BOOT_HEX_DEFS_INC
; Canonical widths: lib/kstring.h and include/types.h.
BITS_PER_BYTE EQU 8
HEX_NIBBLE_BITS EQU 4
HEX_NIBBLE_SHIFT EQU 2
HEX32_DIGITS EQU 8
HEX_DIGIT_MASK EQU (1 << HEX_NIBBLE_BITS) - 1
HEX8_DIGITS EQU BITS_PER_BYTE / HEX_NIBBLE_BITS
HEX_ALPHA_BIAS EQU 'A' - '9' - 1
%endif
