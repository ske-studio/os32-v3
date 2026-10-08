"""Compile real generated wrappers with the real B1 managed walk (ILP32).
Only private generated/source copies are mutated; compiler failure is not RED.
"""
TARGET_SRC = ['sdk/gen_kapi.py', 'sdk/kapi.json', 'exec/exec.c', 'exec/ring3_str.c',
              'exec/access_walk.c', 'exec/redir_access.c', 'kapi/kapi_sys.c', 'tools/tests/kapi_ranges_host.c']
import argparse
import json
import pathlib
import subprocess
import tempfile
import test_access_walk as walk
import test_kapi_out as generator

ROOT = walk.ROOT
MUTANTS = (
    ('mem_stat out none', 'schema', None, None, 'mem_stat NULL'),
    ('legacy VRAM exception restored', 'exec', '    return 0;\n}', '    if (p >= 0xA0000UL && p < 0xC0000UL) return 1;\n    return 0;\n}', 'unleased VRAM refused'),
    ('output NULL bypass', 'generated', '((u32)(n))', '((p) ? (u32)(n) : 0u)', 'output NULL'),
    ('signed output NULL bypass', 'generated', '(((int)(n) > 0) ? (u32)(n) : 0u)', '(((p) && (int)(n) > 0) ? (u32)(n) : 0u)', 'signed output NULL'),
    ('input first byte', 'generated', 'ring3_user_range_ok(p, len)', 'ring3_user_range_ok(p, 1)', 'input tail'),
    ('early NULL rejected', 'exec', 'if (p == 0) return 1;', 'if (p == 0) return 0;', 'sys_ls NULL ctx'),
    ('device read NULL bypass', 'generated', 'if (count > 0) {', 'if (count > 0 && buf) {', 'device NULL'),
    ('input multiplication wrap', 'generated', 'if (n > (0xFFFFFFFFUL / unit))', 'if (0)', 'input multiplication'),
)

def run(runner, mutant=None, fixture_name="kapi_ranges_host.c"):
    with tempfile.TemporaryDirectory(prefix='kapinull-') as directory:
        data = json.loads(generator.KAPI_JSON.read_text())
        no_out = mutant and mutant[1] == 'schema'
        if no_out:
            entry = next(a for a in data['api'] if a['name'] == 'mem_stat')
            assert entry['out'] == [{'arg': 'out', 'len': 'size'}]
            entry['out'] = 'none'
        rc, output, generated = generator.gen(directory, data)
        assert rc == 0, output
        sources = {k: (ROOT / p).read_text() for k, p in walk.FILES.items()}
        sources['generated'] = (generated / 'kapi/kapi_generated.c').read_text()
        source = (ROOT / 'exec/exec.c').read_text()
        sources['exec'] = source[source.index('int ring3_ptr_ok('):source.index('\n#include "ksetjmp.h"')]
        # Execute the actual dispatcher's early-check block, with generated
        # masks/argument sizes. The rest of int80 needs guest CPU state.
        start = source.index('    /* --- (補助) 明示ポインタ引数の早期範囲検証')
        end = source.index('    /* 引数コピー窓:', start)
        sources['early'] = source[start:end]
        start = source.index('static int exec_disk_write_path_allowed(')
        end = source.index('/* ======================================================================== */', start)
        sources['disk'] = '#include "' + str(ROOT / 'fs/vfs.h') + '"\n' + source[start:end]
        vfs = (ROOT / 'fs/vfs.c').read_text()
        start = vfs.index('#define VFS_NAME_MAX')
        end = vfs.index('/* 末尾の', start)
        sources['resolve'] = ('#include "' + str(ROOT / 'fs/vfs.h') + '"\n' +
                              'static char cwd[VFS_MAX_PATH] = "/";\n' + vfs[start:end])
        memstat = (ROOT / 'kapi/kapi_sys.c').read_text()
        sources['memstat'] = memstat[memstat.index('STATIC_ASSERT(sizeof(MemStat)'):memstat.index('/* カーネルビルド時')].replace('i32 kapi_mem_stat(', 'static i32 mem_stat_body(')
        if mutant and not no_out:
            name, key, old, new, expected = mutant
            assert old in sources[key], name
            sources[key] = sources[key].replace(old, new, 1)
        # Real wrapper translation unit; only device/FS/DB side effects are stubs.
        # walk.run's include closure lacks drivers/gfx for the full generated unit.
        # Reuse managed-AS setup, but execute only this fixture's cases.
        fixture = (ROOT / 'tools/tests/access_walk_host.c').read_text()
        fixture = fixture.replace('    caller_copy_tests();', '    caller_copy_tests();\n    die(0);')
        sources['setup'] = fixture.replace('#include "pgalloc_host_fixture.h"', '#include "' + str(ROOT / 'tools/tests/pgalloc_host_fixture.h') + '"')
        # Includes in the generated unit are resolved to the real repository headers.
        import re
        def resolve(m):
            name = m.group(1)
            for base in ('sdk/include/os32', 'gfx', 'drivers', 'exec', 'kernel', 'include', 'fs', 'lib', 'kapi'):
                path = ROOT / base / name
                if path.exists(): return '#include "' + str(path) + '"'
            return m.group(0)
        sources['generated'] = re.sub(r'#include "([^"]+)"', resolve, sources['generated'])
        result = walk.run(sources, fixture=fixture_name, runner=runner)
        if mutant:
            name, key, old, new, expected = mutant
            assert result.returncode == 1 and ('FAIL: ' + expected) in result.stdout, (name, result.returncode, result.stdout, result.stderr)
            print('RED:', name)
        else:
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    try:
        run(args.runner)
        if args.mutate:
            for mutant in MUTANTS: run(args.runner, mutant)
            print(f'PASS mutations {len(MUTANTS)}/{len(MUTANTS)}')
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
