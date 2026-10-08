"""f12 startup budget: real exec launch allocation/teardown with real paging/ledger.
Normal control always precedes runtime mutants. File I/O and entry are boundaries.
"""
import pathlib
import struct
import subprocess
import sys
import tempfile
import host32
import test_app_bb_overlap as bb

ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTANTS = [
    ('halve-available', '\n        exec_heap_size = MEM_EXEC_HEAP_MIN;',
     '\n        exec_heap_size = (pgalloc_free_pages() / 2) * PAGE_SIZE;'),
    ('legacy-ceiling', '        guard_a = sbrk_end;',
     '        guard_a = sbrk_end; if (heap_sz > 0x00C00000UL - MEM_PHYS_EXEC_FLOOR - text_sz - MEM_EXEC_STACK_SIZE - 2 * PAGE_SIZE - 0x40000UL) return EXEC_ERR_NOMEM;'),
    ('old-libc-tier', 'sbrk_end = PAGE_ALIGN_UP(load_base + text_sz + bss_sz) + PAGE_SIZE;',
     'sbrk_end = PAGE_ALIGN_UP(load_base + text_sz + bss_sz) + 0x40000;'),
    ('duplicate-shlib-PDE', '            pages++;', '            pages++;\n        if (pde == MEM_SHLIB_BASE >> 22) pages++;'),
    ('omit-extra', '+ exec_ring3_extra_pages();', '+ 0 * exec_ring3_extra_pages();'),
    ('resident-budget', 'exec_heap_size = MEM_SHELL_HEAP_SIZE;', 'exec_heap_size = MEM_EXEC_HEAP_MIN;'),
    ('resident-limit', 'ctx->sbrk_heap_limit = is_shell ? guard_b : sbrk_end;',
     'ctx->sbrk_heap_limit = is_shell ? guard_b - PAGE_SIZE : sbrk_end;'),
    ('VA-after-admit', None, None),
]

def extract(source):
    parts = [bb.slice_out(source, sig) for sig in ('static u32 exec_ring3_extra_pages(', 'static u32 exec_ring3_pages(')]
    parts.append(bb.slice_out((ROOT / 'exec/appmem.c').read_text(), 'void appmem_init('))
    start = source.index('    want_ring3 = !is_shell;')
    end = source.index('    /* ======== setjmp', start)
    layout = source[source.index('    /* ====== Level に応じた'):source.index('    load_addr = (u8 *)load_base;')]
    shell = source[source.index('    /* ヒープ・ガードページ設定 */'):source.index('    {\n        u32 launch_argv')]
    parts.append('''
#define kmalloc budget_kmalloc
#define appslot_start_admit budget_admit
static int budget_launch(int is_shell, u32 image, u32 heap_sz, u32 requested_stack) {
    u32 load_base, max_size, stack_top, guard_a, guard_b, sbrk_end;
    u32 exec_heap_base, exec_heap_size, stack_size = MEM_EXEC_STACK_SIZE, need_pages;
    u32 text_sz = image, bss_sz = 0;
    int want_ring3, gui = 0, launcher_id = 1, id = is_shell ? 1 : 2, launch_cmd_len = 0;
    const char *resolved = "fixture";
    AppSlot *ctx;
    OS32Header header = {0}, *hdr = &header;
    hdr->stack_size = requested_stack;
'''+layout+source[start:end]+shell+'''
    return id;
}
#undef kmalloc
#undef appslot_start_admit
''')
    return '\n'.join(parts)


def run(source):
    with tempfile.TemporaryDirectory(prefix='os32-f12-') as d:
        tmp = pathlib.Path(d)
        fixture = bb.SRC.read_text().replace('void _start(void)', 'void unused_legacy_start(void)')
        fixture = fixture.replace('void shlib_addrspace_detach(struct addrspace *as) { (void)as; }',
            '/* real shlib detach is included below */')
        (tmp / 'budget_fixture.c').write_text(fixture)
        (tmp / 'exec_bb_overlap.inc').write_text(bb.extract(source))
        (tmp / 'budget_launch.inc').write_text(extract(source))
        shlib = (ROOT / 'kernel/shlib.c').read_text()
        (tmp / 'budget_shlib.inc').write_text('\n'.join(bb.slice_out(shlib, sig) for sig in ('int shlib_addrspace_attach(', 'void shlib_addrspace_detach(')))
        (tmp / 'budget_admit.inc').write_text(bb.slice_out((ROOT / 'exec/appslot.c').read_text(), 'int appslot_start_admit('))
        (tmp / 'paging_host_source.c').write_text((ROOT / 'kernel/paging.c').read_text())
        (tmp / 'pgalloc_host_source.c').write_text((ROOT / 'kernel/pgalloc.c').read_text().replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags'))
        try:
            gui = (ROOT / 'userland/tests/gui_demo.bin').read_bytes()
        except FileNotFoundError:
            raise RuntimeError('gui_demo.bin がありません。make programs が要るため起動予算検査を実行できません。') from None
        text, bss = struct.unpack_from('<II', gui, 20)
        image = text + bss
        n = (image + 4095) // 4096
        pdes = len(set(range(512, ((0x80100000 + (n + 1) * 4096 - 1) >> 22) + 1)) | {544, 575})
        need = n + 1 + 16 + 64 + 2 + pdes + 4
        inc = ['-I'+str(ROOT / p) for p in ('tools/tests/host_arch', 'tools/tests', 'include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'sdk/include/os32')]
        exe = tmp / 'budget'
        cmd = ['gcc', *bb.FLAGS, '-ffunction-sections', '-fdata-sections', '-DPHYSMEM_HOST_TEST=1',
               f'-DGUI_IMAGE_BYTES={image}', f'-DGUI_NEED_PAGES={need}', '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
               *inc, '-I'+str(tmp), str(ROOT / 'tools/tests/exec_budget_host.c'), str(ROOT / 'kernel/physmem.c'), str(ROOT / 'kernel/kmalloc.c'), '-o', str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError('compile is not RED:\n'+built.stderr)
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=60)
        return result, {'image_pages': n, 'need_pages': need, 'shlib_data_pages': 4}


def main():
    source = (ROOT / 'exec/exec.c').read_text()
    r, sizes = run(source)
    print(r.stdout, end='')
    assert r.returncode == 0, (r.returncode, r.stderr)
    print('K7', sizes)
    if '--mutate' in sys.argv:
        for name, old, new in MUTANTS:
            if name == 'VA-after-admit':
                a = '            if (heap_sz > guard_b - exec_heap_base) return EXEC_ERR_NOMEM;'
                b = '        if (sbrk_end + PAGE_SIZE > exec_heap_base || exec_heap_size > guard_b - exec_heap_base)\n            return EXEC_ERR_NOMEM;'
                assert source.count(a) == source.count(b) == 1
                changed = source.replace(a, '').replace(b, '')
                marker = '        if (appslot_start_admit(gui, need_pages, pgalloc_free_pages()) < 0) {'
                changed = changed.replace(marker, '        (void)appslot_start_admit(gui, need_pages, pgalloc_free_pages());\n'+a.replace('            if', '        if')+'\n'+b+'\n'+marker)
            else:
                assert source.count(old) == 1, (name, source.count(old))
                changed = source.replace(old, new)
            r, _ = run(changed)
            assert r.returncode == 1 and 'FAIL:' in r.stdout, (name, r.returncode, r.stdout, r.stderr)
            print('RED (runtime):', name)
    return 0

if __name__ == '__main__':
    sys.exit(main())
