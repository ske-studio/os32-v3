/* ======================================================================== */
/*  vk32_host.c -- VK32 v2 の検査・展開をローダの実物 2 つで回す            */
/*                                                                          */
/*  tools/tests/test_vk32_crc.py が組む。**32bit の静的 ELF** で、libc を   */
/*  使わず int 0x80 で標準入出力だけを使う (FD ローダの ASM を 32bit の     */
/*  まま呼ぶため。tools/tests/vmkernel_lz4_host.c と同じ作り)。              */
/*                                                                          */
/*  標準入力: u32 mode, u32 size, ファイル size バイト (LE)                 */
/*    mode 0 = boot/vk32_boot.c の vk32_boot (HDD ローダ、lz4_mini と組む)  */
/*    mode 1 = boot/loader_fat_new.asm の pm_vk32_boot (FD ローダ)          */
/*    mode 2 = 同じファイルの fat_chain_check (実モードの手続きを bits 32 で) */
/*             ファイル = u32 開始クラスタ, u32 ファイル長, FAT のバイト列  */
/*             出力は s32 戻り値, u32 クラスタ数 だけ                       */
/*  展開先の窓は [VK32_LOAD_MIN, VK32_LOAD_END) と同じ大きさで 0xCC で埋め、 */
/*  後ろに番兵 4KB を置く。番兵を書き越したら終了コード 3。                 */
/*  標準出力: s32 戻り値, u32 out_crc, 窓の全体 (WINDOW バイト)            */
/*  終了コード: 0 = 回した (合否は Python 側)、2 = 入力不正                */
/* ======================================================================== */

typedef unsigned char u8;
typedef unsigned long u32;

int vk32_boot(const u8 *file, u32 file_size, u8 *window, u32 *out_crc);
int pm_vk32_boot(const u8 *file, u32 file_size, u8 *window, u32 *out_crc);
/* FD ローダの fat_chain_check を cdecl で包んだもの (試験が生成する ASM)。
 * 戻り値 FATCHK_*、99 = 保存するはずのレジスタを壊した */
int fat_check_c(u32 start, u32 size, const u8 *fat, u32 *count);

#define IN_MAX   (2u * 1024u * 1024u)
#define WINDOW   (0x2E8000u - 0x100000u)
#define GUARD    4096u

static u8 in_buf[IN_MAX];
static u8 win[WINDOW + GUARD];

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

static void put32(u8 *h, unsigned int v)
{
    h[0] = (u8)v; h[1] = (u8)(v >> 8); h[2] = (u8)(v >> 16); h[3] = (u8)(v >> 24);
}

void harness_main(void)
{
    unsigned int got = 0, mode, size, i;
    u32 crc = 0xDEADBEEFUL;
    int r;
    u8 hdr[8];

    for (;;) {
        int n = sys3(3, 0, (int)(in_buf + got), (int)(IN_MAX - got));
        if (n < 0) sys_exit(2);
        if (n == 0) break;
        got += (unsigned int)n;
        if (got >= IN_MAX) sys_exit(2);
    }
    if (got < 8) sys_exit(2);
    mode = rd32(in_buf);
    size = rd32(in_buf + 4);
    if (size != got - 8) sys_exit(2);

    if (mode == 2) {
        u32 cnt = 0xDEADBEEFUL;
        if (size < 8) sys_exit(2);
        r = fat_check_c((u32)rd32(in_buf + 8), (u32)rd32(in_buf + 12),
                        in_buf + 16, &cnt);
        put32(hdr, (unsigned int)r);
        put32(hdr + 4, (unsigned int)cnt);
        write_all(hdr, 8);
        sys_exit(0);
    }

    for (i = 0; i < WINDOW + GUARD; i++) win[i] = 0xCC;

    if (mode == 0)
        r = vk32_boot(in_buf + 8, size, win, &crc);
    else if (mode == 1) {
        /* DF=1 で呼ぶ — ASM が自分で cld することを確かめる (C 側は ABI どおり
         * DF=0 を前提にするので mode 0 では立てない)。 */
        __asm__ volatile ("std" ::: "memory");
        r = pm_vk32_boot(in_buf + 8, size, win, &crc);
        __asm__ volatile ("cld" ::: "memory");
    }
    else
        sys_exit(2);

    for (i = WINDOW; i < WINDOW + GUARD; i++)
        if (win[i] != 0xCC) sys_exit(3);

    put32(hdr, (unsigned int)r);
    put32(hdr + 4, (unsigned int)crc);
    write_all(hdr, 8);
    write_all(win, WINDOW);
    sys_exit(0);
}

/* 入口。スタックを 16 バイトに揃えて C へ (DF はカーネルが 0 で渡す) */
__asm__ (".globl _start\n"
         "_start:\n"
         "    andl $-16, %esp\n"
         "    call harness_main\n"
         "    hlt\n");
