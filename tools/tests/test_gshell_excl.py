"""Run stage B against the gshell host ABI and real window rings."""
from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='gshell-excl-host', tier=1,
    covers=['kernel/av_focus.c', 'include/av_contract.h', 'exec/exec_gui.h', 'exec/exec_hooks.h', 'input/hal/in2_int.h', 'include/app_ids.h', 'include/callconv.h', 'include/abi.h', 'arch/x86/arch_abi.h', 'userland/gshell/**', 'sdk/rust/os32api/**', 'sdk/rust/os32api/src/ls.rs', 'tools/tests/os32api_host.py', 'exec/shell_service.h', 'userland/lib/font/**', 'sdk/include/os32/os32_kapi_shared.h', 'tools/fetch_fonts.py', 'tools/tests/g16_fixture.py', 'tools/gen_g16.py', 'tools/g16/**', 'assets/ipadic/*.csv', 'tools/tests/test_gshell_excl.py', 'sdk/include/os32/**', 'include/remote_control.h', 'net/link.h', 'assets/fonts/**'],
    needs=[], budget=12.5,
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §5-3, G1', 'docs/decisions/INPUT_HAL_DESIGN.md 7d'],
)

MUTATIONS = [
    ('ignore generation', 'src/excl.rs', 'old != next', 'old.flags != next.flags'),
    ('record rejected append', 'src/excl.rs', 'if !accepted { return false; }', ''),
    ('forget pointer buttons', 'src/excl.rs', 'rec.buttons = ev.sub;', 'rec.buttons = 0;'),
    ('repeat accepted button release', 'src/excl.rs', 'st.excl.delivered[i].buttons &= !bit;',
     'st.excl.delivered[i].buttons |= bit;'),
    ('skip direct input observation', 'src/input.rs', 'crate::excl::observe(st);', ''),
]

def mutations():
    return [(*change, 'excl_release_after_full_ring') for change in MUTATIONS] + ENTRY_MUTATIONS


ENTRY_MUTATIONS = [
    ('reused window slot accepted', 'src/excl.rs', 'st.win_by_id(rec.window) != Some(i)',
     '!st.windows[i].used', 'excl_old_window_identity'),
    ('query error accepted', 'src/excl.rs', 'self.valid && self.observed',
     'self.observed', 'excl_query_error'),
    ('standalone skips observation', 'src/lib.rs', 'input::capture_keyboard(st, input::Ctx::Standalone)',
     'input::capture_keyboard_observed(st, input::Ctx::Standalone)', 'excl_direct_standalone'),
    ('wait skips observation', 'src/handler.rs', 'input::capture_keyboard(st, input::Ctx::Wait)',
     'input::capture_keyboard_observed(st, input::Ctx::Wait)', 'excl_direct_wait'),
    ('resume skips observation', 'src/multiapp.rs', 'crate::input::capture_keyboard(st, crate::input::Ctx::Standalone)',
     'crate::input::capture_keyboard_observed(st, crate::input::Ctx::Standalone)', 'excl_direct_resume'),
    ('pump skips observation', 'src/input.rs', '    capture_keyboard(st, ctx);',
     '    capture_keyboard_observed(st, ctx);', 'excl_direct_pump'),
    ('regen skips observation', 'src/wm.rs', 'crate::excl::observe(st);', '', 'excl_direct_cycle'),
]

def run(ctx):
    argv = sys.argv
    try:
        sys.argv = [str(ROOT / 'userland/gshell/host/integration.py'), '--excl']
        host = runpy.run_path(sys.argv[0])
        if ctx.mutate:
            host['MUTATIONS'][:] = mutations()
            return int(bool(host['mutate']()))
        return 0
    finally:
        sys.argv = argv


if __name__ == '__main__':
    from types import SimpleNamespace
    raise SystemExit(run(SimpleNamespace(mutate='--mutate' in sys.argv)))
