/* D3b: real runtime bootinfo and PC-9821 adapter, with IDE state fakes. */
#include "bootinfo.h"
#include "../../drivers/ata/ide.h"
#include "../../init/bootinfo.c"
#include "../../init/bootinfo_check.c"
#include "../../platform/pc98/boot_disk.c"

#define STR1(x) #x
#define STR(x) STR1(x)
#define CHECK(x) do { if (!(x)) { static const char msg[] = "CHECK " STR(__LINE__) ": " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(msg), "d"(sizeof(msg)-1) : "memory"); return 1; } } while (0)

static IdeGeom geom[4];
static IdeInfo info[4];
static int present[4], geom_rc[4], info_rc[4], mode[4];
static char lines[4][BOOTINFO_LINE_MAX];
static unsigned logs;

int ide_get_geom(int drive, IdeGeom *out)
{
    if (drive < 0 || drive >= 4 || geom_rc[drive]) return -1;
    *out = geom[drive];
    return 0;
}
int ide_get_info(int drive, IdeInfo *out)
{
    if (drive < 0 || drive >= 4 || info_rc[drive]) return -1;
    *out = info[drive];
    return 0;
}
int ide_drive_present(int drive) { return present[drive]; }
int ide_addr_mode_of(int drive) { return mode[drive]; }

void *kmemset(void *dst, int val, u32 n)
{
    u8 *p = dst;
    while (n--) *p++ = (u8)val;
    return dst;
}
void kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list ap;
    const char *s;
    unsigned n = 0;
    (void)attr; (void)fmt;
    __builtin_va_start(ap, fmt);
    s = __builtin_va_arg(ap, const char *);
    if (logs >= 4) __builtin_trap();
    while (*s && n < BOOTINFO_LINE_MAX - 1) lines[logs][n++] = *s++;
    lines[logs++][n] = 0;
    __builtin_va_end(ap);
}
static int same(const void *a, const void *b, unsigned n)
{
    const u8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}
static int text_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int test(void)
{
    HddGeom out, expected, before;
    u16 h, s;
    int d, status, queried, valid, have, fail, zero;
    static const int statuses[] = { BOOTINFO_OK, BOOTINFO_ERR_NONE,
                                    BOOTINFO_ERR_MAGIC, BOOTINFO_ERR_SUM };
    /* Every ABI member offset, including padding/reserved, stays fixed. */
#define OFF(m, n) STATIC_ASSERT(__builtin_offsetof(HddGeom, m) == n, off_##m)
    STATIC_ASSERT(sizeof(HddGeom) == 32, geom_size);
    OFF(bios_queried, 0); OFF(bios_valid, 1); OFF(bios_heads, 2); OFF(bios_spt, 3);
    OFF(bios_cyl, 4); OFF(bios_seclen, 6); OFF(ata_def_cyl, 8); OFF(ata_def_heads, 10);
    OFF(ata_def_spt, 12); OFF(ata_cur_cyl, 14); OFF(ata_cur_heads, 16); OFF(ata_cur_spt, 18);
    OFF(ata_w49, 20); OFF(ata_w53, 22); OFF(ata_total, 24); OFF(ata_present, 28);
    OFF(addr_mode, 29); OFF(bios_da, 30); OFF(reserved, 31);
#undef OFF
    kmemset(&out, 0xA5, sizeof(out)); before = out;
    CHECK(hdd_geom_info(0, &out) == OS32_ERR_INVAL && same(&out, &before, 32));
    CHECK(bootinfo_part_geom(0, &h, &s) == -1);
    logs = 0; bootinfo_report(); CHECK(logs == 1); /* no ATA before registration */
    pc98_boot_disk_bind(); pc98_boot_disk_bind();
    CHECK(hdd_geom_info(-1, &out) == OS32_ERR_INVAL && same(&out, &before, 32));
    CHECK(hdd_geom_info(4, &out) == OS32_ERR_INVAL && same(&out, &before, 32));
    CHECK(hdd_geom_info(0, 0) == OS32_ERR_INVAL);
    CHECK(bootinfo_part_geom(-1, &h, &s) == -1);
    CHECK(bootinfo_part_geom(4, &h, &s) == -1);
    for (d = 0; d < 4; d++) {
        geom[d] = (IdeGeom){ 100, 8, 17, 0x200, 1, 90, 16, 63, 123456, 1 };
        info[d].heads = 7; info[d].sectors = 31;
        mode[d] = d; /* check every preserved enum value */
    }
    for (status = 0; status < 4; status++)
    for (queried = 0; queried < 2; queried++)
    for (valid = 0; valid < 2; valid++)
    for (have = 0; have < 2; have++)
    for (fail = 0; fail < 2; fail++)
    for (zero = 0; zero < 3; zero++)
    for (d = 0; d < 4; d++) {
        int bios = d < 2 && statuses[status] == BOOTINFO_OK && queried;
        s_bootinfo.status = statuses[status];
        for (int i = 0; i < 2; i++) {
            s_bootinfo.drive[i] = (struct bootinfo_drive){
                .da = (u8)(0x80 + i), .queried = (u8)queried, .valid = (u8)valid,
                .heads = 4, .spt = 25, .cyl = 256, .seclen = 512
            };
        }
        present[d] = have; geom_rc[d] = fail; info_rc[d] = fail;
        info[d].heads = zero == 1 ? 0 : 7;
        info[d].sectors = zero == 2 ? 0 : 31;
        kmemset(&expected, 0, sizeof(expected));
        expected.bios_da = d < 2 ? (u8)(0x80 + d) : 0;
        if (bios) {
            expected.bios_queried = 1; expected.bios_valid = (u8)valid;
            expected.bios_heads = 4; expected.bios_spt = 25;
            expected.bios_cyl = 256; expected.bios_seclen = 512;
        }
        if (!fail && have) {
            expected.ata_present = 1; expected.ata_def_cyl = 100;
            expected.ata_def_heads = 8; expected.ata_def_spt = 17;
            expected.ata_cur_cyl = 90; expected.ata_cur_heads = 16; expected.ata_cur_spt = 63;
            expected.ata_w49 = 0x200; expected.ata_w53 = 1; expected.ata_total = 123456;
            expected.addr_mode = (u8)d;
        }
        kmemset(&out, 0xA5, sizeof(out));
        CHECK(hdd_geom_info(d, &out) == 0 && same(&out, &expected, 32));
        h = 0xBEEF; s = 0xCAFE;
        int rc = bootinfo_part_geom(d, &h, &s);
        if (bios && valid) {
            CHECK(rc == BOOTINFO_GEOM_BIOS && h == 4 && s == 25);
        } else if (!fail && !zero) {
            CHECK(rc == BOOTINFO_GEOM_IDENTIFY && h == 7 && s == 31);
        } else {
            CHECK(rc == -1 && h == 0xBEEF && s == 0xCAFE);
        }
        CHECK(bootinfo_part_geom(d, 0, 0) == rc);
    }
    /* Report is independent of present, reports only bank0, and keeps BIOS first. */
    s_bootinfo.status = BOOTINFO_ERR_NONE;
    for (d = 0; d < 4; d++) { present[d] = 0; geom_rc[d] = 0; }
    logs = 0; bootinfo_report(); CHECK(logs == 3);
    CHECK(text_eq(lines[1], "[hdd] ata0 def=100/8/17 cur=90/16/63(valid) lba=1 total=123456"));
    CHECK(text_eq(lines[2], "[hdd] ata1 def=100/8/17 cur=90/16/63(valid) lba=1 total=123456"));
    geom[1].w49 = 0; geom[1].w53 = 0; geom_rc[0] = -1;
    logs = 0; bootinfo_report(); CHECK(logs == 2);
    CHECK(text_eq(lines[1], "[hdd] ata1 def=100/8/17 cur=90/16/63(n/a) lba=0 total=123456"));
    s_bootinfo.status = BOOTINFO_OK;
    logs = 0; bootinfo_report(); CHECK(logs == 3);
    CHECK(lines[0][6] != 'a' && lines[1][6] != 'a' && lines[2][6] == 'a');
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
