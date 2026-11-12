"""Exercise the actual image loader with the PPC publication boundary recorded."""
CHECK = dict(name='ppc-exec-publish', tier=1,
    covers=['exec/exec_image.inc', 'arch/ppc/exec_cache.h', 'arch/x86/exec_cache.h',
            'tools/ppc/test_exec_publish.py'], needs=[], budget=1)
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'exec/exec_image.inc').read_text()
ANCHOR = '        arch_exec_publish(P2V(pa), n);'
HARNESS = r'''
#include <stdint.h>
typedef uint32_t u32;
typedef unsigned char u8;
#define PAGE_SIZE 4096u
#define P2V(pa) ((void *)(uintptr_t)(pa))
#define EXEC_ERR_NOT_FOUND -1
#define EXEC_ERR_INVALID -2
typedef struct { u32 pd_phys; } Space;
typedef struct { u32 load_addr; int cpl3; Space *as; } AppSlot;
typedef struct { u32 header_size, text_size, bss_size; } OS32Header;
static u32 reads, pubs;
static int vfs_open(const char *p, int m) { (void)p; (void)m; return 1; }
static int vfs_seek(int fd, int o, int w) { (void)fd; (void)o; (void)w; return 0; }
static void vfs_close(int fd) { (void)fd; }
static int as_va_to_pa(u32 pd, u32 va, u32 *pa) { (void)pd; *pa = 0x5000u + ((va / PAGE_SIZE) - 1u) * 0x2000u + (va & 4095u); return 0; }
static int vfs_read_fd(int fd, void *p, u32 n) { (void)fd; (void)p; reads++; return (int)n; }
void arch_exec_publish(void *p, u32 n) {
    static const u32 addr[] = {0x5100, 0x7000, 0x9000};
    static const u32 size[] = {3840, 4096, 64};
    if (pubs >= 3 || reads != pubs + 1 || (uintptr_t)p != addr[pubs] || n != size[pubs]) __builtin_trap();
    pubs++;
}
static void *kmemset(void *p, int c, u32 n) { (void)c; (void)n; return p; }
#include "image.inc"
int main(void) {
    Space as = {1}; AppSlot slot = {0x1100, 1, &as};
    OS32Header hdr = {64, 8000, 0};
    return exec_load_image(&slot, "x", &hdr) || reads != 3 || pubs != 3;
}
'''

def execute(source):
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        (tmp / 'image.inc').write_text(source)
        (tmp / 'test.c').write_text(HARNESS)
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-int-to-pointer-cast', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
        return subprocess.run([str(tmp / 'test')], check=False).returncode

if __name__ == '__main__':
    assert execute(SOURCE) == 0
    print('PASS: each physical alias and length published after its read')
    if '--mutate' in sys.argv:
        assert SOURCE.count(ANCHOR) == 1
        assert execute(SOURCE.replace(ANCHOR, '')) == 1
        print('publish omitted: runtime RED')

def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
