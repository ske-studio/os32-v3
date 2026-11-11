#include "sync_int.h"
#include "vfs.h"
static const char *file;
static unsigned pos, size;
static int errors;
int vfs_open(const char *path, int flags) { (void)path; (void)flags; pos = 0; return file ? 1 : -1; }
int vfs_read_fd(int fd, void *buf, u32 n) { u8 *p = buf; unsigned i = 0; (void)fd; while (i < n && pos < size) p[i++] = file[pos++]; return (int)i; }
void vfs_close(int fd) { (void)fd; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; errors++; }
static void set(const char *s) { file = s; size = 0; if (s) while (s[size]) size++; }
static int run(void)
{
    struct gfxsync_cfg c;
    set("[PCI:1023:*]\nmode=shadow unit=4 method=GAP gap_ns=80 flush=NONE read_ns=1500 read=SHADOW ;x\n[PCI:1023:ABCD]\nunit=2\n[pegc]\nunit=4\n");
    if (gfxsync_cfg_lookup("pci:1023:abcd", &c) || c.unit != 2 || c.key[9] != 'a') return 1;
    if (gfxsync_cfg_lookup("PCI:1023:5678", &c) || c.unit != 4 || c.gap_ns != 80 || c.read_ns != 1500 || c.key[9] != '*') return 2;
    if (gfxsync_cfg_lookup("PEGC", &c) || c.unit != 4) return 3;
    if (gfxsync_cfg_lookup("other", &c) != OS32_ERR_NOTFOUND || c.unit != 1 || c.key[0]) return 4;
    set("[pegc]\nunit=3\n"); errors = 0;
    if (gfxsync_cfg_lookup("pegc", &c) != OS32_ERR_NOTFOUND || c.unit != 1 || !errors) return 5;
    set("[pegc]\ngap_ns=100001\n");
    if (gfxsync_cfg_lookup("pegc", &c) != OS32_ERR_NOTFOUND) return 6;
    set(0); if (gfxsync_cfg_lookup("pegc", &c) != OS32_ERR_NOTFOUND || c.mode != GFXSYNC_SHADOW) return 7;
    return 0;
}
void _start(void) { int rc = run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
