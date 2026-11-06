#!/usr/bin/env python3
"""DICT_META N: independent generator oracle and real core probe (no libc)."""
CHECK = dict(name='fep-meta', tier=1, needs=[], budget=30, covers=[
    'tools/fep_to_sqlite.py', 'assets/joyo_kanji.txt',
    'tools/tests/fep_host_build.py', 'tools/tests/fep_boundary_runtime.h',
    'tools/tests/ime_dict_host.c', 'tools/tests/vfs_fd_sqlite_host.c',
    'tools/tests/sqlite_groups_backend.h', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/host_arch/**',
    'tools/tests/input_fixture_host.h', 'tools/tests/serial_emergency_host.h',
    'kernel/sqlite_engine.c', 'kernel/sqlite_engine.h', 'kernel/ime/ime*.c',
    'kernel/ime/ime*.h', 'kernel/input_service.c', 'kernel/serial_diag.c',
    'lib/sqlite3/**', 'lib/kstring*', 'lib/kmath.c', 'lib/divdi3.c',
    'fs/vfs_fd.c', 'fs/vfs.h', 'include/**',
    'sdk/include/os32/os32_kapi_shared.h', 'build/config.mk'])

import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile

import host32
from fep_host_build import ROOT, prepare, build, probe_source

ENTRIES = [('か', '蚊', 9, 30), ('か', '蚊', 2, 10), ('か', '蚊', 1, 10),
           ('a', '亜', 3, -20), ('かな', '仮名', 2047, 801),
           ('かな', '加奈', 8, 1501), ('z', 'Z', 4, 800)]


def builder(source):
    module = {'__file__': str(ROOT / 'tools/fep_to_sqlite.py'), '__name__': 'fep_builder'}
    exec(compile(source, module['__file__'], 'exec'), module)
    return module


def generate(work, source):
    gen = builder(source)
    original = 'COPYING\r\nCopyright \xff\r\n'.encode('latin-1')
    (work / 'COPYING').write_bytes(original)
    os.environ['SOURCE_DATE_EPOCH'] = '0'
    paths = []
    for variant, limit in [('S', 800), ('M', 1500), ('L', 0)]:
        path = work / (variant + '.db')
        gen['create_db'](ENTRIES, str(path), limit, variant, str(work))
        with sqlite3.connect(path) as db:
            rows = db.execute('SELECT * FROM dict').fetchall()
            meta = dict(db.execute('SELECT key,value FROM meta'))
            assert len(meta) == 19 and set(meta) == set('format_version min_fep_version dict_id kind language charset variant source source_version license attribution build_tool build_date entry_count pos_table_version cost_scale mem_reserve_kb cache_pages checksum'.split())
            expected = {}
            for y, k, p, c in ENTRIES:
                if limit and c > limit: continue
                expected[y, k] = min(expected.get((y, k), (c, p)), (c, p))
            assert sorted(rows) == sorted((y, k, p, c) for (y, k), (c, p) in expected.items())
            assert meta['entry_count'] == str(len(rows))
            stream = bytearray()
            for y, k, p, c in sorted(rows, key=lambda r: (r[0].encode(), r[1].encode(), r[2], r[3])):
                for text in (y, k):
                    data = text.encode('utf-8')
                    stream.extend(len(data).to_bytes(4, 'big') + data)
                stream.extend(int(p).to_bytes(4, 'big', signed=True))
                stream.extend(int(c).to_bytes(4, 'big', signed=True))
            assert meta['checksum'] == 'sha256:' + hashlib.sha256(stream).hexdigest()
            assert meta['license'].encode('latin-1') == original
            assert meta['attribution'] == meta['license']
            assert meta['build_date'] == '1970-01-01T00:00:00Z'
            assert meta['build_tool'] == 'fep_to_sqlite.py/1'
            assert {k: meta[k] for k in ('kind', 'language', 'charset', 'variant', 'source', 'source_version')} == dict(kind='system', language='ja', charset='UTF-8', variant=variant, source='IPADIC', source_version='2.7.0')
        paths.append((path, meta))
    for missing in ('empty', 'missing'):
        if missing == 'empty': (work / 'COPYING').write_bytes(b' \r\n')
        else: (work / 'COPYING').unlink()
        try: gen['create_db'](ENTRIES, str(work / 'invalid.db'), 0, 'L', str(work))
        except (ValueError, FileNotFoundError): pass
        else: raise AssertionError('accepted missing/empty original')
    return paths


def c_bytes(data):
    return ','.join(str(b) for b in data)


def runtime_body(paths):
    body = 'int main(void) { CHECK(os32_sqlite_init() == SQLITE_OK); fixture_init(); FepMeta m;\n'
    for i, (path, meta) in enumerate(paths):
        body += f'static const unsigned char data{i}[] = {{{c_bytes(path.read_bytes())}}};\n'
        body += f'CHECK(fixture_create(0, FEP_DICT_PATH_M, data{i}, sizeof(data{i})) == 0);\n'
        # Probe known M path with matching variant, regardless of generated variant.
        body += 'sqlite3 *db = 0; CHECK(setup_open(FEP_DICT_PATH_M, &db, SQLITE_OPEN_READWRITE, 0) == 0);\n' if i == 0 else 'db = 0; CHECK(setup_open(FEP_DICT_PATH_M, &db, SQLITE_OPEN_READWRITE, 0) == 0);\n'
        body += 'CHECK(sqlite3_exec(db, "UPDATE meta SET value=\'M\' WHERE key=\'variant\'",0,0,0) == 0); CHECK(setup_close(db) == 0);\n'
        body += 'CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == 0);\n'
        body += f'CHECK(m.format == FEP_DICT_FORMAT_VERSION && m.format == {meta["format_version"]}u);\n'
        for field, key, const in [('min_reader', 'min_fep_version', 'FEP_DICT_READER_VERSION'), ('pos_version', 'pos_table_version', 'FEP_DICT_POS_VERSION'), ('reserve_kb', 'mem_reserve_kb', 'FEP_DICT_RESERVE_KB'), ('cache_pages', 'cache_pages', 'FEP_DICT_CACHE_PAGES')]:
            body += f'CHECK(m.{field} == {meta[key]}u && m.{field} == {const});\n'
        body += f'CHECK(!strcmp(m.dict_id, "{meta["dict_id"]}") && !strcmp(m.dict_id, FEP_DICT_ID));\n'
        body += f'CHECK(!strcmp("{meta["cost_scale"]}", FEP_DICT_COST_SCALE) && m.entries == {meta["entry_count"]}u);\n'
    body += '''FepStore store = {0};
    store.boot_reserve_kb = FEP_DICT_RESERVE_KB;
    CHECK(fep_store_open(&store, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
    void *system_db = store.system.db, *user_db = store.user.db;
    CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == OS32_ERR_IO);
    CHECK(fep_store_switch(&store, FEP_DICT_PATH_M) == 0);
    CHECK(store.system.db == system_db && store.user.db == user_db);
    CHECK(fep_store_close(&store) == 0);
    FixtureFile *f = fixture_find(FEP_DICT_PATH_M, 0);
    FepMeta sentinel; memset(&sentinel, 0x55, sizeof(sentinel)); m = sentinel;
    f->data[63] = 2;
    CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == OS32_ERR_INVAL);
    CHECK(!memcmp(&m, &sentinel, sizeof(m)));
    f->data[63] = 1;
    db = 0;
    CHECK(setup_open(FEP_DICT_PATH_M, &db, SQLITE_OPEN_READWRITE, 0) == 0);
    CHECK(sqlite3_exec(db, "UPDATE meta SET value='2' WHERE key='format_version'",0,0,0) == 0);
    CHECK(setup_close(db) == 0);
    CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == OS32_ERR_INVAL);
    f->data[63] = 0;
    CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == 0 && m.format == 0);
    CHECK(m.reserve_kb == FEP_DICT_RESERVE_KB && !strcmp(m.dict_id, FEP_DICT_ID));
    f->data[0] = 'X';
    CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == OS32_ERR_INVAL);
    f->data[0] = 'S'; f->data[16] = 8;
    CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == OS32_ERR_INVAL);
    f->data[16] = 4; f->size = 99;
    CHECK(fep_meta_probe(FEP_DICT_PATH_M, &m) == OS32_ERR_IO);
    puts("PASS generated metadata/constants/canonical stream and real probe versions/header");
    return 0; }
    '''
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'tools/fep_to_sqlite.py').read_text()
    with tempfile.TemporaryDirectory(prefix='fep-meta-') as directory:
        work = Path(directory)
        paths = generate(work, source)
        prepare(work, probe_source())
        body = runtime_body(paths)
        def run_case(name):
            result = host32.run([str(build(work, body, name))], capture_output=True, text=True, timeout=120)
            print(result.stdout + result.stderr, end='', flush=True)
            return result
        assert run_case('normal').returncode == 0
        if args.mutate:
            probe = probe_source()
            for label, old, new in [
                ('future-version', 'if (version > FEP_DICT_FORMAT_VERSION)', 'if (version < FEP_DICT_FORMAT_VERSION)'),
                ('meta-version', 'if (number != version)', 'if (0)'),
                ('header-magic', 'memcmp(header, "SQLite format 3", 16)', '0'),
                ('page-size', '((u32)header[16] << 8 | header[17]) != FEP_DICT_PAGE_SIZE', '0')]:
                assert probe.count(old) == 1, label
                (work / 'probe.c').write_text(probe.replace(old, new))
                red = run_case(label)
                assert red.returncode == 1 and 'FAIL main:' in red.stderr, label
                print('RED runtime ' + label, flush=True)
            (work / 'probe.c').write_text(probe)
            product_path = ROOT / 'kernel/ime/ime_dict.c'
            product = product_path.read_text()
            anchor = 'if (s->system.db && !kstrcmp(path, s->system.path))'
            assert product.count(anchor) == 1
            (work / 'dict-mutant.c').write_text(product.replace(anchor, 'if (0)'))
            harness_path = work / 'ime_dict_host.c'
            harness = harness_path.read_text()
            include = '#include "' + str(product_path) + '"'
            assert harness.count(include) == 1
            harness_path.write_text(harness.replace(include, '#include "dict-mutant.c"'))
            red = run_case('same-selection')
            assert red.returncode == 1 and 'fep_store_switch(&store' in red.stderr
            print('RED runtime same-selection', flush=True)
            harness_path.write_text(harness)
            for label, old, new in [
                ('checksum-endian', "struct.pack('>ii', pos_id, cost)", "struct.pack('<ii', pos_id, cost)"),
                ('entry-count', "'entry_count': str(final_count)", "'entry_count': str(final_count + 1)"),
                ('copying-bytes', "'license': original", "'license': original.strip()")]:
                assert source.count(old) == 1
                try: generate(work, source.replace(old, new))
                except AssertionError: print('RED runtime ' + label, flush=True)
                else: raise AssertionError('survived ' + label)
    return 0


def run(ctx):
    for index, runner in enumerate(ctx.runners):
        argv = [sys.executable, '-B', 'tools/tests/test_fep_meta.py']
        if ctx.mutate and index == 0: argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=runner))
        if rc: return rc
    return 0 if ctx.runners else 1


if __name__ == '__main__':
    raise SystemExit(main())
