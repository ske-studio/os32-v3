"""Real link-sh reboot acceptance from PPC_RESET_DESIGN §7."""
CHECK = dict(name='ppc-reset', env=['ppc-cross', 'qemu-ppc', 'socket'], tier=2,
    budget=1200, needs=['make:ppc-ci-image'], full_only=True,
    covers=['Makefile', 'build/**', 'arch/ppc/**', 'platform/powermac/**',
            'drivers/**', 'include/**', 'sdk/**', 'userland/**', 'assets/**',
            'init/**', 'kernel/**', 'mm/**', 'exec/**', 'fs/**', 'gfx/**',
            'net/**', 'input/**', 'kapi/**', 'lib/**', 'tools/ppc/**', 'tools/*.py',
            'tools/gen_unicode.c'],
    basis=['docs/decisions/ppc/PPC_RESET_DESIGN.md §7'],
    env_limit='Requires mac99 QEMU, OpenBIOS, and AF_INET UDP/TCP.')


def run_cases(mutate, build_only=False):
    import os
    from pathlib import Path
    import shutil
    import socket
    import sys
    import tempfile

    sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'ppc'))
    from link_sh_run import ROM, accept_reset, build, cross
    from kboot_accept import mutant_settings
    from test_kapi_binding import generate

    required = [cross(tool) for tool in ('gcc', 'nm', 'objdump', 'ld', 'objcopy')]
    if not build_only:
        required += ['qemu-system-ppc', 'genisoimage']
    missing = [tool for tool in required if not shutil.which(tool)]
    if not build_only and not ROM.is_file():
        missing.append(str(ROM))
    if missing:
        strict = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if strict else 'SKIP') + ' ppc-reset: unavailable '+', '.join(missing))
        return int(strict)

    out = Path(os.environ.get('TMPDIR', '/tmp')).resolve()/'ppc-reset'
    out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=out))
    print('evidence='+str(work), flush=True)
    if not build_only:
        try:
            for kind in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
                with socket.socket(socket.AF_INET, kind) as probe:
                    probe.bind(('127.0.0.1', 0))
        except OSError as exc:
            print('未 ppc-reset: AF_INET unavailable: '+str(exc))
            return 3

    def image(folder, *settings):
        return build(folder/'kernel', 'IMAGE=kboot-link-sh',
                     'KB_SH_OUT='+str(folder/'user'), *settings)

    timeout = 180
    elf = image(work/'normal')
    if not build_only:
        accept_reset(elf, work/'cuda', timeout, 'cuda')
    if mutate:
        for mutation in ('reboot-absent', 'cuda-reset-byte'):
            folder = work/mutation
            folder.mkdir()
            if mutation == 'reboot-absent':
                # Use the normal generator with the existing private SDK copy.
                kernel = folder/'kernel'
                kernel.mkdir()
                gen = generate(kernel, mutation)
                (gen/'stamp').touch()
                settings = []
            else:
                settings = mutant_settings(folder, mutation, 'drivers/macintosh/via.c',
                                           'CUDA_RESET = 0x11', 'CUDA_RESET = 0xff')
            if mutation == 'cuda-reset-byte':
                source = folder/'mutant-cuda-reset-byte.c'
                text = source.read_text()
                anchor = '                write8(v, SR, out[i]);'
                if text.count(anchor) != 1:
                    raise RuntimeError('cuda-reset-byte: send anchor must be unique')
                text = 'volatile unsigned reset_sent_byte;\n' + text.replace(
                    anchor, anchor+'\n                reset_sent_byte = 0x100 | out[i];')
                source.write_text(text)
            mutant = image(folder, *settings)
            if build_only:
                print('BUILT mutation '+mutation, flush=True)
            else:
                accept_reset(mutant, folder/'guest', timeout, 'cuda', mutation)
        if not build_only:
            accept_reset(elf, work/'cuda-control', timeout, 'cuda')
    if not build_only:
        accept_reset(elf, work/'pmu', timeout, 'pmu')
    return 0


def run(ctx):
    return run_cases(ctx.mutate)


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    raise SystemExit(run_cases(args.mutate, args.build_only))
