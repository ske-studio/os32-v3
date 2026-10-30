"""V1: real build dependency loading and the kernel formatter's product version."""
import os
import pathlib
import re
import subprocess
import tempfile
import time

import mutpar
from ver_test_utils import ROOT, arguments

CONSUMER = 'kernel/ver_format.c'
VERSION_USE = '"PC-9821 OS32 v" SYS_VERSION'
OBJECTS = ('build/out/obj/kernel/ver_format.o', 'build/out/obj/kernel/module/module.o')
# These directories contain all outputs/generators reachable from OBJECTS.
# Copy tracked sources, excluding .o/.d/target caches; other inputs are read-only
# links. Neither config edits nor make outputs touch the invoking worktree.
BUILD_REAL = ('Makefile', 'build', 'include', 'kernel',
              'sdk', 'kapi', 'exec', 'tools')


def check_version(config):
    values = re.findall(r'^\s*#\s*define\s+SYS_VERSION\s+"([^"]*)"', config, re.M)
    assert values == ['3.0-dev'], values
    assert len(values[0]) <= 20


def heading(version):
    return ('PC-9821 OS32 v' + version + ' (Ring3 Native)\0').encode('ascii')


def rebuild(config, edits=None):
    with tempfile.TemporaryDirectory(prefix='ver-version-') as directory:
        tree = mutpar.mutant_tree(ROOT, pathlib.Path(directory)/'tree',
                                  {'include/config.h': config, **(edits or {})},
                                  real=BUILD_REAL)
        env = dict(os.environ)
        env['PATH'] = str(pathlib.Path(env['CROSS_DIR'])/'bin') + os.pathsep + env['PATH']
        # Respect the actual compiler flags, pattern rules, generated .d files,
        # and Makefile's DEPFILES discovery/include; no replacement make graph.
        def make():
            result = subprocess.run(['make', '--no-print-directory', '-j1', *OBJECTS],
                                    cwd=tree, env=env, capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr

        make()
        objects = [tree/path for path in OBJECTS]
        before = [path.stat().st_mtime_ns for path in objects]
        assert heading('3.0-dev') in objects[0].read_bytes()
        make()
        assert before == [path.stat().st_mtime_ns for path in objects], 'unchanged inputs rebuilt'
        header = tree/'include/config.h'
        header.write_text(config.replace('"3.0-dev"', '"9.9-test"'))
        stamp = time.time_ns()
        assert stamp > max(before)
        os.utime(header, ns=(stamp, stamp))
        make()
        for path, old_time in zip(objects, before):
            assert path.stat().st_mtime_ns > old_time, f'{path.name}: missing config-only rebuild'
        content = objects[0].read_bytes()
        assert heading('9.9-test') in content and heading('3.0-dev') not in content, 'consumer ignored changed SYS_VERSION'
        print('PASS SYS_VERSION: actual Makefile rebuilt kernel formatter and module after config-only edit')


def main():
    args = arguments()
    config = (ROOT/'include/config.h').read_text()
    check_version(config)
    rebuild(config)
    if args.mutate:
        changed = config.replace('"3.0-dev"', '"9.9-test"')
        try:
            check_version(changed)
        except AssertionError:
            print('RED: product version=9.9-test')
        else:
            raise AssertionError('version mutant survived')
        makefile = (ROOT/'Makefile').read_text()
        anchor = '-include $(DEPFILES)'
        assert makefile.count(anchor) == 1
        consumer = (ROOT/CONSUMER).read_text()
        assert consumer.count(VERSION_USE) == 1
        for label, edits, expected in (
            ('dependency loading removed', {'Makefile': makefile.replace(anchor, '# dependency loading removed')},
             'missing config-only rebuild'),
            ('consumer hardcodes version', {CONSUMER: consumer.replace(VERSION_USE, '"PC-9821 OS32 v" "3.0-dev"', 1)},
             'consumer ignored changed SYS_VERSION'),
        ):
            try:
                rebuild(config, edits)
            except AssertionError as error:
                assert expected in str(error), (label, str(error))
                print('RED: '+label)
            else:
                raise AssertionError(label+' survived')


if __name__ == '__main__':
    main()
