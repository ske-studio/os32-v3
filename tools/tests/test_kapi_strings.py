"""T4b-8b: USER string groups with real B1, distinct VA/PA, runtime RED."""
import argparse
import json
import subprocess
import test_access_walk as walk

ROOT = walk.ROOT
GROUPS = {
    'path': 'sys_unlink sys_rename sys_mkdir sys_rmdir sys_chdir sys_open sys_stat sys_redirect_fd path_set_drive path_set_cwd path_parse',
    'mount': 'sys_mount sys_umount sys_umount_checked sys_is_mounted vfs_devname',
    'device': 'kcg_load_font loop_attach dev_blk_read dev_blk_write v86_disktest v86_boot v86_boot2 sys_switch_shell',
    'command': 'exec_run exec_start launch_req',
    'output': 'serial_puts shell_print_utf8 fm_play_mml snd_bgm_play',
}
NULLABLE = {name: 1 for name in (
    'sys_unlink sys_mkdir sys_rmdir sys_chdir sys_open sys_stat '
    'kcg_load_font loop_attach v86_disktest v86_boot sys_switch_shell '
    'exec_start launch_req dev_blk_write').split()}
NULLABLE.update(sys_rename=3, sys_redirect_fd=2, v86_boot2=3,
                sys_mount=7, sys_umount_checked=1)
TARGET_SRC = [*walk.FILES.values(), 'kapi/kapi_strings.c', 'include/kapi_strings.h',
              'exec/exec.c', 'sdk/kapi.json', 'exec/ring3_ls.c']


def run(args, mutant=None):
    sources = {k: (ROOT / p).read_text() for k, p in walk.FILES.items()}
    setup = (ROOT / 'tools/tests/access_walk_host.c').read_text()
    setup = setup.replace('#include "pgalloc_host_fixture.h"',
                          '#include "' + str(ROOT / 'tools/tests/pgalloc_host_fixture.h') + '"')
    setup = setup.replace('    caller_copy_tests();', '    caller_copy_tests();\n    die(0);')
    sources['setup'] = setup
    sources['strings'] = (ROOT / 'kapi/kapi_strings.c').read_text()
    dispatch = (ROOT / 'exec/exec.c').read_text()
    start = dispatch.index('    int strings = kapi_strings_call(')
    sources['dispatch'] = dispatch[start:dispatch.index('    /* --- (補助)', start)]
    cleanup = dispatch.index('static void exec_reclaim_resources(int id)\n{')
    sources['cleanup'] = dispatch[cleanup:dispatch.index('    /* (1)', cleanup)] + '    (void)id;\n}\n'
    sources['ls'] = (ROOT / 'exec/ring3_ls.c').read_text().split('static int ls_copy_path', 1)[1].split('static void ls_collect_entry', 1)[0]
    sources['ls'] = 'static int ls_copy_path' + sources['ls']
    data = json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
    group_names = GROUPS if args.group == 'all' else [args.group]
    rows = []
    for group in group_names:
        for name in GROUPS[group].split():
            index, api = next((i, a) for i, a in enumerate(data) if a['name'] == name)
            mask = sum(1 << i for i, arg in enumerate(api['args']) if 'const char *' in arg)
            ptrmask = sum(1 << i for i, arg in enumerate(api['args']) if '*' in arg)
            cap = 4096 if group in ('command', 'output') and name != 'launch_req' else 256
            rows.append(f'{{{index}, {len(api["args"]) * 4}, {mask}, {ptrmask}, {cap}, {NULLABLE.get(name, 0)}}}, /* {name} */')
    sources['cases'] = '\n'.join(rows)
    if mutant:
        key, old, new, label = mutant
        assert sources[key].count(old) == 1, label
        sources[key] = sources[key].replace(old, new)
    result = walk.run(sources, fixture='kapi_strings_host.c', runner=args.runner)
    if mutant:
        assert result.returncode == 1 and 'FAIL:' in result.stdout, (label, result)
        print('RED (compiled, runtime rc=1):', label, result.stdout.strip())
    else:
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        print(args.group + ': ' + result.stdout.strip())


def mutants(args):
    names = GROUPS if args.group == 'all' else [args.group]
    if args.group in ('all', 'mount'):
        for slot, mask in (('SYS_MOUNT', 7), ('SYS_UMOUNT_CHECKED', 1)):
            yield ('strings', f'[KAPI_SLOT_{slot}] = {{{mask}, {mask}, VFS_MAX_PATH}}',
                   f'[KAPI_SLOT_{slot}] = {{{mask}, 0, VFS_MAX_PATH}}',
                   slot.lower() + ' NULL killed')
    if args.group in ('all', 'device'):
        yield ('strings', '[KAPI_SLOT_DEV_BLK_WRITE] = {1, 1, VFS_MAX_PATH}',
               '[KAPI_SLOT_DEV_BLK_WRITE] = {1, 0, VFS_MAX_PATH}',
               'dev_blk_write NULL killed')
    for group in names:
        slot = 'KAPI_SLOT_' + GROUPS[group].split()[0].upper()
        yield ('strings', 'args[i] = (u32)(uptr)p;',
               f'if (slot != {slot}) args[i] = (u32)(uptr)p;', group + ' raw string pointer')
    yield ('strings', '/* Preserve the early pointer gate',
           'for (u32 i = 0; i < nbytes / 4; i++) args[i] = ((const u32 *)user_args)[i];\n    /* Preserve the early pointer gate', 'raw argument words')
    yield ('strings', 'p, input->cap)) goto out;',
           'p, input->cap)) { *result = kapi_invoke(target, args, nbytes); goto out; }', 'side effect on refusal')
    yield ('cleanup', '    kapi_strings_cleanup_owned(id);',
           '    /* missing owner cleanup */', 'nonlocal owner cleanup hook')
    yield ('strings', 'if (p) kfree(p);', 'if (p) (void)p;', 'staging leak')
    if args.group in ('all', 'path'):
        yield ('ls', 'return copy_caller_cstr(caller, (const char *)(uptr)va,\n                            path, VFS_MAX_PATH);',
               'if (!copy_caller_cstr(caller, (const char *)(uptr)va, path, VFS_MAX_PATH)) { path[0] = 0; } return 1;', 'ls unterminated accepted')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    parser.add_argument('--group', choices=(*GROUPS, 'all'), default='all')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    try:
        run(args)
        if args.mutate:
            all_mutants = list(mutants(args))
            for m in all_mutants:
                run(args, m)
            print(f'PASS {len(all_mutants)}/{len(all_mutants)} mutations runtime RED')
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
