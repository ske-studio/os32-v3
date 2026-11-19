"""Common grid through the real console stream and PowerMac backend."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTANTS = [
    ('park before attach', 'if (!c || !c->ready || !c->target) return 0;', 'if (!c) return 0;'),
    ('park discards cells', 'c->target = NULL;', 'fbcon_detach(c);'),
    ('hidden cursor', '(((s->attr & 1) && (ink ^ inverse)) ^ cursor)', '(s->attr & 1) && (ink ^ inverse ^ cursor)'),
    ('cursor clamp', 'x = c->geometry.cols - 1;', 'x = c->geometry.cols;'),
    ('hidden move', 'if (!c->visible) return;', 'if (0) return;'),
    ('same position', 'if (x == ox && y == oy) return;', 'if (0) return;'),
    ('same shape', 'if (c->visible == !!visible) return;', 'if (0) return;'),
    ('scroll unchanged', 'next.part == old.part) continue;', 'next.part == old.part && 0) continue;'),
    ('scroll amount', 'if (n > rows) n = rows;', 'n = 1;'),
    ('wide cursor overlay', '(s->part == 1 && x + 1 == c->cursor_x)', '(0)'),
    ('wide cursor erase', 'w = c->cells[y * c->geometry.cols + x].part ? 2 : 1;\n    redraw(c, x, y, w, 1);\n}\n\nvoid fbcon_cursor_at', 'w = 1;\n    redraw(c, x, y, w, 1);\n}\n\nvoid fbcon_cursor_at'),
    ('colors swapped', '? c->fg : c->bg;', '? c->bg : c->fg;'),
    ('scroll redraw removed', 'paint(c, x, y, pixels);', '(void)cols;'),
    ('width fixed', 'cols = t->width / 8;', 'cols = 80;'),
    ('top ignored', 'c->top = top_pixels;', 'c->top = 0;'),
    ('wide trailing cell', 'if (wide) c->cells[y * c->geometry.cols + x + 1] =', 'if (0) c->cells[y * c->geometry.cols + x + 1] ='),
    ('bit order', '0x80u >> bit', '1u << bit'),
    ('scroll short', 'i < cols * (rows - n)', 'i < cols * (rows - n - 1)'),
    ('scroll long', 'i < cols * (rows - n)', 'i < cols * rows'),
    ('old cursor', 'redraw_cursor(c, ox, oy);', '(void)ox; (void)oy;'),
]
TEXT = ROOT / 'platform/powermac/console_text.c'
TEXT_MUTANTS = [
    ('nonowner exit', 'id == appslot_gfx_owner()', '(void)id, 1'),
    ('duplicate return', '!pm_text_was_up || pm_console_text_ready()', '!pm_text_was_up'),
]

def build_run(tmp, source, console_text):
    (tmp / 'console_text.c').write_text(console_text)
    (tmp / 'fbcon_source.inc').write_text(source)
    exe = tmp / 'fbcon'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O0', '-ffreestanding',
        '-fno-builtin', '-fno-pie', '-no-pie', '-fno-stack-protector', '-nostdlib', '-static',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare', '-D__KERNEL_BUILD__',
        '-I'+str(tmp), '-I'+str(ROOT/'platform/powermac'), '-I'+str(ROOT), '-I'+str(ROOT/'tools/tests'), *include_flags(ROOT),
        str(tmp/'console_text.c'), str(ROOT/'platform/powermac/display.c'),
        str(ROOT/'tools/tests/fbcon_host.c'), str(ROOT/'platform/powermac/console.c'),
        str(ROOT/'kernel/console/console.c'), str(ROOT/'lib/utf8.c'), str(ROOT/'lib/kstring_c.c'),
        str(ROOT/'kernel/console/font_source_kcg.c'), str(ROOT/'lib/lz4.c'), '-o',str(exe)],check=True)
    return host32.run([str(exe)], runner='qemu', capture_output=True,text=True)
def cross_compile(tmp):
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
    if not cc.is_file():
        print('SKIP fbcon PPC cross gcc unavailable', flush=True)
        return
    for source in ('kernel/console/fbcon.c', 'platform/powermac/console.c'):
        subprocess.run([str(cc), '-std=gnu11', '-mcpu=7450', '-m32', '-mbig-endian',
            '-msoft-float', '-mno-altivec', '-msdata=none', '-ffreestanding',
            '-fno-pie', '-fno-builtin', '-fno-stack-protector', '-nostdlib',
            '-O2', '-Wall', '-Wextra', '-Werror', '-Werror=vla',
            '-D__cdecl=', '-D__KERNEL_BUILD__', '-I'+str(ROOT),
            '-I'+str(ROOT/'include'), '-I'+str(ROOT/'arch/ppc'),
            '-I'+str(ROOT/'sdk/include/os32'), '-I'+str(ROOT/'kernel/console'),
            '-c', str(ROOT/source), '-o', str(tmp/(Path(source).stem+'.o'))], check=True)
    print('PASS fbcon PPC cross compile', flush=True)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    source=(ROOT/'kernel/console/fbcon.c').read_text()
    with tempfile.TemporaryDirectory(prefix='fbcon-') as directory:
        tmp=Path(directory)
        cross_compile(tmp)
        result=build_run(tmp,source,TEXT.read_text())
        print(result.stdout,end='',flush=True)
        assert result.returncode==0,(result.returncode,result.stderr)
        if args.mutate:
            for label,old,new in TEXT_MUTANTS:
                original = TEXT.read_text()
                assert original.count(old) == 1, label
                result = build_run(tmp, source, original.replace(old, new))
                assert result.returncode == 1 and 'FAIL ' in result.stdout, (label, result)
                print('RED runtime: ' + label + ': ' + result.stdout.strip(), flush=True)
            for label,old,new in MUTANTS:
                assert source.count(old)==1,(label,'anchor')
                result=build_run(tmp,source.replace(old,new),TEXT.read_text())
                assert result.returncode==1 and 'FAIL ' in result.stdout,(label,result.returncode,result.stdout,result.stderr)
                print('RED runtime: '+label+': '+result.stdout.strip(),flush=True)
if __name__=='__main__': main()
