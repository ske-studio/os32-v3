/* ======================================================================== */
/*  KSELFTEST.C — カーネル内プリミティブの自己診断                          */
/*                                                                          */
/*  なぜカーネル内でやるのか:                                                */
/*    programs/tests/klibc_test.c は **newlib とリンクされる外部プログラム** */
/*    なので、そこで通る strlen/memcpy/malloc は newlib の実装であって       */
/*    カーネルの kstring_asm.asm / kmalloc.c ではない。つまりカーネルが      */
/*    実際に使うプリミティブはこれまで一度もテストされていなかった。         */
/*                                                                          */
/*  ここでは「境界ケースだけ」を見る。通常ケースはシェルが動いている時点で   */
/*  通っているので、n=0 / バッファぴったり / 重なりコピー / 二重解放 など、   */
/*  実際にバグが潜んでいた形だけを実機で毎回踏んで確認する。                 */
/*                                                                          */
/*  ブート時に 1 回走り、全部通れば 1 行だけ出す。落ちた項目は赤で名前を     */
/*  出す (実機で回帰した瞬間に画面で分かる)。                               */
/* ======================================================================== */

#include "kselftest.h"
#include "pegc.h"
#include "gfx_hal.h"
#include "kstring.h"
#include "kprintf.h"
#include "kmalloc.h"
#include "paging.h"
#include "lease.h"
#include "con_sink.h"
#include "pc98.h"
#include "tvram.h"
#include "console.h"
#include "kbd_inject.h"
#include "kbd.h"          /* kbd_diag / KBD_CMD_* (POLICY_DEBUG §4-57) */
#include "appslot.h"
#include "launch.h"
#include "exec.h"
#include "fd_redirect.h"  /* fd_redirect_buf_write_ok (門はポインタの由来で決める) */
#include "kapi_db.h"
#include "cpu_calibrate.h"
#include "cpu_calibrate_math.h"
#include "idt.h"          /* pit_get_setup / PIT_MODE_TIMER0 */
#include "sysclk.h"       /* sysclk_hz / sysclk_detected */
#include "memmap.h"       /* PIT_HZ / MEM_DMA_POOL_* */
#include "dma_pool.h"     /* DMA プール (票 TASK_HAL_WIRING §1-3) */
#include "pcm_cs4231.h"
#include "dma8237.h"      /* 8237 の共通部 (同 §1-2) */
#include "irq.h"          /* 動的 IRQ 登録 (票 TASK_HAL_WIRING §1-1) */
#include "time_math.h"    /* time_branch_hits (票 §1-5) */
#include "ktime.h"        /* 試験専用の注入口 (p1/p2/count) */
#include "sys.h"          /* sys_time_now */
#include "io.h"           /* inp / irq_save */
#include "bootinfo.h"     /* ブート情報域 (票 TASK_HDD_INSTALL 段 0) */
#include "bootlog.h"      /* 起動ログ (/var/log/boot.log) */
#include "gui.h"          /* gui_ime_set_render (ime_set_render の門) */
#include "pgalloc.h"      /* pgalloc_model_state (T1a: legacy 経路の撤去) */

/* 結果はホストから読めるようにグローバルにする。
 * ブート時の出力はスプラッシュで流れてしまい、rshell も未起動なので
 * シリアルにも出ない。kernel.map 経由で emu_read_mem するのが確実。
 * (v86.c の観測値と同じ理由で static にしない) */
int kselftest_pass = 0;
int kselftest_fail = 0;
#define ksel_pass kselftest_pass
#define ksel_fail kselftest_fail

/* **`noinline` はカーネルの予算のため** (票 TASK_HAL_WIRING、A+B 合流後に
 * 残り 1.2KB まで詰まった)。-O2 はこの 6 行を呼び出し 194 か所すべてに
 * 展開していて、それだけで 6KB を食っていた。自己診断は起動時に 1 回しか
 * 走らないので速度は要らない。判定そのものは 1 バイトも変わらない。
 * (同じ理由の先例: kernel/cpu_calibrate.c の nop_loop) */
static void __attribute__((noinline)) check(int cond, const char *name)
{
    if (cond) {
        ksel_pass++;
    } else {
        ksel_fail++;
        kprintf(0xC1, "[selftest] FAIL: %s\n", name);
    }
}

/* ------------------------------------------------------------------------ */
/*  kstring_asm.asm (kmemcpy/kmemset/kstrlen/kstrcmp/kstrncpy)              */
/*                                                                          */
/*  ASM 実装は 4 バイト単位の高速パスと 1 バイトずつの端数パスを持つので、  */
/*  n が 0 / 端数 / 境界ちょうど のときに崩れやすい。番兵を置いて           */
/*  「書きすぎていないこと」まで見る。                                       */
/* ------------------------------------------------------------------------ */
static void test_mem(void)
{
    static u8 buf[32];
    static u8 src[32];
    u32 i;
    int ok;

    for (i = 0; i < 32; i++) src[i] = (u8)(i + 1);

    /* kmemset: n=0 は 1 バイトも書いてはいけない */
    buf[0] = 0xAA;
    kmemset(buf, 0x55, 0);
    check(buf[0] == 0xAA, "kmemset n=0");

    /* kmemset: 端数長 (高速パスと端数パスの境目) */
    for (i = 0; i < 32; i++) buf[i] = 0xAA;
    kmemset(buf, 0x55, 5);
    ok = (buf[0] == 0x55 && buf[4] == 0x55 && buf[5] == 0xAA);
    check(ok, "kmemset n=5 no overrun");

    /* kmemcpy: n=0 */
    buf[0] = 0xAA;
    kmemcpy(buf, src, 0);
    check(buf[0] == 0xAA, "kmemcpy n=0");

    /* kmemcpy: 端数長 + 番兵 */
    for (i = 0; i < 32; i++) buf[i] = 0xAA;
    kmemcpy(buf, src, 7);
    ok = (buf[0] == 1 && buf[6] == 7 && buf[7] == 0xAA);
    check(ok, "kmemcpy n=7 no overrun");

    /* kmemcpy: 非アラインの dst/src */
    for (i = 0; i < 32; i++) buf[i] = 0xAA;
    kmemcpy(buf + 1, src + 1, 9);
    ok = (buf[0] == 0xAA && buf[1] == 2 && buf[9] == 10 && buf[10] == 0xAA);
    check(ok, "kmemcpy unaligned");

    /* memmove: 前方に重なるコピー (kmemcpy では壊れる形) */
    for (i = 0; i < 32; i++) buf[i] = (u8)(i + 1);
    memmove(buf, buf + 2, 8);
    ok = (buf[0] == 3 && buf[7] == 10);
    check(ok, "memmove overlap forward");

    /* memmove: 後方に重なるコピー */
    for (i = 0; i < 32; i++) buf[i] = (u8)(i + 1);
    memmove(buf + 2, buf, 8);
    ok = (buf[2] == 1 && buf[9] == 8);
    check(ok, "memmove overlap backward");

    /* memmove: n=0 と同一ポインタ */
    buf[0] = 0x42;
    memmove(buf, buf, 8);
    memmove(buf, buf + 1, 0);
    check(buf[0] == 0x42, "memmove n=0 / same ptr");
}

static void test_str(void)
{
    static char dst[16];
    int ok;

    check(kstrlen("") == 0, "kstrlen empty");
    check(kstrlen("abcd") == 4, "kstrlen 4");

    check(kstrcmp("", "") == 0, "kstrcmp empty");
    check(kstrcmp("abc", "abd") < 0, "kstrcmp lt");
    check(kstrcmp("abd", "abc") > 0, "kstrcmp gt");
    /* 0x80 以上のバイトを含む比較。ASM 側が符号付きで比べていると
     * 大小が逆転する (CP932 の 2 バイト目が該当する) */
    check(kstrcmp("\x80", "\x01") > 0, "kstrcmp high byte unsigned");

    check(kstrncmp("abc", "abd", 0) == 0, "kstrncmp n=0");
    check(kstrncmp("abc", "abd", 2) == 0, "kstrncmp n=2");
    check(kstrncmp("abc", "abd", 3) != 0, "kstrncmp n=3");

    /* kstrncpy は strlcpy セマンティクス: n はバッファ全体サイズ、
     * 必ず NUL 終端する */
    kmemset(dst, 0x7F, sizeof(dst));
    kstrncpy(dst, "abcdefgh", 4);
    ok = (dst[0] == 'a' && dst[2] == 'c' && dst[3] == '\0');
    check(ok, "kstrncpy truncates + NUL");

    /* kstrncat: n=0 は 1 バイトも触らない (n-1 のラップで暴走した形) */
    dst[0] = 'X'; dst[1] = '\0';
    kstrncat(dst, "yyyy", 0);
    check(dst[0] == 'X' && dst[1] == '\0', "kstrncat n=0");

    /* kstrncat: バッファぴったりで切り詰め + NUL */
    kstrcpy(dst, "abc");
    kstrncat(dst, "defgh", 6);
    ok = (kstrcmp(dst, "abcde") == 0);
    check(ok, "kstrncat exact fit");

    /* kstrncat: 既に満杯なら何もしない */
    kstrcpy(dst, "abcde");
    kstrncat(dst, "zzz", 6);
    check(kstrcmp(dst, "abcde") == 0, "kstrncat already full");

    /* strchr は NUL 自身も見つける契約 */
    check(strchr("abc", '\0') != (char *)0, "strchr finds NUL");
    check(strchr("abc", 'z') == (char *)0, "strchr not found");
}

static void test_utoa(void)
{
    char buf[16];

    check(kutoa_dec(0, buf, 12) == 1 && kstrcmp(buf, "0") == 0,
          "kutoa_dec 0");
    check(kutoa_dec(4294967295UL, buf, 12) == 10 &&
          kstrcmp(buf, "4294967295") == 0, "kutoa_dec u32 max");
    check(kutoa_hex(0, buf, 12, 0) == 1 && kstrcmp(buf, "0") == 0,
          "kutoa_hex 0");
    check(kutoa_hex(0xDEADBEEFUL, buf, 12, 1) == 8 &&
          kstrcmp(buf, "DEADBEEF") == 0, "kutoa_hex upper");
    check(kutoa_hex(0xDEADBEEFUL, buf, 12, 0) == 8 &&
          kstrcmp(buf, "deadbeef") == 0, "kutoa_hex lower");
}

/* ------------------------------------------------------------------------ */
/*  アロケータ                                                              */
/*                                                                          */
/*  カーネルヒープを壊さずに実装そのものを試すため、専用の KHeap             */
/*  インスタンスを静的バッファ上に作る (kheap_* をパラメータ化してある       */
/*  おかげでこれができる)。                                                  */
/* ------------------------------------------------------------------------ */
static u8 ksel_heap_buf[2048];

static void test_heap(void)
{
    KHeap h;
    void *a, *b, *c;
    u32 free_all;
    int i;

    kheap_init(&h, ksel_heap_buf, sizeof(ksel_heap_buf), "selftest");

    a = kheap_alloc(&h, 16);
    check(a != (void *)0, "kheap_alloc basic");
    /* SQLite が double を置くので 8 バイト境界が要る */
    check((((u32)a) & 7) == 0, "kheap_alloc 8-byte aligned");

    b = kheap_alloc(&h, 1);
    check(b != (void *)0 && (((u32)b) & 7) == 0, "kheap_alloc(1) aligned");
    check(kheap_block_size(&h, a) >= 16, "kheap_block_size");

    /* 0 バイト要求と、ヒープ全体を超える要求は NULL */
    check(kheap_alloc(&h, 0) == (void *)0, "kheap_alloc(0) = NULL");
    check(kheap_alloc(&h, sizeof(ksel_heap_buf) * 2) == (void *)0,
          "kheap_alloc too big = NULL");
    /* アライン時に 0 へ化ける値 (整数オーバーフロー) も弾けること */
    check(kheap_alloc(&h, 0xFFFFFFFFUL) == (void *)0,
          "kheap_alloc overflow = NULL");

    /* 解放 → 再確保でブロックが再利用される */
    kheap_free(&h, a);
    kheap_free(&h, b);
    c = kheap_alloc(&h, 16);
    check(c == a, "kheap reuse after free");
    kheap_free(&h, c);

    /* 全域が 1 個のフリーブロックに戻っている (前後の結合が効いている)。
     * 結合漏れがあると、この後の「ほぼ全域」確保が失敗する。 */
    free_all = sizeof(ksel_heap_buf) - 64;
    c = kheap_alloc(&h, free_all);
    check(c != (void *)0, "kheap coalesce to one block");
    if (c) kheap_free(&h, c);

    /* 断片化 → 全解放で必ず 1 個に戻る (結合の取りこぼし検出) */
    {
        void *p[16];
        for (i = 0; i < 16; i++) p[i] = kheap_alloc(&h, 32);
        /* 飛び飛びに解放してから残りを解放 */
        for (i = 0; i < 16; i += 2) kheap_free(&h, p[i]);
        for (i = 1; i < 16; i += 2) kheap_free(&h, p[i]);
        c = kheap_alloc(&h, free_all);
        check(c != (void *)0, "kheap coalesce after fragmentation");
        if (c) kheap_free(&h, c);
    }

    /* 不正な解放を検出して弾くこと (弾かないとヒープ管理が壊れる)。
     * 診断が 1 行出るのは想定内。 */
    a = kheap_alloc(&h, 16);
    kheap_free(&h, a);
    kheap_free(&h, a);                      /* 二重解放 */
    kheap_free(&h, (void *)0x1);            /* 範囲外 */
    kheap_free(&h, (u8 *)a + 1);            /* 非アライン */
    c = kheap_alloc(&h, free_all);
    check(c != (void *)0, "kheap survives invalid frees");
    if (c) kheap_free(&h, c);

    /* 極小サイズのヒープ: ヘッダも入らないなら空ヒープとして扱う
     * (size - BLK_HDR_SIZE のアンダーフローで巨大ブロックに化けた形) */
    {
        KHeap tiny;
        kheap_init(&tiny, ksel_heap_buf, 4, "tiny");
        check(kheap_alloc(&tiny, 1) == (void *)0, "kheap tiny = empty");
    }
}

/* ------------------------------------------------------------------------ */
/*  kprintf                                                                 */
/*                                                                          */
/*  出力内容は取れないので「戻ってくること」を確かめる。書式末尾が '%' の    */
/*  ときに無限ループしていた (KAPI 公開関数なので、ユーザプログラムの        */
/*  1 行でカーネルが固まった)。ここで固まればブートが止まるので分かる。      */
/* ------------------------------------------------------------------------ */
static void test_kprintf(void)
{
    kprintf(0x07, "");
    kprintf(0x07, "%");             /* 末尾 % — 旧実装はここで無限ループ */
    kprintf(0x07, "%%");
    kprintf(0x07, "%z", 1);         /* 未知指定子 (vararg を消費する) */
    kprintf(0x07, "\n");
    check(1, "kprintf returns (no hang)");
}

/* ------------------------------------------------------------------------ */
/*  kprintf の属性変換 (lib/kprintf_attr.c)                                 */
/*                                                                          */
/*  PC-98 のテキスト属性は PC/AT (CGA) とビットの意味が別物で、             */
/*  呼び出し側の大半が渡している 07h をそのまま属性 VRAM へ書くと           */
/*  **画面に 1 文字も出ない** (bit0 = 表示 / bit1 = ブリンク /              */
/*  bit2 = リバース / bit5,6,7 = 青,赤,緑)。実機 PC-9821Ra266 の FD 起動が  */
/*  失敗したとき、[fdc] / [ide] の診断行が 1 行も読めなかったのがこれ       */
/*  (2026-09-22)。kprintf は KAPI 公開関数でもあるので、ここが壊れると      */
/*  カーネルもアプリも同時に黙る — プリミティブとして毎回踏む。            */
/*  ホスト側の試験は tools/tests/test_kprintf_attr.py。                     */
/* ------------------------------------------------------------------------ */
static void test_kprintf_attr(void)
{
    /* PC/AT 流の値 → PC-98 の色 + 表示。反転も点滅も付けない。 */
    check(kprintf_attr_to_pc98(0x07) == 0xE1, "kprintf attr 07 -> E1 (white)");
    check(kprintf_attr_to_pc98(0x0A) == 0x81, "kprintf attr 0A -> 81 (green)");
    check(kprintf_attr_to_pc98(0x0C) == 0x41, "kprintf attr 0C -> 41 (red)");
    check(kprintf_attr_to_pc98(0x0E) == 0xC1, "kprintf attr 0E -> C1 (yellow)");
    check(kprintf_attr_to_pc98(0x02) == 0x81, "kprintf attr 02 -> 81 (green)");
    check(kprintf_attr_to_pc98(0x04) == 0x41, "kprintf attr 04 -> 41 (red)");
    check(kprintf_attr_to_pc98(0x0B) == 0xA1, "kprintf attr 0B -> A1 (cyan)");
    /* 色を持たない値は黒 = 不可視になるので白へ倒す。 */
    check(kprintf_attr_to_pc98(0x00) == 0xE1, "kprintf attr 00 -> E1 (black->white)");
    /* 既に PC-98 流の値はそのまま。bit0 が落ちていれば立てるだけ。 */
    check(kprintf_attr_to_pc98(0xC1) == 0xC1, "kprintf attr C1 kept");
    check(kprintf_attr_to_pc98(0xE0) == 0xE1, "kprintf attr E0 -> E1 (visible)");
}

/* ======================================================================== */
/*  公開エントリ                                                            */
/* ======================================================================== */
/* ------------------------------------------------------------------------ */
/*  リング3 PD 複製 (v2 M1b): 新 PD を作って CR3 に載せてもカーネル帯域が    */
/*  同一物理で共有され続けるか (V1)。ここが壊れると CPL=3 化以降が全滅する    */
/*  ので、ブート時に毎回検証する (CLAUDE.md: プリミティブは selftest に載せる)。*/
/* ------------------------------------------------------------------------ */
static void test_ring3_pd(void)
{
    int rc = paging_pd_clone_selftest();
    check((rc & 15) == 0, "ring3 PD clone (kernel band shared across PDs)");
    if (rc != 0) {
        kprintf(0xC1, "[selftest]   paging_pd_clone_selftest rc=%d\n", rc);
    }
    check((rc & 16) == 0, "paging: live AS rejects generic USER");
}

/* ------------------------------------------------------------------------ */
/*  デバイス窓の貸し出し (レビュー #5 ②③): 表示面を CPL=3 に見せないまま     */
/*  クライアント面だけを USER にでき、そのときキャッシュ属性 (PCD) が        */
/*  消えないこと。ここが壊れると Cirrus で「commit 前の描画が表示面に出る」   */
/*  (契約 G4 違反) か「BLT が古い VRAM を読んでちらつく」になるが、どちらも   */
/*  画面を見るまで分からないので毎回ブート時に見る。                          */
/* ------------------------------------------------------------------------ */
static void test_map_user_keep(void)
{
    int rc = paging_map_user_keep_selftest();
    check(rc == 0, "device window lease (USER only on client page, PCD kept)");
    if (rc != 0) {
        kprintf(0xC1, "[selftest]   paging_map_user_keep_selftest rc=%d\n", rc);
    }
}

/* ------------------------------------------------------------------------ */
/*  T2c の高位アプリ帯 (票 docs/tasks/v3/TASK_T2_APPBAND.md): 未使用 PDE は  */
/*  空のまま、写像時に必要なアプリ固有 PT を疎確保する。USER が              */
/*  master の PDE/PT へ漏れないこと。ここが壊れると CPL=3 アプリが master の  */
/*  ページテーブルを書き換えられる (= 任意物理への読み書き) が、動いている    */
/*  ように見えてしまうので毎回ブート時に見る。                                */
/* ------------------------------------------------------------------------ */
static void test_app_band_pde(void)
{
    int rc = paging_app_band_selftest();
    check(rc == 0, "app band PDEs (private PTs, USER never reaches master)");
    if (rc != 0) {
        kprintf(0xC1, "[selftest]   paging_app_band_selftest rc=%d\n", rc);
    }
}

/* ------------------------------------------------------------------------ */
/*  console シンクのリング (票 K6C の受入 C2): GUI モード中のカーネル出力を  */
/*  溜める 8KB の環。レコード境界で切ること・あふれで **古い方**を捨てる     */
/*  こと・CUI 復帰で捨てること・読み手が 1 本であることが崩れると、端末      */
/*  アプリには「出力が出ない」か「途中で化ける」としか見えず原因が遠い。      */
/*  ホスト試験 (tools/tests/test_con_sink.py) と同じ形をブート時にも踏む。    */
/* ------------------------------------------------------------------------ */
static void test_con_sink(void)
{
    u32 bad = con_sink_selftest();
    check((bad & (1u << 0)) == 0, "con_sink push/read (record round-trip)");
    check((bad & (1u << 1)) == 0, "con_sink CLEAR / CURSOR records");
    check((bad & (1u << 2)) == 0, "con_sink read cuts on a record boundary");
    check((bad & (1u << 3)) == 0, "con_sink overflow drops the oldest record");
    check((bad & (1u << 4)) == 0, "con_sink discards on return to CUI");
    check((bad & (1u << 5)) == 0, "con_sink single reader (owner reclaim)");
    check((bad & (1u << 6)) == 0, "con_sink EXIT record (child exit, T7 E1)");
}

/* ------------------------------------------------------------------------ */
/*  GUI モード中の描画抑止 (票 K6C-2)                                        */
/*                                                                          */
/*  シンクが有効なあいだ console.c が従来どおりテキスト VRAM にも描いていた  */
/*  ので、gshell の GFX 画面の上に CUI プログラムの出力が残像として重なって  */
/*  いた (PM 実測 2026-09-12、K7 受入 I2)。実機で見えるのは「左上に古い文字」 */
/*  だけで、シンク側は正常に見えるため原因が遠い。ここで毎回踏む。           */
/*  con_sink_enable/disable を直に使う (console_text_gdc_start はブート画面を */
/*  消してしまう)。 */
/* ------------------------------------------------------------------------ */
static void test_con_sink_render_gate(void)
{
    int sx = console_get_cursor_x();
    int sy = console_get_cursor_y();
    int x  = TVRAM_COLS - 1;
    int y  = TVRAM_ROWS - 1;
    u16 before = 0;
    u16 after = 0;
    u8  attr = 0;

    tvram_readchar_at(x, y, &before, &attr);
    console_set_cursor(x, y);
    con_sink_enable();
    shell_print("Z", TATTR_WHITE);
    tvram_readchar_at(x, y, &after, &attr);
    check(after == before, "console: GUI mode does not draw to text VRAM");
    check(console_get_cursor_x() == x && console_get_cursor_y() == y,
          "console: GUI mode does not advance the logical cursor");
    con_sink_disable();
    console_set_cursor(sx, sy);
}

/* ------------------------------------------------------------------------ */
/*  打鍵の注入リングと「印の無い resume は拒否」(票 K7 の受入 I5)            */
/*                                                                          */
/*  GUI 中の kbd_getchar は第 2 の park 点になった。壊れたときに実機で見える */
/*  のは「端末に打っても文字が出ない」か「GUI ごと固まる」だけで、原因が     */
/*  遠い。ブート時に踏むのは 2 つ:                                           */
/*    (1) 256B の環 — 積んだ順に 1 バイトずつ出る (UTF-8 の並びを変えない)、 */
/*        あふれは新しい方を捨てる、破棄で空、読み手未確立の注入は拒否。     */
/*    (2) 印の無いフレームは起こせない (C6 の規則が PARKED / WAIT_KEY /      */
/*        WAIT_POLL の 3 つに効く)。resume の切替点が緩むとフレームが宙に    */
/*        浮く。D8 の tick の間引きが表の検査より先に効くことも見る。        */
/* ------------------------------------------------------------------------ */
static void test_kbd_inject(void)
{
    u32 bad = kbd_inject_selftest();
    check((bad & (1u << 0)) == 0, "kbd_inject refuses with no con_sink reader");
    check((bad & (1u << 1)) == 0, "kbd_inject keeps UTF-8 byte order (FIFO)");
    check((bad & (1u << 2)) == 0, "kbd_inject take on empty ring returns 0");
    check((bad & (1u << 3)) == 0, "kbd_inject overflow drops the newest byte");
    check((bad & (1u << 4)) == 0, "kbd_inject discard empties the ring");
}

/* ------------------------------------------------------------------------ */
/*  キーボード 8251 のコマンド語 (POLICY_DEBUG §4-57)                        */
/*                                                                          */
/*  kbd_init が 0043h に書いた語が BIOS の定常値 0x16 で、DTR (bit1) が      */
/*  立っていること — 0 だと RTY# が LOW に張り付いてキーボードに再送を       */
/*  要求し続け、実機で打鍵が一切届かない (2026-09-23)。NP21/W は bit1 を     */
/*  見ないのでエミュレータの打鍵では気づけない。ここが唯一の見張り。         */
/*  ついでに kbd_diag (KAPI v62) が NULL を断ること。                        */
/* ------------------------------------------------------------------------ */
static void test_kbd_cmd(void)
{
    KbdDiag d;

    check(kbd_diag((KbdDiag *)0) == OS32_ERR_INVAL, "kbd_diag refuses NULL");
    kmemset(&d, 0, sizeof(d));
    check(kbd_diag(&d) == 0, "kbd_diag returns 0");
    /* マクロではなくリテラルと比べる — kbd_init もマクロを書くので、マクロ
     * どうしの比較は恒真で、定数を誤って変えても落ちない。 */
    check(d.cmd == 0x16, "kbd cmd word is 0x16 (BIOS)");
    check((d.cmd & KBD_CMD_DTR) != 0, "kbd cmd keeps DTR=1 (RTY# HIGH)");
}

static void test_resume_mark(void)
{
    u32 bad = appslot_resume_mark_selftest();
    check((bad & (1u << 0)) == 0, "resume refuses a free slot");
    check((bad & (1u << 1)) == 0, "resume needs the OP_WAIT mark (PARKED)");
    check((bad & (1u << 2)) == 0, "resume needs the kbd mark (WAIT_KEY)");
    check((bad & (1u << 3)) == 0, "kill folds WAIT_KEY but not a running app");
    check((bad & (1u << 4)) == 0, "exec_app_state adds 3/4 without moving 0/1/2");
    check((bad & (1u << 5)) == 0, "refused resume never counts as a switch");
    /* 票 T8 §7 D8 (第 3 の park 点 = ポーリング型の 1 周だけの譲り) */
    check((bad & (1u << 6)) == 0, "resume needs the poll mark (WAIT_POLL)");
    check((bad & (1u << 7)) == 0, "poll yield is throttled to one PIT tick");
    /* 票 T9 D5 (第 4 の park 点 = 明示的な譲り)。印から「EAX に何を入れるか」
     * が導けないと、sh が譲っている間に子宛の打鍵を吸って捨てる。 */
    check((bad & (1u << 8)) == 0, "resume needs the yield mark, and reads no key");
}

/* ------------------------------------------------------------------------ */
/*  画面の所有者 (票 T8 D1 / D1a)                                            */
/*                                                                          */
/*  全画面 GFX の持ち主は 1 つで、gfx_init で移り、回収で WM へ戻る。ここが  */
/*  緩むと「プログラムが抜けたのに GUI が戻らない」「宣言していないプログラム */
/*  が黙って画面を壊す」の両方が起きる。                                     */
/* ------------------------------------------------------------------------ */
static void test_gfx_owner(void)
{
    u32 bad = appslot_gfx_owner_selftest();
    check((bad & (1u << 0)) == 0, "gfx owner moves on claim, returns on exit");
    check((bad & (1u << 1)) == 0, "gfx claim without OS32X_FLAG_GFX is refused");
    check((bad & (1u << 2)) == 0, "OS32X_FLAG_CUI_ONLY refused only from GUI");
}

/* ------------------------------------------------------------------------ */
/*  起動要求表 (票 T9 D3 の受入): GUI 中の外部プログラム起動は端末 / sh から */
/*  カーネルの表を通って WM へ渡る。ここが壊れたとき実機で見えるのは         */
/*  「sh> から何も起動しない」「プロンプトに戻らない」「2 回目以降が         */
/*  ERR_FULL」だけで原因が遠いので、遷移の骨だけをブート時に踏む。          */
/*  ホスト試験 (tools/tests/test_launch.py) と同じ形。                       */
/* ------------------------------------------------------------------------ */
static void test_launch(void)
{
    u32 bad = launch_selftest();
    check((bad & (1u << 0)) == 0, "launch: child exit marks DONE and clears child");
    check((bad & (1u << 1)) == 0, "launch: a finished row is handed over once");
    check((bad & (1u << 2)) == 0, "launch: requester exit becomes an orphan KILL");
}

/* ------------------------------------------------------------------------ */
/*  KAPI が CPL=3 へ返す文字列の置き場 (票 T9 §12 R1)                       */
/*                                                                          */
/*  sys_getcwd はカーネル帯の static cwd をそのまま返していた。カーネル帯は  */
/*  USER ビット無しで張られるので、CPL=3 の sh.bin が `cd` / `pwd` で戻り値  */
/*  を読んだ瞬間に #PF → fault kill になる (実機で見えるのは「cd したら      */
/*  シェルが落ちる」だけ)。写し先はトランポリンページ (RO+USER) の空き。     */
/*  ここで踏むのは「その番地がページに収まり、PTE に USER が立っていて、     */
/*  CPL=0 の呼び手には従来どおり static cwd が返る」の 3 つ。                */
/* ------------------------------------------------------------------------ */
static void test_tramp_user_str(void)
{
    u32 bad = exec_tramp_user_selftest();
    check((bad & (1u << 0)) == 0, "getcwd scratch fits after the KAPI stubs");
    check((bad & (1u << 1)) == 0, "getcwd scratch page is present and USER");
    check((bad & (1u << 2)) == 0, "sys_getcwd copies only for CPL=3 callers");
    /* TASK_HDD_INSTALL 段 2: vfs_devname / path_get_* もカーネル帯を返していた */
    check((bad & (1u << 3)) == 0, "vfs_devname copies into the scratch for CPL=3");
    check((bad & (1u << 4)) == 0, "path_get_drive/cwd copy into the scratch for CPL=3");
}

/* ------------------------------------------------------------------------ */
/*  地図と実物の照合 (票 TASK_KSTACK_USER §4 の 3)                           */
/*                                                                          */
/*  PDE 0 の PTE 1024 本を include/memmap.h から導いた期待値と比べる。       */
/*  exec_init の **後** に呼ぶ — KAPI 踏み台ページ (RO+USER) が張られるのが  */
/*  exec_init なので、前に呼ぶとそれを食い違いとして数えてしまう。           */
/*                                                                          */
/*  逆転した範囲を撥ねた回数も同時に見る。paging_init の                     */
/*  paging_set_not_present(MEM_SHM_RESV_START, MEM_SHM_RESV_END) は          */
/*  start > end なので空振りしているが、呼び側が戻り値を見ていないため       */
/*  今まで誰も気づかなかった (票 §3)。                                       */
/* ------------------------------------------------------------------------ */
static void test_memmap(void)
{
    int bad = paging_memmap_selftest(exec_tramp_page_addr());
    u32 i, n;

    u32 *pt = (u32 *)P2V(paging_registered_pt(MEM_SHM_BASE));
    check(pt && (pt[(MEM_SHM_BASE >> PAGE_SHIFT) % PTE_COUNT] &
          (PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) == (PAGE_RW | PTE_USER),
          "paging: SHM boot USER RW WB");

    check(paging_range_reject_count == 0,
          "paging: no reversed (start > end) range was rejected");
    if (paging_range_reject_count != 0) {
        kprintf(0xC1, "[memmap] reversed ranges rejected: %d\n",
                (int)paging_range_reject_count);
    }

    if (bad <= 0) {
        check(bad == 0, "memmap: PDE 0 matches include/memmap.h");
        return;
    }

    /* 食い違いは **件数ぶん** kselftest_fail に積む。1 件にまとめると
     * 「1 か所ずれている」のか「帯ごと食い違っている」のかが見えない。 */
    n = paging_memmap_bad_count;
    if (n > MM_BAD_MAX) n = MM_BAD_MAX;
    for (i = 0; i < n; i++) {
        kprintf(0xC1, "[memmap] %x-%x want=%d seen=%d\n",
                paging_memmap_bad[i * 3 + 0], paging_memmap_bad[i * 3 + 1],
                (int)(paging_memmap_bad[i * 3 + 2] >> 4),
                (int)(paging_memmap_bad[i * 3 + 2] & 0xF));
        check(0, "memmap: band above differs from the map");
    }
}

/* ------------------------------------------------------------------------ */
/*  DMA プールの写像 (票 TASK_HAL_WIRING §1-3 / 受入 W5)                     */
/*                                                                          */
/*  プールは予約域 (NP) の中に開けた 64KB の穴で、present / supervisor /     */
/*  R/W でなければならない。**USER が立ってはいけない** — 立つと CPL=3 の   */
/*  アプリが装置の記述子 (CB/RFD、PCM リング) を書き換えられる。            */
/*                                                                          */
/*  この検査が意味を持つのは `memmap_seen_at` が MM_RW と MM_RWU を         */
/*  分けてからで、それより前は RW を見た時点で MM_RW を返していた。         */
/*  **見分けられることを毎回確かめる**ために、PTE 1 本に USER を立てて       */
/*  検査が落ちるところまで見て、戻す (往復 3 B6)。                          */
/* ------------------------------------------------------------------------ */
static void test_memmap_pool_user(void)
{
    u32 tramp = exec_tramp_page_addr();
    int clean, poked;

    /* 変異の前。ここが 0 でなければ test_memmap が既に報告している。 */
    clean = paging_memmap_selftest(tramp);
    check(clean == 0, "mmu:map ok b4");
    if (clean != 0) return;   /* 既に壊れている。変異しても意味が無い */

    if (paging_poke_user_bit(MEM_DMA_POOL_BASE, 1) != 0) {
        check(0, "mmu:PTE poke failed");
        return;
    }
    poked = paging_memmap_selftest(tramp);
    /* **必ず戻す** — 落ちたかどうかを見る前に戻しておく。 */
    (void)paging_poke_user_bit(MEM_DMA_POOL_BASE, 0);

    check(poked > 0, "mmu:USER bit caught");

    /* 戻したので、もう一度通ること。TLB は poke の中で無効化している。 */
    check(paging_memmap_selftest(tramp) == 0,
          "mmu:map ok after");
}

/* ------------------------------------------------------------------------ */
/*  物理ページの池 (票 docs/archive/v3/TASK_T1_LEDGER.md §4-1、T1a)             */
/*                                                                          */
/*  legacy の pgalloc_init への fallback は撤去した。8MB でも 17MB でも池は  */
/*  モデル経路で ONLINE でなければならない (legacy に戻ったら落ちる)。       */
/*  exec の上端は台帳の置き場ではなくアリーナの上端から決まるので、8MB でも */
/*  exec の最小域 (ロード起点 + スタック + sbrk + exec_heap) が残る (B2)。    */
/*  置き場 (FIXED 型の [0x2F9000, 0x2FB000)) の RW / USER なしは             */
/*  test_memmap の MM 検査が見る (T1-U1)。                                  */
/* ------------------------------------------------------------------------ */
static void test_pool_model(void)
{
    check(pgalloc_model_state() == PGALLOC_ONLINE, "pool:model online");
    check(sys_usable_mem_end() >= MEM_PHYS_EXEC_FLOOR + MEM_EXEC_STACK_SIZE +
          MEM_EXEC_SBRK_MIN + MEM_EXEC_HEAP_MIN, "pool:exec range");
}

/* ------------------------------------------------------------------------ */
/*  所有権台帳 (票 docs/archive/v3/TASK_T1_LEDGER.md §4-2、T1b)                 */
/*                                                                          */
/*  (1) 通常文脈では割り込み / 例外の深さが 0 (§3-5。longjmp の控えの戻し  */
/*      忘れや IRQ_LEAVE の抜けがあると 0 に戻らない)。                     */
/*  (2) 不変条件 (eligible で allocated ⇔ owner ≠ 0、pages と L2 の一致、   */
/*      区間の非重複) — ledger_selfcheck("boot")。                          */
/*  (3) R5 (a): AS を作り、その owner でページを取り、壊して回収すると AS    */
/*      owner のページが 0 になり、番号を返せる。                           */
/*  (4) R5 (b): 永続 owner の総量はその間に変わらない。                     */
/*  件数 (R1) は 1 行に出す。値そのものは kernel.map の番地で読む。          */
/* ------------------------------------------------------------------------ */
static u32 ledger_persist_total(void)
{
    u32 i, n = 0;
    for (i = 1; i <= LEDGER_OWNER_FIXED_LAST; i++) n += ledger_owner_pages(i);
    return n;
}

/* T2d: bounded boot smoke test. Reuse ledger's AS/data allocation, add only
 * one sparse APP PT. No CR3 switch, callbacks, or persistent test storage. */
static void test_caller_boot(struct addrspace *as, u32 phys)
{
    CallerAccessFrame previous;
    struct caller_access c = {0};
    char src[2] = {'d', 0}, dst[2] = {0};
    u32 pa = 0, root = paging_current_cr3();
    unsigned int flags, entry_flags = irq_save();
    irq_restore(entry_flags);
    int ok = caller_access_enter(&previous, CALLER_TRUSTED);
    check(ok, "caller:boot descriptor enter");
    if (ok) {
        check(caller_access_get(&c) && c.origin == CALLER_TRUSTED,
              "caller:boot trusted descriptor");
        check(copy_caller_cstr(&c, src, dst, sizeof(dst)) && dst[0] == 'd' && !dst[1],
              "caller:boot bounded cstr");
        dst[0] = 'x';
        check(!copy_caller_cstr(&c, src, dst, 1), "caller:boot missing NUL");
        dst[0] = 'x'; dst[1] = 'y';
        check(copy_to_caller(&c, dst, src, sizeof(src)) && dst[0] == 'd' && !dst[1],
              "caller:boot copyout");
        check(!copy_to_caller(&c, 0, src, 1), "caller:boot NULL output");
        caller_access_leave(&previous);
    }
    ok = paging_addrspace_map_user(as, MEM_EXEC_LOAD_ADDR, phys, PAGE_RO | PTE_USER) == 0;
    check(ok, "caller:boot RO map");
    if (ok) {
        flags = irq_save();
        ok = as_access_page(as, MEM_EXEC_LOAD_ADDR, 0, &pa) && pa == phys;
        check(ok, "caller:boot RO read walk");
        pa = phys;
        check(!as_access_page(as, MEM_EXEC_LOAD_ADDR, 1, &pa) && pa == phys,
              "caller:boot RO write unchanged");
        irq_restore(flags);
    }
    flags = irq_save();
    irq_restore(flags);
    check(((flags ^ entry_flags) & X86_EFLAGS_IF) == 0 && paging_current_cr3() == root,
          "caller:boot IF/CR3 unchanged");
}

static void test_ledger(void)
{
    struct addrspace as;
    enum { data_pages = 2 };
    const u32 expected_pages = PDE_COUNT * sizeof(u32) / PAGE_SIZE + data_pages;
    u32 persist, owner, phys, left, generation;
    int ok;

    check(kctx_irq_depth == 0 && kctx_exc_depth == 0, "ledger:ctx depth 0");
    check(ledger_selfcheck("boot"), "ledger:selfcheck boot");
    persist = ledger_persist_total();
    ok = ledger_owner_new(LEDGER_KIND_AS, 0, "kstest", &owner);
    check(ok, "ledger:AS owner new");
    if (ok) {
        ok = paging_addrspace_create(&as, owner) == 0;
        phys = ok ? pgalloc_alloc_phys(owner, data_pages) : 0;
        /* T2c: PD (PDE_COUNT entries) + data_pages。高位 PT は map 時の
         * 疎確保なので 0。create() は lease 先頭 PT も 0 (起動用の
         * create_lease() だけが 1 枚事前確保)。AS 制御はここでは stack。 */
        check(ok && phys && ledger_owner_pages(owner) == expected_pages, "ledger:AS alloc");
        if (ok) {
            if (phys) test_caller_boot(&as, phys);
            generation = as.generation;
            paging_addrspace_destroy(&as);
            ok = paging_addrspace_create(&as, owner) == 0;
            check(ok && generation && as.generation > generation,
                  "ledger:AS generation not reused");
            if (ok) paging_addrspace_destroy(&as);
        }
        left = 0;
        check(ledger_reclaim_owner(owner, &left) && left == (phys ? data_pages : 0) &&
              ledger_owner_pages(owner) == 0, "ledger:AS pages 0 (R5a)");
        check(ledger_owner_retire(owner), "ledger:AS owner retire");
    }
    check(ledger_persist_total() == persist, "ledger:persist unchanged (R5b)");
    check(ledger_selfcheck("boot"), "ledger:selfcheck after AS");
    kprintf(0x07, "[ledger] irq_ops=%u exc_ops=%u check_fail=%u bad_free=%u\n",
            ledger_irq_ops, ledger_exc_ops, ledger_check_fail, ledger_bad_free);
}

/* ------------------------------------------------------------------------ */
/*  gfx の予約・BB・SURFACE (票 docs/archive/v3/TASK_T1_LEDGER.md §4-5、T1e)    */
/*                                                                          */
/*  ⑥ (gfx_boot_reserve) の後・⑦ (probe) の前に走るので、選択中の backend  */
/*  はまだ決まっていない — 候補ごとに見る。                                  */
/*  (1) 3 地点のうち boot / gfx の ledger_selfcheck が落ちていない。         */
/*  (2) planar の固定 SURFACE (0x6A000、FIXED_RAM、owner = boot) が常にある。 */
/*  (3) sys_usable_mem_end() <= ledger_arena_top() (§3-6)。                 */
/*  (4) PEGC 候補なら BB は池の RAM で owner = boot、アリーナの上端より上、   */
/*      PEGC の窓が gfx の DEVICE 区間に入っている。                        */
/*  (5) Cirrus 候補なら CLIENT + DISPLAY の 2 本 (MMIO、UC、表示面は kernel)、 */
/*      どちらも gfx の DEVICE 区間 (Xe10-linear) の中。                     */
/* ------------------------------------------------------------------------ */
static int gfx_dev_covers(u32 pfn)
{
    const struct ledger_region *r;
    u32 i;
    for (i = 0; i < ledger_region_count; i++) {
        r = &ledger_regions[i];
        if (r->type == LEDGER_R_DEVICE && r->owner == LEDGER_OWNER_GFX &&
            r->first <= pfn && pfn < r->end) return 1;
    }
    return 0;
}

static void __attribute__((cold)) test_gfx_ledger(void)
{
    const struct ledger_surface *p, *e, *c, *d;
    u32 top = ledger_arena_top() * PAGE_SIZE;
    p = ledger_surface_find(LEDGER_SF_PC98, LEDGER_ROLE_CLIENT);
    e = ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_CLIENT);
    c = ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_CLIENT);
    d = ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_DISPLAY);
    check(ledger_check_fail == 0, "gfx:selfcheck boot+gfx");
    check(p && p->first * PAGE_SIZE == MEM_GFX_BB_BASE &&
          p->backing == LEDGER_SB_FIXED_RAM && p->owner == LEDGER_OWNER_BOOT,
          "gfx:planar surface");
    {
        const struct ledger_surface *pd = ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_DISPLAY);
        check(!pd || (pd->first == PEGC_LINEAR_BASE / PAGE_SIZE &&
              pd->npages == PEGC_FB_SIZE_480 / PAGE_SIZE &&
              pd->width == MEM_GFX_BB8_WIDTH && pd->height == MEM_GFX_BB8_HEIGHT &&
              pd->pitch == MEM_GFX_BB8_PITCH && pd->planes == 1 && !pd->plane_offset[0] &&
              pd->format == GFX_BB_PACKED8 && pd->owner == LEDGER_OWNER_KERNEL &&
              pd->backing == LEDGER_SB_VRAM && pd->cache == LEDGER_CACHE_UC &&
              pd->perm_max == LEDGER_PERM_RW && ledger_surface_validate(pd)),
              "gfx:pegc display");
    }
    {
        u32 gen = p ? p->gen : 0, first = p ? p->first : 0;
        check(p && ledger_surface_regen((u32)(p - ledger_surfaces), 0) &&
              p->gen == gen + 1 && p->first == first &&
              p->npages == MEM_GFX_BB_SIZE / PAGE_SIZE,
              "gfx:regen keeps backing");
    }
    check(sys_usable_mem_end() <= top, "gfx:usable <= arena top");
    check(!e || (e->owner == LEDGER_OWNER_BOOT && e->backing == LEDGER_SB_RAM &&
                 e->first * PAGE_SIZE >= top &&
                 gfx_dev_covers(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE)), "gfx:pegc bb");
    check(!c == !d && (!c || (c->backing == LEDGER_SB_MMIO &&
                              d->backing == LEDGER_SB_MMIO &&
                              c->cache == LEDGER_CACHE_UC && d->cache == LEDGER_CACHE_UC &&
                              d->owner == LEDGER_OWNER_KERNEL &&
                              gfx_dev_covers(c->first) && gfx_dev_covers(d->first))),
          "gfx:cirrus client+display");
    {
        const u32 planes[4] = {GVRAM_PLANE_B, GVRAM_PLANE_R, GVRAM_PLANE_G, GVRAM_PLANE_I};
        u32 i, n = 0, valid = 1, cache_ok = 1;
        u32 *pd = P2V(paging_kernel_pd_phys());
        u32 *pt = P2V(pd[0] & ~0xfffUL);
        for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
            const struct ledger_surface *sf = &ledger_surfaces[i];
            if (!sf->npages || sf->backend != LEDGER_SF_PC98 ||
                sf->role != LEDGER_ROLE_DISPLAY) continue;
            if (n >= 4 || sf->first * PAGE_SIZE != planes[n] ||
                sf->npages != GVRAM_PLANE_SIZE / PAGE_SIZE ||
                sf->cache != LEDGER_CACHE_UC || sf->planes != 1 ||
                sf->plane_offset[0] || !ledger_surface_validate(sf)) valid = 0;
            n++;
        }
        for (i = 0; i < MEM_1MB / PAGE_SIZE; i++)
            if ((pt[i] & (PTE_PCD | PTE_PWT)) !=
                (PC98_NATIVE_VRAM(i * PAGE_SIZE) ? PTE_PCD : 0)) cache_ok = 0;
        check(valid && n == 4, "gfx:planar display bundle");
        check(cache_ok, "gfx:native aliases UC, CG/ROM WB");
    }
}

/* ------------------------------------------------------------------------ */
/*  PCM (票 TASK_PCM_CS4231): 起動時の検出が走った後、driver が CLOSED で    */
/*  待っていること。装置の有無は機種で変わるので**状態だけ**を見る           */
/*  (NP21/W の既定構成には CS4231 が無い — 無くても壊れないのが要件)。       */
/*  ついでに、知らないレートは**装置に 1 バイトも書かずに**断ること。        */
/* ------------------------------------------------------------------------ */
static void test_pcm(void)
{
    check(pcm_state() == PCM_ST_CLOSED, "pcm:closed");
    check(pcm_open(PCM_RATE_44100 + 1) == OS32_ERR_INVAL, "pcm:rate refused");
    check(pcm_state() == PCM_ST_CLOSED, "pcm:still closed");
}

/* KAPI データ欄の固定配置と予約スロット (票 TASK_KAPI_DATA_FIELDS、v63)。
 * 配置が動くと旧バイナリの malloc が黙って全部 ENOMEM になる。予約スロットが
 * NULL だと旧 SDK の呼び出しが 0 番地へ飛ぶ。 */
static void test_kapi_layout(void)
{
    u32 bad = exec_kapi_layout_selftest();
    check((bad & (1u << 0)) == 0, "kapi:data@fixed");
    check((bad & (1u << 1)) == 0, "kapi:rsv nosys");
    check((bad & (1u << 2)) == 0, "kapi:rsv stubs");
    check((bad & (1u << 3)) == 0, "kapi:hdr v3");
}

int kselftest_run_post_unicode(void)
{
    int before = ksel_fail;
    const struct ledger_surface *sf = ledger_surface_find(0, LEDGER_ROLE_UNICODE);
    check(sf && sf->owner == LEDGER_OWNER_KERNEL && !sf->backend &&
          sf->perm_max == LEDGER_PERM_RO && sf->backing == LEDGER_SB_FIXED_RAM &&
          sf->first == MEM_UNICODE_TABLE_BASE / PAGE_SIZE &&
          sf->npages == MEM_UNICODE_TABLE_SIZE / PAGE_SIZE &&
          ledger_surface_validate(sf), "unicode:registered RO backend0");
    return ksel_fail - before;
}

int kselftest_run_post_exec(void)
{
    int before = ksel_fail;

    test_tramp_user_str();
    test_kapi_layout();
    test_memmap();
    test_memmap_pool_user();
    test_pool_model();
    test_ledger();
    test_gfx_ledger();
    test_pcm();

    if (ksel_fail != before) {
        kprintf(0xC1, "[selftest] %d FAILED after exec_init\n",
                ksel_fail - before);
    }
    return ksel_fail - before;
}

/* ------------------------------------------------------------------------ */
/*  GUI 中の CTRL+STOP は WM が宛先を決める (票 T9 §12 S6)                   */
/*                                                                          */
/*  IRQ1 は「そのとき走っていた slot」しか知らないが、GUI 配下の宛先は       */
/*  フォーカス窓の連鎖の末尾 (D8) で、それを解決できるのは WM だけ。         */
/*  ここが崩れると実機では「CTRL+STOP で端末まで消える」(立て過ぎ) か        */
/*  「暴走したアプリを畳めない」(立て無さ過ぎ) としか見えない。              */
/* ------------------------------------------------------------------------ */
static void test_abort_admit(void)
{
    u32 bad = appslot_abort_admit_selftest();
    check((bad & (1u << 0)) == 0, "CUI keeps the CTRL+STOP escape hatch (K2)");
    check((bad & (1u << 1)) == 0, "GUI running app: target is left to the WM");
    check((bad & (1u << 2)) == 0, "GUI still kills a runaway app (2s no syscall)");
    check((bad & (1u << 3)) == 0, "CTRL+STOP never lands on the shell band");
    check((bad & (1u << 4)) == 0, "GUI keeps the K5c path (inside gui_call OP_WAIT)");
}

/* ------------------------------------------------------------------------ */
/*  設定レジストリの基盤 (票 S0-K、KAPI v50)                                 */
/*                                                                          */
/*  ここで踏むのは 2 つだけ。(a) KAPI の表が v50 の形か — 末尾追記の 7 本が   */
/*  201..207 に居て既存 db_* が動いていないこと。ずれると外部プログラムは     */
/*  「別の関数を呼ぶ」という最も静かな壊れ方をする。(b) 16KB の結果ブロックの */
/*  境界検査 — header + 全列 descriptor + payload が溢れる行で範囲外へ書かず  */
/*  部分 ROW も返さないこと (票 §1b)。                                       */
/*  ホスト試験 (tools/tests/test_kapi_db_v50.py) と同じ判定を使う。          */
/* ------------------------------------------------------------------------ */
static void test_db_v50(void)
{
    u32 bad = db_v50_selftest();
    check((bad & (1u << 0)) == 0, "KAPI v50: 7 new db slots appended at 201..207");
    check((bad & (1u << 1)) == 0, "db row: 16KB block bound counts descriptors");
    check((bad & (1u << 2)) == 0, "db ptr: NULL and length overflow refused");
    check((bad & (1u << 3)) == 0, "db path: journal name fits the VFS capacity");
    check((bad & (1u << 4)) == 0, "db diag: one open-failure slot per owner ID");
    /* 票 TASK_DB_ERRSTR: db_last_error() の返り先 (SHM 末尾の診断領域) が
     * 結果データと重なっていないこと。重なると結果が診断文を踏み潰す。 */
    check((bad & (1u << 5)) == 0, "db diag: error string area is outside the result area");
    /* owner 別の欄が ID の池を覆っているか (kapi_db.h の DB_OWNER_SLOTS)。 */
    check(DB_OWNER_SLOTS >= APP_SLOT_COUNT,
          "db diag: DB_OWNER_SLOTS covers the whole app ID pool");
}

/* CPU 校正が**丸めに負けていない**こと (票 TASK_SERIAL_VFAST 往復 3)。
 *
 * 直す前は校正ループを 1 周だけ回して tick で割っていた。実機の
 * PC-9821Ra266 (266MHz) では 1 周が 1 tick に満たず `elapsed = 0 → 1` に
 * 丸められ、`s_loops_per_tick` が実際の 1/7〜1/13 になっていた。すると
 * `cpu_delay_us(5)` が 0.5µs しか待たず、シリアルの送信ループが TxRDY を
 * 待てずに `_halt()` へ落ちて **1 バイト約 2ms の固定費**になる
 * (実機実測: 9600 で 389B/s、38400 でも 437B/s)。
 *
 * **NP21/W でも丸めは起きる** (実測 rounds = 16 / ticks = 5 = 1 周 ≒ 0.31
 * tick。旧コードの生の elapsed はそこでも 0 で、補正が 200,000 を入れていた)。
 * 実機 266MHz は 1 周 0.1 tick 未満でもっと深く落ちる。症状が出るかどうかの
 * 境目は「予算が 1 文字時間を割り込むか」だけなので、ここで起動時に見る。 */
static void test_cpu_calibrate(void)
{
    u32 lpt = cpu_loops_per_tick();

    /* 測れていれば必ず下限を超える。フォールバックに倒れたら測れていない。 */
    check(lpt >= CALIBRATE_MIN_LPT,
          "cpu calib: loops_per_tick is above the floor");
    check(lpt != 0, "cpu calib: loops_per_tick was actually set");

    /* **必要な tick 数を本当に測れたか。** ここが 5 未満なら、周回を
     * 打ち切ってしまったか PIT が止まっている = 値は当てにならない。 */
    check(cpu_calib_ticks >= CALIBRATE_MIN_TICKS,
          "cpu calib: measured at least CALIBRATE_MIN_TICKS ticks");
    check(cpu_calib_rounds >= 1, "cpu calib: ran at least one round");
    /* 打ち切りに当たっていない (当たっていたら PIT を疑う)。 */
    check(cpu_calib_rounds < CALIBRATE_MAX_ROUNDS,
          "cpu calib: did not hit the round cap");

    /* **結果が測った値そのものか。** `lpt == 合計ループ / 経過 tick` を
     * 周回数に関わらず照合する。直す前のここは `|| cpu_calib_rounds > 1` が
     * 付いていて、**複数周回ったら何であれ通る**ザルだった (往復 4 の非
     * blocker)。合計は rounds × CALIBRATE_LOOPS = 最大 4000 万で u32 に収まる。
     * フォールバックに倒れた場合だけ式から外れるので、それは別に許す
     * (倒れたこと自体は下の check が落とす)。 */
    check(cpu_calib_ticks == 0
          || lpt == (cpu_calib_rounds * CALIBRATE_LOOPS) / cpu_calib_ticks
          || lpt == CALIBRATE_FALLBACK_LPT,
          "cpu calib: loops_per_tick == total loops / ticks");

    /* **打ち切りに当たったら測れていない** (PIT が止まっている疑い)。
     * 上の `< CALIBRATE_MAX_ROUNDS` と同じことを「失敗」として言い直す —
     * 当たったときに何が起きたかを名前で残すため。 */
    check(cpu_calib_rounds != CALIBRATE_MAX_ROUNDS,
          "cpu calib: did NOT give up at the round cap");

    /* **フォールバック値そのものだったら測れていない。**
     * cpu_calibrate_compute() は ticks == 0 か極端に小さい結果のときだけ
     * この値を返すので、一致したら測定が成立していない
     * (8MHz 実機でたまたま一致する確率は無視する — その場合も
     *  「測れたかどうか分からない」ので落ちてよい)。 */
    check(lpt != CALIBRATE_FALLBACK_LPT,
          "cpu calib: result is a real measurement, not the fallback");
}

/* PIT の分周が**判定したクロックに従っている**こと (票 TASK_HAL_WIRING §1-0)。
 *
 * 直す前の `pit_init()` は 1.9968MHz 決め打ちで割っていた。2.4576MHz 系
 * (実機 PC-9821Ra266、0000:0501h bit7 = 0) では 24576 を積むべきところに
 * 19968 が入り、100Hz のつもりの tick が **123Hz = 8.125ms** になる。
 * tick を数える待ち・番犬・CPU 校正がまとめて 23% 速くなるが、
 * **NP21/W は 1.9968MHz 設定なので一度も踏めない**。だからここで起動時に
 * 実機の値そのものを見る (ホスト試験 tools/tests/test_pit_clock.py は
 * 同じ算数を両クロックで見る)。 */
/* ------------------------------------------------------------------------ */
/*  ブート情報域 (票 TASK_HDD_INSTALL 段 0)                                  */
/*                                                                          */
/*  起動時の情報域そのものは FD 起動 / HDD 起動 / HDD 無しで答えが違うので    */
/*  「写したこと」と、検証関数が良い域を通し壊れた域を断ることだけを見る。   */
/*  規則の網羅はホスト試験 (tools/tests/test_bootinfo.py)。                 */
/* ------------------------------------------------------------------------ */
static void test_bootinfo(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;
    u8 *d0 = raw + BI_OFF_DRIVE0;
    u16 c = 0, sec = 0;
    u8 h = 0, s = 0;
    int i;

    /* kernel_main の最初で写している (NONE のままなら呼び忘れ)。 */
    check(bootinfo_get()->status != BOOTINFO_ERR_NONE, "bootinfo:captured");
    /* 写した後は低位の magic を消している (次の起動で残りを読まない)。 */
    check(*(volatile u32 *)P2V_IO(MEM_BOOTINFO_BASE) != BOOTINFO_MAGIC,
          "bootinfo:low magic cleared");
    /* 有効な域なら、使えると言ったドライブの幾何が取れる。 */
    if (bootinfo_get()->status == BOOTINFO_OK &&
        bootinfo_get()->drive[0].valid) {
        check(bootinfo_hdd_geom(bootinfo_get()->drive[0].da, &c, &h, &s, &sec) == 0
              && sec == 512 && h != 0 && s != 0, "bootinfo:geom");
    }
    /* v2 のローダ (FD / HDD) は vmkernel.lz4 を検査し終えてから、起動した
     * イメージの CRC をイメージ欄に残す (票 TASK_SERIAL_HOSTFS A-4)。
     * 主部が有効なのに記録が無ければ、ローダが古いか書き忘れ。 */
    if (bootinfo_get()->status == BOOTINFO_OK) {
        check(bootinfo_get()->img_valid == 1 && bootinfo_get()->img_size != 0,
              "bootinfo:image crc recorded");
    }

    for (i = 0; i < (int)sizeof(raw); i++) raw[i] = 0;
    raw[BI_OFF_VERSION] = (u8)BOOTINFO_VERSION;
    raw[BI_OFF_SOURCE] = (u8)BOOTINFO_SRC_FD;
    raw[BI_OFF_NDRIVES] = (u8)BOOTINFO_NDRIVES;
    d0[BI_DRV_DA] = 0x80; d0[BI_DRV_VALID] = 1; d0[BI_DRV_QUERIED] = 1;
    d0[BI_DRV_BX] = 0x00; d0[BI_DRV_BX + 1] = 0x02;       /* 512 */
    d0[BI_DRV_CX] = 0x10; d0[BI_DRV_CX + 1] = 0x01;       /* 272 */
    d0[BI_DRV_DH] = 8; d0[BI_DRV_DL] = 17;
    bootinfo_seal(raw);
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK &&
          bi.drive[0].valid == 1 && bi.drive[0].cyl == 272, "bootinfo:good");
    bootinfo_seal_image(raw, 0x12345678UL, 447337UL);
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK &&
          bi.img_valid == 1 && bi.img_crc == 0x12345678UL && bi.img_size == 447337UL,
          "bootinfo:image good");
    raw[BI_OFF_IMG_CRC] ^= 1;
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK && bi.img_valid == 0,
          "bootinfo:image bad check");
    raw[BI_OFF_CHECK] ^= 1;
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_CHECK,
          "bootinfo:bad check");
}

static void test_pit_setup(void)
{
    const struct pit_setup *p = pit_get_setup();

    /* 積んでいない (= pit_init が値を記録していない) なら以後は無意味。 */
    check(p->valid != 0, "pit:setup recorded");

    /* **クロックを読んでいること。** 未判定のまま既定値で走ると、
     * 2.4576MHz 機が直す前と同じ分周のままになる。 */
    check(sysclk_detected() != 0, "pit:sysclk detected");

    /* 分周に使ったのが判定値そのものか (既定へ倒れていないか)。 */
    check(p->clk_hz == sysclk_hz(), "pit:clk=sysclk");
    check(p->hz == (unsigned int)PIT_HZ, "pit:hz=PIT_HZ");
    check(p->reload == (unsigned int)(sysclk_hz() / (unsigned long)PIT_HZ),
          "pit:reload=clk/hz");

    /* **どちらのクロックでもちょうど 10ms。** ここが 8125 なら 19968 を
     * 2.4576MHz 機に積んでいる (直す前の姿)。 */
    check(p->period_us == (unsigned int)(1000000UL / (unsigned long)PIT_HZ),
          "pit:period=1/hz");

    /* カウンタ#0 / LSB-MSB / モード2 (レートジェネレータ) / バイナリ。
     * モードが変わると周期そのものの意味が変わる。 */
    check(p->mode == (u8)PIT_MODE_TIMER0,
          "pit:ch0 mode2");
}

/* ------------------------------------------------------------------------ */
/*  DMA プールの配り方 (票 TASK_HAL_WIRING §1-3、TASK_T1_LEDGER §3-7 / R4)   */
/*                                                                          */
/*  表の算数はホスト試験 (tools/tests/test_dma_pool.py) が見る。ここが       */
/*  見るのは **実物の池**: 最悪の並び (R4) が入ること、組が va = P2V(pa)    */
/*  で 16MB 未満・64KB 非またぎであること、失敗時に *out を書かないこと、   */
/*  枯渇と解放が起動のたびに一度踏まれること。**池の番地は焼かない**        */
/*  (T3 で池が 64KB 整列の番地へ移っても同じ試験が通る)。                   */
/*                                                                          */
/*  **後片付けまでが試験。** 途中で return すると池が埋まったまま残り、      */
/*  82557 の probe が取れなくなる。                                          */
/* ------------------------------------------------------------------------ */
#define DMAP_NIC_BYTES   (16UL * 1024UL)  /* 82557 の CB/RFD ≒16KB (R4) */
#define DMAP_HOLE_BYTES  (8UL * 1024UL)   /* 最悪の並びの前置き */

static int dmap_buf_ok(const struct dma_buf *b, u32 size)
{
    return b->size == size && b->va == P2V(b->pa) &&
           !dma_crosses_64k(b->pa, size) &&
           b->pa < DMA_PHYS_LIMIT && size <= DMA_PHYS_LIMIT - b->pa;
}

static void test_dma_pool(void)
{
    struct dma_buf h1, h2, ring, nic, a, b, c, d, x;
    int pre, got_h1, got_h2, got_r, got_n;
    int fit = 1, apart = 1;
    u32 bad_before = dma_pool_bad_free();
    u32 leak_before = dma_pool_leaked();

    check(dma_pool_free_pages() == DMA_POOL_PAGES, "dmap:starts empty");

    /* R4 の最悪の並び: PCM リング 16KB + 82557 ≒16KB。前置きは
     * 0 = 空 / 1 = 先頭に 8KB 使用中 / 2 = 先頭に 8KB の穴 (次の 8KB は
     * 使用中)。どの前置きでも 2 本とも入り、重ならず、64KB をまたがず
     * 16MB 未満であること。 */
    for (pre = 0; pre < 3; pre++) {
        got_h1 = got_h2 = 0;
        if (pre >= 1) {
            got_h1 = (dma_alloc(DMAP_HOLE_BYTES, 0, DMA_PHYS_LIMIT, &h1) == 0);
            if (!got_h1) fit = 0;
        }
        if (pre == 2) {
            got_h2 = (dma_alloc(DMAP_HOLE_BYTES, 0, DMA_PHYS_LIMIT, &h2) == 0);
            if (!got_h2) fit = 0;
            if (got_h1) {
                if (dma_free(&h1) == 0) got_h1 = 0;
                else fit = 0;
            }
        }
        got_r = (dma_alloc(PCM_RING_BYTES, PCM_POOL_ALIGN, DMA_PHYS_LIMIT,
                           &ring) == 0);
        got_n = (dma_alloc(DMAP_NIC_BYTES, 0, DMA_PHYS_LIMIT, &nic) == 0);
        if (!got_r || !got_n) fit = 0;
        if (got_r && !dmap_buf_ok(&ring, PCM_RING_BYTES)) fit = 0;
        if (got_n && !dmap_buf_ok(&nic, DMAP_NIC_BYTES)) fit = 0;
        if (got_r && got_n && ring.pa < nic.pa + DMAP_NIC_BYTES &&
            nic.pa < ring.pa + PCM_RING_BYTES)
            apart = 0;
        if (got_r) (void)dma_free(&ring);
        if (got_n) (void)dma_free(&nic);
        if (got_h1) (void)dma_free(&h1);
        if (got_h2) (void)dma_free(&h2);
    }
    check(fit, "dmap:R4 worst fits");
    check(apart, "dmap:R4 disjoint");
    check(dma_pool_free_pages() == DMA_POOL_PAGES, "dmap:R4 all back");

    /* 失敗時 *out は不変 (内部の 0 を外へ漏らさない)。 */
    x.pa = 0x5A5A5000UL;
    x.va = (void *)0;
    x.size = 0x77;
    check(dma_alloc(33 * 1024, 0, DMA_PHYS_LIMIT, &x) < 0 &&
          x.pa == 0x5A5A5000UL && x.size == 0x77, "dmap:fail keeps out");
    /* limit が池の先頭なら、どの候補も上限を越える。 */
    check(dma_alloc(4096, 0, (u32)MEM_DMA_POOL_BASE, &x) < 0 &&
          x.pa == 0x5A5A5000UL && x.size == 0x77, "dmap:limit refused");
    check(dma_alloc(0, 0, DMA_PHYS_LIMIT, &x) < 0, "dmap:0 bytes refused");

    /* 真ん中を返すと、そこに 8KB が入る。 */
    check(dma_alloc(16 * 1024, 0, DMA_PHYS_LIMIT, &a) == 0 &&
          dma_alloc(16 * 1024, 0, DMA_PHYS_LIMIT, &b) == 0 &&
          dma_alloc(16 * 1024, 0, DMA_PHYS_LIMIT, &c) == 0,
          "dmap:3x16KB fit");
    check(dma_free(&b) == 0, "dmap:free middle");
    check(dma_alloc(8 * 1024, 0, DMA_PHYS_LIMIT, &d) == 0 && d.pa == b.pa,
          "dmap:8KB reuses");

    /* 途中ポインタの解放は数える (装置がまだ書いているかもしれない)。 */
    x = a;
    x.pa += 4096;
    check(dma_free(&x) < 0, "dmap:mid ptr refused");
    check(dma_pool_bad_free() == bad_before + 1, "dmap:bad free cnt");

    /* LEAKED は二度と配らない。 */
    check(dma_mark_leaked(&c) == 0, "dmap:mark_leaked ok");
    check(dma_pool_leaked() == leak_before + 1, "dmap:leak cnt");
    check(dma_free(&c) < 0, "dmap:leaked no free");

    /* 後片付け。LEAKED の c は**戻せない**ので、その 16KB は使えないまま。
     * 起動ごとの自己診断で池を削るわけにはいかないので、
     * **プールを作り直す**。そのため kernel.c は `pci_bind_all` を
     * **この自己診断より後**で呼ぶ (先に呼ぶと driver の span が消える)。 */
    (void)dma_free(&a);
    (void)dma_free(&d);
    dma_pool_init();
    check(dma_pool_free_pages() == DMA_POOL_PAGES,
          "dmap:empty again");
}

/* ------------------------------------------------------------------------ */
/*  8237 の共通部 (票 TASK_HAL_WIRING §1-2)                                  */
/*                                                                          */
/*  **悪い引数でハードウェアに触らないこと**だけを見る。良い引数の転送は     */
/*  FDC が起動のたびに踏んでいる (W2)。ここで出せない out が 1 つでも        */
/*  出ると、他チャネル (CS4231) の設定を壊す。                              */
/* ------------------------------------------------------------------------ */
static void test_dma8237(void)
{
    int done = -1, tc = -1;

    check(dma8237_ready() != 0, "dma:init b4 fdc");
    check(dma_above_1mb_state() != DMA_A20_UNKNOWN,
          "dma:0439h recorded");

    /* ch は 0〜3。範囲外は表も引かない。 */
    check(dma_chan_setup(DMA_CHAN_COUNT, MEM_DMA_POOL_BASE, 512,
                         DMA_DIR_TO_MEM, DMA_MODE_SINGLE) < 0,
          "dma:bad ch refused");
    /* 0 バイトは積めない (カウントに -1 を積むので 65536 になる)。 */
    check(dma_chan_setup(2, MEM_DMA_POOL_BASE, 0, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0,
          "dma:0 bytes refused");
    /* 64KB バンクまたぎ ([HW2])。プールの境界 0x2F0000 の手前から。 */
    check(dma_chan_setup(3, MEM_DMA_POOL_BASE + 0x7000UL, 0x4000UL,
                         DMA_DIR_TO_MEM, DMA_MODE_SINGLE) < 0,
          "dma:64KB cross refused");
    /* 16MB 以上 */
    check(dma_chan_setup(3, 0x1000000UL, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0,
          "dma:>=16MB refused");

    /* 引数が通っても **マスクしていなければ積まない**。ch3 は誰も使って
     * いないので、ここで触っても他の装置に当たらない。 */
    check(dma_chan_setup(3, MEM_DMA_POOL_BASE, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0,
          "dma:unmasked refused");

    /* マスクしてから積むと通り、done / tc_event が落ちている。
     * **積んだだけでマスクは外れない**ので、ch3 は閉じたまま。 */
    dma_chan_mask(3);
    check(dma_chan_setup(3, MEM_DMA_POOL_BASE, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) == 0,
          "dma:masked setup ok");
    check(dma_chan_state(3, &done, &tc) == 0, "dma:state readable");
    check(done == 0 && tc == 0, "dma:setup cleared TC");
    dma_chan_mask(3);   /* 念のため閉じたままにしておく */
}

/* ------------------------------------------------------------------------ */
/*  W3: 動的 IRQ の登録・ディスパッチ (票 TASK_HAL_WIRING §1-1)             */
/*                                                                          */
/*  **PIC のエッジを使わずに経路を通す**: IDT ゲート 0x23 (IRQ3) を          */
/*  `int $0x23` で直に叩く。共通スタブ → irq_dispatch → 表 → irq_finish     */
/*  まで本物が走り、最後の EOI は「ISR ビットが 1 つも立っていない 8259 への */
/*  非特定 EOI」= 無動作になる (CPL=0 の通常コードでは ISR は空)。           */
/*  実 IRQ での確認 (master 3 / slave 9、/api/pic で IRR・ISR を見る) は      */
/*  W3 の NP21/W 側が持つ。                                                  */
/*                                                                          */
/*  判定の網羅そのものはホスト試験 (tools/tests/test_irq_math.py) にある。    */
/*  ここで見るのは「配線が本当に通っているか」— 表・PIC のマスク・スタブ。   */
/* ------------------------------------------------------------------------ */
#define KSEL_IRQ_LINE   3
#define KSEL_IRQ_VECTOR "$0x23"

struct ksel_fake_dev {
    int id;
    int calls;
    int ret[4];
    int try_register_rc;   /* ISR 文脈からの登録を試した結果 (0 = 試していない) */
    int try_register;
};

static struct ksel_fake_dev ksel_dev[5];
static int ksel_irq_log[8];
static int ksel_irq_log_n;

static int ksel_fake_irq(unsigned int irq, void *arg)
{
    struct ksel_fake_dev *d = (struct ksel_fake_dev *)arg;
    int rc;

    (void)irq;
    rc = d->ret[(d->calls < 4) ? d->calls : 3];
    if (ksel_irq_log_n < 8) ksel_irq_log[ksel_irq_log_n++] = d->id;
    d->calls++;
    if (d->try_register) {
        d->try_register = 0;
        /* **ISR の中からの登録は契約違反** — 断られることを見る。 */
        d->try_register_rc = irq_register(5, ksel_fake_irq, &ksel_dev[4],
                                          IRQ_F_SHARED);
    }
    return rc;
}

static void ksel_raise_irq3(void)
{
    __asm__ __volatile__("int " KSEL_IRQ_VECTOR);
}

/* PIC のマスク (IMR) を読む。1 = マスク中。 */
static int ksel_irq_masked(unsigned int irq)
{
    u8 imr = (u8)inp((irq < 8) ? PIC1_DATA : PIC2_DATA);
    return (int)((imr >> (irq & 7)) & 1);
}

static void ksel_irq_reset_devs(void)
{
    int i, k;
    for (i = 0; i < 5; i++) {
        ksel_dev[i].id = i;
        ksel_dev[i].calls = 0;
        ksel_dev[i].try_register = 0;
        ksel_dev[i].try_register_rc = 0;
        for (k = 0; k < 4; k++) ksel_dev[i].ret[k] = IRQ_NONE;
    }
    ksel_irq_log_n = 0;
}

/* 試験の前後で見比べる IRQ3 の状態 (課題: 実機 Ra266 の 82557 が IRQ3)。 */
struct ksel_irq_snap {
    int  masked;        /* PIC の IMR (1 = マスク) */
    int  quarantined;   /* irq_line_quarantined のビット */
    int  storm;         /* irq_storm_masked のビット */
    int  count;         /* 登録数 */
    int  ln_q;          /* 表の quarantined / storm_masked */
    int  ln_s;
};

static void ksel_irq_snap_take(struct ksel_irq_snap *s)
{
    int idx = irq_dyn_index(KSEL_IRQ_LINE);
    s->masked      = ksel_irq_masked(KSEL_IRQ_LINE);
    s->quarantined = (irq_line_quarantined & (1u << KSEL_IRQ_LINE)) ? 1 : 0;
    s->storm       = (irq_storm_masked & (1u << KSEL_IRQ_LINE)) ? 1 : 0;
    s->count       = irq_lines[idx].count;
    s->ln_q        = irq_lines[idx].quarantined;
    s->ln_s        = irq_lines[idx].storm_masked;
}

static void test_irq_dynamic_body(int skip_unmask);

/* IMR を開ける部分 (test_irq_dynamic_body の skip_unmask より後) の check 数。
 * IRR に本物の要求が来ていて飛ばしたとき、飛ばした件数としてログに出す
 * (実機の合計が NP21/W より少なく見える理由を行で分かるように。レビュー
 * 往復 2)。**数え違いは本体の最後の "irq:unmask count" が検出する** —
 * 飛ばさなかった起動 (NP21/W) で、実際に走った check の数と突き合わせる。
 * 飛ばす件数は、その突き合わせの 1 件を足した値。 */
#define KSEL_IRQ_UNMASK_CHECKS   28
#define KSEL_IRQ_UNMASK_SKIPPED  (KSEL_IRQ_UNMASK_CHECKS + 1)

/* マスタ PIC の IRR (要求が来ているか) を OCW3 で読む。IMR に関係なく立つ。
 * 読んだ後は既定の IRR 読み出しのまま (irq_finish と同じ流儀)。 */
static int ksel_irq_pending(unsigned int irq)
{
    u8 irr;
    outp(PIC1_CMD, OCW3_IRR);
    irr = (u8)inp(PIC1_CMD);
    return (int)((irr >> (irq & 7)) & 1);
}

/* ------------------------------------------------------------------------ */
/*  IRQ3 は **本物の線** (実機 Ra266 では内蔵 LAN の 82557 が PCI Line 3 で   */
/*  上げたままにしている)。この試験は PIC のマスクを登録数で持つことを見る   */
/*  ので、途中で本当に IRQ3 の IMR を開ける。そこへ本物の IRQ3 が入ると:     */
/*    - 偽の登録者が IRQ_NONE で断り、呼ばれた回数の判定が揺れる。          */
/*    - 試験が終わった後の線の状態が、試験の前と同じだと言えなくなる。      */
/*  そこで:                                                                  */
/*    1. 試験の本体を **IF=0 で** 回す。`int $0x23` は IF に関係なく通るが、  */
/*       PIC からの本物の IRQ3 は CPU が受け取らない (IMR を開けていても)。  */
/*       本体の最後で登録数が 0 に戻り、irq_recount_locked が IMR を閉じて   */
/*       から IF を戻す。                                                    */
/*    2. 試験の前に線が「素」(未登録・未隔離・ストーム無し・マスク) でなけ   */
/*       れば **試験しない** — 本物の登録者の線を試験で上書きしない。        */
/*    3. 試験の後、線の状態 (IMR・隔離・ストーム・登録数) が試験の前と同じ   */
/*       ことを **ここで検査する**。誰も受けなかった数 (irq_unexpected) も   */
/*       試験の前の値へ戻す (実機の切り分けで kernel.map から読む数)。       */
/*    4. 試験がわざと起こすストーム・隔離・unclaimed の表示は irq_test_quiet */
/*       で黙らせる。起動画面に本物の障害と同じ文言で出ていたうえ、        */
/*       unclaimed の表示は線ごと 1 回きりなので、試験が IRQ3 の 1 回を使い   */
/*       切って本物の装置の unclaimed が出なくなっていた。                   */
/* ------------------------------------------------------------------------ */
static void test_irq_dynamic(void)
{
    struct ksel_irq_snap before, after;
    u32 unexpected_before;
    unsigned int saved;
    int pending;

    ksel_irq_snap_take(&before);
    if (before.count != 0 || before.quarantined || before.storm ||
        before.ln_q || before.ln_s || !before.masked) {
        kprintf(0xC1, "[selftest] irq: IRQ%d not pristine "
                "(cnt=%d q=%d storm=%d masked=%d) -> skipped\n",
                KSEL_IRQ_LINE, before.count, before.quarantined,
                before.storm, before.masked);
        check(0, "irq:line pristine b4 test");
        return;
    }

    saved = irq_save();
    /* **本物の要求が IRR に来ていたら IMR を開けない** (レビュー往復 1)。
     * IF=0 でも、要求の立った線の IMR を開けて閉じると 8259 が INT を上げて
     * 下ろすことになり、偽の割り込み (IR7 / IRQ15 のスプリアス) を招きうる。
     * それが 1 回きりの unclaimed 表示を食うのを避ける。この場合は IMR を
     * 開けない拒否規則だけを見て、残りは飛ばしたことをログに残す
     * (FAIL にはしない — 装置が線を上げているのは試験の失敗ではない)。
     * 実機 Ra266 では内蔵 LAN の 82557 が IRQ3 を上げたままのはず (未確認)。 */
    pending = ksel_irq_pending(KSEL_IRQ_LINE);
    unexpected_before = irq_unexpected;
    irq_test_quiet = 1;
    test_irq_dynamic_body(pending);
    irq_test_quiet = 0;
    irq_unexpected = unexpected_before;
    ksel_irq_snap_take(&after);
    irq_restore(saved);

    if (pending)
        kprintf(0x07, "[selftest] irq: IRQ%d pending in IRR -> unmask part "
                "skipped %d checks (not a failure)\n", KSEL_IRQ_LINE,
                (int)KSEL_IRQ_UNMASK_SKIPPED);

    check(after.masked == before.masked &&
          after.quarantined == before.quarantined &&
          after.storm == before.storm &&
          after.count == before.count &&
          after.ln_q == before.ln_q && after.ln_s == before.ln_s,
          "irq:line restored");
}

/* 本体は **IF=0 で** 呼ぶこと (上の注記)。skip_unmask が 1 なら、IMR を
 * 開けない拒否規則だけを見て戻る。 */
static void test_irq_dynamic_body(int skip_unmask)
{
    u32 ctx_before = irq_ctx_violations;
    u32 deferred_before;
    int unmask_base;
    int i;

    ksel_irq_reset_devs();

    /* --- 拒否規則 (配線の確認。網羅はホスト試験) --- */
    check(irq_register(0, ksel_fake_irq, &ksel_dev[0], 0) == IRQ_ERR_NOTSUP,
          "irq:reg0=NOTSUP");
    check(irq_register(11, ksel_fake_irq, &ksel_dev[0], 0) == IRQ_ERR_NOTSUP,
          "irq:reg11=NOTSUP");
    check(irq_register(16, ksel_fake_irq, &ksel_dev[0], 0) == IRQ_ERR_INVAL,
          "irq:reg16=INVAL");
    check(irq_register(KSEL_IRQ_LINE, 0, &ksel_dev[0], 0) == IRQ_ERR_INVAL,
          "irq:regNULL=INVAL");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0], 0x80)
          == IRQ_ERR_INVAL, "irq:regflag=INVAL");

    if (skip_unmask) return;   /* ここから先は IRQ3 の IMR を開ける */
    unmask_base = ksel_pass + ksel_fail;

    /* --- 登録数で PIC のマスクを持つ --- */
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 1, "irq:masked b4 reg");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0], IRQ_F_SHARED)
          == 0, "irq:reg1 ok");
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 0, "irq:reg1 unmasks");

    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0], IRQ_F_SHARED)
          == IRQ_ERR_EXIST, "irq:dup=EXIST");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1], 0)
          == IRQ_ERR_SHARE, "irq:excl=SHARE");
    /* 2〜4 本目は同じ「末尾に足す」1 つの振る舞い (深さの境目は次の
     * reg5=FULL が見る)。個別に落ちる経路が無いので 1 本にまとめる。 */
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1], IRQ_F_SHARED)
              == 0 &&
          irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[2], IRQ_F_SHARED)
              == 0 &&
          irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[3], IRQ_F_SHARED)
              == 0, "irq:reg2-4 ok");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[4], IRQ_F_SHARED)
          == IRQ_ERR_FULL, "irq:reg5=FULL");

    /* --- 走査の順と 2 巡 (`int 0x23` で共通スタブを通す) --- */
    ksel_irq_reset_devs();
    ksel_dev[0].ret[0] = IRQ_HANDLED;    /* A は 1 巡目で受け、2 巡目は NONE */
    ksel_dev[3].try_register = 1;        /* D が ISR 文脈から登録を試す */
    ksel_raise_irq3();
    check(ksel_dev[0].calls == 2 && ksel_dev[1].calls == 2 &&
          ksel_dev[2].calls == 2 && ksel_dev[3].calls == 2,
          "irq:4 devs x2 passes");
    check(ksel_irq_log_n == 8 && ksel_irq_log[0] == 0 && ksel_irq_log[1] == 1 &&
          ksel_irq_log[2] == 2 && ksel_irq_log[3] == 3 && ksel_irq_log[4] == 0,
          "irq:order 0123 x2");
    check(irq_shared_dispatch(KSEL_IRQ_LINE) > 0, "irq:shared cnt");
    /* **ISR の中からの登録は断る** (契約、debug で数える) */
    check(ksel_dev[3].try_register_rc == IRQ_ERR_CTX,
          "irq:isr reg=CTX");
    check(irq_ctx_violations == ctx_before + 1, "irq:ctx cnt");

    /* --- DEFERRED を数える --- */
    deferred_before = irq_deferred_count(KSEL_IRQ_LINE);
    ksel_irq_reset_devs();
    ksel_dev[1].ret[0] = IRQ_DEFERRED;
    ksel_dev[1].ret[1] = IRQ_DEFERRED;
    ksel_raise_irq3();
    check(irq_deferred_count(KSEL_IRQ_LINE) == deferred_before + 2,
          "irq:deferred cnt");

    /* --- 解除: 残った側は動き、抜けた側は呼ばれない --- */
    check(irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1]) == 0,
          "irq:unreg ok");
    check(irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1])
          == IRQ_ERR_NOENT, "irq:unreg2=NOENT");
    ksel_irq_reset_devs();
    ksel_raise_irq3();
    check(ksel_dev[1].calls == 0, "irq:unreg silent");
    check(ksel_dev[0].calls > 0 && ksel_dev[2].calls > 0,
          "irq:rest still run");
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 0, "irq:still unmasked");

    /* --- ストーム: 本体は IF=0 なので tick_count が止まり、窓は 1 つ --- */
    /* **撃つ前に窓を空にすること。** `irq_storm_step` は tick ごとに数え直す
     * ので、ここまでの解除の試験で撃った「誰も受けない `int 0x23`」が同じ
     * tick に入っていると、その数だけ下駄を履いて 200 本目で閾値を越える
     * (2026-09-23 に NP21/W で FAIL した形)。以前は IF=1 で tick の頭を
     * 待っていたが、その間は IRQ3 の IMR が開いていて本物の IRQ3 を受けて
     * しまう (実機 Ra266 の 82557)。irq_test_reset_line (試験専用) は
     * tick_hits を 0 にするので、それで窓を作り直す。登録は変わらない。 */
    irq_test_reset_line(KSEL_IRQ_LINE);
    ksel_irq_reset_devs();                      /* 全員 IRQ_NONE = 誰も受けない */
    for (i = 0; i < IRQ_STORM_LIMIT; i++) ksel_raise_irq3();
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 0 &&
          (irq_storm_masked & (1u << KSEL_IRQ_LINE)) == 0,
          "irq:200/tick no mask");
    irq_test_reset_line(KSEL_IRQ_LINE);         /* 窓を作り直す (試験専用) */
    for (i = 0; i <= IRQ_STORM_LIMIT; i++) ksel_raise_irq3();
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 1 &&
          (irq_storm_masked & (1u << KSEL_IRQ_LINE)) != 0,
          "irq:201/tick masks+bit");

    /* --- 隔離: sticky で、登録の再計算では解けない --- */
    check(irq_quarantine_line(0xFF) == IRQ_ERR_INVAL,
          "irq:qFF=INVAL");
    check(irq_quarantine_line(0) == IRQ_ERR_INVAL,
          "irq:q0=INVAL");
    /* 返り値・診断ビット・PIC のマスクは irq_quarantine_line が**同じ
     * irq_save の中でまとめて**書く。片方だけ落ちる経路は無いので 1 本で見る。 */
    check(irq_quarantine_line(KSEL_IRQ_LINE) == 0 &&
          (irq_line_quarantined & (1u << KSEL_IRQ_LINE)) != 0 &&
          ksel_irq_masked(KSEL_IRQ_LINE) == 1, "irq:q ok+bit+masked");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[4], IRQ_F_SHARED)
          == IRQ_ERR_BUSY, "irq:q reg=BUSY");

    /* --- 後始末: 残りを外して線を素の状態へ戻す --- */
    (void)irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0]);
    (void)irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[2]);
    (void)irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[3]);
    check((irq_line_quarantined & (1u << KSEL_IRQ_LINE)) != 0,
          "irq:q vs recount");
    /* **試験専用の戻し** (kernel/irq.h の注記)。これが無いと以後の起動で
     * IRQ3 が使えないままになる。 */
    irq_test_reset_line(KSEL_IRQ_LINE);
    check((irq_line_quarantined & (1u << KSEL_IRQ_LINE)) == 0,
          "irq:reset clears q");
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 1,
          "irq:masked at end");
    check(irq_lines[0].count == 0, "irq:table empty");

    /* 飛ばしたときに出す件数 (KSEL_IRQ_UNMASK_SKIPPED) の元の数と一致するか */
    check(ksel_pass + ksel_fail - unmask_base == KSEL_IRQ_UNMASK_CHECKS,
          "irq:unmask count");
}

/* ------------------------------------------------------------------------ */
/*  W4: µs 時計 (票 TASK_HAL_WIRING §1-5)                                   */
/*                                                                          */
/*  三分岐の網羅は**入力列の注入**で行う (往復 8 の中継 1): 実 PIT では      */
/*  呼び出しから p1 読みまでに境界を越える機械があり、位相を待っても         */
/*  0/0 と 0/1 を撃ち分けられない。実 PIT 側は「残り 1〜2 count を見てから   */
/*  読む」を期限つきで試し、**踏めた分岐を記録して、踏めなかったものは       */
/*  未検証と報告する** (失敗にしない)。                                      */
/* ------------------------------------------------------------------------ */
#define KSEL_TIME_READS      10000
#define KSEL_TIME_PHASE_TICKS 100    /* 位相待ちの期限 */
#define KSEL_TIME_PHASE_POLL  1000   /* IF=0 の観測ループの上限 */

static void ksel_time_feed(int n, int p1a, int p2a, int p1b, int p2b)
{
    time_test_feed[0].p1 = p1a; time_test_feed[0].p2 = p2a;
    time_test_feed[0].count = -1;
    time_test_feed[1].p1 = p1b; time_test_feed[1].p2 = p2b;
    time_test_feed[1].count = -1;
    time_test_feed[2] = time_test_feed[1];
    time_test_feed_n = n;
}

static void test_time_now(void)
{
    const struct pit_setup *ps = pit_get_setup();
    u32 lo = 0, hi = 0, plo = 0, phi = 0;
    u32 t0lo = 0, t0hi = 0, t1lo = 0, t1hi = 0;
    u32 clamp0;
    unsigned int hit_t0, hit_t1, hit_retry;
    int back = 0;
    int rc, i;

    /* --- 素の読み --- */
    rc = sys_time_now(&lo, &hi);
    check(rc == 0, "time:first read ok");
    check(hi == 0, "time:hi=0 at boot");
    check(lo > 0 || tick_count == 0, "time:us advance");

    /* --- **1 万回連続で逆行しない** ---
     * 単調性を持たせているのは p1/p2 の判定**だけではない**。8254 の再ロードと
     * 8259 の IRR が原子的でない機械 (NP21/W は模擬しない。実機も数百 ns の窓が
     * ある) では判定表を正しく通しても前回より小さい値が出るので、
     * `sys_time_now` が最後に前回値でクランプする。押さえた回数はその場で
     * 表示する — 0 なら「この機械では p1/p2 だけで足りていた」の記録になる。 */
    clamp0 = ktime_clamp_count;
    plo = lo; phi = hi;
    for (i = 0; i < KSEL_TIME_READS; i++) {
        if (sys_time_now(&lo, &hi) != 0) { back = -1; break; }
        if (hi < phi || (hi == phi && lo < plo)) { back++; break; }
        plo = lo; phi = hi;
    }
    check(back == 0, "time:10k reads monotonic");
    clamp0 = ktime_clamp_count - clamp0;
    kprintf(0x07, "[selftest] time: clamped %u of %d reads\n",
            (unsigned int)clamp0, (int)KSEL_TIME_READS);
    /* 全部がクランプなら時計が止まっている (補間が効いていない)。 */
    check(clamp0 < (u32)KSEL_TIME_READS, "time:clamp count sane");

    /* --- 既知の待ちを挟む (校正済み delay と突き合わせる) --- */
    check(sys_time_now(&t0lo, &t0hi) == 0, "time:bracket b4");
    cpu_delay_us(1000);
    check(sys_time_now(&t1lo, &t1hi) == 0, "time:bracket after");
    /* 1ms の待ちが 0.2ms〜20ms に見えれば配線は正しい (校正の精度そのものは
     * cpu_calibrate の試験が持つ。ここは「時計が進むか」だけ)。 */
    check(t1hi == t0hi && t1lo > t0lo + 200 && t1lo < t0lo + 20000,
          "time:1ms delay plausible");
    /* 外れたら測った値を出す (2026-09-24 の NP21/W で 1 回だけ FAIL。
     * cpu_delay_us の校正がずれたのか、時計の補間がずれたのかを分ける)。 */
    if (!(t1hi == t0hi && t1lo > t0lo + 200 && t1lo < t0lo + 20000)) {
        kprintf(0x07, "[selftest] time: 1ms delay measured %u us (hi %u->%u)\n",
                (unsigned int)(t1lo - t0lo), (unsigned int)t0hi,
                (unsigned int)t1hi);
    }

    /* --- 注入: p1/p2 の三分岐を全部踏む --- */
    time_branch_reset();
    ksel_time_feed(1, 0, 0, 0, 0);            /* 両方 0 → t をそのまま */
    check(sys_time_now(&lo, &hi) == 0, "time:p1=0 p2=0 ok");
    ksel_time_feed(1, 1, 0, 1, 0);            /* p1 = 1 → t + 1 */
    check(sys_time_now(&lo, &hi) == 0, "time:p1=1 -> t+1");
    ksel_time_feed(2, 0, 1, 0, 0);            /* 1 回やり直して成功 */
    check(sys_time_now(&lo, &hi) == 0, "time:0/1 retry ok");
    /* **3 回とも 0/1 を作る** — p2 だけ強制すると境界後の p1=1 で成功し得る */
    ksel_time_feed(1, 0, 1, 0, 1);
    check(sys_time_now(&lo, &hi) == OS32_ERR_AGAIN, "time:0/1 x3=EAGAIN");
    /* 負のときは出力を触らない。2 回目の -EAGAIN は同じ入力列の同じ経路
     * なので、返り値と「出力が不変」を 1 本で見る。 */
    lo = 0xDEADBEEFUL; hi = 0xFEEDFACEUL;
    check(sys_time_now(&lo, &hi) == OS32_ERR_AGAIN &&
          lo == 0xDEADBEEFUL && hi == 0xFEEDFACEUL, "time:neg keeps out");
    time_test_feed_clear();
    check(time_branch_hits[TIME_BR_T0] >= 1 &&
          time_branch_hits[TIME_BR_T1] >= 1 &&
          time_branch_hits[TIME_BR_RETRY] >= 4,
          "time:3 branches hit");

    /* --- 注入: 周期の境界の count ---
     * **2 本の読みは同じ irq_save の中で**。あいだに tick 境界が入ると
     * 「はじめ」の側が 1 周期ぶん大きくなり、補間の比較が偶発的に落ちる
     * (10ms に数 µs の窓。踏むまで気づけない形なので先に塞ぐ)。
     * 2 本とも同じ注入経路なので、成功の判定は 1 本にまとめる。
     * 注入中はクランプを通らない (kernel/ktime.c) ので、
     * 「終わり」→「はじめ」の巻き戻りがそのまま観測できる。 */
    time_test_feed[0].p1 = 0; time_test_feed[0].p2 = 0;
    time_test_feed[0].count = 1;              /* 周期の終わり */
    time_test_feed_n = 1;
    {
        unsigned int saved = irq_save();
        rc = sys_time_now(&t0lo, &t0hi);
        time_test_feed[0].count = (int)ps->reload;  /* 周期のはじめ */
        if (sys_time_now(&t1lo, &t1hi) != 0) rc = -1;
        irq_restore(saved);
    }
    check(rc == 0, "time:count=1/reload ok");
    /* 同じ tick なら「終わり」のほうが大きい (補間が効いている) */
    check(t0lo > t1lo || t0hi > t1hi || tick_count == 0,
          "time:frac moves");
    time_test_feed_clear();

    /* --- 実 PIT での位相待ち (**IF=1 で待つ**。IF=0 では IRQ0 が止まり
     * tick の期限が進まない。往復 9 の中継 3) --- */
    time_branch_reset();
    {
        u32 deadline = tick_count + KSEL_TIME_PHASE_TICKS;
        int done = 0;
        while (tick_count < deadline && !done) {
            unsigned int saved = irq_save();
            int poll;
            for (poll = 0; poll < KSEL_TIME_PHASE_POLL; poll++) {
                u32 c;
                outp(PIT_MODE, PIT_LATCH_TIMER0);
                c = (u32)(u8)inp(PIT_CNTR0);
                c |= (u32)((u8)inp(PIT_CNTR0)) << 8;
                if (c <= 2) {                  /* 残り 1〜2 count */
                    (void)sys_time_now(&lo, &hi);
                    done = 1;
                    break;
                }
            }
            irq_restore(saved);
        }
    }
    hit_t0    = time_branch_hits[TIME_BR_T0];
    hit_t1    = time_branch_hits[TIME_BR_T1];
    hit_retry = time_branch_hits[TIME_BR_RETRY];
    /* **踏めなかった分岐は失敗にしない** — 網羅は注入が持つ。 */
    if (hit_t0 == 0 || hit_t1 == 0 || hit_retry == 0) {
        /* 踏めなかった分岐は実機では UNVERIFIED。網羅は注入の表が持つ。 */
        kprintf(0x07, "[selftest] time: real-PIT t0=%d t1=%d retry=%d"
                      " (unhit = UNVERIFIED)\n",
                (int)hit_t0, (int)hit_t1, (int)hit_retry);
    }
    /* 位相窓を 1 度も捕まえられなくても失敗にしない (実装レビュー往復 1 の
     * 非 blocker 3: 正常な時計でも狭い窓は取り逃がし得る。網羅は注入の表)。 */
    if (hit_t0 + hit_t1 + hit_retry == 0) {
        kprintf(0x07, "[selftest] time: real-PIT phase window not caught (UNVERIFIED)\n");
    }
    time_branch_reset();

    /* --- CPL=0 の直呼びは書き込み検査の対象外 (Approve 後の注意 2) ---
     * 常駐シェル / gshell はカーネル帯のローカル変数を渡す。`ring3_in_syscall`
     * が 0 のあいだは素通しでなければ、それらが全部落ちる。 */
    check(ring3_user_range_writable((u32)&lo, sizeof(u32)) == 1,
          "time:CPL0 wr check off");
    check(ring3_user_ranges_writable((u32)&lo, sizeof(u32),
                                     (u32)&hi, sizeof(u32)) == 1,
          "time:CPL0 2-range same");

    /* --- 呼び出しコストと IF=0 の最長区間 (Approve 後の注意 3) ---
     * この時計の IF=0 区間は sys_time_now 1 回ぶんそのもの。1000 回を
     * 挟んで測り、1 回あたりの µs を報告する (合否にはしない — 機械差が
     * そのまま出る)。 */
    if (sys_time_now(&t0lo, &t0hi) == 0) {
        for (i = 0; i < 1000; i++) (void)sys_time_now(&lo, &hi);
        if (sys_time_now(&t1lo, &t1hi) == 0 && t1hi == t0hi && t1lo > t0lo) {
            /* 1 回あたりの µs = この時計の IF=0 区間そのもの。 */
            kprintf(0x07, "[selftest] time: 1000 reads %u us (%u us/call)\n",
                    (unsigned int)(t1lo - t0lo),
                    (unsigned int)((t1lo - t0lo) / 1000u));
        }
    }
}

/* ------------------------------------------------------------------------ */
/*  起動ログ (kernel/bootlog.c)。ここは書き出しの前なので、溜まっている     */
/*  最中の本物を見る。compose は本文を動かさないので呼んでも害は無い。       */
/*  組み方と手順の網羅はホスト試験 (tools/tests/test_bootlog.py)。          */
/* ------------------------------------------------------------------------ */
static void test_bootlog(void)
{
    char hdr[BOOTLOG_HDR_MAX];
    BootlogHeaderInfo hi;
    const char *out;
    u32 hl, n = 0, i;
    int same = 1;

    check(bootlog_is_active(), "bootlog still collecting before the shell");
    check(bootlog_len() > 0, "bootlog has the boot messages");
    kmemset(&hi, 0, sizeof(hi));
    hi.build = "b";
    hi.commit = "c";
    hl = bootlog_format_header(hdr, (u32)sizeof(hdr), &hi);
    check(hl > 16 && kstrncmp(hdr, "# OS32 boot log ", 16) == 0 &&
          hdr[hl - 1] == '\n', "bootlog header line");
    out = bootlog_compose(hdr, &n);
    for (i = 0; i < hl; i++) if (out[i] != hdr[i]) same = 0;
    check(same && n > hl + bootlog_len() && out[n - 1] == '\n',
          "bootlog compose = header + text + end line");
}

/* ------------------------------------------------------------------------ */
/*  WM の文脈では KAPI の出力検査を効かせない (2026-09-26、filer が窓も出さず  */
/*  に消えた件。POLICY_DEBUG §4-61)。WM (gshell) はアプリの syscall の中で     */
/*  走るので ring3_in_syscall = 1 のまま。その間に WM が自分のスタックの        */
/*  MouseInfo を mouse_poll に渡すと、シェル帯には USER が無いので拒否 →        */
/*  アプリが kill された。ここでは実物の門 3 つを、ディスパッチ中を装って     */
/*  カーネル帯のローカル変数で叩く: 深さ 0 なら拒否 (= 従来どおりアプリの      */
/*  不正なポインタは kill)、深さ 1 以上なら素通し。純関数の表はホスト         */
/*  (tools/tests/ring3_guard_host.c) が持つ。                                  */
/* ------------------------------------------------------------------------ */
extern volatile int ring3_in_syscall;

static void test_ring3_wm_guard(void)
{
    u32 local = 0;
    int saved = ring3_in_syscall;
    int saved_depth = ring3_wm_depth;
    u32 base;

    ring3_wm_depth = 0;
    ring3_in_syscall = 1;               /* ディスパッチ中を装う */
    base = ring3_range_reject_count;

    /* 深さ 0 = アプリ由来: カーネル帯 (USER 無し) は書けないと言う。
     * これが「アプリの不正なポインタは今までどおり kill」の側。 */
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 0,
          "wm-guard: app-origin kernel ptr refused");
    check(ring3_range_reject_count == base + 1u &&
          ring3_range_reject_last != 0 &&
          ring3_range_reject_addr == (u32)&local,
          "wm-guard: write-side refusal is counted");
    /* 起動中は保存callerが無効。writeはWR_TABLE、readはBANDで断る。 */
    check(ring3_user_range_ok((u32)&local, sizeof(u32)) == 0 &&
          ring3_range_reject_count == base + 2u,
          "wm-guard: app-origin read range refused");

    /* 深さ 1 = WM の文脈: 同じポインタが素通しになる (mouse_poll の経路)。 */
    ring3_wm_enter();
    check(ring3_wm_depth == 1, "wm-guard: enter -> depth 1");
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 1,
          "wm-guard: WM ptr passes while in WM");
    check(ring3_user_ranges_writable((u32)&local, sizeof(u32),
                                     (u32)&base, sizeof(u32)) == 1,
          "wm-guard: WM 2-range passes while in WM");
    check(ring3_user_range_ok((u32)&local, sizeof(u32)) == 1,
          "wm-guard: WM read range passes while in WM");
    check(ring3_range_reject_count == base + 2u,
          "wm-guard: pass-through is not counted");

    /* 入れ子 (WM の中の gui_call → ハンドラ) でも深さが 0 に戻るまで素通し。 */
    ring3_wm_enter();
    ring3_wm_leave();
    check(ring3_wm_depth == 1 &&
          ring3_user_range_writable((u32)&local, sizeof(u32)) == 1,
          "wm-guard: nested leave keeps WM context");

    /* 出口で深さ 0 に戻れば再び拒否する (WM を抜けた後のアプリのポインタ)。 */
    ring3_wm_leave();
    check(ring3_wm_depth == 0, "wm-guard: leave -> depth 0");
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 0,
          "wm-guard: refused again after leaving WM");
    {
        u32 u0 = ring3_wm_depth_underflow;
        ring3_wm_leave();               /* 余分な leave は負にしない */
        check(ring3_wm_depth == 0, "wm-guard: leave at 0 stays 0");
        /* 黙って止めずに数える (代行レビュー P3、exec.h の注記)。 */
        check(ring3_wm_depth_underflow == u0 + 1u,
              "wm-guard: leave at 0 is counted (underflow)");
    }

    /* 負の深さ (壊れた状態) は安全側 = ガードを効かせる。 */
    ring3_wm_depth = -1;
    check(ring3_user_range_writable((u32)&local, sizeof(u32)) == 0,
          "wm-guard: negative depth still guards");

    ring3_wm_depth = saved_depth;
    ring3_in_syscall = saved;
}

/* ------------------------------------------------------------------------ */
/*  KAPI ime_set_render は常駐側だけ (2026-09-26、代行レビュー P2。           */
/*  POLICY_DEBUG §4-61)。カーネルは控えた IME_Render 表の関数を以後 CPL=0 で  */
/*  呼ぶので、アプリ由来 (owner != 1 か ring3_call_from_user 真) の登録は     */
/*  kernel/gui.c の gui_ime_set_render が黙って断る。ここではディスパッチ中の */
/*  アプリを装って NULL を渡し (門が壊れていても描画先は TVRAM 版のまま =     */
/*  起動時の値なので害が無い)、断った回数が増えることを見る。常駐側の直呼び  */
/*  (owner 1、ディスパッチの外) の NULL は通って回数が増えない。表の全体は    */
/*  ホスト (tools/tests/ring3_guard_host.c の 5) が持つ。                      */
/* ------------------------------------------------------------------------ */
static void test_ime_render_gate(void)
{
    int saved = ring3_in_syscall;
    int saved_depth = ring3_wm_depth;
    int saved_owner = res_owner_get();
    u32 base = gui_ime_render_rejected;

    ring3_wm_depth = 0;
    ring3_in_syscall = 1;               /* アプリ (ID 2) の syscall を装う */
    res_owner_set(2);
    gui_ime_set_render((void *)0);
    check(gui_ime_render_rejected == base + 1u,
          "ime-render: app-origin registration refused");

    ring3_in_syscall = 0;               /* 常駐側 (owner 1) の直呼び */
    res_owner_set(1);
    gui_ime_set_render((void *)0);
    check(gui_ime_render_rejected == base + 1u,
          "ime-render: resident shell registration passes");

    res_owner_set(saved_owner);
    ring3_wm_depth = saved_depth;
    ring3_in_syscall = saved;
}

/* ------------------------------------------------------------------------ */
/*  RedirAccess の登録者なし拒否と TRUSTED の恒等写像を確認する。          */
/*  この作りものの USER 項目は生存確認で断られ、walk には達しない。         */
/*  RO 登録の KAPI guard は CPL=3 の d0a_test、登録者 walk はホストで確認。      */
/* ------------------------------------------------------------------------ */
static void test_fd_redirect_access_guard(void)
{
    u8 kbuf[16];
    FdRedirect r;
    int saved = ring3_in_syscall;
    int saved_depth = ring3_wm_depth;
    u32 base;

    kmemset(&r, 0, sizeof(r));
    r.target_type = FD_TARGET_BUFFER;
    r.file_fd = -1;
    r.buffer = kbuf;
    r.buf_capacity = sizeof(kbuf);
    r.owner = 2;                        /* アプリ (ID 2) が張った */
    r.access.origin = REDIR_USER;

    ring3_in_syscall = 1;               /* ディスパッチ中を装う */
    ring3_wm_depth = 0;
    ring3_wm_enter();                   /* WM の文脈 (gui_call のハンドラ) */
    base = ring3_range_reject_count;

    check(fd_redirect_buf_write_ok(&r, 1u) == 0,
          "access-guard: missing registrant refused at depth 1");
    check(ring3_range_reject_count == base,
          "access-guard: missing registrant refused before walk");

    /* 同じ番地でも WM (常駐側) が張ったものなら深さ 1 では素通し。 */
    r.access.origin = REDIR_TRUSTED;
    r.owner = 1;
    check(fd_redirect_buf_write_ok(&r, 1u) == 1 &&
          ring3_range_reject_count == base,
          "access-guard: trusted kernel buffer passes at depth 1");
    ring3_wm_leave();

    /* boot の CPL=0 slot は USER 登録者にできない。walk 前に断り、表は不変。 */
    check(ring3_wm_depth == 0 &&
          fd_redirect_to_buffer(2, kbuf, sizeof(kbuf), 0) == -1,
          "access-guard: CPL0 slot cannot capture USER registration");
    check(!fd_is_redirected(2),
          "access-guard: refused registration leaves the table");

    ring3_wm_depth = saved_depth;
    ring3_in_syscall = saved;
}

int kselftest_run(void)
{
    ksel_pass = 0;
    ksel_fail = 0;

    test_mem();
    test_str();
    test_utoa();
    test_heap();
    test_kprintf();
    test_kprintf_attr();
    test_ring3_pd();
    test_map_user_keep();
    test_app_band_pde();
    check(lease_selftest() == 0, "private lease S/T/U, master unchanged, owner zero");
    test_con_sink();
    test_con_sink_render_gate();
    test_kbd_inject();
    test_kbd_cmd();
    test_resume_mark();
    test_gfx_owner();
    test_abort_admit();
    test_launch();
    test_db_v50();
    test_cpu_calibrate();
    test_pit_setup();
    test_bootinfo();
    test_bootlog();
    test_dma8237();
    test_dma_pool();
    test_irq_dynamic();
    test_time_now();
    test_ring3_wm_guard();
    test_fd_redirect_access_guard();
    test_ime_render_gate();

    if (ksel_fail == 0) {
        kprintf(0xA1, "[selftest] %d/%d passed\n", ksel_pass, ksel_pass);
    } else {
        kprintf(0xC1, "[selftest] %d FAILED (%d passed)\n",
                ksel_fail, ksel_pass);
    }
    return ksel_fail;
}
