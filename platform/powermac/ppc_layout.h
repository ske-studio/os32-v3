/* Subsystem constants shared by C, assembler and the generated linker script.
 * PPC_BOOT_DESIGN §2/3; BAT values: NetBSD Locore.c:108-113.
 * No public ABI constant is introduced by this independent boot stage. */
#ifndef OS32_PPC_LAYOUT_H
#define OS32_PPC_LAYOUT_H
#define PM_LINK_BASE 0x20000
#define PM_IMAGE_LIMIT 0x10000
#define PM_ELF_BUDGET 0x6000
#define PM_STACK_SIZE 8192
#define PM_STACK_ALIGN 16
#define PM_ABI_FRAME 32
#define PM_CELL_BYTES 4
#define PM_BAT_RAM_LOWER 0x12
#define PM_BAT_RAM_UPPER 0x1ffe
#define PM_MSR_EE 0x8000
#define PM_ESCC_WINDOW 0x20
#define PM_ESCC_STRIDE 0x10
/* NetBSD oea/pte.h:96-107: PTE_I | PTE_G | PTE_RW. */
#define PM_MAP_MODE_IO (0x20 | 0x08 | 0x02)
#define PM_POLL_LIMIT 1000000
#define PM_RR0_TX_READY 0x04
#define PM_RR1_ALL_SENT 0x01
#define PM_RR1_INDEX 1
#define PM_PROP_CAP 4096
#define PM_PATH_CAP 1024
#define PM_OPTION_CAP 32
#define PM_FAIL_CAP 64
#define PM_CI_CAP 16
#define PM_METHOD_MAX_INPUTS 6
#define PM_NOTE_NAMESZ 8
#define PM_NOTE_DESCSZ 20
#define PM_NOTE_TYPE 0x1275
#define PM_ROOT "/"
#define PM_CHOSEN "/chosen"
#define PM_OPTIONS "/options"
#define PM_OPENPROM "/openprom"
#define PM_SCREEN "screen"
#endif
