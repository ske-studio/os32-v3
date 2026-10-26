"""V6: execute the real saved-state getter with fake, side-effect-free queries.
Only gfx_ver_state and the unchanged gfx_screen_info are extracted; real HAL
headers supply types/mode selection. No display/port/allocator substitutes exist.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'gfx/gfx_core.c'
MUTATIONS = [
    ('active-always', '*active = gfx_started != 0;', '*active = 1;', 'active == 0'),
    ('target-height', 'else if (mode) info.height = mode->height;',
     'else if (mode) info.height = gfx_current_height;', 'out.height == 480'),
    ('active-height', 'if (gfx_started) info.height = gfx_current_height;',
     'if (gfx_started) info.height = 480;', 'out.height == 200'),
    ('query-failure', 'if (rc) return rc;', 'if (rc) return 0;',
     'gfx_ver_state(&out, &kind, &active) == -7'),
    ('pegc-kind', '*kind = GFX_VER_PEGC;', '*kind = GFX_VER_PC98;', 'kind == ids[i]'),
]

HARNESS = r'''
#include "gfx_hal.h"
static int gfx_started, gfx_current_height, queries, query_rc, effects;
static int query(GFX_ScreenInfo *out) {
    ++queries;
    *out = (GFX_ScreenInfo){.width=640, .height=400, .bpp=8,
        .format=GFX_FMT_PACKED8, .lease_first=32, .lease_count=16};
    return query_rc;
}
static int probe(void) { ++effects; return 1; }
static void effect(void) { ++effects; }
static const GfxMode modes[] = {
    {640, 480, GFX_FMT_PACKED8, 8, 0, 0, 640},
    {640, 400, GFX_FMT_PLANAR4, 4, 0, 0, 80}
};
#define BACKEND {.query=query, .probe=probe, .init=effect, .prepare=effect, \
    .shutdown=effect, .enter=effect, .leave=effect, .modes=modes, \
    .mode_count=2, .default_mode=0}
GfxBackend gfx_backend_pc98 = BACKEND;
#ifndef NO_OPTIONAL
GfxBackend gfx_backend_pegc = BACKEND;
GfxBackend gfx_backend_cirrus = BACKEND;
#endif
static GfxBackend other = BACKEND;
const GfxBackend *g_backend;
#include "getter.inc"
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg)-1) : "memory"); finish(1); \
} } while (0)
void _start(void) {
    GFX_ScreenInfo out;
    u32 kind, active;
    GfxBackend *backends[] = {&gfx_backend_pc98,
#ifndef NO_OPTIONAL
        &gfx_backend_pegc, &gfx_backend_cirrus,
#endif
        &other};
    u32 ids[] = {GFX_VER_PC98,
#ifndef NO_OPTIONAL
        GFX_VER_PEGC, GFX_VER_CIRRUS,
#endif
        GFX_VER_UNKNOWN};
    for (u32 i=0; i<sizeof(ids)/sizeof(ids[0]); ++i) {
        g_backend=backends[i]; queries=0; gfx_started=0; gfx_current_height=400;
        CHECK(gfx_ver_state(&out, &kind, &active) == 0);
        CHECK(kind == ids[i]); CHECK(active == 0); CHECK(out.height == 480);
        CHECK(out.width == 640 && out.bpp == 8 && out.format == GFX_FMT_PACKED8);
        CHECK(out.lease_first == 32 && out.lease_count == 16 && queries == 1);
        gfx_started=1; gfx_current_height=200;
        CHECK(gfx_ver_state(&out, &kind, &active) == 0);
        CHECK(active == 1); CHECK(out.height == 200); CHECK(queries == 2);
        gfx_current_height=480;
        CHECK(gfx_ver_state(&out, &kind, &active) == 0 && out.height == 480);
        gfx_started=0; g_backend->query(&out);
        backends[i]->selected_mode=&modes[1];
        CHECK(gfx_ver_state(&out, &kind, &active) == 0 && out.height == 400);
        gfx_screen_info(&out); CHECK(out.height == 400); /* Existing contract. */
        backends[i]->selected_mode=0;
    }
    g_backend=&other; query_rc=-7;
    CHECK(gfx_ver_state(&out, &kind, &active) == -7);
    CHECK(kind == GFX_VER_UNKNOWN && active == 0 && out.width == 0);
    query_rc=0; other.query=0;
    CHECK(gfx_ver_state(&out, &kind, &active) == OS32_ERR_NOSYS);
    g_backend=0;
    CHECK(gfx_ver_state(&out, &kind, &active) == OS32_ERR_NOSYS);
    CHECK(kind == GFX_VER_UNKNOWN && active == 0 && out.height == 0);
    CHECK(gfx_ver_state(0, &kind, &active) == OS32_ERR_INVAL);
    CHECK(gfx_ver_state(&out, 0, &active) == OS32_ERR_INVAL);
    CHECK(gfx_ver_state(&out, &kind, 0) == OS32_ERR_INVAL);
    CHECK(effects == 0);
    finish(0);
}
'''


def run(runner, mutation=None):
    text = (ROOT / SOURCE).read_text()
    start = text.index('int gfx_ver_state(')
    end = text.index('\n/* ======================================================================== */', start)
    getter = text[start:end]
    if mutation:
        _, old, new, _ = mutation
        assert getter.count(old) == 1, mutation
        getter = getter.replace(old, new)
    with tempfile.TemporaryDirectory(prefix='ver-gfx-') as td:
        folder = Path(td)
        (folder / 'getter.inc').write_text(getter)
        (folder / 'fixture.c').write_text(HARNESS)
        # Backend-identity mutations require the optional backend to exist.
        variants = (True,) if mutation else (True, False)
        for optional in variants:
            exe = folder / 'fixture'
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
                   '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                   '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']
            cmd += ['-I' + str(ROOT / p) for p in
                    ('include', 'sdk/include/os32', 'arch/x86', 'platform/pc98')]
            if not optional:
                cmd += ['-DNO_OPTIONAL']
            subprocess.run(cmd + [str(folder / 'fixture.c'), '-o', str(exe)], check=True)
            result = host32.run([str(exe)], runner=runner, capture_output=True,
                                text=True, timeout=30)
            if mutation:
                assert result.returncode == 1 and 'FAIL ' + mutation[3] in result.stdout, result
            else:
                assert result.returncode == 0, result
    print(('RED ' + mutation[0] if mutation else 'PASS saved GFX state') + ' (' + runner + ')')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runner = host32.selected_runner(args.runner)
    run(runner)
    if args.mutate:
        for mutation in MUTATIONS:
            run(runner, mutation)


if __name__ == '__main__':
    main()
