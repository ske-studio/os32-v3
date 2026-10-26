"""V9: generated slot/layout and real API bodies, including legacy side effects."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from ver_test_utils import ROOT, arguments, execute, function

GENERATED = [
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_kapi_slots.h', 'kapi/kapi_generated.c',
    'exec/exec_kapi_init.inc', 'sdk/rust/os32api/src/kapi_generated.rs',
    'sdk/include/os32/os32_generations.h', 'sdk/os32_generations.py',
    'sdk/rust/os32api/src/generations.rs',
]

FIXTURE = r'''
#include "support.h"
#include "ver_info.h"
#include "os32_kapi_generated.h"
#include "os32_kapi_slots.h"
#include "../lib/kstring.h"
#include "offsets.inc"
static u32 collected, formatted, stack_reports, service_reports, range_calls;
static void *expected_out;
static u32 expected_size;
static char expected_build[VER_BUILD_TEXT];
void sqlite_engine_stack_report(void) { stack_reports++; }
void services_report(void) { service_reports++; }
void ver_collect(VerSnapshot *s) {
    collected++;
    kmemset(s, 0, sizeof(*s));
    s->api_version = KAPI_VERSION;
}
void ver_format(const VerSnapshot *s, OS32_VerInfo *out) {
    CHECK(collected == formatted + 1);
    CHECK(s->api_version == KAPI_VERSION);
    CHECK(equal(s->build, expected_build));
    CHECK(out == expected_out);
    formatted++;
    kmemset(out, 0, sizeof(*out));
    out->count = 1;
    kstrncpy(out->lines[0].text, s->build, OS32_VER_TEXT);
}
static int ring3_user_ranges_writable(u32 p, u32 n, u32 p2, u32 n2) {
    CHECK(p == (u32)expected_out && n == expected_size && !p2 && !n2);
    range_calls++;
    return 1; /* ordinary CPL=0 pass-through */
}
static void ring3_fault_kill(void) { CHECK(0); }
#define KAPI_HIT(n) CHECK((n) == 254)
#define KAPI_OUT_LEN(p, n) ((u32)(n))
#include "api.inc"
#include "wrapper.inc"
static void legacy(void) {
    char buf[VER_BUILD_TEXT], tiny[2] = {'x', 'y'};
    kapi_sys_get_build_info(0, sizeof(buf));
    kapi_sys_get_build_info(buf, 0);
    kapi_sys_get_build_info(buf, -1);
    CHECK(!stack_reports && !service_reports);
    kapi_sys_get_build_info(buf, sizeof(buf));
    CHECK(equal(buf, __DATE__ " " __TIME__));
    CHECK(stack_reports == 1 && service_reports == 1);
    kstrncpy(expected_build, buf, sizeof(expected_build));
    kapi_sys_get_build_info(tiny, 1);
    CHECK(tiny[0] == 0 && tiny[1] == 'y');
    CHECK(stack_reports == 2 && service_reports == 2);
}
void _start(void) {
    struct { u32 before; OS32_VerInfo info; u32 after; } guarded;
    u32 sizes[] = {0, 1, sizeof(OS32_VerInfo)-1, sizeof(OS32_VerInfo)+1, ~0u};
    u32 i, j;
    legacy();
    expected_out = &guarded.info;
    kmemset(&guarded, 0xa5, sizeof(guarded));
    for (i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        expected_size = sizes[i];
        CHECK(wrap_sys_ver_info(expected_out, expected_size) == OS32_ERR_INVAL);
        CHECK(!collected && !formatted);
        for (j = 0; j < sizeof(guarded); j++) CHECK(((u8 *)&guarded)[j] == 0xa5);
    }
    expected_out = 0;
    expected_size = sizeof(OS32_VerInfo);
    CHECK(wrap_sys_ver_info(0, expected_size) == OS32_ERR_INVAL);
    CHECK(!collected && !formatted);
    expected_out = &guarded.info;
    CHECK(wrap_sys_ver_info(expected_out, expected_size) == 0);
    CHECK(collected == 1 && formatted == 1 && range_calls == 7);
    CHECK(guarded.before == 0xa5a5a5a5 && guarded.after == 0xa5a5a5a5);
    CHECK(guarded.info.count == 1 && equal(guarded.info.lines[0].text, expected_build));
    CHECK(stack_reports == 2 && service_reports == 2);
    say("PASS ver API: slot/old offsets, exact size/NULL/no write, build, silent new/legacy reports\n");
    die(0);
}
'''


def generated_layout(runner):
    data = json.loads((ROOT/'sdk/kapi.json').read_text())
    assert data['version'] == 79 and len(data['api']) == 255
    assert data['api'][-1] == dict(name='sys_ver_info', ret='int',
        args=['void *out', 'u32 size'], out=[dict(arg='out', len='size')],
        target='kapi_sys_ver_info')
    # Frozen v78 prefix: a reordered/changed old signature must also fail.
    assert hashlib.sha256(json.dumps(data['api'][:-1], sort_keys=True).encode()).hexdigest() == \
        '28aa38a7adb1cbdc41db6d9fcb74e8895d699e8de0272c2332f2d13024c0586c'
    with tempfile.TemporaryDirectory(prefix='ver-api-gen-') as directory:
        tmp = Path(directory)
        for folder in ('sdk/include/os32', 'sdk/rust/os32api/src', 'kapi', 'exec'):
            (tmp/folder).mkdir(parents=True, exist_ok=True)
        (tmp/'sdk/kapi.json').write_text(json.dumps(data))
        for generator in ('sdk/gen_kapi.py', 'sdk/kapi_rust_gen.py'):
            subprocess.run([sys.executable, str(ROOT/generator)], cwd=tmp, check=True)
        for name in GENERATED:
            assert (tmp/name).read_bytes() == (ROOT/name).read_bytes(), name
    lines = []
    for i, entry in enumerate(data['api']):
        name = entry['name']
        lines.append('STATIC_ASSERT(__builtin_offsetof(KernelAPI, %s) == %d, off_%s);' %
                     (name, 8+4*i, name))
        lines.append('STATIC_ASSERT(KAPI_SLOT_%s == %d, slot_%s);' % (name.upper(), i, name))
    lines += ['STATIC_ASSERT(__builtin_offsetof(KernelAPI, sbrk_heap_limit) == 0x4b8, heap_off);',
              'STATIC_ASSERT(__builtin_offsetof(KernelAPI, shm_base) == 0x4bc, shm_off);',
              'STATIC_ASSERT(sizeof(KernelAPI) == 0x4c0, table_size);',
              'STATIC_ASSERT(OS32X_MIN_API == 79 && KAPI_VERSION == 79, api_version);']
    print('PASS ver API: regeneration exact; frozen 254-slot v78 prefix')
    return '\n'.join(lines)


def run(source, offsets, runner):
    code = ''.join(function(source, sig) for sig in
                   ('static void build_time_copy(', 'void kapi_sys_get_build_info(',
                    'int __cdecl kapi_sys_ver_info('))
    wrapper = function((ROOT/'kapi/kapi_generated.c').read_text(),
                       'int __cdecl wrap_sys_ver_info(')
    try:
        return execute(FIXTURE, runner, {'api.inc': code, 'wrapper.inc': wrapper,
                       'offsets.inc': offsets}, assembly='lib/kstring_asm.asm')
    except subprocess.CalledProcessError as error:
        print(error.stderr)
        raise


def main():
    args = arguments()
    offsets = generated_layout(args.runner)
    source = (ROOT/'kapi/kapi_sys.c').read_text()
    assert run(source, offsets, args.runner).returncode == 0
    if args.mutate:
        old = 'size != sizeof(OS32_VerInfo)'
        assert source.count(old) == 1
        result = run(source.replace(old, 'size == sizeof(OS32_VerInfo)'), offsets, args.runner)
        assert result.returncode == 1 and 'FAIL:' in result.stdout
        print('RED ver API: exact-size condition inverted (compiled runtime failure)')


if __name__ == '__main__':
    main()
