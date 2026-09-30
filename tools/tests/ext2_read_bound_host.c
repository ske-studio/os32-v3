/* ========================================================================
 *  ext2_read_bound_host.c — ext2_read_file() が max_size を 1 バイトも
 *  越えないことの回帰試験 (K5b-K 差し戻し 2026-09-11 の根本原因)
 *
 *  対象票: docs/archive/gui_v13/TASK_K5B_kernel.md (差し戻し)
 *  実行:   python3 -B tools/tests/test_ext2_read_bound.py
 *  記録:   tools/tests/k5b_kernel_tdd.md 節 R
 *
 *  ext2_read_block() は to_copy に関係なく **必ず 1KB 書く**。
 *  ext2_read_file() は端数ブロックでもそれを宛先へ直接読んでいたため、
 *  max_size が 1KB の倍数でない呼び出しは最大 1023 バイト溢れていた。
 *  K5b-K が exec のヘッダ先読みを 108 バイトのカーネル .bss バッファに
 *  変えた瞬間に顕在化し、隣の resolved[] (解決済みパス) ごと潰して
 *  shell.bin の読み込みが NOT_FOUND になった (= 実機の
 *  「FATAL: shell.bin load failed」)。
 *
 *  実物の fs/ext2_file.c をそのまま取り込み、境界 (ブロック I/O、inode、
 *  bmap) だけを差し替える。実デバイス・実イメージには一切触れない。
 *  C89 ([C1])。
 * ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext2_priv.h"
#include "os32_kapi_shared.h"   /* OS32X_HDR_V2_SIZE */

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)

/* ---- カーネル側の共有バッファ (実物と同じ実体を用意する) ---- */
u8 ext2_g_blk[EXT2_BLOCK_SIZE];
u8 ext2_g_aux[EXT2_BLOCK_SIZE];
u8 ext2_g_dat[EXT2_BLOCK_SIZE];

/* ---- kstring / kprintf の境界 ---- */
void *kmemcpy(void *dst, const void *src, u32 n) { return memcpy(dst, src, n); }
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }
u32   kstrlen(const char *s) { return (u32)strlen(s); }
int   kstrncmp(const char *a, const char *b, u32 n)
{
    return strncmp(a, b, n);
}
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

/* ---- 偽ファイルシステム ---------------------------------------------
 *  1 ファイルだけを持つ。ブロック番号 b の中身は (b*7 + i) の 1KB。      */
static u32 g_file_size = 0;

static u8 block_byte(u32 phys, u32 i) { return (u8)(phys * 7u + i); }

int ext2_read_block(Ext2Ctx *ctx, u32 block_num, void *buf)
{
    u32 i;
    u8 *d = (u8 *)buf;
    (void)ctx;
    /* 実物と同じ「必ず 1KB 書く」振る舞い (512B セクタ 2 本ぶん)。 */
    for (i = 0; i < EXT2_BLOCK_SIZE; i++) d[i] = block_byte(block_num, i);
    return 0;
}

int ext2_read_inode(Ext2Ctx *ctx, u32 ino, Ext2Inode *inode)
{
    (void)ctx; (void)ino;
    memset(inode, 0, sizeof(*inode));
    inode->size = g_file_size;
    inode->mode = EXT2_S_IFREG;
    inode->links_count = 1;
    return 0;
}

/* 票 B8 で「未割当」と「読めなかった」を分ける形になった (fs/ext2_priv.h)。
 * この試験は I/O 失敗を注入しないので、常に EXT2_OK を返す。 */
int ext2_bmap(Ext2Ctx *ctx, const Ext2Inode *inode, u32 file_block,
              u32 *out_phys)
{
    u32 blocks = (inode->size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    (void)ctx;
    *out_phys = (file_block >= blocks) ? 0 : (100 + file_block);
    return EXT2_OK;                   /* 物理ブロック 0 は「穴」 */
}

/* 票 B8 往復 5: ファイルの中身の I/O はデータ用の関数を通るようになった
 * (fs/ext2_priv.h)。読み取りは上と同じ「必ず 1KB 書く」贋物へ回す —
 * この試験が見ているのはまさに端数ブロックのデータ読み取りなので。 */
int ext2_read_data_block(Ext2Ctx *ctx, u32 block_num, void *buf)
{ return ext2_read_block(ctx, block_num, buf); }

/* ---- ext2_file.c が呼ぶ書き込み側 (この試験では使わない) ---- */
int ext2_write_block(Ext2Ctx *c, u32 b, const void *p)
{ (void)c; (void)b; (void)p; return 0; }
int ext2_write_data_block(Ext2Ctx *c, u32 b, const void *p)
{ (void)c; (void)b; (void)p; return 0; }
/* 票 B8 往復 5: 書き込み系の入口の拒否 (エラー状態)。この試験では常に書ける */
int ext2_check_writable(Ext2Ctx *c) { (void)c; return EXT2_OK; }
int ext2_write_inode(Ext2Ctx *c, u32 i, const Ext2Inode *n)
{ (void)c; (void)i; (void)n; return 0; }
int ext2_alloc_block(Ext2Ctx *c) { (void)c; return 0; }
/* 票 B8 往復 3: 解放は戻り値を持ち、順序付きの 2 関数になった
 * (fs/ext2_priv.h)。この試験は解放経路を動かさないので常に成功。 */
int ext2_free_block(Ext2Ctx *c, u32 b) { (void)c; (void)b; return EXT2_OK; }
int ext2_alloc_inode(Ext2Ctx *c) { (void)c; return 0; }
int ext2_free_inode(Ext2Ctx *c, u32 i) { (void)c; (void)i; return EXT2_OK; }
int ext2_truncate_blocks(Ext2Ctx *c, u32 i, Ext2Inode *n, int *leaked)
{ (void)c; (void)i; (void)n; if (leaked) *leaked = 0; return EXT2_OK; }
int ext2_release_blocks(Ext2Ctx *c, const u32 *b)
{ (void)c; (void)b; return EXT2_OK; }
int ext2_bmap_set(Ext2Ctx *c, Ext2Inode *n, u32 f, u32 p)
{ (void)c; (void)n; (void)f; (void)p; return 0; }
int ext2_add_entry(Ext2Ctx *c, u32 d, const char *n, u32 i, u8 t)
{ (void)c; (void)d; (void)n; (void)i; (void)t; return 0; }
int ext2_delete_entry(Ext2Ctx *c, u32 d, const char *n)
{ (void)c; (void)d; (void)n; return 0; }
/* 票 TASK_EXT2_EMPTY_NAME: create / unlink の入口の名前検査 (実物は ext2_dir.c) */
int ext2_name_check(const char *n) { (void)n; return EXT2_OK; }
int ext2_find_entry(Ext2Ctx *c, u32 d, const char *n, u32 *o, u8 *t)
{ (void)c; (void)d; (void)n; (void)o; (void)t; return EXT2_ERR_NOTFOUND; }
u32 ext2_current_time(void) { return 0; }
int ext2_sync(Ext2Ctx *c) { (void)c; return 0; }

#include "../../fs/ext2_file.c"

/* ======================================================================== */
/*  試験本体                                                                */
/* ======================================================================== */

#define ARENA_PAD   2048          /* 溢れを受け止める番犬領域 */
#define GUARD_BYTE  0xEE

static Ext2Ctx g_ctx;

static void ctx_reset(u32 size)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.mounted = 1;
    g_file_size = size;
}

/* 番犬付きの読み込み。戻り値と「書かれた最後のバイトの次」を検査する。 */
static int read_guarded(u32 file_size, u32 max_size, u32 expect_written)
{
    static u8 arena[ARENA_PAD * 4];
    u8 *dst = arena + ARENA_PAD;
    u32 i;
    int n;

    memset(arena, GUARD_BYTE, sizeof(arena));
    ctx_reset(file_size);
    n = ext2_read_file(&g_ctx, EXT2_ROOT_INO, dst, max_size);

    CHECK(n == (int)expect_written);

    /* 手前を踏んでいない */
    for (i = 0; i < ARENA_PAD; i++) CHECK(arena[i] == GUARD_BYTE);
    /* **返した長さより先を 1 バイトも書いていない** — 元の欠陥はここ */
    for (i = ARENA_PAD + expect_written; i < sizeof(arena); i++) {
        if (arena[i] != GUARD_BYTE) {
            fprintf(stderr,
                    "FAIL overrun: max_size=%lu wrote past +%lu (offset +%lu)\n",
                    (unsigned long)max_size, (unsigned long)expect_written,
                    (unsigned long)(i - ARENA_PAD));
            exit(1);
        }
    }
    /* 中身は正しい (端数ブロックの中継で壊していない) */
    for (i = 0; i < expect_written; i++) {
        CHECK(dst[i] == block_byte(100 + i / EXT2_BLOCK_SIZE,
                                   i % EXT2_BLOCK_SIZE));
    }
    return n;
}

/* R1: max_size がブロックの倍数でない (exec のヘッダ先読みと同じ形) */
static void case_bound_header(void)
{
    read_guarded(64u * 1024u, OS32X_HDR_V2_SIZE + 64, OS32X_HDR_V2_SIZE + 64);
    printf("  R1 header-sized read stays inside max_size\n");
}

/* R2: ファイル末尾が端数ブロック (max_size は十分大きい) */
static void case_bound_tail(void)
{
    read_guarded(1500, 64u * 1024u, 1500);
    read_guarded(1, 64u * 1024u, 1);
    read_guarded(EXT2_BLOCK_SIZE + 1, 64u * 1024u, EXT2_BLOCK_SIZE + 1);
    printf("  R2 partial tail block stays inside the file size\n");
}

/* R3: ちょうどブロック境界 (従来経路が変わっていないこと) */
static void case_bound_aligned(void)
{
    read_guarded(4u * EXT2_BLOCK_SIZE, 4u * EXT2_BLOCK_SIZE,
                 4u * EXT2_BLOCK_SIZE);
    read_guarded(4u * EXT2_BLOCK_SIZE, 2u * EXT2_BLOCK_SIZE,
                 2u * EXT2_BLOCK_SIZE);
    printf("  R3 block-aligned reads unchanged\n");
}

/* R4: 実機で壊れた並び — 108 バイトのヘッダバッファの直後に解決済みパス。
 *     exec/exec.c の hdrbuf.2 / resolved.3 が .bss で隣接していた形。 */
static void case_exec_bss_neighbour(void)
{
    static struct {
        u8   hdrbuf[OS32X_HDR_V2_SIZE + 64];
        char resolved[VFS_MAX_PATH];
    } bss;
    static const char *path = "/sys/shell.bin";
    int n;

    memset(&bss, 0, sizeof(bss));
    strcpy(bss.resolved, path);
    ctx_reset(64u * 1024u);

    n = ext2_read_file(&g_ctx, EXT2_ROOT_INO, bss.hdrbuf, sizeof(bss.hdrbuf));
    CHECK(n == (int)sizeof(bss.hdrbuf));
    /* 解決済みパスが生きている = 続く本体の読み込みが NOT_FOUND にならない */
    if (strcmp(bss.resolved, path) != 0) {
        fprintf(stderr, "FAIL: resolved[] clobbered: \"%s\"\n", bss.resolved);
        exit(1);
    }
    printf("  R4 exec header pre-read leaves resolved[] intact\n");
}

int main(int argc, char **argv)
{
    const char *sel = (argc > 1) ? argv[1] : "all";

    if (!strcmp(sel, "all") || !strcmp(sel, "bound_header"))
        case_bound_header();
    if (!strcmp(sel, "all") || !strcmp(sel, "bound_tail"))
        case_bound_tail();
    if (!strcmp(sel, "all") || !strcmp(sel, "bound_aligned"))
        case_bound_aligned();
    if (!strcmp(sel, "all") || !strcmp(sel, "exec_bss_neighbour"))
        case_exec_bss_neighbour();

    printf("ext2_read_bound: PASS (%s)\n", sel);
    return 0;
}
