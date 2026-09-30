#include "gfx_internal.h"
#include "gfx_hal.h"
#include "os32_kapi_shared.h"
#include "cpu_calibrate.h"
#include "io.h"

static void _flush_dirty_queue(void);
static void _merge_prev_dirty(void);
static void _flip_page(void);

/* ======================================================================== */
/*  パックド系バックエンド (PEGC 256 色 / H2) 用のダーティ転送               */
/*                                                                          */
/*  _flush_dirty_queue 以下は 4 プレーンの VRAM (0xA8000 系) を前提にした    */
/*  9801 専用の転送エンジン。パックド 8bpp ではプレーンが無く、しかも        */
/*  拡張グラフィックモードでは 0xA8000 が 32KB のバンク窓に化けているので、  */
/*  同じ経路を走らせると画面が壊れる。gdi_test / gshell の WM は            */
/*  gfx_present_dirty() を直接呼ぶ (KAPI) ため、ここで受けてバックエンドの   */
/*  present_rect へ流す。                                                    */
/*                                                                          */
/*  戻り値: 1 = パックド系だったので処理した (呼び出し元は即 return)。       */
/* ======================================================================== */
static int _present_dirty_packed(void)
{
    u32 commits_before;
    int i;

    if (!g_backend || g_backend->bb_format == GFX_BB_PLANAR4) return 0;
    if (!g_backend->present_rect) return 1;
    if (dirty_queue.count == 0) return 1;

    /* present_rect は 1 回ごとに commits を数えるが、契約 G7 の commits は
     * 「present (commit) 回数」なので、この 1 回ぶんに揃える。 */
    commits_before = gfx_counters.commits;
    for (i = 0; i < dirty_queue.count; i++) {
        GFX_Rect *r = &dirty_queue.rects[i];
        g_backend->present_rect(r->x, r->y, r->w, r->h);
    }
    gfx_counters.commits = commits_before + 1;

    dirty_queue.count = 0;
    prev_dirty.count = 0;   /* ページフリップが無いのでステイルページも無い */
    return 1;
}

/* ======================================================================== */
/*  KAPI: ダーティレクタングルの追加 (OS管理)                              */
/* ======================================================================== */
void __cdecl gfx_add_dirty_rect(int x, int y, int w, int h)
{
    int aligned_x, aligned_w;
    int right = x + w;
    int bottom = y + h;
    int i;
    GFX_Rect *r;

    /* マージ判定用のギャップ閾値 (px)。
     * この距離以内の矩形は統合する (32pxアライメントに合わせる) */
    int gap = 32;

    int new_x, new_y, new_r, new_b;

    if (w <= 0 || h <= 0) return;

    /* 32ピクセル境界 (4バイト) にアライメント:
     * xは切り捨て、幅は切り上げ
     */
    aligned_x = x & ~31;
    aligned_w = ((right + 31) & ~31) - aligned_x;

    /* 画面クリッピング */
    if (aligned_x < 0) {
        aligned_w += aligned_x;
        aligned_x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (aligned_x + aligned_w > GFX_WIDTH) aligned_w = GFX_WIDTH - aligned_x;
    if (y + h > gfx_current_height) h = gfx_current_height - y;

    if (aligned_w <= 0 || h <= 0) return;

    /* --- マージ判定 --- */
    new_x = aligned_x;
    new_y = y;
    new_r = aligned_x + aligned_w;
    new_b = y + h;

    for (i = 0; i < dirty_queue.count; i++) {
        int ex, ey, er, eb;

        r = &dirty_queue.rects[i];
        ex = r->x;
        ey = r->y;
        er = ex + r->w;
        eb = ey + r->h;

        /* 完全包含チェック: 新rectが既存rectに包含されるなら何もしない */
        if (new_x >= ex && new_y >= ey && new_r <= er && new_b <= eb) {
            return;
        }

        /* オーバーラップ or 隣接(gap px以内)なら統合 */
        if (new_x <= er + gap && new_r >= ex - gap &&
            new_y <= eb + gap && new_b >= ey - gap) {
            /* バウンディングボックスに統合 */
            if (new_x < ex) r->x = new_x; else new_x = ex;
            if (new_y < ey) r->y = new_y; else new_y = ey;
            if (new_r > er) er = new_r;
            if (new_b > eb) eb = new_b;
            r->w = er - r->x;
            r->h = eb - r->y;
            return;
        }
    }

    /* マージ対象なし → 新規追加 */

    /* キューがいっぱいの場合はバウンディングボックスに圧縮 */
    if (dirty_queue.count >= MAX_DIRTY_RECTS) {
        int min_x = GFX_WIDTH, min_y = gfx_current_height, max_x = 0, max_y = 0;
        for (i = 0; i < dirty_queue.count; i++) {
            if (dirty_queue.rects[i].x < min_x) min_x = dirty_queue.rects[i].x;
            if (dirty_queue.rects[i].y < min_y) min_y = dirty_queue.rects[i].y;
            if (dirty_queue.rects[i].x + dirty_queue.rects[i].w > max_x) max_x = dirty_queue.rects[i].x + dirty_queue.rects[i].w;
            if (dirty_queue.rects[i].y + dirty_queue.rects[i].h > max_y) max_y = dirty_queue.rects[i].y + dirty_queue.rects[i].h;
        }
        dirty_queue.count = 1;
        dirty_queue.rects[0].x = min_x;
        dirty_queue.rects[0].y = min_y;
        dirty_queue.rects[0].w = max_x - min_x;
        dirty_queue.rects[0].h = max_y - min_y;
    }

    r = &dirty_queue.rects[dirty_queue.count++];
    r->x = new_x;
    r->y = new_y;
    r->w = new_r - new_x;
    r->h = new_b - new_y;
}

/* ======================================================================== */
/*  ダーティレクタングルのVRAM転送 (VSYNC同期内等で呼ばれる)              */
/* ======================================================================== */
static void _flush_dirty_queue(void)
{
    int i, row;
    u8 *vb_base = (u8 *)VRAM_PLANE_B;
    u8 *vr_base = (u8 *)VRAM_PLANE_R;
    u8 *vg_base = (u8 *)VRAM_PLANE_G;
    u8 *vi_base = (u8 *)VRAM_PLANE_I;

    for (i = 0; i < dirty_queue.count; i++) {
        GFX_Rect *r = &dirty_queue.rects[i];
        int byte_x = r->x >> 3;           /* 横は何バイト目か */
        int byte_w = r->w >> 3;           /* 横幅のバイト数 */
        int words  = byte_w >> 1;         /* 16bit転送するワード数 */
        int physical_y;
        unsigned long phys_off, base_off;

        if (words <= 0) continue;

        /* 実 VRAM 書き込み量を数える (レビュー ⑦)。1 行 = words ワード ×2B ×4 プレーン、
         * ×r->h 行。ステイルページのマージ (prev_dirty) 分もここで正しく積まれる。 */
        gfx_counters.present_bytes += (u32)words * 2u * 4u * (u32)r->h;

        physical_y = (r->y + vram_scroll_y) % gfx_current_height;
        phys_off = (unsigned long)physical_y * GFX_BPL + byte_x;
        base_off = (unsigned long)r->y * GFX_BPL + byte_x;

        for (row = 0; row < r->h; row++) {
            _memcpy_w(vb_base + phys_off, bb_b + base_off, words);
            _memcpy_w(vr_base + phys_off, bb_r + base_off, words);
            _memcpy_w(vg_base + phys_off, bb_g + base_off, words);
            _memcpy_w(vi_base + phys_off, bb_i + base_off, words);

            base_off += GFX_BPL;
            phys_off += GFX_BPL;
            physical_y++;
            if (physical_y >= gfx_current_height) {
                physical_y = 0;
                phys_off = byte_x;
            }
        }
    }
    dirty_queue.count = 0;
}

/* ======================================================================== */
/*  ステイルページ対策: 前フレームのdirty rectを現フレームにマージ            */
/* ======================================================================== */
static void _merge_prev_dirty(void)
{
    int i;
    for (i = 0; i < prev_dirty.count; i++) {
        GFX_Rect *r = &prev_dirty.rects[i];
        gfx_add_dirty_rect(r->x, r->y, r->w, r->h);
    }
}

/* ======================================================================== */
/*  ページ切替: 表示ページと描画ページを入れ替える (~3µs)              */
/* ======================================================================== */
static void _flip_page(void)
{
    gfx_display_page ^= 1;
    _out(GDC_DISP_PAGE, gfx_display_page);
    _out(GDC_ACCESS_PAGE, gfx_display_page ^ 1);
    gfx_counters.io_accesses += 2;   /* OUT ×2 (表示ページ + 描画ページ) */
}

/* ======================================================================== */
/*  KAPI: 強制でダーティ領域をVSYNC同期転送する                           */
/*                                                                          */
/*  フリップモード時: VSYNC待ちなしで非表示ページに転送→ページ切替。       */
/*  従来モード時: VSYNC待ち→転送 (従来動作維持)。                    */
/* ======================================================================== */
void __cdecl gfx_present_dirty(void)
{
    if (_present_dirty_packed()) return;
    if (gfx_flip_enabled) {
        DirtyRectQueue snapshot;
        if (dirty_queue.count == 0 && prev_dirty.count == 0) return;
        gfx_counters.commits++;
        /* 今フレームのオリジナルdirtyを保存 (マージ前) */
        snapshot = dirty_queue;
        _merge_prev_dirty();
        _flush_dirty_queue();
        prev_dirty = snapshot;
        _flip_page();
    } else {
        if (dirty_queue.count == 0) return;
        gfx_counters.commits++;
        /* VSYNC期間になるまで待つ */
        while ((_in(0x60) & 0x20) == 0) { }
        _flush_dirty_queue();
    }
}

/* ======================================================================== */
/*  KAPI: VSYNC待ちなしでダーティ領域を即座にVRAM転送する                  */
/*                                                                          */
/*  フリップモード時: gfx_present_dirty()と同じ動作 (元々VSYNC不要)。    */
/*  従来モード時: VSYNC待ちなしで即座に転送 (従来動作維持)。           */
/* ======================================================================== */
void __cdecl gfx_present_nosync(void)
{
    if (_present_dirty_packed()) return;
    if (gfx_flip_enabled) {
        DirtyRectQueue snapshot;
        if (dirty_queue.count == 0 && prev_dirty.count == 0) return;
        gfx_counters.commits++;
        snapshot = dirty_queue;
        _merge_prev_dirty();
        _flush_dirty_queue();
        prev_dirty = snapshot;
        _flip_page();
    } else {
        if (dirty_queue.count == 0) return;
        gfx_counters.commits++;
        _flush_dirty_queue();
    }
}

/* ======================================================================== */
/*  互換KAPI実装 (OS管理に寄せる)                                          */
/* ======================================================================== */
void __cdecl gfx_present(void)
{
    /* HAL バックエンド経由 (契約 G4 の present_rect)。全画面 present。 */
    if (g_backend && g_backend->present_rect)
        g_backend->present_rect(0, 0, GFX_WIDTH, gfx_current_height);
}

void __cdecl gfx_present_rect(int rx, int ry, int rw, int rh)
{
    if (g_backend && g_backend->present_rect)
        g_backend->present_rect(rx, ry, rw, rh);
}

/* ======================================================================== */
/*  1ライン分だけVRAM転送 (全dirty rectの該当ラインのみ)                   */
/* ======================================================================== */
static void _flush_dirty_line(int line)
{
    int i;
    u8 *vb_base = (u8 *)VRAM_PLANE_B;
    u8 *vr_base = (u8 *)VRAM_PLANE_R;
    u8 *vg_base = (u8 *)VRAM_PLANE_G;
    u8 *vi_base = (u8 *)VRAM_PLANE_I;

    for (i = 0; i < dirty_queue.count; i++) {
        GFX_Rect *r = &dirty_queue.rects[i];
        int byte_x, byte_w, words;
        int physical_y;
        unsigned long phys_off, base_off;

        /* このdirty rectにこのラインが含まれるかチェック */
        if (line < r->y || line >= r->y + r->h) continue;

        byte_x = r->x >> 3;
        byte_w = r->w >> 3;
        words  = byte_w >> 1;
        if (words <= 0) continue;

        physical_y = (line + vram_scroll_y) % gfx_current_height;
        phys_off = (unsigned long)physical_y * GFX_BPL + byte_x;
        base_off = (unsigned long)line * GFX_BPL + byte_x;

        _memcpy_w(vb_base + phys_off, bb_b + base_off, words);
        _memcpy_w(vr_base + phys_off, bb_r + base_off, words);
        _memcpy_w(vg_base + phys_off, bb_g + base_off, words);
        _memcpy_w(vi_base + phys_off, bb_i + base_off, words);
    }
}

/* ======================================================================== */
/*  KAPI: ラスタパレット付きVRAM転送                                        */
/*                                                                          */
/*  VSYNC待機後、ラスタパレットテーブルに従ってパレットI/Oを書き換える。      */
/*  NP21/Wのrasterdraw機構はCPUクロック値でイベントをラスタラインに            */
/*  対応付けるため、HBLANK I/Oポーリングではなく固定CPUサイクルの              */
/*  ディレイで書き込み間隔を制御する。                                        */
/*                                                                          */
/*  2ライン ≈ gdc.rasterclock*2 CPUサイクル。                                */
/*  実機8MHz時: 1ライン ≈ 199サイクル → 2ライン ≈ 398サイクル。              */
/*  NOPディレイでこれに近い間隔を確保する。                                  */
/* ======================================================================== */

/* ラスタパレット書き換え間ディレイ
 * NP21/W 実測: 約120µs で全400ラインカバー (100エントリ × 4ライン間隔)。
 * cpu_delay_us() により CPU 速度に自動適応する。 */
#define RASTER_LINE_US  120

static inline void _raster_delay(void)
{
    cpu_delay_us(RASTER_LINE_US);
}

/* ======================================================================== */
/*  _raster_delay_open — 待つあいだは **割り込みを戻す**                    */
/*                                                                          */
/*  ⚠ ここで割り込みを止め続けてはいけない (Codex レビュー往復 2 B4)。       */
/*  ラスタ期間は IF=0 で守られているが、待ちは 100〜400 回繰り返される。     */
/*  cpu_calibrate() の丸めを直して cpu_delay_us が**本来の長さ**を待つように */
/*  なったので (票 TASK_SERIAL_VFAST 往復 3)、この待ちの合計は実機で         */
/*  **12〜48ms** になる。そのあいだ割り込みを止めると:                      */
/*    - 38400 以上の受信を取りこぼす (FIFO は 16 バイトしかない)             */
/*    - PIT の tick を失う → drivers/serial.c の送信予算 (tick を数える)     */
/*      の前提が崩れる                                                      */
/*                                                                          */
/*  **開けるのは待ちのあいだだけ。** VSYNC 終了待ち・VRAM 転送                */
/*  (`_flush_dirty_line`)・パレットの 4 ポート連続 out は **IF=0 のまま**     */
/*  (割り込みで分断されると色や転送先がずれる)。つまり「最小区間だけ守る」    */
/*  ではなく「**待ちのぶんだけ窓を開ける**」で、1 行あたり                    */
/*  RASTER_LINE_US (120µs) ごとに割り込みが 1 回通る。400 行なら 400 回。     */
/*  引き換えに、待ちの途中で割り込みが入るとラスタの位置が数十µs ずれうる    */
/*  — 呼び手は kernel/boot_splash.c の 2 か所だけなので、スプラッシュの      */
/*  見た目で確かめる。                                                      */
/* ======================================================================== */
static void _raster_delay_open(unsigned int *flags)
{
    irq_restore(*flags);
    _raster_delay();
    *flags = irq_save();
}

void __cdecl gfx_present_raster(GFX_RasterPalTable *table)
{
    int entry_idx, line;
    int has_dirty;
    unsigned int flags;

    if (!table || table->count == 0) return;

    /* ラスタパレットは 16 色プレーン機の HBLANK 同期パレット書き換え。
     * パックド系 (PEGC 256 色) には対応物が無いので、v1 では普通の present に
     * 落とす (色は化けず、ラスタ効果だけが出ない)。 */
    if (_present_dirty_packed()) return;

    gfx_counters.commits++;

    /* フリップモード: VRAM転送はフリップ経由、パレット書き換えのみVSYNC同期 */
    if (gfx_flip_enabled) {
        /* まずフリップ転送でVRAMを更新 */
        if (dirty_queue.count > 0 || prev_dirty.count > 0) {
            DirtyRectQueue snapshot;
            snapshot = dirty_queue;
            _merge_prev_dirty();
            _flush_dirty_queue();
            prev_dirty = snapshot;
            _flip_page();
        }
        /* VSYNC待ち → パレット書き換えのみ */
        while ((_in(0x60) & 0x20) == 0) { }
        flags = irq_save();
        while (_in(GDC_STATUS_PORT) & 0x20) { }
        for (entry_idx = 0; entry_idx < table->count; entry_idx++) {
            GFX_RasterPalEntry *e = &table->entries[entry_idx];
            _out(PAL_IDX_PORT, e->pal_idx);
            _out(PAL_G_PORT, e->g & 0x0F);
            _out(PAL_R_PORT, e->r & 0x0F);
            _out(PAL_B_PORT, e->b & 0x0F);
            _raster_delay_open(&flags);
        }
        irq_restore(flags);
        return;
    }

    has_dirty = (dirty_queue.count > 0);

    /* VSYNC期間になるまで待つ */
    while ((_in(0x60) & 0x20) == 0) { }

    /* 割り込み禁止 (全ラスタ期間を通じて安定させる) */
    flags = irq_save();

    /* VSYNC終了を待つ → アクティブ表示領域が始まる */
    while (_in(GDC_STATUS_PORT) & 0x20) { }

    entry_idx = 0;

    if (has_dirty) {
        /* VRAM転送 + パレット書き換え */
        for (line = 0; line < gfx_current_height; line++) {
            _flush_dirty_line(line);

            while (entry_idx < table->count &&
                   table->entries[entry_idx].line <= (u16)line) {
                GFX_RasterPalEntry *e = &table->entries[entry_idx];
                _out(PAL_IDX_PORT, e->pal_idx);
                _out(PAL_G_PORT, e->g & 0x0F);
                _out(PAL_R_PORT, e->r & 0x0F);
                _out(PAL_B_PORT, e->b & 0x0F);
                entry_idx++;
            }
            _raster_delay_open(&flags);
        }
    } else {
        /* パレット書き換えのみ (VRAM転送なし) */
        for (entry_idx = 0; entry_idx < table->count; entry_idx++) {
            GFX_RasterPalEntry *e = &table->entries[entry_idx];
            _out(PAL_IDX_PORT, e->pal_idx);
            _out(PAL_G_PORT, e->g & 0x0F);
            _out(PAL_R_PORT, e->r & 0x0F);
            _out(PAL_B_PORT, e->b & 0x0F);
            _raster_delay_open(&flags);
        }
    }

    /* 割り込み復帰 */
    irq_restore(flags);

    dirty_queue.count = 0;
}
