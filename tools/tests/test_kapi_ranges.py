"""Compile real generated wrappers with the real B1 managed walk (ILP32).
Only private generated/source copies are mutated; compiler failure is not RED.
"""
TARGET_SRC = ['sys/platform/pc98/legacy/guest.c', 'sdk/gen_kapi.py', 'sdk/kapi.json', 'sys/kern/exec/exec.c', 'sys/kern/exec/ring3_str.c',
              'sys/net/host_mailbox.c', 'sys/kapi/kapi_host_notice.c',
              'sys/kern/exec/access_walk.c', 'sys/kern/exec/redir_access.c', 'sys/kapi/kapi_sysinfo.c', 'sys/platform/pc98/platform_mem_stat.h', 'tools/tests/kapi_ranges_host.c', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h']
from exec_source import exec_source
import argparse
import json
import pathlib
import subprocess
import tempfile
from mem_stat_source import mem_stat_source
import test_access_walk as walk
import test_kapi_out as generator

ROOT = walk.ROOT
MUTANTS = (
    ('in2 read alignment', 'generated', ' || (u32)out % __alignof__(IN2_Event)', '', 'in2 read alignment'),
    ('legacy dispatch guard', 'legacy', 'int rc = kapi_v86s_check(slot, args.value);',
     'int rc = 0;', 'AV legacy sequence tail'),
    ('retired FM pointer mask', 'generated', '0x0000,  /* fm_play_mml */', '0x0001,  /* fm_play_mml */', 'retired FM returns'),
    ('retired BGM pointer mask', 'generated', '0x0000,  /* snd_bgm_play */', '0x0001,  /* snd_bgm_play */', 'retired BGM returns'),
    ('retired BGM result', 'generated', 'wrap_snd_bgm_is_playing(void)\n{\n    return 0;', 'wrap_snd_bgm_is_playing(void)\n{\n    return OS32_ERR_NOSYS;', 'retired BGM idle'),
    ('retired focus result', 'generated', 'wrap_snd_focus(int app_id)\n{\n    (void)app_id;\n    return OS32_ERR_NOSYS;', 'wrap_snd_focus(int app_id)\n{\n    (void)app_id;\n    return 0;', 'retired focus'),
    ('session sequence guard', 'generated', '\n                                        (u32)last_seq, KAPI_OUT_LEN(last_seq, sizeof(u32))', '\n                                        (u32)last_seq, 0', 'AV legacy sequence tail'),
    ('notice copy skipped', 'notice',
     'copy_to_caller(&caller, out, &notice, sizeof(notice))', '1', 'notice output bytes'),
    ('notice not consumed', 'notice',
     'host_mailbox_commit(notice.host_generation, notice.serial);',
     '(void)notice.serial;', 'notice consumed once'),
    ('mem_stat out none', 'schema', None, None, 'mem_stat NULL'),
    ('legacy VRAM exception restored', 'exec', '    return 0;\n}', '    if (p >= 0xA0000UL && p < 0xC0000UL) return 1;\n    return 0;\n}', 'unleased VRAM refused'),
    ('output NULL bypass', 'generated', '((u32)(n))', '((p) ? (u32)(n) : 0u)', 'output NULL'),
    ('signed output NULL bypass', 'generated', '(((int)(n) > 0) ? (u32)(n) : 0u)', '(((p) && (int)(n) > 0) ? (u32)(n) : 0u)', 'signed output NULL'),
    ('input first byte', 'generated', 'ring3_user_range_ok(p, len)', 'ring3_user_range_ok(p, 1)', 'input tail'),
    ('early NULL rejected', 'exec', 'if (p == 0) return 1;', 'if (p == 0) return 0;', 'sys_ls NULL ctx'),
    ('device read NULL bypass', 'generated', 'if (count > 0) {', 'if (count > 0 && buf) {', 'device NULL'),
    ('input multiplication wrap', 'generated', 'if (n > (0xFFFFFFFFUL / unit))', 'if (0)', 'input multiplication'),
    ('CPL0 two outputs rejected', 'exec',
     'if (!ring3_guard_active(ring3_in_syscall, ring3_wm_depth)) return 1;',
     'if (!ring3_guard_active(ring3_in_syscall, ring3_wm_depth)) return lb == 0;',
     'trusted clock two outputs'),
)

# Narrow each generated guard to one byte. The first byte is mapped and the
# complete input/output crosses the page boundary in the corresponding case.
for name in ('pad_state', 'pad_caps', 'pad_vibrate', 'dev_info', 'read',
             'key_state', 'mouse_state', 'status', 'wm_state'):
    case = 'AV key state tail' if name == 'key_state' else 'in2 ' + name + ' tail'
    MUTANTS += ((name + ' pointer guard', 'in2_guard', 'wrap_in2_' + name + '(',
                 '1u', case),)

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
        sources['notice'] = (ROOT / 'sys/kapi/kapi_host_notice.c').read_text().replace(
            '#include "../net/host_mailbox.h"', '#include "' + str(ROOT / 'sys/net/host_mailbox.h') + '"')
        sources['mailbox'] = (ROOT / 'sys/net/host_mailbox.c').read_text().replace(
            '#include "host_mailbox.h"', '#include "' + str(ROOT / 'sys/net/host_mailbox.h') + '"')
        sources['generated'] = (generated / 'sys/kapi/kapi_generated.c').read_text()
        guest = (ROOT / 'sys/platform/pc98/legacy/guest.c').read_text()
        start = guest.index('int legacy_call_dispatch(')
        sources['legacy'] = guest[start:guest.index('\n}', start) + 2]
        source = exec_source(ROOT)
        sources['exec'] = source[source.index('int ring3_ptr_ok('):source.index('\n#include "ksetjmp.h"')]
        # Keep every range guard, excluding the unrelated g1 DONE function.
        trim_start = sources['exec'].index('i32 exec_trim_done(')
        trim_end = sources['exec'].index('\n}', trim_start) + 2
        sources['exec'] = sources['exec'][:trim_start] + sources['exec'][trim_end:]
        # Execute the actual dispatcher's early-check block, with generated
        # masks/argument sizes. The rest of int80 needs guest CPU state.
        start = source.index('    /* --- (補助) 明示ポインタ引数の早期範囲検証')
        end = source.index('    result = arch_kapi_invoke(', start)
        sources['early'] = source[start:end]
        start = source.index('static int exec_disk_write_path_allowed(')
        end = source.index('/* ======================================================================== */', start)
        sources['disk'] = '#include "' + str(ROOT / 'sys/fs/vfs.h') + '"\n' + source[start:end]
        vfs = (ROOT / 'sys/fs/vfs.c').read_text()
        start = vfs.index('#define VFS_NAME_MAX')
        end = vfs.index('/* 末尾の', start)
        sources['resolve'] = ('#include "' + str(ROOT / 'sys/fs/vfs.h') + '"\n' +
                              # These caller/range fixtures have no mounted FS.
                              # Resource lifetime is outside their boundary.
                              'int vfs_resource_prepare(VfsOps *ops, void *ctx)\n'
                              '{ (void)ops; (void)ctx; return VFS_OK; }\n' +
                              'static char cwd[VFS_MAX_PATH] = "/";\n' + vfs[start:end])
        memstat = (ROOT / 'sys/kapi/kapi_sysinfo.c').read_text()
        sources['memstat'] = mem_stat_source(memstat).replace('i32 kapi_mem_stat(', 'static i32 mem_stat_body(')
        if mutant and not no_out:
            name, key, old, new, expected = mutant
            if key == 'in2_guard':
                key = 'generated'
                assert sources[key].count(old) == 1
                start = sources[key].index(old)
                end = sources[key].index('return in2_', start)
                body = sources[key][start:end]
                # Keep the address and access direction, narrow the length.
                guard = next(line for line in body.splitlines() if 'if (!' in line)
                address_end = guard.index(', ') + 2
                guard_end = ',' if 'ranges_writable' in guard else ')) {'
                new = body.replace(guard, guard[:address_end] + new + guard_end)
                old = body
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
