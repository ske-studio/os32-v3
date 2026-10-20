/* ======================================================================== */
/*  cmd_peek.c — `peek` (TASK_KPEEK §2-1・§2-7、段 T3)                       */
/*                                                                          */
/*  peek [-k|-p|-m] [-w1|-w2|-w4] <hex 番地> [<hex 長さ>]                    */
/*                                                                          */
/*  kdebug 部品のメモリ読み (KAPI kdebug_call、op KDEBUG_OP_PEEK) を 1 KB    */
/*  ずつ回し、shell が整形する。kdebug は生バイトを返すだけで、行・8 bit    */
/*  和・CRC32 はここで作る (§1-4)。出力 (§2-7):                             */
/*                                                                          */
/*    peek k 00100000 w1 len=00000024                                       */
/*    00100000: 55 AA ... (16 B)  |s=3F                                     */
/*    00100020: 01 02 03 04  |s=0A        <- 最後の行だけ端数 (K10)         */
/*    peek: ok len=00000024 crc32=89ABCDEF frames=1                         */
/*                                                                          */
/*  誤りは `peek: err <名> at <番地>` の 1 行だけで、読めた分は出さない —   */
/*  全部を読んでから整形する (要求長ぶんを heap に置く)。1 行は kprintf     */
/*  1 回 (rshell 中は 1 文字ずつシリアルへ写る、EOT は rshell)。            */
/*  -p は -k の別名 (カーネルは恒等写像、§1-2)。-m は -w を必須にする。     */
/*  ホスト側の照合: tools/rshell_peek.py。試験: tools/tests/kpeek_cli_host.c */
/* ======================================================================== */
#include "shell.h"
#include <stdlib.h>
#include "../../lib/crc32_core.inc"

#define KPEEK_CMD_MAX       0x10000UL  /* 1 コマンドの上限 (64 KB) */
#define KPEEK_DEFAULT_LEN   0x10UL
#define KPEEK_ROW_BYTES     16U
#define KPEEK_HEX_DIGITS    8U
#define KPEEK_KAPI_MIN      78U        /* kdebug_call (slot 253) の版 */
#define KPEEK_STATUS_UNSET  0xFFFFFFFFUL
#define KPEEK_ADDR_LAST     0xFFFFFFFFUL  /* 32 ビット空間の最後の番地 */
/* 1 行: 番地 8 + ": " + 16 x " XX" + "  |s=XX\n" + NUL */
#define KPEEK_LINE_MAX      96

/* status の名前 (KDEBUG_PEEK_* の番号順、os32_kapi_shared.h)。 */
static const char *const kpeek_status_names[] = {
    "OK", "ARG", "NOMAP", "DENY", "WIDTH", "WINDOW", "NOSYS", "LAN"
};
#define KPEEK_STATUS_COUNT \
    (sizeof(kpeek_status_names) / sizeof(kpeek_status_names[0]))

static KdebugPeek kpeek_req;   /* 1064 B、スタックに置かない */

static int kpeek_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 16 進 1〜8 桁 (先頭の 0x は任意)。0 = 成功、-1 = 形が違う。 */
static int kpeek_parse_hex(const char *s, u32 *out)
{
    u32 v = 0;
    u32 n = 0;
    int d;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (*s == '\0') return -1;
    for (; *s; s++) {
        d = kpeek_hexval(*s);
        if (d < 0 || n >= KPEEK_HEX_DIGITS) return -1;
        v = (v << 4) | (u32)d;
        n++;
    }
    *out = v;
    return 0;
}

static char *kpeek_put_hex(char *p, u32 v, u32 digits)
{
    static const char hex[] = "0123456789ABCDEF";
    u32 i;
    for (i = digits; i > 0; i--)
        *p++ = hex[(v >> ((i - 1U) * 4U)) & 0xFU];
    return p;
}

/* 1 行を作る: 番地・n バイト (n <= 16、端数行は残りだけ)・その行の 8 bit 和。 */
static void kpeek_format_row(char *line, u32 addr, const u8 *bytes, u32 n)
{
    char *p = line;
    u8 sum = 0;
    u32 i;
    p = kpeek_put_hex(p, addr, KPEEK_HEX_DIGITS);
    *p++ = ':';
    for (i = 0; i < n; i++) {
        *p++ = ' ';
        p = kpeek_put_hex(p, bytes[i], 2);
        sum = (u8)(sum + bytes[i]);
    }
    *p++ = ' '; *p++ = ' '; *p++ = '|'; *p++ = 's'; *p++ = '=';
    p = kpeek_put_hex(p, sum, 2);
    *p++ = '\n';
    *p = '\0';
}

static int kpeek_usage_err(void)
{
    g_api->kprintf(ATTR_RED, "%s", "peek: err ARG\n");
    return SH_STATUS_USAGE;
}

/* kdebug の断り (status != OK) を 1 行で。 */
static void kpeek_print_status(u32 status, u32 fail_addr)
{
    if (status == KDEBUG_PEEK_NOSYS) {
        g_api->kprintf(ATTR_RED, "%s",
                       "peek: err NOSYS (kdebug not loaded; boot with KDEBUG=1)\n");
    } else if (status < KPEEK_STATUS_COUNT) {
        g_api->kprintf(ATTR_RED, "peek: err %s at %08X\n",
                       kpeek_status_names[status], fail_addr);
    } else {
        g_api->kprintf(ATTR_RED, "peek: err STATUS=%u at %08X\n",
                       status, fail_addr);
    }
}

int shell_cmd_peek(int argc, char **argv)
{
    u32 space = KDEBUG_PEEK_SPACE_RAM;
    u32 width = 1;
    int width_given = 0;
    u32 addr = 0, len = KPEEK_DEFAULT_LEN;
    int npos = 0;
    int i;
    u32 off, n, frames, crc;
    i32 rc;
    u8 *buf;
    char line[KPEEK_LINE_MAX];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (str_eq(a, "-k") || str_eq(a, "-p")) {
            space = KDEBUG_PEEK_SPACE_RAM;
        } else if (str_eq(a, "-m")) {
            space = KDEBUG_PEEK_SPACE_MMIO;
        } else if (str_eq(a, "-w1") || str_eq(a, "-w2") || str_eq(a, "-w4")) {
            width = (u32)(a[2] - '0');
            width_given = 1;
        } else if (a[0] == '-') {
            return kpeek_usage_err();
        } else if (npos == 0) {
            if (kpeek_parse_hex(a, &addr) != 0) return kpeek_usage_err();
            npos++;
        } else if (npos == 1) {
            if (kpeek_parse_hex(a, &len) != 0) return kpeek_usage_err();
            npos++;
        } else {
            return kpeek_usage_err();
        }
    }
    /* 実行せずに断る (§2-1): 番地無し・長さ 0 / 上限超え・幅の倍数でない・
     * 4 GB を越える・-m で -w 無し。 */
    if (npos == 0 || len == 0 || len > KPEEK_CMD_MAX ||
        (addr % width) != 0 || (len % width) != 0 ||
        len - 1U > KPEEK_ADDR_LAST - addr ||
        (space == KDEBUG_PEEK_SPACE_MMIO && !width_given))
        return kpeek_usage_err();

    if (g_api->version < KPEEK_KAPI_MIN) {
        kpeek_print_status(KDEBUG_PEEK_NOSYS, addr);
        return SH_STATUS_ERROR;
    }
    buf = (u8 *)malloc(len);
    if (!buf) {
        g_api->kprintf(ATTR_RED, "peek: err NOMEM len=%08X\n", len);
        return SH_STATUS_ERROR;
    }

    /* 全部を読んでから出す (誤りなら 1 行だけ、§2-7)。 */
    frames = 0;
    for (off = 0; off < len; off += n) {
        n = len - off;
        if (n > KDEBUG_PEEK_CHUNK) n = KDEBUG_PEEK_CHUNK;
        memset(&kpeek_req, 0, sizeof(kpeek_req));
        kpeek_req.space = space;
        kpeek_req.addr = addr + off;
        kpeek_req.width = width;
        kpeek_req.len = n;
        kpeek_req.status = KPEEK_STATUS_UNSET;
        rc = g_api->kdebug_call(KDEBUG_OP_PEEK, &kpeek_req,
                                (u32)sizeof(kpeek_req));
        frames++;
        if (rc == OS32_ERR_NOSYS) {
            kpeek_print_status(KDEBUG_PEEK_NOSYS, addr + off);
            free(buf);
            return SH_STATUS_ERROR;
        }
        if (rc < 0) {
            g_api->kprintf(ATTR_RED, "peek: err KAPI rc=%d at %08X\n",
                           (int)rc, addr + off);
            free(buf);
            return SH_STATUS_ERROR;
        }
        if (kpeek_req.status != KDEBUG_PEEK_OK) {
            kpeek_print_status(kpeek_req.status, kpeek_req.fail_addr);
            free(buf);
            return SH_STATUS_ERROR;
        }
        memcpy(buf + off, kpeek_req.data, n);
    }

    g_api->kprintf(ATTR_CYAN, "peek %s %08X w%u len=%08X\n",
                   space == KDEBUG_PEEK_SPACE_MMIO ? "m" : "k", addr, width, len);
    crc = crc32_core_update(CRC32_INIT, buf, len);
    for (off = 0; off < len; off += n) {
        n = len - off;
        if (n > KPEEK_ROW_BYTES) n = KPEEK_ROW_BYTES;
        kpeek_format_row(line, addr + off, buf + off, n);
        g_api->kprintf(ATTR_WHITE, "%s", line);
    }
    g_api->kprintf(ATTR_GREEN, "peek: ok len=%08X crc32=%08X frames=%u\n",
                   len, crc32_core_final(crc), frames);
    free(buf);
    return SH_STATUS_OK;
}
