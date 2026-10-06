"""T2e e7: two real SDK instances over kernel query/lease and all backends."""
import host32
import argparse
import pathlib
import re
import tempfile
from test_gfx_attach import TARGET_SRCS, SDK_SRCS, HEADERS, CORE
from test_gfx_kernel_fb import ROOT, run_case
from mutpar import run_ordered

FIXTURE = 'tools/tests/gfx_reattach_host.c'
MUTANTS = [
    ('one-return-check', FIXTURE, 'int b = shl_libos32gfx_check();', 'int b = 0;', 'FAIL both_new_generation'),
    ('static-return-check', FIXTURE, 'int a = libos32gfx_check();', 'int a = 0;', 'FAIL both_new_generation'),
    ('failure-no-static-detach', FIXTURE, '        libos32gfx_detach(); /* application rollback */', '', 'FAIL static_token_returned'),
    ('old-bb', CORE, '    gfx_fb = fb;\n    gfx_packed = desc.format', '    static u8 *old_bb;\n    gfx_fb = fb;\n    if (old_bb) gfx_fb.planes[0] = old_bb; else old_bb = gfx_fb.planes[0];\n    gfx_packed = desc.format', 'FAIL old_bb_not_reused'),
]


@host32.control_session
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', choices=['native', 'qemu'], default='native')
    p.add_argument('--mutate', action='store_true')
    a = p.parse_args()
    host32.begin_control(a.mutate, a.runner, ROOT)
    texts = {x: (ROOT/x).read_text() for x in TARGET_SRCS}
    names = set(re.findall(r'\b(gfx_\w+)\s*\(', '\n'.join(texts[x] for x in SDK_SRCS+HEADERS)))
    names.discard('gfx_cpl')
    pattern = re.compile(r'(?<!->)(?<!\.)\b('+'|'.join(sorted(names))+r')\b')
    # First namespace is the e6 fixture's sdk_ drawing exports; its globals
    # keep their names. The second copy prefixes every exported SDK symbol.
    globals_ = ['gfx_api', 'gfx_fb', 'gfx_ready', 'gfx_packed', 'gfx_dirty_suppress', 'gfx_attach_port', 'gfx_unicode_port']
    second = re.compile(r'(?<!->)(?<!\.)(?<!struct )\b('+'|'.join(sorted(names | set(globals_)))+r'|libos32gfx_\w+|utf8_\w+(?=\s*\()|unicode_to_\w+(?=\s*\())\b')
    with tempfile.TemporaryDirectory(prefix='os32-e7-objects-') as cache:
        def run(m=None):
            def stage(tmp, sources):
                for shlib in (False, True):
                    destdir = tmp/'shl' if shlib else tmp
                    destdir.mkdir(exist_ok=True)
                    for path in SDK_SRCS+HEADERS:
                        body = texts[path]
                        if m and m[1] == path:
                            assert body.count(m[2]) == 1
                            body = body.replace(m[2], m[3])
                        if path == CORE:
                            body = body.replace('return cs & 3U;', 'extern unsigned int host_sdk_cpl; return host_sdk_cpl;')
                        if path.endswith('gfx_dump.c'):
                            body = '#pragma GCC diagnostic ignored "-Wsign-compare"\n'+body
                        body = (second.sub(lambda x: 'shl_'+x[0], body) if shlib else
                                pattern.sub(lambda x: 'sdk_'+x[0], body))
                        dest = destdir/pathlib.Path(path).name
                        dest.write_text(body)
                        if path in SDK_SRCS: sources.append(dest)
                fixture = (ROOT/FIXTURE).read_text()
                if m and m[1] == FIXTURE:
                    assert fixture.count(m[2]) == 1
                    fixture = fixture.replace(m[2], m[3])
                # Reuse only hardware/MMU/API adapters from e6, not its run().
                prefix = (ROOT/'tools/tests/gfx_attach_host.c').read_text().split('static void run(void) __attribute__((used));')[0]
                (tmp/'fixture.c').write_text(prefix+'\n'+fixture)
            return run_case(None, a.runner, fixture_body='/* staged below */', source_texts=texts,
                            object_cache=pathlib.Path(cache), stage=stage)
        with host32.control(a.mutate, a.runner, ROOT) as normal:
            if normal:
                r = run()
                print(r.stdout+r.stderr, end='')
                assert r.returncode == 0, r.returncode
        if a.mutate:
            def one(m):
                r = run(m)
                assert r.returncode == 1 and m[4] in r.stdout, (m[0],r.returncode,r.stdout,r.stderr)
                return 'RED '+m[0]
            for msg in run_ordered(one, MUTANTS): print(msg)
            print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} runtime mutants')

if __name__ == '__main__': main()
