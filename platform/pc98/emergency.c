/* Early boot and fault output; independent of the normal console. */
#include "platform_emergency.h"
#include "tvram.h"
#include "memmap.h"

void platform_boot_puts(int x, int y, const char *str, u8 color)
{
    u32 offset;
    while (*str) {
        if (x >= TVRAM_COLS) { x = 0; y++; }
        if (y >= TVRAM_ROWS) break;
        offset = (u32)y * TVRAM_BPR + (u32)x * TVRAM_CELL_BYTES;
        *(volatile u16 *)P2V_IO(TVRAM_BASE + offset) = (u16)(u8)*str;
        *(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = color;
        str++; x++;
    }
}

void platform_fault_cell(u32 pos, u16 code, u8 attr)
{
    ((volatile u16 *)P2V_IO(TVRAM_BASE))[pos] = code;
    ((volatile u16 *)P2V_IO(TVRAM_ATTR))[pos] = (u16)attr;
}
