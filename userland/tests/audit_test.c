/* CPL3 guest probes for KAPI-AUDIT-FIX. PM drives CTRL+STOP externally.
 * No FORCE_CPL0 flag: use the ordinary SDK startup and int 0x80 path.
 * main must remain the first function. */
#include "os32api.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#define AUDIT_MML_NOTES 1024
#define AUDIT_SENTINEL_CODE 0x5A5A
#define AUDIT_SENTINEL_ATTR 0xA5
#define AUDIT_GUARD 0x19573246UL
#define AUDIT_JIS 0x2422
#define AUDIT_REVERSE_BIT 0x04

int main(int argc, char **argv, KernelAPI *api)
{
    static const int cursor_inputs[] = { -1, INT_MIN, INT_MAX };
    static const int extremes[] = { -1, 3, INT_MIN, INT_MAX };
    int i, j;

    if (argc != 2) {
        api->kprintf(ATTR_RED,
                     "usage: audit_test tvram|cursor|fmch|mml|serial|ime|rshell\n");
        return 1;
    }
    if (strcmp(argv[1], "tvram") == 0) {
        struct cell { u16 code; u8 attr; };
        struct audit_sentinel { u32 before; u16 code; u8 attr; u32 after; } sentinel;
        struct cell *saved;
        int w = 0, h = 0, fails = 0, x, y;
        api->console_get_size(&w, &h);
        if (w < 2 || h < 1 || w > INT_MAX / h ||
            (u32)(w * h) > (u32)-1 / sizeof(*saved)) return 1;
        saved = malloc((u32)(w * h) * sizeof(*saved));
        if (!saved) return 1;
        for (y = 0; y < h; y++) for (x = 0; x < w; x++)
            api->tvram_readchar_at(x, y, &saved[y * w + x].code,
                                  &saved[y * w + x].attr);
        /* Independent axes, both axes, and overflow-sized coordinates.
         * No diagnostics until all cells have been compared. */
        for (i = 0; i < 10; i++) {
            u16 code;
            u8 attr;
            x = i < 4 ? (i == 0 ? -1 : i == 1 ? w : i == 2 ? INT_MIN : INT_MAX) : 0;
            y = i >= 4 && i < 8 ? (i == 4 ? -1 : i == 5 ? h : i == 6 ? INT_MIN : INT_MAX) : 0;
            if (i >= 8) { x = i == 8 ? -1 : INT_MAX; y = x; }
            sentinel.before = sentinel.after = AUDIT_GUARD;
            sentinel.code = AUDIT_SENTINEL_CODE;
            sentinel.attr = AUDIT_SENTINEL_ATTR;
            api->tvram_putchar_at(x, y, 'X', ATTR_RED);
            api->tvram_putkanji_at(x, y, AUDIT_JIS, ATTR_RED);
            api->tvram_readchar_at(x, y, &sentinel.code, &sentinel.attr);
            if (api->tvram_reverse_cell(x, y) != 0 ||
                sentinel.code != AUDIT_SENTINEL_CODE || sentinel.attr != AUDIT_SENTINEL_ATTR ||
                sentinel.before != AUDIT_GUARD || sentinel.after != AUDIT_GUARD) fails++;
            for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
                api->tvram_readchar_at(x, y, &code, &attr);
                if (code != saved[y * w + x].code || attr != saved[y * w + x].attr) fails++;
            }
        }
        /* A two-cell kanji cannot start at the final column. */
        api->tvram_putkanji_at(w - 1, h - 1, AUDIT_JIS, ATTR_RED);
        u16 code;
        u8 attr;
        api->tvram_readchar_at(w - 1, h - 1, &code, &attr);
        if (code != saved[w * h - 1].code || attr != saved[w * h - 1].attr) fails++;
        api->tvram_readchar_at(w - 2, h - 1, &code, &attr);
        if (code != saved[w * h - 2].code || attr != saved[w * h - 2].attr) fails++;
        /* The next valid operation must still work, including reverse. */
        api->tvram_putchar_at(w - 1, h - 1, 'Q', ATTR_WHITE);
        api->tvram_readchar_at(w - 1, h - 1, &code, &attr);
        if (code != 'Q' || attr != ATTR_WHITE ||
            api->tvram_reverse_cell(w - 1, h - 1) != 1) fails++;
        api->tvram_readchar_at(w - 1, h - 1, &code, &attr);
        if (code != 'Q' || attr != (ATTR_WHITE ^ AUDIT_REVERSE_BIT)) fails++;
        free(saved);
        api->kprintf(fails ? ATTR_RED : ATTR_GREEN,
                     "audit_test: tvram %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
        return fails ? 1 : 0;
    }
    if (strcmp(argv[1], "cursor") == 0) {
        int w = 0, h = 0, fails = 0;
        int x, y, after_x, after_y, want_x, want_y;
        api->console_get_size(&w, &h);
        if (w <= 0 || h <= 0) return 1;
        /* Test each axis independently and together, then the bottom right.
         * Capture before printing diagnostics, which also move the cursor. */
        for (i = 0; i < 10; i++) {
            int input_x = i == 9 ? w - 1 : (i % 3 == 1 ? 0 : cursor_inputs[i / 3]);
            int input_y = i == 9 ? h - 1 : (i % 3 == 0 ? 0 : cursor_inputs[i / 3]);
            want_x = input_x < 0 ? 0 : (input_x >= w ? w - 1 : input_x);
            want_y = input_y < 0 ? 0 : (input_y >= h ? h - 1 : input_y);
            api->console_set_cursor(input_x, input_y);
            x = api->console_get_cursor_x();
            y = api->console_get_cursor_y();
            api->shell_putchar('A' + i, ATTR_WHITE);
            after_x = api->console_get_cursor_x();
            after_y = api->console_get_cursor_y();
            if (x != want_x || y != want_y || after_x < 0 || after_x >= w ||
                after_y < 0 || after_y >= h) fails++;
            api->kprintf(ATTR_WHITE,
                         "cursor (%d,%d) -> (%d,%d), after char (%d,%d)\n",
                         input_x, input_y, x, y, after_x, after_y);
        }
        api->kprintf(fails ? ATTR_RED : ATTR_GREEN,
                     "audit_test: cursor %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
        return fails ? 1 : 0;
    }
    if (strcmp(argv[1], "fmch") == 0) {
        /* All five entries return void; returning to the next line proves survival.
         * 3 is invalid as a channel, but a valid tone in the current five-tone table. */
        for (i = 0; i < (int)(sizeof(extremes) / sizeof(extremes[0])); i++) {
            api->fm_note_on(extremes[i], 0);
            api->fm_note_off(extremes[i]);
            api->ssg_tone(extremes[i], 0);
            api->ssg_volume(extremes[i], 0);
            for (j = 0; j < (int)(sizeof(extremes) / sizeof(extremes[0])); j++)
                api->fm_set_tone_num(extremes[i], extremes[j]);
            api->kprintf(ATTR_WHITE, "fmch ch=%d: returned (void APIs)\n", extremes[i]);
            /* Also exercise the tone index with a valid channel. */
            api->fm_set_tone_num(0, extremes[i]);
            api->kprintf(ATTR_WHITE, "fmch tone=%d: returned (void API)\n", extremes[i]);
        }
        api->kprintf(ATTR_GREEN, "audit_test: fmch PASS (survived)\n");
        return 0;
    }
    if (strcmp(argv[1], "rshell") == 0) {
        api->kprintf(ATTR_WHITE, "audit_test: rshell before set_active(0), set_active(1)\n");
        api->rshell_set_active(0);
        api->rshell_set_active(1);
        api->kprintf(ATTR_WHITE, "audit_test: rshell after; PM verify continued response\n");
        return 0;
    }
    if (strcmp(argv[1], "mml") == 0) {
        static char mml[AUDIT_MML_NOTES + 1];
        for (i = 0; i < AUDIT_MML_NOTES; i++) mml[i] = 'C';
        mml[AUDIT_MML_NOTES] = '\0';
        api->kprintf(ATTR_WHITE, "audit_test: waiting mml\n");
        api->fm_play_mml(mml);
    } else if (strcmp(argv[1], "serial") == 0) {
        api->kprintf(ATTR_WHITE, "audit_test: waiting serial\n");
        i = api->serial_getchar();
        api->kprintf(ATTR_RED, "audit_test: serial unexpectedly returned %d\n", i);
    } else if (strcmp(argv[1], "ime") == 0) {
        api->kprintf(ATTR_WHITE, "audit_test: waiting ime\n");
        i = api->ime_getkey();
        api->kprintf(ATTR_RED, "audit_test: ime unexpectedly returned %d\n", i);
    } else {
        api->kprintf(ATTR_RED, "audit_test: unknown mode %s\n", argv[1]);
        return 1;
    }
    /* A successful manual wait probe is killed, never a normal return. */
    api->kprintf(ATTR_RED, "audit_test: FAIL wait returned before CTRL+STOP\n");
    return 1;
}
