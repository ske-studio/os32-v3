; Named constants; C mirrors are checked by test_boot_named_constants.py.
%ifndef OS32_BOOT_REALMODE_DEFS_INC
%define OS32_BOOT_REALMODE_DEFS_INC
; Real-mode segment granularity, independent of operand size.
RM_SEGMENT_SHIFT EQU 4
RM_SEGMENT_OFFSET_MASK EQU (1 << RM_SEGMENT_SHIFT) - 1
%endif
