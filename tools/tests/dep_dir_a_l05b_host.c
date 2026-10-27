/* Real adapter, isolated IDE. Geometry is a value snapshot, not driver state. */
#include "platform_device.h"
#include "ide.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(c) do { if (!(c)) die(1); } while (0)
static int calls, result, expected_drive;
static u32 expected_lba, expected_count;
static IdeInfo snapshot;

int ide_get_info(int drv, IdeInfo *out)
{
    CHECK(drv == expected_drive);
    calls++;
    *out = snapshot; /* Even a failed driver may have written a partial result. */
    return result;
}
int ide_range_ok(int drv, u32 lba, u32 count)
{
    CHECK(drv == expected_drive && lba == expected_lba && count == expected_count);
    calls++;
    return result;
}
static void check_geometry(u16 bps)
{
    struct platform_disk_geometry out = {7, 11, 13, 17, 19};
    snapshot.total_sectors = 0x87654321u;
    snapshot.cylinders = 0xabcd;
    snapshot.heads = 0x1234;
    snapshot.sectors = 0x5678;
    snapshot.phys_sector_size = bps;
    calls = 0;
    CHECK(platform_disk_geometry_get(expected_drive, &out) == result);
    CHECK(calls == 1);
    if (result) {
        CHECK(out.total_sectors == 7 && out.cylinders == 11 && out.heads == 13);
        CHECK(out.sectors == 17 && out.phys_sector_size == 19);
    } else {
        CHECK(out.total_sectors == 0x87654321u && out.cylinders == 0xabcd);
        CHECK(out.heads == 0x1234 && out.sectors == 0x5678 && out.phys_sector_size == bps);
        snapshot.total_sectors = 0;
        CHECK(out.total_sectors == 0x87654321u);
    }
}
void _start(void)
{
    static const int results[] = {IDE_OK, IDE_ERR_NO_DRIVE, IDE_ERR_IO, IDE_ERR_TIMEOUT, IDE_ERR_RANGE};
    static const u32 ranges[][2] = {
        {0, 0}, {0, 1}, {0x87654320u, 1}, {0x87654321u, 1},
        {0xffffffffu, 2}, {0, 0xffffffffu}
    };
    for (expected_drive = -1; expected_drive <= IDE_MAX_DRIVES; expected_drive++) {
        for (unsigned i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
            result = results[i];
            check_geometry(SASI_SECTOR_BYTES);
            check_geometry(512);
        }
        for (unsigned i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
            expected_lba = ranges[i][0]; expected_count = ranges[i][1];
            for (result = 0; result <= 1; result++) {
                calls = 0;
                CHECK(platform_ide_range_ok(expected_drive, expected_lba, expected_count) == result);
                CHECK(calls == 1);
            }
        }
    }
    die(0);
}
