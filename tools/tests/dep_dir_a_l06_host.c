/* Generated KAPI -> real platform -> FS substitutes, ILP32. */
#include "platform_device.h"
#include "ext2.h"
#include "serialfs.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(c) do { if (!(c)) die(1); } while (0)
static int calls, result, drive, code, allowed = 1;
static u32 start, length;
int ext2_format(int drv, u32 sectors)
{ CHECK(drv == drive && sectors == length); calls++; return result; }
int ext2_format_at(int drv, u32 start_lba, u32 sectors)
{ CHECK(drv == drive && start_lba == start && sectors == length); calls++; return result; }
int serialfs_session_begin(void) { calls++; return result; }
int serialfs_session_end(int exit_code)
{ CHECK(exit_code == code); calls++; return result; }
static int exec_disk_write_allowed(void) { return allowed; }
#define KAPI_HIT(n) ((void)(n))
#include "wrappers.inc"

void _start(void)
{
    const u32 edges[] = {0, 1, 18, 0x87654321u, 0xffffffffu};
    for (result = -16; result <= 1; result++) {
        for (drive = -1; drive <= 4; drive++) {
            for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
                start = edges[i];
                length = edges[(i + 1) % (sizeof(edges) / sizeof(edges[0]))];
                code = drive;
                calls = 0;
                CHECK(platform_ext2_format(drive, length) == result);
                CHECK(platform_ext2_format_at(drive, start, length) == result);
                CHECK(platform_serialfs_session_begin() == result);
                CHECK(platform_serialfs_session_end(code) == result);
                CHECK(wrap_ext2_format(drive, length) == result);
                CHECK(wrap_ext2_format_at(drive, start, length) == result);
                CHECK(wrap_sfs_begin() == result);
                CHECK(wrap_sfs_end(code) == result);
                CHECK(calls == 8);
                allowed = 0;
                calls = 0;
                CHECK(wrap_ext2_format(drive, length) == -1);
                CHECK(wrap_ext2_format_at(drive, start, length) == -1);
                CHECK(calls == 0);
                allowed = 1;
            }
        }
    }
    die(0);
}
