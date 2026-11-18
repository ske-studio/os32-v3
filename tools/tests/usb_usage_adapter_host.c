/* 期待の出所: INPUT_KEYMAP_DESIGN §7 H-usbp・S2u / astra レビュー 2026-11-18 */
#include "types.h"
static unsigned makes, breaks, repeats, releases;
static u8 last_sc;
static struct { u8 sc; int down; } events[32];
static unsigned event_count;
u8 hid_kbd_sc(u8 usage);
void usbp_input_key(u8 src, u8 sc, int down)
{ (void)src; last_sc=sc; events[event_count].sc=sc; events[event_count++].down=down;
  if (down) ++makes; else ++breaks; }
void usbp_input_key_repeat(u8 src, u8 sc)
{ (void)src; last_sc=sc; ++repeats; }
void pm_input_usb_release(u8 src) { (void)src; ++releases; }
static u8 sources[2];
void usbp_log(const char *fmt, ...) { (void)fmt; }
#include "adapter.inc"
#define REQUIRE(c) do { if (!(c)) return __LINE__; } while (0)
static int run(void)
{
    usbp_input_usage(1,4,1); usbp_input_usage_commit(1);
    REQUIRE(makes==1 && last_sc==0x1d);
    usbp_input_usage_repeat(1,4); REQUIRE(repeats==1 && last_sc==0x1d);
    usbp_input_usage(1,0xe1,1); usbp_input_usage(1,0xe5,1); usbp_input_usage_commit(1);
    REQUIRE(makes==2 && last_sc==0x70);
    usbp_input_usage(1,0xe1,0); usbp_input_usage_commit(1); REQUIRE(breaks==0);
    usbp_input_usage(1,0xe5,0); usbp_input_usage_commit(1); REQUIRE(breaks==1 && last_sc==0x70);
    usbp_input_usage(1,0xff,1); usbp_input_usage_repeat(1,0xff);
    REQUIRE(makes==2 && repeats==1);
    usbp_input_usage(2,4,1); usbp_input_usage_commit(2); REQUIRE(makes==3);
    usbp_input_release(0,1); REQUIRE(releases==1);
    usbp_input_usage_repeat(1,4); REQUIRE(repeats==1);
    usbp_input_usage(1,4,1); usbp_input_usage_commit(1); REQUIRE(makes==4);
    usbp_input_usage(1,4,0); usbp_input_usage_commit(1); REQUIRE(breaks==2);
    usbp_input_usage_repeat(2,4); REQUIRE(repeats==2);
    usbp_input_usage(0,4,1); usbp_input_usage(5,4,1); REQUIRE(makes==4);
    usbp_input_usage(1,0x31,1); usbp_input_usage_commit(1);
    unsigned m = makes, b = breaks;
    usbp_input_usage(1,0x31,0); usbp_input_usage(1,0x32,1); usbp_input_usage_commit(1);
    REQUIRE(makes == m && breaks == b);
    usbp_input_usage(1,0x32,0); usbp_input_usage_commit(1);
    REQUIRE(makes == m && breaks == b + 1);
    usbp_input_usage(1,0x31,1); usbp_input_usage_commit(1);
    m = makes; b = breaks;
    unsigned event_before = event_count;
    usbp_input_usage(1,0x31,0); usbp_input_usage(1,4,1); usbp_input_usage_commit(1);
    REQUIRE(makes == m + 1 && breaks == b + 1);
    REQUIRE(event_count == event_before + 2 && events[event_before].sc == 0x28 &&
            events[event_before].down == 0 && events[event_before + 1].sc == 0x1d &&
            events[event_before + 1].down == 1);
    usbp_input_usage(1,4,0); /* pending break for a committed make */
    usbp_input_release(0,1); usbp_input_usage_commit(1);
    REQUIRE(makes == m + 1 && breaks == b + 1);
    return 0;
}
void _start(void)
{
    int rc=run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
