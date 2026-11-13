"""Exercise policy notification at the actual state-changing entry points."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32

CHECK = dict(name='gfx-policy-triggers-host', tier=1, needs=[], budget=5, covers=[
    'gfx/gfx_core.c', 'kernel/console/console.c', 'exec/appslot.c',
    'tools/tests/test_gfx_policy_triggers.py', 'tools/tests/host32.py'])
ROOT = Path(__file__).resolve().parents[2]
CASES = [
    ('claim', 'gfx/gfx_core.c', '        gfx_policy_reeval();'),
    ('shutdown', 'gfx/gfx_core.c', '    gfx_policy_reeval();\n    gfx_reinit_surfaces(0);'),
    ('gui-enter', 'kernel/console/console.c', '    gfx_policy_reeval();\n    if (legacy_active()) return;'),
    ('gui-exit', 'kernel/console/console.c', '    gfx_policy_reeval();\n    /* 票 K7'),
    ('owner-exit', 'exec/appslot.c', '        exec_surface_policy_reeval();'),
]

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

PRELUDE = r'''
#define ATTR_RED 1
#define GFX_OWNER_WM 1
#define GFX_HEIGHT 400
struct caller_access { int app_id; };
typedef struct { void (*leave)(void); void (*shutdown)(void); } Backend;
Backend gfx_backend_geforce, gfx_backend_pc98, *g_backend;
int gfx_started, gfx_current_height, gfx_flip_enabled, gfx_display_page;
int gui, g_gfx_owner, calls, observed_gui, observed_owner, observed_started;
void gfx_policy_reeval(void) { calls++; observed_gui=gui; observed_owner=g_gfx_owner; observed_started=gfx_started; }
int con_dual_active(void) { return 0; }
int con_dual_was_live(void) { return 0; }
void con_dual_session_end(void) { }
int con_sink_is_enabled(void) { return gui; }
void con_sink_enable(void) { gui=1; }
void con_sink_disable(void) { gui=0; }
int pc98_console_safe(void) { return 1; }
int con_backend_safe(void) { return 1; }
int legacy_active(void) { return 0; }
void con_backend_cursor_shape(int n) { (void)n; }
void kbd_inject_discard(void) {}
void tvram_clear(void) {}
void console_hw_cursor_enable(void) {}
int kcg_module_ready(void) { return 1; }
void shell_print(const char *s, int n) { (void)s; (void)n; }
void kapi_sys_exit(int n) { (void)n; }
int appslot_gfx_claim(int n) { (void)n; g_gfx_owner=2; return 0; }
int appslot_gfx_owner(void) { return g_gfx_owner; }
void gf_display_owner_exit(int id) __attribute__((weak));
int ring3_call_from_user(void) { return 0; }
int caller_access_get_user(struct caller_access *c) { c->app_id=2; return 1; }
void display_provider_raster_reset(void) {}
int display_provider_restore_fallback(Backend *b) { (void)b; return 0; }
void gfx_reinit_surfaces(int n) { (void)n; }
int pc98_route_safe(void) { return 0; }
/* <display_fallback.h> の PC-98 の値 (第 5 段 B4)。この fixture の型は Backend。 */
#define DISPLAY_FALLBACK_BACKEND (&gfx_backend_pc98)
void gfx_select_mode(Backend *b, int n) { (void)b; (void)n; }
void gfx_bind_client(void) {}
typedef unsigned long u32;
struct gfx2_adapter { u32 heads; };
struct gfx2_adapter *gfx2_active(void) { return (void *)0; }
void gfx2_head_detach(struct gfx2_adapter *a, u32 h) { (void)a; (void)h; }
void gfx2_register_selected(void) {}
'''
POSTLUDE = r'''
int test(void) {
  gui=1; g_gfx_owner=1; calls=0; if (gfx_kapi_claim() || calls!=1 || observed_owner!=2) return 1;
  calls=0; appslot_gfx_owner_exit(3); if (calls) return 2;
  appslot_gfx_owner_exit(2); if (calls!=1 || observed_owner!=1) return 3;
  gui=0; calls=0; console_text_gdc_stop(); if (calls!=1 || observed_gui!=1) return 4;
  calls=0; console_text_gdc_start(); if (calls!=1 || observed_gui!=0) return 5;
  gfx_started=1; calls=0; gfx_shutdown(); if (calls!=1 || observed_started!=0) return 6;
  return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
'''

def run_case(runner, mutation):
    sources = {p: (ROOT / p).read_text() for _, p, _ in CASES}
    if mutation:
        name, path, anchor = mutation
        assert sources[path].count(anchor) == 1, name
        sources[path] = sources[path].replace(anchor, anchor.replace('exec_surface_policy_reeval();', '(void)0;', 1).replace('gfx_policy_reeval();', '(void)0;', 1), 1)
    code = PRELUDE + '\n' + function((ROOT / 'arch/x86/exec_surface.h').read_text(), 'static inline void exec_surface_policy_reeval(') + '\n' + '\n'.join([
        function(sources['gfx/gfx_core.c'], 'static int gfx_kapi_claim(void)'),
        function(sources['gfx/gfx_core.c'], 'void gfx_shutdown(void)'),
        function(sources['kernel/console/console.c'], 'void console_text_gdc_stop(void)'),
        function(sources['kernel/console/console.c'], 'void console_text_gdc_start(void)'),
        function(sources['exec/appslot.c'], 'void appslot_gfx_owner_exit(int id)'),
    ]) + POSTLUDE
    with tempfile.TemporaryDirectory(prefix='gfx-policy-triggers-') as td:
        src = Path(td) / 'test.c'; exe = Path(td) / 'test'
        src.write_text(code)
        subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', str(src), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner=runner)
        expected = {'claim': 1, 'shutdown': 6, 'gui-enter': 4, 'gui-exit': 5, 'owner-exit': 3}
        assert result.returncode == (expected[mutation[0]] if mutation else 0), (mutation, result.returncode)
        print('RED '+mutation[0] if mutation else 'PASS policy triggers')

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true'); args = parser.parse_args()
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        for mutation in (None, *CASES) if args.mutate else (None,): run_case(runner, mutation)

def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)

if __name__ == '__main__': main()
