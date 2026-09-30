/* ========================================================================
 *  kbd_inject_host.c — 打鍵の注入リングを **実物のソースで** 確かめる
 *
 *  対象票: docs/archive/gui_v13/TASK_K7_input.md §1 D2〜D5 / §5 (R1 / R2 / B)
 *  実行:   python3 -B tools/tests/test_kbd_inject.py
 *  記録:   tools/tests/k7_tdd.md
 *
 *  kernel/kbd_inject.c を 1 行も写さずそのまま #include する (模型ではない)。
 *  **kernel/con_sink.c も同じ翻訳単位へ入れる** — 注入の権限は「con_sink の
 *  読み手 1 本」(票 §5 R2) で、そこを模型に置き換えると試験の意味が消える
 *  ため。読み手の確立は本物の con_sink_read() で行う。
 *
 *  カーネル帯に要るものは 2 つだけ (con_sink_host.c と同じ):
 *    - 割込み禁止区間: ホストは CPL=3 で cli/popfl を実行できないので
 *      CON_SINK_NO_IRQ_LOCK / KBD_INJECT_NO_IRQ_LOCK で空の錠に差し替える。
 *    - 所有者: fs/fd_redirect.c の res_owner_get/set をここで持つ。
 *
 *  libc は使わない (-nostdlib、Linux の int 0x80 で write/exit するだけ)。
 *  C89 ([C1])。
 * ======================================================================== */

/* ---- カーネル帯の代わり ------------------------------------------------ */
static int g_owner;
int  res_owner_get(void)      { return g_owner; }
void res_owner_set(int owner) { g_owner = owner; }

/* 実物。*_NO_IRQ_LOCK は test_kbd_inject.py が -D で渡す。 */
#include "con_sink.c"
#include "kbd_inject.c"

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

/* con_sink_read の受け皿 (最長レコードが入る大きさが要る)。ホストでだけ
 * 使う静的配列で、カーネル帯には置かない。 */
static u8 sink_buf[CON_SINK_REC_MAX];

/* "あ" = E3 81 82 (UTF-8)。GUI_EV_TEXT はこの並びのまま注がれる (D4)。 */
static const u8 utf8_a[3] = { 0xE3, 0x81, 0x82 };

/* 全部まっさらに戻す。con_sink の読み手も返す。 */
static void reset_all(void)
{
    con_sink_owner_exit(con_sink_reader_get());
    con_sink_disable();
    con_sink_drop_count = 0;
    kbd_inject_discard();
    kbd_inject_drop_count = 0;
    res_owner_set(0);
}

/* owner を con_sink の読み手にする (端末アプリの規約 R2 と同じ手順)。 */
static int become_reader(int owner)
{
    res_owner_set(owner);
    return con_sink_read(sink_buf, (u32)CON_SINK_REC_MAX) >= 0;
}

/* 1 バイト取り出して返す。空なら -1。 */
static int take_one(void)
{
    u8 b = 0;
    if (!kbd_inject_take(&b)) return -1;
    return (int)b;
}

/* ========================================================================
 *  1. 権限 — 注げるのは con_sink の読み手だけ (票 §5 R2)
 * ======================================================================== */
static void case_permission(void)
{
    reset_all();
    report("1 permission (only the con_sink reader may inject)\n");

    res_owner_set(2);
    check(kbd_inject((const u8 *)"A", 1) == (i32)OS32_ERR_EXIST,
          "1a 読み手が未確立なら OS32_ERR_EXIST");
    check(kbd_inject_pending() == 0,
          "1b 拒否された注入はリングを動かさない");
    check(kbd_inject((const u8 *)0, 1) == (i32)OS32_ERR_INVAL,
          "1c NULL は OS32_ERR_INVAL");

    check(become_reader(2), "1d con_sink_read で読み手になれる");
    check(kbd_inject((const u8 *)"A", 1) == 1,
          "1e 読み手は注げる (戻り = 積んだバイト数)");
    check(kbd_inject_pending() == 1, "1f 注いだ分が pending に出る");

    res_owner_set(3);
    check(kbd_inject((const u8 *)"B", 1) == (i32)OS32_ERR_EXIST,
          "1g 別の所有者からは OS32_ERR_EXIST");
    check(kbd_inject_pending() == 1, "1h その拒否もリングを動かさない");

    res_owner_set(2);
    check(kbd_inject((const u8 *)"B", 0) == 0,
          "1i len 0 は 0 (エラーではない)");
    check(kbd_inject_pending() == 1, "1j len 0 は何も積まない");

    /* 読み手が退場したら、次の注入はまた拒否される */
    con_sink_owner_exit(2);
    check(kbd_inject((const u8 *)"C", 1) == (i32)OS32_ERR_EXIST,
          "1k 読み手の退場後は誰も注げない");

    /* 端末が 2 本目でも同じ — 読み手になれなかった側は注げない (§3 の 2) */
    check(become_reader(4), "1l 空いた読み手を次の 1 本が取れる");
    res_owner_set(5);
    check(con_sink_read(sink_buf, (u32)CON_SINK_REC_MAX) == (i32)OS32_ERR_EXIST,
          "1m 2 本目の端末は読み手になれず");
    check(kbd_inject((const u8 *)"D", 1) == (i32)OS32_ERR_EXIST,
          "1n その 2 本目は注入も拒否される (busy 側は注がない)");
}

/* ========================================================================
 *  2. 並び — 積んだ順に 1 バイトずつ、UTF-8 のバイト順はそのまま (D4)
 * ======================================================================== */
static void case_byte_order(void)
{
    u32 i;

    reset_all();
    report("2 byte order (FIFO, UTF-8 passes through untouched)\n");
    become_reader(2);

    check(kbd_inject((const u8 *)"ab", 2) == 2, "2a 2 バイト積める");
    check(take_one() == 'a' && take_one() == 'b', "2b 積んだ順に出る");
    check(take_one() == -1, "2c 空なら -1");

    check(kbd_inject(utf8_a, 3) == 3, "2d 「あ」は 3 バイトとして積まれる");
    for (i = 0; i < 3; i++) {
        check(take_one() == (int)utf8_a[i],
              i == 0 ? "2e 先行バイト E3 が先に出る"
                     : (i == 1 ? "2f 続きバイト 81" : "2g 続きバイト 82"));
    }

    /* 複数回の注入は連結する。カーネルは文字の切れ目を知らない (D4)。 */
    check(kbd_inject(utf8_a, 3) == 3 && kbd_inject((const u8 *)"z", 1) == 1,
          "2h 続けて注げる");
    check(kbd_inject_pending() == 4, "2i pending は連結した長さ");
    check(take_one() == 0xE3, "2j 取り出しは注入の境界を無視して順どおり");
    check(kbd_inject((const u8 *)"w", 1) == 1,
          "2k 取り出しの途中でも注げる");
    check(take_one() == 0x81 && take_one() == 0x82 &&
          take_one() == 'z' && take_one() == 'w',
          "2l 割り込んだ注入は末尾に付く (順序は崩れない)");
    check(kbd_inject_pending() == 0, "2m 全部出たら pending 0");
}

/* ========================================================================
 *  3. pending — 所有権は要らない (WM が ready_to_run に使う、§5 指摘 C)
 * ======================================================================== */
static void case_pending(void)
{
    reset_all();
    report("3 pending (no ownership required)\n");
    become_reader(2);

    check(kbd_inject_pending() == 0, "3a 空は 0");
    kbd_inject((const u8 *)"abc", 3);
    check(kbd_inject_pending() == 3, "3b 注入で増える");
    take_one();
    check(kbd_inject_pending() == 2, "3c 取り出しで減る");

    res_owner_set(9);       /* 読み手でも WM でもない他人 */
    check(kbd_inject_pending() == 2, "3d 誰でも覗ける (WM が見る)");
    check(kbd_inject((const u8 *)"x", 1) == (i32)OS32_ERR_EXIST,
          "3e 覗けても注げはしない");
}

/* ========================================================================
 *  4. 破棄 — CUI 復帰と読み手の退場で捨てる (D5)
 * ======================================================================== */
static void case_discard(void)
{
    reset_all();
    report("4 discard (return to CUI / reader leaves)\n");
    become_reader(2);

    kbd_inject((const u8 *)"abc", 3);
    kbd_inject_discard();
    check(kbd_inject_pending() == 0, "4a discard で空になる");
    check(take_one() == -1, "4b 捨てた後は 1 バイトも出ない");
    check(kbd_inject((const u8 *)"de", 2) == 2 &&
          take_one() == 'd' && take_one() == 'e',
          "4c 捨てた後の環は先頭から使える (head/tail が揃う)");

    /* 読み手でない ID の退場では捨てない (他のアプリが畳まれただけ) */
    kbd_inject((const u8 *)"fg", 2);
    kbd_inject_owner_exit(3);
    check(kbd_inject_pending() == 2, "4d 別 ID の退場では捨てない");
    /* 読み手 (= 注ぎ手) の退場では捨てる */
    kbd_inject_owner_exit(2);
    check(kbd_inject_pending() == 0, "4e 読み手の退場では捨てる");
    /* CON_SINK_NO_READER を渡しても何も起きない (回収の空振り) */
    become_reader(2);
    kbd_inject((const u8 *)"h", 1);
    kbd_inject_owner_exit(CON_SINK_NO_READER);
    check(kbd_inject_pending() == 1, "4f 読み手なしの ID では捨てない");
}

/* ========================================================================
 *  5. あふれ — **新しい方**を捨てる (con_sink と逆。打鍵は順序が意味を持つ)
 * ======================================================================== */
static void case_overflow(void)
{
    u32 i;
    u32 drop0;
    int ok;

    reset_all();
    report("5 overflow (drops the newest, keeps what was typed first)\n");
    become_reader(2);

    /* 容量ちょうどまでは全部入る */
    ok = 1;
    for (i = 0; i < (u32)KBD_INJECT_RING_SIZE; i++) {
        u8 b = (u8)(i & 0xFF);
        if (kbd_inject(&b, 1) != 1) ok = 0;
    }
    check(ok, "5a 容量ちょうどまでは 1 バイトも落ちない");
    check(kbd_inject_pending() == (u32)KBD_INJECT_RING_SIZE,
          "5b pending は容量ちょうど");
    check(kbd_inject_drop_count == 0, "5c ここまでは 1 バイトも捨てていない");

    drop0 = kbd_inject_drop_count;
    check(kbd_inject((const u8 *)"ZZ", 2) == 0,
          "5d 満杯への注入は 0 (積めなかった数が呼び手に返る)");
    check(kbd_inject_drop_count == drop0 + 2,
          "5e 捨てたバイト数が kbd_inject_drop_count に載る");
    check(take_one() == 0,
          "5f 残っているのは**最初に打った**バイト (古い方を捨てない)");

    /* 途中まで入る形: 残り 1 バイトのところへ 3 バイト注ぐ */
    reset_all();
    become_reader(2);
    for (i = 0; i + 1 < (u32)KBD_INJECT_RING_SIZE; i++) {
        u8 b = 'a';
        kbd_inject(&b, 1);
    }
    check(kbd_inject(utf8_a, 3) == 1,
          "5g 入る分だけ積み、積んだ数を返す (半端も正直に)");
    check(kbd_inject_drop_count == 2, "5h 入らなかった 2 バイトを数える");

    /* 折り返し: 半分出してから満杯まで注ぎ直しても順序は崩れない */
    reset_all();
    become_reader(2);
    for (i = 0; i < (u32)KBD_INJECT_RING_SIZE; i++) {
        u8 b = (u8)(i & 0xFF);
        kbd_inject(&b, 1);
    }
    ok = 1;
    for (i = 0; i < (u32)KBD_INJECT_RING_SIZE / 2; i++) {
        if (take_one() != (int)(u8)(i & 0xFF)) ok = 0;
    }
    check(ok, "5i 前半は積んだ順に出る");
    ok = 1;
    for (i = 0; i < (u32)KBD_INJECT_RING_SIZE / 2; i++) {
        u8 b = (u8)0xC0;
        if (kbd_inject(&b, 1) != 1) ok = 0;
    }
    check(ok, "5j 空いた分だけまた積める (環が折り返す)");
    ok = 1;
    for (i = (u32)KBD_INJECT_RING_SIZE / 2;
         i < (u32)KBD_INJECT_RING_SIZE; i++) {
        if (take_one() != (int)(u8)(i & 0xFF)) ok = 0;
    }
    for (i = 0; i < (u32)KBD_INJECT_RING_SIZE / 2; i++) {
        if (take_one() != 0xC0) ok = 0;
    }
    check(ok, "5k 折り返しを跨いでも順序はそのまま");
    check(take_one() == -1, "5l 出し切ったら空");
}

/* ========================================================================
 *  6. kselftest が実機で呼ぶ自己診断そのもの (ビットマスク 0)
 * ======================================================================== */
static void case_selftest_agrees(void)
{
    reset_all();
    report("6 kbd_inject_selftest (what kselftest runs on the guest)\n");
    check(kbd_inject_selftest() == 0, "6a 自己診断のビットマスクは 0");
    check(kbd_inject_pending() == 0, "6b 終わった後は空");
    check(kbd_inject_drop_count == 0, "6c 捨てた累計を元に戻す");
}

int main(void)
{
    failures = 0;
    report("kbd inject ring (K7-K)\n");
    case_permission();
    case_byte_order();
    case_pending();
    case_discard();
    case_overflow();
    case_selftest_agrees();
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
