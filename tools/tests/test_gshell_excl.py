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
    ('ignore generation', 'src/excl.rs', '*old != next', 'old.flags != next.flags'),
    ('record rejected append', 'src/excl.rs', 'if !accepted {', 'if false {'),
    ('forget pointer buttons', 'src/excl.rs', 'rec.buttons = ev.sub;', 'rec.buttons = 0;'),
    ('repeat accepted button release', 'src/excl.rs', 'st.excl.delivered[i].buttons &= !bit;',
     'st.excl.delivered[i].buttons |= bit;'),
    ('skip direct input observation', 'src/input.rs', 'crate::excl::observe(st);', ''),
]

def mutations():
    return [(*change, 'excl_release_after_full_ring') for change in MUTATIONS] + ENTRY_MUTATIONS


ENTRY_MUTATIONS = [
    ('fallback leaks into new window', 'src/input.rs',
     'if stop_raw(raw) { st.abort_seen = true; }\n            continue;',
     'if stop_raw(raw) { st.abort_seen = true; }', 'excl_switch_fallback'),
    ('keep stale window purge after changing owner', 'src/excl.rs',
     'old.owner == next.owner &&\n                (st.excl.purge_same_owner || old.window != next.window)',
     'st.excl.purge_same_owner || (old.owner == next.owner && old.window != next.window)', 'excl_owner_change_supersedes'),

    ('stop reading full deferred queue during exclusivity', 'src/input.rs',
     'if ordinary_pump && !ui_defer && st.pending_raw_n == st.pending_raw.len()', 'if ctx == Ctx::Pump && !ui_defer && st.pending_raw_n == st.pending_raw.len()', 'excl_full_pump'),
    ('inject fullscreen TAB with KBD closed', 'src/input.rs',
     'if !st.excl.open(crate::excl::KBD) { continue; }', 'if !st.excl.open(crate::excl::KBD) && !crate::fullscreen::active() { continue; }', 'excl_fullscreen_tab'),
    ('restart synthetic hold while exclusive', 'src/kbdnav.rs',
     'if st.excl.open(crate::excl::MOUSE) {', 'if true {', 'excl_synthetic_mouse'),
    ('lose keyboard batch termination', 'src/input.rs',
     'if end_batch || !st.excl.open(crate::excl::MOUSE)', 'if !st.excl.open(crate::excl::MOUSE)', 'excl_switch_ends'),

    ('prefer repeat over ordinary', 'src/input.rs', 'else if repeat { 1 } else if wm_scan(scan) { 2 } else { 0 }', 'else if repeat { 0 } else if wm_scan(scan) { 2 } else { 1 }', 'excl_deferred'),

    ('prefer modifier over repeat', 'src/input.rs', 'else if repeat { 1 } else if wm_scan(scan) { 2 }',
     'else if repeat { 2 } else if wm_scan(scan) { 1 }', 'excl_deferred'),
    ('forget finish reservation', 'src/input.rs', 'let reserve_finish = start || (st.pending_switch && !finish);',
     'let reserve_finish = false;', 'excl_deferred'),
    ('borrow STOP seat', 'src/input.rs', '- usize::from(reserve_stop)', '', 'excl_pending_switch'),
    ('purge protected ordinary finish', 'src/input.rs', ' || st.pending_rank[i] == 3', '', 'excl_pending_switch'),
    ('skip same owner raw cleanup', 'src/input.rs', 'if same_owner {', 'if false {', 'excl_same_owner'),
    ('skip physical key resync', 'src/input.rs', 'if *held && unsafe', 'if false && *held && unsafe', 'excl_same_owner'),
    ('expose physical pointer mask', 'src/input.rs', 'let pbtn = btn | st.synth_buttons;', 'let pbtn = mi.buttons | st.synth_buttons;', 'excl_mouse_masks'),
    ('skip static cursor restore', 'src/input.rs', 'st.excl.mouse_baseline = false;\n        cursor::move_to(st, mx, my);',
     'st.excl.mouse_baseline = false;', 'excl_mouse_masks'),
    ('show exclusive cursor', 'src/cursor.rs', '!st.excl.cursor_allowed() || ', '', 'excl_mouse_masks'),
    ('keep drag on boundary', 'src/input.rs', 'if st.drag_index >= 0 { wm::drop_drag_frame(st); }', '', 'excl_drag_cancel'),
    ('process right after left changes focus', 'src/input.rs', 'if front != st.front_id() { st.excl.mouse_baseline = true; return; }',
     '', 'excl_mouse_focus'),
    ('forget successful focus notification requirement', 'src/multiapp.rs',
     'if unsafe { (os32api::api().av_focus_set)(target) } == 0 {', 'if unsafe { (os32api::api().av_focus_set)(target) } <= 0 {',
     'excl_foreground_failure'),
    ('disable old window wake while settling', 'src/multiapp.rs', 'Some(s) => ring::pending(st, s) > 0 || session::quit_pending(s),',
     'Some(s) => st.excl.open(crate::excl::KBD) && ring::pending(st, s) > 0 || session::quit_pending(s),', 'excl_foreground_failure'),
    ('deferred FEP runs with KBD closed', 'src/input.rs',
     'if ctx.wm_ui() && st.excl.open(crate::excl::KBD) {', 'if ctx.wm_ui() {', 'excl_fep_pending'),
    ('trailing FEP bypasses gate', 'src/input.rs',
     'if st.front_id() == front && st.excl.open(crate::excl::KBD) {', 'if true {', 'excl_fep_pending'),
    ('zero pointer forgotten', 'src/excl.rs', 'rec.pointer = true;', 'rec.pointer = ev.sub != 0;', 'excl_zero_pointer'),
    ('button overwrites pointer position', 'src/excl.rs', 'let previously_down = rec.buttons;',
     'rec.pointer_xy = xy; let previously_down = rec.buttons;', 'excl_zero_pointer'),
    ('failed release forgotten', 'src/excl.rs', 'rec.retry_buttons |= bit;', 'rec.retry_buttons = 0;', 'excl_full_release'),
    ('full ring blocks switch', 'src/input.rs', 'if ctx.wm_ui() && scan == SC_TAB',
     'if space_ok && ctx.wm_ui() && scan == SC_TAB', 'excl_switch_full'),

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
