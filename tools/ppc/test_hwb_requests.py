"""PowerMac HW_OP §5, stages 5b, 7b and 7c. Invoke the real request dispatcher."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cases = [('normal', None, None, None, 0)]
    if args.mutate:
        cases += [
            ('check-reads', 'sys/kern/kdebug/hwb_access.c', 'platform_hwb_phys_check(req->addr, width, bytes)',
             'platform_hwb_phys(&(struct hw_req){.action=HW_READ, .width=width, .addr=req->addr})', 17),
            ('port-ok', 'sys/kern/kdebug/hwb_access.c', 'platform_hwb_io(req)', 'HW_OK', 13),
            ('short-fill-request', 'userland/shell/hwb_exec.c', 'r->count * i->width)',
             'i->width)', 1),
            ('short-fill-lookup', 'sys/platform/powermac/hwb_resource.c', 'allows(lookup(addr, bytes)',
             'allows(lookup(addr, width)', 1),
            ('unverified-gmac', 'sys/platform/powermac/hwb_resource.c', 'gmac_verified && f->device',
             'f->device', 29),
        ]
    for name, path, old, new, expected in cases:
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work/'shell.h').write_text(
                'struct fill_api { int (*kdebug_call)(u32,void *,u32); };\n'
                'static struct fill_api *g_api;\n')
            if path:
                source = (ROOT/path).read_text()
                assert source.count(old) == 1, name
                (work/path).parent.mkdir(parents=True)
                (work/path).write_text(source.replace(old, new))
            subprocess.run(['gcc', '-m32', '-O2', '-ffreestanding', '-fno-builtin', '-march=i386',
                            '-fno-pie', '-no-pie', '-nostdinc', '-nostdlib', '-static',
                            '-I'+tmp, '-I.', '-Iinclude', '-Iarch/ppc', '-Ikernel/module',
                            '-Ikdebug', '-Isdk/include/os32', '-Iplatform/powermac', '-Iuserland/shell',
                            'tools/ppc/hwb_fill_host.c', 'tools/ppc/hwb_requests_host.c',
                            '-o', work/'test'], cwd=ROOT, check=True)
            result = subprocess.run(['qemu-i386', work/'test'], capture_output=True, timeout=10)
            assert result.returncode == expected, (name, result.returncode, result.stdout)
            if expected == 1:
                assert b'FAIL:fill-last-element: fill_stores() == 0' in result.stdout
            print(name, 'RED' if expected else 'PASS')
    print("PowerMac request, range and fill tests PASS")


if __name__ == '__main__':
    main()
