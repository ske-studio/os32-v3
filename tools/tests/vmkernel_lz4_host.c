/* ======================================================================== */
/*  vmkernel_lz4_host.c -- vmkernel.lz4 の展開側 3 実装を同じ入力で回す      */
/*                                                                          */
/*  tools/tests/test_vmkernel_lz4.py が組む。**32bit の静的 ELF** で、libc  */
/*  を使わず int 0x80 で標準入出力だけを使う。ASM デコーダ                  */
/*  (boot/loader_fat_new.asm の pm_lz4_decode) を 32bit のまま呼ぶため。    */
/*                                                                          */
/*  標準入力: u32 mode, u32 csz, u32 raw, 圧縮データ csz バイト (LE)        */
/*    mode 0 = boot/lz4_mini.c の boot_lz4_decode (HDD ローダ)              */
/*    mode 1 = lib/lz4.c の lz4_decode (lz4 コマンド)                       */
/*    mode 2 = pm_lz4_decode (FD ローダ)                                    */
/*  展開先の容量はローダと同じく raw ちょうど。後ろに番兵を置き、           */
/*  書き越したら終了コード 3。                                              */
/*  標準出力: s32 戻り値, 展開したバイト列 (戻り値 > 0 のときだけ)           */
/*  終了コード: 0 = 回した (合否は Python 側が中身で決める)、2 = 入力不正   */
/* ======================================================================== */

typedef unsigned char u8;

int boot_lz4_decode(const u8 *src, int compressed_size, u8 *dst, int cap);
int lz4_decode(const u8 *src, int compressed_size, u8 *dst, int cap);
int pm_lz4_decode(const u8 *src, int compressed_size, u8 *dst, int cap);

#define IN_MAX   (4u * 1024u * 1024u)
#define OUT_MAX  (4u * 1024u * 1024u)
#define GUARD    4096u

static u8 in_buf[IN_MAX];
static u8 out_buf[OUT_MAX + GUARD];

/* -O で memcpy / memset を呼ばれても組めるように置いておく */
void *memcpy(void *d, const void *s, unsigned int n)
{
    u8 *dp = (u8 *)d;
    const u8 *sp = (const u8 *)s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memset(void *d, int c, unsigned int n)
{
    u8 *dp = (u8 *)d;
    while (n--) *dp++ = (u8)c;
    return d;
}

static int sys3(int n, int a, int b, int c)
{
    int r;
    __asm__ volatile ("int $0x80"
                      : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static void sys_exit(int code)
{
    sys3(1, code, 0, 0);
    for (;;) { }
}

static unsigned int rd32(const u8 *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void write_all(const u8 *p, unsigned int n)
{
    while (n > 0) {
        int w = sys3(4, 1, (int)p, (int)n);
        if (w <= 0) sys_exit(4);
        p += w;
        n -= (unsigned int)w;
    }
}

void harness_main(void)
{
    unsigned int got = 0, mode, csz, raw, i;
    int r;
    u8 hdr[4];

    for (;;) {
        int n = sys3(3, 0, (int)(in_buf + got), (int)(IN_MAX - got));
        if (n < 0) sys_exit(2);
        if (n == 0) break;
        got += (unsigned int)n;
        if (got >= IN_MAX) sys_exit(2);
    }
    if (got < 12) sys_exit(2);
    mode = rd32(in_buf);
    csz  = rd32(in_buf + 4);
    raw  = rd32(in_buf + 8);
    if (csz != got - 12 || raw > OUT_MAX) sys_exit(2);

    for (i = 0; i < OUT_MAX + GUARD; i++) out_buf[i] = 0xCC;

    if (mode == 0)
        r = boot_lz4_decode(in_buf + 12, (int)csz, out_buf, (int)raw);
    else if (mode == 1)
        r = lz4_decode(in_buf + 12, (int)csz, out_buf, (int)raw);
    else if (mode == 2)
        r = pm_lz4_decode(in_buf + 12, (int)csz, out_buf, (int)raw);
    else
        sys_exit(2);

    for (i = raw; i < raw + GUARD; i++)
        if (out_buf[i] != 0xCC) sys_exit(3);

    hdr[0] = (u8)r; hdr[1] = (u8)(r >> 8);
    hdr[2] = (u8)(r >> 16); hdr[3] = (u8)(r >> 24);
    write_all(hdr, 4);
    if (r > 0) write_all(out_buf, (unsigned int)r);
    sys_exit(0);
}

/* 入口。スタックを 16 バイトに揃えて C へ (DF はカーネルが 0 で渡す) */
__asm__ (".globl _start\n"
         "_start:\n"
         "    andl $-16, %esp\n"
         "    call harness_main\n"
         "    hlt\n");
