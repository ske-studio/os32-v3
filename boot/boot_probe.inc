; Named constants; C mirrors are checked by test_boot_named_constants.py.
%ifndef OS32_BOOT_BOOT_PROBE_INC
%define OS32_BOOT_BOOT_PROBE_INC
; Current RAM probe policy (512 KiB stride, two alternating patterns).
BOOT_PROBE_STEP_KIB EQU 512
BOOT_PROBE_PATTERN_A EQU 0xAA55AA55
BOOT_PROBE_PATTERN_B EQU 0x55AA55AA
%endif
