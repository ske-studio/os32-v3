//! wm_tests.rs — WM / 入力 / モーダルのホスト回帰試験。
//!
//! `src/input.rs` の末尾から `#[cfg(test)] #[path]` で取り込み、
//! `host/integration.py` が gshell の実モジュールをホスト ABI の代用
//! (`host/mocks.rs`) と一緒にビルドして走らせる (`make check-gshell-host`)。
//! 中身は 2026-09-10 の独立レビュー第 2・3 回で足した挙動試験で、
//! T5b (常駐表示パネル) 撤去のときに `host/terminal_tests.rs` から移した。

/// Enter 連打で、FEP が確定した文字が Input dialog の結果から抜けないこと。
///
/// 確定 Enter と決定 Enter が**同じ吸い出し周期**に入ると、`fep::flush_text`
/// が周期末尾のままでは、確定文字が field に入る前にダイアログが閉じる。
/// 結果は空になり、残った確定文字は `Text` として背後のアプリへ流れる。
#[test]
fn double_enter_keeps_fep_commit_in_the_input_dialog_result() {
    use crate::{fep, input, mocks, modal, slot, wm};
    use os32api::gui::proto::{GuiEvent, GUI_EV_TEXT, GUI_RING_CAPACITY};
    const SC_RETURN: i32 = 0x1C;
    const DOWN: i32 = 1 << 8;

    /* リングを直接読んで、アプリへ渡った `Text` の中身を集める。 */
    fn text_events(st: &wm::GuiState, slot_i: usize) -> Vec<u8> {
        let h = slot::read_header(st, slot_i);
        let base = slot::ring_ptr(st, slot_i);
        let mut out = Vec::new();
        let mut i = h.ring_head;
        while i != h.ring_tail {
            let ev: GuiEvent = unsafe {
                core::ptr::read_unaligned(
                    base.add((i as usize % GUI_RING_CAPACITY) * 16) as *const GuiEvent,
                )
            };
            if ev.kind == GUI_EV_TEXT {
                let n = (ev.sub & 0x7F) as usize;
                let n = if n > 8 { 8 } else { n };
                out.extend_from_slice(&ev.payload[..n]);
            }
            i = i.wrapping_add(1);
        }
        out
    }

    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = wm::GuiState::NEW;
    st.shm_base = shm.base();
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    slot::init_header(&st, 0);
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.w = 600;
    w.h = 370;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;
    *mocks::MOUSE.lock().unwrap() = (0, 0, 0);

    /* 1 回目の Enter で "ab" を確定し、2 回目は FEP が素通しする。 */
    mocks::fep_script(&[0x61, 0x62, 0x00, 0x100]);
    fep::install();
    modal::open_wm_input(&mut st, b"Run\0", modal::WM_PURPOSE_FILE_LAUNCH);
    assert!(modal::is_open() && modal::is_input());

    /* 両方の Enter を**同じ周期**に積む。 */
    mocks::push_rawkeys(&[SC_RETURN | DOWN, SC_RETURN | DOWN]);
    input::capture(&mut st, input::Ctx::Wait);

    assert!(!modal::is_open(), "2 回目の Enter でダイアログが閉じていない");
    assert!(
        st.launch_pending,
        "確定文字が field に入る前に閉じた (結果が空)"
    );
    assert_eq!(
        &st.launch_path[..st.launch_path_len],
        b"ab",
        "結果から確定文字が抜けた"
    );
    assert!(
        text_events(&st, 0).is_empty(),
        "確定文字がダイアログを素通りしてアプリへ流れた: {:?}",
        text_events(&st, 0)
    );
}

/* ================================================================ */
/*  2026-09-10 レビュー第 3 回 (4 件のうち gshell 側 3 件)          */
/* ================================================================ */

/// 検査用に窓 1 枚とスロット 1 本を張った `GuiState` を作る。
fn one_window_state(shm: &crate::mocks::Shm) -> crate::wm::GuiState {
    use crate::{slot, wm};
    let mut st = wm::GuiState::NEW;
    st.shm_base = shm.base();
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    slot::init_header(&st, 0);
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.x = 40;
    w.y = 40;
    w.w = 400;
    w.h = 300;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;
    st
}

/// リング内の `Button` を (down, button, x, y) で集める。
fn button_events(st: &crate::wm::GuiState, slot_i: usize) -> Vec<(bool, u8, i16, i16)> {
    use crate::slot;
    use os32api::gui::proto::{GuiEvent, GUI_EV_BUTTON, GUI_RING_CAPACITY};
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
        if ev.kind == GUI_EV_BUTTON {
            let x = i16::from_le_bytes([ev.payload[0], ev.payload[1]]);
            let y = i16::from_le_bytes([ev.payload[2], ev.payload[3]]);
            out.push((ev.sub != 0, ev.payload[4], x, y));
        }
        i = i.wrapping_add(1);
    }
    out
}

/// アプリのクライアント内で押して**タスクバーの上で離す**と、Button-up が
/// 失われていた。ウィジェットの armed / アプリのドラッグ状態が解けず、
/// 押されたままの表示が残る。押下の配送先を捕捉して、離しは同じ相手へ返す。
#[test]
fn button_up_on_taskbar_still_reaches_the_app_that_got_the_press() {
    use crate::{input, mocks, taskbar};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_window_state(&shm);

    let (cx, cy) = (200i32, 200i32);
    assert!(
        st.windows[0].client_rect_screen().contains(cx, cy),
        "検査点がクライアント内でない"
    );
    /* 押下: クライアント内。 */
    *mocks::MOUSE.lock().unwrap() = (cx as i16, cy as i16, 1);
    input::capture(&mut st, input::Ctx::Wait);

    /* 離し: タスクバーの上。 */
    let ty = st.screen_h - 1;
    assert!(taskbar::hit(&st, 500, ty), "検査点がタスクバー上でない");
    *mocks::MOUSE.lock().unwrap() = (500, ty as i16, 0);
    input::capture(&mut st, input::Ctx::Wait);

    let evs = button_events(&st, 0);
    assert_eq!(evs.len(), 2, "Button が 2 件でない: {evs:?}");
    assert!(evs[0].0 && evs[0].1 == 1, "1 件目が左の押下でない: {evs:?}");
    assert!(
        !evs[1].0 && evs[1].1 == 1,
        "タスクバー上の離しが失われた: {evs:?}"
    );
}

/// タイトルバー / 枠の右クリックはアプリへ配らない (契約 D4 は
/// 「前面窓の**クライアント上**」)。負のクライアント座標が届くと、
/// アプリの context menu が誤作動する。
#[test]
fn right_click_on_titlebar_is_not_forwarded_but_client_still_is() {
    use crate::{input, mocks};

    /* (mx, my) で右押下 → 離し。アプリに届いた Button を返す。 */
    fn right_click_at(mx: i32, my: i32) -> Vec<(bool, u8, i16, i16)> {
        use crate::{input, mocks};
        mocks::init();
        let shm = mocks::Shm::new();
        let mut st = one_window_state(&shm);
        *mocks::MOUSE.lock().unwrap() = (mx as i16, my as i16, 2);
        input::capture(&mut st, input::Ctx::Wait);
        *mocks::MOUSE.lock().unwrap() = (mx as i16, my as i16, 0);
        input::capture(&mut st, input::Ctx::Wait);
        button_events(&st, 0)
    }

    /* タイトルバーの座標を実物から取る。 */
    let (tx, ty, cx, cy) = {
        mocks::init();
        let shm = mocks::Shm::new();
        let st = one_window_state(&shm);
        let tb = st.windows[0].titlebar_rect();
        let cr = st.windows[0].client_rect_screen();
        assert!(tb.w > 0 && tb.h > 0, "タイトルバーが無い");
        (
            tb.x + tb.w / 2,
            tb.y + tb.h / 2,
            cr.x + cr.w / 2,
            cr.y + cr.h / 2,
        )
    };
    let _ = (input::Ctx::Wait, mocks::W);

    let title = right_click_at(tx, ty);
    assert!(
        title.is_empty(),
        "タイトルバーの右クリックがアプリへ届いた: {title:?}"
    );

    /* 対照群: クライアント上の右クリックは今までどおり届く。 */
    let client = right_click_at(cx, cy);
    assert_eq!(client.len(), 2, "クライアントの右クリックが届かない: {client:?}");
    assert!(client[0].0 && client[0].1 == 2 && client[0].2 >= 0 && client[0].3 >= 0);
    assert!(!client[1].0 && client[1].1 == 2);
}

/// 48B 以上のエントリ名を、切り詰めた**別名**として返さない (契約 M1
/// 「path/text を切り詰めて別値として返してはならない」)。切り詰めた名前は
/// 実在しない path か、同じ 47B の接頭辞を持つ別ファイルを指す。
#[test]
fn file_dialog_refuses_names_that_do_not_fit_instead_of_truncating() {
    use crate::{input, mocks, modal, wm};

    /* `sys_ls` を差し替えて、この名前 1 件だけを返す。 */
    static LS_NAME: std::sync::Mutex<Vec<u8>> = std::sync::Mutex::new(Vec::new());
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
            ftype: 1, /* FILE_TYPE_FILE */
        };
        let n = LS_NAME.lock().unwrap();
        e.name[..n.len()].copy_from_slice(&n);
        let f: extern "C" fn(*const DirEntryExt, *mut u8) = core::mem::transmute(cb);
        f(&e as *const DirEntryExt, ctx);
        0
    }

    /// `name` 1 件のディレクトリで RETURN を押し、(選ばれたか, 選ばれた path)。
    fn pick(name: &[u8]) -> (bool, Vec<u8>) {
        use crate::{mocks, modal, wm};
        mocks::init();
        unsafe {
            (*os32api::api_ptr()).sys_ls = ls;
        }
        *LS_NAME.lock().unwrap() = name.to_vec();
        let shm = mocks::Shm::new();
        let mut st = one_window_state(&shm);
        modal::open_wm_file(&mut st, b"/\0");
        assert!(modal::is_open());
        modal::x3_cycle(&mut st); /* sys_ls は X3 でだけ走る */
        modal::on_key(&mut st, 0x1C, 0x0D, 0); /* RETURN */
        /* 拒否された名前ではダイアログが開いたまま残る。modal は静的なので
         * 閉じずに戻ると次の試験の入力が全部ダイアログ宛になる (T5b の試験が
         * 間に挟まっていた頃は、そちらの ESC がたまたま閉じていた)。 */
        if modal::is_open() {
            modal::on_key(&mut st, 0, 0x1b, 0);
        }
        let _ = wm::GuiState::NEW.z_count;
        (
            st.launch_pending,
            st.launch_path[..st.launch_path_len].to_vec(),
        )
    }

    /* 47B は収まるので今までどおり選べる。 */
    let short = vec![b'a'; 47];
    let (ok, path) = pick(&short);
    assert!(ok, "47B の名前が選べない");
    assert_eq!(&path[1..], &short[..], "47B の名前が変わった: {path:?}");

    /* 48B は収まらない。切り詰めた別名を返さず、選べないこと。 */
    let long = vec![b'b'; 48];
    let (ok, path) = pick(&long);
    assert!(
        !ok,
        "48B の名前を切り詰めた別名として返した: {:?}",
        core::str::from_utf8(&path)
    );

    /* 80B でも同じ。 */
    let (ok, _) = pick(&vec![b'c'; 80]);
    assert!(!ok, "80B の名前を切り詰めた別名として返した");

    let _ = (input::Ctx::Wait, modal::is_open());
}

/* ================================================================ */
/*  2026-09-10 レビュー第 4 回                                       */
/*                                                                  */
/*  前半は T5b 撤去 (1c98613) で消えた「上位 UI 上の離し」4 本の      */
/*  書き直し、後半はモーダル中に捕捉した離しが失われる [P2] の回帰。 */
/* ================================================================ */

/// アプリのクライアント内の代表点 (窓 1 枚の `one_window_state` 用)。
const APP_PT: (i32, i32) = (200, 200);

/// マウスの位置とボタンをモックへ差し込む。
fn set_mouse(x: i32, y: i32, buttons: u8) {
    *crate::mocks::MOUSE.lock().unwrap() = (x as i16, y as i16, buttons);
}

unsafe extern "C" fn ime_on() -> i32 {
    1
}
unsafe extern "C" fn ime_off() -> i32 {
    0
}

/// 上位 UI (タスクバー / メニュー / モーダル / FEP) の上で押して離しても、
/// **押していないボタンの離し**がアプリへ飛ばず、その直後のアプリ内の押下は
/// 届く。T5b 撤去で消えた 4 本の、パネルに依存しない書き直し。
///
/// 元の 4 本は「常駐パネルが最初の押下を食って `prev_buttons` を立てる」
/// 仕掛けだった。ここでは押下を**上位 UI 自身**に取らせる。ただし FEP は
/// マウスの領分を持たない (`input.rs` は FEP へマウスを一切渡さない) ので、
/// FEP の回だけは元の試験と同じく「押下はタスクバーが取り、離しを FEP 矩形の
/// 上で行う」形にした — 元の被覆 (FEP 矩形の上の離しが漏れない) はそちら。
fn upper_ui_press_release_then_app_press(kind: &str) {
    use crate::{fep, input, mocks, modal, startmenu, taskbar};
    use os32api::gui::proto::GUI_MODAL_OK;

    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_window_state(&shm);
    let (ax, ay) = APP_PT;
    assert!(
        st.windows[0].client_rect_screen().contains(ax, ay),
        "検査点がクライアント内でない"
    );

    /* ---- 上位 UI を出し、押下と離しの位置を決める ---- */
    /* Start ボタンでも窓ボタンでもない帯の上 (押しても何も起きない場所)。
     * Start の上だと押下でメニューが開いてしまい、直後のアプリ押下が
     * 「メニューを閉じる」に化けて試験の主題がぼける。 */
    let taskbar_pt = (500, st.screen_h - 1);
    assert!(
        taskbar::hit(&st, taskbar_pt.0, taskbar_pt.1),
        "検査点がタスクバー上でない"
    );
    assert!(
        !startmenu::is_open(),
        "前の試験がメニューを開いたまま残した"
    );
    let (press_pt, release_pt) = match kind {
        "taskbar" => (taskbar_pt, taskbar_pt),
        "menu" => {
            /* 開いたメニューの外側 = アプリのクライアント上を押すと、
             * メニューが閉じて対の離しは捨てられる (swallow_up)。 */
            startmenu::open_context(&mut st, 60, 60);
            assert!(startmenu::is_open());
            ((ax, ay), (ax, ay))
        }
        "modal" => {
            modal::open_wm_message(&mut st, GUI_MODAL_OK, b"Modal\0", modal::WM_PURPOSE_NOTIFY);
            assert!(modal::is_open());
            let r = modal::rect();
            ((r.x + 2, r.y + 2), (r.x + 2, r.y + 2))
        }
        "fep" => {
            st.windows[0].tc_visible = true;
            st.windows[0].tc_x = 60;
            st.windows[0].tc_y = 180;
            unsafe {
                (*os32api::api_ptr()).ime_is_active = ime_on;
            }
            fep::install();
            fep::pre_cycle(&mut st);
            fep::post_cycle(&mut st);
            let r = fep::rect();
            assert!(!r.is_empty(), "FEP の矩形が出ていない");
            (taskbar_pt, (r.x, r.y))
        }
        _ => unreachable!(),
    };

    /* ---- 上位 UI の上で押して離す: アプリへは 1 件も出ない ---- */
    set_mouse(press_pt.0, press_pt.1, 1);
    input::capture(&mut st, input::Ctx::Wait);
    assert!(
        button_events(&st, 0).is_empty(),
        "上位 UI ({kind}) の押下がアプリへ漏れた: {:?}",
        button_events(&st, 0)
    );
    set_mouse(release_pt.0, release_pt.1, 0);
    input::capture(&mut st, input::Ctx::Wait);
    assert!(
        button_events(&st, 0).is_empty(),
        "上位 UI ({kind}) の離しがアプリへ漏れた: {:?}",
        button_events(&st, 0)
    );

    /* ---- 上位 UI を片付ける (静的な状態を次の試験へ持ち越さない) ---- */
    match kind {
        "menu" => {
            assert!(!startmenu::is_open(), "外側の押下でメニューが閉じていない");
        }
        "modal" => {
            assert!(modal::is_open(), "ボタン以外の押下でダイアログが閉じた");
            modal::on_key(&mut st, 0, 0x1b, 0);
            assert!(!modal::is_open());
        }
        "fep" => {
            unsafe {
                (*os32api::api_ptr()).ime_is_active = ime_off;
            }
            fep::install();
            fep::pre_cycle(&mut st);
            fep::post_cycle(&mut st);
        }
        _ => {}
    }

    /* ---- 直後のアプリ内の押下は届く (間に無操作の周を挟まない) ---- */
    set_mouse(ax, ay, 1);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(
        evs.len(),
        1,
        "上位 UI ({kind}) の離しの直後、アプリの押下が飲まれた: {evs:?}"
    );
    assert!(evs[0].0 && evs[0].1 == 1, "押下でない: {evs:?}");

    /* 捕捉を残さない (CAPTURE は静的)。 */
    set_mouse(ax, ay, 0);
    input::capture(&mut st, input::Ctx::Wait);
}

#[test]
fn taskbar_press_release_then_app_press_is_delivered() {
    upper_ui_press_release_then_app_press("taskbar");
}

#[test]
fn menu_press_release_then_app_press_is_delivered() {
    upper_ui_press_release_then_app_press("menu");
}

#[test]
fn modal_press_release_then_app_press_is_delivered() {
    upper_ui_press_release_then_app_press("modal");
}

#[test]
fn fep_press_release_then_app_press_is_delivered() {
    upper_ui_press_release_then_app_press("fep");
}

/// 逆向きの被覆: **アプリが受けた押下**と対になる離しを上位 UI の上で行っても、
/// 離しはその押下を受けたアプリへ届き、その直後のアプリ内の押下も飲まれない。
///
/// 上の 4 本は「上位 UI 自身が押下を取る」側 (押していないボタンの離しを
/// アプリへ作らない)。こちらは捕捉 (`input.rs` の `CAPTURE`) の側で、契約 D1 の
/// 「タスクバー領域の入力をアプリへ配送しない」を**対になる離しには掛けない**
/// という規則を 4 経路すべてで確かめる。T5b 撤去まで残っていたのは
/// `button_up_on_taskbar_still_reaches_the_app_that_got_the_press` の
/// タスクバー経路だけで、ここでメニュー / モーダル / FEP へ広げ、
/// 「次の押下を飲み込まない」も 4 経路で見る。
fn app_press_then_release_over_upper_ui(kind: &str) {
    use crate::{fep, input, mocks, modal, startmenu, taskbar};
    use os32api::gui::proto::GUI_MODAL_OK;

    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_window_state(&shm);
    let (ax, ay) = APP_PT;
    assert!(
        st.windows[0].client_rect_screen().contains(ax, ay),
        "検査点がクライアント内でない"
    );
    assert!(
        !startmenu::is_open() && !modal::is_open(),
        "前の試験が上位 UI を開いたまま残した"
    );

    /* ---- 押下はアプリのクライアント内 = アプリの領分 (捕捉が立つ) ---- */
    set_mouse(ax, ay, 1);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(evs.len(), 1, "アプリの押下が届いていない: {evs:?}");
    assert!(evs[0].0 && evs[0].1 == 1, "1 件目が左の押下でない: {evs:?}");

    /* ---- 押したまま上位 UI が前に出る。離しはその上で行う ---- */
    let release_pt = match kind {
        "taskbar" => {
            /* Start でも窓ボタンでもない帯 (`upper_ui_press_release_then_app_press`
             * と同じ理由でここを選ぶ)。 */
            let p = (500, st.screen_h - 1);
            assert!(taskbar::hit(&st, p.0, p.1), "検査点がタスクバー上でない");
            p
        }
        "menu" => {
            startmenu::open_context(&mut st, 60, 60);
            let r = startmenu::rect();
            assert!(!r.is_empty(), "メニューが開いていない");
            (r.x + r.w / 2, r.y + r.h / 2)
        }
        "modal" => {
            /* アプリが押されたまま自分でダイアログを開く (OP_MODAL_OPEN)。 */
            modal::open_wm_message(&mut st, GUI_MODAL_OK, b"Modal\0", modal::WM_PURPOSE_NOTIFY);
            assert!(modal::is_open());
            let r = modal::rect();
            (r.x + 2, r.y + 2)
        }
        "fep" => {
            st.windows[0].tc_visible = true;
            st.windows[0].tc_x = 60;
            st.windows[0].tc_y = 180;
            unsafe {
                (*os32api::api_ptr()).ime_is_active = ime_on;
            }
            fep::install();
            fep::pre_cycle(&mut st);
            fep::post_cycle(&mut st);
            let r = fep::rect();
            assert!(!r.is_empty(), "FEP の矩形が出ていない");
            (r.x, r.y)
        }
        _ => unreachable!(),
    };

    set_mouse(release_pt.0, release_pt.1, 0);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(
        evs.len(),
        2,
        "上位 UI ({kind}) の上の離しが、押下を受けたアプリへ届かない: {evs:?}"
    );
    assert!(!evs[1].0 && evs[1].1 == 1, "2 件目が対の離しでない: {evs:?}");

    /* ---- 上位 UI を片付ける (静的な状態を次の試験へ持ち越さない) ---- */
    match kind {
        "menu" => {
            assert!(startmenu::is_open(), "対の離しでメニューが閉じた");
            startmenu::close(&mut st);
        }
        "modal" => {
            assert!(modal::is_open(), "対の離しでダイアログが閉じた");
            modal::on_key(&mut st, 0, 0x1b, 0);
            assert!(!modal::is_open());
        }
        "fep" => {
            unsafe {
                (*os32api::api_ptr()).ime_is_active = ime_off;
            }
            fep::install();
            fep::pre_cycle(&mut st);
            fep::post_cycle(&mut st);
        }
        _ => {}
    }

    /* ---- 直後のアプリ内の押下は届く (間に無操作の周を挟まない) ---- */
    set_mouse(ax, ay, 1);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(
        evs.len(),
        3,
        "上位 UI ({kind}) の上の離しの直後、アプリの押下が飲まれた: {evs:?}"
    );
    assert!(evs[2].0 && evs[2].1 == 1, "3 件目が押下でない: {evs:?}");

    /* 捕捉を残さない (CAPTURE は静的)。 */
    set_mouse(ax, ay, 0);
    input::capture(&mut st, input::Ctx::Wait);
}

#[test]
fn app_release_over_taskbar_reaches_the_app_and_the_next_press_lands() {
    app_press_then_release_over_upper_ui("taskbar");
}

#[test]
fn app_release_over_menu_reaches_the_app_and_the_next_press_lands() {
    app_press_then_release_over_upper_ui("menu");
}

#[test]
fn app_release_over_modal_reaches_the_app_and_the_next_press_lands() {
    app_press_then_release_over_upper_ui("modal");
}

#[test]
fn app_release_over_fep_reaches_the_app_and_the_next_press_lands() {
    app_press_then_release_over_upper_ui("fep");
}

/// アプリ内で押したまま、アプリ自身がダイアログを開き (OP_MODAL_OPEN)、
/// それから離す。契約 U4「モーダル中は宛先をダイアログに限定」は**新しい
/// 入力**の規則で、モーダル前の押下と対になる離しはその押下を受けたアプリの
/// もの。捕捉経由で返さないと、アプリのウィジェットは armed のまま残り、
/// `prev_buttons` だけ進むので up_edge は二度と立たない。
fn captured_release_while_modal(button: u8) {
    use crate::{input, mocks, modal};
    use os32api::gui::proto::GUI_MODAL_OK;

    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_window_state(&shm);
    let (ax, ay) = APP_PT;

    /* 押下はアプリのクライアント内 = アプリの領分。 */
    set_mouse(ax, ay, button);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(evs.len(), 1, "押下が届いていない: {evs:?}");
    assert!(evs[0].0 && evs[0].1 == button, "押下が違う: {evs:?}");

    /* 押したままアプリがダイアログを開く。 */
    modal::open_wm_message(&mut st, GUI_MODAL_OK, b"Modal\0", modal::WM_PURPOSE_NOTIFY);
    assert!(modal::is_open());

    /* ダイアログの上で離す。 */
    let r = modal::rect();
    set_mouse(r.x + 2, r.y + 2, 0);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(
        evs.len(),
        2,
        "モーダル中に捕捉した離しが失われた (button={button}): {evs:?}"
    );
    assert!(
        !evs[1].0 && evs[1].1 == button,
        "2 件目が対の離しでない: {evs:?}"
    );

    /* 閉じたあと、余分な Button が後から湧かない。 */
    assert!(modal::is_open(), "離しでダイアログが閉じた");
    modal::on_key(&mut st, 0, 0x1b, 0);
    assert!(!modal::is_open());
    set_mouse(ax, ay, 0);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(evs.len(), 2, "閉じたあとに Button が増えた: {evs:?}");
}

#[test]
fn captured_release_is_delivered_while_modal_is_open() {
    captured_release_while_modal(1);
}

#[test]
fn captured_right_release_is_delivered_while_modal_is_open() {
    captured_release_while_modal(2);
}

/// X4 (ポンプ) がモーダル中に離しを先に見ても、`prev_buttons` を進めないので
/// 次の X3 が同じエッジを拾い直し、離しは 1 件だけ届く (二重配送も欠落も無し)。
#[test]
fn captured_release_survives_the_pump_while_modal_is_open() {
    use crate::{input, mocks, modal};
    use os32api::gui::proto::GUI_MODAL_OK;

    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_window_state(&shm);
    let (ax, ay) = APP_PT;

    set_mouse(ax, ay, 1);
    input::capture(&mut st, input::Ctx::Wait);
    assert_eq!(button_events(&st, 0).len(), 1);

    modal::open_wm_message(&mut st, GUI_MODAL_OK, b"Modal\0", modal::WM_PURPOSE_NOTIFY);
    assert!(modal::is_open());

    /* ポンプが先に離しを見る: ここでは何も配らず prev_buttons も進めない。 */
    let r = modal::rect();
    set_mouse(r.x + 2, r.y + 2, 0);
    input::capture(&mut st, input::Ctx::Pump);
    assert_eq!(
        button_events(&st, 0).len(),
        1,
        "ポンプが離しを配った (X3 と二重になる)"
    );

    /* 次の X3 が同じエッジを拾い直して、捕捉した相手へ返す。 */
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(evs.len(), 2, "X4 の後で離しが失われた: {evs:?}");
    assert!(!evs[1].0 && evs[1].1 == 1, "2 件目が離しでない: {evs:?}");

    modal::on_key(&mut st, 0, 0x1b, 0);
    assert!(!modal::is_open());
}

/// 負例: モーダル中に**新しく**始まった押下と離しは、契約 U4 のとおり
/// ダイアログだけのもの。アプリへは 1 件も配らない。
#[test]
fn new_press_during_modal_is_not_forwarded_to_the_app() {
    use crate::{input, mocks, modal};
    use os32api::gui::proto::GUI_MODAL_OK;

    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_window_state(&shm);
    let (ax, ay) = APP_PT;

    /* 先に押下と離しを 1 組済ませ、捕捉が空であることを確かめてから開く。 */
    set_mouse(ax, ay, 1);
    input::capture(&mut st, input::Ctx::Wait);
    set_mouse(ax, ay, 0);
    input::capture(&mut st, input::Ctx::Wait);
    let base = button_events(&st, 0).len();
    assert_eq!(base, 2, "前提の押下・離しが 2 件でない");

    modal::open_wm_message(&mut st, GUI_MODAL_OK, b"Modal\0", modal::WM_PURPOSE_NOTIFY);
    assert!(modal::is_open());

    let r = modal::rect();
    set_mouse(r.x + 2, r.y + 2, 1);
    input::capture(&mut st, input::Ctx::Wait);
    set_mouse(r.x + 2, r.y + 2, 0);
    input::capture(&mut st, input::Ctx::Wait);
    let evs = button_events(&st, 0);
    assert_eq!(
        evs.len(),
        base,
        "モーダル中の新しい押下・離しがアプリへ漏れた: {evs:?}"
    );

    modal::on_key(&mut st, 0, 0x1b, 0);
    assert!(!modal::is_open());
}

/* ================================================================ */
/*  K5b-W — アプリ 4 本の同時実行 (票 TASK_K5B_gshell.md)            */
/*                                                                  */
/*  規則の正典は K5a 設計 D11-3 / D11-3a、模型は                     */
/*  `tools/tests/multiapp_model_host.c` のケース 12〜16 (84 検査)。   */
/*  ここは**同じ性質**を、模型の `MaState` ではなく実物の             */
/*  `GuiState` + `multiapp` の状態に対して検査する。                  */
/*                                                                  */
/*  `input_ready` / `derived_ready` は模型では試験が直接立てていたが、 */
/*  実物は WM 状態から算出するので、試験は「リングにイベントを積む」  */
/*  「`configure_pending` を立てる」という**実物の材料**で作る。      */
/* ================================================================ */

/// アプリ 4 本 (owner 2〜5) がスロット 0〜3 と窓を 1 枚ずつ持つ状態。
/// Z 順は 0,1,2,3 なので最前面 = 窓 3 = owner 5 (`front_owner()`)。
fn four_app_state(shm: &crate::mocks::Shm) -> crate::wm::GuiState {
    use crate::{slot, wm};
    let mut st = wm::GuiState::NEW;
    st.shm_base = shm.base();
    let mut k = 0usize;
    while k < 4 {
        let owner = 2 + k as i32;
        st.slots[k].used = true;
        st.slots[k].owner = owner;
        slot::init_header(&st, k);
        let mut w = wm::Win::EMPTY;
        w.used = true;
        w.visible = true;
        w.owner = owner;
        w.gen = 1;
        w.x = 10 + 140 * k as i32;
        w.y = 10;
        w.w = 100;
        w.h = 80;
        st.windows[k] = w;
        st.zorder[k] = k;
        k += 1;
    }
    st.z_count = 4;
    st
}

/// GUI アプリ 1 本 (owner 2、スロット 0 と窓 1 枚) だけの状態。票 K7 の
/// 「端末から起動した CUI アプリ」は `OP_INIT` を通らないのでスロットも窓も
/// 持たない — 表 (`multiapp`) にだけ載る本を作るための土台。
fn one_gui_app_state(shm: &crate::mocks::Shm) -> crate::wm::GuiState {
    use crate::{slot, wm};
    let mut st = wm::GuiState::NEW;
    st.shm_base = shm.base();
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    slot::init_header(&st, 0);
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.x = 10;
    w.y = 10;
    w.w = 100;
    w.h = 80;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;
    st
}

/// `multiapp` の表に 4 本を載せる (`exec_start` が 4 回成功した後と同じ形)。
fn seed_four_apps() {
    use crate::multiapp;
    let mut id = 2;
    while id <= 5 {
        multiapp::on_start(id);
        id += 1;
    }
}

/// 入力群 (未読の待ち行列型) を作る / 消す。実物のリングを使う。
fn set_input_ready(st: &mut crate::wm::GuiState, id: i32, on: bool) {
    use crate::{ring, slot};
    use os32api::gui::proto::GUI_EV_CLOSE;
    let s = st.slot_of_owner(id).expect("スロットが無い");
    if on {
        if crate::multiapp::input_ready(st, id) {
            return;
        }
        let ev = ring::ev_simple(GUI_EV_CLOSE, 0, 0);
        assert!(ring::append(st, s, &ev), "リングへ積めない");
    } else {
        let mut h = slot::read_header(st, s);
        h.ring_head = h.ring_tail;
        slot::write_header(st, s, &h);
    }
}

/// 導出群 (`Configure` 未通知) を作る / 消す。
fn set_derived_ready(st: &mut crate::wm::GuiState, id: i32, on: bool) {
    let i = (id - 2) as usize;
    st.windows[i].configure_pending = on;
}

/// フォーカス (= 最前面の可視窓の owner) をこの ID にする。
fn focus_app(st: &mut crate::wm::GuiState, id: i32) {
    st.bring_to_front((id - 2) as usize);
    assert_eq!(st.front_owner(), id, "フォーカスが動いていない");
}

/// 「park して次の 1 本を起こす」1 回ぶん。**判断は実物**
/// (`should_park` / `pick` / `mark_resumed`) で、ここが模すのは
/// カーネルの制御の流れ (longjmp と `ring3_resume`) だけ。
/// 戻り値は新しく走り出した ID (park しなかったら `cur` のまま、
/// 起こす相手が居なければ 0 = WM top-level)。
fn sched_step(st: &mut crate::wm::GuiState, cur: i32) -> i32 {
    use crate::multiapp;
    if !multiapp::should_park(st, cur) {
        return cur;
    }
    multiapp::note_parked(cur, None);
    let k = multiapp::pick(st);
    if k == 0 {
        return 0;
    }
    multiapp::mark_resumed(k);
    k
}

/* ---- ケース 12 相当: 同時 ready の選択規則 (D11-3 の (2)) ---- */
#[test]
fn pick_follows_the_frozen_rule_focus_then_input_then_derived() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    seed_four_apps();

    /* 12a フォーカス窓の owner が入力群に居れば、それを最優先。 */
    set_input_ready(&mut st, 3, true);
    set_input_ready(&mut st, 5, true);
    focus_app(&mut st, 5);
    multiapp::set_last_run(2);
    assert_eq!(multiapp::pick(&st), 5, "12a 入力群のフォーカスを選ばない");

    /* 12b フォーカスが入力群に居なければ last_run+1 から ID 昇順に巡回。 */
    focus_app(&mut st, 2); /* 2 は ready でない */
    multiapp::set_last_run(3); /* 巡回は 4 → 5 → 2 → 3 */
    assert_eq!(multiapp::pick(&st), 5, "12b 入力群のラウンドロビンが違う");

    /* 12c 入力群が空なら導出群を同じ巡回で。 */
    set_input_ready(&mut st, 3, false);
    set_input_ready(&mut st, 5, false);
    set_derived_ready(&mut st, 2, true);
    set_derived_ready(&mut st, 4, true);
    focus_app(&mut st, 2);
    multiapp::set_last_run(2); /* 巡回は 3 → 4 → 5 → 2 */
    assert_eq!(multiapp::pick(&st), 4, "12c 導出群のラウンドロビンが違う");

    /* 12d 導出群ではフォーカスを優先しない (優先していれば 4 になる)。 */
    focus_app(&mut st, 4);
    multiapp::set_last_run(4); /* 巡回は 5 → 2 → 3 → 4 */
    assert_eq!(multiapp::pick(&st), 2, "12d 導出群でフォーカスを優先した");

    /* 12e 入力は導出より必ず先。 */
    set_input_ready(&mut st, 5, true);
    focus_app(&mut st, 2);
    multiapp::set_last_run(4);
    assert_eq!(multiapp::pick(&st), 5, "12e 入力群が導出群より後になった");

    /* 12f 誰も ready でなければ起こさない。 */
    set_derived_ready(&mut st, 2, false);
    set_derived_ready(&mut st, 4, false);
    set_input_ready(&mut st, 5, false);
    assert_eq!(multiapp::pick(&st), 0, "12f ready ゼロで誰かを起こした");
}

/* ---- ケース 13 相当: 走っているアプリが譲るか (D11-3 の (1)) ---- */
#[test]
fn should_park_matches_the_five_frozen_branches() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    seed_four_apps();
    multiapp::mark_resumed(2); /* ID 2 が走っている */

    /* 13a 自分に入力があれば park しない (打鍵の連続を取りこぼさない)。 */
    set_input_ready(&mut st, 2, true);
    set_input_ready(&mut st, 4, true);
    assert!(!multiapp::should_park(&st, 2), "13a 自分の入力で譲った");
    assert_eq!(multiapp::input_streak(), 1, "13a 据え置きが数えられていない");

    /* 13b 他に ready が居なければ park しない (1 本のときの回帰ゼロ)。 */
    set_input_ready(&mut st, 2, false);
    set_derived_ready(&mut st, 2, true);
    set_input_ready(&mut st, 4, false);
    assert!(!multiapp::should_park(&st, 2), "13b 相手が居ないのに譲った");

    /* 13c 他に入力があれば、導出だけの自分は譲る。 */
    set_input_ready(&mut st, 4, true);
    assert!(multiapp::should_park(&st, 2), "13c 他の入力に譲らない");

    /* 13d 他も導出だけなら巡回のために譲る。 */
    set_input_ready(&mut st, 4, false);
    set_derived_ready(&mut st, 4, true);
    assert!(multiapp::should_park(&st, 2), "13d 導出どうしで譲らない");

    /* 13e 自分が ready でなければ譲る。 */
    set_derived_ready(&mut st, 2, false);
    assert!(multiapp::should_park(&st, 2), "13e ready でないのに譲らない");

    /* 13f (実物だけの分岐) WM が握っていない ID は park できない。 */
    assert!(!multiapp::should_park(&st, 1), "13f シェル帯を park しようとした");
    multiapp::on_owner_exit(3);
    assert!(!multiapp::should_park(&st, 3), "13f 未追跡の ID を park しようとした");
}

/* ---- ケース 14 相当: 導出群だけの 4 本が 1 周で全員走る ---- */
#[test]
fn derived_only_apps_each_get_exactly_one_turn_per_round() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    seed_four_apps();
    let mut id = 2;
    while id <= 5 {
        set_derived_ready(&mut st, id, true);
        id += 1;
    }
    focus_app(&mut st, 2); /* フォーカス固定。飢餓の原因にならないこと */
    multiapp::set_last_run(0);

    let mut order = [0i32; 4];
    let mut k = 0;
    while k < 4 {
        let picked = multiapp::pick(&st);
        assert!(picked >= 2 && picked <= 5, "14a {k} 周目に 1 本選べない");
        order[k] = picked;
        multiapp::mark_resumed(picked);
        multiapp::note_parked(picked, None);
        k += 1;
    }
    assert_eq!(order, [2, 3, 4, 5], "14b 巡回が ID 昇順でない: {order:?}");
    let mut seen = [0u8; 4];
    for o in order.iter() {
        seen[(*o - 2) as usize] += 1;
    }
    assert_eq!(seen, [1, 1, 1, 1], "14c 4 周で全員 1 回ずつにならない");
    assert_eq!(multiapp::pick(&st), 2, "14d 1 周したら先頭へ戻らない");
}

/* ---- ケース 15 相当: 自作入力で park を回避できない (反例 1) ----
 *  レビュアーの反例。アプリが自分の窓 2 枚へ交互に `set_focus()` すると
 *  `emit_focus_change` が旧窓と新窓の**両方の owner** へ `Focus` を流すので、
 *  そのアプリ自身に待ち行列型が湧き続ける (人間の入力は要らない)。
 *  「入力群は有限」では飢餓を止められず、止めるのは `INPUT_STREAK_MAX`。 */
#[test]
fn an_app_feeding_itself_focus_events_cannot_starve_another() {
    use crate::{mocks, multiapp, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    /* N = 2 (ID 2 と 3 だけ)。窓とスロットも 2 本に絞る。 */
    st.windows[2] = wm::Win::EMPTY;
    st.windows[3] = wm::Win::EMPTY;
    st.z_count = 2;
    st.slots[2] = wm::Slot::EMPTY;
    st.slots[3] = wm::Slot::EMPTY;
    multiapp::on_start(2);
    multiapp::on_start(3);

    /* A (=2) は自分で湧かせた入力を持ち続ける。B (=3) は Paint 待ち。 */
    set_input_ready(&mut st, 2, true);
    set_derived_ready(&mut st, 3, true);
    focus_app(&mut st, 2); /* A がフォーカスを握ったまま */
    multiapp::mark_resumed(2);

    let mut cur = 2;
    let mut b_at = 0u32;
    let mut parks = 0u32;
    let mut i = 0u32;
    while i < 200 {
        let before = cur;
        cur = sched_step(&mut st, cur);
        assert_ne!(cur, 0, "15a 譲ったのに起こす相手が居ない");
        if cur != before {
            parks += 1;
        }
        if cur == 3 && b_at == 0 {
            b_at = i + 1;
        }
        i += 1;
    }
    assert!(parks > 0, "15a 自作入力を続けると park が 1 度も起きない");
    assert!(b_at > 0, "15b Paint 待ちの B が走らない (飢餓)");
    /* N=2 の上限 = (2N-2)x(STREAK+1) = 10 に**ちょうど**届く (式がタイト)。 */
    assert_eq!(b_at, 10, "15c N=2 の上限 (10) と実測がずれた");
    assert!(b_at <= multiapp::STARVE_BOUND, "15d 定数の上限を超えた");
}

/* ---- ケース 16 相当: ラウンドをまたぐ待ちの上限 (反例 2) ---- */
fn run_cross_round(focus_id: i32) -> u32 {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    seed_four_apps();
    let mut id = 2;
    while id <= 5 {
        set_input_ready(&mut st, id, true);
        id += 1;
    }
    focus_app(&mut st, 2);
    let k = multiapp::pick(&st);
    assert_eq!(k, 2, "16 前提: ラウンド先頭で A が選ばれる");
    multiapp::mark_resumed(2);
    /* A は入力を消費して Paint だけ残す = 以後ずっと導出群。
     * B/C/D は入力 ready を維持する (消費しない)。 */
    set_input_ready(&mut st, 2, false);
    set_derived_ready(&mut st, 2, true);
    focus_app(&mut st, focus_id);

    let mut cur = 2;
    let mut n = 0u32;
    while n < 200 {
        /* n = 0 が「A が park する OP_WAIT」= 起算点 (D11-3a の前提 1)。 */
        cur = sched_step(&mut st, cur);
        assert_ne!(cur, 0, "16 譲ったのに起こす相手が居ない");
        if cur == 2 {
            return n;
        }
        n += 1;
    }
    0
}

#[test]
fn a_derived_only_app_runs_within_the_re_derived_bound_across_rounds() {
    use crate::multiapp;
    let a2 = run_cross_round(2);
    let a3 = run_cross_round(3);
    let a5 = run_cross_round(5);
    assert!(a2 > 0, "16a 導出群の A が走らない (無限待ち)");
    assert!(a2 <= multiapp::STARVE_BOUND, "16b A が上限 (30) を超えた: {a2}");
    assert_eq!(
        a2,
        multiapp::STARVE_BOUND,
        "16c 上限が緩い (この構成はちょうど 30 に届くはず)"
    );
    assert!(a3 > 0 && a3 <= multiapp::STARVE_BOUND, "16d フォーカス B で超過: {a3}");
    assert!(a5 > 0 && a5 <= multiapp::STARVE_BOUND, "16e フォーカス D で超過: {a5}");
}

/* ---- 票の追加検査 1: 5 本目は ERR_FULL (契約 T2a、受入 G3) ---- */
#[test]
fn the_fifth_app_gets_err_full_from_op_init_and_the_four_survive() {
    use crate::{handler, mocks, wm};
    use os32api::gui::proto::{GUI_OP_INIT, GUI_SLOT_MAX, OS32_ERR_FULL};
    mocks::init();
    let shm = mocks::Shm::new();
    {
        let st = wm::g();
        *st = wm::GuiState::NEW;
        st.shm_base = shm.base();
        st.inited = true;
    }

    let mut k = 0;
    while k < GUI_SLOT_MAX {
        let owner = 2 + k as i32;
        assert_eq!(
            handler::gshell_gui_handler(GUI_OP_INIT, 0, owner),
            k as i32,
            "{owner} にスロット {k} が配られない"
        );
        k += 1;
    }
    assert_eq!(
        handler::gshell_gui_handler(GUI_OP_INIT, 0, 6),
        OS32_ERR_FULL,
        "5 本目が ERR_FULL でない"
    );
    /* 既存 4 本は無事 (T2a「5 本目は起動できない」= 4 本は動き続ける)。 */
    let st = wm::g();
    let mut k = 0;
    while k < GUI_SLOT_MAX {
        assert!(st.slots[k].used, "5 本目の拒否で既存スロットが壊れた");
        assert_eq!(st.slots[k].owner, 2 + k as i32);
        k += 1;
    }
    st.inited = false;
}

/* ---- 票の追加検査 2: 終了で 1 本分だけ回収 (契約 T4 / U8、受入 G2) ---- */
#[test]
fn owner_exit_reclaims_exactly_one_app_worth_of_state() {
    use crate::{handler, mocks, multiapp, timer, wm};
    use os32api::gui::proto::GUI_OP_OWNER_EXIT;
    mocks::init();
    let shm = mocks::Shm::new();
    {
        let g = wm::g();
        *g = four_app_state(&shm);
        g.inited = true;
    }
    seed_four_apps();
    /* 4 本ともタイマを 1 本ずつ持つ (U5)。 */
    let mut id = 2;
    while id <= 5 {
        let g = wm::g();
        let wi = (id - 2) as usize;
        let win = g.windows[wi].id(wi);
        assert_eq!(timer::set(g, id, win, 1, 5, true, 0), 0, "タイマが張れない");
        id += 1;
    }

    /* ID 3 だけ畳む。 */
    assert_eq!(handler::gshell_gui_handler(GUI_OP_OWNER_EXIT, 0, 3), 0);

    let g = wm::g();
    assert!(!multiapp::is_tracked(3), "畳んだ ID が表に残っている");
    assert_eq!(multiapp::live_count(), 3, "回収が 1 本分でない");
    assert!(g.slot_of_owner(3).is_none(), "ID 3 のスロットが残った");
    assert!(!g.windows[1].used, "ID 3 の窓が残った");
    assert!(!timer::has_expired(g, 3, 1000), "ID 3 のタイマが残った");
    /* 他の 3 本は 1 バイトも触られない。 */
    let mut id = 2;
    while id <= 5 {
        if id != 3 {
            assert!(multiapp::is_tracked(id), "ID {id} が巻き添えで消えた");
            assert!(g.slot_of_owner(id).is_some(), "ID {id} のスロットが消えた");
            assert!(g.windows[(id - 2) as usize].used, "ID {id} の窓が消えた");
            assert!(timer::has_expired(g, id, 1000), "ID {id} のタイマが消えた");
        }
        id += 1;
    }
    g.inited = false;
}

/* ---- 票の追加検査 3: 切替は `op_wait` の中でだけ ----
 *  `exec_park` を呼ぶ点も、そこへ入る `maybe_park` を呼ぶ点も 1 か所。
 *  「呼ばれない経路」は実行時に観測できないので、gshell の全ソースを
 *  走査して数える (後から別の場所へ足したら落ちる)。 */
#[test]
fn exec_park_has_exactly_one_call_site_and_it_is_the_op_wait_loop_head() {
    let src_dir = std::path::Path::new(file!())
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("src");
    let mut park_calls = 0;
    let mut maybe_park_calls = 0;
    let mut resume_calls = 0;
    for e in std::fs::read_dir(&src_dir).expect("src が読めない") {
        let p = e.unwrap().path();
        if p.extension().and_then(|s| s.to_str()) != Some("rs") {
            continue;
        }
        let text = std::fs::read_to_string(&p).unwrap();
        for line in text.lines() {
            /* コメント行は数えない。 */
            let t = line.trim_start();
            if t.starts_with("//") || t.starts_with('*') || t.starts_with("/*") {
                continue;
            }
            if line.contains(".exec_park)(") {
                park_calls += 1;
            }
            if line.contains(".exec_resume)(") {
                resume_calls += 1;
            }
            if line.contains("multiapp::maybe_park(") {
                maybe_park_calls += 1;
                assert_eq!(
                    p.file_name().unwrap(),
                    "handler.rs",
                    "maybe_park が handler.rs の外から呼ばれている: {p:?}"
                );
            }
        }
    }
    assert_eq!(park_calls, 1, "exec_park の呼び出し点が 1 つでない");
    assert_eq!(resume_calls, 1, "exec_resume の呼び出し点が 1 つでない");
    assert_eq!(maybe_park_calls, 1, "maybe_park の呼び出し点が 1 つでない");

    /* その 1 か所が `op_wait` の中で、`wm_cycle` より前 (= ループ先頭) にある。 */
    let handler = std::fs::read_to_string(src_dir.join("handler.rs")).unwrap();
    let body = handler
        .split("fn op_wait(")
        .nth(1)
        .expect("op_wait が見つからない");
    let body = body.split("\nfn ").next().unwrap();
    let park_at = body.find("multiapp::maybe_park(").expect("op_wait の中に無い");
    let cycle_at = body.find("wm::wm_cycle(").expect("op_wait に wm_cycle が無い");
    assert!(
        park_at < cycle_at,
        "maybe_park がループ先頭 (wm_cycle の前) に無い"
    );
}

/* ---- 票の追加検査 4: 1 本のときは `exec_park` が 0 回 (回帰ゼロ) ---- */
/// `get_tick` を 1 呼び出しごとに進める (`op_wait` の期限を切るため)。
static TICKS: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
unsafe extern "C" fn ticking() -> u32 {
    TICKS.fetch_add(1, std::sync::atomic::Ordering::SeqCst)
}

/// `op_wait` を timeout つきで 1 回回す (期限で必ず戻る)。
fn drive_op_wait(owner: i32, timeout: u32) -> i32 {
    use crate::handler;
    use os32api::gui::proto::GUI_OP_WAIT;
    TICKS.store(0, std::sync::atomic::Ordering::SeqCst);
    unsafe {
        (*os32api::api_ptr()).get_tick = ticking;
    }
    handler::gshell_gui_handler(GUI_OP_WAIT, timeout, owner)
}

#[test]
fn a_single_app_never_parks_and_keeps_the_old_wm_cycle_halt_loop() {
    use crate::{mocks, multiapp, wm};
    use std::sync::atomic::Ordering;
    mocks::init();
    let shm = mocks::Shm::new();
    {
        let g = wm::g();
        *g = four_app_state(&shm);
        /* アプリは 1 本だけ (ID 2)。他の窓とスロットは畳む。 */
        let mut k = 1;
        while k < 4 {
            g.windows[k] = wm::Win::EMPTY;
            g.slots[k] = wm::Slot::EMPTY;
            k += 1;
        }
        g.z_count = 1;
        g.inited = true;
    }
    multiapp::on_start(2);
    multiapp::mark_resumed(2);

    drive_op_wait(2, 3);
    assert_eq!(
        mocks::PARKS.load(Ordering::SeqCst),
        0,
        "アプリが 1 本なのに exec_park を呼んだ (回帰)"
    );
    /* 空回りで 0 回になったのではないこと: `wm_cycle` が 1 周でも回れば
     * 末尾の `sync_snd_focus` がフォーカス (owner 2) を音へ渡している。 */
    assert_eq!(
        mocks::snd_focus_calls(),
        vec![2],
        "op_wait のループが 1 周も回っていない (試験が空振り)"
    );
    wm::g().inited = false;
}

/* ---- 票の追加検査 5: 2 本目が ready なら `op_wait` の中で譲る ---- */
#[test]
fn op_wait_parks_when_another_app_is_ready() {
    use crate::{mocks, multiapp, wm};
    use std::sync::atomic::Ordering;
    mocks::init();
    let shm = mocks::Shm::new();
    {
        let g = wm::g();
        *g = four_app_state(&shm);
        let mut k = 2;
        while k < 4 {
            g.windows[k] = wm::Win::EMPTY;
            g.slots[k] = wm::Slot::EMPTY;
            k += 1;
        }
        g.z_count = 2;
        g.inited = true;
        g.windows[1].configure_pending = true; /* 相手 (ID 3) だけが ready */
    }
    multiapp::on_start(2);
    multiapp::on_start(3);
    multiapp::mark_resumed(2);

    drive_op_wait(2, 3);
    assert!(
        mocks::PARKS.load(Ordering::SeqCst) >= 1,
        "相手が ready なのに op_wait が譲らなかった"
    );
    wm::g().inited = false;
}

/* ---- 票の追加検査 6: フォーカス切替で `snd_focus` は 1 回だけ ----
 *  決裁 D9-4 / 受入 G10。同じフォーカスのまま何周回しても呼ばない。 */
#[test]
fn snd_focus_is_called_once_per_focus_change() {
    use crate::{mocks, multiapp, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);

    multiapp::sync_snd_focus(&st); /* 最前面 = owner 5 */
    assert_eq!(mocks::snd_focus_calls(), vec![5], "初回が違う");
    multiapp::sync_snd_focus(&st);
    multiapp::sync_snd_focus(&st);
    assert_eq!(
        mocks::snd_focus_calls().len(),
        1,
        "フォーカスが動いていないのに snd_focus を呼んだ"
    );

    focus_app(&mut st, 2);
    multiapp::sync_snd_focus(&st);
    assert_eq!(
        mocks::snd_focus_calls(),
        vec![5, 2],
        "切替のたびに 1 回だけ呼ばれていない"
    );
    assert_eq!(multiapp::snd_owner(), 2);

    /* 窓が 1 枚も無くなったら音の所有権はシェル帯 (1) へ戻す。 */
    let mut k = 0;
    while k < 4 {
        st.windows[k] = wm::Win::EMPTY;
        k += 1;
    }
    st.z_count = 0;
    multiapp::sync_snd_focus(&st);
    assert_eq!(
        mocks::snd_focus_calls(),
        vec![5, 2, 1],
        "最後の窓が消えても音がアプリのままになっている"
    );
}

/* ---- 票の追加検査 7: `LAUNCH` は「1 本増やす」(契約 S2 の読み替え) ---- */
#[test]
fn launch_no_longer_quits_the_running_apps_but_switch_cui_still_does() {
    use crate::{mocks, ring, session, slot, wm};
    use os32api::gui::proto::{
        GuiEvent, GUI_EV_QUIT, GUI_RING_CAPACITY, GUI_SESSION_LAUNCH, GUI_SESSION_SWITCH_CUI,
    };

    fn quit_events(st: &wm::GuiState, slot_i: usize) -> usize {
        let h = slot::read_header(st, slot_i);
        let base = slot::ring_ptr(st, slot_i);
        let mut n = 0;
        let mut i = h.ring_head;
        while i != h.ring_tail {
            let ev: GuiEvent = unsafe {
                core::ptr::read_unaligned(
                    base.add((i as usize % GUI_RING_CAPACITY) * 16) as *const GuiEvent,
                )
            };
            if ev.kind == GUI_EV_QUIT {
                n += 1;
            }
            i = i.wrapping_add(1);
        }
        n
    }

    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);

    /* LAUNCH: 誰にも Quit を送らない。top-level では即実行してよい。 */
    let path = b"/usr/bin/gui_demo.bin";
    assert_eq!(
        session::request(&mut st, 2, GUI_SESSION_LAUNCH, 0, path, path.len()),
        0
    );
    let mut k = 0;
    while k < 4 {
        assert_eq!(quit_events(&st, k), 0, "LAUNCH がスロット {k} を Quit させた");
        assert_eq!(ring::pending(&st, k), 0);
        k += 1;
    }
    assert!(session::ready_to_run(&st), "LAUNCH がアプリ生存で足止めされた");
    session::clear();

    /* SWITCH_CUI: GUI ごと畳むので全アプリへ Quit。回収まで実行しない。 */
    assert_eq!(
        session::request(&mut st, 2, GUI_SESSION_SWITCH_CUI, 0, b"", 0),
        0
    );
    let mut k = 0;
    while k < 4 {
        assert_eq!(quit_events(&st, k), 1, "SWITCH_CUI がスロット {k} へ届いていない");
        k += 1;
    }
    assert!(
        !session::ready_to_run(&st),
        "アプリが生きているのに SWITCH_CUI を実行しようとした"
    );
    session::clear();
}

/* ---- 票の追加検査 8: top-level は 1 周に 1 本だけ起こす ---- */
#[test]
fn resume_one_wakes_a_single_app_with_the_unread_count_as_the_wait_result() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    seed_four_apps();
    set_input_ready(&mut st, 4, true);
    focus_app(&mut st, 4);

    assert!(multiapp::resume_one(&mut st), "起こす相手が居るのに起こさない");
    let calls = mocks::resume_calls();
    assert_eq!(calls.len(), 1, "1 周で 2 本以上起こした: {calls:?}");
    /* 契約 T3: OP_WAIT の戻り値は未読件数。 */
    assert_eq!(calls[0], (4, 1), "exec_resume の引数が違う: {calls:?}");
    assert_eq!(multiapp::last_run(), 4);
    assert!(multiapp::turn_used(4), "resume で turn が使われていない");
    assert_eq!(multiapp::running(), 0, "resume から戻ったのに走ったまま");

    /* ready が 1 本も無ければ誰も起こさない (= top-level は sys_halt へ)。 */
    set_input_ready(&mut st, 4, false);
    assert!(!multiapp::resume_one(&mut st), "ready ゼロで誰かを起こした");
    assert_eq!(mocks::resume_calls().len(), 1);
}

/* ---- 票の追加検査 9: 止めてあるアプリの Quit は `exec_kill` ---- */
#[test]
fn a_parked_app_is_folded_with_exec_kill_from_the_top_level() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    seed_four_apps();

    multiapp::request_kill(4);
    assert!(multiapp::resume_one(&mut st), "kill 予約が実行されない");
    assert_eq!(mocks::kill_calls(), vec![4], "exec_kill の相手が違う");
    assert!(
        mocks::resume_calls().is_empty(),
        "kill する相手を起こしてしまった"
    );
    assert!(!multiapp::is_tracked(4), "kill した ID が表に残った");
    assert_eq!(multiapp::live_count(), 3, "kill が 1 本分でない");
}

/* ---- 票の追加検査 10: 起動したてのアプリは最初の `OP_WAIT` で譲れる ----
 *  `exec_start` は「アプリが最初に park する」まで戻らない (決裁 D9-5)。
 *  park の判断は WM の表を見るが、**その表に載る id は `exec_start` の
 *  戻り値**なので、起動中のアプリはまだ表に居ない。ここを「未追跡だから
 *  譲らない」で弾くと `exec_start` が永久に戻らず、
 *    - 2 本目以降が park できない = 1 本目が二度と起こされない
 *    - top-level へ戻れないので 3 本目の `LAUNCH` も実行できない
 *  という固まり方をする。起動が進行中の間だけ、走っている ID を表へ迎える。 */
#[test]
fn a_just_launched_app_can_park_on_its_first_op_wait() {
    use crate::{mocks, multiapp, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = four_app_state(&shm);
    /* 生きているのは A (=2) だけ。B (=3) はこれから起動する。 */
    st.windows[2] = wm::Win::EMPTY;
    st.windows[3] = wm::Win::EMPTY;
    st.z_count = 2;
    st.slots[2] = wm::Slot::EMPTY;
    st.slots[3] = wm::Slot::EMPTY;
    multiapp::on_start(2);
    set_derived_ready(&mut st, 2, true); /* A は Paint 待ちで park 中 */

    /* 起動中 (`exec_start` を呼んでから戻るまで)。B の最初の OP_WAIT。 */
    multiapp::begin_start();
    assert!(
        multiapp::should_park(&st, 3),
        "起動したてのアプリが最初の OP_WAIT で譲れない (exec_start が戻らない)"
    );
    assert!(multiapp::is_tracked(3), "起動したてのアプリが表に載らない");
    multiapp::note_parked(3, None);
    multiapp::end_start(3);
    assert_eq!(multiapp::live_count(), 2, "起動で 1 本増えていない");
    assert_eq!(multiapp::running(), 0, "park したのに走ったままになっている");

    /* 対照群 1: 起動中でない未追跡 ID (CUI の入れ子の子) は譲らない。
     * カーネルの `appslot_park_check` も `!a->gui` で弾く側。 */
    multiapp::on_owner_exit(3);
    assert!(
        !multiapp::should_park(&st, 3),
        "起動中でない未追跡 ID を park しようとした"
    );

    /* 対照群 2: 1 本目の起動 (他に ready が居ない) では譲らない = 回帰ゼロ。
     * `exec_start` が従来の `exec_run` と同じく終了まで塞ぐのが正しい。 */
    multiapp::reset();
    set_derived_ready(&mut st, 2, false);
    multiapp::begin_start();
    assert!(
        !multiapp::should_park(&st, 2),
        "1 本目の起動で譲った (相手が居ないのに CR3 が動く = 回帰)"
    );
}

/* ================================================================ */
/*  不具合 W-1 (2026-09-11 の実機受入で発見)                          */
/*  park 中のアプリの露出領域が再描画されない                        */
/* ================================================================ */

/// 検査用: `set` のどれか 1 枚が `r` を丸ごと含むか。
fn covers(set: &crate::wm::RectSet, r: crate::wm::Rect) -> bool {
    let mut i = 0;
    while i < set.len {
        let s = set.rects[i];
        if s.x <= r.x && s.y <= r.y && s.right() >= r.right() && s.bottom() >= r.bottom() {
            return true;
        }
        i += 1;
    }
    false
}

/// 検査用: A (owner 2、窓 index 0) と B (owner 3、窓 index 1、A の上半分を覆う)
/// を張った状態を `wm::g()` に置く。
fn two_app_overlap_state(shm: &crate::mocks::Shm) {
    use crate::{slot, wm};
    let g = wm::g();
    *g = one_window_state(shm); /* A = owner 2、窓 index 0、slot 0 */
    g.inited = true;
    /* B の受け皿。窓は「起動中に作られた」ことにして先に張る
     * (`exec_start` のモックは窓を作れない)。A の上半分だけを覆う。 */
    g.slots[1].used = true;
    g.slots[1].owner = 3;
    slot::init_header(g, 1);
    let mut b = wm::Win::EMPTY;
    b.used = true;
    b.visible = true;
    b.owner = 3;
    b.gen = 1;
    b.x = 40;
    b.y = 40;
    b.w = 400;
    b.h = 120;
    g.windows[1] = b;
    g.zorder[1] = 1; /* B が前面 */
    g.z_count = 2;
}

/// A (park 中、1 窓) の上に B の窓が開くと、A の**露出部**が黒いまま残る
/// (実機 `gui_bench` + `gui_demo`、`ring3_switch_count` が動かない)。
///
/// 仕掛けは `run_program` の `gfx_init()`。`gfx/gfx_core.c` の `gfx_init` は
/// **VRAM の両ページをゼロクリアする**。`exec_run` の時代はアプリが終わって
/// からしか戻らなかったので消えるのは死んだアプリの画だけだったが、
/// `exec_start` は park した時点で戻る = **生きているアプリのクライアント面
/// まで消える**。WM はクライアント面を持たない (契約 G4) ので、消したら本人に
/// 描き直させるしか無い。ところが遮蔽は露出を生まないので
/// `recompute_and_expose` は dirty を 1 つも足さず、`derived_ready` が偽の
/// まま = `pick` が A を選ばない = 誰も `exec_resume` しない。
///
/// 検査は「画が残っている」か「全面 dirty で描き直させる」かの**どちらかは
/// 成り立つ**こと。どちらでもないのが W-1 の状態。
#[test]
fn a_parked_app_is_not_left_black_when_another_app_is_launched() {
    use crate::{mocks, multiapp, visible, wm};
    use std::sync::atomic::Ordering;
    mocks::init();
    let shm = mocks::Shm::new();
    two_app_overlap_state(&shm);
    multiapp::on_start(2);

    /* A は全面を描き終えて COMMIT 済み = dirty 無しで park している。 */
    visible::recompute_and_expose(wm::g());
    wm::g().windows[0].dirty.clear();
    wm::g().windows[0].configure_pending = false;
    multiapp::note_parked(2, None);
    assert!(
        !multiapp::derived_ready(wm::g(), 2),
        "前提が崩れている: A が最初から ready"
    );

    /* A のクライアント面に目印を塗る (= アプリが描いた画)。露出部の標本は
     * B に覆われない下側から取る。 */
    let cr = wm::g().windows[0].client_rect_screen();
    let (sx, sy) = (cr.x + 8, cr.bottom() - 8);
    assert!(
        !wm::g().windows[1].outer().contains(sx, sy),
        "標本点が B に隠れている (試験の geometry が違う)"
    );
    unsafe { os32api::gfx::gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, 7) };
    assert_eq!(mocks::gfx_get_pixel(sx, sy), 7);

    /* B を起動する (`exec_start` は park 済みの app_id 3 を返す)。 */
    *mocks::START_SCRIPT.lock().unwrap() = vec![3];
    let mut path = [0u8; 256];
    let p = b"/usr/bin/gui_demo.bin\0";
    path[..p.len()].copy_from_slice(p);
    let rc = crate::run_program(wm::g(), &path, crate::LaunchVia::Wm);
    assert_eq!(rc, 3, "exec_start の戻り値を取り違えている");
    assert_eq!(multiapp::live_count(), 2, "起動で 1 本増えていない");

    /* (1) 画が残っているか、残っていないなら全面 dirty で描き直させるか。 */
    let kept = mocks::gfx_get_pixel(sx, sy) == 7;
    let (cw, ch) = wm::g().windows[0].client_size();
    let repaint = covers(&wm::g().windows[0].dirty, crate::wm::Rect::new(0, 0, cw, ch));
    assert!(
        kept || repaint,
        "W-1: 起動で A の画が消えた (gfx_init {} 回) のに描き直させない (dirty len={})",
        mocks::GFX_INITS.load(Ordering::SeqCst),
        wm::g().windows[0].dirty.len
    );

    /* (2) 描き直しが要るなら、A は導出群の ready = top-level が起こす相手。 */
    if !kept {
        assert!(
            multiapp::derived_ready(wm::g(), 2),
            "W-1: 描き直しが要るのに A が ready にならない (誰も起こさない)"
        );
        assert_eq!(multiapp::pick(wm::g()), 2, "W-1: top-level が A を選ばない");
    }
    wm::g().inited = false;
}

/// 走っている B は、park 中の A に配送できる `Paint` がある間は `OP_WAIT` で
/// 譲る。譲らないと A は永久に描き直せない (D11-3 の (1))。
/// 併せて「A が `Paint` を消費したら dirty が残らない」「B の窓を閉じたら
/// A の露出部が dirty になり、また A が選ばれる」も見る。
#[test]
fn a_running_app_yields_to_a_parked_app_that_has_a_deliverable_paint() {
    use crate::{handler, mocks, multiapp, visible, wm};
    use os32api::gui::proto::{GuiEvent, GUI_EV_PAINT, GUI_OP_POLL, GUI_RING_CAPACITY};
    mocks::init();
    let shm = mocks::Shm::new();
    two_app_overlap_state(&shm);
    multiapp::on_start(2);
    multiapp::on_start(3);
    visible::recompute_and_expose(wm::g());
    wm::g().windows[0].dirty.clear();
    wm::g().windows[0].configure_pending = false;
    wm::g().windows[1].dirty.clear();
    wm::g().windows[1].configure_pending = false;
    multiapp::note_parked(2, None);
    multiapp::mark_resumed(3); /* B が走っている */

    /* (1) A に配送できる Paint がある = B は譲る。 */
    crate::damage::set_dirty_full(&mut wm::g().windows[0]);
    assert!(
        multiapp::derived_ready(wm::g(), 2),
        "露出部に dirty があるのに A が ready でない"
    );
    assert!(
        multiapp::should_park(wm::g(), 3),
        "A に配送できる Paint があるのに B が譲らない"
    );

    /* (2) A を起こして `OP_POLL` させると Paint が配られ、dirty は残らない。 */
    assert_eq!(multiapp::pick(wm::g()), 2, "top-level が A を選ばない");
    multiapp::mark_resumed(2);
    let n = handler::gshell_gui_handler(GUI_OP_POLL, 0, 2);
    assert!(n > 0, "OP_POLL が Paint を 1 件も返さない");
    /* 残ってよいのは **B に隠れている分だけ** (契約 G4: 隠れた場所は露出する
     * まで dirty のまま)。配送できる分が残っていたら配り落としている。 */
    assert!(
        !crate::damage::has_deliverable_paint(&wm::g().windows[0]),
        "Paint を配ったのに配送できる dirty が残った (len={})",
        wm::g().windows[0].dirty.len
    );
    /* 配られたのが Paint であること。 */
    let mut seen = false;
    {
        let h = crate::slot::read_header(wm::g(), 0);
        let base = crate::slot::ring_ptr(wm::g(), 0);
        let mut i = h.ring_head;
        while i != h.ring_tail {
            let ev: GuiEvent = unsafe {
                core::ptr::read_unaligned(
                    base.add((i as usize % GUI_RING_CAPACITY) * 16) as *const GuiEvent,
                )
            };
            if ev.kind == GUI_EV_PAINT {
                seen = true;
            }
            i = i.wrapping_add(1);
        }
    }
    assert!(seen, "配られたイベントに Paint が無い");
    set_input_ready(wm::g(), 2, false); /* アプリが読み切った */
    assert!(
        !multiapp::derived_ready(wm::g(), 2),
        "Paint を消費したのに A がまだ ready"
    );

    /* (3) B の窓を閉じると A の露出部が dirty になり、また A が選ばれる。 */
    multiapp::note_parked(2, None);
    multiapp::mark_resumed(3);
    let bid = wm::g().windows[1].id(1);
    assert_eq!(wm::destroy_window(wm::g(), 3, bid), 0, "B の窓を閉じられない");
    assert!(
        multiapp::derived_ready(wm::g(), 2),
        "B の窓を閉じたのに A の露出部が dirty にならない"
    );
    assert!(
        multiapp::should_park(wm::g(), 3),
        "露出した A を描かせるために B が譲らない"
    );
    assert_eq!(multiapp::pick(wm::g()), 2, "露出した A が選ばれない");
    wm::g().inited = false;
}

/* ================================================================ */
/*  W-2 — K5c への追随 (ユーザー決裁 2026-09-11 A1 / A3)             */
/*                                                                  */
/*  A1: CTRL+STOP の宛先はフォーカス窓のアプリ (契約 T6)。カーネルは  */
/*      IRQ1 で「走っているアプリ」にしか要求を立てられないので、     */
/*      フォーカスが別のアプリなら WM が `exec_abort_clear` で本人の  */
/*      要求を降ろし、フォーカス窓の ID を `exec_kill` で畳む。       */
/*      どちらも owner 1 (top-level) からしか呼べない (K5c) ので、    */
/*      `op_wait` の中では**予約するだけ**で、park で top-level へ    */
/*      戻ったところで実行する。                                     */
/*  A3: `SWITCH_CUI` / `SHUTDOWN` で Quit に応答しないアプリは        */
/*      `QUIT_GRACE_CYCLES` 周待ってから畳む。                       */
/* ================================================================ */

/// アプリ 2 本 (owner 2, 3) をグローバルの `GuiState` に載せる。
/// Z 順は 0,1 なので最前面 = 窓 1 = owner 3。
fn two_app_global(shm: &crate::mocks::Shm) {
    use crate::{multiapp, wm};
    let g = wm::g();
    *g = four_app_state(shm);
    let mut k = 2;
    while k < 4 {
        g.windows[k] = wm::Win::EMPTY;
        g.slots[k] = wm::Slot::EMPTY;
        k += 1;
    }
    g.z_count = 2;
    g.inited = true;
    multiapp::on_start(2);
    multiapp::on_start(3);
    multiapp::mark_resumed(2); /* 走っているのは 2 */
}

/* ---- (a) フォーカスが本人なら abort は取り消さない ---- */
#[test]
fn ctrl_stop_with_focus_on_the_running_app_keeps_the_kernel_abort() {
    use crate::{mocks, multiapp, session, wm};
    use std::sync::atomic::Ordering;
    mocks::init();
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    focus_app(wm::g(), 2); /* フォーカス = 走っている本人 */
    wm::g().abort_seen = true;

    drive_op_wait(2, 3);

    /* `op_wait` の中では owner がアプリ ID なので KAPI は呼べない (K5c)。
     * **本人宛ても予約に統一した** (実機受入 S6 の 3 回目、2026-09-13):
     * 以前は `break` してカーネルの `abort_req` に任せていたが、IRQ1 が
     * CPL=3 の実行中に着地した周は `abort_req` が立たない (暴走ではないので
     * K が立てない) ため、出口に何も無く**要求が消える** (3 回中 2 回失敗)。 */
    assert_eq!(
        mocks::abort_clear_calls(),
        0,
        "op_wait の中から exec_abort_clear を呼んだ (owner 1 でないので ERR_INVAL)"
    );
    assert!(
        multiapp::pending_top_level_work(),
        "本人宛ての CTRL+STOP を予約していない (カーネルの abort_req 頼みは落ちる)"
    );
    /* 次の `GetMessage` (= 次の `op_wait`) で予約を見て譲る。 */
    drive_op_wait(2, 3);
    assert!(
        mocks::PARKS.load(Ordering::SeqCst) >= 1,
        "予約を積んだのに top-level へ戻ろうとしていない (永久に実行されない)"
    );

    /* top-level の 1 周で「要求を降ろす → 本人を畳む」(決裁 A1 の順序)。 */
    mocks::set_kill_frees(&[2]);
    assert!(multiapp::resume_one(wm::g()), "top-level が予約を実行しない");
    assert_eq!(
        mocks::abort_clear_calls(),
        1,
        "exec_abort_clear がちょうど 1 回でない"
    );
    assert_eq!(mocks::kill_calls(), vec![2], "本人を畳んでいない");
    assert!(!multiapp::is_tracked(2), "畳んだ本人が表に残った");
    assert!(multiapp::is_tracked(3), "巻き添えで畳んだ");
    wm::g().inited = false;
}

/* ---- (b) フォーカスが別アプリなら取り消し + フォーカス窓を kill ---- */
#[test]
fn ctrl_stop_with_focus_on_another_app_clears_the_abort_and_kills_the_focused_one() {
    use crate::{mocks, multiapp, session, wm};
    use std::sync::atomic::Ordering;
    mocks::init();
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    focus_app(wm::g(), 3); /* フォーカス = 別のアプリ */
    wm::g().abort_seen = true;

    drive_op_wait(2, 3);

    /* `op_wait` の中では owner がアプリ ID なので KAPI は呼べない (K5c)。 */
    assert_eq!(
        mocks::abort_clear_calls(),
        0,
        "op_wait の中から exec_abort_clear を呼んだ (owner 1 でないので ERR_INVAL)"
    );
    assert!(
        multiapp::pending_top_level_work(),
        "取り消しと kill が top-level へ持ち越されていない"
    );
    /* 持ち越すには top-level へ戻る = 譲るしかない。 */
    assert!(
        mocks::PARKS.load(Ordering::SeqCst) >= 1,
        "予約を積んだのに top-level へ戻ろうとしていない (永久に実行されない)"
    );

    /* top-level の 1 周 (単独ループの `resume_one`)。 */
    assert!(multiapp::resume_one(wm::g()), "top-level が予約を実行しない");
    assert_eq!(
        mocks::abort_clear_calls(),
        1,
        "exec_abort_clear がちょうど 1 回でない"
    );
    assert_eq!(
        mocks::kill_calls(),
        vec![3],
        "畳む相手がフォーカス窓の owner でない"
    );
    assert!(multiapp::is_tracked(2), "走っている本人まで畳んでしまった");
    assert!(!multiapp::is_tracked(3), "kill した ID が表に残った");
    wm::g().inited = false;
}

/* ---- (c) SWITCH_CUI: Quit に応答しないアプリは N 周後に畳む ---- */
#[test]
fn switch_cui_folds_an_app_that_ignores_quit_after_the_grace_cycles() {
    use crate::{mocks, multiapp, session, wm};
    use os32api::gui::proto::GUI_SESSION_SWITCH_CUI;
    mocks::init();
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let shm = mocks::Shm::new();
    two_app_global(&shm);

    /* 全アプリへ Quit を配る (契約 S5)。 */
    assert_eq!(session::set_wm(wm::g(), GUI_SESSION_SWITCH_CUI, b"\0"), 0);
    assert_eq!(
        session::quit_grace_left(),
        session::QUIT_GRACE_CYCLES,
        "Quit を配ったのに猶予が始まっていない"
    );

    /* アプリ 3 は Quit に応じて終了した (回収は `gui_owner_exit` 経由)。 */
    wm::g().reclaim_owner(3);
    session::reclaim_owner(3);
    multiapp::on_owner_exit(3);

    /* アプリ 2 は Quit を無視して待ち続ける。N-1 周ではまだ畳まない。 */
    let mut n = 0;
    while n < session::QUIT_GRACE_CYCLES - 1 {
        session::x3_cycle(wm::g());
        n += 1;
    }
    assert!(
        !multiapp::pending_top_level_work(),
        "猶予が切れる前に畳もうとした ({n} 周)"
    );

    /* N 周目で打ち切り。 */
    session::x3_cycle(wm::g());
    assert!(
        multiapp::pending_top_level_work(),
        "N 周待っても応答しないアプリが畳まれない (SWITCH_CUI が永久に成立しない)"
    );
    assert!(multiapp::resume_one(wm::g()), "top-level が kill を実行しない");
    assert_eq!(
        mocks::kill_calls(),
        vec![2],
        "応答したアプリまで畳んだ / 相手が違う"
    );
    assert_eq!(multiapp::live_count(), 0, "全回収になっていない");
    wm::g().inited = false;
}

/* ---- (c') 全員が応答したら誰も畳まない ---- */
#[test]
fn switch_cui_kills_nobody_when_every_app_answers_the_quit() {
    use crate::{mocks, multiapp, session, wm};
    use os32api::gui::proto::GUI_SESSION_SWITCH_CUI;
    mocks::init();
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    assert_eq!(session::set_wm(wm::g(), GUI_SESSION_SWITCH_CUI, b"\0"), 0);

    let mut id = 2;
    while id <= 3 {
        wm::g().reclaim_owner(id);
        session::reclaim_owner(id);
        multiapp::on_owner_exit(id);
        id += 1;
    }
    let mut n = 0;
    while n < session::QUIT_GRACE_CYCLES + 2 {
        session::x3_cycle(wm::g());
        n += 1;
    }
    assert_eq!(
        session::quit_grace_left(),
        0,
        "全員が応答したのに猶予が走り続けている"
    );
    assert!(
        mocks::kill_calls().is_empty(),
        "応答したアプリを畳んだ: {:?}",
        mocks::kill_calls()
    );
    assert!(session::ready_to_run(wm::g()), "全回収なのに SWITCH_CUI が実行できない");
    wm::g().inited = false;
}

/* ---- (d) 1 本のときも本人宛ては予約に統一する (票 T9 受入 S6 の 3 回目) ----
 *  実機 2026-09-13: `break` してカーネルの `abort_req` に任せる形は、IRQ1 が
 *  アプリの CPL=3 実行中に着地した周だと要求が立たず**消える** (3 回中 2 回
 *  失敗)。**CTRL+STOP を押した周だけ**は 1 本でも park して top-level へ戻す。
 *  押していない周の「1 本なら park しない」(回帰ゼロ) は
 *  `a_single_app_never_parks_and_keeps_the_old_wm_cycle_halt_loop` が見ている。 */
#[test]
fn a_single_app_keeps_the_old_ctrl_stop_path() {
    use crate::{mocks, multiapp, session, wm};
    use std::sync::atomic::Ordering;
    mocks::init();
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let shm = mocks::Shm::new();
    {
        let g = wm::g();
        *g = four_app_state(&shm);
        let mut k = 1;
        while k < 4 {
            g.windows[k] = wm::Win::EMPTY;
            g.slots[k] = wm::Slot::EMPTY;
            k += 1;
        }
        g.z_count = 1;
        g.inited = true;
        g.abort_seen = true;
    }
    multiapp::on_start(2);
    multiapp::mark_resumed(2);

    /* 1 周目: 「フォーカス = 本人」の枝で予約を積む。KAPI はまだ呼ばない
     * (`op_wait` の中は owner がアプリ ID なので通らない — K5c)。 */
    drive_op_wait(2, 3);
    assert_eq!(mocks::abort_clear_calls(), 0, "op_wait の中から exec_abort_clear を呼んだ");
    assert!(
        mocks::kill_calls().is_empty(),
        "op_wait の中から exec_kill を呼んだ: {:?}",
        mocks::kill_calls()
    );
    assert!(
        multiapp::pending_top_level_work(),
        "1 本のときに本人宛ての CTRL+STOP を落とした (実機 S6 の 3 回目)"
    );

    /* 次の `GetMessage` (= 次の `op_wait`): 予約を見て譲り、top-level へ戻す。 */
    drive_op_wait(2, 3);
    assert!(
        mocks::PARKS.load(Ordering::SeqCst) >= 1,
        "予約を積んだのに譲らない (top-level へ戻る道が無い = 永久に実行されない)"
    );

    /* top-level の 1 周で「要求を降ろす → 本人を畳む」(決裁 A1 の順序)。 */
    mocks::set_kill_frees(&[2]);
    assert!(multiapp::resume_one(wm::g()), "top-level が予約を実行しない");
    assert_eq!(mocks::abort_clear_calls(), 1, "exec_abort_clear がちょうど 1 回でない");
    assert_eq!(mocks::kill_calls(), vec![2], "本人を畳んでいない");
    assert!(!multiapp::is_tracked(2), "畳んだ本人が表に残った");
    wm::g().inited = false;
}

/* ================================================================ */
/*  票 K7-W — 鍵待ち (WAIT_KEY) の CUI アプリ (指摘 A / C)           */
/*                                                                  */
/*  端末アプリ経由で走る CUI プログラムは `OP_INIT` を通らないので   */
/*  スロットも窓も持たない。止まる理由は `kbd_getchar` の待ち        */
/*  (`APP_STATE_WAIT_KEY`) だけで、起こしてよいかは注入リングの      */
/*  未読バイト数 (`kbd_inject_pending`) が決める。                   */
/* ================================================================ */

/// カーネル `exec/appslot.h` の `APP_STATE_*` (`exec_app_state` の答え)。
const ST_PARKED: i32 = 2;
const ST_WAIT_KEY: i32 = 3;

/* ---- K7-W 検査 1: pending 0 では起こさず、しかし忘れもしない ---- */
#[test]
fn a_slotless_app_waiting_for_a_key_is_never_forgotten() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_state(&shm);
    multiapp::on_start(2); /* GUI アプリ (スロット 0) */
    multiapp::on_start(3); /* 端末から起動した CUI (スロット無し) */
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_KEY);

    /* 注入リングが空 = 起床の理由が無い (指摘 C)。 */
    mocks::set_kbd_pending(0);
    assert!(!multiapp::input_ready(&st, 3), "pending 0 なのに入力群になった");
    assert_eq!(multiapp::pick(&st), 0, "pending 0 の WAIT_KEY を選んだ");
    assert!(!multiapp::resume_one(&mut st), "pending 0 で誰かを起こした");
    assert!(
        mocks::resume_calls().is_empty(),
        "pending 0 で exec_resume を呼んだ: {:?}",
        mocks::resume_calls()
    );
    /* 指摘 A: スロットが無いからといって表から落としてはならない。 */
    assert!(multiapp::is_tracked(3), "鍵待ちのアプリを forget してしまった");
    assert!(mocks::kill_calls().is_empty(), "鍵待ちのアプリを畳んでしまった");
}

/* ---- K7-W 検査 2: pending > 0 なら入力群として選ばれ resume される ---- */
#[test]
fn a_slotless_app_waiting_for_a_key_is_resumed_when_a_byte_is_injected() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_state(&shm);
    multiapp::on_start(2);
    multiapp::on_start(3);
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_KEY);
    mocks::set_kbd_pending(1);

    assert!(multiapp::input_ready(&st, 3), "注入があるのに入力群でない");
    assert_eq!(multiapp::pick(&st), 3, "注入があるのに WAIT_KEY を選ばない");
    assert!(multiapp::resume_one(&mut st), "起こす相手が居るのに起こさない");
    let calls = mocks::resume_calls();
    assert_eq!(calls.len(), 1, "1 周で 2 本以上起こした: {calls:?}");
    /* 指摘 B: 文字はカーネルが `wait_ret` を上書きして渡すので WM は 0。 */
    assert_eq!(calls[0], (3, 0), "exec_resume の引数が違う: {calls:?}");
    assert!(multiapp::is_tracked(3), "resume したのに表から落ちた");
    assert!(mocks::kill_calls().is_empty(), "resume できたのに畳んだ");
}

/* ---- K7-W 検査 3: `OS32_ERR_AGAIN` はその周を譲るだけ ---- */
#[test]
fn an_again_from_exec_resume_yields_the_round_without_folding_the_app() {
    use crate::{mocks, multiapp};
    use os32api::gui::proto::OS32_ERR_AGAIN;
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_state(&shm);
    multiapp::on_start(2);
    multiapp::on_start(3);
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_KEY);
    mocks::set_kbd_pending(1);
    *mocks::RESUME_SCRIPT.lock().unwrap() = vec![OS32_ERR_AGAIN];
    let last_before = multiapp::last_run();

    assert!(multiapp::resume_one(&mut st), "AGAIN の周が「何もしない」になった");
    assert_eq!(mocks::resume_calls(), vec![(3, 0)], "resume を呼んでいない");
    /* 負値だからといって「起こせない本」として畳んではならない。 */
    assert!(mocks::kill_calls().is_empty(), "AGAIN で exec_kill を呼んだ");
    assert!(multiapp::is_tracked(3), "AGAIN で表から落とした");
    /* 譲るだけ = turn も巡回の起点も動かさない (streak に数えない)。 */
    assert!(!multiapp::turn_used(3), "AGAIN が turn を使った");
    assert_eq!(multiapp::last_run(), last_before, "AGAIN が巡回の起点を動かした");
    assert_eq!(multiapp::running(), 0, "AGAIN から戻ったのに走ったまま");
}

/* ---- K7-W 検査 4: 鍵待ちでないスロット無しは従来どおり忘れる ---- */
#[test]
fn a_slotless_app_that_is_not_waiting_for_a_key_is_still_forgotten() {
    use crate::{mocks, multiapp};
    use crate::wm;
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_state(&shm);
    /* owner 3 の窓だけがあってスロットは無い (`OP_INIT` 前 / 回収済み)。
     * `Configure` 未通知で導出群に入るので `pick` が選ぶ。 */
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 3;
    w.gen = 1;
    w.x = 200;
    w.y = 10;
    w.w = 100;
    w.h = 80;
    w.configure_pending = true;
    st.windows[1] = w;
    st.zorder[1] = 1;
    st.z_count = 2;
    multiapp::on_start(2);
    multiapp::on_start(3);
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_PARKED); /* 鍵待ちではない */
    mocks::set_kbd_pending(0);

    assert!(multiapp::derived_ready(&st, 3), "導出群になっていない");
    assert_eq!(multiapp::pick(&st), 3, "導出群の 1 本を選ばない");
    assert!(multiapp::resume_one(&mut st), "1 周で何もしなかった");
    assert!(
        mocks::resume_calls().is_empty(),
        "スロットの無い非鍵待ちを起こした: {:?}",
        mocks::resume_calls()
    );
    assert!(!multiapp::is_tracked(3), "スロットの無い非鍵待ちが表に残った");
}

/* ---- K7-W 検査 5 (受入 I3): スロット無しの鍵待ちは SWITCH_CUI で畳む ----
 *  端末 (`t5a_display`) と、そこから起動した窓無しの CUI プログラム
 *  (`kbd_getchar` で `WAIT_KEY` に park) が生きている状態で CUI へ戻ると、
 *  端末は畳まれるのに CUI プログラムだけ AppSlot に残っていた
 *  (PM 実測 2026-09-12)。`Quit` はスロットのリングにしか積めないので、
 *  この 1 本は待っても自分から終われない = 即 `exec_kill` するしかない。 */
#[test]
fn switch_cui_kills_a_slotless_key_waiting_app_that_cannot_be_sent_a_quit() {
    use crate::{mocks, multiapp, session, wm};
    use os32api::gui::proto::GUI_SESSION_SWITCH_CUI;
    mocks::init();
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let shm = mocks::Shm::new();
    {
        let g = wm::g();
        *g = one_gui_app_state(&shm);
        g.inited = true;
    }
    multiapp::on_start(2); /* 端末: スロット 0 + 窓 1 枚 */
    multiapp::on_start(3); /* 端末から起動した CUI: スロットも窓も無い */
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_KEY);
    mocks::set_kbd_pending(0); /* 打鍵待ちのまま (起こす理由が無い) */

    /* Start → CUI mode → Yes。 */
    assert_eq!(session::set_wm(wm::g(), GUI_SESSION_SWITCH_CUI, b"\0"), 0);
    /* 配る先の無い 1 本は猶予を待たずに畳む予約が入る (決裁 A3 の外)。 */
    assert!(
        multiapp::pending_top_level_work(),
        "Quit を配れない 1 本の kill が予約されていない"
    );

    /* 端末は Quit に応じて終了した。残るのは表の中の ID 3 だけ。 */
    wm::g().reclaim_owner(2);
    session::reclaim_owner(2);
    multiapp::on_owner_exit(2);

    /* ここで成立させてしまうと ID 3 の AppSlot と物理ページが漏れる。 */
    assert!(
        !session::ready_to_run(wm::g()),
        "スロット無しの被追跡アプリを残したまま CUI へ切り替えようとした"
    );

    /* top-level の 1 周で畳む。 */
    assert!(multiapp::resume_one(wm::g()), "top-level が kill を実行しない");
    assert_eq!(mocks::kill_calls(), vec![3], "畳む相手が違う");
    assert!(
        mocks::resume_calls().is_empty(),
        "畳む相手を起こしてしまった: {:?}",
        mocks::resume_calls()
    );
    assert!(!multiapp::is_tracked(3), "kill した ID が表に残った");
    assert_eq!(multiapp::live_count(), 0, "全回収になっていない");
    assert!(
        session::ready_to_run(wm::g()),
        "全回収なのに SWITCH_CUI が実行できない"
    );
    wm::g().inited = false;
}

/* ================================================================ */
/*  票 T8-3 W — ポーリング型の協調 yield (§7 D8)                     */
/*                                                                  */
/*  GUI 中に `kbd_trygetchar` を回す全画面 GFX プログラムは、カーネル */
/*  が第 3 の park 点で 1 周だけ止める (`APP_STATE_WAIT_POLL` = 4)。  */
/*  WM 側の規則は「**常に ready、ただし優先度は最下位**」— 入力群 /   */
/*  導出群 / `LAUNCH` 保留のどれも無い周にだけ起こす (複数なら ID     */
/*  昇順)。スロットが無くても `WAIT_KEY` と同じく forget しない。     */
/* ================================================================ */

/// `exec_app_state` が返す第 3 の park 点 (カーネル側は別票 T8-3 K)。
const ST_WAIT_POLL: i32 = 4;

/// ID 2 = GUI アプリ (スロット 0 + 窓 1 枚、park 中)、ID 3 = 端末から起動した
/// 全画面 GFX (スロット無し、ポーリングで譲った) の状態を作る。
fn one_gui_app_and_a_polling_app(shm: &crate::mocks::Shm) -> crate::wm::GuiState {
    use crate::{mocks, multiapp, session};
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let st = one_gui_app_state(shm);
    multiapp::on_start(2);
    multiapp::on_start(3);
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_POLL);
    mocks::set_kbd_pending(0);
    st
}

/* ---- T8-3 W 検査 1: スロット無しでも forget せず、空いた周に起こす ---- */
#[test]
fn a_slotless_polling_app_is_never_forgotten_and_runs_on_an_idle_round() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_and_a_polling_app(&shm);

    /* ポーリングは入力群でも導出群でもない (= D11 の数えには入らない)。 */
    assert!(!multiapp::input_ready(&st, 3), "WAIT_POLL が入力群に入った");
    assert!(!multiapp::derived_ready(&st, 3), "WAIT_POLL が導出群に入った");

    /* ID 2 に起床の理由が無い = 誰も ready でない周 → ここで起こす。 */
    assert_eq!(multiapp::pick(&st), 3, "空いた周に WAIT_POLL を起こさない");
    assert!(multiapp::resume_one(&mut st), "起こす相手が居るのに何もしない");
    /* 値はカーネルが -1 (キー無し) か 1 バイトで上書きするので WM は 0。 */
    assert_eq!(mocks::resume_calls(), vec![(3, 0)], "exec_resume の引数が違う");
    /* 票 K7 指摘 A と同じ: スロットが無いからといって落としてはならない。 */
    assert!(multiapp::is_tracked(3), "WAIT_POLL のアプリを forget した");
    assert!(mocks::kill_calls().is_empty(), "WAIT_POLL のアプリを畳んだ");
}

/* ---- T8-3 W 検査 2: 優先度は最下位 ---- */
#[test]
fn a_polling_app_is_picked_only_when_nothing_else_is_ready() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_and_a_polling_app(&shm);

    /* (1) 入力群が 1 本でも居れば、その周は選ばない。 */
    set_input_ready(&mut st, 2, true);
    assert_eq!(multiapp::pick(&st), 2, "入力群より WAIT_POLL を先に選んだ");
    set_input_ready(&mut st, 2, false);

    /* (2) 導出群 (`Configure` 未通知) でも同じ。 */
    set_derived_ready(&mut st, 2, true);
    assert_eq!(multiapp::pick(&st), 2, "導出群より WAIT_POLL を先に選んだ");
    set_derived_ready(&mut st, 2, false);

    /* (3) `LAUNCH` 保留の周も WM の番 (top-level の仕事が先)。 */
    st.launch_pending = true;
    assert_eq!(multiapp::pick(&st), 0, "LAUNCH 保留の周に WAIT_POLL を選んだ");
    st.launch_pending = false;

    /* (4) どれも無い周にだけ降りてくる。 */
    assert_eq!(multiapp::pick(&st), 3, "空いた周に WAIT_POLL を選ばない");
}

/* ---- T8-3 W 検査 3: 複数なら巡回の頭から、全画面中でも同じ ----
 *  票 T9 D5 で「`last_run` に乗せない ID 昇順」から「ポーリング群だけの
 *  巡回 (`poll_last` の次から)」に変わった。`poll_last` はまだ 0 なので
 *  最初の 1 本は従来どおり ID 昇順の先頭で、`last_run` は今も無関係。
 *  巡回そのものは T9-W 検査 7 が見る。 */
#[test]
fn polling_apps_are_picked_in_ascending_id_order_even_in_fullscreen() {
    use crate::{fullscreen, mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let st = one_gui_app_and_a_polling_app(&shm);
    multiapp::on_start(4);
    mocks::set_app_state(3, ST_WAIT_POLL);
    mocks::set_app_state(4, ST_WAIT_POLL);
    /* 入力群 / 導出群の起点 (`last_run`) はポーリング群の順に効かない。 */
    multiapp::set_last_run(3);
    assert_eq!(multiapp::pick(&st), 3, "ポーリング群が `last_run` に引きずられた");

    /* 全画面モード中も同じ (譲ってくるのは所有者の全画面プログラム本人)。 */
    fullscreen::arm(&[0u8; 48]);
    assert!(fullscreen::active(), "全画面モードに入っていない");
    assert_eq!(multiapp::pick(&st), 3, "全画面中に WAIT_POLL を起こさない");
    fullscreen::reset();
}

/* ---- T8-3 W 検査 4 (実機 F8 不合格の修正): ポーリングは「譲る理由」 ----
 *  PM 実測 2026-09-12: 端末 (ID 2) が `op_wait` の中に居ると、WM の 1 周は
 *  `wm_cycle` + `sys_halt` で、**`pick` を呼ぶ点 (WM top-level) へ行けない**。
 *  `WAIT_POLL` を「譲る理由」に数えないと端末は park せず、ポーリングの
 *  1 本は永久に起きない (画面は FPS: 0 で凍結、`ring3_poll_yield_count` 1)。
 *  「最下位」は `pick` の側 (検査 2) で守るので、ここで譲っても順は変わらない。 */
#[test]
fn a_polling_app_makes_the_running_app_yield_so_the_wm_can_resume_it() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_and_a_polling_app(&shm);
    /* ID 2 (端末) が走っている。ready なアプリは 1 本も無い。 */
    multiapp::mark_resumed(2);
    assert!(
        multiapp::should_park(&st, 2),
        "WAIT_POLL が居るのに譲らない = op_wait の中で halt し続ける"
    );
    /* 譲って top-level へ戻れば、最下位の 1 本が起きる (halt ではなく resume)。 */
    multiapp::note_parked(2, None);
    assert_eq!(multiapp::pick(&st), 3, "top-level に戻っても WAIT_POLL を選ばない");
    assert!(multiapp::resume_one(&mut st), "halt になった (resume されない)");
    assert_eq!(mocks::resume_calls(), vec![(3, 0)], "exec_resume の引数が違う");
}

/* ---- T8-3 W 検査 5: 据え置き (`input_streak`) は従来どおり効く ---- */
#[test]
fn a_polling_app_does_not_break_the_input_streak_deferral() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_and_a_polling_app(&shm);
    /* ID 2 に未読の待ち行列型があり、走っている。 */
    set_input_ready(&mut st, 2, true);
    multiapp::mark_resumed(2); /* `input_streak` を 0 に戻す */
    let mut n = 0;
    while n < 4 {
        assert!(
            !multiapp::should_park(&st, 2),
            "自分の入力の据え置き ({n} 回目) を WAIT_POLL が打ち切った"
        );
        n += 1;
    }
    assert!(
        multiapp::should_park(&st, 2),
        "据え置きの上限 (4) を超えても WAIT_POLL に譲らない"
    );
}

/* ---- T8-3 W 検査 6: 畳み (K7-W2) は `WAIT_POLL` にもそのまま効く ----
 *  `session` 側の述語 (`is_slotless` / `slotless_live` /
 *  `request_kill_slotless`) は**状態を見ない** (生きている & スロット無し)
 *  ので、第 3 の park 点が増えても 1 行も変えなくてよい。その確認。 */
#[test]
fn switch_cui_also_kills_a_slotless_polling_app() {
    use crate::{mocks, multiapp, session, wm};
    use os32api::gui::proto::GUI_SESSION_SWITCH_CUI;
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    {
        let g = wm::g();
        *g = one_gui_app_state(&shm);
        g.inited = true;
    }
    multiapp::on_start(2); /* 端末: スロット 0 + 窓 1 枚 */
    multiapp::on_start(3); /* 端末から起動した全画面 GFX: スロットも窓も無い */
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_POLL);
    mocks::set_kbd_pending(0);

    assert!(multiapp::slotless_live(wm::g()), "WAIT_POLL の 1 本を数えていない");
    assert_eq!(session::set_wm(wm::g(), GUI_SESSION_SWITCH_CUI, b"\0"), 0);
    assert!(
        multiapp::pending_top_level_work(),
        "Quit を配れない WAIT_POLL の kill が予約されていない"
    );

    wm::g().reclaim_owner(2);
    session::reclaim_owner(2);
    multiapp::on_owner_exit(2);
    assert!(
        !session::ready_to_run(wm::g()),
        "WAIT_POLL の 1 本を残したまま CUI へ切り替えようとした"
    );
    /* 予約の実行が先 — 「最下位で起こす」より `drain_top_level` が勝つ。 */
    assert!(multiapp::resume_one(wm::g()), "top-level が kill を実行しない");
    assert_eq!(mocks::kill_calls(), vec![3], "畳む相手が違う");
    assert!(
        mocks::resume_calls().is_empty(),
        "畳む相手を起こしてしまった: {:?}",
        mocks::resume_calls()
    );
    assert_eq!(multiapp::live_count(), 0, "全回収になっていない");
    wm::g().inited = false;
}

/* ---- T8-3 W 検査 7 (実機の復旧で判明): `exec_kill` の後も復帰する ----
 *  PM 実測 2026-09-12: park 中 (`WAIT_POLL`) の全画面アプリに CTRL+STOP を
 *  送ると `exec_kill` で畳まれ `g_gfx_owner` は 1 に戻るのに、**WM は全画面
 *  モードのまま `sys_halt` で待ち続け画面が凍った**。所有者の問い合わせ
 *  (`after_exec`) が `exec_start` / `exec_resume` の直後にしか無く、
 *  `exec_kill` の経路と「誰も ready でない → halt」の周回を通らないため。
 *  全画面中は入力もタイマも実質止まるので、自力では二度と復帰しない。 */
#[test]
fn killing_the_fullscreen_owner_restores_the_screen_on_the_next_round() {
    use crate::{fullscreen, mocks, multiapp, wm};
    use std::sync::atomic::Ordering;
    mocks::init();
    crate::session::clear();
    let shm = mocks::Shm::new();
    {
        let g = wm::g();
        *g = one_gui_app_state(&shm);
        g.inited = true;
    }
    multiapp::on_start(2); /* 端末 */
    multiapp::on_start(3); /* 全画面 GFX (ポーリングで park 中) */
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_POLL);
    mocks::set_kbd_pending(0);
    /* 所有者は ID 3 = 全画面モード。 */
    mocks::set_screen_owner(3);
    assert!(crate::after_exec(wm::g()), "全画面モードに入っていない");
    assert!(fullscreen::active());

    /* CTRL+STOP → 宛先は所有者 (D4d)。owner 1 からしか呼べないので予約だけ。 */
    multiapp::request_kill(3);
    let inits = mocks::GFX_INITS.load(Ordering::SeqCst);

    /* top-level の 1 周: `exec_kill` が通り、カーネルが所有者を 1 に戻す。 */
    mocks::set_screen_owner(1);
    assert!(multiapp::resume_one(wm::g()), "top-level が kill を実行しない");
    assert_eq!(mocks::kill_calls(), vec![3], "畳む相手が違う");
    /* ここで復帰まで済んでいなければ、誰も ready でない = 永久に halt。 */
    assert!(
        !fullscreen::active(),
        "所有者を畳んだのに全画面モードのまま (画面が凍る)"
    );
    assert_eq!(
        mocks::GFX_INITS.load(Ordering::SeqCst),
        inits + 1,
        "復帰の gfx_init を呼んでいない"
    );
    wm::g().inited = false;
}

/* ---- T8-3 W 検査 8: 復帰は top-level の仕事なので `op_wait` からは譲る ----
 *  `op_wait` の中は `res_owner_get()` がアプリ ID で、そこで `gfx_init` を
 *  呼ぶと `appslot_gfx_claim_check` が「宣言の無いアプリの要求」と見て
 *  **その端末を畳む**。だから復帰は呼ばず、park して top-level へ返す。 */
#[test]
fn a_pending_fullscreen_restore_makes_the_running_app_yield() {
    use crate::{fullscreen, mocks, multiapp, wm};
    mocks::init();
    crate::session::clear();
    let shm = mocks::Shm::new();
    let st = one_gui_app_state(&shm);
    multiapp::on_start(2);
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_kbd_pending(0);
    /* 全画面モードに入ったまま、所有者だけが WM に戻っている。 */
    fullscreen::arm(&[0u8; 48]);
    mocks::set_screen_owner(1);
    multiapp::mark_resumed(2); /* ID 2 が `op_wait` の中で走っている */

    let inits = mocks::GFX_INITS.load(std::sync::atomic::Ordering::SeqCst);
    assert!(
        multiapp::should_park(&st, 2),
        "復帰が要るのに譲らない = op_wait の中で永久に halt する"
    );
    assert_eq!(
        mocks::GFX_INITS.load(std::sync::atomic::Ordering::SeqCst),
        inits,
        "op_wait の文脈で gfx_init を呼んだ (端末が畳まれる)"
    );
    fullscreen::reset();
    wm::g().inited = false;
}

/* ================================================================ */
/*  全画面 GFX (票 T8 D4)                                            */
/*                                                                  */
/*  画面の所有者はカーネルが持つ (`gfx_screen_owner`、KAPI v48)。      */
/*  ここで試すのは WM の規律 — 所有者 ≠ 1 の間は描かない、戻ったら     */
/*  復帰する、CTRL+STOP の宛先、入口 (OS32X ヘッダ) の判定。          */
/* ================================================================ */

/// 全画面 GFX プログラムを 1 本起動して park させた状態を作る。
/// 戻り値は `run_program` の戻り値 (= app_id)。
fn launch_fullscreen(st: &mut crate::wm::GuiState, path: &[u8]) -> i32 {
    use crate::mocks;
    /* 起動するのは `--gfx` 宣言つきのプログラム。 */
    mocks::set_file(&mocks::os32x_header(crate::os32x::FLAG_GFX));
    /* park した (rc = 3) 後、画面は ID 3 のもの。 */
    *mocks::START_SCRIPT.lock().unwrap() = vec![3];
    mocks::set_screen_owner(3);
    let mut buf = [0u8; 256];
    buf[..path.len()].copy_from_slice(path);
    crate::run_program(st, &buf, crate::LaunchVia::Wm)
}

/* ---- (a) 所有者 ≠ 1 の間は 1 画素も出さない ---- */
#[test]
fn the_wm_draws_nothing_while_another_app_owns_the_screen() {
    use crate::{fullscreen, mocks, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm::g();
    *g = four_app_state(&shm);
    g.inited = true;

    let rc = launch_fullscreen(g, b"/usr/bin/gfx200_test.bin\0");
    assert_eq!(rc, 3, "park した全画面プログラムの app_id が返っていない");
    assert!(
        fullscreen::active() && fullscreen::owner() == 3,
        "所有者 ≠ 1 なのに全画面モードに入っていない (owner={})",
        fullscreen::owner()
    );

    /* ここから先は WM の描画を全部試す — 1 つでも VRAM へ出たら失格。 */
    let before = mocks::present_counts();
    let pixels = mocks::pixels();
    wm::composite_full(g);
    wm::composite_rect(g, wm::Rect::new(0, 0, 320, 200));
    wm::present_rect(g, wm::Rect::new(0, 0, 320, 200));
    wm::flush_present();
    g.dirty_screen(wm::Rect::new(0, 0, 640, 400));
    wm::flush_screen_dirty(g);
    /* WM の 1 周まるごと (FEP の描画・タスクバーの時計・カーソルを含む)。 */
    wm::wm_cycle(g, crate::input::Ctx::Standalone);
    assert_eq!(
        mocks::present_counts(),
        before,
        "全画面 GFX 中に WM が present した (プログラムの画を壊す)"
    );
    assert!(
        mocks::pixels() == pixels,
        "全画面 GFX 中に WM がバックバッファへ描いた"
    );

    /* マウスは誰にも配らない (タスクバー / Start / 窓を含めて無視)。 */
    let ui_before = (crate::startmenu::is_open(), crate::modal::is_open());
    let front_before = g.front_owner();
    *mocks::MOUSE.lock().unwrap() = (5, 5, 1); /* タスクバーではなく窓の上を押す */
    crate::input::capture(g, crate::input::Ctx::Standalone);
    assert_eq!(
        (crate::startmenu::is_open(), crate::modal::is_open()),
        ui_before,
        "全画面 GFX 中のクリックが WM の UI を開閉した"
    );
    assert_eq!(
        g.front_owner(),
        front_before,
        "全画面 GFX 中のクリックがフォーカスを動かした"
    );
    assert!(mocks::pixels() == pixels, "全画面 GFX 中にカーソルを描いた");
    g.inited = false;
    fullscreen::reset();
}

/* ---- (b) 所有者が 1 に戻ったら復帰する ---- */
#[test]
fn the_screen_comes_back_when_the_owner_returns_to_the_wm() {
    use crate::{fullscreen, mocks, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm::g();
    *g = four_app_state(&shm);
    g.inited = true;
    /* 復帰でパレットを戻せるよう、入る前の 16 色を控えさせる。 */
    unsafe { (os32api::api().gfx_set_palette)(1, 0xF, 0, 0) };

    launch_fullscreen(g, b"/usr/bin/gfx200_test.bin\0");
    assert!(fullscreen::active());
    /* プログラムがパレットを壊し、窓の dirty も消えた状態にしておく。 */
    unsafe { (os32api::api().gfx_set_palette)(1, 0, 0, 0xF) };
    let mut i = 0;
    while i < 2 {
        g.windows[i].dirty = wm::RectSet::EMPTY;
        i += 1;
    }
    let inits = mocks::GFX_INITS.load(std::sync::atomic::Ordering::SeqCst);

    /* プログラムが抜けてカーネルが所有者を WM に戻した。 */
    mocks::set_screen_owner(1);
    assert!(
        !crate::after_exec(g),
        "所有者が 1 に戻ったのに全画面モードのまま"
    );
    assert!(!fullscreen::active() && fullscreen::owner() == 0);
    assert_eq!(
        mocks::GFX_INITS.load(std::sync::atomic::Ordering::SeqCst),
        inits + 1,
        "復帰で gfx_init を呼んでいない (バックエンドが戻らない)"
    );
    /* 全クライアントに全面を描き直させる (W-1: 露出は dirty を生まない)。 */
    let mut k = 0;
    while k < 2 {
        assert!(
            g.windows[k].dirty.len > 0,
            "復帰で窓 {} が invalidate されていない",
            k
        );
        k += 1;
    }
    /* パレットは入る時点の控えに戻る (その後に G6 のシステム色が入るので、
     * 見るのは `gfx_set_palette` の呼び出し列)。 */
    assert!(
        mocks::palette_sets().contains(&(1, 0xF, 0, 0)),
        "復帰で入る時点の 16 色を戻していない: {:?}",
        mocks::palette_sets()
    );
    /* 全面を出し直した (門が開いている)。 */
    assert!(
        mocks::present_counts().1 >= 1,
        "復帰で全面 present をしていない"
    );
    g.inited = false;
    fullscreen::reset();
}

/* ---- (d) CTRL+STOP は所有者宛 ---- */
#[test]
fn ctrl_stop_targets_the_screen_owner_while_full_screen() {
    use crate::{fullscreen, mocks, multiapp, session, wm};
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    let g = wm::g();
    focus_app(g, 2); /* フォーカス窓 = 端末 (ID 2) */
    /* 画面を持っているのは端末が起動した ID 3。 */
    mocks::set_screen_owner(3);
    assert!(crate::after_exec(g), "所有者 3 を見て全画面モードに入らない");
    assert_eq!(fullscreen::owner(), 3);

    /* 走っているのは端末 (2)。宛先は所有者 (3) なので本人は畳まれない。 */
    assert!(
        !multiapp::abort_targets_current(g, 2),
        "全画面中の CTRL+STOP がフォーカス窓 (端末) に向いた"
    );
    /* top-level (単独ループ) の 1 周で振り替える。 */
    multiapp::redirect_abort(g, 0);
    assert!(multiapp::pending_top_level_work(), "予約が積まれていない");
    assert!(multiapp::resume_one(g), "top-level が予約を実行しない");
    assert_eq!(mocks::abort_clear_calls(), 1, "exec_abort_clear が 1 回でない");
    assert_eq!(mocks::kill_calls(), vec![3], "畳む相手が画面の所有者でない");
    assert!(!multiapp::is_tracked(3), "kill した所有者が表に残った");
    assert!(multiapp::is_tracked(2), "端末まで畳んでしまった");
    g.inited = false;
    fullscreen::reset();
}

/* ---- 入口 (D4): OS32X ヘッダの宣言で起動を決める ---- */
#[test]
fn the_entry_refuses_cpl0_programs_and_arms_full_screen_for_gfx() {
    use crate::{fullscreen, mocks, modal, os32x, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm::g();
    *g = four_app_state(&shm);
    g.inited = true;

    /* (1) `--cpl0` (v86) は起動しない。理由をモーダルで出す。 */
    mocks::set_file(&mocks::os32x_header(os32x::FLAG_FORCE_CPL0));
    let mut buf = [0u8; 256];
    let p = b"/usr/bin/v86.bin\0";
    buf[..p.len()].copy_from_slice(p);
    let rc = crate::run_program(g, &buf, crate::LaunchVia::Wm);
    assert!(
        mocks::start_calls().is_empty(),
        "CPL=0 強制のプログラムを GUI から起動した: {:?}",
        mocks::start_calls()
    );
    assert_eq!(rc, 0, "断った起動が `Launch failed` を誘発する戻り値になった");
    assert!(modal::is_open(), "`cui only:` を出していない");
    assert!(
        !fullscreen::active(),
        "起動しなかったのに全画面モードへ入った"
    );
    assert!(
        mocks::open_calls().iter().any(|c| c == b"/usr/bin/v86.bin"),
        "入口が OS32X ヘッダを読んでいない: {:?}",
        mocks::open_calls()
    );
    /* 次の試験へ持ち越さない (モーダルは大域)。 */
    modal::state().used = false;

    /* (2) `--gfx` は `exec_start` の**前**に印が立つ (所有者が付くまでの隙間)。 */
    mocks::init();
    let g = wm::g();
    *g = four_app_state(&shm);
    g.inited = true;
    launch_fullscreen(g, b"/usr/bin/blit_test.bin\0");
    assert_eq!(mocks::start_calls().len(), 1, "宣言つきは起動する");
    assert!(fullscreen::active(), "宣言を見て全画面モードに入らない");

    /* (3) どちらも無いプログラムは従来どおり (印も立たない)。 */
    mocks::init();
    let g = wm::g();
    *g = four_app_state(&shm);
    g.inited = true;
    mocks::set_file(&mocks::os32x_header(0x0002 /* RING3 だけ */));
    let mut buf = [0u8; 256];
    let p = b"/usr/bin/gui_demo.bin\0";
    buf[..p.len()].copy_from_slice(p);
    assert_eq!(crate::run_program(g, &buf, crate::LaunchVia::Wm), 2, "普通のアプリが起動しない");
    assert!(!fullscreen::active(), "宣言の無いアプリで全画面モードに入った");
    g.inited = false;
    fullscreen::reset();
}

/* ================================================================ */
/*  票 T9-W — 起動要求表 (KAPI v49) を WM が top-level で仲介する    */
/*                                                                  */
/*  `docs/archive/gui_v13/TASK_T9_sh.md` §1 D3 (3)(4) / D5 / D8、      */
/*  §10 non-blocker 2 / 4。表そのものの遷移は実物のカーネルコードで  */
/*  検査済み (`tools/tests/launch_host.c`) なので、ここで見るのは    */
/*  **WM が「いつ・何を・どの順で」渡したか**だけ。                  */
/* ================================================================ */

/// `sdk/include/os32/os32_kapi_shared.h` の `LAUNCH_KIND_*` (正典はそちら)。
const LK_LAUNCH: i32 = 1;
const LK_KILL: i32 = 2;
/// `OS32_ERR_STALE` (KILL の `launch_report` が返す正常な断り、§10 non-blocker 2)。
const ERR_STALE: i32 = -11;

/// WM が top-level に居て、GUI アプリ 1 本 (ID 2) が窓を持っている状態。
fn wm_at_top_level(shm: &crate::mocks::Shm) -> &'static mut crate::wm::GuiState {
    use crate::{session, wm};
    session::clear(); /* 前の試験の SessionAction を持ち越さない */
    let g = wm::g();
    *g = one_gui_app_state(shm);
    g.inited = true;
    g
}

/* ---- T9-W 検査 1: LAUNCH は `run_program` を通り、戻りをそのまま返す ---- */
#[test]
fn a_taken_launch_goes_through_run_program_and_reports_the_child() {
    use crate::{mocks, modal, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm_at_top_level(&shm);
    *mocks::START_SCRIPT.lock().unwrap() = vec![3];
    mocks::push_take(7, LK_LAUNCH, 0, b"/usr/bin/kbd_echo.bin");

    assert!(crate::drain_launch_requests(g), "要求が積まれているのに何もしない");

    assert_eq!(
        mocks::start_calls(),
        vec![b"/usr/bin/kbd_echo.bin".to_vec()],
        "`run_program` (= `exec_start`) を通していない"
    );
    assert_eq!(
        mocks::report_calls(),
        vec![(7, 3)],
        "`run_program` の戻りをそのまま `launch_report` へ渡していない"
    );
    /* `run_program` を通った = 譲り合いの表にも載る (`begin_start`/`end_start`)。 */
    assert!(multiapp::is_tracked(3), "起動した子が譲り合いの表に居ない");
    assert_eq!(mocks::take_calls(), 1, "`launch_pending` が 0 なのに取りに行った");
    assert!(!modal::is_open(), "要求表経由の起動でモーダルを出した");
    g.inited = false;
}

/* ---- T9-W 検査 2: 失敗は `FAILED(rc)` として返すだけ (モーダルを出さない) ----
 *  票 D3 (3): 要求者が `launch_poll` で `FAILED(rc)` を受けて自分で表示する。
 *  WM が「Launch failed」を出すと、端末の前に WM のモーダルが割り込む。 */
#[test]
fn a_failed_launch_is_reported_without_a_wm_modal() {
    use crate::{mocks, modal};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm_at_top_level(&shm);
    *mocks::START_SCRIPT.lock().unwrap() = vec![-13 /* OS32_ERR_FULL */];
    mocks::push_take(8, LK_LAUNCH, 0, b"/usr/bin/nope.bin");

    assert!(crate::drain_launch_requests(g));

    assert_eq!(mocks::report_calls(), vec![(8, -13)], "失敗の rc を返していない");
    assert!(!modal::is_open(), "要求表経由の失敗で `Launch failed` を出した");
    g.inited = false;
}

/* ---- T9-W 検査 3: KILL は `exec_kill` → FREE を**全部** forget (D8) ----
 *  `exec_kill` は子孫ごと末尾から畳む (K)。WM の表に残すと、その ID が
 *  再利用されたときに「生きている別人」として起こしにいく。 */
#[test]
fn a_taken_kill_folds_the_chain_and_forgets_every_freed_id() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm_at_top_level(&shm);
    multiapp::on_start(3);
    multiapp::on_start(4);
    mocks::set_kill_frees(&[3, 4]); /* 4 を畳むと 3 も道連れ (連鎖) */
    mocks::set_report_ret(ERR_STALE); /* 回収通知で先に DONE (§10 non-blocker 2) */
    mocks::push_take(9, LK_KILL, 4, b"");

    assert!(crate::drain_launch_requests(g));

    assert_eq!(mocks::kill_calls(), vec![4], "KILL の相手が要求の `arg` でない");
    assert_eq!(mocks::report_calls(), vec![(9, 0)], "KILL の完了を返していない");
    assert!(!multiapp::is_tracked(4), "畳んだ ID を表から落としていない");
    assert!(!multiapp::is_tracked(3), "道連れの子孫を表から落としていない");
    /* STALE は正常 — 再試行すると要求表を 2 度取りに行く。 */
    assert_eq!(mocks::take_calls(), 1, "`OS32_ERR_STALE` を再試行した");
    assert!(mocks::start_calls().is_empty(), "KILL で何かを起動した");
    g.inited = false;
}

/* ---- T9-W 検査 4: Start → Run... の経路は従来どおりモーダルを出す ---- */
#[test]
fn the_session_launch_path_still_shows_the_failure_modal() {
    use crate::{mocks, modal, session};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm_at_top_level(&shm);
    *mocks::START_SCRIPT.lock().unwrap() = vec![-13];
    assert_eq!(session::set_wm_launch(g, b"/usr/bin/nope.bin\0"), 0);

    assert!(crate::session_handoff(g), "handoff がデスクトップを畳んだ");

    assert!(modal::is_open(), "Start → Run... の失敗でモーダルが出ない");
    assert!(mocks::report_calls().is_empty(), "`session_launch` が表へ報告した");
    modal::state().used = false;
    g.inited = false;
}

/* ---- T9-W 検査 5: 起動要求は「譲る理由」 (D3 (2)) ----
 *  `launch_take` は owner 1 専用 = WM top-level でしか呼べない。走って
 *  いるアプリが park しない限り top-level へ戻る道が無いので、要求が
 *  積まれている間は譲らせる (`should_park` の (a))。 */
#[test]
fn a_pending_launch_request_makes_the_running_app_yield() {
    use crate::{mocks, multiapp, session};
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    let st = one_gui_app_state(&shm);
    multiapp::on_start(2);
    multiapp::mark_resumed(2);

    assert!(
        !multiapp::should_park(&st, 2),
        "1 本だけ・要求も無い周で譲った (回帰ゼロが壊れている)"
    );
    mocks::set_launch_pending(1);
    assert!(
        multiapp::should_park(&st, 2),
        "起動要求が積まれているのに譲らない = `launch_take` へ永久に行けない"
    );
}

/* ---- T9-W 検査 6: 起動要求のある周は WM の番 (`pick_poll` は降りない) ---- */
#[test]
fn a_pending_launch_request_stops_the_wm_from_resuming_a_polling_app() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let st = one_gui_app_and_a_polling_app(&shm);

    assert_eq!(multiapp::pick(&st), 3, "空いた周に WAIT_POLL を起こさない");
    mocks::set_launch_pending(1);
    assert_eq!(
        multiapp::pick(&st),
        0,
        "起動要求のある周に WAIT_POLL を起こした (top-level の仕事が後回し)"
    );
}

/* ---- T9-W 検査 7: `WAIT_POLL` 群は巡回、同じ tick に 2 回起こさない (D5) ----
 *  sh の `sys_yield` と子の `kbd_trygetchar` が同時に `WAIT_POLL` でも
 *  tick ごとに交互に走らせる。ID 昇順の固定だと若い方が走り続ける。 */
#[test]
fn polling_apps_are_woken_in_rotation_and_only_once_per_tick() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_and_a_polling_app(&shm);
    multiapp::on_start(4);
    mocks::set_app_state(3, ST_WAIT_POLL);
    mocks::set_app_state(4, ST_WAIT_POLL);
    mocks::set_tick(100);

    assert_eq!(multiapp::pick(&st), 3, "巡回の 1 本目が ID 昇順でない");
    assert!(multiapp::resume_one(&mut st), "起こす相手が居るのに何もしない");
    assert_eq!(
        multiapp::pick(&st),
        4,
        "同じ tick に同じ 1 本を選び直した (もう一方が飢える)"
    );
    assert!(multiapp::resume_one(&mut st));
    assert_eq!(
        multiapp::pick(&st),
        0,
        "この tick で起こし済みの相手をもう一度起こした"
    );
    assert!(
        !multiapp::resume_one(&mut st),
        "起こす相手が居ないのに `sys_halt` へ落ちない"
    );

    /* 次の tick で「起こし済み」は消え、巡回は前回の次 (4 の次 = 3) から。 */
    mocks::set_tick(101);
    assert_eq!(multiapp::pick(&st), 3, "tick が進んでも起こし直さない");
    assert_eq!(
        mocks::resume_calls(),
        vec![(3, 0), (4, 0)],
        "起こした順が巡回になっていない"
    );
}

/* ---- T9-W 検査 8: CTRL+STOP は連鎖の末尾へ (D8) ----
 *  端末 (2) → sh (3) → 子 (4)。フォーカスは端末だが、畳むのは末尾 1 本。 */
#[test]
fn ctrl_stop_is_redirected_to_the_tail_of_the_launch_chain() {
    use crate::{mocks, multiapp, session, wm};
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    let g = wm::g();
    focus_app(g, 2); /* フォーカス窓 = 端末 */
    multiapp::on_start(4);
    mocks::set_launch_child(2, 3);
    mocks::set_launch_child(3, 4);

    assert_eq!(multiapp::abort_target(g), 4, "宛先を連鎖の末尾へ解決していない");
    assert!(
        !multiapp::abort_targets_current(g, 2),
        "端末 (連鎖の頭) が自分の syscall 出口で畳まれる"
    );
    assert!(
        !multiapp::abort_targets_current(g, 3),
        "連鎖の途中 (sh) が畳まれる"
    );
    assert!(
        multiapp::abort_targets_current(g, 4),
        "末尾が走っているのに畳ませない"
    );

    /* 走っているのが端末なら振り替え → top-level で末尾 1 本だけ畳む。 */
    multiapp::redirect_abort(g, 2);
    mocks::set_kill_frees(&[4]);
    assert!(multiapp::resume_one(g), "top-level が予約を実行しない");
    assert_eq!(mocks::kill_calls(), vec![4], "畳む相手が連鎖の末尾でない");
    assert!(!multiapp::is_tracked(4), "畳んだ末尾が表に残った");
    assert!(
        multiapp::is_tracked(2) && multiapp::is_tracked(3),
        "端末 / sh まで巻き添えで畳んだ"
    );
    g.inited = false;
}

/* ---- T9-W 検査 9: 壊れた表が環を作っても宛先の解決は止まる (D8) ---- */
#[test]
fn a_cycle_in_the_launch_chain_does_not_hang_the_ctrl_stop_lookup() {
    use crate::{mocks, multiapp, session, wm};
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    let g = wm::g();
    focus_app(g, 2);
    mocks::set_launch_child(2, 3);
    mocks::set_launch_child(3, 2); /* 環 */

    let t = multiapp::abort_target(g);
    assert!(t == 2 || t == 3, "環のある表で宛先が壊れた: {t}");
    g.inited = false;
}

/* ================================================================ */
/*  票 T9-W — 実装レビュー 第 1 版 (2026-09-13) の blocker 2 件      */
/* ================================================================ */

/* ---- T9-W 検査 10 (blocker 1): 入口の拒否は `DONE` ではなく `FAILED` ----
 *  反例: 端末の中の `sh` で `exec /usr/bin/v86.bin`。`run_program` は
 *  `cui only:` を出して `RUN_REFUSED` = 0 を返す。そのまま
 *  `launch_report(token, 0)` へ渡すと表は `DONE` になり、`sh` は
 *  「起動して正常終了した」と読んでしまう (何も起きていないのに)。
 *  要求表経由ではモーダルも出さない — 出すのは要求者 (`sh`) の仕事。 */
#[test]
fn a_cui_only_program_from_the_launch_table_is_reported_as_a_failure() {
    use crate::{mocks, modal};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm_at_top_level(&shm);
    /* `OS32X_FLAG_CUI_ONLY` = 0x0010 (`os32x.rs` の `FLAG_CUI_ONLY`)。 */
    mocks::set_file(&mocks::os32x_header(0x0010));
    mocks::push_take(11, LK_LAUNCH, 0, b"/usr/bin/v86.bin");

    assert!(crate::drain_launch_requests(g));

    assert!(
        mocks::start_calls().is_empty(),
        "CUI 専用プログラムを GUI から起動した: {:?}",
        mocks::start_calls()
    );
    let r = mocks::report_calls();
    assert_eq!(r.len(), 1, "報告が 1 件でない: {r:?}");
    assert_eq!(r[0].0, 11, "別の token へ報告した");
    assert!(
        r[0].1 < 0,
        "入口の拒否を `DONE` (rc = 0 = 正常終了) として報告した: {r:?}"
    );
    assert!(!modal::is_open(), "要求表経由の拒否で WM がモーダルを出した");
    g.inited = false;
}

/* ---- T9-W 検査 11 (blocker 1 の裏): WM の経路は従来どおりモーダル ---- */
#[test]
fn a_cui_only_program_from_the_start_menu_still_opens_the_modal() {
    use crate::{mocks, modal, session};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = wm_at_top_level(&shm);
    mocks::set_file(&mocks::os32x_header(0x0010));
    assert_eq!(session::set_wm_launch(g, b"/usr/bin/v86.bin\0"), 0);

    assert!(crate::session_handoff(g));

    assert!(mocks::start_calls().is_empty(), "CUI 専用を起動した");
    assert!(modal::is_open(), "Start → Run... で `cui only:` を出していない");
    assert!(mocks::report_calls().is_empty(), "WM の経路が要求表へ報告した");
    modal::state().used = false;
    g.inited = false;
}

/* ---- T9-W 検査 12 (blocker 2): tick 境界をまたいでも 2 回起こさない ----
 *  反例: tick N で `pick` が sh を選ぶ → 再開する前に PIT が N+1 へ進む →
 *  「起こし済み」を N の集合へ記録 → 次の `pick` は tick が変わったので
 *  集合を捨てる → **同じ N+1 のうちに sh をもう一度**起こす (子が飢える)。
 *  記録は「選んだ時刻」ではなく **実際に再開した時刻** に紐づける。 */
#[test]
fn a_tick_boundary_between_pick_and_resume_does_not_allow_a_second_wake() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let mut st = one_gui_app_and_a_polling_app(&shm);
    /* `resume_one` の中で: 1 回目 = `pick` (tick 100)、2 回目 = 再開の記録
     * (tick 101 = 選んでから PIT が 1 つ進んだ)。以後は 101 のまま。 */
    mocks::set_tick_script(&[100, 101]);

    assert!(multiapp::resume_one(&mut st), "起こす相手が居るのに何もしない");
    assert_eq!(mocks::resume_calls(), vec![(3, 0)], "起こした相手が違う");

    /* 実際に走ったのは tick 101。同じ 101 の周でもう一度起こしてはいけない
     * (起こす相手が居ない = 単独ループは `sys_halt` で次の tick を待つ)。 */
    assert_eq!(
        multiapp::pick(&st),
        0,
        "tick 境界をまたいで同じ 1 本を同じ tick に 2 回起こした"
    );

    /* tick が進めば当然また起こす。 */
    mocks::set_tick(102);
    assert_eq!(multiapp::pick(&st), 3, "次の tick で起こし直さない");
}

/* ================================================================ */
/*  票 T9-W — 実機受入 S6 不合格の修正 (2026-09-13)                  */
/*                                                                  */
/*  端末 (2) → `sh` (3、`sys_yield` で `WAIT_POLL`) → `kbd_echo`     */
/*  (4、`WAIT_KEY`) の連鎖。`sh` が譲っている間、端末は               */
/*  `should_park` で park しているので **WM は `standalone_loop`     */
/*  (top-level、ウィンドウモード)** で回る。`abort_seen` を見る点が   */
/*  `handler.rs` (op_wait の中) と `lib.rs` の全画面分岐しか無く、    */
/*  この周の CTRL+STOP は捨てられていた (実機で 3 回送っても          */
/*  `ring3_abort_count` = 0、何も畳まれない)。                        */
/* ================================================================ */

/// `exec_app_state` が返す「kbd 待ち」(`APP_STATE_WAIT_KEY`)。
const ST_WAIT_KEY_T9: i32 = 3;

/// 端末 (2、窓つき) → sh (3、`WAIT_POLL`) → 子 (4、`WAIT_KEY`) の連鎖を作る。
fn terminal_sh_child_chain(shm: &crate::mocks::Shm) -> &'static mut crate::wm::GuiState {
    use crate::{mocks, multiapp, session, wm};
    session::clear();
    two_app_global(shm);
    let g = wm::g();
    focus_app(g, 2); /* フォーカス窓 = 端末 */
    multiapp::on_start(4);
    mocks::set_app_state(2, ST_PARKED);
    mocks::set_app_state(3, ST_WAIT_POLL);
    mocks::set_app_state(4, ST_WAIT_KEY_T9);
    mocks::set_launch_child(2, 3);
    mocks::set_launch_child(3, 4);
    g
}

/* ---- T9-W 検査 13: ウィンドウモードの top-level でも末尾を畳む ---- */
#[test]
fn ctrl_stop_at_the_window_mode_top_level_folds_the_tail_of_the_chain() {
    use crate::{fullscreen, mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = terminal_sh_child_chain(&shm);
    assert!(!fullscreen::active(), "ウィンドウモードの前提が崩れている");

    /* 1 回目: 連鎖の末尾 = 子 (4) だけを畳む。 */
    g.abort_seen = true;
    mocks::set_kill_frees(&[4]);
    assert_eq!(
        crate::top_level_abort(g),
        4,
        "ウィンドウモードの top-level で CTRL+STOP が捨てられた (受入 S6)"
    );
    assert!(!g.abort_seen, "`abort_seen` を降ろしていない (次の周で二重に効く)");
    assert_eq!(mocks::kill_calls(), vec![4], "畳む相手が連鎖の末尾でない");
    assert_eq!(
        mocks::abort_clear_calls(),
        1,
        "WM に載った要求を降ろしていない (次の syscall で WM が畳まれる)"
    );
    assert!(!multiapp::is_tracked(4), "畳んだ末尾が表に残った");
    assert!(
        multiapp::is_tracked(2) && multiapp::is_tracked(3),
        "端末 / sh まで巻き添えで畳んだ"
    );

    /* 2 回目: 子の回収通知で `launch_child(3)` は 0 になっている = 末尾は sh。 */
    mocks::set_launch_child(3, 0);
    g.abort_seen = true;
    mocks::set_kill_frees(&[3]);
    assert_eq!(crate::top_level_abort(g), 3, "2 回目で次の末尾 (sh) を畳まない");
    assert_eq!(mocks::kill_calls(), vec![4, 3], "畳んだ順が連鎖の末尾からでない");
    assert!(!multiapp::is_tracked(3), "畳んだ sh が表に残った");
    assert!(multiapp::is_tracked(2), "端末まで畳んだ");
    g.inited = false;
}

/* ---- T9-W 検査 14: `redirect_abort` の予約と二重にしない ----
 *  `op_wait` の中で見た周は `handler.rs` が `redirect_abort` で予約を積み、
 *  同じ 1 周の `resume_one` → `drain_top_level` が実行する。予約が立って
 *  いる間に top-level が直接畳むと、連鎖の末尾を 2 本ぶん畳んでしまう。 */
#[test]
fn a_pending_redirect_reservation_is_not_executed_twice_at_top_level() {
    use crate::{mocks, multiapp};
    mocks::init();
    let shm = mocks::Shm::new();
    let g = terminal_sh_child_chain(&shm);

    /* 端末 (2) が走っている周に `handler.rs` が予約を積んだ状態。 */
    multiapp::redirect_abort(g, 2);
    assert!(multiapp::pending_top_level_work(), "予約が積まれていない");

    g.abort_seen = true;
    assert_eq!(crate::top_level_abort(g), 0, "予約があるのに top-level が直接畳んだ");
    assert!(!g.abort_seen, "`abort_seen` を降ろしていない");
    assert!(mocks::kill_calls().is_empty(), "予約と直接実行で 2 回畳んだ");
    assert_eq!(mocks::abort_clear_calls(), 0, "予約の実行前に要求を降ろした");

    /* 予約は従来どおり `drain_top_level` が実行する (末尾 1 本)。 */
    mocks::set_kill_frees(&[4]);
    assert!(multiapp::resume_one(g), "top-level が予約を実行しない");
    assert_eq!(mocks::kill_calls(), vec![4], "予約の実行で畳む相手が違う");
    assert_eq!(mocks::abort_clear_calls(), 1);
    g.inited = false;
}

/* ---- T9-W 検査 15: 宛先が居なければ要求を降ろすだけ ----
 *  窓もスロットも全画面の所有者も無い周 (`abort_target` = 0)。何も畳まず、
 *  WM に載ったかもしれない要求だけ降ろす — 降ろさないと次に WM が syscall の
 *  出口を通ったときに WM 自身が畳まれる。 */
#[test]
fn ctrl_stop_with_no_target_only_clears_the_request() {
    use crate::{mocks, multiapp, session, wm};
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    let g = wm::g();
    *g = wm::GuiState::NEW; /* 窓もスロットも無い */
    g.shm_base = shm.base();
    g.inited = true;
    assert_eq!(multiapp::abort_target(g), 0, "宛先が居ない前提が崩れている");

    g.abort_seen = true;
    assert_eq!(crate::top_level_abort(g), 0, "宛先が無いのに何かを畳んだ");
    assert!(!g.abort_seen, "`abort_seen` を降ろしていない");
    assert!(mocks::kill_calls().is_empty(), "宛先が無いのに `exec_kill` した");
    assert_eq!(mocks::abort_clear_calls(), 1, "要求を降ろしていない");
    g.inited = false;
}

/* ================================================================ */
/*  票 T9-W — 受入 S6 の 3 回目 (本人宛て) が不安定だった件の修正    */
/*                                                                  */
/*  実機 2026-09-13、3 回中 2 回失敗。IRQ1 の着地点で経路が分かれる: */
/*  (a) アプリが `op_wait` の中 → カーネルが `abort_req` を立てる →  */
/*      `break` → syscall 出口で畳まれる (成功した回)               */
/*  (b) アプリが自分のタイマ処理を CPL=3 で走らせている最中 →        */
/*      `abort_req` は立たない (暴走ではない) → raw リング経由で     */
/*      `abort_seen` だけ → `break` しても出口に要求が無く**消える** */
/*  → 本人宛ても予約に統一し、top-level の `drain_top_level` が      */
/*    `exec_abort_clear` → `exec_kill(本人)` を行う。                */
/* ================================================================ */

/* ---- T9-W 検査 16: 宛先が無い周は要求を降ろす予約だけ (誰も死なない) ---- */
#[test]
fn ctrl_stop_in_op_wait_with_no_target_reserves_only_the_clear() {
    use crate::{mocks, multiapp, session, wm};
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    let g = wm::g();
    /* 窓を全部畳む = `front_owner()` が 0 = 宛先なし。表の 2 本は生かす。 */
    g.windows[0] = wm::Win::EMPTY;
    g.windows[1] = wm::Win::EMPTY;
    g.z_count = 0;
    assert_eq!(multiapp::abort_target(g), 0, "宛先が無い前提が崩れている");
    g.abort_seen = true;

    drive_op_wait(2, 3);

    assert!(
        multiapp::pending_top_level_work(),
        "宛先が無くても要求の取り消しは予約しなければならない"
    );
    assert!(multiapp::resume_one(wm::g()), "top-level が予約を実行しない");
    assert_eq!(mocks::abort_clear_calls(), 1, "要求を降ろしていない");
    assert!(
        mocks::kill_calls().is_empty(),
        "宛先が無いのに畳んだ: {:?}",
        mocks::kill_calls()
    );
    assert!(
        multiapp::is_tracked(2) && multiapp::is_tracked(3),
        "宛先が無いのに誰かを表から落とした"
    );
    wm::g().inited = false;
}

/* ---- T9-W 検査 17: 宛先が別 (子あり) は従来どおり連鎖の末尾 ---- */
#[test]
fn ctrl_stop_in_op_wait_with_a_child_still_folds_the_tail() {
    use crate::{mocks, multiapp, session, wm};
    mocks::init();
    session::clear();
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    let g = wm::g();
    focus_app(g, 2); /* フォーカス窓 = 端末 (走っている本人でもある) */
    multiapp::on_start(4);
    /* 端末 (2) → sh (3) → 子 (4)。宛先は末尾なので**本人ではない**。 */
    mocks::set_launch_child(2, 3);
    mocks::set_launch_child(3, 4);
    assert!(
        !multiapp::abort_targets_current(g, 2),
        "子を持つ端末が本人宛てになっている"
    );
    g.abort_seen = true;

    drive_op_wait(2, 3);

    assert!(multiapp::pending_top_level_work(), "予約が積まれていない");
    mocks::set_kill_frees(&[4]);
    assert!(multiapp::resume_one(wm::g()), "top-level が予約を実行しない");
    assert_eq!(mocks::abort_clear_calls(), 1, "要求を降ろしていない");
    assert_eq!(mocks::kill_calls(), vec![4], "畳む相手が連鎖の末尾でない");
    assert!(
        multiapp::is_tracked(2) && multiapp::is_tracked(3),
        "端末 / sh まで巻き添えで畳んだ"
    );
    wm::g().inited = false;
}

// §5-4: same source path, with the old arg=0 contract as a positive reproducer.
fn stop_exit_scenario(kind: u32, deferred: bool) -> Vec<i32> {
    use crate::{handler, input, mocks, multiapp, wm};
    use os32api::gui::proto::GUI_OP_OWNER_EXIT;
    mocks::init();
    multiapp::reset();
    let shm = mocks::Shm::new();
    two_app_global(&shm);
    let stop = 0x60 | 0x100 | (0x10 << 9);
    if deferred {
        wm::g().pending_raw[..3].copy_from_slice(&[stop, 0x70, stop & !0x100]);
        wm::g().pending_raw_n = 3;
    } else {
        wm::g().abort_seen = true;
    }
    handler::gshell_gui_handler(GUI_OP_OWNER_EXIT, kind, 3);
    if kind == 3 {
        assert!(!wm::g().abort_seen);
        if deferred {
            assert_eq!(&wm::g().pending_raw[..wm::g().pending_raw_n], &[0x70, stop & !0x100]);
        }
    }
    input::capture(wm::g(), input::Ctx::Standalone);
    crate::top_level_abort(wm::g());
    let calls = mocks::kill_calls();
    wm::g().inited = false;
    calls
}

#[test]
fn kstop_old_owner_exit_contract_reproduces_second_kill() {
    assert_eq!(stop_exit_scenario(0, false), vec![2]);
    assert_eq!(stop_exit_scenario(0, true), vec![2]);
}

#[test]
fn kstop_aborted_exit_consumes_captured_and_deferred_stop() {
    assert!(stop_exit_scenario(3, false).is_empty());
    assert!(stop_exit_scenario(3, true).is_empty());
}

#[test]
fn kstop_completion_and_s6_resolve_foreground_once() {
    use crate::{input, mocks, multiapp, wm};
    // STOP completion and ordinary polling both expose WAIT_POLL to the WM.
    for current in [2, 3] {
        mocks::init();
        multiapp::reset();
        let shm = mocks::Shm::new();
        two_app_global(&shm);
        multiapp::mark_resumed(current);
        multiapp::end_start(current);
        mocks::set_app_state(2, 4);
        mocks::set_app_state(3, 4);
        mocks::set_kill_frees(&[3]);
        mocks::push_rawkeys(&[0x60 | 0x100 | (0x10 << 9)]);
        input::capture(wm::g(), input::Ctx::Pump);
        crate::top_level_abort(wm::g());
        crate::top_level_abort(wm::g());
        assert_eq!(mocks::kill_calls(), vec![3]);
        assert_eq!(mocks::abort_clear_calls(), 1);
        assert!(multiapp::is_tracked(2));
        // current!=target: culprit 2 remains eligible, hence can block WM again.
        assert!(multiapp::resume_one(wm::g()));
        assert_eq!(mocks::resume_calls().last().unwrap().0, 2);
        wm::g().inited = false;
    }
}

#[test]
fn kstop_start_return_consumes_stop_before_any_resume() {
    use crate::{mocks, multiapp, wm};
    for form in 0..3 {
        mocks::init();
        let shm = mocks::Shm::new();
        two_app_global(&shm);
        multiapp::end_start(3);
        mocks::set_app_state(2, 4);
        mocks::set_app_state(3, 4);
        mocks::set_kill_frees(&[3]);
        let stop = 0x60 | 0x100 | (0x10 << 9);
        match form {
            0 => mocks::push_rawkeys(&[stop]),
            1 => { wm::g().pending_raw[0] = stop; wm::g().pending_raw_n = 1; }
            _ => wm::g().abort_seen = true,
        }
        assert!(multiapp::resume_one(wm::g()));
        assert_eq!(mocks::kill_calls(), vec![3]);
        assert!(mocks::resume_calls().is_empty());
        assert!(multiapp::is_tracked(2));
        wm::g().inited = false;
    }
}

#[test]
fn kstop_without_a_foreground_resumes_the_slotless_culprit() {
    use crate::{mocks, multiapp, wm};
    mocks::init();
    let shm = mocks::Shm::new();
    let st = wm::g();
    *st = wm::GuiState::NEW;
    st.shm_base = shm.base();
    st.inited = true;
    multiapp::on_start(2);
    multiapp::end_start(2);
    mocks::set_app_state(2, 4);
    mocks::push_rawkeys(&[0x60 | 0x100 | (0x10 << 9)]);
    assert!(multiapp::resume_one(st));
    assert!(mocks::kill_calls().is_empty());
    assert_eq!(mocks::abort_clear_calls(), 1);
    assert_eq!(mocks::resume_calls(), vec![(2, 0)]);
    st.inited = false;
}

#[test]
fn kstop_full_event_ring_drains_raw_and_counts_overflow() {
    use crate::{input, mocks, multiapp, ring, slot, wm};
    use os32api::gui::proto::{GUI_RING_CAPACITY, GUI_HDR_FLAG_OVERFLOW};
    for deferred in [false, true] {
        mocks::init();
        multiapp::reset();
        let shm = mocks::Shm::new();
        two_app_global(&shm);
        let st = wm::g();
        // Foreground app 3 uses slot 1. Leave fewer than the four reserved events.
        let mut h = slot::read_header(st, 1);
        h.ring_tail = h.ring_head.wrapping_add(GUI_RING_CAPACITY as u16 - 3);
        h.dropped = 0;
        h.flags = 0;
        slot::write_header(st, 1, &h);
        let stop = 0x60 | 0x100 | (0x10 << 9);
        if deferred {
            st.pending_raw[..3].copy_from_slice(&[0x110, stop, 0x10]);
            st.pending_raw_n = 3;
        }
        mocks::push_rawkeys(&[0x111, 0x11, stop]);
        input::capture_keyboard(st, input::Ctx::Pump);
        assert!(!st.abort_seen);
        input::capture_keyboard(st, input::Ctx::Standalone);
        assert!(st.abort_seen);
        assert_eq!(st.pending_raw_n, if deferred { 1 } else { 0 });
        assert_eq!(ring::space(st, 1), 3);
        let h = slot::read_header(st, 1);
        assert_eq!(h.dropped, if deferred { 1 } else { 2 });
        assert_ne!(h.flags & GUI_HDR_FLAG_OVERFLOW, 0);
        multiapp::end_start(3);
        mocks::set_app_state(2, 4);
        mocks::set_app_state(3, 4);
        mocks::set_kill_frees(&[3]);
        crate::top_level_abort(st);
        crate::top_level_abort(st);
        assert_eq!(mocks::kill_calls(), vec![3]);
        st.inited = false;
    }
}

#[test]
fn kstop_full_ring_preserves_following_input_for_next_foreground() {
    use crate::{input, mocks, multiapp, ring, slot, wm};
    use os32api::gui::proto::{GuiEvent, GUI_EV_KEY, GUI_RING_CAPACITY};
    for deferred in [false, true] {
        mocks::init();
        multiapp::reset();
        let shm = mocks::Shm::new();
        two_app_global(&shm);
        let st = wm::g();
        let mut h = slot::read_header(st, 1);
        h.ring_tail = h.ring_head.wrapping_add(GUI_RING_CAPACITY as u16 - 3);
        h.dropped = 0;
        slot::write_header(st, 1, &h);
        let stop = 0x60 | 0x100 | (0x10 << 9);
        let keys = [0x111, stop, 0x110, 0x10];
        if deferred {
            st.pending_raw[..4].copy_from_slice(&keys);
            st.pending_raw_n = 4;
        } else {
            mocks::push_rawkeys(&keys);
        }
        input::capture_keyboard(st, input::Ctx::Standalone);
        assert!(st.abort_seen);
        assert_eq!(slot::read_header(st, 1).dropped, 1);
        if deferred {
            assert_eq!(&st.pending_raw[..st.pending_raw_n], &[0x110, 0x10]);
        }
        multiapp::end_start(3);
        mocks::set_app_state(2, 4);
        mocks::set_app_state(3, 4);
        mocks::set_kill_frees(&[3]);
        crate::top_level_abort(st);
        assert_eq!(mocks::kill_calls(), vec![3]);
        // The kill mock only frees AppSlot; deliver the real kernel's WM notice.
        crate::handler::gshell_gui_handler(os32api::gui::proto::GUI_OP_OWNER_EXIT, 3, 3);
        let st = wm::g();
        assert_eq!(input::focus_target(st).unwrap().slot, 0);
        let before = slot::read_header(st, 0).ring_tail;
        input::capture_keyboard(st, input::Ctx::Standalone);
        assert!(!st.abort_seen);
        assert_eq!(st.pending_raw_n, 0);
        assert_eq!(slot::read_header(st, 0).dropped, 0);
        let after = slot::read_header(st, 0).ring_tail;
        let mut scans = Vec::new();
        for i in before..after {
            let ev: GuiEvent = unsafe { core::ptr::read_unaligned(
                slot::ring_ptr(st, 0).add((i as usize % GUI_RING_CAPACITY) * 16) as *const GuiEvent) };
            if ev.kind == GUI_EV_KEY { scans.push((ev.sub, ev.payload[0])); }
        }
        assert_eq!(scans, vec![(1, 0x10), (0, 0x10)]);
        assert!(ring::pending(st, 0) > 0);
        st.inited = false;
    }
}

#[test]
fn e7_restore_refreshes_framebuffer_and_geometry() {
    use crate::{mocks, wm};
    use std::sync::atomic::Ordering::SeqCst;
    mocks::init();
    let st = wm::g();
    *st = wm::GuiState::NEW;
    st.screen_w = 320; st.screen_h = 200;
    let reads = mocks::SCREEN_READS.load(SeqCst);
    let attaches = mocks::FB_ATTACHES.load(SeqCst);
    crate::restore_screen(st, &[0;48]);
    assert_eq!((st.screen_w, st.screen_h), (640,400), "e7 restore geometry stale");
    assert_eq!(mocks::SCREEN_READS.load(SeqCst), reads+1, "e7 restore screen info missing");
    assert_eq!(mocks::FB_ATTACHES.load(SeqCst), attaches+1, "e7 restore framebuffer stale");
}

#[test]
fn e7_failed_cui_refreshes_framebuffer_and_geometry() {
    use crate::{mocks, wm};
    use std::sync::atomic::Ordering::SeqCst;
    mocks::init();
    let st = wm::g();
    *st = wm::GuiState::NEW;
    st.inited = true;
    st.screen_w = 320; st.screen_h = 200;
    let reads = mocks::SCREEN_READS.load(SeqCst);
    let attaches = mocks::FB_ATTACHES.load(SeqCst);
    assert!(crate::switch_cui(st)); // empty FILE_BYTES makes config write fail
    assert_eq!((st.screen_w, st.screen_h), (640,400), "e7 CUI rollback geometry stale");
    assert_eq!(mocks::SCREEN_READS.load(SeqCst), reads+1);
    assert_eq!(mocks::FB_ATTACHES.load(SeqCst), attaches+1);
    st.inited = false;
}
