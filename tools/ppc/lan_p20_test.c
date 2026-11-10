#include "lan_boot.h"
#include "kboot_log.h"
static const struct pm_log_sink *installed;
static int registrations, inject;
u32 ppc_irq_save(void) { return 0; }
void ppc_irq_restore(u32 x) {
    (void)x;
    if (inject) { inject = 0; installed->write(0, "nested", 6); }
}
int pm_log_add_sink(const struct pm_log_sink *s) { installed = s; registrations++; return 0; }
#include "lan_log.c"
static void out(const char *s) {
    u32 n = 0; while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n));
}
static void die(const char *s) { out(s); __asm__ volatile("int $0x80" : : "a"(1), "b"(1)); __builtin_unreachable(); }
#define CHECK(x, msg) do { if (!(x)) die(msg "\n"); } while (0)
static void reset(void) {
    head = count = open_slot = busy = drop_tail = flags = 0;
    next_seq = drop_records = drop_bytes = 0;
}
static const u8 *p; static u32 len, status; static u64 seq, records, bytes;
static int peek(void) { return pm_lan_log_peek(&p, &len, &seq, &records, &bytes, &status); }
static void write_text(const char *s) { u32 n = 0; while (s[n]) n++; installed->write(0, s, n); }
static void test(void) {
    static const char *fragments[] = {
        "KBOOT OF-CAPTURE ram=", "16384", " admitted=", "16384", " tb_hz=", "25000000", "\r\n",
        "KBOOT OF-OFF calls=", "3", "\r\n", "KBOOT MMU-D root=0x", "1234", " hpt=0x", "5678", " bytes=", "4096", " ram_bat=", "1", "\r\n",
        "KBOOT MEMORY online=", "16384", " heap=", "100", " shm=", "0", "\r\n",
        "KBOOT TRAP dsi=", "ok", " isi=", "ok", " guard=", "ok", " flush=", "ok", "\r\n",
        "KBOOT MPIC sources=", "32", " priority=0 cpu=0 delivery=", "ok", "\r\n",
        "KBOOT CLOCK hz=", "1000", " tick=", "1", " us=", "1000", "\r\n",
        "KBOOT READY stage=pre-user\r\n", "KBOOT ALIVE tick_delta=", "2000", " tb_us=", "2000000", " of_calls_delta=", "0", "\r\n", "PHY waiting\r\n" };
    char expected[1024], input[16640]; u32 used = 0, got = 0;
    CHECK(pm_lan_log_register() == 0 && pm_lan_log_register() == 0 && registrations == 1, "register");
    installed->write(0, "", 0); CHECK(peek() == LAN_AGAIN, "empty");
    for (u32 i = 0; i < sizeof(fragments)/sizeof(fragments[0]); i++) {
        const char *s = fragments[i]; for (u32 j = 0; s[j]; j++) expected[used++] = s[j]; write_text(s);
    }
    out("reached=fixture\n");
    CHECK(drop_bytes == 0 && count <= 4, "H:packing");
    while (peek() == LAN_OK) { for (u32 i = 0; i < len; i++) CHECK(p[i] == (u8)expected[got++], "H:packing"); pm_lan_log_complete(LAN_OK); }
    CHECK(got == used && !drop_bytes, "H:packing");
    reset(); for (u32 i = 0; i < sizeof(input); i++) input[i] = (char)(i >> 8);
    installed->write(0, input, sizeof(input)); out("reached=full\n");
    CHECK(count == 64 && drop_records == 1 && drop_bytes == 256 && next_seq == 65, "H:A8 loss-count");
    CHECK(peek() == LAN_OK && seq == 0 && p[0] == 0, "H:oldest");
    pm_lan_log_complete(LAN_AGAIN); CHECK(peek() == LAN_OK && seq == 0 && p[0] == 0, "H:again");
    pm_lan_log_complete(LAN_OK); CHECK(peek() == LAN_OK && seq == 1 && p[0] == 1, "H:accepted");
    reset(); inject = 1; write_text("outer"); out("reached=reentry\n");
    CHECK(drop_bytes == 6 && drop_tail == 0 && drop_records == 1 && next_seq == 1, "H:reentry");
    CHECK(peek() == LAN_OK && seq == 1 && len == 5, "H:reentry-order");
    reset(); installed->write(0, input, 16384); installed->write(0, input, 17);
    CHECK(drop_bytes == 17 && drop_records == 0, "partial-before");
    CHECK(peek() == LAN_OK, "partial-peek"); pm_lan_log_complete(LAN_OK); write_text("x");
    CHECK(drop_records == 1 && drop_bytes == 17 && next_seq == 65, "partial-recovery");
    out("PASS\n");
}
void _start(void) { test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); __builtin_unreachable(); }
