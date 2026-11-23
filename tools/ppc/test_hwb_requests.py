"""PowerMac HW_OP §5, stages 5b and 7c. Invoke the real request dispatcher."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


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


if __name__ == '__main__':
    main()
