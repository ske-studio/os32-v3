/* Real V86 exit functions, extracted without rewriting their bodies. */
#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "pc98.h"
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)
static unsigned int ports[64], values[64], n, waits;
static int palette, cursor, deny, sound, active, restored;
static void outp(unsigned int p, unsigned int v)
{
    CHECK(n < 64, "bounded OUT trace");
    ports[n] = p; values[n++] = v;
}
static void io_wait(void) { waits++; }
static void palette_init(void) { palette++; }
static void console_hw_cursor_enable(void) { cursor++; }
static void tss_iomap_deny_all(void) { deny++; }
static int v86_gcap_active(void) { return active; }
static void snd_state_for_os32(void) { sound++; }
static void tv_restore(const u16 *tv) { CHECK(*tv == 123, "saved TVRAM"); restored++; }
#include "display_cleanup_slice.inc"
int main(int argc, char **argv)
{
    static const unsigned int want_ports[] = {
        0xA2, 0x6A, 0x68, 0xA2, 0xA0, 0xA0, 0xA0, 0xA2,
        0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
        0xA2, 0x68, 0x62, 0xA4, 0xA6
    };
    static const unsigned int want_values[] = {
        0x0C, 0x01, 0x08, 0x4B, 0, 0, 0, 0x70,
        0, 0, 0, 0x19, 0, 0, 0, 0, 0x0C, 0x0F, 0x0D, 0, 0
    };
    u16 tv = 123;
    CHECK(argc == 2, "case argument");
    int mode = atoi(argv[1]);
    if (mode == 0) {
        v86_io_reset_policy();
        for (unsigned int i = 0; i < n; i++) {
            CHECK(ports[i] != GDC_GFX_CMD || values[i] != GDC_CMD_START,
                  "graphics must stay stopped");
            CHECK(ports[i] != GDC_ACCESS_PAGE || values[i] == GDC_PAGE_0,
                  "access page must be zero");
            CHECK(ports[i] != GDC_DISP_PAGE || values[i] == GDC_PAGE_0,
                  "display page must be zero");
        }
        int display_enabled = 0, text_started = 0;
        for (unsigned int i = 0; i < n; i++) {
            if (ports[i] == MODE_FF1_PORT && values[i] == MFF1_DISP_ON) display_enabled = 1;
            if (ports[i] == GDC_TEXT_CMD && values[i] == GDC_CMD_START) text_started = 1;
        }
        CHECK(display_enabled, "display output must be enabled");
        CHECK(text_started, "text must be started");
        CHECK(n == sizeof(want_ports) / sizeof(*want_ports), "normal OUT count");
        for (unsigned int i = 0; i < n; i++)
            CHECK(ports[i] == want_ports[i] && values[i] == want_values[i], "CUI OUT order/value");
        CHECK(palette == 1 && cursor == 1 && deny == 1 && sound == 1, "normal restoration retained");
    } else if (mode == 1) {
        active = 1;
        v86_io_reset_policy();
        CHECK(!n && !palette && !cursor && !sound && deny == 1, "capture bypass retained");
    } else {
        gcap_cui_rebuild(&tv);
        CHECK(n == 3 && restored == 1 && cursor == 1, "capture rebuild");
        for (unsigned int i = 0; i < n; i++)
            CHECK(ports[i] == want_ports[16+i] && values[i] == want_values[16+i], "shared CUI order");
    }
    CHECK(waits == n, "I/O wait for every OUT");
    puts("PASS: V86 CUI exit");
    return 0;
}
