"""KAPI v71 public wraps: real ILP32 caller/B1/lease, runtime mutants."""
import pathlib
import re
import subprocess
import tempfile
import json
from test_surface_lease import main, ROOT
TARGET_SRC = ['sdk/kapi.json', 'kapi/kapi_generated.c', 'exec/surface_query.c',
              'exec/lease.c', 'sdk/include/os32/os32_surface.h', 'exec/ring3_ls.c',
              'sdk/include/os32/os32_ls.h', 'kernel/shm.c',
              'tools/check_kapi_version.py', 'docs/KAPI_SPEC.md', 'exec/exec.c']
NAMES = ['surface_query', 'surface_lease', 'gfx_surface_lease', 'surface_unlease',
         'sys_ls_window', 'caller_identity']

def contract(data, shared, source, ls, shm):
    assert data['version'] >= 71
    assert int(re.search(r'#define KAPI_VERSION\s+(\d+)', shared)[1]) == data['version']
    assert [a['name'] for a in data['api'][240:246]] == NAMES
    assert [a['name'] for a in data['api'][246:248]] == ['mem_map', 'mem_unmap']
    for typ, fields in {
        'surface_ref':'sid generation',
        'surface_desc':'ref role backend format width height pitch planes plane_offset bytes access_max',
        'surface_query_result':'count desc', 'lease_view':'token base bytes planes',
        'surface_lease_result':'count views',
    }.items():
        for f in ['size'] + fields.split():
            assert re.search(r'STATIC_ASSERT\([^;]+\babi_'+typ+'_'+f+r'\);',source)
    for typ, fields in {'LsEntry':'name size_offset type','LsPacket':'count result done entries'}.items():
        for f in ['size'] + fields.split():
            assert 'abi_'+typ+'_'+f+');' in ls
    assert 'STATIC_ASSERT(PAGE_SIZE == OS32_PAGE_SIZE, shm_sdk_page_size);' in shm
    assert 'STATIC_ASSERT(SHM_BLOCK_SIZE == OS32_SHM_BLOCK_SIZE, shm_sdk_block_size);' in shm

if __name__ == '__main__':
    import sys
    values = [json.loads((ROOT/'sdk/kapi.json').read_text()),
              (ROOT/'sdk/include/os32/os32_kapi_shared.h').read_text(),
              (ROOT/'exec/surface_query.c').read_text(),
              (ROOT/'exec/ring3_ls.c').read_text(), (ROOT/'kernel/shm.c').read_text()]
    contract(*values)
    if '--mutate' in sys.argv:
        for idx, changed, label in [
            (0, dict(values[0], version=70), 'version rollback'),
            (1, values[1].replace('KAPI_VERSION      71','KAPI_VERSION      70'), 'shared version rollback'),
            (2, re.sub(r'STATIC_ASSERT\([^;]+\babi_lease_view_size\);','',values[2]), 'STATIC_ASSERT removed'),
            (3, re.sub(r'STATIC_ASSERT[^;]+abi_LsEntry_size_offset\);', '', values[3]), 'ls size offset assertion removed'),
            (4, values[4].replace('STATIC_ASSERT(PAGE_SIZE == OS32_PAGE_SIZE, shm_sdk_page_size);',''), 'page assertion removed'),
        ]:
            mutated = values.copy(); mutated[idx] = changed
            try: contract(*mutated)
            except AssertionError: print('RED (contract):', label)
            else: raise AssertionError('survived: '+label)
    # Exercise the actual version checker against an in-memory stale summary.
    import importlib.util
    import io
    from unittest.mock import patch
    spec = importlib.util.spec_from_file_location('version_check', ROOT/'tools/check_kapi_version.py')
    checker = importlib.util.module_from_spec(spec); spec.loader.exec_module(checker)
    assert checker.main() == 0
    if '--mutate' in sys.argv:
        original_open = open
        doc = (ROOT/'docs/KAPI_SPEC.md').read_text()
        stale = doc.replace('現在のバージョン | **71**', '現在のバージョン | **70**')
        assert stale != doc
        def stale_open(path, *args, **kwargs):
            if pathlib.Path(path).resolve() == ROOT/'docs/KAPI_SPEC.md': return io.StringIO(stale)
            return original_open(path, *args, **kwargs)
        with patch('builtins.open', stale_open):
            assert checker.main() == 1
        print('RED (checker rc=1): overview version rollback')
    gen = (ROOT/'kapi/kapi_generated.c').read_text()
    wraps = '\n'.join(re.search(r'int __cdecl wrap_'+n+r'\([^\n]+\n\{.*?\n\}', gen, re.S)[0]
                      for n in NAMES if n != 'sys_ls_window')
    from test_kcallback import function
    wm = (ROOT/'exec/exec.c').read_text()
    wraps = 'volatile u32 ring3_wm_depth_underflow;\n' + '\n'.join(function(wm, 'void ' + n + '(void)') for n in ('ring3_wm_enter', 'ring3_wm_leave')) + '\n' + wraps
    mutants = [
        ('public_wrap', '!caller_access_get(&caller) || caller.origin != CALLER_USER',
         '!caller_access_get_user(&caller)', 'identity WM origin guard'),
        ('public_wrap', 'KAPI_OUT_LEN(app, sizeof(u32))', 'KAPI_OUT_LEN(app, 0)', 'identity app output guard'),
        ('public_wrap', 'KAPI_OUT_LEN(owner, sizeof(u32))', 'KAPI_OUT_LEN(owner, 0)', 'identity owner output guard'),
        ('public_wrap', 'KAPI_OUT_LEN(generation, sizeof(u32))', 'KAPI_OUT_LEN(generation, 0)', 'identity generation output guard'),
        ('surface_query', '!gfx || (gui && appslot_gfx_owner() != c.app_id)',
         '(gui && appslot_gfx_owner() != c.app_id)', 'DISPLAY authorization removed'),
        ('public_wrap', '(u32)out, KAPI_OUT_LEN(out, sizeof(OS32_SurfaceQueryResult)),',
         '(u32)out, KAPI_OUT_LEN(out, 0),', 'query output guard removed'),
        ('public_wrap', '(u32)out, KAPI_OUT_LEN(out, sizeof(OS32_LeaseView)),',
         '(u32)out, KAPI_OUT_LEN(out, 0),', 'lease output guard removed'),
        ('public_wrap', '(u32)out, KAPI_OUT_LEN(out, sizeof(OS32_LeaseResult)),',
         '(u32)out, KAPI_OUT_LEN(out, 0),', 'bundle output guard removed'),
        ('public_wrap', '*owner = identity.owner;', '*owner = identity.owner + 1;', 'identity value'),
        ('lease', 'if (as->leases[i].token == token)', 'if (as->leases[i].token == token || token)', 'foreign token accepted'),
    ]
    with tempfile.TemporaryDirectory(prefix='public-wrap-') as d:
        src = pathlib.Path(d)/'wrap.c'; src.write_text(wraps)
        try: main(fixture_name='public_surface_host.c', mutants=mutants,
                  extra_sources={'public_wrap':str(src)})
        except subprocess.CalledProcessError as e:
            print(e.stdout or '', e.stderr or ''); raise
