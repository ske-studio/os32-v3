#!/usr/bin/env python3
"""Compile unchanged gshell modules against an explicitly mocked host ABI.
No SDK edits; generated adapters live in target/gshell-host only.

--mutate: after the normal run, apply each mutation in MUTATIONS to a
temporary copy of userland/gshell/{src,host} (the source tree is never
written) and require the named tests to go RED.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'userland/gshell/target/gshell-host'
OUT.mkdir(parents=True, exist_ok=True)
def run(args):
    subprocess.run(args, cwd=ROOT, check=True)
sys.path.insert(0, str(ROOT / 'tools/tests'))
import os32api_host
os32api_host.build(OUT)


def build(gshell: Path, name: str, quiet: bool = False) -> Path:
    """gshell の src/lib.rs を試験用の根に書き換えて rustc --test でビルドする。"""
    src = gshell / 'src'
    root = (src / 'lib.rs').read_text().replace('#![no_std]', '#![allow(dead_code)]').replace('#[no_mangle]', '').replace('pub extern "C" fn main(', 'pub extern "C" fn guest_main(')
    root = re.sub(r'^mod (\w+);', lambda m: '#[path="' + str(src/(m[1]+'.rs')) + '"] mod '+m[1]+';', root, flags=re.M)
    root += '\n#[path="'+str(gshell/'host/mocks.rs')+'"] mod mocks;\n'
    rs = OUT / (name + '_root.rs')
    rs.write_text(root)
    exe = OUT / (name + '-tests')
    args = ['rustc', '--edition=2021', '--test', str(rs), '-L', str(OUT), '--extern', 'os32api='+str(OUT/'libos32api.rlib'), '-o', str(exe)]
    if quiet:
        subprocess.run(args, cwd=ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        run(args)
    return exe


# 票 KBD_NAV (docs/tasks/gui/TASK_KBD_NAV.md) の受け入れ K1 の変異。
# (名前, ファイル, 置き換え前 (ちょうど 1 か所), 置き換え後, RED になるべき試験の絞り込み)
MUTATIONS = [
    ('full ring drains input after STOP', 'src/input.rs',
     '                // Preserve later raw input until STOP changes the foreground.\n                break;',
     '                // Preserve later raw input until STOP changes the foreground.',
     'kstop_full_ring_preserves_following_input_for_next_foreground'),
    ('X3 stops at full ring', 'src/input.rs',
     'if !space_ok && ctx == Ctx::Pump {', 'if !space_ok {',
     'kstop_full_event_ring_drains_raw_and_counts_overflow'),
    ('full ring loses STOP', 'src/input.rs',
     'if scan == SC_STOP && down && (mods & MOD_CTRL) != 0 {', 'if false {',
     'kstop_full_event_ring_drains_raw_and_counts_overflow'),
    ('full ring loses dropped count', 'src/input.rs',
     'ring::add_dropped(st, t.slot, 1);', 'let _ = t;',
     'kstop_full_event_ring_drains_raw_and_counts_overflow'),
    ('scheduler resumes before STOP capture', 'src/multiapp.rs',
     '    crate::input::capture_keyboard(st, crate::input::Ctx::Standalone);', '',
     'kstop_start_return_consumes_stop_before_any_resume'),
    ('scheduler resumes before STOP consumption', 'src/multiapp.rs',
     '    if crate::top_level_abort(st) != 0 {', '    if false {',
     'kstop_start_return_consumes_stop_before_any_resume'),
    ('OWNER_EXIT loses exit kind', 'src/handler.rs',
     'if arg == OWNER_EXIT_ABORTED {', 'if false {',
     'kstop_aborted_exit_consumes_captured_and_deferred_stop'),
    ('captured STOP survives abort', 'src/input.rs',
     'pub fn discard_stop(st: &mut GuiState) {\n    st.abort_seen = false;',
     'pub fn discard_stop(st: &mut GuiState) {',
     'kstop_aborted_exit_consumes_captured_and_deferred_stop'),
    ('deferred STOP survives abort', 'src/input.rs',
     '    st.pending_raw_n = kept;', '    let _ = kept;',
     'kstop_aborted_exit_consumes_captured_and_deferred_stop'),

    ('switch ignores MRU (Z order only)', 'src/kbdnav.rs',
     '        push(out, &mut n, st.kn.mru[i]);', '        let _ = st.kn.mru[i];',
     'k1_1_minimized_window_is_last_in_mru'),
    ('switch on every TAB (not on GRPH release)', 'src/kbdnav.rs',
     '        advance_switch(st, back);\n    } else if back {',
     '        advance_switch(st, back);\n        finish_switch(st);\n        return;\n    } else if back {',
     'k1_1_grph_tab_switches_once_on_release'),
    ('ESC does not cancel the switch', 'src/kbdnav.rs',
     '            cancel_switch(st);\n            return true;', '            return true;',
     'k1_1_grph_tab_esc_cancels'),
    ('taskbar presses the front, not the selection', 'src/taskbar.rs',
     '        Some(id) => id,\n        None => st.front_id(),',
     '        Some(_) => st.front_id(),\n        None => st.front_id(),',
     'k1_1_grph_tab_switches_once_on_release'),
    ('GRPH+f4 during modal does not cancel', 'src/kbdnav.rs',
     '            modal::on_key(st, SC_ESC, 0x1B, 0);', '',
     'k1_2_modal_swallows'),
    ('modal does not stop the shortcuts', 'src/kbdnav.rs',
     '    if modal::is_open() {\n        if k == Shortcut::Close {',
     '    if false {\n        if k == Shortcut::Close {',
     'k1_2_modal_swallows'),
    ('X4 runs WM keys instead of deferring', 'src/input.rs',
     'if defer || (ctx == Ctx::Pump && kbdnav::is_wm_raw(st, raw)) {', 'if defer {',
     'k1_3_pump_defers'),
    ('X4 does not defer the kana raw', 'src/kbdnav.rs',
     '    if scan == SC_KANA {\n        return true;\n    }\n', '',
     'k1_4_kana_release_read_by_the_pump'),
    ('kana mode from the current value, not the raw bit', 'src/kbdnav.rs',
     '        set_mousekeys(st, (mods & MOD_KANA) != 0);',
     '        set_mousekeys(st, (unsafe { (os32api::api().kbd_get_modifiers)() } & MOD_KANA) != 0);',
     'k1_4_kana_bit_is_taken_from_each_raw'),
    ('kana off keeps the held button', 'src/kbdnav.rs',
     '    if !on {\n        release_held(st);\n    }', '',
     'k1_4_held_button_is_released'),
    ('consumed numpad break leaks to the app', 'src/kbdnav.rs',
     '    if c {\n        set_consumed(st, scan, true);\n    }', '',
     'k1_4_numpad5_is_a_click'),
    ('mousekey edge bypasses the modal', 'src/input.rs',
     'pub fn edge_x3(st: &mut GuiState, x: i32, y: i32, button: u8, down: bool) {\n    if modal::is_open() {',
     'pub fn edge_x3(st: &mut GuiState, x: i32, y: i32, button: u8, down: bool) {\n    if false {',
     'k1_4_numpad5_during_modal'),
    ('minus is a left click', 'src/kbdnav.rs',
     '        NP_MINUS => click(st, BTN_RIGHT),', '        NP_MINUS => click(st, BTN_LEFT),',
     'k1_4_minus_is_a_right_click'),
    ('plus is a single click', 'src/kbdnav.rs',
     '            click(st, BTN_LEFT);\n            click(st, BTN_LEFT);', '            click(st, BTN_LEFT);',
     'k1_4_plus_is_a_double_click'),
    ('acceleration 12 -> 6', 'src/kbdnav.rs',
     'const MK_ACCEL: [(u32, i32); 3] = [(7, 3), (15, 12), (u32::MAX, 24)];',
     'const MK_ACCEL: [(u32, i32); 3] = [(7, 3), (15, 6), (u32::MAX, 24)];',
     'k1_5_numpad6_twenty_makes'),
    ('break does not reset the count', 'src/kbdnav.rs',
     '                st.kn.mk_count = 0; /* break で数え直し */', '',
     'k1_5_numpad6_twenty_makes'),
    ('the still real mouse rewinds the pointer', 'src/input.rs',
     'let (mx, my) = if real_moved { (rx, ry) } else { (st.mouse_x, st.mouse_y) };',
     'let (mx, my) = (rx, ry);',
     'k1_5_numpad6_twenty_makes'),
    ('CTRL is ignored', 'src/kbdnav.rs',
     '        let slow = (mods & MOD_CTRL) != 0;', '        let slow = false;',
     'k1_8_ctrl_numpad6'),
    ('the half-dot remainder is dropped', 'src/kbdnav.rs',
     '    *acc -= d * MK_SLOW_DEN;', '    *acc = 0;',
     'k1_8_ctrl_numpad6'),
    ('mousekey move does not follow the drag', 'src/input.rs',
     '    if st.drag_index >= 0 {\n        update_drag(st, x, y);\n    } else {\n        cursor::move_to(st, x, y);\n    }\n    let b',
     '    cursor::move_to(st, x, y);\n    let b',
     'k1_7_drag'),
    ('no FEP commit before a WM focus change', 'src/wm.rs',
     '    if x3 {\n        fep::commit_to(st, old);\n    } else {', '    if x3 {\n    } else {',
     'k1_6_'),
    ('the commit goes to the new focus', 'src/fep.rs',
     '    let t = input::target_of(st, win_id);', '    let t = input::focus_target(st);',
     'k1_6_app_set_focus'),
    ('X1 set_focus runs the conversion at once', 'src/wm.rs',
     '        fep::defer_commit(old);', '        fep::commit_to(st, old);',
     'k1_6_app_set_focus'),
    # 代行レビュー (Fable 5.1、1f69b44) の指摘 P2-1〜P3-5 の守り。
    ('P2-1 the deferred commit is dropped during a modal', 'src/fep.rs',
     '        return; /* 予約は残す */', '        state().commit_for = 0;\n        return;',
     'review_p2_1'),
    ('P2-1 the deferred commit waits for the next cycle', 'src/input.rs',
     '        if ctx.wm_ui() {\n            fep::apply_pending_commit(st);\n        }\n\n        /* 押下は',
     '        /* 押下は',
     'review_p2_1'),
    ('P2-2 app set_focus raises a minimized window', 'src/wm.rs',
     '    if st.windows[index].minimized {\n        return 0;\n    }\n    /* 未確定文字は',
     '    /* 未確定文字は',
     'review_p2_2'),
    ('app move keeps maximized', 'src/wm.rs',
     '    /* アプリが動かしたら最大化ではない (元のサイズの記憶は捨てる)。 */\n    st.windows[index].maximized = false;\n',
     '',
     'review_app_move_clears_maximized'),
    ('P3-1 one RETURN only', 'src/fep.rs',
     'const COMMIT_ROUNDS_MAX: usize = 16;', 'const COMMIT_ROUNDS_MAX: usize = 1;',
     'review_p3_1'),
    ('P3-2 switch finishes during a modal', 'src/kbdnav.rs',
     '    if modal::is_open() {\n        cancel_switch(st);\n        return;\n    }\n', '',
     'review_p3_2'),
    ('P3-3 the pump keeps a stale consumed mark', 'src/input.rs',
     '        if ctx == Ctx::Pump && down {\n            kbdnav::pump_delivered_make(st, scan);\n        }\n', '',
     'review_p3_3'),
    ('P3-4 window menu starts on Restore', 'src/startmenu.rs',
     '            m().cursor = i;\n            break;', '            break;',
     'winmenu_move_by_arrows'),
    ('P3-5 the frame stays when the window goes away', 'src/wm.rs',
     '    st.drag_frame = Rect::EMPTY;\n    kbdnav::drop_kmove(st);\n    input::erase_frame(st, f);',
     '    kbdnav::drop_kmove(st);\n    let _ = f;',
     'review_p3_5'),
    ('P3-A the keyboard move state survives the window', 'src/wm.rs',
     '    kbdnav::drop_kmove(st);\n', '',
     'review_p3_a'),
    ('P3-B TAB during a modal keeps advancing the switch', 'src/kbdnav.rs',
     '    if st.kn.sw_active && modal::is_open() {\n        cancel_switch(st);\n    }\n', '',
     'review_p3_b'),
    # gui_gate v11 (2026-09-29): ドラッグ枠の跡が背面窓のクライアント面に残る / 旧前面のタイトル。
    ('drag frame erase does not repaint the clients it crossed', 'src/input.rs',
     '    erase_frame_edges(st, old_frame);\n    expose_frame_edges(st, old_frame);\n',
     '    erase_frame_edges(st, old_frame);\n',
     'drag_frame_trail_'),
    ('the old front keeps the active title', 'src/wm.rs',
     '            let o = st.windows[oi].outer();\n            st.dirty_screen(o);\n',
     '            let _ = oi;\n',
     'focus_change_repaints_the_old_front_title'),
    # Codex レビュー (2026-09-29) P2×2: 帯が外接矩形へ畳まれる / COMMIT が今の枠を消す。
    ('frame bands merge into their bounding box', 'src/input.rs',
     'crate::damage::add_dirty_band(&mut st.windows[i], e.translate(-ox, -oy));',
     'crate::damage::add_dirty(&mut st.windows[i], e.translate(-ox, -oy));',
     'drag_frame_paint_stays_band_shaped'),
    ('band merge ignores the area bound', 'src/damage.rs',
     'if area(&u) <= area(&d) + area(&r) && (d.intersects(&r) || near(&d, &r)) {',
     'if d.intersects(&r) || near(&d, &r) {',
     'drag_frame_paint_stays_band_shaped'),
    ('app commit does not restore the live frame', 'src/handler.rs',
     '    if edges.is_some() {\n        input::draw_live_outline(st);\n    }\n',
     '',
     'app_commit_during_drag_keeps_the_live_frame'),
    # Codex レビュー 2 回目 (2026-09-29) P2×2。
    ('commit discards a cursor the app did not touch', 'src/handler.rs',
     '        if cr.intersects(&touched) {\n            cursor::discard(st);',
     '        if true {\n            cursor::discard(st);',
     'commit_frame_redraw_under_the_cursor_saves_the_real_background'),
    ('drop leaves the last frame when it differs from the outer', 'src/input.rs',
     '        if nf != st.windows[idx].outer() {',
     '        if false && nf != st.windows[idx].outer() {',
     'drop_after_app_resize_erases_the_last_frame'),
    # Codex レビュー 3 回目 (2026-09-29) P2×2: 重なり順 アプリ < 枠 < 最前面物 < カーソル。
    ('overlays are judged by the app rects only', 'src/handler.rs',
     '    let overlays = wm::overlays_to_refresh(st, &regions);',
     '    let overlays = wm::overlays_to_refresh(st, &regions[..1]);',
     'commit_frame_redraw_keeps_the_modal_on_top'),
    ('the cursor is drawn before the overlays', 'src/handler.rs',
     '    wm::refresh_overlays(st, &overlays);\n    if cursor_hit {\n        cursor::show(st);\n        let cr = cursor::rect(st);\n        wm::queue_present(st, cr);\n    }\n',
     '    if cursor_hit {\n        cursor::show(st);\n        let cr = cursor::rect(st);\n        wm::queue_present(st, cr);\n    }\n    wm::refresh_overlays(st, &overlays);\n',
     'commit_overlay_redraw_keeps_the_cursor'),
    # Codex レビュー 4 回目 (2026-09-29) P2: FEP は X3 と同じく最上位。
    ('FEP is refreshed below the taskbar', 'src/wm.rs',
     '    if !list[1].is_empty() && taskbar::refresh_if_hit(st, list[1]) {\n        queue_present(st, list[1]);\n    }\n    if !list[2].is_empty() && startmenu::refresh_if_hit(st, list[2]) {\n        queue_present(st, list[2]);\n    }\n    if !list[3].is_empty() && fep::refresh_if_hit(st, list[3]) {\n        queue_present(st, list[3]);\n    }\n',
     '    if !list[3].is_empty() && fep::refresh_if_hit(st, list[3]) {\n        queue_present(st, list[3]);\n    }\n    if !list[1].is_empty() && taskbar::refresh_if_hit(st, list[1]) {\n        queue_present(st, list[1]);\n    }\n    if !list[2].is_empty() && startmenu::refresh_if_hit(st, list[2]) {\n        queue_present(st, list[2]);\n    }\n',
     'fep_over_taskbar_keeps_its_order'),
]


def one_mutation(k: int) -> tuple:
    """変異 k を一時の写しに当ててビルドし、絞り込んだ試験を走らせる。(ok, 行)。"""
    name, rel, old, new, filt = MUTATIONS[k]
    with tempfile.TemporaryDirectory() as td:
        g = Path(td) / 'gshell'
        shutil.copytree(ROOT / 'userland/gshell/src', g / 'src')
        shutil.copytree(ROOT / 'userland/gshell/host', g / 'host')
        f = g / rel
        text = f.read_text()
        if text.count(old) != 1:
            return False, f'MUTATION BROKEN  {name}: {rel} has {text.count(old)} matches'
        f.write_text(text.replace(old, new))
        try:
            exe = build(g, f'mutant{k}', quiet=True)
        except subprocess.CalledProcessError:
            return False, f'MUTATION NOT BUILT  {name}'
        r = subprocess.run([str(exe), '--test-threads=1', filt], cwd=ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        exe.unlink(missing_ok=True)
        ran = re.search(r'test result: \w+\. (\d+) passed; (\d+) failed', r.stdout)
        if r.returncode != 0 and ran and int(ran[2]) > 0:
            return True, f'RED  {name}  ({ran[2]} failed)'
        return False, f'SURVIVED  {name}  (filter {filt!r})'


def mutate() -> int:
    import concurrent.futures
    import os
    jobs = max(1, min(8, os.cpu_count() or 1))
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(one_mutation, range(len(MUTATIONS))))
    bad = 0
    for ok, line in results:
        print(line)
        if not ok:
            bad += 1
    print(f'kbdnav mutations: {len(MUTATIONS) - bad}/{len(MUTATIONS)} killed')
    return bad


exe = build(ROOT / 'userland/gshell', 'integration')
run([str(exe), '--test-threads=1'])
if '--mutate' in sys.argv[1:]:
    sys.exit(1 if mutate() else 0)
