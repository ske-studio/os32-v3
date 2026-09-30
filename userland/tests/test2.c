#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
/* ======================================================================== */
/*  TEST2.C — KernelAPI v2 テストプログラム                                  */
/*                                                                          */
/*  新APIの動作確認:                                                         */
/*    - file_write / file_read (ファイルI/O)                                */
/*    - mem_alloc / mem_free   (メモリ管理)                                  */
/*    - get_tick               (タイマ)                                      */
/*    - print_dec              (数値出力)                                    */
/*                                                                          */
/*  ビルド:                                                                  */
/*    wcc386 -s -zl -zls -mf -3s test2.c -fo=test2.obj                      */
/*    wlink FORMAT RAW BIN NAME test2.bin FILE test2.obj                     */
/*          OPTION NODEFAULTLIBS, START=main, OFFSET=0x400000                */
/* ======================================================================== */





/* KernelAPI v2 構造体 (exec.h と同じレイアウト)
 * 注意: 関数ポインタは __cdecl で宣言すること。
 * カーネル側のラッパーが cdecl で定義されているため。 */
#include "os32api.h"

#include "rt/testresult.h"

/* ヘルパー関数の前方宣言 (mainをバイナリ先頭に配置するため) */
static int my_memcmp(const void *a, const void *b, u32 n);
static void check(int cond, const char *label);

static KernelAPI *g_api;
static int g_total;
static int g_passed;

#define GREEN  0xA2
#define RED    0x42
#define CYAN   0x62
#define WHITE  0xE1

/* main はバイナリの先頭に配置される必要がある */
int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)argv;

    g_api = api;
    g_total = 0;
    g_passed = 0;

    api->kprintf(CYAN, "%s", "=== KernelAPI v2 Test ===\n");

    /* --- バージョンチェック --- */
    api->kprintf(WHITE, "%s", "  version: ");
    api->kprintf(WHITE, "%d", api->version);
    api->kprintf(WHITE, "%s", "\n");
    if (api->version < 2) {
        return os32_test_summary_skip(api, "test2",
                                      "kernel is older than KAPI v2");
    }

    /* --- タイマテスト --- */
    {
        u32 t1, t2;
        api->kprintf(WHITE, "%s", "  get_tick: ");
        t1 = api->get_tick();
        api->kprintf(GREEN, "%d", t1);
        /* 少し時間消費 */
        { volatile u32 i; for (i = 0; i < 100000; i++); }
        t2 = api->get_tick();
        api->kprintf(WHITE, "%s", " -> ");
        api->kprintf(GREEN, "%d", t2);
        api->kprintf(WHITE, "%s", "\n");
        check(t2 >= t1, "get_tick does not go backwards");
    }

    /* --- メモリテスト --- */
    {
        char *buf = (char *)api->mem_alloc(256);
        api->kprintf(WHITE, "%s", "  mem_alloc(256): ");
        api->kprintf(WHITE, "%d", (u32)buf);
        api->kprintf(WHITE, "%s", "\n");
        check(buf != (char *)0, "mem_alloc(256)");
        if (buf) {
            /* 書き込みテスト */
            buf[0] = 'H'; buf[1] = 'i'; buf[2] = 0;
            check(buf[0] == 'H' && buf[1] == 'i' && buf[2] == 0,
                  "the allocated block keeps what was written");
            api->mem_free(buf);
        } else {
            check(0, "the allocated block keeps what was written");
        }
    }

    /* --- ファイルI/Oテスト --- */
    {
        const char *test_data = "API-TEST-OK";
        char read_buf[64];
        int wr, rd, t_fd, r_fd;

        api->kprintf(WHITE, "%s", "  file_write: ");
        t_fd = api->sys_open("/api_test.txt",
                             KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
        if (t_fd >= 0) {
            wr = api->sys_write(t_fd, test_data, 11);
            api->sys_close(t_fd);
        } else {
            wr = -1;
        }
        api->kprintf(WHITE, "%d", (u32)(wr < 0 ? -wr : wr));
        api->kprintf(WHITE, "%s", "\n");
        /* sys_write は**書けたバイト数**を返す (fs/vfs_fd.c: vfs_write_fd)。
         * 2026-09-17 まで `wr == 0` を成功と読んでいたので、11 バイト書けた
         * ときでも FAIL を出していた。短く書けたのも成功に数えない。 */
        check(wr == 11, "sys_write wrote all 11 bytes");

        api->kprintf(WHITE, "%s", "  file_read:  ");
        r_fd = api->sys_open("/api_test.txt", KAPI_O_RDONLY);
        if (r_fd >= 0) {
            rd = api->sys_read(r_fd, read_buf, 63);
            api->sys_close(r_fd);
        } else {
            rd = -1;
        }
        api->kprintf(WHITE, "%d", (u32)(rd < 0 ? -rd : rd));
        api->kprintf(WHITE, "%s", "\n");
        check(rd == 11 && my_memcmp(read_buf, test_data, 11) == 0,
              "sys_read reads back the same 11 bytes");
    }

    /* --- 結果 --- */
    return os32_test_summary(api, "test2", g_passed, g_total);
}

static void check(int cond, const char *label)
{
    g_total++;
    if (cond) {
        g_passed++;
        g_api->kprintf(GREEN, "  [OK] %s\n", label);
    } else {
        g_api->kprintf(RED, "  [NG] %s\n", label);
    }
}

/* ======================================================================== */
/*  ヘルパー関数（mainの後に配置）                                          */
/* ======================================================================== */

static int my_memcmp(const void *a, const void *b, u32 n)
{
    const u8 *p = (const u8 *)a;
    const u8 *q = (const u8 *)b;
    u32 i;
    for (i = 0; i < n; i++) {
        if (p[i] != q[i]) return 1;
    }
    return 0;
}
