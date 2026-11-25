#include "sys/kern/idle_work.h"
/* ======================================================================== */
/*  KBD.C — PC-98 キーボードドライバ                                       */
/*                                                                          */
/*  μPD8251A経由でスキャンコードを取得し、ASCII変換してリングバッファに格納 */
/*  IRQ1割り込みで駆動                                                      */
/*                                                                          */
/*  出典: PC9800Bible §2-5 表2-14                                           */
/*  参照: FreeBSD sys/pc98/cbus/pckbd.c                                     */
/*    - IO_KBD = 0x041 (ベースアドレス)                                     */
/*    - KBD_DATA_PORT = base + 0 = 0x41                                     */
/*    - KBD_STATUS_PORT = base + 2 = 0x43 (PC-98はI/O 2バイト間隔)         */
/*    - KBDS_BUFFER_FULL = 0x0002 (8251 RxRDY)                              */
/*    - FreeBSD init_keyboard() は空 → BIOS初期化済みを前提                */
/*      この前提を守る = **BIOS の定常値 (0x16) 以外を書かない**。以前は     */
/*      0x14 (DTR = 0 = RTY# LOW = 再送要求) を書いて自分で壊していた       */
/*      (実機 PC-9821Ra266 で打鍵が届かない、2026-09-23、docs/POLICY_DEBUG.md#4-57-キーボード-8251-の信号線)*/
/* ======================================================================== */

#include "kbd.h"
#include "../../../hal/input/in2_int.h"
#include "../../../hal/input/keymap.h"
#include "../../../input/input_context.h"
int fep_services_public(void);
#include "con_sink.h"
#include "../legacy/v86_bios.h"
#include "io.h"
#include "kprintf.h"
#include "memmap.h"
#include "pc98.h"        /* TATTR_WHITE */
#include "../../../dev/char/serial.h"
#include "kbd_inject.h"   /* K7: GUI 中の打鍵は注入リングから来る */
#include "../../../dev/input/kbd_dlog.h"     /* KAPI v67 kbd_diag_log: 受信のたびの記録 */

extern volatile int exec_nest_level;  /* sys/kern/exec/exec.c */

/* 外部: irq_enable (idt.c で定義) */
extern void irq_enable(unsigned int irq);

/* 外部: V86 セッション (sys/platform/pc98/legacy/v86.c, sys/platform/pc98/legacy/v86_kbd.c)。
 * drivers/ は -Ikernel を持たないので、この 3 本だけここで宣言する
 * (irq_enable と同じ扱い)。ドライバが V86 の中身を知る必要はない。 */
#include "legacy_guest.h"

/* rshellモード判定用 (shell.cで定義) */
extern int rshell_active;

/* 外部: CPL=3 アプリの強制脱出要求 (sys/kern/exec/exec.c、K2 / 契約 T6)。
 * CTRL+STOP を見つけたら要求を立てるだけで、実際に畳むのは IRQ1 スタブ
 * (割り込まれた文脈が CPL=3 のとき) か次の syscall 入口。drivers/ は
 * -Iexec を持たないので irq_enable と同じ流儀で extern 宣言する。 */
extern void ring3_abort_request(void);
extern void ring3_abort_check(void);
extern int ring3_wait_pending(void);

/* 外部: 第 2 の park 点 (sys/kern/exec/exec.c、票 K7 D1)。GUI モードで注入リングが
 * 空のとき、走っている CPL=3 アプリを WAIT_KEY で止めて WM へ戻す。
 * 成立すれば **戻らない** (longjmp)。0 = 止められなかった (CPL=0 の呼び手 /
 * syscall の外 / CUI の入れ子の子) → 従来の hlt 待ちへ落ちる。
 * drivers/ は -Iexec を持たないので irq_enable と同じ流儀で extern 宣言する。 */
extern int exec_park_kbd(void);

/* 外部: 第 3 の park 点 (sys/kern/exec/exec.c、票 T8 §7 D8)。GUI モードで注入リングが
 * 空のとき、**1 周だけ** WM へ譲る。成立すれば戻らない (longjmp)。
 * 0 = 譲れなかった (間引き中 / CPL=0 の呼び手 / syscall の外 / CUI の入れ子の
 * 子) → 呼び手はそのまま -1 を返す。 */
extern int exec_park_poll(u32 now_tick);

/* 外部: PIT の tick (sys/arch/x86/kernel/idt.c、100Hz)。D8 の間引き「10ms に 1 回まで」を
 * 数え、rshell のキー待ちの経過時間にも使う。drivers/ は -Ikernel を持たないので irq_enable と
 * 同じ流儀で extern 宣言する。 */
extern volatile u32 tick_count;


/* ======== 診断カウンタ (KAPI v62 kbd_diag、シェルの `kbdstat`) ========
 * 実機で打鍵が届かないとき、どこで止まっているかを切り分けるための値
 * (読み方は docs/POLICY_DEBUG.md#4-57-キーボード-8251-の信号線)。ISR だけが書く値は volatile。
 * init_* と cmd と flushed は kbd_init が 1 回だけ書く。 */
static volatile u32 kbd_diag_irq;
static volatile u32 kbd_diag_empty;
static volatile u32 kbd_diag_err;
static volatile u32 kbd_diag_overrun;
static volatile u8  kbd_diag_last_st;
static volatile u8  kbd_diag_last_code;
static u32 kbd_diag_flushed;
static u8  kbd_diag_init_before;
static u8  kbd_diag_init_after;
static u8  kbd_diag_cmd;

STATIC_ASSERT(sizeof(KbdDiag) == 24, kbd_diag_is_24);

/* ======== 受信記録リング (KAPI v67 kbd_diag_log、シェルの `kbdstat -w`) ========
 * 票 docs/tasks/gui/TASK_KBD_NAV.md §3。0041h から**使うバイトを読むたびに**
 * 1 件 (seq / 生の code / 処理後の kbd_shift_state / フラグ)。last_code は最後の
 * 1 件しか持たず、irq_count は EMPTY / ERROR も数えるので、カナの make / break の
 * 方式はこちらで見る。書くのは kbd_irq_handler だけ (IF=0、再入しない —
 * kbd_shift_state と同じ所有権)。読むのは kbd_diag_log が irq_save の間に写す。 */
static KbdDlog kbd_dlog;

STATIC_ASSERT(sizeof(KbdDiagLogEnt) == 8, kbd_diag_log_ent_is_8);
STATIC_ASSERT(KBD_DLOG_MOD_SHIFT == SHIFT_SHIFT && KBD_DLOG_MOD_CAPS == SHIFT_CAPS &&
              KBD_DLOG_MOD_KANA == SHIFT_KANA && KBD_DLOG_MOD_GRPH == SHIFT_GRPH &&
              KBD_DLOG_MOD_CTRL == SHIFT_CTRL, kbd_dlog_mods_match_shift);
STATIC_ASSERT(KBD_DLOG_BREAK == SCANCODE_BREAK, kbd_dlog_break_matches);

/* ======================================================================== */
/*  ロックキー (カナ・CAPS) — 方式 B: make で ON、break で OFF              */
/*                                                                          */
/*  PC-98 のカナ・CAPS は機械式ロックで、押し込んで留まると make、もう一度  */
/*  押して外れると break が来る。根拠:                                      */
/*    - BIOS 自身がこの扱い。0000:053Ah (KB_SHFT_STS) の bit2 カナ / bit1   */
/*      CAPS は SHIFT・CTRL と同じ「1= 押されている」で、「INT 09h ハンドラ */
/*      がセット／リセットする」(memsys.md)。反転ではない。                  */
/*    - シフト系キーはキーボード側でリピートしない (memsys.md 052A〜0539h)。 */
/*    - NP21/W の keystat.c「シフトメカニカル処理」も 1 回目 make・2 回目     */
/*      break を送る。kbdstat -w で観測 (TASK_KBD_NAV §3-1)。                */
/*  以前 (方式 A) は make で反転して break を捨てていたので、ロックを外して  */
/*  も KANA / CAPS が立ったままになった。make の繰り返しが来ても ON のまま   */
/*  (冪等) なので、ロックの物理状態から外れない。                            */
/*  V86 セッション中も追う (打鍵はゲストへ回すが、ロックの物理状態は OS32   */
/*  側でも正しく持っておく — セッション後に食い違わないように)。            */
/*  状態の所有者は sys/hal/input/kbd_merge.c。                                   */
/* ======================================================================== */
/* 起動時のロックの初期値。ロックしたまま電源を入れた / リセットした場合、
 * OS32 が IRQ1 を開く前の make は OS32 には届かない (BIOS が受けたか、kbd_init
 * の読み捨てで消える)。そこで BIOS が INT 09h で追ってきた 0000:053Ah の
 * カナ・CAPS ビットを引き継ぐ。電源投入時にロック済みのキーを BIOS が知って
 * いるかは資料に無い (実機は票 TASK_KBD_NAV §3 の表の 6 で見る)。
 * SHIFT / CTRL / GRPH は引き継がない
 * (押しっぱなしなら離したときの break で落ち、押し続けていなければ 0 が正しい)。 */
static u8 kbd_lock_bits_from_bios(u8 bios_shift)
{
    u8 s = 0;

    if (bios_shift & BIOS_KB_SHIFT_CAPS) s |= SHIFT_CAPS;
    if (bios_shift & BIOS_KB_SHIFT_KANA) s |= SHIFT_KANA;
    return s;
}

/* Use the immutable boot BDA; page0 is NP outside V86 sessions. */
#if defined(KBD_HOST_TEST) && !defined(__KERNEL_BUILD__)
extern u8 kbd_host_bios_shift;   /* tools/tests/kbd_dlog_host.c */
static u8 kbd_bios_shift_peek(void) { return kbd_host_bios_shift; }
#else
static u8 kbd_bios_shift_peek(void)
{
    return v86_bios_real_lowmem()[BIOS_WORK_KB_SHIFT];
}
#endif

/* ======================================================================== */
/*  kbd_irq_handler — IRQ1 割り込みハンドラ (Cレベル)                      */
/*  ASMスタブ (sys/arch/x86/kernel/isr_stub.asm の irq_stub_1、呼び手はここだけ) から    */
/*  呼ばれる。戻り値は「IRQ1 を V86 ゲストへ反射してよいか」:               */
/*  0041h から実データを読んだとき (DATA / OVERRUN) だけ 1。空 IRQ・エラー */
/*  で反射すると、ゲストは空の仮想 FIFO を読んで偽の打鍵を得る              */
/*  (以前は 0x00 = ESC のメイク、docs/POLICY_DEBUG.md#4-57-キーボード-8251-の信号線)。                      */
/*  スタブは V86 中でなければ戻り値に関係なく反射しない (EFLAGS.VM を見る)。*/
/* ======================================================================== */
static void kbd_deliver(u8 scancode);

int kbd_irq_handler(void)
{
    u8 scancode;
    u8 st;
    int kind;
    u8 lflags;

    /* 0041h を読む前に 0043h を見る (票: 実機の打鍵不達、docs/POLICY_DEBUG.md#4-57-キーボード-8251-の信号線)。
     *   RxRDY = 0      → 空 IRQ。0041h は読まない (読んでも打鍵ではない)。
     *   PE / FE        → 0041h を読み捨て (化けたバイトは使わない)、
     *                    コマンド語 (ER 込み) を書き直してエラーを解除する。
     *   OE だけ        → 前のバイトを取りこぼしただけで 0041h のバイトは
     *                    正しい。使って、ER で OE を解除する (数える)。
     * NP21/W の keyboard_i43 は `status | 0x85` を返し、IRQ1 の前に RxRDY
     * (status bit1) を立てる。エラービットは向こうのバッファが溢れたときの
     * OE だけなので、通常の打鍵では従来どおり KBD_ST_DATA に落ちる。
     * **V86 分岐より前に置く** — V86 中も化けたバイトはゲストへ渡さない
     * (ゲストの 0041h/0043h は sys/platform/pc98/legacy/v86_kbd.c の FIFO が答えるので、
     * 実チップを読むのはここだけ)。 */
    st = (u8)inp(KBD_CMD);
    kbd_diag_irq++;
    kbd_diag_last_st = st;
    kind = kbd_status_classify(st);
    if (kind == KBD_ST_EMPTY) {
        kbd_diag_empty++;
        return kbd_status_reflects(kind);
    }
    if (kind == KBD_ST_ERROR) {
        (void)inp(KBD_DATA);
        outp(KBD_CMD, KBD_CMD_ERRRST_RXE_RTYHIGH);
        kbd_diag_err++;
        return kbd_status_reflects(kind);
    }

    /* μPD8251Aからスキャンコード読み取り */
    scancode = (u8)inp(KBD_DATA);
    kbd_diag_last_code = scancode;
    if (kind == KBD_ST_OVERRUN) {
        /* バイトを読んだ後で OE を解除する (ER はフラグを落とすだけで、
         * 受信データには触らない)。 */
        outp(KBD_CMD, KBD_CMD_ERRRST_RXE_RTYHIGH);
        kbd_diag_overrun++;
    }

    /* 記録のフラグは配る前の状態で決める (配った結果で V86 を抜けることがある) */
    lflags = 0;
    if (kind == KBD_ST_OVERRUN) lflags |= KBD_DLOG_F_OVERRUN;
    if (legacy_active())        lflags |= KBD_DLOG_F_V86;
    if (kbd_gui_mode)           lflags |= KBD_DLOG_F_GUI;

    kbd_deliver(scancode);

    /* 配った**後**に積む — 修飾キー自身の行に更新後の kbd_shift_state を載せる
     * (raw リングと同じ約束。カナの行の KANA ビットで「その make / break で
     * 状態がどう変わったか」が読める)。 */
    kbd_dlog_push(&kbd_dlog, scancode, kbd_shift_state, lflags);
    return kbd_status_reflects(kind);
}

/* 読んだ 1 バイトを配る (V86 の FIFO か、OS32 のリング)。途中の return は
 * 配らない場合で、反射の判定 (上の戻り値) には効かない。 */
static void kbd_deliver(u8 scancode)
{
    kbd_hal_capture(scancode);
    if (legacy_active()) {
        if (legacy_kbd_push(scancode)) legacy_request_exit();
        return;
    }
    if (scancode == KEY_STOP && input_key_pressed(KEY_CTRL) &&
        !(kbd_gui_mode && kbd_stop_suppressed)) ring3_abort_request();
}

/* ======================================================================== */
/*  kbd_init — キーボード初期化                                             */
/*  μPD8251Aの初期化とIRQ1有効化                                            */
/* ======================================================================== */
static const struct InputLegacyProvider kbd_legacy_provider;

void kbd_init(void)
{
    u8 dummy;
    u32 flushed = 0;

    input_legacy_register(&kbd_legacy_provider);

    /*
     * μPD8251A 初期化
     *
     * FreeBSDのPC-98 pckbd.c では init_keyboard() は空関数で、
     * BIOSが既に8251Aを初期化済みであることを前提としている。
     *
     * 我々のベアメタルOSでも、ブートローダ経由でBIOSが起動時に
     * 8251Aを初期化しているため、そのまま使える。前提を守るとは
     * **BIOS の定常値以外を書かない**こと — 書くのはエラーリセットを
     * 兼ねた定常値 0x16 (ER / RxE / DTR = RTY# HIGH) だけ。以前は 0x14
     * (DTR = 0 = RTY# LOW = 再送要求) を書いて自分で壊していた
     * (実機 PC-9821Ra266、2026-09-23)。モード語からのやり直しはしない
     * (第 2 段の候補として kbd.h の KBD_MODE_1S_ODD_8B_X16 に残すだけ)。
     */
    kbd_diag_init_before = (u8)inp(KBD_CMD);

    /* 既存のデータを読み捨て (バッファフラッシュ) */
    while (inp(KBD_CMD) & KBD_STAT_RXRDY) {
        dummy = (u8)inp(KBD_DATA);
        flushed++;
    }
    (void)dummy;

    /* コマンド: エラーリセット(D4) + 受信イネーブル(D2) + DTR=1 (RTY# HIGH) */
    kbd_diag_cmd = KBD_CMD_ERRRST_RXE_RTYHIGH;
    outp(KBD_CMD, kbd_diag_cmd);
    kbd_diag_init_after = (u8)inp(KBD_CMD);
    kbd_diag_flushed = flushed;
    kbd_diag_irq = 0;
    kbd_diag_empty = 0;
    kbd_diag_err = 0;
    kbd_diag_overrun = 0;
    kbd_diag_last_st = 0;
    kbd_diag_last_code = 0;
    kbd_dlog_reset(&kbd_dlog);

    /* バッファクリア */
    legacy_cooked_flush();
    legacy_cooked_reset_dropped();
    /* カナ・CAPS のロックは BIOS の 053Ah から引き継ぐ (kbd_lock_bits_from_bios)。
     * IRQ1 はまだ閉じているので ISR の所有権に触れない。 */
    kbd_hal_init(kbd_lock_bits_from_bios(kbd_bios_shift_peek()));
    kbd_gui_mode = 0;       /* 起動直後は CUI (WM 未登録) */
    kbd_stop_suppressed = 0;

    legacy_raw_flush();

    /* キーボードIRQを有効化 */
    irq_enable(KBD_IRQ);

    kprintf(TATTR_WHITE, "[kbd] st=%02x -> %02x cmd=%02x flushed=%u lock=%02x\n",
            (u32)kbd_diag_init_before, (u32)kbd_diag_init_after,
            (u32)kbd_diag_cmd, kbd_diag_flushed, (u32)kbd_shift_state);
}

/* ======================================================================== */
/*  kbd_diag — 診断カウンタを写す (KAPI v62、シェルの `kbdstat`)            */
/*  ISR が書く u32 は割り込み禁止の間に一括で写す (同じ瞬間の組にする)。   */
/* ======================================================================== */
int kbd_diag(KbdDiag *out)
{
    unsigned int flags;
    u32 irq, empty, err, overrun;
    u8 last_st, last_code;

    if (out == NULL) {
        return OS32_ERR_INVAL;
    }
    /* 禁止区間ではローカルへ写すだけ (呼び手のページには IF=1 で書く) */
    flags = irq_save();
    irq       = kbd_diag_irq;
    empty     = kbd_diag_empty;
    err       = kbd_diag_err;
    overrun   = kbd_diag_overrun;
    last_st   = kbd_diag_last_st;
    last_code = kbd_diag_last_code;
    irq_restore(flags);
    out->irq_count      = irq;
    out->empty_count    = empty;
    out->err_count      = err;
    out->last_st        = last_st;
    out->last_code      = last_code;
    out->flushed        = kbd_diag_flushed;
    out->init_st_before = kbd_diag_init_before;
    out->init_st_after  = kbd_diag_init_after;
    out->cmd            = kbd_diag_cmd;
    /* いまの 0043h。IRQ が 1 回も来ていない (irq_count = 0) とき、RxRDY が
     * 立ったままなら 8251 は受けている = PIC 側、落ちていれば キーボードが
     * 送っていない、と分けるのに使う (last_st は IRQ の中でしか更新されない)。
     * ステータスの読みは 8251A の状態を変えない。 */
    out->now_st         = (u8)inp(KBD_CMD);
    /* u16 に収まらなければ 0xFFFF で止める (折り返すと 0 に見える) */
    out->overrun_count  = (u16)(overrun > U16_MAX ? U16_MAX : overrun);
    return 0;
}

/* ======================================================================== */
/*  kbd_diag_log — 受信記録を写す (KAPI v67、シェルの `kbdstat -w`)         */
/*  after_seq より新しい分を古い順に最大 max 件 (KBD_DLOG_CAP で頭打ち)。    */
/*  戻り = 写した件数 (0 = 新しい分なし) / OS32_ERR_INVAL (out が NULL・     */
/*  max <= 0)。上書きで失われた分は飛ばすので、呼び手は先頭の seq が         */
/*  after_seq + 1 かで取りこぼしを知る。禁止区間ではローカルへ写すだけ      */
/*  (呼び手のページには IF=1 で書く — kbd_diag と同じ)。                   */
/* ======================================================================== */
int kbd_diag_log(u32 after_seq, KbdDiagLogEnt *out, int max)
{
    KbdDiagLogEnt tmp[KBD_DLOG_CAP];
    unsigned int flags;
    int n;
    int i;

    if (out == NULL || max <= 0) {
        return OS32_ERR_INVAL;
    }
    if (max > KBD_DLOG_CAP) {
        max = KBD_DLOG_CAP;
    }
    flags = irq_save();
    n = kbd_dlog_copy(&kbd_dlog, after_seq, tmp, max);
    irq_restore(flags);
    for (i = 0; i < n; i++) {
        out[i] = tmp[i];
    }
    return n;
}

/* ======================================================================== */
/*  公開API                                                                */
/* ======================================================================== */

int kbd_has_key(void)
{
    /* GUI 中の cooked リング (kbd_buf) は常に空なので、注入リングを見る
     * (kbd_gui_mode の説明)。**譲らない**のが D8 との違い: この関数は KAPI に
     * 無く CPL=3 から呼べないうえ、戻り値が「値か -1」ではなく真偽なので、
     * exec_resume が EAX に書く -1 / 1 バイトのどちらとも噛み合わない
     * (1 バイトを書けば、真を返しつつその 1 バイトを落とすことになる)。
     * ポーリングで譲りたい呼び手は kbd_trygetchar / kbd_trygetkey を使う。 */
    if (kbd_gui_mode) return kbd_inject_pending() > 0;
    return legacy_cooked_pending() > 0;
}

/* GUI モード中の 1 バイト取り出し (票 K7 D2)。GUI 中の IRQ1 は cooked リング
 * に積まないので、打鍵は端末アプリが kbd_inject() で注ぐ注入リングから来る。
 * 取れたら 0..255、無ければ -1。 */
static int kbd_gui_trygetbyte(void)
{
    u8 b = 0;
    if (!kbd_inject_take(&b)) return -1;
    return (int)b;
}

static int kbd_local_trykey(void) { return legacy_cooked_take(); }
static int kbd_local_peekkey(void) { return legacy_cooked_peek(); }

static int kbd_legacy_gui(void) { return kbd_gui_mode; }
static int kbd_legacy_terminal_enabled(void)
{
    return rshell_active && !con_sink_capture_active();
}
static int kbd_legacy_inject_peek(void)
{
    u8 b = 0;
    return kbd_inject_peek(&b) ? (int)b : -1;
}
/* The x86 adapter supplies its native PIT clock; the bridge has no clock. */
static void kbd_legacy_park_poll(void)
{
    (void)exec_park_poll(tick_count);
}
static void kbd_foreground_poll(void);
static u32 kbd_legacy_wait_ticks(void) { return tick_count; }
static u32 kbd_legacy_wait_timeout(void) { return KBD_RSHELL_TIMEOUT_SEC * PIT_HZ; }
static void kbd_legacy_park_kbd(int interruptible)
{
    (void)interruptible;
    (void)exec_park_kbd();
}
static void kbd_legacy_wait_step(void)
{
    idle_wait_step();
}
#if OS32_HOOK_IME
static int kbd_capture_ime(const KmKey *key, u8 old, const KmEvalResult *result)
{
    if (!fep_services_public()) return result->action != 0;
    return input_capture_ime(key, old, result);
}

#endif

static const struct InputLegacyProvider kbd_legacy_provider = {
#if OS32_HOOK_IME
    .capture_ime = kbd_capture_ime, .flush_actions = input_ime_action_flush,
    .read_cooked = input_read_cooked,
#endif
    .terminal_try = serial_trygetchar, .terminal_peek = serial_peekchar,
    .inject_try = kbd_gui_trygetbyte, .inject_peek = kbd_legacy_inject_peek,
    .try_key = kbd_local_trykey, .peek_key = kbd_local_peekkey,
    .gui_active = kbd_legacy_gui, .terminal_enabled = kbd_legacy_terminal_enabled,
    .park_poll = kbd_legacy_park_poll, .park_kbd = kbd_legacy_park_kbd,
    .wait_ticks = kbd_legacy_wait_ticks, .wait_timeout_ticks = kbd_legacy_wait_timeout,
    .capture_active = con_sink_capture_active, .wait_pending = ring3_wait_pending,
    .wait_step = kbd_legacy_wait_step, .wait_prepare = kbd_foreground_poll,
};

#include "kbd_fgpoll.inc"

int kbd_trygetchar(void)
{
    kbd_foreground_poll();
    return input_legacy_try(IL_CHAR);
}

/* ======================================================================== */
/*  kbd_trygetchar_local — **ローカルの打鍵だけ**を取る                     */
/*                                                                          */
/*  `kbd_trygetchar()` は rshell 中 (`rshell_active`) シリアルも見るので、    */
/*  「この 1 バイトはどこから来たか」を呼び手が知る方法が無かった。          */
/*  rshell の番犬は「シリアル由来の行」だけを往復の証拠に数えるので、        */
/*  **シリアルを見ない読み口**が要る (Codex レビュー往復 3 ④)。             */
/*                                                                          */
/*  これがあると rshell は「シリアルを 1 回読む → 空ならローカルを 1 回読む」 */
/*  と書けて、2 度読みのあいだに届いたバイトが由来の印を落とす窓が消える。   */
/*                                                                          */
/*  cooked リングだけを見る。GUI 中 (`kbd_gui_mode`) の cooked リングは常に  */
/*  空なので -1 を返す (注入リングは見ない — GUI の入力は WM の持ち物)。     */
/*  **譲らない** (`exec_park_poll` を呼ばない): 由来を知るためだけの口で、    */
/*  待つ意図は呼び手の側にある。                                            */
/* ======================================================================== */
int kbd_trygetchar_local(void)
{
    return input_legacy_try(IL_LOCAL_CHAR);
}

/* GUI モード中のブロッキング待ち (票 K7 D1 / §5 R1)。
 *
 *   1. 注入リングに文字があれば即返す (UTF-8 の続きバイトを含む)。
 *   2. 無ければ **第 2 の park 点**として exec_park_kbd() を試す。成立すれば
 *      戻らない — WM が起こすとき exec_resume が注入リングの 1 バイトを EAX に
 *      入れるので、アプリからは kbd_getchar() が普通に値を返したように見える。
 *   3. park できない文脈 (CPL=0 の呼び手 / syscall の外 / CUI の入れ子の子)
 *      だけ、従来どおり `hlt` で待つ。
 *
 *  `hlt` ループを GUI 中の CPL=3 アプリに残すと、syscall の中で止まったまま
 *  協調型の全体 (gshell と他の 3 本) が動かなくなる (票 §0)。 */
static int kbd_getchar_wait(int interruptible)
{
    return input_legacy_wait(IL_CHAR, interruptible);
}

/* u16キーコードを返す (上位=スキャンコード, 下位=ASCII)。
 * GUI 中は下位 8bit だけが意味を持ち、スキャンコードは 0 (票 D7) —
 * 端末経由で届く打鍵にはスキャンコードが無い。 */
int kbd_getkey_wait(int interruptible)
{
    return input_legacy_wait(IL_KEY, interruptible);
}

/* ノンブロッキング版: キーコードデータ(u16)を返す。なければ-1 */
int kbd_trygetkey(void)
{
    kbd_foreground_poll();
    return input_legacy_try(IL_KEY);
}

/* 取り出さずに次のキーを覗く (継承バグ「source が ESC 以外も食う」)。
 * 戻り値は kbd_trygetkey と同じ形 (上位=スキャンコード, 下位=ASCII。GUI 中は
 * 下位 8bit だけ)。無ければ -1。**キューは 1 バイトも動かさない**ので、
 * 「ESC なら打ち切る、ESC でなければ次の読み手に残す」が書ける。取り除きたい
 * ときは覗いた後に kbd_trygetkey を 1 回呼ぶ (ESC のときだけ)。
 *
 * 源の見る順番は kbd_trygetkey と 1 行も違えない (GUI の注入リング →
 * rshell のシリアル → cooked リング)。違えると「覗いたキー」と「次に
 * 取り出されるキー」が別物になり、ESC の取り除きが別のキーを消す。
 *
 * D8 のポーリング型 yield (exec_park_poll) は**呼ばない**。park は成立すると
 * 戻らず、WM が起こすときに exec_resume が注入リングの 1 バイトを取り出して
 * EAX に入れてしまう — 覗いただけのはずのキーが消える。覗きは「今そこに
 * あるか」を見るだけの口なので、譲りたい呼び手は kbd_trygetchar を使う。 */
int kbd_peekkey(void)
{
    return input_legacy_peek(IL_KEY);
}

/* 修飾キー(Ctrl/Shift/Alt等)の押下状態を取得 */
u32 kbd_get_modifiers(void)
{
    return (u32)kbd_shift_state;
}

/* 指定スキャンコードのキーが現在押されているかを返す (1=押下中, 0=離されている) */
int kbd_is_pressed(int scancode)
{
    if (scancode < 0 || scancode >= KBD_KEY_COUNT) return 0;
    return input_key_pressed((u8)scancode);
}


/* 生 make/break イベントを 1 件取り出す (レビュー ⑥)。無ければ -1。
 * 戻り値 = keycode | (down << 8) | (mods << KBD_RAW_MOD_SHIFT)。down=1 が押下 (make)、0 が離し
 * (break)。mods はそのイベント時点の修飾状態 (SHIFT_* の 7bit、レビュー #3 ②)。
 * GUI モード中 (kbd_set_gui_mode(1)) にしか積まれない。
 * WM (gshell) が Key down/up イベントを作るのに使う。 */
int kbd_trygetrawkey(void)
{
    return legacy_raw_take();
}

/* 待ち行列が満杯で捨てた打鍵の累計を返す (契約 T3、GUI v1.1 の KAPI)。 */
u32 kbd_dropped_count(void)
{
    return legacy_cooked_dropped() + legacy_raw_dropped();
}

int kbd_getchar(void) { return kbd_getchar_wait(0); }
int kbd_getkey(void) { return kbd_getkey_wait(0); }
int kbd_getchar_kapi(void)
{
    int c = kbd_getchar_wait(1);
    if (c < 0) ring3_abort_check();
    return c;
}
int kbd_getkey_kapi(void)
{
    int c = kbd_getkey_wait(1);
    if (c < 0) ring3_abort_check();
    return c;
}
