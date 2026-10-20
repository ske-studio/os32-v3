/* ======================================================================== */
/*  KPEEK_CLI_HOST.C — shell の `peek` (userland/shell/cmd_peek.c) をホストで */
/*                                                                          */
/*  実物の cmd_peek.c を #include し、KernelAPI の kprintf / kdebug_call /   */
/*  version だけを偽物にする (lspci_cmd_host.c と同じ作り)。見るのは        */
/*  TASK_KPEEK §2-1・§2-7・§4 の CLI 行:                                    */
/*    - 引数の解釈 (-k/-p/-m、-w1/2/4、16 進、既定長 0x10、-m は -w 必須)    */
/*    - 64 KB 上限・幅の倍数・4 GB 越えは 1 回も呼ばずに `peek: err ARG`     */
/*    - 1 KB ずつの分割 (回数・番地・長さ・端数)、要求の大きさ 1064          */
/*    - 行の書式と 8 bit 和、**最後の行だけ端数** (K10)、CRC32 と frames     */
/*    - 誤りは 1 行だけ (読めた分の行を出さない)、NOSYS は KDEBUG=1 の案内   */
/*    - -l (T5、§2-8): probe → flags・tag・dst、行を出さず見出しと ok だけ、  */
/*      使えなければ `lan unavailable (state=..), serial` の後にシリアル版   */
/*  試験: tools/tests/test_kpeek_cli.py                                     */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* 本物の os32_kapi_shared.h は u32 を unsigned long にする (ILP32 で 32
 * ビット)。ホスト (LP64) では 64 ビットになるので、取り込むあいだだけ
 * long を int に読み替えて KdebugPeek (1064 B) の並びを本物のまま使う。 */
#define __cdecl
#define long int
#include "os32_kapi_shared.h"
#undef long
_Static_assert(sizeof(KdebugPeek) == 1064, "KdebugPeek layout");

/* ---- 贋の shell.h (cmd_peek.c が使う分だけ) ----------------------------- */
#define SH_STATUS_OK        0
#define SH_STATUS_ERROR     1
#define SH_STATUS_USAGE     2
#define str_eq(a, b) (strcmp((a), (b)) == 0)
extern KernelAPI *g_api;
int shell_cmd_peek(int argc, char **argv);
#define SHELL_H             /* cmd_peek.c の #include "shell.h" を空にする */

/* ---- 出力の写し -------------------------------------------------------- */
static char g_out[1 << 20];
static int  g_lines;          /* kprintf の回数 (1 行 1 回) */

static void h_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    size_t len = strlen(g_out);
    (void)attr;
    va_start(ap, fmt);
    vsnprintf(g_out + len, sizeof(g_out) - len, fmt, ap);
    va_end(ap);
    g_lines++;
}

/* ---- 実物 -------------------------------------------------------------- */
#include "../../userland/shell/cmd_peek.c"

/* ---- 偽の kdebug_call --------------------------------------------------- */
typedef struct { u32 op, size, space, addr, width, len, flags, tag; u8 dst[6]; } CallLog;
static CallLog g_calls[256];
static int     g_ncalls;
static int     g_fail_at = -1;     /* この回 (0 起点) で断る */
static u32     g_fail_status;
static u32     g_fail_addr;
static i32     g_fail_rc;          /* 0 なら status で断る、負なら rc で */
static int     g_leave_status;     /* 1 = status を書かない */
/* probe (KDEBUG_OP_PEEK_LAN_PROBE) の答え */
static i32     g_probe_rc;
static u32     g_probe_status;
static u32     g_probe_state;
static u8      g_probe_peer[6];
static u32     g_tick;

static u8 mem_byte(u32 a) { return (u8)((a * 7U + (a >> 8) * 13U + 3U) & 0xFFU); }

static i32 h_kdebug_call(u32 op, void *buf, u32 size)
{
    KdebugPeek *r = (KdebugPeek *)buf;
    u32 i;
    int idx = g_ncalls++;
    if (idx < (int)(sizeof(g_calls) / sizeof(g_calls[0]))) {
        g_calls[idx].op = op; g_calls[idx].size = size;
        g_calls[idx].space = r->space; g_calls[idx].addr = r->addr;
        g_calls[idx].width = r->width; g_calls[idx].len = r->len;
        g_calls[idx].flags = r->flags; g_calls[idx].tag = r->tag;
        memcpy(g_calls[idx].dst, r->dst, 6);
    }
    if (op == KDEBUG_OP_PEEK_LAN_PROBE) {
        static const u8 zero[6];
        if (g_probe_rc < 0) return g_probe_rc;
        r->status = g_probe_status;
        r->fail_addr = g_probe_state;
        if (g_probe_status == KDEBUG_PEEK_OK && memcmp(r->dst, zero, 6) == 0)
            memcpy(r->dst, g_probe_peer, 6);
        return 0;
    }
    if (idx == g_fail_at) {
        if (g_fail_rc < 0) return g_fail_rc;
        r->status = g_fail_status;
        r->fail_addr = g_fail_addr;
        /* 断った回の data は読まない約束 (§2-4)。ゴミを入れて、shell が
         * それを出さないことも見る。 */
        memset(r->data, 0xEE, sizeof(r->data));
        return 0;
    }
    if (g_leave_status) return 0;
    for (i = 0; i < r->len && i < KDEBUG_PEEK_CHUNK; i++)
        r->data[i] = mem_byte(r->addr + i);
    r->status = KDEBUG_PEEK_OK;
    r->fail_addr = 0;
    return 0;
}

static KernelAPI g_fake;
KernelAPI *g_api;

static int failed;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static void *h_mem_alloc(u32 n) { return malloc(n); }
static u32 h_get_tick(void) { return g_tick; }
static void h_mem_free(void *p) { free(p); }

static void reset(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.kprintf = (void (*)(u8, const char *, ...))h_kprintf;
    g_fake.kdebug_call = h_kdebug_call;
    g_fake.mem_alloc = h_mem_alloc;
    g_fake.mem_free = h_mem_free;
    g_fake.get_tick = h_get_tick;
    g_fake.version = KAPI_VERSION;
    g_api = &g_fake;
    g_out[0] = '\0';
    g_lines = 0;
    g_ncalls = 0;
    g_fail_at = -1;
    g_fail_status = 0;
    g_fail_addr = 0;
    g_fail_rc = 0;
    g_leave_status = 0;
    g_probe_rc = 0;
    g_probe_status = KDEBUG_PEEK_OK;
    g_probe_state = 2;              /* E82557_RUNNING */
    memcpy(g_probe_peer, "\x52\x54\x00\x12\x34\x56", 6);
    g_tick = 0x1000;
}

static int run(const char *line)
{
    char buf[256];
    char *argv[16];
    int argc = 0;
    char *p;
    snprintf(buf, sizeof(buf), "%s", line);
    for (p = strtok(buf, " "); p && argc < 16; p = strtok(NULL, " "))
        argv[argc++] = p;
    return shell_cmd_peek(argc, argv);
}

/* 独立に組み直した期待値 (ビット版 CRC32 と snprintf の行)。 */
static u32 ref_crc(u32 addr, u32 len)
{
    u32 c = 0xFFFFFFFFU, i;
    int k;
    for (i = 0; i < len; i++) {
        c ^= mem_byte(addr + i);
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
    }
    return c ^ 0xFFFFFFFFU;
}

static void ref_output(char *out, size_t cap, const char *sp, u32 addr, u32 w,
                       u32 len)
{
    size_t n = 0;
    u32 off, i;
    n += (size_t)snprintf(out + n, cap - n, "peek %s %08X w%u len=%08X\n",
                          sp, addr, w, len);
    for (off = 0; off < len; off += 16) {
        u32 row = len - off < 16 ? len - off : 16;
        unsigned sum = 0;
        n += (size_t)snprintf(out + n, cap - n, "%08X:", addr + off);
        for (i = 0; i < row; i++) {
            u8 b = mem_byte(addr + off + i);
            sum += b;
            n += (size_t)snprintf(out + n, cap - n, " %02X", b);
        }
        n += (size_t)snprintf(out + n, cap - n, "  |s=%02X\n", sum & 0xFFU);
    }
    snprintf(out + n, cap - n, "peek: ok len=%08X crc32=%08X frames=%u\n",
             len, ref_crc(addr, len), (len + 1023U) / 1024U);
}

static char g_ref[1 << 20];

static void expect_ok(const char *line, const char *sp, u32 addr, u32 w, u32 len)
{
    reset();
    CHECK(run(line) == SH_STATUS_OK);
    ref_output(g_ref, sizeof(g_ref), sp, addr, w, len);
    if (strcmp(g_out, g_ref) != 0) {
        fprintf(stderr, "--- got (%s)\n%.400s\n--- want\n%.400s\n", line, g_out, g_ref);
        failed++;
    }
}

static void expect_arg(const char *line)
{
    reset();
    if (run(line) != SH_STATUS_USAGE || strcmp(g_out, "peek: err ARG\n") != 0 ||
        g_ncalls != 0) {
        fprintf(stderr, "FAIL arg %s: out=%s calls=%d\n", line, g_out, g_ncalls);
        failed++;
    }
}

/* ---- ケース ------------------------------------------------------------ */
static void case_golden(void)
{
    /* 書式を文字で固定する (§2-7): 2 行 + 端数 4 B の行。 */
    static const char want[] =
        "peek k 00100000 w1 len=00000024\n"
        "00100000: 03 0A 11 18 1F 26 2D 34 3B 42 49 50 57 5E 65 6C  |s=78\n"
        "00100010: 73 7A 81 88 8F 96 9D A4 AB B2 B9 C0 C7 CE D5 DC  |s=78\n"
        "00100020: E3 EA F1 F8  |s=B6\n";
    char crc_line[64];
    reset();
    /* mem_byte の (a >> 8) 項は 0x100000 では 0x1000*13 = 0xD000 (下位 0)。 */
    CHECK(run("peek 100000 24") == SH_STATUS_OK);
    CHECK(strncmp(g_out, want, sizeof(want) - 1) == 0);
    snprintf(crc_line, sizeof(crc_line), "peek: ok len=00000024 crc32=%08X frames=1\n",
             ref_crc(0x100000U, 0x24U));
    CHECK(strcmp(g_out + sizeof(want) - 1, crc_line) == 0);
    CHECK(g_lines == 5);        /* 1 行 1 回 */
    CHECK(g_ncalls == 1);
    CHECK(g_calls[0].op == KDEBUG_OP_PEEK && g_calls[0].size == 1064);
    CHECK(g_calls[0].space == KDEBUG_PEEK_SPACE_RAM && g_calls[0].width == 1);
    CHECK(g_calls[0].addr == 0x100000U && g_calls[0].len == 0x24U);
}

static void case_format(void)
{
    expect_ok("peek 1000", "k", 0x1000, 1, 0x10);             /* 既定の長さ */
    expect_ok("peek -k 0x2000 1", "k", 0x2000, 1, 1);          /* 1 B の端数行 */
    expect_ok("peek -p 3000 20", "k", 0x3000, 1, 0x20);        /* -p = -k、端数無し */
    expect_ok("peek 4000 2F", "k", 0x4000, 1, 0x2F);
    expect_ok("peek -w2 5002 1E", "k", 0x5002, 2, 0x1E);
    expect_ok("peek -m -w4 FE000000 8", "m", 0xFE000000U, 4, 8);
    expect_ok("peek FFFFFFF0 10", "k", 0xFFFFFFF0U, 1, 0x10);  /* 4 GB ちょうどまで */
    CHECK(g_calls[0].space == KDEBUG_PEEK_SPACE_RAM);
    reset();
    CHECK(run("peek -w4 -m 10000 4") == SH_STATUS_OK);
    CHECK(g_calls[0].space == KDEBUG_PEEK_SPACE_MMIO && g_calls[0].width == 4);
}

static void case_split(void)
{
    int i, ok = 1;
    expect_ok("peek 100000 10000", "k", 0x100000, 1, 0x10000);  /* 64 KB */
    CHECK(g_ncalls == 64);
    for (i = 0; i < g_ncalls && i < 64; i++)
        ok &= g_calls[i].addr == 0x100000U + (u32)i * 1024U && g_calls[i].len == 1024U &&
              g_calls[i].size == 1064U;
    CHECK(ok);
    CHECK(strstr(g_out, "frames=64\n") != NULL);
    expect_ok("peek 200000 401", "k", 0x200000, 1, 0x401);       /* 端数 1 B */
    CHECK(g_ncalls == 2 && g_calls[1].addr == 0x200400U && g_calls[1].len == 1U);
    expect_ok("peek -w4 300000 7FC", "k", 0x300000, 4, 0x7FC);
    CHECK(g_ncalls == 2 && g_calls[0].len == 1024U && g_calls[1].len == 0x3FCU);
}

static void case_args(void)
{
    expect_arg("peek");                 /* 番地無し */
    expect_arg("peek 0 0");             /* 長さ 0 */
    expect_arg("peek 0 10001");         /* 64 KB 超え */
    expect_arg("peek -w2 1001 2");      /* 番地が幅の倍数でない */
    expect_arg("peek -w4 1000 6");      /* 長さが幅の倍数でない */
    expect_arg("peek -m 1000 4");       /* -m は -w 必須 */
    expect_arg("peek FFFFFFF0 11");     /* 4 GB を越える */
    expect_arg("peek xyz");
    expect_arg("peek 123456789");       /* 9 桁 */
    expect_arg("peek 0x");
    expect_arg("peek 1000 10 20");      /* 余り */
    expect_arg("peek -q 1000");
    expect_arg("peek -w3 1000");
}

static void case_errors(void)
{
    /* 3 回目で NOMAP: 読めた 2 KB の行も見出しも出さない (§2-7)。 */
    reset();
    g_fail_at = 2; g_fail_status = KDEBUG_PEEK_NOMAP; g_fail_addr = 0x00101000U;
    CHECK(run("peek 100000 C00") == SH_STATUS_ERROR);
    CHECK(strcmp(g_out, "peek: err NOMAP at 00101000\n") == 0);
    CHECK(g_ncalls == 3);

    reset();
    g_fail_at = 0; g_fail_status = KDEBUG_PEEK_DENY; g_fail_addr = 0xFE000000U;
    CHECK(run("peek FE000000 10") == SH_STATUS_ERROR);
    CHECK(strcmp(g_out, "peek: err DENY at FE000000\n") == 0);

    reset();
    g_fail_at = 0; g_fail_status = KDEBUG_PEEK_WINDOW; g_fail_addr = 0xE0000000U;
    CHECK(run("peek -m -w4 E0000000 10") == SH_STATUS_ERROR);
    CHECK(strcmp(g_out, "peek: err WINDOW at E0000000\n") == 0);

    /* 部品が無い (T2 前の今の姿): KAPI が OS32_ERR_NOSYS。 */
    reset();
    g_fail_at = 0; g_fail_rc = OS32_ERR_NOSYS;
    CHECK(run("peek 100000 10") == SH_STATUS_ERROR);
    CHECK(strcmp(g_out, "peek: err NOSYS (kdebug not loaded; boot with KDEBUG=1)\n") == 0);
    CHECK(g_lines == 1);

    reset();
    g_fail_at = 0; g_fail_status = KDEBUG_PEEK_NOSYS;
    CHECK(run("peek 100000 10") == SH_STATUS_ERROR);
    CHECK(strcmp(g_out, "peek: err NOSYS (kdebug not loaded; boot with KDEBUG=1)\n") == 0);

    /* 古いカーネル (slot 253 が無い) は呼ばない。 */
    reset();
    g_fake.version = 77;
    CHECK(run("peek 100000 10") == SH_STATUS_ERROR);
    CHECK(g_ncalls == 0);
    CHECK(strcmp(g_out, "peek: err NOSYS (kdebug not loaded; boot with KDEBUG=1)\n") == 0);

    reset();
    g_fail_at = 1; g_fail_rc = OS32_ERR_INVAL;
    CHECK(run("peek 100000 800") == SH_STATUS_ERROR);
    CHECK(strncmp(g_out, "peek: err KAPI rc=", 18) == 0 && strchr(g_out, '\n') ==
          g_out + strlen(g_out) - 1);

    /* status を書かない部品: OK と読まない。 */
    reset();
    g_leave_status = 1;
    CHECK(run("peek 100000 10") == SH_STATUS_ERROR);
    CHECK(strncmp(g_out, "peek: err STATUS=", 17) == 0);
}

static void case_lan(void)
{
    static const u8 mac[6] = { 0x00, 0xAA, 0x00, 0x11, 0x22, 0x33 };
    char want[256];
    int i, ok = 1;
    u32 tag1;

    /* -l<mac>: probe に宛先、1 KB ずつ flags=LAN・同じ tag・dst、行は出さない */
    reset();
    CHECK(run("peek -l00:aa:00:11:22:33 100000 800") == SH_STATUS_OK);
    CHECK(g_ncalls == 3 && g_calls[0].op == KDEBUG_OP_PEEK_LAN_PROBE &&
          g_calls[0].size == 1064 && memcmp(g_calls[0].dst, mac, 6) == 0);
    for (i = 1; i < 3; i++)
        ok &= g_calls[i].op == KDEBUG_OP_PEEK && g_calls[i].flags == KDEBUG_PEEK_F_LAN &&
              g_calls[i].tag == 0x1000U && memcmp(g_calls[i].dst, mac, 6) == 0 &&
              g_calls[i].addr == 0x100000U + (u32)(i - 1) * 1024U;
    CHECK(ok);
    snprintf(want, sizeof(want),
             "peek k 00100000 w1 len=00000800\n"
             "peek: ok len=00000800 crc32=%08X tag=00001000 frames=2\n",
             ref_crc(0x100000U, 0x800U));
    CHECK(strcmp(g_out, want) == 0);
    CHECK(g_lines == 2);

    /* --lan / -l: 宛先は probe が写すペア。同じ tick でも tag を変える */
    reset();
    CHECK(run("peek --lan -w4 200000 10") == SH_STATUS_OK);
    tag1 = g_calls[1].tag;
    CHECK(g_calls[0].op == KDEBUG_OP_PEEK_LAN_PROBE && g_calls[0].dst[0] == 0 &&
          memcmp(g_calls[1].dst, g_probe_peer, 6) == 0 && g_calls[1].width == 4);
    reset();
    CHECK(run("peek -l 200000 10") == SH_STATUS_OK);
    CHECK(g_calls[1].tag != tag1 && g_calls[1].flags == KDEBUG_PEEK_F_LAN);

    /* lan.mod が無い (NP21/W): 1 行の後にシリアル版をそのまま */
    reset();
    g_probe_status = KDEBUG_PEEK_LAN; g_probe_state = 0;
    CHECK(run("peek -l 100000 24") == SH_STATUS_OK);
    ref_output(g_ref, sizeof(g_ref), "k", 0x100000, 1, 0x24);
    snprintf(want, sizeof(want), "%s", "peek: lan unavailable (state=OFF), serial\n");
    CHECK(strncmp(g_out, want, strlen(want)) == 0 &&
          strcmp(g_out + strlen(want), g_ref) == 0);
    CHECK(g_ncalls == 2 && g_calls[1].flags == 0 && g_calls[1].tag == 0);
    reset();
    g_probe_status = KDEBUG_PEEK_LAN; g_probe_state = 2;
    CHECK(run("peek -l 100000 10") == SH_STATUS_OK);
    CHECK(strncmp(g_out, "peek: lan unavailable (state=RUNNING no peer), serial\n", 54) == 0);
    reset();
    g_probe_status = KDEBUG_PEEK_LAN; g_probe_state = 7;
    CHECK(run("peek -l 100000 10") == SH_STATUS_OK);
    CHECK(strncmp(g_out, "peek: lan unavailable (state=QUARANTINED), serial\n", 50) == 0);
    reset();
    g_probe_rc = OS32_ERR_NOSYS;
    g_fail_at = 1; g_fail_rc = OS32_ERR_NOSYS;
    CHECK(run("peek -l 100000 10") == SH_STATUS_ERROR);
    snprintf(want, sizeof(want), "peek: lan unavailable (kdebug rc=%d), serial\n"
             "peek: err NOSYS (kdebug not loaded; boot with KDEBUG=1)\n", (int)OS32_ERR_NOSYS);
    CHECK(strcmp(g_out, want) == 0);

    /* 送れなかった: err LAN の 1 行だけ */
    reset();
    g_fail_at = 2; g_fail_status = KDEBUG_PEEK_LAN; g_fail_addr = 0x00100400U;
    CHECK(run("peek -l 100000 800") == SH_STATUS_ERROR);
    CHECK(strcmp(g_out, "peek: err LAN at 00100400\n") == 0);

    expect_arg("peek -l00:aa:00:11:22 100000");      /* MAC が短い */
    expect_arg("peek -l00:aa:00:11:22:3g 100000");
    expect_arg("peek -l00-aa-00-11-22-33 100000");
    expect_arg("peek -l00:aa:00:11:22:33: 100000");
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(void); } cases[] = {
        { "golden", case_golden }, { "format", case_format },
        { "split", case_split }, { "args", case_args }, { "errors", case_errors },
        { "lan", case_lan },
    };
    size_t i;
    int ran = 0;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (argc > 1 && strcmp(argv[1], cases[i].name) != 0) continue;
        cases[i].fn();
        ran++;
    }
    if (!ran) { fprintf(stderr, "unknown case\n"); return 2; }
    return failed ? 1 : 0;
}
