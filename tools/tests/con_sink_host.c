/* ========================================================================
 *  con_sink_host.c — console シンクのリングを **実物のソースで** 確かめる
 *
 *  対象票: docs/archive/gui_v13/TASK_K6C_console.md §2 (設計 3 点 + レコード形式)
 *  実行:   python3 -B tools/tests/test_con_sink.py
 *  記録:   tools/tests/con_sink_tdd.md
 *
 *  kernel/con_sink.c を 1 行も写さずそのまま #include する (模型ではない)。
 *  カーネル帯に要るものは 2 つだけ:
 *    - 割込み禁止区間: ホストは CPL=3 で cli/popfl を実行できないので
 *      CON_SINK_NO_IRQ_LOCK で空の錠に差し替える。守っている不変条件
 *      (「錠の中でしか head/tail/count を動かさない」) はソースの形が同じ。
 *    - 所有者: fs/fd_redirect.c の res_owner_get/set をここで持つ。
 *
 *  test_multiapp_model.py と同じ様式 — ホスト ILP32 GNU89 で走らせ、同じ
 *  ソースが i386-elf-gcc -Werror でも通ることを別に見る ([C1])。
 *  libc は使わない (-nostdlib、Linux の int 0x80 で write/exit するだけ)。
 * ======================================================================== */

/* ---- カーネル帯の代わり ------------------------------------------------ */
static int g_owner;
int  res_owner_get(void)      { return g_owner; }
void res_owner_set(int owner) { g_owner = owner; }

/* 実物。CON_SINK_NO_IRQ_LOCK は test_con_sink.py が -D で渡す。 */
#include "con_sink.c"

/* ======================================================================== */
/*  kernel/console.c も**実物のまま**取り込む (K6C-2)                        */
/*                                                                          */
/*  「シンク有効中は描画関数が TVRAM に触らない」を見るために要るのは 2 つ:  */
/*    - テキスト VRAM: tvram.h の TVRAM_BASE / TVRAM_ATTR はただの番地       */
/*      マクロなので、ホスト配列へ差し替えられる (console.c は                */
/*      `#define TVRAM_TEXT TVRAM_BASE` で使うだけ)。                        */
/*    - I/O ポート: CPL=3 では outb が撃てないので、記録するだけの            */
/*      host_outp に差し替える (GDC を触ったかどうかも検査になる)。          */
/*  console.c に元からある警告 2 件 (未使用変数・符号比較) はこの票の         */
/*  範囲外なので pragma で黙らせる。番地マクロ以外はカーネルと同じソース。    */
/* ======================================================================== */

#include "tvram.h"
/* tvram.h now includes the canonical pc98.h constants. */
#include "pc98.h"
#include "io.h"
#include "utf8.h"

/* テキスト面の代わり。u16 で持つのはアラインのため (console.c は u16 で書く) */
static u16 g_text_plane[TVRAM_ROWS * TVRAM_BPR / 2];
static u16 g_attr_plane[TVRAM_ROWS * TVRAM_BPR / 2];

/* GDC へ出した OUT の本数。GUI 中にカーソルが動かないことの検査に使う。 */
static u32 g_port_writes;
static void host_outp(unsigned int port, unsigned int value)
{
    (void)port; (void)value;
    g_port_writes++;
}

/* console.c が引く外部シンボル (カーネル側の本物の代わり) */
static int g_v86;
int v86_is_active(void) { return g_v86; }
int serial_putchar(char c) { (void)c; return 0; }

/* --- 打鍵の注入リング (票 N4 の貼り付け) の代わり ------------------------
 * exec_reclaim_owned (9a) は端末**配下の子**の退場で残りを捨てる。ホストで
 * は「捨てたか」だけ分かればよいので、残バイト数を 1 個の変数で持つ。
 * kbd_inject_discard は実物と同じく中身を空にする (0 にする)。 */
static u32 g_inject_pending;
void kbd_inject_discard(void) { g_inject_pending = 0; }

/* --- 起動ログ (kernel/bootlog.c) の代わり --------------------------------
 * console.c の入口は con_sink と同じ 4 か所で bootlog_push も呼ぶ。この
 * 試験が見るのはシンクなので、起動ログは受け流す (実物の試験は
 * tools/tests/test_bootlog.py)。 */
void bootlog_push(const char *buf, u32 len) { (void)buf; (void)len; }

/* --- launch_req 表の launch_child の代わり (exec/launch.c は引けない) -----
 * 実物は「その ID の表が所有する子。不正 ID / 不在は 0」。ここでは読み手→子
 * の対応を 1 本の配列で持ち、同じ約束 (範囲外は 0) を守る。 */
#define HOST_LC_MAX 64
static i32 g_child_of[HOST_LC_MAX];
i32 launch_child(i32 id)
{
    if (id < 0 || id >= (i32)HOST_LC_MAX) return 0;   /* 不正 ID は 0 */
    return g_child_of[(int)id];
}

u32 kstrlen(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

int kutoa_dec(u32 val, char *buf, int bufsz)
{
    if (bufsz < 2) return 0;
    buf[0] = (char)('0' + (int)(val % 10));
    buf[1] = '\0';
    return 1;
}

/* UTF-8 の最小デコーダ。この票が見るのは「描いたか描かなかったか」なので、
 * ASCII だけ通れば足りる (漢字表はカーネルの 0x4A000 に居る)。 */
utf8_decode_t utf8_decode(const u8 *src_p)
{
    utf8_decode_t d;
    d.codepoint = (u32)src_p[0];
    d.bytes_used = 1;
    return d;
}
u8  unicode_to_ank(u32 cp) { return (cp < 0x80) ? (u8)cp : (u8)0; }
u16 unicode_to_jis(u32 cp) { (void)cp; return 0; }

void console_hw_cursor_enable(void);    /* console.c の中で前方参照される */

#undef TVRAM_BASE
#define TVRAM_BASE  ((u32)(unsigned long)g_text_plane)
#undef TVRAM_ATTR
#define TVRAM_ATTR  ((u32)(unsigned long)g_attr_plane)
#define outp(p, v)  host_outp((p), (v))

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "console.c"
#pragma GCC diagnostic pop

/* ---- 最小の報告系 (libc 無し) ------------------------------------------ */

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}

static void report(const char *text)
{
    u32 len = 0;
    while (text[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static int failures;

static void check(int cond, const char *name)
{
    report(cond ? "  ok   " : "  FAIL ");
    report(name);
    report("\n");
    if (!cond) failures++;
}

/* ---- 試験の道具 -------------------------------------------------------- */

static u8 out[CON_SINK_RING_SIZE * 2];
static u8 src[CON_SINK_PRINT_MAX * 2];

static u32 pending_now(void)
{
    u32 pending = 0;
    u32 dropped = 0;
    con_sink_stat(&pending, &dropped);
    return pending;
}

static u32 dropped_now(void)
{
    u32 pending = 0;
    u32 dropped = 0;
    con_sink_stat(&pending, &dropped);
    return dropped;
}

/* 溜まっているものを全部吸う (何回でも 1 レコードずつ取れる大きさで回す)。 */
static u32 drain(void)
{
    u32 total = 0;
    for (;;) {
        i32 n = con_sink_read(out + total, (u32)CON_SINK_REC_MAX);
        if (n <= 0) break;
        total += (u32)n;
    }
    return total;
}

/* exec/exec.c の exec_reclaim_owned (9) の con_sink 部分を**そのまま写した**
 * 形 (exec.c はカーネル一式を引くのでホストへ #include できない)。順序が
 * 効く: con_sink_owner_exit で読み手が消えた後に判定すると、端末自身の
 * 退場でも EXIT を積んでしまう (受け取る相手はもう居ない)。 */
#define HOST_APP_ID_SHELL 1
static void reclaim_con_sink(int id)
{
    if (id != HOST_APP_ID_SHELL && con_sink_is_enabled() &&
        con_sink_reader_get() != id) {
        con_sink_push_exit(id);
        /* (9a) 退場したのが読み手 (端末) の子のときだけ注入リングを捨てる
         * (N4a 実装レビュー B1)。launch_child は不正 ID / 読み手不在で 0。 */
        if (launch_child(con_sink_reader_get()) == id) {
            kbd_inject_discard();
        }
    }
    con_sink_owner_exit(id);
}

/* 試験ごとに空・無効・読み手なしから始める。 */
static void reset_all(void)
{
    con_sink_disable();
    con_sink_owner_exit(res_owner_get());
    con_sink_owner_exit(0);
    con_sink_owner_exit(1);
    con_sink_owner_exit(2);
    con_sink_owner_exit(3);
    con_sink_drop_count = 0;
    g_owner = 0;
    g_inject_pending = 0;
    {
        int i;
        for (i = 0; i < HOST_LC_MAX; i++) g_child_of[i] = 0;
    }
}

/* ======================================================================== */
/*  1. 無効のあいだは 1 バイトも溜めない (受入 C1: CUI モードに無影響)       */
/* ======================================================================== */
static void case_disabled_is_silent(void)
{
    reset_all();
    report("1 disabled (CUI mode)\n");
    check(con_sink_is_enabled() == 0, "1a 既定は無効");
    con_sink_push_print("hello", 5, 7);
    con_sink_push_clear();
    con_sink_push_cursor(3, 4);
    check(pending_now() == 0, "1b 無効中の push は溜まらない");
    check(drain() == 0, "1c 読んでも 0 バイト");
}

/* ======================================================================== */
/*  2. push → read のレコード形式 (PRINT / CLEAR / CURSOR)                   */
/* ======================================================================== */
static void case_record_shape(void)
{
    i32 n;

    reset_all();
    report("2 record shape\n");
    con_sink_enable();
    check(con_sink_is_enabled() == 1, "2a GUI 入場で有効");

    con_sink_push_print("AB", 2, 0xE1);
    check(pending_now() == (u32)CON_SINK_HDR_PRINT + 2u,
          "2b PRINT は type+color+len+本文");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)((u32)CON_SINK_HDR_PRINT + 2u), "2c 読めた長さ");
    check(out[0] == (u8)CON_SINK_REC_PRINT, "2d type = PRINT");
    check(out[1] == 0xE1, "2e color がそのまま乗る");
    check(out[2] == 2, "2f len = 本文バイト数");
    check(out[3] == 'A' && out[4] == 'B', "2g 本文");
    check(pending_now() == 0, "2h 読んだ分は消える");
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == 0, "2i 空なら 0");

    con_sink_push_clear();
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_HDR_CLEAR && out[0] == (u8)CON_SINK_REC_CLEAR,
          "2j CLEAR は 1 バイト");

    con_sink_push_cursor(12, 34);
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_HDR_CURSOR &&
          out[0] == (u8)CON_SINK_REC_CURSOR && out[1] == 12 && out[2] == 34,
          "2k CURSOR は type+x+y");

    /* 空の PRINT / NULL は積まない (入口の色々な呼び方で来る) */
    con_sink_push_print("x", 0, 7);
    con_sink_push_print((const char *)0, 5, 7);
    check(pending_now() == 0, "2l 長さ 0 / NULL は積まない");
}

/* ======================================================================== */
/*  3. 読みはレコード境界で切る (途中で切らない)                             */
/* ======================================================================== */
static void case_read_cuts_on_record(void)
{
    i32 n;
    u32 i;

    reset_all();
    report("3 record boundary\n");
    con_sink_enable();
    for (i = 0; i < (u32)CON_SINK_PRINT_MAX; i++) src[i] = (u8)('a' + (i & 15));

    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX, 1);
    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX, 2);
    con_sink_push_print("z", 1, 3);

    /* cap = 最長レコード 1 本ぶん → 1 本ずつしか出ない */
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_REC_MAX && out[1] == 1, "3a 1 本目だけ");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_REC_MAX && out[1] == 2, "3b 2 本目だけ");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)((u32)CON_SINK_HDR_PRINT + 1u) && out[1] == 3,
          "3c 端数の 3 本目");
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == 0, "3d もう空");

    /* cap に 2 本入るなら 2 本まとめて出る (レコードの列として連なる) */
    con_sink_push_print("AA", 2, 4);
    con_sink_push_print("BBB", 3, 5);
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX * 2u);
    check(n == (i32)(2u * (u32)CON_SINK_HDR_PRINT + 5u), "3e 2 本まとめて");
    check(out[0] == (u8)CON_SINK_REC_PRINT && out[1] == 4 && out[2] == 2 &&
          out[5] == (u8)CON_SINK_REC_PRINT && out[6] == 5 && out[7] == 3,
          "3f 2 本目のヘッダが 1 本目の直後に来る");

    /* cap がレコードの倍数でないとき、余りに次のレコードの頭を詰めない。
     * ここが「境界を見ずに cap まで詰める」実装との差が出る唯一の形。 */
    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX, 7);
    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX, 8);
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX + 100u);
    check(n == (i32)CON_SINK_REC_MAX, "3j 半端な cap でも 1 本ぶんで止まる");
    check(pending_now() == (u32)CON_SINK_REC_MAX,
          "3k 2 本目は丸ごと残る (頭だけ齧られない)");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX + 100u);
    check(n == (i32)CON_SINK_REC_MAX && out[0] == (u8)CON_SINK_REC_PRINT &&
          out[1] == 8 && out[2] == (u8)CON_SINK_PRINT_MAX,
          "3l 2 本目は次の読みで丸ごと出る");

    /* 最長レコードが入らない cap は弾く (0 を返すと「空」と区別できない) */
    con_sink_push_print("q", 1, 6);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX - 1u) == (i32)OS32_ERR_INVAL,
          "3g cap < CON_SINK_REC_MAX は OS32_ERR_INVAL");
    check(con_sink_read((void *)0, (u32)CON_SINK_REC_MAX) == (i32)OS32_ERR_INVAL,
          "3h NULL は OS32_ERR_INVAL");
    check(pending_now() == (u32)CON_SINK_HDR_PRINT + 1u,
          "3i 弾いた読みはリングを動かさない");
}

/* ======================================================================== */
/*  4. 200 バイトを超える PRINT は分割される (UTF-8 の切れ目で)              */
/* ======================================================================== */
static void case_print_split(void)
{
    u32 i;
    u32 total;

    reset_all();
    report("4 long print splits\n");
    con_sink_enable();

    for (i = 0; i < (u32)CON_SINK_PRINT_MAX + 50u; i++) src[i] = (u8)'A';
    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX + 50u, 9);
    total = drain();
    check(total == 2u * (u32)CON_SINK_HDR_PRINT + (u32)CON_SINK_PRINT_MAX + 50u,
          "4a 2 本に分かれる");
    check(out[0] == (u8)CON_SINK_REC_PRINT && out[2] == (u8)CON_SINK_PRINT_MAX,
          "4b 1 本目は満杯");
    check(out[(u32)CON_SINK_HDR_PRINT + (u32)CON_SINK_PRINT_MAX + 2u] == 50,
          "4c 2 本目は残り");

    /* 全角 (3 バイト) が境界に跨る配置。200 は 3 で割り切れない (200=3*66+2)
     * ので、素朴に 200 で切ると 67 文字目が真っ二つになる。 */
    for (i = 0; i + 2 < (u32)CON_SINK_PRINT_MAX + 20u; i += 3) {
        src[i] = 0xE3; src[i + 1] = 0x81; src[i + 2] = 0x82;   /* U+3042 'あ' */
    }
    con_sink_push_print((const char *)src, 3u * 67u, 9);
    total = drain();
    check(total == 2u * (u32)CON_SINK_HDR_PRINT + 3u * 67u, "4d 2 本に分かれる");
    check(out[2] == 198, "4e 1 本目は UTF-8 の切れ目 (66 文字 = 198B) まで");
    check(out[(u32)CON_SINK_HDR_PRINT + 198u + 2u] == 3,
          "4f 2 本目に 3 バイトまるごと残る");
    check(out[(u32)CON_SINK_HDR_PRINT + 198u + 3u] == 0xE3,
          "4g 2 本目の先頭は先行バイト");
}

/* ======================================================================== */
/*  5. あふれ: 古い方をレコード単位で捨て、drop カウンタが進む               */
/* ======================================================================== */
static void case_overflow_drops_oldest(void)
{
    u32 i;
    u32 dropped_before;
    u32 fit;
    u32 seen;
    int intact;
    i32 n;

    reset_all();
    report("5 overflow drops the oldest\n");
    con_sink_enable();
    for (i = 0; i < (u32)CON_SINK_PRINT_MAX; i++) src[i] = (u8)('a' + (i & 15));

    /* 1 本 = CON_SINK_REC_MAX。ちょうど入る本数まで積む。 */
    fit = (u32)CON_SINK_RING_SIZE / (u32)CON_SINK_REC_MAX;
    for (i = 0; i < fit; i++) {
        con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX,
                            (u8)(i + 1));
    }
    check(dropped_now() == 0, "5a 容量内では 1 本も捨てない");
    check(pending_now() == fit * (u32)CON_SINK_REC_MAX, "5b 溜まりは積んだ分");

    /* もう 1 本でいちばん古い 1 本が落ちる */
    dropped_before = dropped_now();
    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX,
                        (u8)(fit + 1));
    check(dropped_now() == dropped_before + 1u, "5c 捨てた回数が 1 増える");
    check(pending_now() <= (u32)CON_SINK_RING_SIZE, "5d 容量を超えない");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_REC_MAX && out[1] == 2,
          "5e 残った先頭は 2 本目 (= 最古が消えた)");

    /* 環の折り返しを跨いだレコードも壊れない。ここまでで head/tail は
     * 配列の途中に居るので、以降の push は必ず端をまたぐ。 */
    drain();
    for (i = 0; i < fit * 3u; i++) {
        con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX,
                            (u8)(i + 1));
    }
    intact = 1;
    seen = 0;
    for (;;) {
        n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
        if (n <= 0) break;
        seen++;
        if (n != (i32)CON_SINK_REC_MAX ||
            out[0] != (u8)CON_SINK_REC_PRINT ||
            out[2] != (u8)CON_SINK_PRINT_MAX ||
            out[3] != src[0] ||
            out[(u32)CON_SINK_HDR_PRINT + (u32)CON_SINK_PRINT_MAX - 1u] !=
                src[(u32)CON_SINK_PRINT_MAX - 1u]) {
            intact = 0;
        }
    }
    check(seen == fit && intact,
          "5f 折り返しを跨いだレコードもそのまま出る");

    /* 型ごとに長さの違うレコードが混ざっていても、捨てるのはレコード単位。 */
    drain();
    con_sink_drop_count = 0;
    for (i = 0; i < (u32)CON_SINK_RING_SIZE; i++) {
        con_sink_push_cursor((int)(i & 31), (int)(i & 15));
    }
    check(pending_now() <= (u32)CON_SINK_RING_SIZE, "5g 混在でも容量を超えない");
    check(pending_now() % (u32)CON_SINK_HDR_CURSOR == 0,
          "5h 端数レコードが残らない");
    /* cap にはレコードが何本も入るので、返ってくるのは「レコードの列」。
     * 先頭がレコードの先頭で、長さが CURSOR の倍数であればよい。 */
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n > 0 && n % (i32)CON_SINK_HDR_CURSOR == 0 &&
          out[0] == (u8)CON_SINK_REC_CURSOR &&
          out[(u32)CON_SINK_HDR_CURSOR] == (u8)CON_SINK_REC_CURSOR,
          "5i 先頭は必ずレコードの先頭");
}

/* ======================================================================== */
/*  6. CUI 復帰 (disable) で破棄、再入場 (enable) は空から                   */
/* ======================================================================== */
static void case_discard_on_cui(void)
{
    reset_all();
    report("6 discard on return to CUI\n");
    con_sink_enable();
    con_sink_push_print("hello", 5, 7);
    check(pending_now() != 0, "6a 溜まっている");

    con_sink_disable();
    check(con_sink_is_enabled() == 0, "6b CUI 復帰で無効");
    check(pending_now() == 0, "6c 溜まっていたものは捨てる");
    con_sink_push_print("more", 4, 7);
    check(pending_now() == 0, "6d 無効中は積まない");

    con_sink_enable();
    check(pending_now() == 0, "6e 再入場は空から");
    con_sink_push_print("again", 5, 7);
    check(pending_now() == (u32)CON_SINK_HDR_PRINT + 5u, "6f 積み直せる");
    con_sink_enable();
    check(pending_now() == 0, "6g 二重の入場でも空になるだけ");
}

/* ======================================================================== */
/*  7. 読み手は 1 本。owner 回収で次の 1 本が読めるようになる                 */
/* ======================================================================== */
static void case_single_reader(void)
{
    reset_all();
    report("7 single reader / owner reclaim\n");
    con_sink_enable();
    con_sink_push_print("one", 3, 7);

    res_owner_set(2);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) ==
          (i32)((u32)CON_SINK_HDR_PRINT + 3u), "7a 最初に読んだ者が所有する");

    res_owner_set(3);
    con_sink_push_print("two", 3, 7);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == (i32)OS32_ERR_EXIST,
          "7b 2 本目の読み手は拒否される");
    check(pending_now() == (u32)CON_SINK_HDR_PRINT + 3u,
          "7c 拒否された読みはリングを動かさない");

    res_owner_set(2);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) ==
          (i32)((u32)CON_SINK_HDR_PRINT + 3u), "7d 持ち主は読み続けられる");

    /* 関係ない ID の終了では所有は動かない */
    con_sink_owner_exit(4);
    res_owner_set(3);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == (i32)OS32_ERR_EXIST,
          "7e 別 ID の回収では明け渡さない");

    /* 持ち主が畳まれたら次の 1 本が読み手になれる (受入 C4) */
    con_sink_owner_exit(2);
    res_owner_set(3);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == 0, "7f 次の読み手が取れる");
    con_sink_push_print("three", 5, 7);
    res_owner_set(2);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == (i32)OS32_ERR_EXIST,
          "7g 今度は元の持ち主の方が拒否される");

    /* CUI 復帰は中身を捨てるだけで所有は返さない (返すのは owner 回収だけ) */
    con_sink_disable();
    con_sink_enable();
    res_owner_set(2);
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == (i32)OS32_ERR_EXIST,
          "7h CUI 往復では所有は動かない");

    /* stat は所有が要らない (誰でも覗ける) */
    res_owner_set(9);
    check(con_sink_stat((u32 *)0, (u32 *)0) == 0, "7i stat は NULL でも 0");
    check(pending_now() == 0, "7j stat は所有を要求しない");
}

/* ======================================================================== */
/*  8. kselftest が実機で呼ぶ自己診断そのもの (ビットマスク 0)               */
/* ======================================================================== */
static void case_selftest_agrees(void)
{
    reset_all();
    report("8 con_sink_selftest (what kselftest runs on the guest)\n");
    check(con_sink_selftest() == 0, "8a 自己診断のビットマスクは 0");
    check(con_sink_is_enabled() == 0, "8b 終わった後は無効 (CUI に戻る)");
    check(pending_now() == 0, "8c 終わった後は空");
    check(res_owner_get() == 0, "8d 所有者タグを元に戻す");
}

/* ======================================================================== */
/*  9. console.c の描画抑止 (K6C-2)                                          */
/*                                                                          */
/*  GUI モード中 (シンク有効) に CUI プログラムが出力すると、K6C のシンクに  */
/*  積まれると同時にテキスト VRAM にも描かれ、GFX 画面の上に残像が出ていた。 */
/*  描く側だけを止め、積む側と論理カーソルの整合は保つ。                     */
/* ======================================================================== */

#define PLANE_CELLS   (TVRAM_COLS * TVRAM_ROWS)
#define PLANE_PITCH   (TVRAM_BPR / 2)

static void plane_mark(void)
{
    int i;
    for (i = 0; i < PLANE_CELLS; i++) {
        g_text_plane[i] = 0x5A5A;
        g_attr_plane[i] = 0xA5A5;
    }
}

static int plane_untouched(void)
{
    int i;
    for (i = 0; i < PLANE_CELLS; i++) {
        if (g_text_plane[i] != 0x5A5A) return 0;
        if (g_attr_plane[i] != 0xA5A5) return 0;
    }
    return 1;
}

static int plane_all_blank(void)
{
    int i;
    for (i = 0; i < PLANE_CELLS; i++) {
        if (g_text_plane[i] != 0x0020) return 0;
    }
    return 1;
}

static u16 cell(int x, int y)
{
    return g_text_plane[y * PLANE_PITCH + x];
}

static void case_console_render_gate(void)
{
    int cx, cy;

    reset_all();
    g_v86 = 0;
    report("9 console.c drawing gate (K6C-2)\n");

    /* --- CUI (シンク無効): 従来どおり描く --------------------------------- */
    console_set_cursor(0, 0);
    plane_mark();
    g_port_writes = 0;
    shell_print("AB", 7);
    check(cell(0, 0) == (u16)'A' && cell(1, 0) == (u16)'B',
          "9a CUI では TVRAM に描く");
    check(console_get_cursor_x() == 2 && console_get_cursor_y() == 0,
          "9b CUI ではカーソルが進む");
    check(g_port_writes > 0, "9c CUI では GDC へカーソルを送る");

    /* --- GUI へ (console_text_gdc_stop がシンクを有効にする) -------------- */
    console_text_gdc_stop();
    check(con_sink_is_enabled() == 1, "9d GUI へ入るとシンクが有効");

    plane_mark();
    g_port_writes = 0;
    cx = console_get_cursor_x();
    cy = console_get_cursor_y();

    shell_print("XY", 7);
    shell_print_utf8("Z", 7);
    console_write("W", 1, 7);
    shell_putchar('Q', 7);
    shell_print_dec(5, 7);
    shell_print_hex32(0x1234, 7);

    check(plane_untouched(), "9e GUI 中はどの出力経路も TVRAM に触らない");
    check(console_get_cursor_x() == cx && console_get_cursor_y() == cy,
          "9f GUI 中は論理カーソルを進めない");
    check(g_port_writes == 0, "9g GUI 中は GDC を触らない");
    check(pending_now() > 0, "9h GUI 中もシンクには積む");

    /* --- GUI 中の tvram_clear / console_set_cursor ------------------------ */
    plane_mark();
    g_port_writes = 0;
    tvram_clear();
    check(plane_untouched(), "9i GUI 中の tvram_clear はテキスト面を消さない");
    console_set_cursor(10, 5);
    check(console_get_cursor_x() == cx && console_get_cursor_y() == cy,
          "9j GUI 中の console_set_cursor は論理位置を動かさない");
    check(g_port_writes == 0, "9k GUI 中は CSRFORM/CSRW を出さない");
    check(pending_now() > 0, "9l CLEAR / CURSOR はシンクに積まれている");

    /* --- CUI 復帰: 画面と論理位置が必ず一致する --------------------------- */
    console_text_gdc_start();
    check(con_sink_is_enabled() == 0, "9m CUI 復帰でシンクは無効");
    check(plane_all_blank(), "9n CUI 復帰でテキスト面を消す");
    check(console_get_cursor_x() == 0 && console_get_cursor_y() == 0,
          "9o CUI 復帰で論理位置は 0,0 (画面と一致)");

    /* --- V86 セッション中も描かない (従来の抑止を壊していない) ------------ */
    plane_mark();
    g_v86 = 1;
    shell_print("v86", 7);
    console_write("v", 1, 7);
    shell_putchar('v', 7);
    check(plane_untouched(), "9p V86 中も TVRAM に触らない");
    g_v86 = 0;
}

/* ======================================================================== */
/*  10. EXIT レコード (票 T7 E1) — 子の終了を端末へ知らせる                  */
/* ======================================================================== */
static void case_exit_record(void)
{
    i32 n;
    u32 i;
    u32 fit;
    u32 dropped_before;

    reset_all();
    report("10 EXIT record (T7 E1)\n");

    /* --- 積む / 読む -------------------------------------------------- */
    con_sink_push_exit(5);
    check(pending_now() == 0, "10a 無効中の EXIT は溜まらない");
    con_sink_enable();
    con_sink_push_exit(5);
    check(pending_now() == (u32)CON_SINK_HDR_EXIT, "10b EXIT は type+id の 2B");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_HDR_EXIT && out[0] == (u8)CON_SINK_REC_EXIT &&
          out[1] == 5, "10c [type=4][id]");
    check(pending_now() == 0, "10d 読んだ分は消える");

    /* --- レコード境界: EXIT を挟んでも前後のレコードが齧られない -------- */
    for (i = 0; i < (u32)CON_SINK_PRINT_MAX; i++) src[i] = (u8)('a' + (i & 15));
    con_sink_push_print("bye\n", 4, 7);
    con_sink_push_exit(9);
    con_sink_push_cursor(7, 8);
    check(pending_now() ==
          (u32)CON_SINK_HDR_PRINT + 4u + (u32)CON_SINK_HDR_EXIT +
          (u32)CON_SINK_HDR_CURSOR,
          "10e EXIT の長さが型から導ける (前後と合わせて端数が出ない)");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX * 2u);
    check(n == (i32)((u32)CON_SINK_HDR_PRINT + 4u + (u32)CON_SINK_HDR_EXIT +
                     (u32)CON_SINK_HDR_CURSOR), "10f 3 本まとめて出る");
    check(out[0] == (u8)CON_SINK_REC_PRINT &&
          out[(u32)CON_SINK_HDR_PRINT + 4u] == (u8)CON_SINK_REC_EXIT &&
          out[(u32)CON_SINK_HDR_PRINT + 4u + 1u] == 9 &&
          out[(u32)CON_SINK_HDR_PRINT + 4u + (u32)CON_SINK_HDR_EXIT] ==
              (u8)CON_SINK_REC_CURSOR,
          "10g EXIT の直後に次のレコードの頭が来る");
    check(pending_now() == 0, "10h 端数が残らない");

    /* 最長レコードの直後に置いても、1 本ずつ切り出せる */
    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX, 3);
    con_sink_push_exit(2);
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_REC_MAX, "10i cap 1 本ぶんでは PRINT だけ");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_HDR_EXIT && out[0] == (u8)CON_SINK_REC_EXIT &&
          out[1] == 2, "10j 次の読みで EXIT が丸ごと出る");

    /* --- あふれ: EXIT も「古い方をレコード単位で」の対象 ---------------- */
    reset_all();
    con_sink_enable();
    fit = (u32)CON_SINK_RING_SIZE / (u32)CON_SINK_REC_MAX;
    for (i = 0; i < fit; i++) {
        con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX,
                            (u8)(i + 1));
    }
    /* 40 本 = 8120B なので 72B 余る。EXIT (2B) で隙間を埋め切ってから 1 本
     * 足すと、初めて最古の PRINT が 1 本まるごと消える。 */
    while ((u32)CON_SINK_RING_SIZE - pending_now() >= (u32)CON_SINK_HDR_EXIT) {
        con_sink_push_exit(10);
    }
    dropped_before = dropped_now();
    check(dropped_before == 0 && pending_now() == (u32)CON_SINK_RING_SIZE,
          "10k 容量内では EXIT を足しても捨てない (隙間ちょうどで満杯)");
    con_sink_push_exit(11);
    check(dropped_now() == dropped_before + 1u,
          "10l あふれた EXIT は古い PRINT を 1 本だけ捨てて入る");
    check(pending_now() <= (u32)CON_SINK_RING_SIZE, "10m 容量を超えない");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_REC_MAX && out[1] == 2,
          "10m2 残った先頭は 2 本目 (最古が消えた)");

    /* EXIT だけでリングを埋めても端数は出ない */
    reset_all();
    con_sink_enable();
    con_sink_drop_count = 0;
    for (i = 0; i < (u32)CON_SINK_RING_SIZE; i++) con_sink_push_exit((int)(i & 255));
    check(pending_now() % (u32)CON_SINK_HDR_EXIT == 0 &&
          pending_now() <= (u32)CON_SINK_RING_SIZE,
          "10n EXIT だけで埋めても端数が残らない");
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n > 0 && n % (i32)CON_SINK_HDR_EXIT == 0 &&
          out[0] == (u8)CON_SINK_REC_EXIT &&
          out[(u32)CON_SINK_HDR_EXIT] == (u8)CON_SINK_REC_EXIT,
          "10o 先頭は必ず EXIT レコードの先頭");
    /* EXIT で満杯の環へ最長 PRINT を入れる。捨てるのが**レコード単位**なら
     * 203B ぶん = 102 本。型ごとの長さを知らない実装だと 1B ずつ 203 本
     * 捨てるので、ここで差が出る。 */
    drain();
    con_sink_drop_count = 0;
    while ((u32)CON_SINK_RING_SIZE - pending_now() >= (u32)CON_SINK_HDR_EXIT) {
        con_sink_push_exit(12);
    }
    check(con_sink_drop_count == 0 && pending_now() == (u32)CON_SINK_RING_SIZE,
          "10o2 EXIT だけで環が満杯になる");
    con_sink_push_print((const char *)src, (u32)CON_SINK_PRINT_MAX, 1);
    check(con_sink_drop_count ==
          ((u32)CON_SINK_REC_MAX + (u32)CON_SINK_HDR_EXIT - 1u) /
              (u32)CON_SINK_HDR_EXIT,
          "10o3 捨てたのは EXIT 102 本 (レコード単位、バイト単位ではない)");

    /* --- 誰の退場で積むか (exec_reclaim_owned の順序) ------------------- */
    reset_all();
    con_sink_enable();
    g_owner = 4;                       /* 端末アプリが読み手になる */
    check(con_sink_read(out, (u32)CON_SINK_REC_MAX) == 0, "10p 端末が読み手");
    check(con_sink_reader_get() == 4, "10q 読み手は ID 4");

    reclaim_con_sink(6);               /* 子が正常終了 / kill / fault */
    n = con_sink_read(out, (u32)CON_SINK_REC_MAX);
    check(n == (i32)CON_SINK_HDR_EXIT && out[1] == 6,
          "10r 非シェルの子の退場で EXIT が積まれる");

    reclaim_con_sink(HOST_APP_ID_SHELL);
    check(pending_now() == 0, "10s gshell (ID 1) の退場では積まない");

    reclaim_con_sink(4);               /* 読み手本人 (端末) の退場 */
    check(pending_now() == 0, "10t 読み手本人の退場では積まない");
    check(con_sink_reader_get() == CON_SINK_NO_READER,
          "10u 読み手本人の退場で所有は返る");

    /* 読み手が居ない (端末がもう畳まれた) ときは、子の退場でも積まない */
    reclaim_con_sink(7);
    check(pending_now() == (u32)CON_SINK_HDR_EXIT,
          "10v 読み手不在でも積む (次の読み手が拾う — リングは捨てない)");

    /* CUI に戻っていれば (シンク無効) 積まない */
    reset_all();
    reclaim_con_sink(6);
    check(pending_now() == 0 && con_sink_is_enabled() == 0,
          "10w CUI モード中 (無効) は子の退場でも積まない");
}

/* ======================================================================== */
/*  11. 注入リングの破棄条件 (票 N4a-fix B1) — 端末の子の退場でだけ捨てる   */
/*                                                                          */
/*  exec_reclaim_owned (9a) は貼り付け途中の注入リングを捨てる。無条件だと   */
/*  無関係な GUI アプリが畳まれただけで貼り付けが 256B 欠ける退行になる      */
/*  (N4a 実装レビュー B1)。捨てるのは **退場したのが読み手 (端末) の子の      */
/*  ときだけ**。読み手→子の対応を launch_child の贋物で与えて確かめる。       */
/* ======================================================================== */
static void case_inject_discard_scope(void)
{
    reset_all();
    report("11 inject discard scope (N4a-fix B1)\n");

    con_sink_enable();
    g_owner = 4;                       /* 端末アプリ (ID 4) が読み手 */
    g_child_of[4] = 6;                 /* その端末が起動した子は ID 6 */
    con_sink_read(out, (u32)CON_SINK_REC_MAX);   /* 読みで所有を確定する */
    check(con_sink_reader_get() == 4, "11a 読み手は端末 ID 4");
    check(launch_child(con_sink_reader_get()) == 6, "11b 端末の子は ID 6");

    /* --- 無関係な GUI アプリの退場では捨てない ------------------------- */
    g_inject_pending = 256;            /* 端末が子の stdin へ積んだ残り */
    reclaim_con_sink(9);               /* ID 9 は読み手でも読み手の子でもない */
    check(g_inject_pending == 256,
          "11c 無関係 ID (9) の退場では注入リングは減らない");
    check(pending_now() == (u32)CON_SINK_HDR_EXIT,
          "11d それでも EXIT レコードは積まれる (端末は子の終了を知る)");
    drain();

    /* --- 読み手の子の退場では捨てる ------------------------------------ */
    g_inject_pending = 256;
    reclaim_con_sink(6);               /* 端末 (4) の子 = ID 6 */
    check(g_inject_pending == 0,
          "11e 読み手の子 (6) の退場で注入リングは 0 になる");
    check(pending_now() == (u32)CON_SINK_HDR_EXIT,
          "11f 子の退場でも EXIT は積まれる");
    drain();

    /* --- 読み手不在 (端末が居ない) のときは捨てない --------------------- */
    reset_all();
    con_sink_enable();                 /* GUI 中だが読み手はまだ居ない */
    g_child_of[4] = 6;                 /* 表に残骸があっても照合は reader 起点 */
    g_inject_pending = 128;
    reclaim_con_sink(6);               /* launch_child(NO_READER) は 0 */
    check(g_inject_pending == 128,
          "11g 読み手不在なら子らしき ID の退場でも捨てない");
}

int main(void)
{
    failures = 0;
    report("con_sink ring (K6C)\n");
    case_disabled_is_silent();
    case_record_shape();
    case_read_cuts_on_record();
    case_print_split();
    case_overflow_drops_oldest();
    case_discard_on_cui();
    case_single_reader();
    case_selftest_agrees();
    case_console_render_gate();
    case_exit_record();
    case_inject_discard_scope();
    if (failures) {
        report("FAILURES\n");
        die(1);
    }
    report("ALL PASS\n");
    die(0);
    return 0;
}

/* -nostdlib の入口 */
void _start(void)
{
    main();
}
