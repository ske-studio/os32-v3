#include "sys/hal/gfx/sync/sync_int.h"
#include "vfs.h"

__asm__(".pushsection .rodata\n"
        "profile: .incbin \"assets/gfx/sync.cfg\"\n.byte 0\n.popsection");
extern const char profile[];
static const char *input = profile, *cursor;

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int vfs_open(const char *path, int flags)
{
    cursor = input;
    return cursor && equal(path, "/etc/gfx/sync.cfg") && flags == O_RDONLY ? 1 : -1;
}

int vfs_read_fd(int fd, void *buf, u32 n)
{
    u32 i = 0;
    if (fd != 1) return -1;
    while (i < n && *cursor) ((char *)buf)[i++] = *cursor++;
    return (int)i;
}

void vfs_close(int fd) { (void)fd; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

static int lookup(const char *key, int rc, const char *match, u8 unit, u32 gap, u32 read_ns)
{
    struct gfxsync_cfg c;
    return gfxsync_cfg_lookup(key, &c) == rc && equal(c.key, match) &&
        c.mode == GFXSYNC_SHADOW && c.unit == unit && c.flush == GFXSYNC_F_NONE &&
        c.method == (gap ? GFXSYNC_M_GAP : GFXSYNC_M_PLAIN) && c.gap_ns == gap &&
        c.read == GFXSYNC_R_SHADOW && c.read_ns == read_ns;
}

static int check(void)
{
    if (!lookup("pegc", 0, "pegc", 4, 0, 0)) return 1;
    if (!lookup("pci:1023:9397", 0, "pci:1023:*", 4, 80, 1500)) return 2;
    if (!lookup("pci:10de:0221", OS32_ERR_NOTFOUND, "", 1, 0, 0)) return 3;
    input = 0;
    if (!lookup("pegc", OS32_ERR_NOTFOUND, "", 1, 0, 0)) return 4;
    input = "[pci:1023:*]\nunit=4\n[pci:1023:9397]\nunit=2\n";
    if (!lookup("pci:1023:9397", 0, "pci:1023:9397", 2, 0, 0)) return 5;
    input = "[pci:1023:*]\nunit=4\n[pci:1023:9397]\nunit=3\n";
    if (!lookup("pci:1023:9397", OS32_ERR_NOTFOUND, "", 1, 0, 0)) return 6;
    return 0;
}

void _start(void)
{
    int rc = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
