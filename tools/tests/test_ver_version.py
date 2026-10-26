"""V1: one product version definition; header-only edits rebuild a consumer."""
import os
import pathlib
import re
import subprocess
import tempfile
from ver_test_utils import ROOT, arguments


def check_version(config):
    values = re.findall(r'^\s*#\s*define\s+SYS_VERSION\s+"([^"]*)"', config, re.M)
    assert values == ['3.0-dev'], values
    assert len(values[0]) <= 20


def rebuild(config):
    with tempfile.TemporaryDirectory(prefix='ver-version-') as directory:
        tmp = pathlib.Path(directory)
        header = tmp/'config.h'
        header.write_text(config)
        (tmp/'kernel_version.c').write_text('#include "config.h"\nconst char kernel_version[] = SYS_VERSION;\n')
        (tmp/'Makefile').write_text('kernel_version.o: kernel_version.c\n\t$(CC) -m32 -ffreestanding -MMD -c $< -o $@\n-include kernel_version.d\n')
        env = dict(os.environ, CC=str(pathlib.Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'))
        def make(*args):
            return subprocess.run(['make', '--no-print-directory', '-C', str(tmp), *args],
                                  env=env, check=True, capture_output=True, text=True)
        make()
        obj = tmp/'kernel_version.o'
        before = obj.stat().st_mtime_ns
        assert b'3.0-dev\0' in obj.read_bytes()
        assert 'config.h' in (tmp/'kernel_version.d').read_text()
        header.write_text(config.replace('"3.0-dev"', '"9.9-test"'))
        # Explicit timestamps avoid dependence on wall-clock filesystem granularity.
        os.utime(header, ns=(before+2_000_000_000, before+2_000_000_000))
        os.utime(obj, ns=(before-2_000_000_000, before-2_000_000_000))
        make()
        assert b'9.9-test\0' in obj.read_bytes() and b'3.0-dev\0' not in obj.read_bytes()
        print('PASS SYS_VERSION: one 3.0-dev definition, config-only edit rebuilds kernel consumer')


def main():
    args = arguments()
    config = (ROOT/'include/config.h').read_text()
    check_version(config)
    # The real kernel build also records its dependency on the product header.
    assert 'include/config.h' in (ROOT/'kernel/module.d').read_text()
    rebuild(config)
    if args.mutate:
        changed = config.replace('"3.0-dev"', '"9.9-test"')
        try:
            check_version(changed)
        except AssertionError:
            print('RED: product version=9.9-test')
        else:
            raise AssertionError('version mutant survived')


if __name__ == '__main__':
    main()
