#include <stdio.h>
#include <string.h>
#include "os32api.h"
#include "libos32gfx.h"

static KernelAPI *kapi = NULL;
static int log_fd = -1;


/* PC-98 テキスト属性色 (os32_kapi_shared.h の定数を使用) */
#define COL_WHITE   ATTR_WHITE    /* 0xE1: 白 */
#define COL_GREEN   ATTR_GREEN    /* 0x81: 緑 */
#define COL_RED     ATTR_RED      /* 0x41: 赤 */
#define COL_CYAN    ATTR_CYAN     /* 0xA1: 水色 */

static void wait_ms(int ms) {
    u32 start = kapi->get_tick();
    /* PC-98 PIT timer 100Hz -> 10 ticks / 100ms */
    u32 wait_ticks = ms / 10;
    while (kapi->get_tick() - start < wait_ticks) {
        /* busy wait */
    }
}

static void wait_5sec(void) {
    kapi->kprintf(COL_WHITE, "Waiting 5 seconds...\r\n");
    wait_ms(5000);
}

static void log_result(const char *test_name, u32 ticks, const char *desc) {
    char buf[128];
    int len;
    
    kapi->kprintf(COL_GREEN, "%s: %u ticks (%s)\r\n", test_name, ticks, desc);
    
    if (log_fd >= 0) {
        len = sprintf(buf, "%s: %lu ticks (%s)\r\n", test_name, ticks, desc);
        kapi->sys_write(log_fd, buf, len);
    }
}

static void cleanup_gfx(void) {
    /* 描画結果を表示するため一時待機 */
    wait_ms(3000);

    /* GFXプレーンをクリアしてシャットダウン */
    gfx_clear(0);
    gfx_present();
    libos32gfx_shutdown();

    /* テキストVRAMを復旧 */
    kapi->tvram_clear();
}

static void test_memory(void) {
    u32 start, end;
    int i, j;
    void *ptrs[10];

    kapi->kprintf(COL_WHITE, "--- Memory & CPU Test ---\r\n");
    start = kapi->get_tick();
    for (i = 0; i < 100; i++) {
        for (j = 0; j < 10; j++) {
            ptrs[j] = kapi->mem_alloc(4096);
            if (ptrs[j]) {
                memset(ptrs[j], 0xAA, 4096);
            }
        }
        for (j = 0; j < 10; j++) {
            if (ptrs[j]) {
                kapi->mem_free(ptrs[j]);
            }
        }
    }
    end = kapi->get_tick();
    log_result("Memory Test Done", end - start, "100 * 10 * 4KB alloc/free/memset");
}

static void test_gfx(void) {
    u32 start, end;
    int i;

    kapi->kprintf(COL_WHITE, "--- Graphics (GFX) Test ---\r\n");

    /* GFXサブシステム初期化 (GDC START, パレット, バックバッファクリア) */
    libos32gfx_init(kapi);

    start = kapi->get_tick();
    gfx_clear(0);
    for (i = 0; i < 500; i++) {
        gfx_line(i % 640, 0, 639 - (i % 640), 399, (i % 15) + 1);
        gfx_fill_rect((i * 7) % 600, (i * 13) % 350, 40, 40, (i % 15) + 1);
    }
    gfx_present();
    
    /* GFX出力のテストとしてスクリーンショットを書き出し */
    /* kapi->gfx_screenshot("0:/GFX.VDP"); */
    
    end = kapi->get_tick();

    cleanup_gfx();

    log_result("Graphics Test Done", end - start, "500 lines & rects & Screenshot");
}

static void test_kcg(void) {
    u32 start, end;
    int y;

    kapi->kprintf(COL_WHITE, "--- KCG Text Rendering Test ---\r\n");

    /* KCGテストにもGFX初期化が必要 (バックバッファに描画する) */
    libos32gfx_init(kapi);
    kapi->kcg_init();

    start = kapi->get_tick();
    gfx_clear(0);
    for (y = 0; y < 400; y += 16) {
        kcg_draw_utf8(0, y, "OS32 Benchmark KCG Rendering Test - ABC", 7, 0);
    }
    gfx_present();
    end = kapi->get_tick();

    cleanup_gfx();

    log_result("KCG Test Done", end - start, "Fullscreen text");
}

static void test_vfs(void) {
    u32 start, end;
    int fd;
    int i;
    char buf[128];
    OS32_Stat st;

    kapi->kprintf(COL_WHITE, "--- VFS (File System) Test ---\r\n");
    start = kapi->get_tick();

    kapi->sys_unlink("0:/bench.tmp");

    fd = kapi->sys_open("0:/bench.tmp", KAPI_O_CREAT | KAPI_O_WRONLY);
    if (fd >= 0) {
        memset(buf, 'X', sizeof(buf));
        for (i = 0; i < 50; i++) {
            kapi->sys_write(fd, buf, sizeof(buf));
        }
        kapi->sys_close(fd);
    } else {
        kapi->kprintf(COL_RED, "Failed to create file 0:/bench.tmp\r\n");
    }

    kapi->sys_stat("0:/bench.tmp", &st);

    fd = kapi->sys_open("0:/bench.tmp", KAPI_O_RDONLY);
    if (fd >= 0) {
        for (i = 0; i < 50; i++) {
            kapi->sys_read(fd, buf, sizeof(buf));
        }
        kapi->sys_close(fd);
    } else {
        kapi->kprintf(COL_RED, "Failed to open file for read\r\n");
    }

    kapi->sys_unlink("0:/bench.tmp");

    end = kapi->get_tick();
    log_result("VFS Test Done", end - start, "Create, Write, Read, Unlink");
}

static void test_fm(void) {
    u32 start, end;

    kapi->kprintf(COL_WHITE, "--- FM Sound Test ---\r\n");
    start = kapi->get_tick();
    kapi->fm_play_mml("t120l4cdefg a b >c<");
    end = kapi->get_tick();
    log_result("FM Sound Init Done", end - start, "MML processing");
}

int main(int argc, char *argv[], KernelAPI *api) {
    int i;
    void (*tests[])(void) = {
        test_memory,
        test_gfx,
        test_kcg,
        test_vfs,
        test_fm,
        NULL
    };

    if (!api) {
        return -1;
    }
    kapi = api;
    
    log_fd = kapi->sys_open("0:/bench.log", KAPI_O_CREAT | KAPI_O_WRONLY | KAPI_O_TRUNC);
    
    api->kprintf(COL_WHITE, "OS32 Benchmark Started.\r\n");
    
    for (i = 0; tests[i] != NULL; i++) {
        if (i > 0) {
            wait_5sec();
        }
        tests[i]();
    }
    
    if (log_fd >= 0) {
        kapi->sys_close(log_fd);
    }
    
    api->kprintf(COL_CYAN, "\r\nAll benchmarks completed.\r\n");
    
    return 0;
}
