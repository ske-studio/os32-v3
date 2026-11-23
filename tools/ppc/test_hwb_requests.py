"""PowerMac HW_OP §5, stages 5b, 7b and 7c. Invoke the real request dispatcher."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def fill(work, mutate):
    (work/'shell.h').write_text(
        'struct fill_api { int (*kdebug_call)(u32,void *,u32); };\n'
        'static struct fill_api *g_api;\n')
    path = Path('userland/shell/hwb_exec.c')
    source = (ROOT/path).read_text()
    anchor = 'platform_hwb_fill_check(r->addr, i->width, r->count * i->width)'
    assert source.count(anchor) == 1
    (work/path).parent.mkdir(parents=True)
    for narrow in ([False, True] if mutate else [False]):
        (work/path).write_text(source.replace(anchor, anchor.replace('r->count * ', ''))
                               if narrow else source)
        subprocess.run(['gcc', '-m32', '-O2', '-ffreestanding', '-fno-builtin', '-march=i386',
                        '-fno-pie', '-no-pie', '-nostdinc', '-nostdlib', '-static',
                        '-I'+str(work), '-I.', '-Iinclude', '-Iplatform/powermac',
                        '-Iuserland/shell', '-Isdk/include/os32',
                        'tools/ppc/hwb_fill_host.c', '-o', work/'fill'], cwd=ROOT, check=True)
        result = subprocess.run(['qemu-i386', work/'fill'], capture_output=True, timeout=10)
        if narrow:
            assert result.returncode == 1 and b'FAIL:fill-last-element: !bus.bus_accesses' in result.stdout
            print('RED fill checks only the first element')
        else:
            assert result.returncode == 0, result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'kdebug/hwb_access.c').read_text()
    cases = [('normal', source, 0)]
    if args.mutate:
        for name, old, new, rc in [
                ('check-reads', 'platform_hwb_phys_check(req->addr, width, bytes)',
                 'platform_hwb_phys(&(struct hw_req){.action=HW_READ, .width=width, .addr=req->addr})', 17),
                ('port-ok', 'platform_hwb_io(req)', 'HW_OK', 13)]:
            assert source.count(old) == 1
            cases.append((name, source.replace(old, new), rc))
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        fill(work, args.mutate)
        (work/'kdebug').mkdir()
        for name, code, expected in cases:
            (work/'kdebug/hwb_access.c').write_text(code)
            subprocess.run(['gcc', '-m32', '-O2', '-ffreestanding', '-fno-pie', '-no-pie',
                            '-nostdinc', '-nostdlib', '-static', '-I'+tmp, '-I.', '-Iinclude',
                            '-Iarch/ppc', '-Ikernel/module', '-Ikdebug', '-Isdk/include/os32', '-Iplatform/powermac',
                            'tools/ppc/hwb_requests_host.c', '-o', work/'test'], cwd=ROOT, check=True)
            rc = subprocess.run(['qemu-i386', work/'test'], timeout=10).returncode
            assert rc == expected, (name, rc, expected)
            print(name, rc)
    print("PowerMac request, range and fill tests PASS")


if __name__ == '__main__':
    main()
