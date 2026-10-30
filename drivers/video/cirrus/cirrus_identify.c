/* Core-only, read-only board/window identification. No device I/O. */
#include "gfx_hal.h"
#include "pgalloc.h"
#include "paging.h"
#include "wab_xe10.h"

#define CIRRUS_PATTERN_BYTES 8u

static int cirrus_win_usable(u32 base, u32 size)
{
    u32 last;
    if (size == 0) return 0;
    /* 32bit の物理空間の末尾を越えないこと (4GB ちょうどで終わるのは可)。
     * 末尾番地 (inclusive) で持つので base + size の桁あふれを作らない。 */
    if (size - 1 > U32_MAX - base) return 0;
    last = base + (size - 1);
    /* 窓に RAM が登録されていれば張ってはいけない (自分の RAM を隠す)。
     * **RAM の上端 (sys_get_mem_kb) では決めない** — K6-RAM 以後、15MB 機
     * + 高位 RAM (NP21/W ExMemory 16) の上端は 17,408KB で、15-16MB の穴の
     * 中にあるバンク窓 F60000h まで「RAM が届いている」と誤判定して probe が
     * ID 判定の前に落ちていた (PEGC で直した §4-34 と同じ形)。見るのはブート
     * 時に凍結した物理地図で、窓 [base, last] にかかるページを 1 枚も
     * 落とさずに問い合わせる。pgalloc 未初期化は RAM あり扱い (張らない)。
     * リニア窓は v3 のデバイス窓の帯 (物理地図で MMIO) に置くので RAM とは
     * 重ならない。ページテーブル側の置き場 (帯の静的 PT) に収まることは
     * ボードの層 (drivers/video/cirrus/wab_glue_xe10.c) の STATIC_ASSERT が見ている。 */
    return !pgalloc_range_has_ram(base / PAGE_SIZE, last / PAGE_SIZE + 1);
}

/* 副作用のない識別 (TASK_T1_LEDGER §3-8)。物理地図とボードの表を見るだけで、
 * ポートは叩かない (ID の読み出しは index OUT なので識別に使えない —
 * **予約は存在の証明ではない**、DEVICE_RESERVATION §6)。機種が 9821 系で
 * あることは ⑥ の呼び手 (gfx_core.c) が BIOS ワークエリアで見る。
 * 1 = Cirrus (Xe10) の候補。 */
int cirrus_identify(void)
{
    if (!cirrus_win_usable(WAB_XE10_WIN_BASE, WAB_XE10_WIN_SIZE)) return 0;
    /* リニア窓が使えないと CPU 描画面 (bb_base) を出せない = gshell も
     * gdi_test も描けないので、ここで諦めて PEGC / 9801 へ譲る (H3b)。 */
    if (!cirrus_win_usable(WAB_XE10_LINEARWIN_BASE, WAB_XE10_LINEARWIN_SIZE)) return 0;
    /* 窓の先頭がページ境界で、使う VRAM (表示面 + 非表示面 + パターン) が
     * 窓に収まること。ボードごとの値なので実行時に見る。 */
    if (WAB_XE10_LINEARWIN_BASE & (PAGE_SIZE - 1)) return 0;
    if (WAB_XE10_LINEARWIN_SIZE < (MEM_GFX_BB8_SIZE * 2UL + CIRRUS_PATTERN_BYTES)) return 0;
    return 1;
}

