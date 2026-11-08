"""U3/U4: actual widget, editor refresh and filer selection damage over pixel storage."""
CHECK = dict(name='widget-scroll-host', tier=1,
    covers=['tools/tests/test_widget_scroll.py', 'tools/tests/widget_scroll_host.rs',
            'tools/tests/os32api_host.py', 'userland/rust/libos32gui/src/widget*.rs',
            'userland/rust/libos32gui/src/uistate.rs', 'userland/rust/libos32gui/src/layout.rs',
            'userland/rust/libos32gui/src/textcore.rs', 'userland/rust/libos32gui/src/utf8core.rs',
            'userland/rust/edit_gui/src/lib.rs', 'userland/rust/edit_gui/src/doc.rs',
            'userland/rust/filer/src/lib.rs', 'userland/rust/libos32gui/src/shlib.rs',
            'userland/rust/libos32gui_stub/src/widget.rs', 'userland/rust/libos32gui_stub/src/scroll.rs',
            'sdk/rust/os32api/src/**'],
    needs=[], budget=12.0)
import argparse
from pathlib import Path
import subprocess
import tempfile
import os32api_host

ROOT = Path(__file__).resolve().parents[2]
GUI = 'userland/rust/libos32gui/src/'
MUTANTS = [
    ('selection-row', GUI+'widget_scroll.rs', 'for sel in [old_sel, w.sel]', 'for sel in [w.sel, w.sel]', 'list pixels'),
    ('exposed-direction', GUI+'widget_scroll.rs', 'let exposed = if dy < 0', 'let exposed = if dy > 0', 'textarea pixels'),
    ('error-full', GUI+'widget_scroll.rs', '    } else {\n        invalidate(idx);\n    }', '    } else {\n        // missing fallback\n    }', 'textarea pixels'),
    ('editor-no-scroll', 'userland/rust/edit_gui/src/lib.rs', 'if self.top != old_top {', 'if false {', 'editor must scroll'),
    ('filer-no-old-selection', 'userland/rust/filer/src/lib.rs', '                self.repaint(self.row_rect(&r, old_sel));', '', 'filer pixels'),
]

def function(src, signature):
    a = src.index(signature)
    start = src.index('{', a)
    depth = 1
    end = start+1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[a:end]


def method(src, name):
    return function(src, '    fn '+name+'(')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mutate', action='store_true')
    args = p.parse_args()
    paths = [GUI+n+'.rs' for n in ('widget', 'widget_scroll', 'uistate', 'layout', 'textcore', 'utf8core')]
    paths += ['userland/rust/edit_gui/src/lib.rs', 'userland/rust/edit_gui/src/doc.rs', 'userland/rust/filer/src/lib.rs']
    paths += [GUI+'shlib.rs', 'userland/rust/libos32gui_stub/src/widget.rs',
              'userland/rust/libos32gui_stub/src/scroll.rs']
    original = {p: (ROOT/p).read_text() for p in paths}
    with tempfile.TemporaryDirectory(prefix='widget-scroll-') as tmp:
        base = Path(tmp)
        rlib = os32api_host.build(base)
        def run_case(m=None, label='normal'):
            texts = dict(original)
            if m:
                label, path, old, new, _ = m
                assert texts[path].count(old) == 1, label
                texts[path] = texts[path].replace(old, new)
            dest = base/label
            dest.mkdir()
            root = ''
            for path in paths[:6]:
                name = Path(path).stem
                (dest/(name+'.rs')).write_text(texts[path])
                if name != 'widget_scroll':
                    root += f'mod {name};\n'
            (dest/'doc.rs').write_text(texts['userland/rust/edit_gui/src/doc.rs'])
            root += 'mod doc;\n'
            root += (ROOT/'tools/tests/widget_scroll_host.rs').read_text()
            root += '\nimpl Editor {\n'+ '\n'.join(method(texts[paths[6]], n) for n in ('refresh', 'view_geom'))+'\n}\n'
            root += '\nimpl Filer {\n'+ '\n'.join(method(texts[paths[8]], n) for n in ('repaint_sel', 'row_rect', 'rows_band', 'visible_rows'))+'\n}\n'
            root += '\nmacro_rules! gate { ($v:expr) => {}; }\n'
            root += 'fn r0(r:client::GuiResult<()>) -> i32 { r.map(|_|0).unwrap_or_else(|e|e.0) }\n'
            root += "unsafe fn slice<'a>(p:*const u8,n:u32)->&'a [u8] { if n==0 { &[] } else { core::slice::from_raw_parts(p,n as usize) } }\n"
            for name in ('os32gui_scroll', 'os32gui_w_textarea_scroll'):
                root += function(texts[paths[9]], 'pub extern "C" fn '+name+'(')+'\n'
            root += 'mod stub_widget { use crate::{client::{GuiResult,GuiErr},WidgetId}; use os32api::gui::stub as sh; use crate::client::ok0;\n'
            root += function(texts[paths[10]], 'pub fn textarea_scroll(')+'\n}\n'
            root += 'mod stub_scroll { use crate::{client::{GuiResult,ok0},Rect}; use os32api::gui::stub as sh;\n'
            root += function(texts[paths[11]], 'pub fn scroll(')+'\n}\n'

            (dest/'main.rs').write_text(root)
            c = subprocess.run(['rustc', '--edition=2021', '--test', str(dest/'main.rs'), '--extern', 'os32api='+str(rlib), '-o', str(dest/'test')], capture_output=True, text=True)
            assert c.returncode == 0, c.stderr
            return subprocess.run([str(dest/'test'), '--test-threads=1'], capture_output=True, text=True)
        r = run_case()
        print(r.stdout+r.stderr, end='')
        assert r.returncode == 0
        if args.mutate:
            for m in MUTANTS:
                r = run_case(m)
                assert r.returncode == 101 and m[4] in r.stdout, (m[0], r.stdout, r.stderr)
                print('RED '+m[0])
            r = run_case(label='restored')
            assert r.returncode == 0, r.stdout+r.stderr
            print('GREEN restored')

if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    return subprocess.call(['python3', '-B', 'tools/tests/test_widget_scroll.py', *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)
