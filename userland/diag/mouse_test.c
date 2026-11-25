/* ======================================================================== */
/*  MOUSE_TEST.C — マウスドライバ動作テスト                                  */
/*                                                                          */
/*  TVRAMにマウス座標・ボタン状態・モードをリアルタイム表示する。             */
/*  ESCキーで終了。                                                          */
/* ======================================================================== */

#include "os32_kapi_shared.h"

static KernelAPI *api;

/* 数値→文字列変換 (符号付き, 右寄せ固定幅) */
static void itoa_pad(int val, char *buf, int width)
{
    int neg = 0;
    int i;
    unsigned int uv;
    char tmp[12];
    int len = 0;

    if (val < 0) { neg = 1; uv = (unsigned int)(-val); } else { uv = (unsigned int)val; }
    if (uv == 0) { tmp[len++] = '0'; }
    while (uv > 0) { tmp[len++] = '0' + (uv % 10); uv /= 10; }
    if (neg) tmp[len++] = '-';

    /* 右寄せ */
    for (i = 0; i < width - len; i++) buf[i] = ' ';
    for (i = 0; i < len; i++) buf[width - len + i] = tmp[len - 1 - i];
    buf[width] = '\0';
}

/* TVRAM文字列出力 */
static void tv_print(int x, int y, const char *s, u8 attr)
{
    while (*s) {
        api->tvram_putchar_at(x, y, *s, attr);
        s++;
        x++;
    }
}

/* ボタン状態を文字列化 */
static void btn_str(u8 btn, char *buf)
{
    buf[0] = (btn & MOUSE_BTN_LEFT)   ? 'L' : '.';
    buf[1] = (btn & MOUSE_BTN_MIDDLE) ? 'M' : '.';
    buf[2] = (btn & MOUSE_BTN_RIGHT)  ? 'R' : '.';
    buf[3] = '\0';
}

int main(int argc, char **argv, KernelAPI *k)
{
    MouseInfo mi;
    char buf[16];
    u32 frame = 0;

    api = k;

    api->tvram_clear();

    /* ヘッダ表示 */
    tv_print(0, 0, "=== Mouse Driver Test ===", ATTR_CYAN);
    tv_print(0, 2, "Mode:", ATTR_WHITE);
    tv_print(0, 3, "X:", ATTR_WHITE);
    tv_print(0, 4, "Y:", ATTR_WHITE);
    tv_print(0, 5, "dX:", ATTR_WHITE);
    tv_print(0, 6, "dY:", ATTR_WHITE);
    tv_print(0, 7, "Btn:", ATTR_WHITE);
    tv_print(0, 9, "Frame:", ATTR_WHITE);
    tv_print(0, 11, "Cursor: reverse char under mouse", ATTR_GREEN);
    tv_print(0, 24, "ESC to exit", ATTR_YELLOW);

    if (!api->mouse_available()) {
        tv_print(6, 2, "NOT AVAILABLE", ATTR_RED);
        api->kbd_getchar();
        return 1;
    }

    /* テキストモードカーソルを有効化 */
    api->mouse_cursor_set_mode(MOUSE_CURSOR_TEXT);

    for (;;) {
        int key;

        key = api->kbd_trygetkey();
        if (key == 0x1B) break;

        api->mouse_poll(&mi);

        /* 画面更新前にカーソル消去 */
        api->mouse_cursor_hide();

        /* モード表示 */
        switch (mi.mode) {
        case 1: tv_print(6, 2, "BUS (IRQ13)   ", ATTR_GREEN); break;
        case 2: tv_print(6, 2, "SEAMLESS(NP2W)", ATTR_GREEN); break;
        default: tv_print(6, 2, "NONE          ", ATTR_RED); break;
        }

        /* 座標・ボタン表示 */
        itoa_pad(mi.x, buf, 5); tv_print(6, 3, buf, ATTR_WHITE);
        itoa_pad(mi.y, buf, 5); tv_print(6, 4, buf, ATTR_WHITE);
        itoa_pad(mi.dx, buf, 5); tv_print(6, 5, buf, ATTR_WHITE);
        itoa_pad(mi.dy, buf, 5); tv_print(6, 6, buf, ATTR_WHITE);
        btn_str(mi.buttons, buf);
        tv_print(6, 7, buf, (mi.buttons ? ATTR_RED : ATTR_WHITE));
        itoa_pad((int)(frame & 0x7FFFFFFF), buf, 8);
        tv_print(7, 9, buf, ATTR_WHITE);

        /* 画面更新後にカーソル表示 */
        api->mouse_cursor_show();

        frame++;

        /* CPU負荷軽減 (~50fps) */
        {
            u32 next = api->get_tick() + 2;
            while (api->get_tick() < next) {
                api->sys_halt();
            }
        }
    }

    /* 終了: カーソルモード解除 */
    api->mouse_cursor_set_mode(MOUSE_CURSOR_NONE);
    api->tvram_clear();
    return 0;
}
