/* Freestanding 32-bit host: only privileged instruction boundaries are faked. */
#include "ppc_oea.h"
static u32 fake_pvr, hid, bats[16], sr[16], sdr, events[256], used;
static void event(u32 x) { if (used < 256) events[used++] = x; }
static void quit(int status) __attribute__((noreturn));
static void quit(int status)
{
    __asm__ volatile("int $0x80" :: "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}
static int verdict = 1;
static void mark(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void check(int ok) { if (!ok) quit(verdict); }
u32 ppc_read_pvr(void) { return fake_pvr; }
u32 ppc_read_hid0(void) { return hid; }
void ppc_write_hid0(u32 v) { hid = v; event(4); }
void ppc_cpu_test_stream_stop(void) { event(6); }
void ppc_sync(void) { event(1); }
void ppc_isync(void) { event(2); }
void ppc_tlb_flush_all(void) { event(3); }
void ppc_stop(void) { quit(2); }
void ppc_cpu_test_spr(u32 n, u32 v)
{
    if (n == PPC_SPR_SDR1) { sdr = v; event(5); return; }
    check((n >= 528 && n <= 542 && !(n&1)) ||
          (n >= 560 && n <= 574 && !(n&1)));
    u32 i = n < 560 ? (n-528)/2 : 8+(n-560)/2;
    if (i >= 8) check((hid & (PPC_HID_HIGHBAT|PPC_HID_XBSEN)) ==
                               (PPC_HID_HIGHBAT|PPC_HID_XBSEN));
    bats[i] = v; event(16+i);
}
void ppc_bat_set(u32 index, u32 lower, u32 upper)
{
    (void)lower; check(index==0); bats[0]=bats[4]=upper;
}
void ppc_cpu_test_sr(u32 n, u32 v) { check(n < 16); sr[n] = v; event(40+n); }
void _start(void)
{
    u32 pvr;
    fake_pvr = 0x12345678;
    check(!ppc_cpu_identify(&pvr) && pvr == fake_pvr);
    for (u32 cpu=0; cpu<2; ++cpu) {
        fake_pvr = ((PPC_PVR_7450+cpu)<<16)|0x0201;
        check(ppc_cpu_identify(&pvr) && pvr == fake_pvr);
        check(ppc_cpu_identify(0));
        for (u32 flags=0; flags<4; ++flags) {
            hid = PPC_HID_STEN|PPC_HID_BTIC;
            if (cpu && (flags&1)) hid |= PPC_HID_HIGHBAT;
            if (cpu && (flags&2)) hid |= PPC_HID_XBSEN;
            for (u32 i=0; i<16; ++i) bats[i]=3;
            used=0;
            ppc_cpu_disable_bats(pvr);
            check(!(hid&PPC_HID_STEN) && (hid&PPC_HID_TBEN) && (hid&PPC_HID_BTIC));
            mark("BAT-READY\n", 10);
            for (u32 i=0; i<(cpu ? 16U : 8U); ++i) {
                verdict = i < 8 ? 11 : 12;
                check(bats[i]==0);
            }
            verdict = 1;
            if (!cpu) for (u32 i=8; i<16; ++i) check(bats[i]==3);
            check(events[0]==6 && events[1]==1 && events[2]==4 && events[3]==1 && events[4]==2);
        }
    }
    used=0;
    ppc_cpu_load_segments();
    check(used==20 && events[0]==1 && events[17]==1 && events[18]==2 && events[19]==3);
    mark("SR-READY\n", 9);
    verdict = 13;
    for (u32 i=0; i<16; ++i) check(sr[i]==(0x20000010U|i) && events[1+i]==40+i);
    verdict = 1;
    used=0;
    ppc_cpu_load_sdr1(0x01000001);
    mark("SDR1-READY\n", 11);
    verdict = 14;
    check(sdr==0x01000001 && used==5 && events[0]==1 && events[1]==5 &&
          events[2]==1 && events[3]==2 && events[4]==3);
    quit(0);
}
