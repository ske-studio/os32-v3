/* ========================================================================
 *  ext2_mini_host.c — ローダの ext2 読み手の上限 (票 TASK_HDD_INSTALL N8)
 *
 *  実行:   python3 -B tools/tests/test_hdd_stage2.py
 *  記録:   tools/tests/hdd_stage2_tdd.md
 *
 *  実物 boot/ext2_mini.c をそのまま取り込み、boot_read_sector_asm だけを
 *  贋物 (mke2fs + debugfs で作った像をメモリに読んで返す) にする。
 *  見るもの: MAX_IMAGE_SIZE (508KiB) ちょうどは読み、1 バイトでも超える
 *  ファイルは**切り詰めずに** EXT2M_ERR_TOO_BIG を返して、読み先に 1 バイトも
 *  書かない (以前は上限で切り詰めた途中までのイメージを展開した)。
 *
 *    ext2-mini-host <像> <パス> <期待: 大きさ | -2>
 *
 *  ローダと同じ ILP32 (u32 = unsigned long = 4 B)。ホスト -m32、-nostdlib。
 * ======================================================================== */

#include "boot_defs.h"

/* ---- 入出力 (int 0x80) ---- */
static int sys3(int nr, long a, long b, long c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
static void die(int code) { (void)sys3(1, code, 0, 0); for (;;) { } }
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void say(const char *s) { (void)sys3(4, 2, (long)s, (long)slen(s)); }
static void fail(const char *what) { say("FAIL "); say(what); say("\n"); die(1); }

/* ---- 像 (最大 4MB) ---- */
#define IMG_MAX (4u * 1024u * 1024u)
static u8 img[IMG_MAX];
static u32 img_len;

void boot_read_sector_asm(u32 lba, u8 *buf)
{
    u32 i;
    if ((lba + 1u) * 512u > img_len) fail("read past the image");
    for (i = 0; i < 512u; i++) buf[i] = img[lba * 512u + i];
}
void boot_print_asm(u32 tvram_addr, const char *msg) { (void)tvram_addr; (void)msg; }
u8 param_da, param_heads, param_spt;

#include "ext2_mini.c"

/* 読み先: 上限 + 8KiB の余白を印で埋め、範囲の外へ書いていないかを見る */
#define GUARD 8192u
static u8 dst[MAX_IMAGE_SIZE + GUARD];

static long atol_s(const char *s)
{
    long v = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

static u8 pat(u32 i) { return (u8)((i * 7u + 3u) & 0xFFu); }

int os32_main(int argc, char **argv)
{
    int fd, n, rc;
    u32 ino, i;
    long want;

    if (argc != 4) return 2;
    fd = sys3(5, (long)argv[1], 0, 0);
    if (fd < 0) fail("open image");
    while ((n = sys3(3, fd, (long)(img + img_len), (long)(IMG_MAX - img_len))) > 0)
        img_len += (u32)n;
    want = atol_s(argv[3]);

    if (ext2m_init(0) != 0) fail("ext2m_init");
    ino = ext2m_lookup(argv[2]);
    if (ino == 0) fail("lookup");
    for (i = 0; i < sizeof(dst); i++) dst[i] = 0xEE;

    rc = ext2m_read_file(ino, dst, MAX_IMAGE_SIZE);
    if (want < 0) {
        if (rc != (int)want) fail("expected an error (not truncated)");
        for (i = 0; i < sizeof(dst); i++)
            if (dst[i] != 0xEE) fail("wrote into the buffer although the file is too large");
    } else {
        if (rc != (int)want) fail("size");
        for (i = 0; i < (u32)rc; i++) if (dst[i] != pat(i)) fail("content");
        for (i = (u32)rc; i < sizeof(dst); i++) if (dst[i] != 0xEE) fail("wrote past the file");
    }
    say("ext2_mini: PASS\n");
    return 0;
}

void start_c(long *sp);

__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  movl %esp, %eax\n"
        "  andl $-16, %esp\n"
        "  pushl %eax\n"
        "  call start_c\n"
        "  hlt\n");

void start_c(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)&sp[1];
    die(os32_main(argc, argv));
}
