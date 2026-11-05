/* Declaration-stage checks, not an Init oracle. No libc or guest execution. */
#include "tools/tests/trident_device_fake.h"
#include "drivers/video/trident/trident_module.h"
#include "gfx/gfx_emergency.h"
static struct trident_fake fake;
static u8 storage[4194304];
static struct trident_fake_event trace[32];
static struct trident_device device;
static int equal(u32 a, u32 b)
{
    __asm__ volatile ("" : "+r"(a), "+r"(b));
    return a == b;
}
#define REQUIRE(cond) do { if (!(cond)) return __LINE__; } while (0)
static int selftest(void)
{
    struct trident_fake *f = &fake;
    REQUIRE(!trident_fake_reset(f, storage, sizeof(storage), trace, 32));
    trident_fake_bind(f, &device);
    REQUIRE(device.io == &trident_fake_io && device.io_opaque == f);
    REQUIRE(!device.armed && !device.pinned && !device.saved.valid);
    REQUIRE(!device.init_done && !device.first_init_done && !device.active);
    REQUIRE(!f->resources.ready && !f->resources.client && !f->resources.owner);
    REQUIRE(!f->resources.bar0_rid && !f->resources.bar1_rid);
    REQUIRE(!f->resources.display_sid && !f->resources.client_sid);
    REQUIRE(equal(f->resources.pci.command, 2) && equal(f->sr[9], 0x10));
    REQUIRE(equal(f->cr_color[0x1F], 0x7F) && equal(f->gr[6], 0x0C));
    REQUIRE(equal(f->real_bytes, 2097152) && f->upper == TRI_UPPER_INDEPENDENT);
    REQUIRE(equal(f->resources.decode_bytes, 4194304));
    REQUIRE(equal(f->resources.regs_bytes, 36864));
    REQUIRE(equal(f->resources.pci.bar0, 0x20000000));
    REQUIRE(equal(f->resources.pci.bar1, 0x20400000));
    REQUIRE(!f->pc98_initialized && !f->cr_color[0x21]);
    u32 word = 0;
    const struct trident_io *io = device.io;
    REQUIRE(!io->write32(f, TRI_SPACE_LFB, 0, 0x12345678));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 0, &word));
    REQUIRE(equal(word, 0xFA7F3F6F) && !storage[0]);
    /* Linear without Init is still blocked (observed missing-Init failure). */
    REQUIRE(!io->write16(f, TRI_SPACE_REGS, 0x3D4, 0x2021));
    REQUIRE(!io->write32(f, TRI_SPACE_LFB, 0, 0x12345678));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 0, &word));
    REQUIRE(equal(word, 0xFA7F3F6F));
    /* Test oracle gate mechanics with one synthetic event; 3b supplies full
     * Init trace, independently from product. This is not Init acceptance. */
    static const struct trident_fake_event expected = {
        .space = 0, .offset = 0x3C4, .value = 0x0B, .width = 1, .write = true
    };
    trident_fake_trace_reset(f);
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C4, 0x0B));
    struct trident_fake_event wrong = expected;
    wrong.width = 2;
    REQUIRE(trident_fake_accept_init(f, 0, &wrong, 1) == TRI_E_STATE);
    REQUIRE(!f->pc98_initialized);
    REQUIRE(trident_fake_accept_init(f, 0, &expected, 0) == TRI_E_INVAL);
    REQUIRE(trident_fake_accept_init(f, 1, &expected, 1) == TRI_E_INVAL);
    REQUIRE(!trident_fake_accept_init(f, 0, &expected, 1));
    REQUIRE(!io->write32(f, TRI_SPACE_LFB, 0, 0x12345678));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 0, &word));
    REQUIRE(equal(word, 0x12345678));
    REQUIRE(!io->write32(f, TRI_SPACE_LFB, 307196, 0x87654321));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 307196, &word));
    REQUIRE(equal(word, 0x87654321));
    REQUIRE(!io->write32(f, TRI_SPACE_LFB, 2097152, 0x76543210));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 2097152, &word));
    REQUIRE(equal(word, 0xFFFFFFFF));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 0, &word));
    REQUIRE(equal(word, 0x12345678));
    REQUIRE(!trident_fake_memory(f, 2097152, TRI_UPPER_MIRROR));
    REQUIRE(!io->write32(f, TRI_SPACE_LFB, 2097152, 0x87654321));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 0, &word));
    REQUIRE(equal(word, 0x87654321));
    REQUIRE(!trident_fake_memory(f, 4194304, TRI_UPPER_INDEPENDENT));
    REQUIRE(!io->write32(f, TRI_SPACE_LFB, 4194300, 0x13579BDF));
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 4194300, &word));
    REQUIRE(equal(word, 0x13579BDF));
    REQUIRE(io->read32(f, TRI_SPACE_LFB, 4194304, &word) == TRI_E_ACCESS);
    REQUIRE(equal(word, 0x13579BDF)); /* failed read leaves output alone */
    REQUIRE(io->read32(f, TRI_SPACE_LFB, 1, &word) == TRI_E_ACCESS);
    f->hole_start = 307196;
    REQUIRE(io->read32(f, TRI_SPACE_LFB, 307196, &word) == TRI_E_ACCESS);
    f->hole_start = U32_MAX;
    trident_fake_fail(f, f->operations + 1, -73);
    REQUIRE(io->write32(f, TRI_SPACE_LFB, 0, 0) == -73);
    REQUIRE(!io->read32(f, TRI_SPACE_LFB, 0, &word));
    REQUIRE(equal(word, 0x87654321));
    /* Access width and original address preserved in trace, including ports. */
    trident_fake_trace_reset(f);
    REQUIRE(!io->write16(f, TRI_SPACE_REGS, 0x3CE, 0x0506));
    REQUIRE(equal(f->trace_count, 1) && equal(f->trace[0].width, 2));
    REQUIRE(equal(f->trace[0].offset, 0x3CE) && equal(f->trace[0].value, 0x0506));
    u8 byte;
    REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x3CF, &byte) && equal(byte, 5));
    u16 half;
    REQUIRE(!io->read16(f, TRI_SPACE_REGS, 0x43C8, &half) && !half);
    REQUIRE(!io->port8(f, 0xFAC, 2));
    REQUIRE(equal(f->trace[3].space, 2) && equal(f->trace[3].width, 1));
    REQUIRE(equal(f->last_port, 0xFAC) && equal(f->last_port_value, 2));
    trident_fake_fail(f, f->operations + 1, -74);
    byte = 0x55;
    REQUIRE(io->read8(f, TRI_SPACE_REGS, 0x3CF, &byte) == -74);
    REQUIRE(equal(byte, 0x55) && f->trace[4].rc == -74);
    /* Attribute flip-flop, mono/color CRTC and all DAC components. */
    REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x3BA, &byte));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C0, 0x10));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C0, 0x41));
    REQUIRE(equal(f->ar[0x10], 0x41));
    REQUIRE(!io->write16(f, TRI_SPACE_REGS, 0x3B4, 0xAB11));
    REQUIRE(equal(f->cr_mono[0x11], 0xAB) && !f->cr_color[0x11]);
    f->trace_capacity = 0; /* bulk palette doesn't need a trace buffer */
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C8, 0));
    for (u32 i = 0; i < 768; ++i)
        REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C9, i & 63));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C7, 0));
    for (u32 i = 0; i < 768; ++i) {
        REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x3C9, &byte));
        REQUIRE(equal(byte, i & 63));
    }
    f->dac_readable = false;
    REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x3C9, &byte) && equal(byte, 255));
    REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x3C8, &byte));
    for (u32 i = 0; i < 4; ++i) REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x3C6, &byte));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C6, 0xA5));
    REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x3C6, &byte) && equal(byte, 0xA5));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x83C8, 4));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x83C6, 15));
    REQUIRE(!io->read8(f, TRI_SPACE_REGS, 0x83C6, &byte) && equal(byte, 15));
    REQUIRE(!trident_fake_reset(f, storage, sizeof(storage), trace, 1));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C4, 0x0B));
    REQUIRE(!io->write8(f, TRI_SPACE_REGS, 0x3C4, 0x0E));
    REQUIRE(f->trace_overflow);
    REQUIRE(trident_fake_accept_init(f, 0, &expected, 1) == TRI_E_INVAL);
    REQUIRE(!f->pc98_initialized && !f->sr[0x0E]);
    return 0;
}
/* The Python driver supplies runtime constant/layout checks and signature
 * assertions, then calls selftest. This file deliberately has no _start. */
