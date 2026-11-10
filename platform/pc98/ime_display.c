/* PC-98 FEP-private candidate planes and boot resources. */
#include "kernel/ime/ime_display_private.h"
#include "pc98.h"
#include "tvram.h"
#include "tvram_private.h"
#include "memmap.h"
#include "mm/kmalloc.h"
#include "kernel/sqlite_engine.h"
#ifdef OS32_FEP_MODULE
#include "fep_imports.h"
#endif

static u16 *g_cand_save_text;
static u8 *g_cand_save_attr;

int ime_display_cols(void) { return TVRAM_COLS; }
int ime_display_preedit_row(void) { return TVRAM_ROWS - 1; }

int ime_display_boot(u32 fault)
{
    if (!g_cand_save_text && fault != SERVICES_FAULT_FEP_TEXT)
        g_cand_save_text = kmalloc(IME_CANDLIST_SAVE_ROWS * TVRAM_COLS * sizeof(u16));
    if (!g_cand_save_text) return -1;
    if (!g_cand_save_attr && fault != SERVICES_FAULT_FEP_ATTR)
        g_cand_save_attr = kmalloc(IME_CANDLIST_SAVE_ROWS * TVRAM_COLS * sizeof(u8));
    if (!g_cand_save_attr) return -1;
    return 0;
}

u32 ime_display_bytes(void)
{
    return (g_cand_save_text ? kmalloc_block_size(g_cand_save_text) + BLK_HDR_SIZE : 0) +
           (g_cand_save_attr ? kmalloc_block_size(g_cand_save_attr) + BLK_HDR_SIZE : 0);
}

void ime_display_abort(void)
{
    if (g_cand_save_attr) kfree(g_cand_save_attr);
    if (g_cand_save_text) kfree(g_cand_save_text);
    g_cand_save_attr = 0;
    g_cand_save_text = 0;
}

int ime_display_save(int top, int rows)
{
    int y, x;
    if (!g_cand_save_text || !g_cand_save_attr) return 0;
    for (y = 0; y < rows; y++) {
        for (x = 0; x < TVRAM_COLS; x++) {
            tvram_readchar_at(x, top + y,
                              &g_cand_save_text[y * TVRAM_COLS + x],
                              &g_cand_save_attr[y * TVRAM_COLS + x]);
        }
    }
    return 1;
}

int ime_display_restore(int top, int rows)
{
    int y, x;
    if (!g_cand_save_text || !g_cand_save_attr) return 0;
    for (y = 0; y < rows; y++) {
        for (x = 0; x < TVRAM_COLS; x++) {
            u32 offset = (u32)(top + y) * TVRAM_BPR + (u32)x * TVRAM_CELL_BYTES;
            *(volatile u16 *)P2V_IO(TVRAM_BASE + offset) = g_cand_save_text[y * TVRAM_COLS + x];
            *(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = g_cand_save_attr[y * TVRAM_COLS + x];
        }
    }
    return 1;
}
