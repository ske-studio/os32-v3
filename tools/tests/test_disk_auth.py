"""Real disk KAPI authorization policy; every mutant must fail at runtime."""
import argparse
from test_kapi_ranges import ROOT, run

MUTANTS = (
    ('path normalization removed', 'disk',
     'if (vfs_resolve_path(resolved, absolute, sizeof(absolute)) != VFS_OK) return 0;',
     'kstrncpy(absolute, resolved, sizeof(absolute));',
     'wrap_ide_write_sector(0, 0, buf) == expected'),
    ('GUI slot allowed', 'disk', ' || slot->gui', '',
     'wrap_ide_write_sector(0, 0, buf) == expected'),
    ('console sink allowed', 'disk', ' || con_sink_is_enabled()', '',
     'wrap_ide_write_sector(0, 0, buf) == expected'),
    ('any nonempty path', 'disk', 'kstrcmp(absolute, paths[i]) == 0',
     'absolute[0] != 0', 'wrap_ide_write_sector(0, 0, buf) == expected'),
    ('TRUSTED denied', 'disk', 'appslot_cur() == APP_ID_SHELL && !slot->cpl3',
     '0 && !slot->cpl3', 'wrap_ide_write_sector(0, 0, buf) == expected'),
)
for slot, name, args in ((58, 'ext2_format', '0, 4096'),
                         (64, 'ide_write_sector', '0, 0, buf'),
                         (65, 'ide_write_sectors', '0, 0, 1, buf'),
                         (157, 'dev_blk_write', '"disk", 0, 1, buf'),
                         (230, 'ext2_format_at', '0, 18, 4096')):
    MUTANTS += ((name + ' authorization removed', 'generated',
                 f'KAPI_HIT({slot});\n    if (!exec_disk_write_allowed()) return -1;',
                 f'KAPI_HIT({slot});', f'wrap_{name}({args}) == expected'),)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    p.add_argument('--mutate', action='store_true')
    args = p.parse_args()
    # Fail closed if the loader no longer records the actual resolved image.
    source = (ROOT / 'exec/exec.c').read_text()
    assert 'ctx->disk_write_authorized = exec_disk_write_path_allowed(resolved);' in source
    # Exercise the manifest check with real inputs and a moved installer.
    import copy
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    from check_manifests import check_disk_write_paths, yaml
    config = (ROOT / 'include/config.h').read_text()
    manifest = yaml.safe_load((ROOT / 'userland/deploy.yaml').read_text())
    assert not check_disk_write_paths(config, manifest)
    for name in ('install', 'cdinst'):
        moved = copy.deepcopy(manifest)
        for entry in moved['filesystem']['files']:
            if entry['host'] == f'userland/system/{name}.bin':
                entry['guest'] = '/tmp/'
        assert check_disk_write_paths(config, moved), name
    assert check_disk_write_paths(config.replace('/sbin/install.bin', '/tmp/install.bin'), manifest)
    print('PASS manifest paths / moved installer rejected')
    run(args.runner, fixture_name='disk_auth_host.c')
    if args.mutate:
        for m in MUTANTS:
            run(args.runner, m, fixture_name='disk_auth_host.c')
        print(f'PASS mutations {len(MUTANTS)}/{len(MUTANTS)} runtime RED')
