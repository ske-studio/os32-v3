/* Expected behavior follows HW_OP §5 3b/6b/6c and R3-P1/P3. */
#include "kboot_info.h"
#include "ppc_trap.h"
#include "kernel_boot.h"
#include "kdebug/hwb_wire.h"
#include "kstring.h"
#include "lan_boot.h"
#include "pci_config_contract.h"
#include "drivers/pci/pci_bind.h"

static struct pm_bridge_state fixture_bridge = {.addr_va=0x1000, .data_va=0x2000};
static u32 address, last_address, last_port, data, writes, reads, tree;
u32 ppc_irq_save(void) { return 0; }
void ppc_irq_restore(u32 flags) { (void)flags; }

void pm_pci_mmio_write32(u32 va, u32 raw)
{
    u32 value = __builtin_bswap32(raw);
    if (va == 0x1000) {
        address = value;
        if (value)
            last_address = value;
    } else {
        last_port = va;
        data = value;
        writes++;
    }
}

u32 pm_pci_mmio_read32(u32 va)
{
    if (va == 0x1000)
        return __builtin_bswap32(address);
    last_port = va;
    reads++;
    if (!tree)
        return __builtin_bswap32(data);
    u32 device = address & ~0xfcu;
    u32 reg = address & 0xfcu;
    if (device != (1u << 11) && device != 0x10001 &&
        device != (1u << 12) && device != ((1u << 11) | 0x100))
        return U32_MAX;
    u32 word = 0;
    if (!reg) word = 0x1234abcd;
    if (device == (1u << 11) && reg == 0x0c) word = 0x00810000;
    if (reg == 0x18) word = 0x100;
    if (reg == 0x3c) word = 0x300;
    return __builtin_bswap32(word);
}

void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    (void)fmt;
}

static int stage, fail_at, accesses, stop_count, memory_calls;
static int fault_command_read, fault_command_write;
static struct pm_bridge_state second = {.addr_va=0x3000, .data_va=0x4000};
struct pm_bridge_state *pm_bridge_lookup(int id)
{ return id == 1 ? &fixture_bridge : id == 2 ? &second : NULL; }

static int fake_instruction(u32 va, u32 width, int write, u32 *value)
{
    accesses++;
    if (++stage == fail_at) { ppc_trap_onfault_clear(); return 1; }
    if (va == 0x3000 || va == 0x4000) va -= 0x2000;
    if (va == 0x2004 && width == 2 &&
        (write ? fault_command_write : fault_command_read)) { ppc_trap_onfault_clear(); return 1; }
    u32 mask = U32_MAX >> ((4 - width) * 8);
    if (write) pm_pci_mmio_write32(va, __builtin_bswap32(*value & mask));
    else if (va == 0x1000) *value = 0xdeadbeef; /* posted-write flush only */
    else *value = __builtin_bswap32(pm_pci_mmio_read32(va)) & mask;
    return 0;
}

u32 ppc_trap_msr(void) { return 0; }
#define FAKE_INSN(name, width, store) \
    char ppc_hwb_##name##_insn[4] __attribute__((aligned(4))); \
    char ppc_hwb_##name##_resume[4] __attribute__((aligned(4))); \
    int ppc_hwb_##name(u32 va, u32 operand, u32 count, u32 inc) { \
        (void)count; (void)inc; \
        return fake_instruction(va, width, store, store ? &operand : (u32 *)operand); \
    }
FAKE_INSN(read8, 1, 0)
FAKE_INSN(write8, 1, 1)
FAKE_INSN(read16, 2, 0)
FAKE_INSN(write16, 2, 1)
FAKE_INSN(read32, 4, 0)
FAKE_INSN(write32, 4, 1)
FAKE_INSN(read16le, 2, 0)
FAKE_INSN(write16le, 2, 1)
FAKE_INSN(read32le, 4, 0)
FAKE_INSN(write32le, 4, 1)

static int memory_boot_init(u32 kb) { (void)kb; memory_calls++; return 1; }
static void *stop_env[5];
static void kboot_stop(const char *why)
{ (void)why; stop_count++; __builtin_longjmp(stop_env, 1); }
#define KBOOT_MEM_KB 1024
#include "pci_boot_memory.inc"
int pm_gmac_clock(const struct pm_gmac_boot *b, u32 va)
{ (void)b; (void)va; return 0; }
static struct pm_kboot_info info;
static struct ppc_boot_devices devices;
static int boot_case(int expect_stop)
{
    stop_count = memory_calls = 0;
    if (!__builtin_setjmp(stop_env)) pci_boot_memory(&info, &devices);
    return stop_count == expect_stop && memory_calls == !expect_stop;
}

static int bridge_cases(void)
{
    struct pm_pci handle = {0, 0, 0, &fixture_bridge};
    u32 tag = 12u << 11, value, packed = tag >> 8;
    info.pci.bridges = 1;
    info.pci.bridge[0] = (struct pm_of_bridge){.id=1, .parent=-1, .supported=1};
    if (!boot_case(0)) return 20;
    data = 0x12345678;
    if (pm_hwb_cfg_read(packed,0,4,&value) || value != data ||
        pci_config_read32(0,12,0,0) != data) return 21;
    if (!ppc_trap_onfault_data(4, 8, 12)) return 22;
    accesses = 0;
    if (pm_hwb_cfg_read(packed,0,4,&value) != HW_IO_ERROR || value ||
        accesses || fixture_bridge.isolated || ppc_trap_onfault_data(16, 20, 24) || fixture_bridge.busy) return 22;
    ppc_trap_onfault_clear();
    for (int fault = 1; fault <= 5; ++fault) {
        fixture_bridge.isolated = 0;
        static const int expected_calls[] = {0, 3, 4, 5, 4, 5};
        reads = stage = 0;
        fail_at = fault;
        if (pm_hwb_cfg_read(packed,0,4,&value) != HW_IO_ERROR || value ||
            stage != expected_calls[fault] || reads != (unsigned)(fault >= 4) ||
            fixture_bridge.isolated != (unsigned)(fault >= 4)) return 23;
    }
    accesses = 0;
    struct pm_pci copy = handle;
    if (pci_config_read32(0,12,0,0) != U32_MAX ||
        pm_hwb_cfg_write(packed,0,4,data) != HW_IO_ERROR ||
        !pm_pci_read32(&copy,tag,0,&value) || accesses) return 24;
    handle.bridge = &second;
    fail_at = 0;
    if (pm_pci_read32(&handle,tag,0,&value) || value != data) return 25;
    fixture_bridge.isolated = 0;
    info.pci.gmac_known = 1;
    info.gmac = (struct pm_gmac_boot){.node=3,.profile=PM_GMAC_PROFILE_UNINORTH,
        .vendor_device=0x0021106b,.bdf=tag,.root_node=1};
    info.gmac_rc = LAN_EIO;
    if (!boot_case(1)) return 26;
    info.gmac_rc = 0;
    if (!boot_case(1)) return 27;
    devices.gmac_ready = 1;
    info.gmac.root_node = 99;
    if (!boot_case(1)) return 28;
    info.gmac.root_node = 1;
    data = info.gmac.vendor_device;
    fault_command_read = 1;
    if (!boot_case(1)) return 29;
    fault_command_read = 0; fault_command_write = 1;
    if (!boot_case(1)) return 30;
    fault_command_write = 0;
    if (!boot_case(0)) return 31;
    return 0;
}

static int test(void)
{
    struct pm_pci window = {2, 3, 0, &fixture_bridge};
    u32 bus = 99;
    if (pci_config_probe() || reads || !pci_config_root_bus(0, &bus))
        return 1;
    pm_pci_boot(&window, (2u << 16) | (12u << 11));
    window.bus_first = 0;
    data = 0x1234abcd;
    if (!pci_config_probe() || last_address || reads || writes)
        return 2;
    if (pci_config_root_bus(0, &bus) || bus != 2 ||
        pci_config_root_bus(1, &bus) != -1 || pci_config_root_bus(U32_MAX, &bus) != -1)
        return 3;
    if (pci_config_bar_measure() || kstrcmp(pci_config_name(), "uni-north"))
        return 4;
    if (pci_config_read32(3, 1, 2, 0x0e) != data || last_address != 0x30a0d ||
        last_port != 0x2004)
        return 5;
    u32 previous = reads;
    if (pci_config_read32(4, 12, 0, 0) != U32_MAX ||
        pci_config_read32(2, 32, 0, 0) != U32_MAX ||
        pci_config_read32(3, 0, 8, 0) != U32_MAX ||
        pci_config_read32(258, 12, 0, 0) != U32_MAX ||
        pci_config_read32(2, 12, 0, 256) != U32_MAX ||
        pci_config_read32(2, 10, 0, 0) != U32_MAX || reads != previous)
        return 6;
    pci_config_write16(2, 12, 0, 4, 0xbeef);
    if (writes != 1 || data != 0xbeef || last_port != 0x2004 ||
        last_address != ((1u << 12) | 4))
        return 7;
    pci_config_write16(2, 12, 0, 6, 1);
    pci_config_write16(2, 12, 0, 8, 1);
    if (writes != 1)
        return 8;
    pci_config_write32(2, 12, 0, 4, 0xffff1234);
    if (writes != 2 || data != 0x1234)
        return 9;
    pci_config_write32(2, 12, 0, 0x10, 0xfeed1234);
    pci_config_write32(2, 12, 0, 0x11, 0);
    if (writes != 3 || data != 0xfeed1234 || last_port != 0x2000)
        return 10;
    data = U32_MAX;
    if (!pci_config_probe())
        return 11;
    tree = 1;
    writes = 0;
    pm_pci_boot(&window, 11u << 11);
    pm_pci_enumerate();
    static const u8 expected[][3] = {{0,11,0}, {1,0,0}, {0,11,1}, {0,12,0}};
    if (writes || pci_count() != 4 || pci_cfg_read8(0,11,0,0x19) != 1 ||
        pci_cfg_read8(0,11,0,0x3d) != 3) return 12;
    for (int i = 0; i < 4; i++) {
        struct pci_dev d;
        struct pci_bind_info bind;
        if (pci_get(i, &d) || d.bus != expected[i][0] || d.dev != expected[i][1] ||
            d.fn != expected[i][2] || d.vendor != 0xabcd || d.device != 0x1234 ||
            pci_bind_info_get(i, &bind) || bind.reason != PCI_BIND_NO_DRIVER)
            return 13;
    }
    tree = 0;
    return bridge_cases();
}

void _start(void)
{
    int result = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result));
    __builtin_unreachable();
}
