/* ======================================================================== */
/*  KBD_ECHO.C — CUI キー入力エコー (票 K7 の受入 I1 / I2 / I3 の観測手段)    */
/*                                                                          */
/*  api->kbd_getchar() を回し、届いた 1 バイトを 1 行ずつ表示する。           */
/*    got 0x41 'A'      印字可能なバイト                                     */
/*    got 0x0A '\n'     改行                                                 */
/*    got 0x1B          印字できないバイト (コードのみ)                       */
/*  10 バイトごとに count=N を挟み、取りこぼしの有無を数で確かめられる。       */
/*  UTF-8 の多バイト文字は再構成せず、バイトのまま並べて出す。                 */
/*  'q' を受け取ったら bye を出して 0 で終了する。                            */
/*                                                                          */
/*  注意: CUI (rshell) 経路の kbd_getchar は無入力が続くとタイムアウトして     */
/*  ' ' (0x20) を返す。rshell 越しに走らせると打鍵していなくても              */
/*  got 0x20 ' ' が周期的に出る — 実キーボード / GUI 経路で判定すること。      */
/* ======================================================================== */

#include "os32api.h"

int main(int argc, char **argv, KernelAPI *api)
{
    int ch;
    int b;
    int count;

    (void)argc;
    (void)argv;

    api->kprintf(ATTR_WHITE, "kbd_echo: type keys, 'q' to quit\n");

    count = 0;

    for (;;) {
        ch = api->kbd_getchar();
        if (ch < 0) continue;           /* 入力なし扱い — 何も出さない */
        b = ch & 0xFF;

        if (b == '\n') {
            api->kprintf(ATTR_WHITE, "got 0x%02X '\\n'\n", b);
        } else if (b >= 0x20 && b <= 0x7E) {
            api->kprintf(ATTR_WHITE, "got 0x%02X '%c'\n", b, b);
        } else {
            api->kprintf(ATTR_WHITE, "got 0x%02X\n", b);
        }

        count++;
        if ((count % 10) == 0) {
            api->kprintf(ATTR_CYAN, "count=%d\n", count);
        }

        if (b == 'q') {
            api->kprintf(ATTR_WHITE, "bye\n");
            return 0;
        }
    }
}
