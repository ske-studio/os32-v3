/* ========================================================================
 *  FAULTTEST.C — CPL=3 の例外 kill と CTRL+STOP を起こす試験バイナリ
 *
 *  票: docs/tasks/v3/TASK_T1_LEDGER.md §4-2-N (T1b の NP21/W 回帰の未確認)
 *      docs/tasks/v3/TASK_T2_APPBAND.md §5-1 (T2a: park → resume 後の fault/STOP)
 *
 *  使い方:
 *    faulttest gp          #GP を起こす (CPL=3 からの hlt)
 *    faulttest de          #DE を起こす (0 で div)
 *    faulttest ud          #UD を起こす (ud2)
 *    faulttest pf          #PF を起こす (カーネル帯へ書く。ring3_fault と同じ比較用)
 *    faulttest loop [秒]   KAPI を呼ばない CPL=3 の無限ループ (CTRL+STOP 用)
 *    faulttest kloop [秒]  KAPI (get_tick) を連打するループ
 *    faulttest wait <gp|de|ud|pf|loop|kloop> [秒]  1 文字待ってから実行
 *
 *  wait は待機の 1 行を出して kbd_getchar で 1 文字待つ。GUI (gshell) では
 *  APP_STATE_WAIT_KEY で park し、キーで resume された後に既存の処理へ進む
 *  (exec_resume の setjmp 着地の試験)。CUI でも同じ KAPI でキーを待つ。
 *
 *  fault 系は起こす前に 1 行出す。kill されればシェルに `[ring3] ...` の行が
 *  出て、この後ろには来ない。戻ってきたら `faulttest: SURVIVED <kind>` を出して
 *  2 で終わる (保護が効いていない証拠)。
 *
 *  #GP に hlt を選ぶ理由: i386 の HLT は CPL が 0 でなければ IOPL に関係なく
 *  #GP(0) になる。cli も CPL=3 のアプリでは #GP だが、それは OS32 が IOPL=0 で
 *  降ろしている (arch/x86/arch_cpu.h の arch_enter_user、EFLAGS=0x202) から
 *  成り立つだけで、IOPL の設定に依存する。hlt はその前提が要らない。
 *
 *  loop の秒数: CPL=3 からカーネルの tick_count は読めない (supervisor ページ)
 *  ので、ループに入る前に get_tick で「空回り K 回が何 tick か」を測り、
 *  本番は K 回の空回りを必要なだけ繰り返す。本番のループの中では KAPI を
 *  一度も呼ばない (CTRL+STOP が IRQ の出口で CPL=3 に効くかを見るため)。
 *  終わった後で get_tick を読み、実際にかかった tick を出す。
 *
 *  main() が **ファイルの最初の関数** であること (OS32X の約束)。
 * ======================================================================== */
#include "os32api.h"
#include "memmap.h"     /* KERNEL_LOAD_ADDR / PIT_HZ ([C4]) */
#include <stdbool.h>

#define CAL_MIN_TICKS   10u             /* 較正の 1 塊がこれ以上かかるまで倍にする */
#define CAL_START_ITERS 0x10000u
#define CAL_MAX_ITERS   0x40000000u
#define WAIT_ATTR       0x07

static int streq(const char *a, const char *b);
static unsigned parse_uint(const char *s);
static void usage(KernelAPI *api);
static void spin(unsigned n);
static int do_fault(KernelAPI *api, const char *kind);
static int do_loop(KernelAPI *api, unsigned sec);
static int do_kloop(KernelAPI *api, unsigned sec);

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned sec = 0;
    int mode_arg = 1;
    const char *kind;
    bool wait_key, fault_mode;

    if (argc < 2) {
        usage(api);
        return 1;
    }
    wait_key = streq(argv[mode_arg], "wait");
    if (wait_key) mode_arg++;
    if (argc <= mode_arg) {
        usage(api);
        return 1;
    }
    kind = argv[mode_arg];
    if (argc > mode_arg + 1) sec = parse_uint(argv[mode_arg + 1]);

    fault_mode = streq(kind, "gp") || streq(kind, "de") ||
                 streq(kind, "ud") || streq(kind, "pf");
    if (!fault_mode && !streq(kind, "loop") && !streq(kind, "kloop")) {
        api->kprintf(0x0C, "faulttest: unknown mode '%s'\n", kind);
        usage(api);
        return 1;
    }
    if (wait_key) {
        api->kprintf(WAIT_ATTR, "faulttest: waiting for a key (then %s)\n", kind);
        (void)api->kbd_getchar();
    }

    if (fault_mode)
        return do_fault(api, kind);
    if (streq(kind, "loop"))
        return do_loop(api, sec);
    return do_kloop(api, sec);
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static unsigned parse_uint(const char *s)
{
    unsigned v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (unsigned)(*s - '0');
        s++;
    }
    return v;
}

static void usage(KernelAPI *api)
{
    api->kprintf(0x07, "%s",
        "usage: faulttest gp|de|ud|pf\n"
        "       faulttest loop [sec]   pure CPL=3 loop (no KAPI)\n"
        "       faulttest kloop [sec]  get_tick loop\n"
        "       faulttest wait <gp|de|ud|pf|loop|kloop> [sec]  wait for a key first\n");
}

/* 空回り n 回 (n >= 1)。コンパイラに消させないため asm で書く。 */
static void spin(unsigned n)
{
    __asm__ __volatile__("1:\n\tdecl %0\n\tjnz 1b" : "+r"(n) : : "cc");
}

static int do_fault(KernelAPI *api, const char *kind)
{
    if (streq(kind, "gp")) {
        api->kprintf(0x0E, "%s", "faulttest: raising #GP (hlt at CPL=3)\n");
        __asm__ __volatile__("hlt" : : : "memory");
    } else if (streq(kind, "de")) {
        api->kprintf(0x0E, "%s", "faulttest: raising #DE (div by zero)\n");
        __asm__ __volatile__(
            "movl $1, %%eax\n\t"
            "xorl %%edx, %%edx\n\t"
            "xorl %%ecx, %%ecx\n\t"
            "divl %%ecx"
            : : : "eax", "ecx", "edx", "cc", "memory");
    } else if (streq(kind, "ud")) {
        api->kprintf(0x0E, "%s", "faulttest: raising #UD (ud2)\n");
        __asm__ __volatile__("ud2" : : : "memory");
    } else {
        volatile unsigned *kern = (volatile unsigned *)KERNEL_LOAD_ADDR;
        api->kprintf(0x0E, "faulttest: raising #PF (write to 0x%x)\n",
                     (unsigned)KERNEL_LOAD_ADDR);
        *kern = 0xDEADBEEFu;
    }

    api->kprintf(0x0C, "faulttest: SURVIVED %s\n", kind);
    return 2;
}

static int do_loop(KernelAPI *api, unsigned sec)
{
    unsigned iters = CAL_START_ITERS;
    unsigned took = 0;
    unsigned chunks, i, t, t0;

    if (sec == 0) {
        api->kprintf(0x0E, "%s", "faulttest: looping (CTRL+STOP to kill)\n");
        for (;;) {
            __asm__ __volatile__("" : : : "memory");
        }
    }

    /* 較正: tick の縁から測り、空回り iters 回が CAL_MIN_TICKS 以上になるまで倍 */
    for (;;) {
        t = api->get_tick();
        while ((t0 = api->get_tick()) == t) { }
        spin(iters);
        took = api->get_tick() - t0;
        if (took >= CAL_MIN_TICKS || iters >= CAL_MAX_ITERS) break;
        iters *= 2u;
    }
    if (took == 0) took = 1;
    chunks = (sec * (unsigned)PIT_HZ + took - 1u) / took;
    if (chunks == 0) chunks = 1;
    api->kprintf(0x07, "faulttest: calibrated %u iters = %u ticks, %u chunks\n",
                 iters, took, chunks);

    api->kprintf(0x0E, "faulttest: looping %u s (CTRL+STOP to kill)\n", sec);
    t0 = api->get_tick();
    for (i = 0; i < chunks; i++) spin(iters);   /* ここは KAPI を呼ばない */
    api->kprintf(0x07, "faulttest: loop done after %u ticks\n",
                 api->get_tick() - t0);
    return 0;
}

static int do_kloop(KernelAPI *api, unsigned sec)
{
    unsigned limit = sec * (unsigned)PIT_HZ;
    unsigned calls = 0;
    unsigned t0, now;

    api->kprintf(0x0E, "%s", "faulttest: kloop get_tick (CTRL+STOP to kill)\n");
    t0 = api->get_tick();
    for (;;) {
        now = api->get_tick();
        calls++;
        if (limit != 0 && now - t0 >= limit) break;
    }
    api->kprintf(0x07, "faulttest: kloop done after %u ticks, %u calls\n",
                 now - t0, calls);
    return 0;
}
