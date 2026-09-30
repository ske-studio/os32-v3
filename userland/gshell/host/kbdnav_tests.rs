//! kbdnav_tests.rs — 票 KBD_NAV の受け入れ K1 (ホスト試験)。
//!
//! `src/kbdnav.rs` の末尾から `#[cfg(test)] #[path]` で取り込み、
//! `host/integration.py` (`make check-gshell-host`) が走らせる。gshell の入力部を
//! **偽の raw 列** (`mocks::push_rawkeys`) で駆動し、アプリのリングに積まれた
//! イベントで判定する。項目番号は票 §4 の K1 ①〜⑦ (+ ⑧ CTRL の半分速度)。

use crate::wm::{GuiState, Win};
use crate::{fep, input, kbdnav, mocks, modal, slot, startmenu, wm};
use os32api::gui::proto::{
    GuiEvent, GUI_EV_BUTTON, GUI_EV_FOCUS, GUI_EV_KEY, GUI_EV_POINTER, GUI_EV_TEXT,
    GUI_MODAL_YES_NO, GUI_RING_CAPACITY,
};

/* ---- raw の組み立て (drivers/kbd.c: keycode | down<<8 | mods<<9) ---- */
const DOWN: i32 = 1 << 8;
const SHIFT: i32 = 0x01 << 9;
const KANA: i32 = 0x04 << 9;
const GRPH: i32 = 0x08 << 9;
const CTRL: i32 = 0x10 << 9;

const SC_ESC: i32 = 0x00;
const SC_TAB: i32 = 0x0F;
const SC_A: i32 = 0x1D;
const SC_B: i32 = 0x2D;
const SC_N: i32 = 0x2E;
const SC_I: i32 = 0x17;
const SC_H: i32 = 0x22;
const SC_SPACE: i32 = 0x34;
const SC_F4: i32 = 0x65;
const SC_F10: i32 = 0x6B;
const SC_KANA: i32 = 0x72;
const SC_GRPH: i32 = 0x73;
const NP_6: i32 = 0x48;
const NP_5: i32 = 0x47;
const NP_PLUS: i32 = 0x49;
const NP_0: i32 = 0x4E;
const NP_DOT: i32 = 0x50;

/// make + break (同じ修飾)。
fn tap(scan: i32, mods: i32) -> [i32; 2] {
    [scan | DOWN | mods, scan | mods]
}

/* ---- 状態の組み立て ---- */

/// 窓を `rects` の順に作る (添字 i = 所有者 2+i、スロット i)。Z 順も同じ順
/// (最後が最前面)。MRU も最前面が先頭になるように積む。
fn windows(shm: &mocks::Shm, rects: &[(i32, i32, i32, i32)]) -> GuiState {
    let mut st = GuiState::NEW;
    st.shm_base = shm.base();
    let mut i = 0;
    while i < rects.len() {
        st.slots[i].used = true;
        st.slots[i].owner = 2 + i as i32;
        slot::init_header(&st, i);
        let mut w = Win::EMPTY;
        w.used = true;
        w.visible = true;
        w.owner = 2 + i as i32;
        w.gen = 1;
        w.flags = os32api::gui::proto::GUI_WF_DEFAULT;
        w.x = rects[i].0;
        w.y = rects[i].1;
        w.w = rects[i].2;
        w.h = rects[i].3;
        st.windows[i] = w;
        st.zorder[i] = i;
        i += 1;
    }
    st.z_count = rects.len();
    let mut k = 0;
    while k < rects.len() {
        let id = st.windows[k].id(k);
        kbdnav::mru_touch(&mut st, id);
        k += 1;
    }
    st
}

/// FEP をオフに戻す (静的な FEP 状態を前後の試験へ持ち越さない)。
fn fep_off() {
    unsafe extern "C" fn off() -> i32 {
        0
    }
    unsafe {
        (*os32api::api_ptr()).ime_is_active = off;
    }
    fep::install();
}

/// ポインタを (x, y) に置く (実マウスをそこへ動かして 1 周)。リングは空にする。
fn park_pointer(st: &mut GuiState, x: i32, y: i32) {
    *mocks::MOUSE.lock().unwrap() = (x as i16, y as i16, 0);
    input::capture(st, input::Ctx::Wait);
    clear_rings(st);
}

fn clear_rings(st: &GuiState) {
    let mut i = 0;
    while i < 4 {
        if st.slots[i].used {
            slot::init_header(st, i);
        }
        i += 1;
    }
}

fn events(st: &GuiState, slot_i: usize) -> Vec<GuiEvent> {
    let h = slot::read_header(st, slot_i);
    let base = slot::ring_ptr(st, slot_i);
    let mut out = Vec::new();
    let mut i = h.ring_head;
    while i != h.ring_tail {
        let ev: GuiEvent = unsafe {
            core::ptr::read_unaligned(
                base.add((i as usize % GUI_RING_CAPACITY) * 16) as *const GuiEvent
            )
        };
        out.push(ev);
        i = i.wrapping_add(1);
    }
    out
}

/// (down, button) の列。
fn buttons(st: &GuiState, slot_i: usize) -> Vec<(bool, u8)> {
    events(st, slot_i)
        .iter()
        .filter(|e| e.kind == GUI_EV_BUTTON)
        .map(|e| (e.sub != 0, e.payload[4]))
        .collect()
}

/// (in?, window) の列。
fn focus(st: &GuiState, slot_i: usize) -> Vec<(bool, u32)> {
    events(st, slot_i)
        .iter()
        .filter(|e| e.kind == GUI_EV_FOCUS)
        .map(|e| (e.sub != 0, e.window))
        .collect()
}

/// (down, scan, ch, mods) の列。
fn keys(st: &GuiState, slot_i: usize) -> Vec<(bool, u8, u8, u8)> {
    events(st, slot_i)
        .iter()
        .filter(|e| e.kind == GUI_EV_KEY)
        .map(|e| (e.sub != 0, e.payload[0], e.payload[1], e.payload[2]))
        .collect()
}

fn text(st: &GuiState, slot_i: usize) -> Vec<u8> {
    let mut out = Vec::new();
    for e in events(st, slot_i) {
        if e.kind == GUI_EV_TEXT {
            let n = ((e.sub & 0x7F) as usize).min(8);
            out.extend_from_slice(&e.payload[..n]);
        }
    }
    out
}

fn run(st: &mut GuiState, raws: &[i32]) {
    mocks::push_rawkeys(raws);
    input::capture(st, input::Ctx::Wait);
}

fn id(st: &GuiState, i: usize) -> u32 {
    st.windows[i].id(i)
}

/* ================================================================ */
/*  ① GRPH+TAB — GRPH を離したときに 1 回だけ切り替える (MRU 順)     */
/* ================================================================ */

/// MRU が A(前)・B・C(最小化) のとき GRPH↓ TAB TAB GRPH↑ → GRPH↑ の時点で
/// 1 回だけ C が元に戻って前、Focus は A→C の 1 回だけ (B へは出ない)。
#[test]
fn k1_1_grph_tab_switches_once_on_release_in_mru_order() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    /* 添字 0 = C (最背面)、1 = B、2 = A (最前面)。 */
    let mut st = windows(&shm, &[(20, 20, 200, 120), (60, 60, 200, 120), (100, 100, 200, 120)]);
    let (c, b, a) = (0usize, 1usize, 2usize);
    wm::minimize(&mut st, c);
    assert!(st.windows[c].minimized && !st.windows[c].visible);
    assert_eq!(st.front_index(), Some(a));
    park_pointer(&mut st, 600, 10);

    let mut r = vec![SC_GRPH | DOWN | GRPH];
    r.extend_from_slice(&tap(SC_TAB, GRPH));
    /* 途中 (2 回目の TAB の後、GRPH↑ の前) で止めて「まだ何も動いていない」を見る。 */
    r.extend_from_slice(&tap(SC_TAB, GRPH));
    run(&mut st, &r);
    assert_eq!(st.front_index(), Some(a), "GRPH を離す前に切り替わった");
    assert_eq!(kbdnav::switch_selection(&st), Some(id(&st, c)), "選択が C でない");
    assert_eq!(crate::taskbar::pressed_id(&st), id(&st, c), "タスクバーの押し込みが選択を示さない");
    assert!(focus(&st, a).is_empty() && focus(&st, b).is_empty() && focus(&st, c).is_empty());

    run(&mut st, &[SC_GRPH]);
    assert_eq!(st.front_index(), Some(c), "GRPH↑ で C へ切り替わらない");
    assert!(!st.windows[c].minimized && st.windows[c].visible, "C が元に戻っていない");
    assert_eq!(focus(&st, a), vec![(false, id(&st, a))], "A の Focus out が 1 回でない");
    assert_eq!(focus(&st, c), vec![(true, id(&st, c))], "C の Focus in が 1 回でない");
    assert!(focus(&st, b).is_empty(), "B へ Focus が出た: {:?}", focus(&st, b));
    /* TAB の Key はどこへも配られない。 */
    for s in 0..3 {
        assert!(keys(&st, s).is_empty(), "WM のキーがアプリへ漏れた: slot {s}");
    }
}

/// 途中で ESC → 何も変わらない (ESC の make / break もアプリへ行かない)。
#[test]
fn k1_1_grph_tab_esc_cancels() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (60, 60, 200, 120), (100, 100, 200, 120)]);
    wm::minimize(&mut st, 0);
    park_pointer(&mut st, 600, 10);
    let mut r = vec![SC_GRPH | DOWN | GRPH];
    r.extend_from_slice(&tap(SC_TAB, GRPH));
    r.extend_from_slice(&tap(SC_TAB, GRPH));
    r.extend_from_slice(&tap(SC_ESC, GRPH));
    r.push(SC_GRPH);
    run(&mut st, &r);
    assert_eq!(st.front_index(), Some(2), "ESC で取り消したのに切り替わった");
    assert!(st.windows[0].minimized, "取り消したのに C が戻った");
    for s in 0..3 {
        assert!(focus(&st, s).is_empty(), "Focus が出た: slot {s}");
        assert!(keys(&st, s).is_empty(), "キーがアプリへ漏れた: slot {s}");
    }
    assert_eq!(kbdnav::switch_selection(&st), None);
}

/// 初期状態 A・B で GRPH↓ TAB GRPH↑ を 2 回 → B → A と戻る (MRU)。
#[test]
fn k1_1_grph_tab_twice_goes_back_and_forth() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    /* 0 = B、1 = A (最前面)。 */
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    let one = [SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH, SC_GRPH];
    run(&mut st, &one);
    assert_eq!(st.front_index(), Some(0), "1 回目で B にならない");
    run(&mut st, &one);
    assert_eq!(st.front_index(), Some(1), "2 回目で A に戻らない (MRU でない)");
    /* SHIFT+TAB は逆回り: 2 枚なら同じく相手へ。 */
    run(
        &mut st,
        &[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH | SHIFT, SC_TAB | GRPH | SHIFT, SC_GRPH],
    );
    assert_eq!(st.front_index(), Some(0), "SHIFT+TAB で切り替わらない");
}

/// 最小化した窓は Z 順の途中に居ても MRU の末尾 (Win98 と同じ)。
/// Z 順 (前から) で並べる実装だと 1 回目の TAB が最小化した窓に当たる。
#[test]
fn k1_1_minimized_window_is_last_in_mru_even_if_higher_in_z() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    /* Z: 0 = B (最背面)、1 = C、2 = A (最前面)。C を最小化 → MRU は A・B・C。 */
    let mut st = windows(&shm, &[(20, 20, 200, 120), (60, 60, 200, 120), (100, 100, 200, 120)]);
    wm::minimize(&mut st, 1);
    park_pointer(&mut st, 600, 10);
    run(&mut st, &[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH, SC_GRPH]);
    assert_eq!(st.front_index(), Some(0), "1 回目の TAB が MRU の 2 番目 (B) でない");
    assert!(st.windows[1].minimized, "最小化した窓が戻った");
}

/* ================================================================ */
/*  ② モーダル中: §1-2 の表の全キーでアプリのリングに何も積まれない  */
/* ================================================================ */

#[test]
fn k1_2_modal_swallows_wm_keys_and_grph_f4_cancels_the_dialog() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    modal::open_wm_message(
        &mut st,
        GUI_MODAL_YES_NO,
        b"halt?\0",
        modal::WM_PURPOSE_CONFIRM_HALT,
    );
    assert!(modal::is_open());
    let mut r = Vec::new();
    r.extend_from_slice(&tap(SC_ESC, CTRL));
    r.push(SC_GRPH | DOWN | GRPH);
    r.extend_from_slice(&tap(SC_TAB, GRPH));
    r.extend_from_slice(&tap(SC_ESC, GRPH));
    r.extend_from_slice(&tap(SC_SPACE, GRPH));
    r.push(SC_GRPH);
    r.extend_from_slice(&tap(SC_F10, SHIFT));
    run(&mut st, &r);
    assert!(modal::is_open(), "GRPH+f･4 の前にダイアログが閉じた");
    assert!(!startmenu::is_open(), "モーダル中に CTRL+ESC でメニューが開いた");
    assert_eq!(st.front_index(), Some(1), "モーダル中に窓が切り替わった");
    for s in 0..2 {
        assert!(events(&st, s).is_empty(), "アプリのリングに積まれた: slot {s}");
    }
    /* GRPH+f･4 = ダイアログへ ESC (取消)。停止は予約されない。 */
    run(&mut st, &tap(SC_F4, GRPH));
    assert!(!modal::is_open(), "GRPH+f･4 でダイアログが閉じない");
    assert_eq!(crate::session::pending_action(), 0, "取消のはずが停止が予約された");
    for s in 0..2 {
        assert!(events(&st, s).is_empty(), "アプリのリングに積まれた: slot {s}");
    }
}

/// 窓が無い状態で GRPH+f･4 → Shut Down の確認が出る (K2 の最後の段のホスト版)。
#[test]
fn k1_2_grph_f4_without_windows_asks_to_shut_down() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[]);
    run(&mut st, &tap(SC_F4, GRPH));
    assert!(modal::is_open(), "Shut Down の確認が出ない");
    run(&mut st, &tap(SC_ESC, 0));
    assert!(!modal::is_open());
    assert_eq!(crate::session::pending_action(), 0);
}

/* ================================================================ */
/*  ③ X4 (ポンプ) では何も配らず、次の X3 で GRPH↑ の時点で切り替え  */
/* ================================================================ */

#[test]
fn k1_3_pump_defers_wm_keys_and_the_rest_to_the_next_x3() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    let (b, a) = (0usize, 1usize);
    park_pointer(&mut st, 600, 10);
    let mut r = vec![SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH, SC_GRPH];
    r.extend_from_slice(&tap(SC_A, 0));
    r.extend_from_slice(&tap(SC_B, 0));
    mocks::push_rawkeys(&r);
    input::capture(&mut st, input::Ctx::Pump);
    assert_eq!(st.front_index(), Some(a), "X4 で切り替わった");
    assert!(events(&st, a).is_empty() && events(&st, b).is_empty(), "X4 で配った");
    assert!(st.pending_raw_n > 0, "退避していない");

    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(st.front_index(), Some(b), "次の X3 で切り替わらない");
    assert_eq!(text(&st, b), b"ab".to_vec(), "切り替え後の窓に ab が届かない");
    assert!(text(&st, a).is_empty() && keys(&st, a).is_empty(), "元の窓へ ab が漏れた");
}

/// GRPH を押したままの文字は、今までどおり GRPH 付きの Key として元の窓へ
/// (切り替えは GRPH↑ で起きる)。
#[test]
fn k1_3_chars_typed_while_grph_is_held_go_to_the_old_window() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    let (b, a) = (0usize, 1usize);
    park_pointer(&mut st, 600, 10);
    let mut r = vec![SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH];
    r.extend_from_slice(&tap(SC_A, GRPH));
    r.push(SC_GRPH);
    run(&mut st, &r);
    let ka = keys(&st, a);
    assert_eq!(ka.len(), 2, "GRPH+a が元の窓へ届かない: {ka:?}");
    assert!(ka[0].0 && ka[0].1 == SC_A as u8 && (ka[0].3 & 0x08) != 0, "GRPH 付きでない: {ka:?}");
    assert!(keys(&st, b).is_empty(), "GRPH+a が切り替え先へ届いた");
    assert_eq!(st.front_index(), Some(b), "GRPH↑ で切り替わらない");
}

/* ================================================================ */
/*  ④ マウスキー (カナ中のテンキー) のボタン                         */
/* ================================================================ */

fn one_window(shm: &mocks::Shm) -> GuiState {
    let mut st = windows(shm, &[(40, 40, 400, 300)]);
    park_pointer(&mut st, 200, 200);
    st
}

#[test]
fn k1_4_numpad5_is_a_click_with_two_edges_in_one_cycle() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_5, KANA));
    run(&mut st, &r);
    assert_eq!(buttons(&st, 0), vec![(true, 1), (false, 1)], "5 が押下→離しにならない");
    assert!(keys(&st, 0).is_empty(), "テンキーが Key として漏れた");
    assert!(st.kn.mk_on);
    clear_rings(&st);
    run(&mut st, &tap(NP_PLUS, KANA));
    assert_eq!(
        buttons(&st, 0),
        vec![(true, 1), (false, 1), (true, 1), (false, 1)],
        "+ がダブルクリックにならない"
    );
    assert!(keys(&st, 0).is_empty());
}

#[test]
fn k1_4_held_button_is_released_when_kana_turns_off() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_0, KANA));
    run(&mut st, &r);
    assert_eq!(buttons(&st, 0), vec![(true, 1)], "0 で押したままにならない");
    assert_eq!(st.synth_buttons, 1);
    run(&mut st, &[SC_KANA | DOWN]); /* カナ解除 (KANA ビットが落ちた raw) */
    assert_eq!(buttons(&st, 0), vec![(true, 1), (false, 1)], "カナ解除で離しが配られない");
    assert_eq!(st.synth_buttons, 0);
}

#[test]
fn k1_4_kana_release_read_by_the_pump_still_releases_on_the_next_x3() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_0, KANA));
    run(&mut st, &r);
    clear_rings(&st);
    mocks::push_rawkeys(&[SC_KANA | DOWN]);
    input::capture(&mut st, input::Ctx::Pump);
    assert!(buttons(&st, 0).is_empty(), "X4 で離しを配った");
    assert_eq!(st.synth_buttons, 1, "X4 が状態機械を進めた");
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(buttons(&st, 0), vec![(false, 1)], "次の X3 で離しが配られない");
}

#[test]
fn k1_4_numpad5_during_modal_does_not_reach_the_window_behind() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    modal::open_wm_message(&mut st, GUI_MODAL_YES_NO, b"x\0", modal::WM_PURPOSE_NOTIFY);
    let mr = modal::rect();
    /* 背後の窓のクライアントで、ダイアログの外の点。 */
    let (px, py) = (50, 330);
    assert!(st.windows[0].client_rect_screen().contains(px, py) && !mr.contains(px, py));
    park_pointer(&mut st, px, py);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_5, KANA));
    run(&mut st, &r);
    assert!(events(&st, 0).is_empty(), "モーダル中の 5 が背後の窓へ届いた: {}", events(&st, 0).len());
    modal::on_key(&mut st, 0, 0x1B, 0);
    assert!(!modal::is_open());
}

/// カナ ON・5・カナ OFF・5 を 1 回で取り込む → 1 つ目はクリック、2 つ目は数字 "5"
/// (モードは raw の KANA ビットで決める。モードの外で押したテンキーの break はアプリへ)。
#[test]
fn k1_4_kana_bit_is_taken_from_each_raw() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    /* 取り込む時点の現在値は「オフ」(最後のカナ解除の後)。これを読む実装は
     * 1 つ目の 5 も数字にしてしまう。 */
    mocks::set_kbd_mods(0);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_5, KANA));
    r.push(SC_KANA | DOWN);
    r.extend_from_slice(&tap(NP_5, 0));
    run(&mut st, &r);
    assert_eq!(buttons(&st, 0), vec![(true, 1), (false, 1)], "1 つ目がクリックでない");
    assert_eq!(
        keys(&st, 0),
        vec![(true, NP_5 as u8, b'5', 0), (false, NP_5 as u8, b'5', 0)],
        "2 つ目が数字の 5 でない"
    );
    assert_eq!(text(&st, 0), b"5".to_vec());
    assert!(!st.kn.mk_on);
}

/* ================================================================ */
/*  ⑤ 加速: 6 を make 20 回 → 7×3 + 8×12 + 5×24 = 237 ドット         */
/* ================================================================ */

fn pointer_x(st: &GuiState, slot_i: usize) -> Option<i16> {
    events(st, slot_i)
        .iter()
        .filter(|e| e.kind == GUI_EV_POINTER)
        .map(|e| i16::from_le_bytes([e.payload[0], e.payload[1]]))
        .last()
}

#[test]
fn k1_5_numpad6_twenty_makes_move_237_dots() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    let x0 = st.mouse_x;
    let mut r = vec![SC_KANA | DOWN | KANA];
    for _ in 0..20 {
        r.push(NP_6 | DOWN | KANA);
    }
    run(&mut st, &r);
    assert_eq!(st.mouse_x - x0, 237, "20 回の make で 237 ドットでない");
    let (cox, _) = st.windows[0].client_origin();
    assert_eq!(pointer_x(&st, 0), Some((x0 + 237 - cox) as i16), "Pointer が最後の位置でない");
    /* break で数え直し: 次の 1 回は 3 ドット。 */
    run(&mut st, &[NP_6 | KANA, NP_6 | DOWN | KANA]);
    assert_eq!(st.mouse_x - x0, 240, "break で数え直していない");
    /* 止まっている実マウスで位置が巻き戻らない。 */
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(st.mouse_x - x0, 240, "止まった実マウスの値で巻き戻った");
}

/* ================================================================ */
/*  ⑧ CTRL = 半分の速度 (端数 1/2 ドットは持ち越す)                  */
/* ================================================================ */

#[test]
fn k1_8_ctrl_numpad6_twenty_makes_move_118_dots_and_keep_the_half() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    let x0 = st.mouse_x;
    let mut r = vec![SC_KANA | DOWN | KANA];
    for _ in 0..20 {
        r.push(NP_6 | DOWN | KANA | CTRL);
    }
    run(&mut st, &r);
    assert_eq!(st.mouse_x - x0, 118, "CTRL で半分 (237/2 = 118) にならない");
    /* 端数 1/2 は持ち越す: 数え直した次の 1 回 (3/2 ドット) と合わせて 2 ドット。 */
    run(&mut st, &[NP_6 | KANA | CTRL, NP_6 | DOWN | KANA | CTRL]);
    assert_eq!(st.mouse_x - x0, 120, "端数 1/2 を捨てた");
    /* 端数なしから 1 回目 (3/2) は 1 ドットだけ動き、1/2 が残る。 */
    run(&mut st, &[NP_6 | KANA | CTRL, NP_6 | DOWN | KANA | CTRL]);
    assert_eq!(st.mouse_x - x0, 121, "3/2 ドットが 1 ドットにならない");
    /* カナを切ると端数は捨てる (残っていれば 1/2 + 3/2 で 2 ドット動く)。 */
    run(&mut st, &[NP_6 | KANA | CTRL, SC_KANA | DOWN, SC_KANA | DOWN | KANA]);
    run(&mut st, &[NP_6 | DOWN | KANA | CTRL]);
    assert_eq!(st.mouse_x - x0, 122, "モードが切れても端数が残った");
}

/* ================================================================ */
/*  ⑦ タイトルバーで 0 → 6×10 → . (同じ取り込み周期) → 窓が移る      */
/* ================================================================ */

fn titlebar_point(st: &GuiState) -> (i32, i32) {
    let tb = st.windows[0].titlebar_rect();
    (tb.x + 20, tb.y + tb.h / 2)
}

#[test]
fn k1_7_drag_by_numpad_moves_the_window_in_one_cycle() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(40, 40, 400, 300)]);
    let (tx, ty) = titlebar_point(&st);
    park_pointer(&mut st, tx, ty);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_0, KANA));
    for _ in 0..10 {
        r.push(NP_6 | DOWN | KANA);
    }
    r.push(NP_6 | KANA);
    r.extend_from_slice(&tap(NP_DOT, KANA));
    run(&mut st, &r);
    /* 7×3 + 3×12 = 57 ドット。 */
    assert_eq!((st.windows[0].x, st.windows[0].y), (97, 40), "離しで窓が移らない");
    assert_eq!(st.drag_index, -1, "ドラッグが終わっていない");
    assert!(buttons(&st, 0).is_empty(), "タイトルバーの押下がアプリへ漏れた");
}

/// 枠 (XOR 枠) がマウスキーの移動に追従する (離す前に見る)。
#[test]
fn k1_7_drag_frame_follows_numpad_moves() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(40, 40, 400, 300)]);
    let (tx, ty) = titlebar_point(&st);
    park_pointer(&mut st, tx, ty);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_0, KANA));
    for _ in 0..10 {
        r.push(NP_6 | DOWN | KANA);
    }
    run(&mut st, &r);
    assert_eq!(st.drag_index, 0, "ドラッグが始まらない");
    assert_eq!(st.drag_frame.x, 97, "枠が追従しない");
    assert_eq!(st.windows[0].x, 40, "離す前に窓が動いた");
    run(&mut st, &tap(NP_DOT, KANA));
    assert_eq!(st.windows[0].x, 97);
}

/* ================================================================ */
/*  ⑥ FEP の未確定文字はフォーカスが移る前に元の窓へ確定する        */
/* ================================================================ */

/// "にほん" の UTF-8 を 1 バイトずつ返す台本 (確定) + 終端。
fn nihon_commit() -> Vec<i32> {
    let mut v: Vec<i32> = "にほん".bytes().map(|b| b as i32).collect();
    v.push(0);
    v
}

fn fep_two_windows(shm: &mocks::Shm) -> GuiState {
    /* 0 = B、1 = A (最前面)。 */
    let mut st = windows(shm, &[(300, 20, 200, 150), (20, 20, 200, 150)]);
    park_pointer(&mut st, 600, 300);
    /* n i h の打鍵は FEP が消費 (-1)、確定は RETURN 1 回で "にほん"。 */
    let mut script = vec![-1, -1, -1];
    script.extend(nihon_commit());
    mocks::fep_script(&script);
    fep::install();
    assert!(fep::is_on());
    let mut r = Vec::new();
    r.extend_from_slice(&tap(SC_N, 0));
    r.extend_from_slice(&tap(SC_I, 0));
    r.extend_from_slice(&tap(SC_H, 0));
    run(&mut st, &r);
    /* FEP は make だけを取る (break は今までどおりアプリへ)。 */
    assert!(
        text(&st, 1).is_empty() && keys(&st, 1).iter().all(|k| !k.0),
        "未確定の打鍵がアプリへ届いた"
    );
    st
}

#[test]
fn k1_6_grph_tab_commits_the_preedit_to_the_old_window() {
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = fep_two_windows(&shm);
    run(&mut st, &[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH, SC_GRPH]);
    assert_eq!(st.front_index(), Some(0), "切り替わらない");
    assert_eq!(text(&st, 1), "にほん".as_bytes().to_vec(), "元の窓 A に確定文字が届かない");
    assert!(text(&st, 0).is_empty(), "切り替え先 B に確定文字が届いた");
    fep_off();
}

#[test]
fn k1_6_mouse_click_on_another_window_commits_to_the_old_window() {
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = fep_two_windows(&shm);
    let cr = st.windows[0].client_rect_screen();
    *mocks::MOUSE.lock().unwrap() = ((cr.x + 10) as i16, (cr.y + 10) as i16, 1);
    input::capture(&mut st, input::Ctx::Wait);
    *mocks::MOUSE.lock().unwrap() = ((cr.x + 10) as i16, (cr.y + 10) as i16, 0);
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(st.front_index(), Some(0), "クリックで切り替わらない");
    assert_eq!(text(&st, 1), "にほん".as_bytes().to_vec(), "元の窓 A に確定文字が届かない");
    assert!(text(&st, 0).is_empty(), "クリックした B に確定文字が届いた");
    fep_off();
}

/// アプリの set_focus (X1) は確定を予約だけし、次の X3 の頭で元の窓へ流す。
#[test]
fn k1_6_app_set_focus_commits_to_the_old_window_at_the_next_x3() {
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = fep_two_windows(&shm);
    let b = id(&st, 0);
    assert_eq!(wm::set_focus(&mut st, 2, b), 0);
    assert!(text(&st, 1).is_empty(), "X1 で変換を走らせた");
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(text(&st, 1), "にほん".as_bytes().to_vec(), "元の窓 A に確定文字が届かない");
    assert!(text(&st, 0).is_empty());
    fep_off();
}

/* ================================================================ */
/*  窓メニュー (GRPH+SPACE) と GRPH+ESC / CTRL+ESC / SHIFT+f･10      */
/* ================================================================ */

#[test]
fn winmenu_move_by_arrows_and_return() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(40, 40, 300, 200)]);
    park_pointer(&mut st, 600, 10);
    run(&mut st, &tap(SC_SPACE, GRPH));
    assert_eq!(startmenu::winmenu_target(), Some(id(&st, 0)), "窓メニューが開かない");
    /* 初期カーソルは最初の使える項目 = Move (Restore は灰色、代行レビュー P3-4)。 */
    assert_eq!(startmenu::cursor(), kbdnav::WM_MOVE, "初期カーソルが Move でない");
    run(&mut st, &tap(0x1C, 0));
    assert!(!startmenu::is_open());
    assert_eq!(st.drag_index, 0, "移動が始まらない");
    let mut r = Vec::new();
    r.extend_from_slice(&tap(0x3C, 0)); /* → 8 */
    r.extend_from_slice(&tap(0x3D, SHIFT)); /* SHIFT+↓ 1 */
    r.extend_from_slice(&tap(0x1C, 0));
    run(&mut st, &r);
    assert_eq!((st.windows[0].x, st.windows[0].y), (48, 41), "矢印で移らない");
    assert_eq!(st.drag_index, -1);
    for s in 0..1 {
        assert!(keys(&st, s).is_empty(), "移動中のキーがアプリへ漏れた");
    }
}

#[test]
fn winmenu_minimize_then_grph_tab_restores() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    /* 窓メニュー → Minimize (初期カーソル Move から 2 行下)。 */
    let mut r = Vec::new();
    r.extend_from_slice(&tap(SC_SPACE, GRPH));
    for _ in 0..2 {
        r.extend_from_slice(&tap(0x3D, 0));
    }
    r.extend_from_slice(&tap(0x1C, 0));
    run(&mut st, &r);
    assert!(st.windows[1].minimized, "最小化されない");
    assert_eq!(st.front_index(), Some(0));
    /* GRPH+TAB で戻す (最小化は MRU の末尾 = 前面の次)。 */
    run(&mut st, &[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH, SC_GRPH]);
    assert!(!st.windows[1].minimized && st.front_index() == Some(1), "GRPH+TAB で戻らない");
}

#[test]
fn winmenu_maximize_and_restore() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(40, 40, 300, 200)]);
    park_pointer(&mut st, 600, 10);
    let w0 = id(&st, 0);
    kbdnav::winmenu_run(&mut st, w0, kbdnav::WM_MAXIMIZE);
    let wa = wm::work_area(&st);
    assert!(st.windows[0].outer() == wa, "最大化で作業領域いっぱいにならない");
    assert!(!kbdnav::winmenu_enabled(&st, id(&st, 0), kbdnav::WM_MOVE));
    kbdnav::winmenu_run(&mut st, w0, kbdnav::WM_RESTORE);
    assert_eq!(
        (st.windows[0].x, st.windows[0].y, st.windows[0].w, st.windows[0].h),
        (40, 40, 300, 200)
    );
}

#[test]
fn grph_esc_sends_front_to_back_and_ctrl_esc_toggles_start() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    run(&mut st, &tap(SC_ESC, GRPH));
    assert_eq!(st.front_index(), Some(0), "GRPH+ESC で次の窓へ行かない");
    assert_eq!(st.zorder[0], 1, "最背面へ回らない");
    run(&mut st, &tap(SC_ESC, CTRL));
    assert!(startmenu::is_open() && startmenu::start_pressed(), "CTRL+ESC で開かない");
    run(&mut st, &tap(SC_ESC, CTRL));
    assert!(!startmenu::is_open(), "CTRL+ESC で閉じない");
    for s in 0..2 {
        assert!(keys(&st, s).is_empty(), "WM のキーがアプリへ漏れた");
    }
}

#[test]
fn shift_f10_goes_to_the_window_or_opens_the_desktop_menu() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    run(&mut st, &tap(SC_F10, SHIFT));
    assert!(!startmenu::is_open(), "窓があるのに WM がメニューを出した");
    assert_eq!(keys(&st, 0).len(), 2, "SHIFT+f･10 が窓へ届かない");
    let mut st2 = windows(&shm, &[]);
    run(&mut st2, &tap(SC_F10, SHIFT));
    assert!(startmenu::is_open(), "窓が無いのにデスクトップのメニューが出ない");
    startmenu::close(&mut st2);
}

/* ================================================================ */
/*  ④' ボタンの直接割り当て (ユーザー決定 2026-09-26)                */
/*  5 = 左、- = 右、+ = 左ダブル、0 = 左を押したまま、. = 離す       */
/* ================================================================ */

const NP_MINUS: i32 = 0x40;
const NP_SLASH: i32 = 0x41;
const NP_STAR: i32 = 0x45;

#[test]
fn k1_4_minus_is_a_right_click_and_slash_star_do_nothing() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_MINUS, KANA));
    run(&mut st, &r);
    assert_eq!(buttons(&st, 0), vec![(true, 2), (false, 2)], "- 1 回で右クリックにならない");
    clear_rings(&st);
    /* / と * はマウスモード中は消費して何もしない (ボタンの選択は持たない)。 */
    let mut r = Vec::new();
    r.extend_from_slice(&tap(NP_SLASH, KANA));
    r.extend_from_slice(&tap(NP_STAR, KANA));
    r.extend_from_slice(&tap(NP_5, KANA));
    run(&mut st, &r);
    assert!(keys(&st, 0).is_empty() && text(&st, 0).is_empty(), "/ * が文字として漏れた");
    assert_eq!(buttons(&st, 0), vec![(true, 1), (false, 1)], "/ * の後の 5 が左クリックでない");
}

/// + は同じ周期の down・up・down・up — WM のファイル選択のダブルクリック判定
/// (modal.rs、同じ行・DBLCLICK_TICKS 以内) に入って項目が開く。filer
/// (`userland/rust/filer` の 30 tick 以内・同じ行) も同じ判定の形。
#[test]
fn k1_4_plus_is_a_double_click_that_the_file_dialog_accepts() {
    static LS_DONE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    #[repr(C)]
    struct DirEntryExt {
        name: [u8; 256],
        size: u32,
        ftype: u8,
    }
    unsafe extern "C" fn ls(_path: *const u8, cb: *mut u8, ctx: *mut u8) -> i32 {
        let mut e = DirEntryExt {
            name: [0u8; 256],
            size: 1,
            ftype: 1,
        };
        e.name[..5].copy_from_slice(b"a.bin");
        let f: extern "C" fn(*const DirEntryExt, *mut u8) = core::mem::transmute(cb);
        f(&e as *const DirEntryExt, ctx);
        LS_DONE.store(true, std::sync::atomic::Ordering::SeqCst);
        0
    }
    mocks::init();
    fep_off();
    unsafe {
        (*os32api::api_ptr()).sys_ls = ls;
    }
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    modal::open_wm_file(&mut st, b"/\0");
    modal::x3_cycle(&mut st);
    assert!(LS_DONE.load(std::sync::atomic::Ordering::SeqCst));
    let rr = modal::file_row_rect(0);
    park_pointer(&mut st, rr.x + 8, rr.y + rr.h / 2);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_PLUS, KANA));
    run(&mut st, &r);
    assert!(!modal::is_open(), "+ でダブルクリックにならない (ダイアログが開いたまま)");
    assert!(st.launch_pending, "ダブルクリックで項目が選ばれない");
    assert_eq!(&st.launch_path[..st.launch_path_len], b"/a.bin");
}


/* ================================================================ */
/*  代行レビュー (Fable 5.1、1f69b44) の指摘の探り                    */
/* ================================================================ */

/// [P2-1] X1 の予約確定はモーダル中に捨てない。モーダルが閉じた後 (同じ周期の
/// 次の打鍵より先) に元の窓 A へ流す。捨てると閉じた後の RETURN で B に確定した。
#[test]
fn review_p2_1_deferred_commit_survives_a_modal() {
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = fep_two_windows(&shm);
    let b = id(&st, 0);
    modal::open_wm_message(&mut st, GUI_MODAL_YES_NO, b"x\0", modal::WM_PURPOSE_NOTIFY);
    assert_eq!(wm::set_focus(&mut st, 2, b), 0);
    input::capture(&mut st, input::Ctx::Wait); /* モーダル中の X3: 流さない */
    assert!(text(&st, 1).is_empty() && text(&st, 0).is_empty());
    /* ESC でモーダルを閉じ、同じ周期で RETURN。 */
    let mut r = Vec::new();
    r.extend_from_slice(&tap(SC_ESC, 0));
    r.extend_from_slice(&tap(0x1C, 0));
    run(&mut st, &r);
    assert!(!modal::is_open());
    assert_eq!(text(&st, 1), "にほん".as_bytes().to_vec(), "予約した確定が A に届かない");
    assert!(text(&st, 0).is_empty(), "未確定が B に確定した: {:?}", text(&st, 0));
    fep_off();
}

/// [P3-1] RETURN 1 打で確定し切らない (最長一致で残りが未確定へ戻る) 場合も、
/// 素通りになるまで流して全部を元の窓へ。
#[test]
fn review_p3_1_commit_repeats_return_until_nothing_is_left() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(300, 20, 200, 150), (20, 20, 200, 150)]);
    park_pointer(&mut st, 600, 300);
    let mut script = vec![-1, -1];
    script.extend("にほん".bytes().map(|b| b as i32));
    script.push(0);
    script.extend("ご".bytes().map(|b| b as i32));
    script.push(0);
    mocks::fep_script(&script);
    fep::install();
    let mut r = Vec::new();
    r.extend_from_slice(&tap(SC_N, 0));
    r.extend_from_slice(&tap(SC_I, 0));
    r.extend_from_slice(&[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH, SC_GRPH]);
    run(&mut st, &r);
    assert_eq!(text(&st, 1), "にほんご".as_bytes().to_vec(), "残りのかなが確定しない");
    assert!(text(&st, 0).is_empty());
    fep_off();
}

/// [P2-2] WM が最小化した窓へのアプリの set_focus は何もしない (Focus も出さない)。
#[test]
fn review_p2_2_app_set_focus_on_a_minimized_window_is_a_no_op() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    wm::minimize(&mut st, 0);
    park_pointer(&mut st, 600, 10);
    let b = id(&st, 0);
    assert_eq!(wm::set_focus(&mut st, 2, b), 0);
    assert!(st.windows[0].minimized && st.front_index() == Some(1));
    assert_eq!(st.zorder[st.z_count - 1], 1, "不可視の窓が Z の最前面へ出た");
    assert!(focus(&st, 0).is_empty() && focus(&st, 1).is_empty(), "Focus が食い違った");
    run(&mut st, &tap(SC_A, 0));
    assert_eq!(keys(&st, 1).len(), 2, "打鍵が前面の窓へ届かない");
}

/// アプリが最大化中の窓を動かしたら最大化の旗を落とす (Restore が古い矩形へ戻さない)。
#[test]
fn review_app_move_clears_maximized() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(40, 40, 300, 200)]);
    wm::maximize(&mut st, 0);
    let w0 = id(&st, 0);
    assert_eq!(wm::move_window(&mut st, 2, w0, 10, 10), 0);
    assert!(!st.windows[0].maximized, "move で最大化が解けない");
    wm::maximize(&mut st, 0);
    assert_eq!(wm::resize_window(&mut st, 2, w0, 200, 100), 0);
    assert!(!st.windows[0].maximized, "resize で最大化が解けない");
}

/// [P3-2] GRPH+TAB の途中でモーダルが開いたら GRPH↑ で切り替えない。
#[test]
fn review_p3_2_switch_is_cancelled_when_a_modal_opens() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    run(&mut st, &[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH]);
    modal::open_wm_message(&mut st, GUI_MODAL_YES_NO, b"x\0", modal::WM_PURPOSE_NOTIFY);
    run(&mut st, &[SC_GRPH]);
    assert_eq!(st.front_index(), Some(1), "モーダル中に切り替わった");
    assert_eq!(kbdnav::switch_selection(&st), None);
    modal::on_key(&mut st, 0, 0x1B, 0);
}

/// [P3-3] break を取りこぼして印が残っても、X4 で配った make の break は X4 が配る。
#[test]
fn review_p3_3_pump_delivers_the_break_of_a_make_it_delivered() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = one_window(&shm);
    /* X3: カナ中の 5 の make だけ (break を取りこぼした) → 印が残る。カナ解除。 */
    run(&mut st, &[SC_KANA | DOWN | KANA, NP_5 | DOWN | KANA, SC_KANA | DOWN]);
    clear_rings(&st);
    /* X4: 数字の 5 の make と break。 */
    mocks::push_rawkeys(&tap(NP_5, 0));
    input::capture(&mut st, input::Ctx::Pump);
    assert_eq!(
        keys(&st, 0),
        vec![(true, NP_5 as u8, b'5', 0), (false, NP_5 as u8, b'5', 0)],
        "X4 で配った make の break が握り潰された"
    );
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(keys(&st, 0).len(), 2);
}

/// [P3-5] キーボードの移動中に窓が消えたら枠を消す (画面に残さない)。
#[test]
fn review_p3_5_frame_is_erased_when_the_window_goes_away() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(40, 40, 300, 200)]);
    park_pointer(&mut st, 600, 10);
    let w0 = id(&st, 0);
    kbdnav::winmenu_run(&mut st, w0, kbdnav::WM_MOVE);
    run(&mut st, &tap(0x3C, 0)); /* → 8 */
    let f = st.drag_frame;
    assert_eq!(f.x, 48);
    st.screen_dirty.clear();
    assert_eq!(wm::destroy_window(&mut st, 2, w0), 0);
    assert_eq!(st.drag_index, -1);
    assert!(st.drag_frame.is_empty(), "枠が残った");
    assert!(
        st.screen_dirty.as_slice().iter().any(|r| r.intersect(&f) == f),
        "消えた窓の枠が損傷に積まれていない"
    );
    run(&mut st, &tap(0x1C, 0)); /* 移動モードは終わっている */
}

/// [P3-A] キーボードの移動中に窓が消えた後、同じ添字に作られた窓のマウスドラッグは
/// 枠が追従する (移動の状態 `kn.kmode` が残って「キーボードの移動中」に見えていた)。
#[test]
fn review_p3_a_mouse_drag_works_on_a_new_window_at_the_same_index() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(40, 40, 300, 200)]);
    park_pointer(&mut st, 600, 10);
    let w0 = id(&st, 0);
    kbdnav::winmenu_run(&mut st, w0, kbdnav::WM_MOVE);
    run(&mut st, &tap(0x3C, 0)); /* → 8 */
    assert!(kbdnav::kmove_busy(&st));
    assert_eq!(wm::destroy_window(&mut st, 2, w0), 0);
    /* 同じ添字 0 に新しい窓 (世代は destroy が進めた値のまま) */
    let gen = st.windows[0].gen;
    let mut w = Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = gen;
    w.flags = os32api::gui::proto::GUI_WF_DEFAULT;
    w.x = 40;
    w.y = 40;
    w.w = 300;
    w.h = 200;
    st.windows[0] = w;
    st.zorder[st.z_count] = 0;
    st.z_count += 1;
    let (tx, ty) = titlebar_point(&st);
    park_pointer(&mut st, tx, ty);
    *mocks::MOUSE.lock().unwrap() = (tx as i16, ty as i16, 1);
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(st.drag_index, 0, "マウスのドラッグが始まらない");
    assert!(!kbdnav::kmove_busy(&st), "マウスのドラッグがキーボードの移動中に見える");
    *mocks::MOUSE.lock().unwrap() = ((tx + 30) as i16, (ty + 10) as i16, 1);
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!((st.drag_frame.x, st.drag_frame.y), (70, 50), "枠がマウスに追従しない");
    *mocks::MOUSE.lock().unwrap() = ((tx + 30) as i16, (ty + 10) as i16, 0);
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!((st.windows[0].x, st.windows[0].y), (70, 50), "離しで窓が移らない");
    assert_eq!(st.drag_index, -1);
    /* 矢印キーは WM の移動に食われずアプリへ届く */
    clear_rings(&st);
    run(&mut st, &tap(0x3C, 0));
    assert_eq!((st.windows[0].x, st.windows[0].y), (70, 50), "矢印で窓が動いた");
    assert_eq!(keys(&st, 0).len(), 2, "矢印キーがアプリへ届かない");
}

/// [P3-B] GRPH+TAB の途中でモーダルが開いたら、以後の TAB は選択を進めず取り消す
/// (タスクバーの押し込みが動き続けていた)。GRPH↑ でも切り替えない。
#[test]
fn review_p3_b_tab_during_a_modal_cancels_the_switch() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (60, 60, 200, 120), (100, 100, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    run(&mut st, &[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH]);
    assert_eq!(kbdnav::switch_selection(&st), Some(id(&st, 1)));
    modal::open_wm_message(&mut st, GUI_MODAL_YES_NO, b"x\0", modal::WM_PURPOSE_NOTIFY);
    run(&mut st, &tap(SC_TAB, GRPH));
    assert_eq!(kbdnav::switch_selection(&st), None, "モーダル中の TAB で選択が進んだ");
    assert!(modal::is_open(), "TAB でダイアログが閉じた");
    run(&mut st, &tap(SC_TAB, GRPH));
    assert_eq!(kbdnav::switch_selection(&st), None, "取り消した後の TAB で切り替えが始まった");
    run(&mut st, &[SC_GRPH]);
    assert_eq!(st.front_index(), Some(2), "モーダル中に切り替わった");
    for s in 0..3 {
        assert!(keys(&st, s).is_empty(), "TAB がアプリへ漏れた: slot {s}");
    }
    modal::on_key(&mut st, 0, 0x1B, 0);
    assert!(!modal::is_open());
}

/// GRPH+f･4 は前面の窓へ Close を送る。
#[test]
fn grph_f4_sends_close_to_the_front_window() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    run(&mut st, &tap(SC_F4, GRPH));
    let c: Vec<u32> = events(&st, 1)
        .iter()
        .filter(|e| e.kind == os32api::gui::proto::GUI_EV_CLOSE)
        .map(|e| e.window)
        .collect();
    assert_eq!(c, vec![id(&st, 1)], "前面の窓へ Close が届かない");
    assert!(events(&st, 0).iter().all(|e| e.kind != os32api::gui::proto::GUI_EV_CLOSE));
    assert!(keys(&st, 1).is_empty());
}

/// メニュー表示中: CTRL+ESC は閉じる、GRPH+TAB は閉じてから切り替える。
#[test]
fn menu_open_ctrl_esc_closes_and_grph_tab_closes_then_switches() {
    mocks::init();
    fep_off();
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 20, 200, 120)]);
    park_pointer(&mut st, 600, 10);
    startmenu::toggle_start(&mut st);
    assert!(startmenu::is_open());
    run(&mut st, &tap(SC_ESC, CTRL));
    assert!(!startmenu::is_open(), "CTRL+ESC で閉じない");
    startmenu::toggle_start(&mut st);
    run(&mut st, &[SC_GRPH | DOWN | GRPH, SC_TAB | DOWN | GRPH, SC_TAB | GRPH, SC_GRPH]);
    assert!(!startmenu::is_open(), "GRPH+TAB でメニューが閉じない");
    assert_eq!(st.front_index(), Some(0), "閉じた後に切り替わらない");
    for s in 0..2 {
        assert!(keys(&st, s).is_empty());
    }
}

/* ================================================================ */
/*  ドラッグ枠の跡 (gui_gate v11、2026-09-29: PEGC / Cirrus で再現)   */
/* ================================================================ */

/// アプリが描いたことにするクライアント面の見張り色 (システム色と重ならない)。
const CLIENT_SENTINEL: u8 = 201;

/// 可視な窓のクライアント面を見張り色で塗り、損傷を空にする
/// (「アプリが全部描き終えた」状態)。
fn paint_clients_as_app(st: &mut GuiState) {
    let mut i = 0;
    while i < st.windows.len() {
        if st.windows[i].used && st.windows[i].visible {
            let r = st.windows[i].client_rect_screen();
            unsafe { os32api::gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, CLIENT_SENTINEL) };
            st.windows[i].dirty.clear();
            st.windows[i].issued.clear();
        }
        i += 1;
    }
}

/// 窓 `idx` の可視なクライアント面で、アプリが描いた画素 (見張り色) を WM が
/// 書き替えたのに Paint (dirty / issued) が出ていない点を**全部**集める。WM は
/// クライアント面を持たないので、書き替えた画素はアプリに描き直させる以外に消せない。
/// `allowed(x, y)` が真の画素 (今の枠・カーソル) は走査の中で除く。
fn stale_client_pixels(st: &GuiState, idx: usize, allowed: &dyn Fn(i32, i32) -> bool) -> Vec<(i32, i32, u8)> {
    let px = mocks::pixels();
    let w = &st.windows[idx];
    let (ox, oy) = w.client_origin();
    let covered = |lx: i32, ly: i32| {
        (0..w.dirty.len).any(|k| w.dirty.rects[k].contains(lx, ly))
            || (0..w.issued.len).any(|k| w.issued.rects[k].contains(lx, ly))
    };
    let mut out = Vec::new();
    for v in 0..w.vis.len {
        let r = w.vis.rects[v];
        for ly in r.y..r.bottom() {
            for lx in r.x..r.right() {
                let (sx, sy) = (ox + lx, oy + ly);
                let got = px[sy as usize * mocks::W + sx as usize];
                if got != CLIENT_SENTINEL && !covered(lx, ly) && !allowed(sx, sy) {
                    out.push((sx, sy, got));
                }
            }
        }
    }
    out
}

fn nothing_allowed(_x: i32, _y: i32) -> bool {
    false
}

/// 枠 `f` の輪郭 (1px) の上か。
fn on_outline(f: wm::Rect, x: i32, y: i32) -> bool {
    f.contains(x, y) && (x == f.x || x == f.right() - 1 || y == f.y || y == f.bottom() - 1)
}

/// gui_gate v11 の台本: Widgets を掴んで Help の上を通し、Help と重ねて離す。
/// 途中位置の枠 (x=459 の縦線・y=111 の横線) が Help のクライアント面に残っていた
/// — 枠の消去 (`redraw_frame`) は下地とクロームしか描き直さず、枠が横切った窓へ
/// Paint を返していなかった。
#[test]
fn drag_frame_trail_on_another_client_gets_a_paint() {
    mocks::init();
    fep_off();
    mocks::clear(0);
    let shm = mocks::Shm::new();
    /* 添字 0 = Help (背面)、1 = Widgets (前面)。gui_gate v11 の配置。 */
    let mut st = windows(&shm, &[(372, 80, 242, 180), (40, 40, 320, 240)]);
    st.screen_w = 640;
    st.screen_h = 480;
    crate::visible::recompute_and_expose(&mut st);
    wm::composite_full(&mut st);
    park_pointer(&mut st, 20, 400);
    paint_clients_as_app(&mut st);
    let tb = st.windows[1].titlebar_rect();
    let (tx, ty) = (tb.x + 20, tb.y + tb.h / 2);
    park_pointer(&mut st, tx, ty);
    /* 掴む → 途中 (枠が Help の中を横切る) → Help と重なる位置 → 離す */
    for &(dx, dy, b) in &[(0, 0, 1u8), (100, 71, 1), (200, 160, 1), (200, 160, 0)] {
        *mocks::MOUSE.lock().unwrap() = ((tx + dx) as i16, (ty + dy) as i16, b);
        input::capture(&mut st, input::Ctx::Wait);
        wm::flush_screen_dirty(&mut st);
    }
    assert_eq!(st.drag_index, -1, "離しでドラッグが終わらない");
    assert_eq!((st.windows[1].x, st.windows[1].y), (240, 200), "窓が枠の位置へ移らない");
    let stale = stale_client_pixels(&st, 0, &nothing_allowed);
    assert!(stale.is_empty(), "Help のクライアント面に枠の跡が残り、Paint も出ていない: {} 点 (先頭 {:?})", stale.len(), stale.first());
}

/// ドラッグ中 (離す前) も、消した枠が横切ったクライアント面には Paint を返す
/// (跡がドラッグの間ずっと画面に残らない)。自分自身の窓の旧位置も同じ。
#[test]
fn drag_frame_trail_is_repainted_while_dragging() {
    mocks::init();
    fep_off();
    mocks::clear(0);
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(372, 80, 242, 180), (40, 40, 320, 240)]);
    st.screen_w = 640;
    st.screen_h = 480;
    crate::visible::recompute_and_expose(&mut st);
    wm::composite_full(&mut st);
    park_pointer(&mut st, 20, 400);
    paint_clients_as_app(&mut st);
    let tb = st.windows[1].titlebar_rect();
    let (tx, ty) = (tb.x + 20, tb.y + tb.h / 2);
    park_pointer(&mut st, tx, ty);
    for &(dx, dy) in &[(0, 0), (30, 20), (100, 71), (200, 160)] {
        *mocks::MOUSE.lock().unwrap() = ((tx + dx) as i16, (ty + dy) as i16, 1);
        input::capture(&mut st, input::Ctx::Wait);
        wm::flush_screen_dirty(&mut st);
    }
    assert_eq!(st.drag_index, 1, "ドラッグが続いていない");
    /* いまの枠と、カーソルの下は描いてあって当然なので除く */
    let f = st.drag_frame;
    let cr = crate::cursor::rect(&st);
    let allowed = move |x: i32, y: i32| on_outline(f, x, y) || cr.contains(x, y);
    for idx in 0..2 {
        let stale = stale_client_pixels(&st, idx, &allowed);
        assert!(stale.is_empty(), "窓 {idx} に消した枠の跡が残り、Paint も無い: {} 点 (先頭 {:?})", stale.len(), stale.first());
    }
}

/// 旧枠の帯は**帯のまま** Paint にする (Codex レビュー P2: 縦横の帯が外接矩形へ
/// 畳まれて窓の内側まで丸ごと Paint になっていた)。Help の例で旧枠 1 回の
/// 消去が出す Paint の面積を、帯 (32px 丸め) の面積の和で抑える。
#[test]
fn drag_frame_paint_stays_band_shaped() {
    mocks::init();
    fep_off();
    mocks::clear(0);
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(372, 80, 242, 180), (40, 40, 320, 240)]);
    st.screen_w = 640;
    st.screen_h = 480;
    crate::visible::recompute_and_expose(&mut st);
    wm::composite_full(&mut st);
    park_pointer(&mut st, 20, 400);
    paint_clients_as_app(&mut st);
    let tb = st.windows[1].titlebar_rect();
    let (tx, ty) = (tb.x + 20, tb.y + tb.h / 2);
    park_pointer(&mut st, tx, ty);
    for &(dx, dy) in &[(0, 0), (100, 71)] {
        *mocks::MOUSE.lock().unwrap() = ((tx + dx) as i16, (ty + dy) as i16, 1);
        input::capture(&mut st, input::Ctx::Wait);
    }
    st.windows[0].dirty.clear();
    /* 枠 (140,111,320,240) を別の位置へ動かす = 旧枠の縁を Help へ返す */
    *mocks::MOUSE.lock().unwrap() = ((tx + 200) as i16, (ty + 160) as i16, 1);
    input::capture(&mut st, input::Ctx::Wait);
    let d = st.windows[0].dirty;
    assert!(d.len > 0, "旧枠が横切った Help に Paint が出ない");
    let area: i32 = (0..d.len).map(|k| d.rects[k].w * d.rects[k].h).sum();
    /* Help のクライアント内の旧枠: 縦 1 本 (x=459) と横 1 本 (y=111)。32px 丸めで
     * 縦 = 32 × 高さ、横 = 幅 × 32 が上限 (角の重なりを含めても和を超えない)。 */
    let (cw, ch) = st.windows[0].client_size();
    let bound = 32 * ch + cw * 32;
    assert!(area <= bound, "Paint が帯より広い: 面積 {area} > {bound} ({:?})", (0..d.len).map(|k| (d.rects[k].x, d.rects[k].y, d.rects[k].w, d.rects[k].h)).collect::<Vec<_>>());
}

/// アプリが Paint を受けて描き直し、COMMIT しても**今の枠は欠けない**
/// (Codex レビュー P2: 帯の 32px 丸めが今の枠に掛かり、op_commit が枠を戻さずに
/// present していた = 次の WM 周まで枠の上辺が消えてちらつく)。
#[test]
fn app_commit_during_drag_keeps_the_live_frame() {
    use crate::handler;
    use os32api::gui::proto::{GUI_COLOR_HIGHLIGHT, GUI_OP_COMMIT, GUI_OP_POLL};
    mocks::init();
    fep_off();
    mocks::clear(0);
    let shm = mocks::Shm::new();
    let g = wm::g();
    *g = windows(&shm, &[(372, 80, 242, 180), (40, 40, 320, 240)]);
    g.inited = true;
    g.screen_w = 640;
    g.screen_h = 480;
    crate::visible::recompute_and_expose(g);
    wm::composite_full(g);
    park_pointer(g, 20, 400);
    paint_clients_as_app(g);
    let tb = g.windows[1].titlebar_rect();
    let (tx, ty) = (tb.x + 20, tb.y + tb.h / 2);
    park_pointer(g, tx, ty);
    for &(dx, dy) in &[(0, 0), (100, 71), (200, 160)] {
        *mocks::MOUSE.lock().unwrap() = ((tx + dx) as i16, (ty + dy) as i16, 1);
        input::capture(g, input::Ctx::Wait);
        wm::flush_screen_dirty(g);
    }
    assert_eq!(g.drag_index, 1, "ドラッグが続いていない");
    /* Help (owner 2) のアプリ: Paint を受けて、その矩形を丸ごと塗って COMMIT */
    let n = handler::gshell_gui_handler(GUI_OP_POLL, 0, 2);
    assert!(n > 0, "旧枠の帯が Help の Paint にならない");
    let (ox, oy) = g.windows[0].client_origin();
    let mut hit_live = false;
    let f = g.drag_frame;
    for k in 0..g.windows[0].issued.len {
        let r = g.windows[0].issued.rects[k].translate(ox, oy);
        if r.intersects(&wm::Rect::new(f.x, f.y, f.w, 1)) || r.intersects(&wm::Rect::new(f.x, f.y, 1, f.h)) {
            hit_live = true;
        }
        unsafe { os32api::gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, CLIENT_SENTINEL) };
    }
    assert!(hit_live, "前提: Paint の矩形が今の枠に掛かる配置になっていない");
    assert_eq!(handler::gshell_gui_handler(GUI_OP_COMMIT, 0, 2), 0);
    /* 今の枠の輪郭 (カーソルの下を除く) が全部枠の色のまま */
    let px = mocks::pixels();
    let cr = crate::cursor::rect(g);
    let mut broken = Vec::new();
    for x in f.x..f.right() {
        for &y in &[f.y, f.bottom() - 1] {
            if !cr.contains(x, y) && px[y as usize * mocks::W + x as usize] != GUI_COLOR_HIGHLIGHT {
                broken.push((x, y));
            }
        }
    }
    for y in f.y..f.bottom() {
        for &x in &[f.x, f.right() - 1] {
            if !cr.contains(x, y) && px[y as usize * mocks::W + x as usize] != GUI_COLOR_HIGHLIGHT {
                broken.push((x, y));
            }
        }
    }
    assert!(broken.is_empty(), "COMMIT で今の枠が欠けた: {} 点 (先頭 {:?})", broken.len(), broken.first());
    /* カーソルは COMMIT に掛かっていない (Help の Paint から離れている)。退避した
     * 下地が本当の下地なら、消したあとにカーソルの形の画素は残らない (Codex レビュー
     * 2 回目 P2: 枠の外接矩形で discard → show して、カーソル自身を下地に退避していた)。 */
    let cr = crate::cursor::rect(g);
    assert!(
        (0..g.windows[0].issued.len).all(|k| !g.windows[0].issued.rects[k].translate(ox, oy).intersects(&cr)),
        "前提: カーソルが COMMIT の矩形に掛かっている"
    );
    crate::cursor::hide(g);
    let px = mocks::pixels();
    use os32api::gui::proto::{GUI_COLOR_TEXT, GUI_COLOR_WINDOW};
    let mut ghost = Vec::new();
    for y in cr.y..cr.bottom() {
        for x in cr.x..cr.right() {
            let c = px[y as usize * mocks::W + x as usize];
            if c == GUI_COLOR_TEXT || c == GUI_COLOR_WINDOW {
                ghost.push((x, y, c));
            }
        }
    }
    assert!(ghost.is_empty(), "カーソルを消した跡にカーソルの画素が残る: {} 点 (先頭 {:?})", ghost.len(), ghost.first());
    crate::cursor::show(g);
    *mocks::MOUSE.lock().unwrap() = ((tx + 200) as i16, (ty + 160) as i16, 0);
    input::capture(g, input::Ctx::Wait);
}

/// 前面が替わったら、**重なっていなくても**旧前面のタイトルを非アクティブ色へ
/// 描き直す (束ねビルドの報告: GRPH+TAB / タスクバーで旧前面が青のまま)。
#[test]
fn focus_change_repaints_the_old_front_title_without_overlap() {
    mocks::init();
    fep_off();
    mocks::clear(0);
    let shm = mocks::Shm::new();
    /* 重ならない 2 枚。添字 1 が前面。 */
    let mut st = windows(&shm, &[(20, 20, 200, 120), (300, 200, 200, 120)]);
    st.screen_w = 640;
    st.screen_h = 480;
    crate::visible::recompute_and_expose(&mut st);
    wm::composite_full(&mut st);
    park_pointer(&mut st, 600, 20);
    wm::flush_screen_dirty(&mut st);
    let px_at = |st: &GuiState, i: usize| {
        let tb = st.windows[i].titlebar_rect();
        mocks::pixels()[(tb.y + 1) as usize * mocks::W + (tb.x + tb.w / 2) as usize]
    };
    let inactive = px_at(&st, 0);
    let active = px_at(&st, 1);
    assert_ne!(inactive, active, "前提: アクティブと非アクティブのタイトル色が違う");
    /* タスクバー / GRPH+TAB の共通経路で窓 0 を前面へ */
    wm::activate_index(&mut st, 0);
    wm::flush_screen_dirty(&mut st);
    assert_eq!(px_at(&st, 0), active, "新しい前面のタイトルがアクティブ色にならない");
    assert_eq!(px_at(&st, 1), inactive, "旧前面のタイトルがアクティブ色のまま残る");
}

/// ドラッグ中にアプリが窓を resize し、マウスキーの離し (NP_DOT) が枠の追従を
/// 通らずに確定しても、最後の枠の跡を残さない (Codex レビュー 2 回目 P2)。
#[test]
fn drop_after_app_resize_erases_the_last_frame() {
    mocks::init();
    fep_off();
    mocks::clear(0);
    let shm = mocks::Shm::new();
    /* 0 = 背面 (枠の右辺がクライアント面を通る)、1 = 動かす窓 (前面) */
    let mut st = windows(&shm, &[(150, 40, 300, 300), (20, 20, 200, 150)]);
    st.screen_w = 640;
    st.screen_h = 480;
    crate::visible::recompute_and_expose(&mut st);
    wm::composite_full(&mut st);
    let tb = st.windows[1].titlebar_rect();
    park_pointer(&mut st, tb.x + 20, tb.y + tb.h / 2);
    wm::flush_screen_dirty(&mut st);
    paint_clients_as_app(&mut st);
    let mut r = vec![SC_KANA | DOWN | KANA];
    r.extend_from_slice(&tap(NP_0, KANA));
    for _ in 0..10 {
        r.push(NP_6 | DOWN | KANA);
    }
    run(&mut st, &r);
    wm::flush_screen_dirty(&mut st);
    assert_eq!(st.drag_index, 1, "ドラッグが始まらない");
    let last = st.drag_frame;
    assert_eq!((last.x, last.w), (77, 200), "前提: 枠が 57 ドット動いていない");
    /* ドラッグ中にアプリが自分の窓を縮める */
    let wid = id(&st, 1);
    assert_eq!(wm::resize_window(&mut st, 3, wid, 80, 60), 0);
    /* 背面のアプリが resize の露出分 (dirty) を描き直した状態にする。枠の跡は
     * その外にあるので残っている。 */
    {
        let (ox, oy) = st.windows[0].client_origin();
        let d = st.windows[0].dirty;
        let mut touched = wm::Rect::EMPTY;
        for k in 0..d.len {
            let r = d.rects[k].translate(ox, oy);
            unsafe { os32api::gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, CLIENT_SENTINEL) };
            touched = touched.union(&r);
        }
        st.windows[0].dirty.clear();
        st.windows[0].issued.clear();
        /* その COMMIT は今の枠を描き直す (op_commit と同じ) */
        if input::live_frame_edges_hit(&st, touched).is_some() {
            input::draw_live_outline(&st);
        }
    }
    let rx = last.right() - 1;
    let px = mocks::pixels();
    assert_ne!(px[(last.y + 60) as usize * mocks::W + rx as usize], CLIENT_SENTINEL, "前提: 枠の右辺の跡が背面のクライアント面に無い");
    run(&mut st, &tap(NP_DOT, KANA));
    wm::flush_screen_dirty(&mut st);
    assert_eq!(st.drag_index, -1, "離しでドラッグが終わらない");
    assert_eq!((st.windows[1].w, st.windows[1].h), (80, 60));
    assert!(st.windows[1].outer() != last, "前提: 確定した外形が最後の枠と同じ");
    let cr = crate::cursor::rect(&st);
    let stale = stale_client_pixels(&st, 0, &|x, y| cr.contains(x, y));
    assert!(stale.is_empty(), "背面のクライアント面に最後の枠の跡が残り、Paint も無い: {} 点 (先頭 {:?})", stale.len(), stale.first());
}


/* ---- COMMIT と最前面物 (Codex レビュー 3 回目 P2×2) ----
 * モーダル (274,185,92,86) を開いたまま、枠 (330,100,200,300) をドラッグ中に
 * 背面の Help (owner 2) が Paint → COMMIT する。重なり順は
 * 「アプリ < 枠 < 最前面物 < カーソル」。 */

/// 枠をドラッグ中の状態を組む。`cursor` の位置へカーソルを置き、`pieces`
/// (画面座標) を Help の dirty にして返す (COMMIT の直前まで)。
fn commit_over_modal_fixture(shm: &mocks::Shm, cursor: (i32, i32), pieces: &[wm::Rect]) -> &'static mut GuiState {
    use crate::handler;
    use os32api::gui::proto::{GUI_MODAL_OK, GUI_OP_POLL};
    mocks::init();
    fep_off();
    mocks::clear(0);
    let g = wm::g();
    *g = windows(shm, &[(300, 40, 320, 400), (20, 20, 200, 150)]);
    g.inited = true;
    g.screen_w = 640;
    g.screen_h = 480;
    crate::visible::recompute_and_expose(g);
    wm::composite_full(g);
    paint_clients_as_app(g);
    assert!(modal::open_wm_message(g, GUI_MODAL_OK, b"Modal\0", modal::WM_PURPOSE_NOTIFY));
    crate::visible::recompute_and_expose(g);
    wm::flush_screen_dirty(g);
    let m = modal::rect();
    assert_eq!((m.x, m.y, m.w, m.h), (274, 185, 92, 86), "前提: モーダルの位置が変わった");
    park_pointer(g, cursor.0, cursor.1);
    /* ドラッグ中 (マウスの状態は触らずに枠だけ置く): 枠 → 最前面物 → カーソル */
    g.drag_index = 1;
    g.drag_frame = wm::Rect::new(330, 100, 200, 300);
    crate::cursor::hide(g);
    input::draw_live_frame(g, &[]);
    crate::cursor::show(g);
    wm::flush_present();
    /* 背面の Help: 与えた断片だけが汚れている */
    let (ox, oy) = g.windows[0].client_origin();
    g.windows[0].dirty.clear();
    g.windows[0].issued.clear();
    for r in pieces {
        g.windows[0].dirty.push(r.translate(-ox, -oy));
    }
    let n = handler::gshell_gui_handler(GUI_OP_POLL, 0, 2);
    assert!(n > 0, "前提: Help に Paint が出ない");
    g
}

/// アプリが issued を塗って COMMIT する。塗った矩形の外接矩形を返す。
fn app_paints_and_commits(g: &mut GuiState) -> wm::Rect {
    use crate::handler;
    use os32api::gui::proto::GUI_OP_COMMIT;
    let (ox, oy) = g.windows[0].client_origin();
    let mut touched = wm::Rect::EMPTY;
    for k in 0..g.windows[0].issued.len {
        let r = g.windows[0].issued.rects[k].translate(ox, oy);
        unsafe { os32api::gfx::gfx_fill_rect(r.x, r.y, r.w, r.h, CLIENT_SENTINEL) };
        touched = touched.union(&r);
    }
    assert_eq!(handler::gshell_gui_handler(GUI_OP_COMMIT, 0, 2), 0);
    touched
}

fn snapshot(r: wm::Rect) -> Vec<u8> {
    let px = mocks::pixels();
    let mut v = Vec::new();
    for y in r.y..r.bottom() {
        for x in r.x..r.right() {
            v.push(px[y as usize * mocks::W + x as usize]);
        }
    }
    v
}

fn first_diff(r: wm::Rect, before: &[u8]) -> Option<(i32, i32, u8, u8)> {
    let now = snapshot(r);
    let mut i = 0;
    for y in r.y..r.bottom() {
        for x in r.x..r.right() {
            if now[i] != before[i] {
                return Some((x, y, before[i], now[i]));
            }
            i += 1;
        }
    }
    None
}

/// COMMIT が枠の右辺 (モーダルから離れた所) だけに掛かっても、全周を描き直した
/// 枠の左辺がモーダルに線を残さない (Codex レビュー 3 回目 P2 の 2 件目)。
#[test]
fn commit_frame_redraw_keeps_the_modal_on_top() {
    let shm = mocks::Shm::new();
    /* カーソルはモーダルから離れた所。Help の断片は枠の右辺 (x=529) の周り。 */
    let g = commit_over_modal_fixture(&shm, (600, 445), &[wm::Rect::new(512, 96, 32, 320)]);
    let m = modal::rect();
    let before = snapshot(m);
    let touched = app_paints_and_commits(g);
    assert!(!touched.intersects(&m), "前提: COMMIT がモーダルに掛かった");
    assert_eq!(first_diff(m, &before), None, "モーダルの画素が変わった (枠の線が上に残った)");
}

/// COMMIT がモーダルの一部 (カーソルの無い所) に掛かってモーダルを描き直しても、
/// カーソルは最後に描かれて欠けない (Codex レビュー 3 回目 P2 の 1 件目)。
#[test]
fn commit_overlay_redraw_keeps_the_cursor() {
    let shm = mocks::Shm::new();
    /* カーソルはモーダルの左端 (x=276..285)。Help の断片は枠の右辺の周りと
     * モーダルの下 — 外接矩形 (x>=302) がモーダルに掛かり、カーソルは避ける。 */
    let g = commit_over_modal_fixture(
        &shm,
        (276, 200),
        &[wm::Rect::new(512, 96, 32, 320), wm::Rect::new(304, 280, 32, 20)],
    );
    let m = modal::rect();
    let cr = crate::cursor::rect(g);
    assert!(m.contains(cr.x, cr.y) && m.contains(cr.right() - 1, cr.bottom() - 1), "前提: カーソルがモーダルの上に無い");
    let before_cursor = snapshot(cr);
    let before_modal = snapshot(m);
    let touched = app_paints_and_commits(g);
    assert!(touched.intersects(&m) && !touched.intersects(&cr), "前提: COMMIT がモーダルに掛かり、カーソルは避ける配置でない");
    assert_eq!(first_diff(cr, &before_cursor), None, "カーソルが欠けた (最前面物の描き直しに消された)");
    assert_eq!(first_diff(m, &before_modal), None, "モーダルの画素が変わった");
}

/// カーソルが今の枠の縁に掛かり、COMMIT はカーソルを避けて同じ縁の別の所に
/// 掛かる。枠を描き直してもカーソルの退避は本当の下地 (枠 + アプリの画) で、
/// カーソルを消した跡にカーソルの画素が残らない (2 回目 P2 の守り: アプリに潰されて
/// いないカーソルを `discard` すると、表示中のカーソル画素を下地として退避する)。
#[test]
fn commit_frame_redraw_under_the_cursor_saves_the_real_background() {
    use os32api::gui::proto::{GUI_COLOR_TEXT, GUI_COLOR_WINDOW};
    let shm = mocks::Shm::new();
    let g = commit_over_modal_fixture(&shm, (524, 380), &[wm::Rect::new(512, 96, 32, 200)]);
    let cr = crate::cursor::rect(g);
    let f = g.drag_frame;
    assert!(cr.contains(f.right() - 1, cr.y), "前提: カーソルが枠の右辺に掛かっていない");
    let touched = app_paints_and_commits(g);
    assert!(!touched.intersects(&cr), "前提: COMMIT がカーソルに掛かった");
    crate::cursor::hide(g);
    let px = mocks::pixels();
    let mut ghost = Vec::new();
    for y in cr.y..cr.bottom() {
        for x in cr.x..cr.right() {
            let c = px[y as usize * mocks::W + x as usize];
            if c == GUI_COLOR_TEXT || c == GUI_COLOR_WINDOW {
                ghost.push((x, y, c));
            }
        }
    }
    assert!(ghost.is_empty(), "カーソルの画素が下地として退避されていた: {} 点 (先頭 {:?})", ghost.len(), ghost.first());
    crate::cursor::show(g);
}

/// 画面下端の FEP 候補窓がタスクバーに重なり、そこにドラッグ枠の縁が掛かる。
/// 枠の周 (FEP を描かない) と候補の更新の周 (FEP だけを描く `post_cycle`) とで
/// 重なりの画素が変わらない = 上下が入れ替わって点滅しない (Codex レビュー 4 回目 P2)。
#[test]
fn fep_over_taskbar_keeps_its_order_across_frame_and_fep_cycles() {
    unsafe extern "C" fn on() -> i32 {
        1
    }
    mocks::init();
    fep_off();
    mocks::clear(0);
    let shm = mocks::Shm::new();
    let mut st = windows(&shm, &[(20, 20, 400, 420)]);
    st.screen_w = 640;
    st.screen_h = 480;
    crate::visible::recompute_and_expose(&mut st);
    wm::composite_full(&mut st);
    park_pointer(&mut st, 600, 20);
    /* テキストカーソルを画面の下端近くに置いて FEP を出す */
    st.windows[0].tc_visible = true;
    st.windows[0].tc_x = 60;
    st.windows[0].tc_y = 410;
    unsafe {
        (*os32api::api_ptr()).ime_is_active = on;
    }
    fep::install();
    fep::pre_cycle(&mut st);
    wm::flush_screen_dirty(&mut st);
    fep::post_cycle(&mut st);
    let fr = fep::rect();
    let tb = crate::taskbar::rect(&st);
    assert!(!fr.is_empty() && fr.intersects(&tb), "前提: FEP がタスクバーに重なっていない ({},{},{},{})", fr.x, fr.y, fr.w, fr.h);
    let r = fr.intersect(&tb);
    /* ドラッグ中: 枠の左辺が FEP とタスクバーを縦に横切る */
    st.drag_index = 0;
    st.drag_frame = wm::Rect::new(fr.x + 5, 100, 100, fr.bottom() - 100);
    let frame_cycle = |st: &mut GuiState| {
        wm::flush_screen_dirty(st);
        snapshot(r)
    };
    let fep_cycle = |st: &mut GuiState| {
        fep::mark_redraw();
        fep::pre_cycle(st);
        wm::flush_screen_dirty(st);
        fep::post_cycle(st);
        snapshot(r)
    };
    let a = frame_cycle(&mut st);
    let b = fep_cycle(&mut st);
    let c = frame_cycle(&mut st);
    assert!(a == b, "枠の周と FEP の周とで FEP / タスクバーの重なりの画素が違う (上下が入れ替わる)");
    assert!(b == c, "FEP の周の次の枠の周で重なりの画素が戻る (点滅)");
    st.drag_index = -1;
    fep_off();
}
