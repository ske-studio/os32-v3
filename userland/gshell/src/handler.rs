//! handler.rs — `gui_call` ハンドラ (契約 T1 / T8 の X1・X2・X3)。
//!
//! カーネル (`kernel/gui.c`) は `gui_call(op, arg)` を
//! `handler(op, arg, res_owner_get())` へ転送するだけで、op の意味を知らない。
//! ここが WM の唯一の入口。op → 関数表で引き、owner ↔ スロットを検証してから
//! 各処理へ渡す (match の巨大化を避ける)。
//!
//! 実行文脈の割り当て (契約 T8。**票の鉄則と対**):
//!   - X1 = WAIT / COMMIT 以外のすべて。**状態更新とリング追記だけ**。VRAM へ
//!     書かず、待たず、数十 µs で戻る。画面の描き直しが要るものは
//!     `st.screen_dirty` / `win.dirty` に積むだけで、実際に描くのは X3。
//!   - X2 = [`op_commit`]。issued 矩形の present とカーソルの描き直しだけ。
//!   - X3 = [`op_wait`]。WM の全周期 (入力 → WM の UI → クローム present → halt)。
//!
//! 要求は SHM スロットの要求ブロック (512B) から読み、応答は応答ブロック
//! (512B) へ書く。小さい値 (WindowId など) は `arg` に直接載ってくるので、
//! **`arg != 0` ならそれを、0 なら要求ブロックの `GuiReqWindow` を**採る
//! (どちらの流儀のクライアントでも動く)。

use crate::wm::{self, GuiState, Rect, RectSet, MAX_DMG, MAX_VIS};
use crate::{
    cursor, damage, input, lease, modal, multiapp, reqs, ring, session, slot, timer, visible,
};
use os32api::gui::proto::{
    GuiRect16, GuiReqModalResult, GuiReqSession, GuiRespModalResult, GuiString, GuiWinSpec,
    GUI_EV_PAINT, GUI_MAX_WINDOWS, GUI_OP_COMMIT, GUI_OP_INIT, GUI_OP_INVALIDATE,
    GUI_OP_LEASE_PALETTE, GUI_OP_MODAL_OPEN, GUI_OP_MODAL_RESULT, GUI_OP_OWNER_EXIT, GUI_OP_POLL,
    GUI_OP_SESSION_REQUEST, GUI_OP_STATS, GUI_OP_SURF_CREATE, GUI_OP_SURF_DESTROY,
    GUI_OP_TIMER_KILL, GUI_OP_TIMER_SET, GUI_OP_WAIT, GUI_OP_WIN_CLIENT_RECT, GUI_OP_WIN_CREATE,
    GUI_OP_WIN_DESTROY, GUI_OP_WIN_MOVE, GUI_OP_WIN_RAISE, GUI_OP_WIN_RESIZE,
    GUI_OP_WIN_SET_FOCUS, GUI_OP_WIN_SET_TEXT_CURSOR, GUI_OP_WIN_SET_TITLE, GUI_OP_WIN_SHOW,
    GUI_PROTO_VERSION, OS32_ERR_FULL, OS32_ERR_INVAL, OS32_ERR_NOSYS, OS32_ERR_STALE,
    OS32_ERR_VERSION,
};

/* ================================================================ */
/*  入口 (カーネルが呼ぶ C ABI)                                      */
/* ================================================================ */

// Internal OWNER_EXIT arg; EXEC_KIND_ABORTED in os32_kapi_shared.h.
const OWNER_EXIT_ABORTED: u32 = 3;

/// `gui_register` に渡すハンドラ。**この 1 本がアプリ → WM の全経路**。
#[no_mangle]
pub extern "C" fn gshell_gui_handler(op: u32, arg: u32, owner: i32) -> i32 {
    let st = wm::g();
    if !st.inited {
        return OS32_ERR_NOSYS;
    }
    st.now = unsafe { (os32api::api().get_tick)() };

    /* スロットを要らない (あるいは持てない) op を先に片づける。 */
    if op == GUI_OP_OWNER_EXIT {
        if arg == OWNER_EXIT_ABORTED {
            input::discard_stop(st);
        }
        /* カーネルの exec_exit から。owner のウィンドウ / タイマ / スロットを回収。
         * 描かない (X1) — 空いた領域は screen_dirty に積まれ、次の X3 で埋まる。 */
        modal::reclaim_owner(st, owner);
        /* sticky な Quit は宛先ごと消える。**SessionAction は残す** (契約 S2:
         * 正常 exit / CTRL+STOP / fault kill のいずれでも失わない)。 */
        session::reclaim_owner(owner);
        st.reclaim_owner(owner);
        /* K5b-W: 譲り合いの表からもこの ID の 1 本分だけ落とす (T4 / U8)。
         * 4 つの畳み方 (exit / fault / CTRL+STOP / exec_kill) はすべて
         * カーネルの `gui_owner_exit` を通るので、回収はここ 1 か所で足りる。 */
        multiapp::on_owner_exit(owner);
        visible::recompute_and_expose(st);
        return 0;
    }
    if op == GUI_OP_INIT {
        return op_init(st, owner, arg);
    }

    let slot_no = match st.slot_of_owner(owner) {
        Some(s) => s,
        None => return OS32_ERR_INVAL, /* OP_INIT を先に呼ぶこと */
    };
    match lookup(op) {
        Some(f) => f(st, owner, slot_no, arg),
        None => OS32_ERR_NOSYS,
    }
}

/* ================================================================ */
/*  op → 関数表                                                      */
/* ================================================================ */

type OpFn = fn(&mut GuiState, i32, usize, u32) -> i32;

struct Entry {
    op: u32,
    f: OpFn,
}

static TABLE: [Entry; 23] = [
    Entry { op: GUI_OP_POLL, f: op_poll },
    Entry { op: GUI_OP_WAIT, f: op_wait },
    Entry { op: GUI_OP_COMMIT, f: op_commit },
    Entry { op: GUI_OP_INVALIDATE, f: op_invalidate },
    Entry { op: GUI_OP_STATS, f: op_stats },
    Entry { op: GUI_OP_LEASE_PALETTE, f: op_lease_palette },
    Entry { op: GUI_OP_WIN_CREATE, f: op_win_create },
    Entry { op: GUI_OP_WIN_DESTROY, f: op_win_destroy },
    Entry { op: GUI_OP_WIN_MOVE, f: op_win_move },
    Entry { op: GUI_OP_WIN_RESIZE, f: op_win_resize },
    Entry { op: GUI_OP_WIN_SHOW, f: op_win_show },
    Entry { op: GUI_OP_WIN_SET_TITLE, f: op_win_set_title },
    Entry { op: GUI_OP_WIN_CLIENT_RECT, f: op_win_client_rect },
    Entry { op: GUI_OP_WIN_RAISE, f: op_win_raise },
    Entry { op: GUI_OP_WIN_SET_FOCUS, f: op_win_set_focus },
    Entry { op: GUI_OP_WIN_SET_TEXT_CURSOR, f: op_win_set_text_cursor },
    Entry { op: GUI_OP_SURF_CREATE, f: op_nosys },
    Entry { op: GUI_OP_SURF_DESTROY, f: op_nosys },
    Entry { op: GUI_OP_TIMER_SET, f: op_timer_set },
    Entry { op: GUI_OP_TIMER_KILL, f: op_timer_kill },
    Entry { op: GUI_OP_MODAL_OPEN, f: op_modal_open },
    /* v1.2 (W4): 完了したモーダルの結果取得 (契約 V12-M の M1 / M2)。 */
    Entry { op: GUI_OP_MODAL_RESULT, f: op_modal_result },
    /* v1.2 (W3): セッション要求 (契約 V12-S の S4)。 */
    Entry { op: GUI_OP_SESSION_REQUEST, f: op_session_request },
];

fn lookup(op: u32) -> Option<OpFn> {
    let mut i = 0;
    while i < TABLE.len() {
        if TABLE[i].op == op {
            return Some(TABLE[i].f);
        }
        i += 1;
    }
    None
}

/// 契約にある op のうち WM が実装しないもの (G3 のサーフェスはクライアント側の
/// 領分)。`OS32_ERR_NOSYS`。
fn op_nosys(_st: &mut GuiState, _owner: i32, _slot: usize, _arg: u32) -> i32 {
    OS32_ERR_NOSYS
}

/* ================================================================ */
/*  小道具                                                           */
/* ================================================================ */

#[inline]
fn rect16(r: Rect) -> GuiRect16 {
    GuiRect16 { x: r.x as i16, y: r.y as i16, w: r.w as i16, h: r.h as i16 }
}

/// 対象ウィンドウ ID を決める: `arg` が非 0 ならそれ、0 なら要求ブロックの
/// `GuiReqWindow` (契約 T2 の「小さい値は arg に載せる」の両対応)。
fn target_window(st: &GuiState, slot_no: usize, arg: u32) -> u32 {
    if arg != 0 {
        arg
    } else {
        let r: reqs::GuiReqWindow = unsafe { slot::read_req(st, slot_no) };
        r.window
    }
}

/// 所有者の窓 index を引く (generation + owner 検証つき)。
fn owned_index(st: &GuiState, owner: i32, id: u32) -> Result<usize, i32> {
    match st.win_by_id(id) {
        None => Err(OS32_ERR_STALE),
        Some(i) => {
            if st.windows[i].owner != owner {
                Err(OS32_ERR_INVAL)
            } else {
                Ok(i)
            }
        }
    }
}

/// 損傷集合へ 1 矩形を足す (上限を超えたら先頭と union して数を保つ)。
fn push_or_merge(set: &mut RectSet, r: Rect) {
    if r.is_empty() {
        return;
    }
    if set.len < MAX_DMG {
        set.push(r);
    } else {
        set.rects[0] = set.rects[0].union(&r);
    }
}

/* ================================================================ */
/*  OP_INIT (契約 T2a / T5)                                          */
/*                                                                  */
/*  arg = アプリが期待する proto_version (0 = 指定なし)。戻り値は     */
/*  スロット番号 (0〜3)。v1 は常に 0。                                */
/* ================================================================ */
fn op_init(st: &mut GuiState, owner: i32, arg: u32) -> i32 {
    if arg != 0 && arg != GUI_PROTO_VERSION as u32 {
        return OS32_ERR_VERSION;
    }
    let slot_no = match st.alloc_slot(owner) {
        Some(s) => s,
        None => return OS32_ERR_FULL, /* 5 本目は起動できない (T2a) */
    };
    slot::init_header(st, slot_no);
    st.slots[slot_no].serial = 0;
    st.slots[slot_no].last_reported_dropped = 0;
    slot_no as i32
}

/* ================================================================ */
/*  OP_POLL (契約 T3) — 導出型を同じリングへ追記して未読件数を返す     */
/* ================================================================ */
fn op_poll(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    /* 前回の OP_POLL で渡した dropped / OVERFLOW を消し込む (受領済み)。 */
    consume_reported_dropped(st, slot_no);

    /* (0) sticky な制御イベントを Paint より先に入れる (W4 §1 の 4 / §8、
     * W3 の契約 S5)。リング満杯で入らなかった完了通知 / Quit は捨てず、
     * 空きができたここで届く。**`dropped` には数えない**。 */
    modal::retry_pending(st, slot_no);
    session::retry_pending(st, slot_no);

    /* (1) 導出型を追記: Configure → Timer → Paint。 */
    emit_configures(st, owner, slot_no);
    let now = st.now;
    timer::fire_expired(st, slot_no, owner, now);
    emit_paints(st, owner, slot_no);

    /* (2) 戻り値 = tail − head。seq を進め、今回渡した dropped を控える。 */
    let mut h = slot::read_header(st, slot_no);
    h.seq = h.seq.wrapping_add(1);
    slot::write_header(st, slot_no, &h);
    st.slots[slot_no].last_reported_dropped = h.dropped;
    ring::count(h.ring_head, h.ring_tail) as i32
}

/// 前回の `OP_POLL` でアプリへ見せた分の `dropped` を引き、0 になったら
/// `OVERFLOW` を落とす。その後に増えた分は残す (見せていないので消さない)。
fn consume_reported_dropped(st: &mut GuiState, slot_no: usize) {
    let seen = st.slots[slot_no].last_reported_dropped;
    if seen == 0 {
        return;
    }
    let mut h = slot::read_header(st, slot_no);
    h.dropped = h.dropped.saturating_sub(seen);
    if h.dropped == 0 {
        h.flags &= !os32api::gui::proto::GUI_HDR_FLAG_OVERFLOW;
    }
    slot::write_header(st, slot_no, &h);
    st.slots[slot_no].last_reported_dropped = 0;
}

/// 座標が確定した窓の `Configure` を流す (導出型、最新 1 件)。
fn emit_configures(st: &mut GuiState, owner: i32, _slot_no: usize) {
    let mut idx = 0;
    while idx < GUI_MAX_WINDOWS {
        if st.windows[idx].used
            && st.windows[idx].owner == owner
            && st.windows[idx].configure_pending
        {
            input::emit_configure(st, idx);
        }
        idx += 1;
    }
}

/// dirty ∩ 可視領域を `Paint` にして流し、渡った分を issued へ移す (契約 G4)。
/// リングに入らなかった分と可視領域の外は dirty のまま残る (状態なので失われない)。
fn emit_paints(st: &mut GuiState, owner: i32, slot_no: usize) {
    let mut idx = 0;
    while idx < GUI_MAX_WINDOWS {
        if st.windows[idx].used && st.windows[idx].owner == owner {
            emit_paints_win(st, idx, slot_no);
        }
        idx += 1;
    }
}

/// `cand` に dirty の添字 `d` 由来の断片があるか。
fn cand_has(cand: &[(usize, Rect); MAX_VIS], ncand: usize, d: usize) -> bool {
    let mut m = 0;
    while m < ncand {
        if cand[m].0 == d {
            return true;
        }
        m += 1;
    }
    false
}

fn emit_paints_win(st: &mut GuiState, idx: usize, slot_no: usize) {
    /* 可視領域が 16 超で打ち切られていた窓は、周ごとに捨てる断片を入れ替える
     * (契約 G4「超過分は次の周」、レビュー #4 ⑤)。 */
    visible::page_vis(st, idx);
    if st.windows[idx].dirty.is_empty() || st.windows[idx].vis.is_empty() {
        return;
    }
    let id = st.windows[idx].id(idx);
    let dirty = st.windows[idx].dirty;

    /* (a) 配送候補 = dirty ∩ 可視領域。**起床判定と同じ関数**を使う
     * (`damage::has_deliverable_paint` もこれを通る。両者が食い違うと
     * 「起こされないと配れない / 配れないと起きられない」で止まる)。 */
    let (cand, ncand, cand_capped) = damage::deliverable_cand(&st.windows[idx]);

    /* (b) リングへ流す。空きが尽きたらそこで止める (残りは dirty のまま)。 */
    let mut delivered = [false; MAX_VIS];
    let mut j = 0;
    while j < ncand {
        if st.windows[idx].issued.len >= MAX_VIS {
            break;
        }
        let ev = ring::ev_rect(GUI_EV_PAINT, id, rect16(cand[j].1));
        if !ring::append(st, slot_no, &ev) {
            break;
        }
        st.windows[idx].issued.push(cand[j].1);
        delivered[j] = true;
        j += 1;
    }

    /* (c) 渡せなかった分を dirty として残す (dirty 各矩形 − 渡した断片)。
     *
     * ただし**可視領域が確定していて (打ち切り無し・空でない)、候補が 1 つも
     * 出なかった dirty** は捨てる。`add_dirty` の 32px 丸めではみ出した縁など、
     * 可視領域の外にある分は何周回しても `Paint` にならず、`dirty` が永久に
     * 空にならない原因になる (実測: dirty が空になる周が 1 度も無かった)。
     * 隠れていた場所が後で出てきたときは `recompute_and_expose` が露出分を
     * dirty に足し直すので、描き落としにはならない (契約 G4)。 */
    /* `cand_capped` のときは「候補に無い = 不可視」と言えない。候補配列が
     * 16 で埋まって**まだ見ていない dirty** が残っているだけかもしれず、
     * そこを捨てると可視なのに描かれない (レビュー指摘 P2、2026-09-10)。
     * 打ち切られた周は 1 つも捨てず、次の周に回す。 */
    let authoritative = !st.windows[idx].vis_capped
        && !st.windows[idx].vis.is_empty()
        && !cand_capped;
    let mut new_dirty = RectSet::EMPTY;
    let mut d = 0;
    while d < dirty.len {
        if authoritative && !cand_has(&cand, ncand, d) {
            d += 1;
            continue;
        }
        let mut region = RectSet::EMPTY;
        region.push(dirty.rects[d]);
        let mut m = 0;
        while m < ncand {
            if delivered[m] && cand[m].0 == d {
                let (next, _ok) = visible::region_subtract_rect(&region, cand[m].1);
                region = next;
            }
            m += 1;
        }
        let mut r = 0;
        while r < region.len {
            push_or_merge(&mut new_dirty, region.rects[r]);
            r += 1;
        }
        d += 1;
    }
    st.windows[idx].dirty = new_dirty;
}

/* ================================================================ */
/*  OP_WAIT (契約 T3 / T8 の X3) — WM の全周期を回して眠る            */
/* ================================================================ */
fn op_wait(st: &mut GuiState, owner: i32, slot_no: usize, arg: u32) -> i32 {
    let start = unsafe { (os32api::api().get_tick)() };
    let timeout = arg; /* ticks。0 = 期限なし */

    /* 期限 = min(timeout, 次のタイマ) (契約 U5)。どちらも無ければ「起こされるまで」。 */
    let mut deadline: Option<u32> = if timeout > 0 { Some(start.wrapping_add(timeout)) } else { None };
    if let Some(t) = timer::next_deadline_owner(st, owner) {
        deadline = Some(match deadline {
            Some(d) if d.wrapping_sub(t) >= 0x8000_0000 => d, /* d <= t */
            _ => t,
        });
    }

    loop {
        /* (0) 譲り合い (D11-3 の (1))。**`exec_park` を呼ぶ唯一の点**で、
         * ここは WM の 1 周が終わって状態が整っている地点 (前の周の
         * `wm_cycle` の直後、`ring::pending` を読む前)。park が成立すると
         * この関数は戻らず、WM top-level が別のアプリを起こす。
         * 起こす相手が居ないとき (= アプリが 1 本のとき) は何もしないので、
         * 下のループは従来どおり `wm_cycle` + `sys_halt` のまま回る。 */
        multiapp::maybe_park(st, owner, deadline);

        /* WM の 1 周: 入力取り込み → WM 自身の UI → クローム/デスクトップ present。 */
        wm::wm_cycle(st, input::Ctx::Wait);

        /* CTRL+STOP (契約 T6)。宛先は**フォーカス窓のアプリ** (T9 D8 以降は
         * そこから要求表の連鎖を辿った末尾)。
         *
         * 宛先が本人でも別アプリでも**予約に統一する** (票 T9 受入 S6 の
         * 3 回目、実機 2026-09-13)。以前は本人宛てなら `break` してアプリを
         * syscall の出口へ戻し、カーネルの `abort_req` に畳ませていたが、
         * IRQ1 がアプリの CPL=3 実行中に着地した周は `abort_req` が立たず
         * (暴走ではないので K が立てない)、出口に何も無くて要求が消える
         * (3 回中 2 回失敗)。`exec_abort_clear` も `exec_kill` も owner 1
         * (WM top-level) からしか呼べない (K5c) ので、ここでは予約だけ積み、
         * `should_park` が譲らせて `drain_top_level` が実行する。 */
        if st.abort_seen {
            st.abort_seen = false;
            if multiapp::abort_targets_current(st, owner) {
                /* 予約は WM の表に載っている ID にしか積めない。載っていない
                 * = `should_park` も譲らせないので、top-level へ戻る道が無い。
                 * その 1 本だけは従来どおり出口に任せる (park できない文脈で
                 * 予約を残すと `has_top_level_work` が立ちっぱなしになる)。 */
                if !multiapp::is_tracked(owner) {
                    break;
                }
                multiapp::reserve_abort_self(st, owner);
            } else {
                /* 宛先はフォーカス窓の**別の**アプリ (決裁 A1)。走っている
                 * 本人が負っている要求を降ろし、宛先を畳む。 */
                multiapp::redirect_abort(st, owner);
            }
        }
        if wake_ready(st, owner, slot_no) {
            break;
        }
        let now = st.now;
        if let Some(d) = deadline {
            /* now >= d (ラップ安全) */
            if now.wrapping_sub(d) < 0x8000_0000 {
                break;
            }
        }
        /* 待ちは sys_halt だけ (get_tick スピン禁止)。PIT 10ms で必ず起きる。 */
        unsafe { (os32api::api().sys_halt)() };
    }
    ring::pending(st, slot_no) as i32
}

/// `OP_WAIT` が戻る条件 (契約 T3): 未読がある、または**配送できる**導出型がある。
/// 完全に隠れた窓の dirty (可視領域が空) では起きない (G0b-3c)。
fn wake_ready(st: &GuiState, owner: i32, slot_no: usize) -> bool {
    if ring::pending(st, slot_no) > 0 {
        return true;
    }
    if timer::has_expired(st, owner, st.now) {
        return true;
    }
    let mut idx = 0;
    while idx < GUI_MAX_WINDOWS {
        let w = &st.windows[idx];
        if w.used && w.owner == owner {
            if w.configure_pending {
                return true;
            }
            if damage::has_deliverable_paint(w) {
                return true;
            }
        }
        idx += 1;
    }
    false
}

/* ================================================================ */
/*  OP_COMMIT (契約 G4 / T8 の X2) — issued だけを present            */
/* ================================================================ */
fn op_commit(st: &mut GuiState, owner: i32, _slot_no: usize, arg: u32) -> i32 {
    /* X1 で控えたパレットのリースをここで実際に入れる (契約 T8 / G8)。 */
    lease::reconcile(st);
    let target = arg; /* 0 = この owner の全ウィンドウ */
    if target != 0 {
        if let Err(e) = owned_index(st, owner, target) {
            return e;
        }
    }
    let mut touched = Rect::EMPTY;
    let mut any = false;

    let mut idx = 0;
    while idx < GUI_MAX_WINDOWS {
        let hit = st.windows[idx].used
            && st.windows[idx].owner == owner
            && (target == 0 || st.windows[idx].id(idx) == target);
        if hit && st.windows[idx].issued.len > 0 {
            let (cox, coy) = st.windows[idx].client_origin();
            let n = st.windows[idx].issued.len;
            let mut i = 0;
            while i < n {
                /* issued はクライアントローカル。画面座標へ移して積む。 */
                let scr = st.windows[idx].issued.rects[i].translate(cox, coy);
                wm::queue_present(st, scr);
                touched = touched.union(&scr);
                i += 1;
            }
            st.windows[idx].issued.clear();
            any = true;
        }
        idx += 1;
    }

    if !any {
        return 0;
    }
    /* アプリの描画の上に WM が描き直す物: ドラッグ枠 → 最前面物 (モーダル /
     * タスクバー / メニュー / FEP の候補窓) → カーソル の順 (X3 と同じ重なり順。
     * `wm::overlays_to_refresh` の注記)。
     *
     * 判定範囲は touched ∪ 実際に描き直す枠の縁 ∪ 描き直す最前面物。枠は全周を
     * 描くので、touched に掛からない縁が最前面物を上書きすることがある (Codex
     * レビュー 3 回目 P2)。カーソルは**最後に 1 回だけ**描く — 途中で出すと後の
     * 最前面物の描き直しに消される。アプリに潰されたカーソルは退避を捨て、
     * 潰されていないカーソルは下地を戻してから描き直す (表示中のカーソル画素を
     * 下地として退避しない。2 回目 P2)。 */
    let edges = input::live_frame_edges_hit(st, touched);
    let mut regions = [Rect::EMPTY; 5];
    regions[0] = touched;
    if let Some(es) = edges {
        regions[1..5].copy_from_slice(&es);
    }
    let overlays = wm::overlays_to_refresh(st, &regions);
    let cr = cursor::rect(st);
    let cursor_hit = st.cursor.shown
        && (regions.iter().any(|r| !r.is_empty() && r.intersects(&cr))
            || overlays.iter().any(|r| !r.is_empty() && r.intersects(&cr)));
    if cursor_hit {
        if cr.intersects(&touched) {
            cursor::discard(st);
        } else {
            cursor::hide(st);
        }
    }
    if edges.is_some() {
        input::draw_live_outline(st);
    }
    wm::refresh_overlays(st, &overlays);
    if cursor_hit {
        cursor::show(st);
        let cr = cursor::rect(st);
        wm::queue_present(st, cr);
    }
    wm::flush_present(); /* ← 1 周 1 回の commit (契約 P) */
    0
}

/* ================================================================ */
/*  OP_LEASE_PALETTE (契約 G8) — 記録だけ。適用は X2 / X3            */
/* ================================================================ */
fn op_lease_palette(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: os32api::gui::proto::GuiReqLease = unsafe { slot::read_req(st, slot_no) };
    lease::request(st, owner, req.first, req.count, &req.rgb)
}

/* ================================================================ */
/*  OP_MODAL_OPEN (契約 U4 + v1.2 の W4 §2)                          */
/*  ダイアログを立てるだけ (描画も VFS 走査も X3)。                   */
/* ================================================================ */

/// この owner が持つ**現存する**ウィンドウのうち最前面のもの (Z 順)。
/// W4 §2 の「parent は caller owner の現存 window」の解決に使う。
fn front_owned_index(st: &GuiState, owner: i32) -> Option<usize> {
    let mut z = st.z_count;
    while z > 0 {
        z -= 1;
        let i = st.zorder[z];
        if st.windows[i].used && st.windows[i].owner == owner {
            return Some(i);
        }
    }
    /* Z 順に載っていない窓 (生成直後に溢れた等) も救う。 */
    let mut i = 0;
    while i < GUI_MAX_WINDOWS {
        if st.windows[i].used && st.windows[i].owner == owner {
            return Some(i);
        }
        i += 1;
    }
    None
}

fn op_modal_open(st: &mut GuiState, owner: i32, slot_no: usize, arg: u32) -> i32 {
    let req: reqs::GuiReqModal = unsafe { slot::read_req(st, slot_no) };
    /* 親ウィンドウ: arg で指定されていればそれ (generation + owner 検証つき)、
     * 無ければこの owner の最前面。**どちらも無ければ STALE** (W4 §2)。 */
    let parent = if arg != 0 {
        match owned_index(st, owner, arg) {
            Ok(i) => st.windows[i].id(i),
            Err(e) => return modal_open_fail(st, slot_no, e),
        }
    } else {
        match front_owned_index(st, owner) {
            Some(i) => st.windows[i].id(i),
            None => return modal_open_fail(st, slot_no, OS32_ERR_STALE),
        }
    };
    let len = req.message.len as usize;
    /* 未 consume の completed result / active modal / 種別の検査は modal::open。 */
    let r = modal::open(st, slot_no, owner, parent, req.buttons, &req.message.s, len);
    /* v1.1 の GuiRespModal (button) はそのまま。値は MODAL_RESULT で取る。 */
    let resp = reqs::GuiRespModal {
        result: if r < 0 { r } else { 0 },
        button: 0,
        _pad: 0,
    };
    slot::write_resp(st, slot_no, resp);
    r
}

/// `MODAL_OPEN` の失敗を応答ブロックにも書いてから返す。
fn modal_open_fail(st: &GuiState, slot_no: usize, e: i32) -> i32 {
    let resp = reqs::GuiRespModal { result: e, button: 0, _pad: 0 };
    slot::write_resp(st, slot_no, resp);
    e
}

/* ================================================================ */
/*  OP_MODAL_RESULT (契約 V12-M の M1 / M2、W4 §3)                   */
/*                                                                  */
/*  応答 (result / dialog / GuiString) を**全部書いてから** consume する。 */
/*  ID 不一致・二重 consume は OS32_ERR_STALE。                       */
/* ================================================================ */
fn op_modal_result(st: &mut GuiState, _owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: GuiReqModalResult = unsafe { slot::read_req(st, slot_no) };
    let mut resp = GuiRespModalResult {
        result: 0,
        dialog: 0,
        value: GuiString { len: 0, s: [0; 255] },
    };
    modal::fill_resp(slot_no, req.dialog, &mut resp);
    let ok = resp.result >= 0;
    /* (1) 応答を書き切る。 */
    slot::write_resp(st, slot_no, resp);
    if !ok {
        return OS32_ERR_STALE;
    }
    /* (2) 書き切ってから consume (以後は STALE)。 */
    modal::consume_completed(slot_no, req.dialog);
    0
}

/* ================================================================ */
/*  OP_SESSION_REQUEST (契約 V12-S の S4、W3 §5.2)                   */
/*                                                                  */
/*  X1: 検証して私有バッファへ写し、pending を立てるだけ。            */
/*  **VFS / exec_run / system.cfg 更新はここでは絶対にしない** (S8)。  */
/*  戻り値 0 は「受理」であって action の完了ではない。               */
/* ================================================================ */
fn op_session_request(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: GuiReqSession = unsafe { slot::read_req(st, slot_no) };
    let len = req.value.len as usize;
    session::request(st, owner, req.action, req.flags, &req.value.s, len)
}

/* ================================================================ */
/*  OP_INVALIDATE (契約 G4) — dirty に足すだけ                        */
/* ================================================================ */
fn op_invalidate(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqInvalidate = unsafe { slot::read_req(st, slot_no) };
    let idx = match owned_index(st, owner, req.window) {
        Ok(i) => i,
        Err(e) => return e,
    };
    let r = Rect::new(req.rect.x as i32, req.rect.y as i32, req.rect.w as i32, req.rect.h as i32);
    damage::add_dirty(&mut st.windows[idx], r);
    0
}

/* ================================================================ */
/*  OP_STATS (契約 G7) — GFX_Stats を応答ブロックへ                   */
/* ================================================================ */
fn op_stats(st: &mut GuiState, _owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let out = slot::resp_ptr(st, slot_no);
    unsafe { (os32api::api().gfx_stats)(out) }
}

/* ================================================================ */
/*  ウィンドウ (契約 U1)                                              */
/* ================================================================ */

fn op_win_create(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let spec: GuiWinSpec = unsafe { slot::read_req(st, slot_no) };
    let r = wm::create_window(st, owner, &spec);
    let resp = reqs::GuiRespWindow {
        result: if r < 0 { r } else { 0 },
        window: if r < 0 { 0 } else { r as u32 },
    };
    slot::write_resp(st, slot_no, resp);
    r
}

fn op_win_destroy(st: &mut GuiState, owner: i32, slot_no: usize, arg: u32) -> i32 {
    let id = target_window(st, slot_no, arg);
    wm::destroy_window(st, owner, id)
}

fn op_win_move(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqWinMove = unsafe { slot::read_req(st, slot_no) };
    wm::move_window(st, owner, req.window, req.x as i32, req.y as i32)
}

fn op_win_resize(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqWinResize = unsafe { slot::read_req(st, slot_no) };
    wm::resize_window(st, owner, req.window, req.w as i32, req.h as i32)
}

fn op_win_show(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqWinShow = unsafe { slot::read_req(st, slot_no) };
    wm::show_window(st, owner, req.window, req.show != 0)
}

fn op_win_set_title(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqWinTitle = unsafe { slot::read_req(st, slot_no) };
    let len = req.title.len as usize;
    wm::set_title(st, owner, req.window, &req.title.s, len)
}

fn op_win_client_rect(st: &mut GuiState, owner: i32, slot_no: usize, arg: u32) -> i32 {
    let id = target_window(st, slot_no, arg);
    match wm::client_rect(st, owner, id) {
        Ok(r) => {
            let resp = reqs::GuiRespRect { result: 0, rect: rect16(r) };
            slot::write_resp(st, slot_no, resp);
            0
        }
        Err(e) => {
            let resp =
                reqs::GuiRespRect { result: e, rect: GuiRect16 { x: 0, y: 0, w: 0, h: 0 } };
            slot::write_resp(st, slot_no, resp);
            e
        }
    }
}

fn op_win_raise(st: &mut GuiState, owner: i32, slot_no: usize, arg: u32) -> i32 {
    let id = target_window(st, slot_no, arg);
    wm::raise(st, owner, id)
}

fn op_win_set_focus(st: &mut GuiState, owner: i32, slot_no: usize, arg: u32) -> i32 {
    let id = target_window(st, slot_no, arg);
    wm::set_focus(st, owner, id)
}

fn op_win_set_text_cursor(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqTextCursor = unsafe { slot::read_req(st, slot_no) };
    wm::set_text_cursor(st, owner, req.window, req.x as i32, req.y as i32, req.visible != 0)
}

/* ================================================================ */
/*  タイマ (契約 U5)                                                  */
/* ================================================================ */

fn op_timer_set(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqTimerSet = unsafe { slot::read_req(st, slot_no) };
    if owned_index(st, owner, req.window).is_err() {
        return OS32_ERR_STALE;
    }
    let now = st.now;
    timer::set(st, owner, req.window, req.timer_id, req.interval_ticks, req.repeat != 0, now)
}

fn op_timer_kill(st: &mut GuiState, owner: i32, slot_no: usize, _arg: u32) -> i32 {
    let req: reqs::GuiReqTimerKill = unsafe { slot::read_req(st, slot_no) };
    timer::kill(st, owner, req.window, req.timer_id)
}
