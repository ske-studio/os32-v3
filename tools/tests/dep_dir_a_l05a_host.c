/* ILP32: generated wrappers -> real platform -> isolated IDE substitutes. */
#include "platform_device.h"
#include "ide.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(c) do { if (!(c)) die(1); } while (0)
static int calls, result, drive, allowed = 1, range_ok = 1, guards, fault;
static void *pointer;
static u32 length;
static IdeInfo info;
static u8 buffer[1024];

int ide_range_ok(int drv, u32 lba, u32 count)
{ (void)drv; (void)lba; (void)count; return 0; }
int ide_init(void) { calls++; return result; }
int ide_drive_present(int drv)
{ CHECK(drv == drive); calls++; return result; }
int ide_identify(int drv, IdeInfo *out)
{
    CHECK(drv == drive && out == pointer);
    calls++;
    if (out) out->total_sectors = 0x87654321;
    return result;
}
int ide_get_info(int drv, IdeInfo *out)
{
    CHECK(drv == drive && out == pointer);
    calls++;
    if (out) out->phys_sector_size = 256;
    return result;
}
int ide_read_sector(int drv, u32 lba, void *out)
{
    CHECK(drv == drive && lba == 0x98765432 && out == pointer);
    calls++;
    if (out) *(u8 *)out = 0x75;
    return result;
}
int ide_write_sector(int drv, u32 lba, const void *in)
{
    CHECK(drv == drive && lba == 0x98765432 && in == pointer);
    calls++;
    return result;
}
int ide_write_sectors(int drv, u32 lba, u32 count, const void *in)
{
    CHECK(drv == drive && lba == 0x98765432 && count == 2 && in == pointer);
    calls++;
    return result;
}
static int exec_disk_write_allowed(void) { return allowed; }
static int ring3_user_ranges_writable(u32 p, u32 n, u32 p2, u32 n2)
{
    CHECK(p == (u32)pointer && n == length && p2 == 0 && n2 == 0);
    guards++;
    return range_ok;
}
static int kapi_in_range(u32 p, u32 n)
{
    CHECK(p == (u32)pointer && n == length);
    guards++;
    return range_ok;
}
static u32 kapi_out_mul(u32 n, u32 unit)
{
    CHECK(unit == 512 && n == 2);
    return n * unit;
}
static void ring3_fault_kill(void) __attribute__((noreturn));
static void ring3_fault_kill(void)
{
    CHECK(fault && calls == 0 && guards == 1);
    die(0);
}
#define KAPI_HIT(n) ((void)(n))
#define KAPI_OUT_LEN(p, n) ((u32)(n))
#include "wrappers.inc"

static void forwards(void)
{
    /* Success, absent, every IDE error and out-of-range drive are opaque. */
    for (result = -4; result <= 1; result++) {
        for (drive = -1; drive <= 4; drive++) {
            calls = 0;
            platform_ide_init();
            CHECK(platform_ide_drive_present(drive) == result);
            for (int null = 0; null <= 1; null++) {
                pointer = null ? 0 : &info;
                CHECK(platform_ide_identify(drive, pointer) == result);
                CHECK(null || info.total_sectors == 0x87654321);
                CHECK(platform_ide_get_info(drive, pointer) == result);
                CHECK(null || info.phys_sector_size == 256);
                pointer = null ? 0 : buffer;
                CHECK(platform_ide_read_sector(drive, 0x98765432, pointer) == result);
                CHECK(null || buffer[0] == 0x75);
                CHECK(platform_ide_write_sector(drive, 0x98765432, pointer) == result);
                CHECK(platform_ide_write_sectors(drive, 0x98765432, 2, pointer) == result);
            }
            CHECK(calls == 12);
            calls = guards = 0;
            wrap_ide_init();
            CHECK(wrap_ide_drive_present(drive) == result);
            pointer = &info; length = 96;
            CHECK(wrap_ide_identify(drive, pointer) == result);
            CHECK(wrap_ide_get_info(drive, pointer) == result);
            pointer = buffer; length = 512;
            CHECK(wrap_ide_read_sector(drive, 0x98765432, pointer) == result);
            CHECK(wrap_ide_write_sector(drive, 0x98765432, pointer) == result);
            length = 1024;
            CHECK(wrap_ide_write_sectors(drive, 0x98765432, 2, pointer) == result);
            CHECK(calls == 7 && guards == 5);
        }
    }
    calls = guards = 0;
    allowed = 0;
    CHECK(wrap_ide_write_sector(drive, 0, 0) == -1);
    CHECK(wrap_ide_write_sectors(drive, 0, 0, 0) == -1);
    CHECK(wrap_ide_write_sectors(drive, 0, 2, 0) == -1);
    allowed = 1;
    CHECK(wrap_ide_write_sectors(drive, 0, 0, 0) == 0);
    CHECK(calls == 0 && guards == 0);
}
void start(u32 *stack)
{
    fault = stack[0] > 1 ? ((char **)stack)[2][0] - '0' : 0;
    if (!fault) { forwards(); die(0); }
    range_ok = 0;
    pointer = 0;
    length = 96;
    if (fault == 1) wrap_ide_identify(drive, pointer);
    if (fault == 2) wrap_ide_get_info(drive, pointer);
    length = 512;
    if (fault == 3) wrap_ide_read_sector(drive, 0x98765432, pointer);
    if (fault == 4) wrap_ide_write_sector(drive, 0x98765432, pointer);
    length = 1024;
    if (fault == 5) wrap_ide_write_sectors(drive, 0x98765432, 2, pointer);
    die(1);
}
__asm__(".global _start\n_start:\nmov %esp,%eax\npush %eax\ncall start\n");
