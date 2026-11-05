#!/usr/bin/env python3
"""DICT_META stage A: freestanding declarations and shared fake substrate.

Layout/values below are independent transcriptions of design §§2,3,5 and
existing ILP32 VfsSqliteCookie/OS32_MAX_PATH. Regenerate expectations only when
that contract changes. Mutants must compile and return a runtime failure.
"""
CHECK = dict(name='fep-store-contracts', tier=1, needs=[], budget=3,
    covers=['kernel/ime/ime_store.h', 'kernel/ime/ime.h',
            'tools/tests/fep_store_fixture.h', 'tools/tests/fep_store_fixture.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'build/config.mk', 'kernel/ime/ime_dict_info.c',
            'lib/sqlite3/os32_sqlite_vfs.h'])

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
HEADER = 'kernel/ime/ime_store.h'
FIXTURE_H = 'tools/tests/fep_store_fixture.h'
FIXTURE_C = 'tools/tests/fep_store_fixture.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
VALUES = {
    'FEP_DICT_READER_VERSION': 1, 'FEP_DICT_FORMAT_VERSION': 1,
    'FEP_DICT_POS_VERSION': 1, 'FEP_DICT_ID_CAP': 64,
    'FEP_DICT_PAGE_SIZE': 1024, 'FEP_DICT_HEADER_SIZE': 100,
    'FEP_DICT_CACHE_PAGES': 16, 'FEP_USER_CACHE_PAGES': 8,
    'FEP_DICT_RESERVE_KB': 192, 'FEP_DICT_MIN_RESERVE_KB': 99,
    'OS32_SQLITE_RESIDENT_GROUPS': 3,
    'IME_MAX_RESULTS': 32, 'IME_DICT_INFO_CAP': 256,
    'OS32_ERR_IO': -1, 'OS32_ERR_NOTFOUND': -2, 'OS32_ERR_INVAL': -9,
    'OS32_ERR_NOSYS': -10, 'OS32_ERR_STALE': -11, 'OS32_ERR_AGAIN': -14,
    'OS32_ERR_ROFS': -15, 'OS32_ERR_BUSY': -17,
}
KEYS = ('STATUS', 'FORMAT_VERSION', 'MIN_FEP_VERSION', 'DICT_ID', 'KIND',
        'LANGUAGE', 'CHARSET', 'VARIANT', 'SOURCE', 'SOURCE_VERSION',
        'LICENSE', 'ATTRIBUTION', 'BUILD_TOOL', 'BUILD_DATE', 'ENTRY_COUNT',
        'POS_TABLE_VERSION', 'COST_SCALE', 'MEM_RESERVE_KB', 'CACHE_PAGES', 'CHECKSUM')
STRINGS = {
    'FEP_DICT_ID': 'ipadic-2.7.0', 'FEP_DICT_COST_SCALE': 'os32-log2047-boost-v1',
    'FEP_DICT_PATH_S': '/db/fep_s.db', 'FEP_DICT_PATH_M': '/db/fep.db',
    'FEP_DICT_PATH_L': '/db/fep_l.db', 'FEP_USER_PATH': '/etc/fep_user.db',
    'FEP_USER_DIR': '/etc/', 'FEP_USER_JOURNAL_SUFFIX': '-journal',
    'FEP_SETTING_RESERVE': 'fep_mem_reserve_kb',
    'FEP_SETTING_USER_PATH': 'fep_user_dict_path',
}
SIGNATURES = {
    'fep_meta_probe': 'int (*)(const char *, FepMeta *)',
    'fep_boot_config_get': 'int (*)(FepMeta *, u32 *)',
    'fep_boot_user_path_get': 'int (*)(char *, u32)',
    'fep_store_open': 'int (*)(FepStore *, const char *, const char *)',
    'fep_store_switch': 'int (*)(FepStore *, const char *)',
    'fep_user_open': 'int (*)(FepStore *)',
    'fep_conn_close': 'int (*)(FepConn *)',
    'fep_conn_ready': 'int (*)(FepConn *)',
    'fep_store_close': 'int (*)(FepStore *)',
    'fep_store_live_connections': 'u32 (*)(const FepStore *)',
    'ime_boot_connections': 'u32 (*)(void)',
    'fep_store_search': 'int (*)(FepStore *, const char *, IME_Result *, int)',
    'fep_store_learn': 'void (*)(FepStore *, const char *, const char *)',
    'fep_store_user_list': 'int (*)(FepStore *, const char *, IME_UserEntry *, int)',
    'fep_store_user_delete': 'int (*)(FepStore *, const char *, const char *)',
    'fep_store_user_export': 'int (*)(FepStore *, const char *)',
    'fep_store_user_clear': 'int (*)(FepStore *)',
    'fep_store_info': 'int (*)(FepStore *, int, int, char *, int)',
    'hsp_set_fep_user_path': 'int (*)(const char *)',
    'fep_test_reset': 'void (*)(FepTestFixture *)',
    'fep_test_settings': 'int (*)(const FepTestFixture *, int *, int *, const char **)',
    'fep_test_script': 'void (*)(FepTestFixture *, const FepTestReply *, u32)',
    'fep_test_next': 'const FepTestReply *(*)(FepTestFixture *, enum FepTestOp)',
    'fep_test_done': 'int (*)(const FepTestFixture *)',
    'fep_test_resident_take': 'int (*)(FepTestFixture *)',
    'fep_test_resident_finish': 'void (*)(FepTestFixture *, int)',
}
MUTATIONS = (
    ('reader', HEADER, 'FEP_DICT_READER_VERSION 1u', 'FEP_DICT_READER_VERSION 2u'),
    ('format', HEADER, 'FEP_DICT_FORMAT_VERSION 1u', 'FEP_DICT_FORMAT_VERSION 2u'),
    ('page', HEADER, 'FEP_DICT_PAGE_SIZE 1024u', 'FEP_DICT_PAGE_SIZE 2048u'),
    ('header', HEADER, 'FEP_DICT_HEADER_SIZE 100u', 'FEP_DICT_HEADER_SIZE 99u'),
    ('cache', HEADER, 'FEP_DICT_CACHE_PAGES 16u', 'FEP_DICT_CACHE_PAGES 17u'),
    ('user-cache', HEADER, 'FEP_USER_CACHE_PAGES 8u', 'FEP_USER_CACHE_PAGES 9u'),
    ('id-cap', HEADER, 'FEP_DICT_ID_CAP 64u', 'FEP_DICT_ID_CAP 65u'),
    ('meta-order', HEADER, 'format, min_reader', 'min_reader, format'),
    ('conn-order', HEADER, '*db, *exact', '*exact, *db'),
    ('store-order', HEADER, 'FepConn system, user;', 'FepConn user, system;'),
    ('user-path', HEADER, '"/etc/fep_user.db"', '"/etc/fep_users.db"'),
    ('settings-presence', FIXTURE_C, '*present = f->reserve_present;', '*present = 0;'),
    ('fake-cap', FIXTURE_C, '>= OS32_SQLITE_RESIDENT_GROUPS', '> OS32_SQLITE_RESIDENT_GROUPS'),
    ('fake-isolation', FIXTURE_C, 'if (f->resident_isolated) return SQLITE_BUSY;',
     'if (f->resident_isolated) return SQLITE_OK;'),
)


def source():
    checks = [(name, str(value)) for name, value in VALUES.items()]
    checks += [('IME_DICT_INFO_' + name, str(i)) for i, name in enumerate(KEYS, 1)]
    layouts = {
        'FepMeta': (92, [('format', 0), ('min_reader', 4), ('pos_version', 8),
                        ('reserve_kb', 12), ('cache_pages', 16), ('entries', 20),
                        ('dict_id', 24), ('variant', 88)]),
        'FepConn': (288, [('db', 0), ('exact', 4), ('prefix', 8), ('learn', 12),
                         ('group', 16), ('path', 24), ('isolated', 280), ('io_retried', 284)]),
        'FepStore': (676, [('system', 0), ('user', 288), ('meta', 576),
                          ('boot_reserve_kb', 668), ('user_enabled', 672)]),
    }
    body = '#include "tools/tests/fep_store_fixture.h"\n'
    for name, sig in SIGNATURES.items():
        body += (f'_Static_assert(__builtin_types_compatible_p(__typeof__(&{name}), '
                 f'{sig}), "{name}");\n')
    for typ, (size, fields) in layouts.items():
        checks.append((f'sizeof({typ})', str(size)))
        for field, offset in fields:
            checks.append((f'__builtin_offsetof({typ}, {field})', str(offset)))
    field_types = {
        'FepMeta': dict.fromkeys(('format', 'min_reader', 'pos_version',
                                 'reserve_kb', 'cache_pages', 'entries'), 'u32'),
        'FepConn': dict.fromkeys(('db', 'exact', 'prefix', 'learn'), 'void *'),
        'FepStore': {'system': 'FepConn', 'user': 'FepConn', 'meta': 'FepMeta',
                     'boot_reserve_kb': 'u32', 'user_enabled': 'int'},
    }
    field_types['FepMeta'].update(dict_id='char[64]', variant='char')
    field_types['FepConn'].update(group='Os32SqliteGroup', path='char[256]',
                                  isolated='int', io_retried='int')
    for typ, fields in field_types.items():
        for field, kind in fields.items():
            # Array lengths are runtime layout contracts, not compile-time mutants.
            if '[' in kind:
                checks.append((f'sizeof((({typ} *)0)->{field})', kind.split('[')[1][:-1]))
                continue
            body += (f'_Static_assert(__builtin_types_compatible_p('
                     f'__typeof__((({typ} *)0)->{field}), {kind}), "{typ}.{field}");\n')
    body += '''static int equal(int a, int b) {
    __asm__ volatile ("" : "+r"(a), "+r"(b)); return a == b;
}
static int text_equal(const char *a, const char *b) {
    __asm__ volatile ("" : "+r"(a), "+r"(b) : : "memory");
    while (*a && *a == *b) { a++; b++; } return *a == *b;
}
static int test(void) {
'''
    for i, (actual, expected) in enumerate(checks, 1):
        body += f'    if (!equal({actual}, {expected})) return {i};\n'
    for name, value in STRINGS.items():
        body += f'    if (!text_equal({name}, "{value}")) return 190;\n'
    body += '''    FepTestFixture f;
    unsigned char *bytes = (unsigned char *)&f;
    for (u32 i = 0; i < sizeof(f); i++) bytes[i] = 0xa5;
    fep_test_reset(&f);
    for (u32 i = 0; i < sizeof(f); i++) if (bytes[i]) return 200;
    int present = 77, kb = 77; const char *path = "sentinel";
    if (fep_test_settings(&f, &present, &kb, &path) || present || kb || path) return 201;
    f.reserve_present = 1; f.reserve_kb = 0; f.user_path = "/etc/custom.db";
    if (fep_test_settings(&f, &present, &kb, &path) || !present || kb ||
        !text_equal(path, "/etc/custom.db")) return 202;
    f.settings_rc = OS32_ERR_IO; present = 77; kb = 77; path = "sentinel";
    if (fep_test_settings(&f, &present, &kb, &path) != OS32_ERR_IO || present != 77 ||
        kb != 77 || !text_equal(path, "sentinel")) return 203;
    static const FepTestReply script[] = {
        {FEP_TEST_PREPARE, SQLITE_OK, 0, 0},
        {FEP_TEST_STEP, SQLITE_ROW, "license", 7},
        {FEP_TEST_STEP, SQLITE_IOERR, 0, 0},
        {FEP_TEST_FINALIZE, SQLITE_OK, 0, 0}
    };
    fep_test_script(&f, script, sizeof(script) / sizeof(script[0]));
    if (fep_test_done(&f) || fep_test_next(&f, FEP_TEST_PREPARE)->rc) return 204;
    const FepTestReply *row = fep_test_next(&f, FEP_TEST_STEP);
    if (row->rc != SQLITE_ROW || row->bytes != 7 || !text_equal(row->data, "license")) return 205;
    if (fep_test_next(&f, FEP_TEST_STEP)->rc != SQLITE_IOERR ||
        fep_test_next(&f, FEP_TEST_FINALIZE)->rc || !fep_test_done(&f)) return 206;
    if (fep_test_next(&f, FEP_TEST_STEP)->rc != SQLITE_MISUSE || fep_test_done(&f)) return 207;
    fep_test_script(&f, script, sizeof(script) / sizeof(script[0]));
    if (fep_test_next(&f, FEP_TEST_OPEN)->rc != SQLITE_MISUSE || !f.mismatch || f.next) return 208;
    fep_test_reset(&f);
    for (u32 i = 0; i < OS32_SQLITE_RESIDENT_GROUPS; i++)
        if (fep_test_resident_take(&f) != SQLITE_OK) return 209;
    if (fep_test_resident_take(&f) != SQLITE_FULL) return 210;
    fep_test_resident_finish(&f, 0);
    if (fep_test_resident_take(&f) != SQLITE_OK) return 211;
    fep_test_resident_finish(&f, 1);
    if (fep_test_resident_take(&f) != SQLITE_BUSY ||
        f.resident_live != OS32_SQLITE_RESIDENT_GROUPS) return 212;
    fep_test_reset(&f); fep_test_resident_finish(&f, 0);
    if (!f.mismatch || f.resident_live) return 213;
    return 0;
}
void _start(void) {
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''
    return body


def build(work, tree, compiler, execute):
    src = work / 'contracts.c'
    src.write_text(source())
    cmd = [compiler, *FLAGS, '-I' + str(tree), *include_flags(tree)]
    if execute:
        exe = work / 'contracts'
        subprocess.run([*cmd, '-nostdlib', '-static', '-no-pie', str(src),
                        str(tree / FIXTURE_C), '-o', str(exe)], check=True)
        return host32.run([str(exe)], cwd=ROOT).returncode
    subprocess.run([*cmd, '-c', str(src), '-o', str(work / 'types.o')], check=True)
    subprocess.run([*cmd, '-c', str(tree / FIXTURE_C), '-o', str(work / 'fixture.o')], check=True)
    for header in (HEADER, FIXTURE_H, 'kernel/ime/ime.h'):
        src.write_text(f'#include "{header}"\n#include "{header}"\n')
        subprocess.run([*cmd, '-c', str(src), '-o', str(work / 'header.o')], check=True)
    src.write_text('#include "kernel/ime/ime_store.h"\n'
                   '#include "kernel/ime/ime_dict_info.c"\n')
    subprocess.run([*cmd, '-c', str(src), '-o', str(work / 'coinclude.o')], check=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-contracts-') as tmp:
        work = Path(tmp)
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        for compiler in ('gcc', str(cross)):
            build(work, ROOT, compiler, False)
            print('PASS standalone headers, field/function types:', compiler, flush=True)
        rc = build(work, ROOT, 'gcc', True)
        if rc:
            raise RuntimeError(f'normal runtime failure rc={rc}')
        print('PASS constants/layouts/fake/settings/reset; runner=' + host32.selected_runner(), flush=True)
        if args.mutate:
            headers = subprocess.check_output(['git', 'ls-files', '*.h'], cwd=ROOT,
                                              text=True).splitlines()
            tree = work / 'overlay'
            for rel in set(headers) | {HEADER, FIXTURE_H, FIXTURE_C}:
                target = tree / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / rel, target)
            for name, rel, old, new in MUTATIONS:
                target = tree / rel
                original = target.read_text()
                if original.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor not unique')
                target.write_text(original.replace(old, new))
                try:
                    rc = build(work, tree, 'gcc', True)
                finally:
                    target.write_text(original)
                if rc <= 0:
                    raise RuntimeError(f'{name}: runtime RED required, rc={rc}')
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_store_contracts.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
