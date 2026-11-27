//! Admission, deferral and WM control handling for one raw keyboard event.
use super::{Ctx, GuiState, MOD_CTRL, MOD_GRPH, SC_CTRL, SC_GRPH, SC_SHIFT, SC_STOP, SC_TAB,
            STOP_SUPPRESS_TICKS};
#[cfg(feature = "c_gui_accept")]
use super::report_accept_raw;
use crate::{fep, kbdnav, modal, startmenu};

fn wm_scan(scan: u8) -> bool {
    matches!(scan, SC_STOP | SC_CTRL | SC_GRPH | SC_TAB)
}

fn stop_raw(raw: i32) -> bool {
    raw & 0x1ff == (0x100 | SC_STOP as i32) && raw & ((MOD_CTRL as i32) << 9) != 0
}

const SEALED_REPEAT: u8 = 3;
const SWITCH_OR_STOP: u8 = 4;

fn make_room(st: &mut GuiState, needed: usize, rank: u8) -> bool {
    if needed == 0 { return true; }
    let mut victims = [false; 128];
    let mut found = 0;
    for priority in 0..rank {
        for offset in 0..st.pending_raw_n {
            let i = if priority == SEALED_REPEAT { offset } else { st.pending_raw_n - 1 - offset };
            if found < needed && st.pending_rank[i] == priority {
                victims[i] = true;
                found += 1;
            }
        }
    }
    if found < needed {
        return false;
    }
    let mut write = 0;
    for (i, &remove) in victims[..st.pending_raw_n].iter().enumerate() {
        if !remove {
            st.pending_raw[write] = st.pending_raw[i];
            st.pending_rank[write] = st.pending_rank[i];
            write += 1;
        }
    }
    st.pending_raw_n = write;
    st.pending_raw_dropped = st.pending_raw_dropped.saturating_add(found as u32);
    true
}

// A completed TAB press contributes at most one queued repeat. Keep its
// direction and reserve the completion and emergency STOP seats before sealing it.
fn seal_repeats(st: &mut GuiState) {
    let mut seen = false;
    let mut kept = 0;
    for i in 0..st.pending_raw_n {
        if st.pending_rank[i] == 1 {
            if seen { continue; }
            seen = true;
        }
        st.pending_raw[kept] = st.pending_raw[i];
        st.pending_rank[kept] = st.pending_rank[i];
        kept += 1;
    }
    st.pending_raw_dropped = st.pending_raw_dropped.saturating_add((st.pending_raw_n - kept) as u32);
    st.pending_raw_n = kept;
    if seen {
        let limit = st.pending_raw.len() - 1 - usize::from(!st.pending_raw[..kept].iter().copied().any(stop_raw));
        make_room(st, kept.saturating_sub(limit), SWITCH_OR_STOP);
        if let Some(rank) = st.pending_rank[..st.pending_raw_n].iter_mut().find(|r| **r == 1) {
            *rank = SEALED_REPEAT;
        }
    }
}

// Admit a switch start and reserve its completion together. Evict from
// the oldest sealed press first among sealed repeats; compaction retains event order.
pub(crate) fn defer_raw(st: &mut GuiState, raw: i32) {
    let scan = (raw & 127) as u8;
    let down = raw & 256 != 0;
    let grph = raw & ((MOD_GRPH as i32) << 9) != 0;
    let finish = st.pending_switch && (!grph || (scan == SC_GRPH && !down));
    if finish && stop_raw(raw) {
        // STOP cleanup may remove the interrupt request independently.
        defer_raw(st, SC_GRPH as i32);
        defer_raw(st, raw);
        return;
    }
    let start = !st.pending_switch && grph && scan == SC_TAB && down;
    let repeat = st.pending_switch && st.pending_tab && scan == SC_TAB && down;
    if st.pending_tab && (finish || (scan == SC_TAB && !down)) {
        seal_repeats(st);
    }
    if scan == SC_TAB { st.pending_tab = down; }
    if !grph || finish { st.pending_tab = false; }
    let rank = if start || finish || stop_raw(raw) { SWITCH_OR_STOP }
        else if repeat { 1 } else if wm_scan(scan) { 2 } else { 0 };
    let reserve_finish = start || (st.pending_switch && !finish);
    let reserve_stop = start && !st.pending_raw[..st.pending_raw_n].iter().copied().any(stop_raw);
    let limit = st.pending_raw.len() - usize::from(reserve_finish) - usize::from(reserve_stop);
    let needed = (st.pending_raw_n + 1).saturating_sub(limit);
    if !make_room(st, needed, rank) {
        st.pending_raw_dropped = st.pending_raw_dropped.saturating_add(1);
        return;
    }
    st.pending_raw[st.pending_raw_n] = raw;
    st.pending_rank[st.pending_raw_n] = rank;
    st.pending_raw_n += 1;
    if start { st.pending_switch = true; }
    if finish { st.pending_switch = false; }
}

pub(crate) fn purge_boundary(st: &mut GuiState, same_owner: bool) {
    let mut write = 0;
    for i in 0..st.pending_raw_n {
        if wm_scan(st.pending_raw[i] as u8 & 127) || st.pending_rank[i] == SWITCH_OR_STOP {
            st.pending_raw[write] = st.pending_raw[i];
            st.pending_rank[write] = st.pending_rank[i];
            write += 1;
        }
    }
    st.pending_raw_n = write;
    if same_owner {
        loop {
            let raw = unsafe { (os32api::api().kbd_trygetrawkey)() };
            if raw < 0 { break; }
            if wm_scan(raw as u8 & 127) || (st.pending_switch && raw & ((MOD_GRPH as i32) << 9) == 0) { defer_raw(st, raw); }
        }
        for (scan, held) in st.held_keys.iter_mut().enumerate() {
            *held = !wm_scan(scan as u8) && unsafe { (os32api::api().kbd_is_pressed)(scan as i32) } > 0;
        }
    }
}

pub(super) enum Step {
    End,
    Consumed,
    Deliver(u8, bool, u32),
}

pub(super) fn next(st: &mut GuiState, ctx: Ctx, space_ok: bool, captured: &mut bool) -> Step {
    let ordinary_pump = ctx == Ctx::Pump && st.excl.open(crate::excl::KBD | crate::excl::MOUSE);
    if !space_ok && ordinary_pump {
        return Step::End;
    }
    /* X4 (ポンプ) で FEP がオンなら変換をここでは行わない (契約 T8: 辞書検索
     * まで走る)。退避して次の X3 に回す。SHIFT+SPACE が保留中 (on/off が次の
     * X3 で確定する) のときと、既に退避した打鍵があるときも退避する — でないと
     * 「SHIFT+SPACE → n i h o n g o」が X4 に来た場合、toggle 前の打鍵が FEP を
     * 素通りして先にアプリへ届く (2026-09-06 実測: 変換されず "nihongo" が入った)。
     *
     * モーダルと Start メニューも X3 でだけ処理する。 */
    let ui_defer = modal::is_open() || startmenu::is_open();
    let defer = ctx == Ctx::Pump
        && (ui_defer || fep::is_on() || fep::toggle_pending() || st.pending_raw_n > 0);
    /* 通常は満杯ならカーネル側に残す。WM UI 中は読み続け、あふれた
     * 新しい raw を数える。古い退避済み raw の順序は保つ。 */
    if ordinary_pump && !ui_defer && st.pending_raw_n == st.pending_raw.len() { return Step::End; }
    /* X3 は前の X4 が退避した raw を先に消費する (順序を保つ)。 */
    let from_pending = ctx.wm_ui() && st.pending_raw_n > 0;
    let raw = if from_pending {
        let r = st.pending_raw[0];
        let mut q = 1;
        while q < st.pending_raw_n {
            st.pending_rank[q - 1] = st.pending_rank[q];
            st.pending_raw[q - 1] = st.pending_raw[q];
            q += 1;
        }
        st.pending_raw_n -= 1;
        r
    } else {
        unsafe { (os32api::api().kbd_trygetrawkey)() }
    };
    if raw < 0 {
        return Step::End;
    }
    *captured = true;
    let scan = (raw & 0x7F) as u8;
    let down = ((raw >> 8) & 1) != 0;
    let mods = ((raw >> 9) & 0x7F) as u32;
    #[cfg(feature = "c_gui_accept")]
    if scan == 0x1d || wm_scan(scan) {
        report_accept_raw(scan, down);
    }
    /* Mirror kbd.c for WM-deferred raw as well. Inspect before defer and
     * overflow handling, so Pump/FEP and full event rings obey the same
     * boundary. Breaks and modifier makes never start a new operation. */
    if (st.stop_suppressed
        && unsafe { (os32api::api().get_tick)() }.wrapping_sub(st.stop_tick) >= STOP_SUPPRESS_TICKS)
        || (down && scan != SC_STOP && !(SC_SHIFT..=SC_CTRL).contains(&scan))
    {
        st.stop_suppressed = false;
    }
    if st.stop_suppressed && scan == SC_STOP && down && (mods & MOD_CTRL) != 0 {
        return Step::Consumed;
    }
    /* WM のショートカットとマウスキーは状態機械を動かすので X3 でだけ
     * 実行する (票 KBD_NAV §1-4)。X4 で見つけたら FEP と同じく、この raw と
     * 以後を全部退避して次の X3 へ回す (順序を保つ)。 */
    if defer || (ctx == Ctx::Pump && kbdnav::is_wm_raw(st, raw)) {
        defer_raw(st, raw);
        return Step::Consumed;
    }
    if !from_pending {
        if mods & MOD_GRPH == 0 || (scan == SC_GRPH && !down) {
            st.pending_switch = false;
            st.pending_tab = false;
        } else if scan == SC_TAB {
            st.pending_tab = down;
            st.pending_switch |= down;
        }
    }
    /* X4 まで来た make は WM のキーではない (上で退避していない)。古い印を
     * 消して、対になる break を X4 がそのまま配れるようにする。 */
    if ctx == Ctx::Pump && down {
        kbdnav::pump_delivered_make(st, scan);
    }

    /* 修飾キーとして捨てる**前**に: カナ (マウスキーの切り替え) と、GRPH+TAB
     * 切り替え中の GRPH の離し (票 KBD_NAV §1-2 / §1-5)。 */
    if ctx.wm_ui() && kbdnav::finish_if_released(st, scan, down, mods) {
        if stop_raw(raw) { st.abort_seen = true; }
        return Step::Consumed;
    }
    if ctx.wm_ui() && scan == SC_TAB && mods & MOD_GRPH != 0 {
        kbdnav::on_key(st, scan, down, mods);
        return Step::Consumed;
    }
    if scan == SC_STOP && down && (mods & MOD_CTRL) != 0 {
        st.abort_seen = true;
        if !space_ok {
            // Preserve later raw input until STOP changes the foreground.
            return Step::End;
        }
        return Step::Consumed;
    }
    if st.held_keys[scan as usize] {
        if !down { st.held_keys[scan as usize] = false; }
        return Step::Consumed;
    }
    if !st.excl.open(crate::excl::KBD) { return Step::Consumed; }
    Step::Deliver(scan, down, mods)
}
