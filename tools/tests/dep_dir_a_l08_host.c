/* ILP32 generated wrappers -> platform -> PCI substitutes. */
#include <stddef.h>
#include "platform_device.h"
#include "pci_bind_contract.h"
#include "os32_kapi_shared.h"
_Static_assert(sizeof(struct pci_dev) == 40, "PCI enumeration ABI");
_Static_assert(PCI_DEV_STRUCT_SIZE == 40, "PCI public size");
_Static_assert(sizeof(struct pci_bind_info) == 8, "PCI diagnosis ABI");
_Static_assert(offsetof(struct pci_dev, bar) == 12, "BAR offset");
_Static_assert(offsetof(struct pci_dev, command) == 38, "command offset");
static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(c) do { if (!(c)) die(1); } while (0)
static int calls, absent, range_ok = 1, writable = 1, guards;
static u32 index, bus_arg, dev_arg, fn_arg, reg_arg, guard_len;
static void *user_out;
int pci_count(void) { calls++; return absent ? 0 : 32; }
int pci_get(u32 idx, void *out)
{
    CHECK(idx == index && out == user_out); calls++;
    if (absent) return -1;
    for (int i = 0; i < 40; i++) ((u8 *)out)[i] = (u8)(i + 1);
    return 0;
}
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    CHECK(bus == bus_arg && dev == dev_arg && fn == fn_arg && reg == reg_arg);
    calls++; return absent ? 0xffffffffu : 0x12348086u;
}
int pci_bind_info_get(int idx, struct pci_bind_info *out)
{
    CHECK(idx == (int)index && (void *)out != user_out); calls++;
    if (absent) return -1;
    for (int i = 0; i < 8; i++) ((u8 *)out)[i] = (u8)(i + 2);
    return 0;
}
static int ring3_user_range_ok(u32 p, u32 len)
{ CHECK(p == (u32)user_out && len == 8); return range_ok; }
static int ring3_user_ranges_writable(u32 p, u32 len, u32 p2, u32 len2)
{
    CHECK(p == (u32)user_out && p2 == 0 && len2 == 0);
    guards++; guard_len = len; return writable;
}
static void ring3_fault_kill(void) { die(writable ? 1 : 0); }
#define KAPI_HIT(n) ((void)(n))
#define KAPI_OUT_LEN(p, n) ((u32)(n))
#include "wrappers.inc"

void _start(void)
{
    u8 buf[42];
    const u32 edges[] = {0, 1, 31, 32, 0x80000000u, 0xffffffffu};
    user_out = buf + 1;
    for (absent = 0; absent <= 1; absent++) {
        for (unsigned e = 0; e < sizeof(edges) / sizeof(edges[0]); e++) {
            index = bus_arg = edges[e]; dev_arg = edges[(e + 1) % 6];
            fn_arg = edges[(e + 2) % 6]; reg_arg = edges[(e + 3) % 6];
            for (int i = 0; i < 42; i++) buf[i] = 0xa5;
            calls = guards = 0;
            CHECK(wrap_pci_count() == (absent ? 0 : 32));
            CHECK(wrap_pci_get(index, user_out) == (absent ? -1 : 0));
            CHECK(guards == 1 && guard_len == 40);
            CHECK(buf[0] == 0xa5 && buf[41] == 0xa5);
            for (int i = 0; i < 40; i++) CHECK(buf[i + 1] == (absent ? 0xa5 : i + 1));
            CHECK(wrap_pci_cfg_read32(bus_arg, dev_arg, fn_arg, reg_arg) ==
                  (absent ? 0xffffffffu : 0x12348086u));
            for (int i = 0; i < 42; i++) buf[i] = 0xa5;
            CHECK(wrap_pci_bind_info(index, user_out) == (absent ? OS32_ERR_INVAL : 0));
            CHECK(guards == 2 && guard_len == 8 && calls == 4);
            CHECK(buf[0] == 0xa5 && buf[9] == 0xa5);
            for (int i = 0; i < 8; i++) CHECK(buf[i + 1] == (absent ? 0xa5 : i + 2));
            range_ok = 0;
            CHECK(wrap_pci_bind_info(index, user_out) == OS32_ERR_INVAL && calls == 4);
            range_ok = 1;
        }
    }
    calls = guards = 0;
    CHECK(kapi_pci_bind_info(0, 0) == OS32_ERR_INVAL);
    user_out = (void *)0xfffffffcu;
    CHECK(kapi_pci_bind_info(0, user_out) == OS32_ERR_INVAL);
    CHECK(calls == 0 && guards == 0);
    user_out = buf + 1;
    writable = 0;
    /* die(0) only through the protection fault, before driver access. */
    wrap_pci_get(0, user_out);
    die(1);
}
