//! settings_tests.rs — 設定レジストリ (票 S4 §5 の (1)〜(18)) のホスト TDD。
//!
//! `src/settings.rs` の末尾から `#[cfg(test)] #[path]` で取り込み、
//! `host/integration.py` が gshell の実モジュールをホスト ABI の代用
//! (`host/mocks.rs`) と一緒にビルドして走らせる (`make check-gshell-host`)。
//!
//! `cfg_*` は `mocks.rs` の贋物 — **実 DB も SQLite も出てこない**
//! (それは S2-C の `tools/tests/test_cfg.py` の領分)。ここで固定するのは
//! gshell の判断:
//!
//! - いつ `cfg_open` するか、**いつ呼ばないか** (X4 / handler / 無編集の OK)
//! - 1 周回の中で open → begin → set → commit → close を閉じているか
//! - 失敗の段ごとに適用値が動くか動かないか
//! - モーダル枠が塞がっているときの予約と通知の保持 (R1)

use crate::mocks::CfgCall::{Begin, Close, Commit, GetInt, Open, SetInt};
use crate::{mocks, modal, multiapp, settings, startmenu, taskbar, wm};
use os32api::cfg::{CFG_CORRUPT, CFG_ERROR, CFG_MISSING, CFG_OK, CFG_VERSION};

/* ================================================================ */
/*  小道具                                                           */
/* ================================================================ */

const SCOPE: &[u8] = b"gshell";
const K_COLOR: &[u8] = b"desktop/color";
const K_CLOCK: &[u8] = b"taskbar/clock_24h";

/* スキャンコード (drivers/kbd.h)。 */
const SC_ESC: u8 = 0x00;
const SC_TAB: u8 = 0x0F;
const SC_RETURN: u8 = 0x1C;
const SC_SPACE: u8 = 0x34;
const SC_UP: u8 = 0x3A;
const SC_LEFT: u8 = 0x3B;
const SC_RIGHT: u8 = 0x3C;
const SC_DOWN: u8 = 0x3D;

/// 画面だけ整えた素の `GuiState` (窓もスロットも無い)。
fn bare() -> wm::GuiState {
    mocks::init();
    let mut st = wm::GuiState::NEW;
    st.screen_w = 640;
    st.screen_h = 400;
    st
}

fn get_int(key: &[u8]) -> crate::mocks::CfgCall {
    GetInt(SCOPE.to_vec(), key.to_vec())
}
fn set_int(key: &[u8], v: i32) -> crate::mocks::CfgCall {
    SetInt(SCOPE.to_vec(), key.to_vec(), v)
}

/// `kprintf` の行に `needle` を含むものがあるか。
fn printed(needle: &[u8]) -> bool {
    mocks::kprint_lines()
        .iter()
        .any(|l| l.windows(needle.len()).any(|w| w == needle))
}

/// いま開いているモーダルの本文。
fn modal_msg() -> Vec<u8> {
    modal::msg_bytes().to_vec()
}

/// 設定ダイアログを開くところまで進める (予約 → top-level の消費)。
fn open_settings(st: &mut wm::GuiState) {
    settings::request_open();
    assert!(settings::pending(), "予約が立っていない");
    settings::consume(st);
    assert!(modal::is_settings(), "設定ダイアログが開いていない");
}

/* ================================================================ */
/*  (1) load() の 7 経路                                             */
/* ================================================================ */

#[test]
fn s01_load_fills_defaults_and_status_on_every_path() {
    /* --- OK: 読めた値がそのまま入る --- */
    let _st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(0);
    });
    let c = settings::load(0);
    assert_eq!((c.desktop_color, c.clock_24h), (3, false));
    assert_eq!(c.status, CFG_OK);
    assert_eq!(c.close_error, 0);
    assert_eq!(
        mocks::cfg_calls(),
        vec![Open(0), get_int(K_COLOR), get_int(K_CLOCK), Close],
        "open(0) → get ×2 → close で閉じきっていない"
    );

    /* --- MISSING / CORRUPT / VERSION / ERROR: 既定値で status だけ載る --- */
    for (want, schema) in [
        (CFG_MISSING, 0),
        (CFG_CORRUPT, 0),
        (CFG_VERSION, 2),
        (CFG_ERROR, 0),
    ] {
        let _st = bare();
        mocks::cfg(|c| {
            c.status = want;
            c.schema = schema;
            c.sqlite = 11;
        });
        let c = settings::load(0);
        assert_eq!(c.status, want);
        assert_eq!(
            (c.desktop_color, c.clock_24h),
            (settings::DEFAULT_DESKTOP_COLOR, settings::DEFAULT_CLOCK_24H),
            "status={} で既定値が入っていない",
            want
        );
        if want == CFG_VERSION {
            assert_eq!(c.schema_version, 2, "VERSION は読める値をそのまま使う");
        }
        if want == CFG_ERROR {
            assert_eq!(c.sqlite, 11);
        }
    }

    /* --- cfg_open が負: status = -1、**close を呼ばない** --- */
    let _st = bare();
    mocks::cfg(|c| c.open_ret = -9);
    let c = settings::load(0);
    assert_eq!(c.status, settings::STATUS_OPEN_FAILED);
    assert_eq!(c.sqlite, -9);
    assert_eq!(c.desktop_color, settings::DEFAULT_DESKTOP_COLOR);
    assert_eq!(mocks::cfg_calls(), vec![Open(0)], "開けなかったのに触った");

    /* --- cfg_open が 0 を返しながら NULL: 同じ扱い (触らない) --- */
    let _st = bare();
    mocks::cfg(|c| c.open_null = true);
    let c = settings::load(0);
    assert_eq!(c.status, settings::STATUS_OPEN_FAILED);
    assert_eq!(mocks::cfg_calls(), vec![Open(0)]);

    /* --- cfg_close が負: 値は使うが close_error に残す --- */
    let _st = bare();
    mocks::cfg(|c| {
        c.color = Some(5);
        c.close_ret = -1;
        c.last_close_error = -7;
    });
    let c = settings::load(0);
    assert_eq!(c.desktop_color, 5, "close 失敗で読めた値を捨てた");
    assert_eq!(c.close_error, -7, "close 失敗を握りつぶした");
    assert_eq!(c.status, CFG_OK, "close 失敗で status を作り変えた");
}

/* ================================================================ */
/*  (2) 範囲外と NOTFOUND が既定へ落ちる                             */
/* ================================================================ */

#[test]
fn s02_out_of_range_values_fall_back_to_defaults() {
    for v in [-1i32, 16, 255, i32::MIN, i32::MAX] {
        assert_eq!(
            settings::clamp_color(v),
            settings::DEFAULT_DESKTOP_COLOR,
            "color {} が既定に落ちない",
            v
        );
    }
    for v in 0..=15 {
        assert_eq!(settings::clamp_color(v), v as u8);
    }
    assert_eq!(settings::clamp_clock(0), false);
    assert_eq!(settings::clamp_clock(1), true);
    for v in [2i32, -1, 99] {
        assert_eq!(settings::clamp_clock(v), settings::DEFAULT_CLOCK_24H);
    }

    /* NOTFOUND (= 行が無い) は `cfg_get_int` が def を返す経路。 */
    let _st = bare();
    let c = settings::load(0);
    assert_eq!(c.desktop_color, 12);
    assert!(c.clock_24h);

    /* 範囲外が DB に入っていた場合。 */
    let _st = bare();
    mocks::cfg(|c| {
        c.color = Some(99);
        c.clock = Some(2);
    });
    let c = settings::load(0);
    assert_eq!(c.desktop_color, 12, "0〜15 以外が 12 に落ちない");
    assert!(c.clock_24h, "clock_24h の 2 が 1 に落ちない");
}

/* ================================================================ */
/*  (3) 時計の整形と幅                                               */
/* ================================================================ */

#[test]
fn s03_clock_formatting_and_width() {
    let mut buf = [0u8; 9];
    let cases: [(u32, bool, &[u8]); 8] = [
        (0, true, b"00:00"),
        (23 * 3600 + 59 * 60, true, b"23:59"),
        (0, false, b"12:00 AM"),
        (12 * 3600, false, b"12:00 PM"),
        (13 * 3600 + 5 * 60, false, b"1:05 PM"),
        (23 * 3600 + 59 * 60, false, b"11:59 PM"),
        (60, false, b"12:01 AM"),
        (9 * 3600 + 7 * 60, false, b"9:07 AM"),
    ];
    for (epoch, h24, want) in cases {
        let n = taskbar::format_clock(epoch, h24, &mut buf);
        assert_eq!(&buf[..n], want, "epoch={} h24={}", epoch, h24);
        assert_eq!(buf[n], 0, "NUL 終端していない");
    }

    /* 幅 = 文字数 × 8 + 8 (右詰め)。 */
    let st = bare();
    assert_eq!(taskbar::clock_rect_for(&st, 5).w, 48, "24h の幅が変わった");
    assert_eq!(taskbar::clock_rect_for(&st, 8).w, 72);
    assert_eq!(taskbar::clock_rect_for(&st, 7).w, 64);
    assert_eq!(
        taskbar::clock_rect_for(&st, 5).right(),
        taskbar::clock_rect_for(&st, 8).right(),
        "右端が揃っていない (右詰めでない)"
    );
}

/* ================================================================ */
/*  (4) ダイアログの状態機械                                         */
/* ================================================================ */

#[test]
fn s04_dialog_state_machine() {
    let mut st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(1);
    });
    open_settings(&mut st);
    /* 開いた周回で load が走っている。 */
    assert_eq!(
        mocks::cfg_calls(),
        vec![Open(0), get_int(K_COLOR), get_int(K_CLOCK), Close],
        "開いたときに load が呼ばれていない"
    );
    assert_eq!(modal::settings_values(), (3, true, 0));

    /* → で色が進み、← で戻り、0..15 を循環する。 */
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    assert_eq!(modal::settings_values().0, 4);
    for _ in 0..4 {
        modal::on_key(&mut st, SC_LEFT, 0, 0); /* 4 → 0 */
    }
    assert_eq!(modal::settings_values().0, 0);
    modal::on_key(&mut st, SC_LEFT, 0, 0);
    assert_eq!(modal::settings_values().0, 15, "0 の左で 15 に回らない");
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    assert_eq!(modal::settings_values().0, 0, "15 の右で 0 に回らない");

    /* ↓ で clock 行へ、SPACE でトグル、↑ で戻る。 */
    modal::on_key(&mut st, SC_DOWN, 0, 0);
    assert_eq!(modal::settings_values().2, 1);
    modal::on_key(&mut st, SC_SPACE, 0, 0);
    assert_eq!(modal::settings_values().1, false, "SPACE で clock が変わらない");
    modal::on_key(&mut st, SC_DOWN, 0, 0);
    assert_eq!(modal::settings_values().2, 1, "行が 2 を超えた");
    modal::on_key(&mut st, SC_UP, 0, 0);
    modal::on_key(&mut st, SC_UP, 0, 0);
    assert_eq!(modal::settings_values().2, 0, "行が 0 を下回った");

    /* ESC は何も書かない。 */
    let before = mocks::cfg_calls().len();
    assert!(modal::on_key(&mut st, SC_ESC, 0, 0));
    assert!(!modal::is_open());
    settings::consume(&mut st);
    assert_eq!(mocks::cfg_calls().len(), before, "ESC で DB に触った");

    /* --- 値が同じなら OK で cfg_open が呼ばれない --- */
    let mut st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(1);
    });
    open_settings(&mut st);
    let opens = mocks::cfg_open_calls();
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    settings::consume(&mut st);
    assert_eq!(mocks::cfg_open_calls(), opens, "無編集の OK が DB を開いた");
    assert!(printed(b"cfg write skipped"), "write skipped が出ていない");
    assert!(!modal::is_open(), "無編集の OK でメッセージが出た");

    /* --- 違えば open(1) → begin → set (変わった key だけ) → commit → close --- */
    let mut st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(1);
    });
    open_settings(&mut st);
    modal::on_key(&mut st, SC_RIGHT, 0, 0); /* color 3 → 4 */
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    /* `finish_wm` の中では `cfg_*` が 1 本も呼ばれない (票 §5 の (10))。 */
    assert_eq!(
        mocks::cfg_calls(),
        vec![Open(0), get_int(K_COLOR), get_int(K_CLOCK), Close],
        "OK の処理 (finish_wm) の中で DB に触った"
    );
    settings::consume(&mut st);
    assert_eq!(
        mocks::cfg_calls()[4..],
        [Open(1), Begin, set_int(K_COLOR, 4), Commit, Close],
        "保存の順序が違う (clock は変わっていないので set は 1 本)"
    );
    assert_eq!(st.cfg.desktop_color, 4, "適用値が更新されていない");
}

/* ================================================================ */
/*  (5) begin / set / commit の失敗                                  */
/* ================================================================ */

#[test]
fn s05_write_failures_never_move_the_applied_values() {
    for (stage, apply) in [("begin", 0usize), ("set", 1), ("commit", 2)] {
        let mut st = bare();
        st.cfg.desktop_color = 3;
        mocks::cfg(|c| {
            c.color = Some(3);
            c.clock = Some(1);
        });
        open_settings(&mut st);
        modal::on_key(&mut st, SC_RIGHT, 0, 0); /* 3 → 4 */
        assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
        mocks::cfg(|c| match apply {
            0 => c.begin_ret = -1,
            1 => c.set_ret = -1,
            _ => c.commit_ret = -1,
        });
        settings::consume(&mut st);
        assert_eq!(st.cfg.desktop_color, 3, "{} 失敗で適用値が動いた", stage);
        /* 失敗しても `cfg_close` は必ず通る (rollback は close の仕事)。 */
        assert_eq!(
            mocks::cfg_calls().last(),
            Some(&Close),
            "{} 失敗で close していない",
            stage
        );
        /* メッセージが出る (枠が空いているので同じ周回で)。 */
        assert!(modal::is_open(), "{} 失敗でメッセージが出ない", stage);
        let msg = modal_msg();
        assert!(
            msg.starts_with(b"save failed: ") && msg.windows(stage.len()).any(|w| w == stage.as_bytes()),
            "{} 失敗の文言が違う: {:?}",
            stage,
            String::from_utf8_lossy(&msg)
        );
    }
}

/* ================================================================ */
/*  (6) MISSING / CORRUPT / VERSION では保存しない                   */
/* ================================================================ */

#[test]
fn s06_non_ok_status_refuses_to_save_after_an_edit() {
    for (status, word) in [
        (CFG_MISSING, "MISSING"),
        (CFG_CORRUPT, "CORRUPT"),
        (CFG_VERSION, "VERSION"),
    ] {
        /* --- 編集してから OK → open(1) を呼ばず cannot save --- */
        let mut st = bare();
        mocks::cfg(|c| c.status = status);
        open_settings(&mut st);
        let opens = mocks::cfg_open_calls();
        modal::on_key(&mut st, SC_RIGHT, 0, 0);
        assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
        settings::consume(&mut st);
        assert_eq!(
            mocks::cfg_open_calls(),
            opens,
            "{} なのに書きに行った",
            word
        );
        assert!(modal::is_open(), "{} で cannot save が出ない", word);
        let msg = modal_msg();
        assert_eq!(
            msg,
            format!("cannot save: {}", word).into_bytes(),
            "文言が違う"
        );

        /* --- 無編集の OK は何も出さずに閉じる --- */
        let mut st = bare();
        mocks::cfg(|c| c.status = status);
        open_settings(&mut st);
        assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
        settings::consume(&mut st);
        assert!(!modal::is_open(), "{} の無編集の OK でメッセージが出た", word);
        assert!(printed(b"cfg write skipped"));
    }
}

/* ================================================================ */
/*  (7) Start メニューの項目数と順                                   */
/* ================================================================ */

#[test]
fn s07_start_menu_has_six_root_items_in_order() {
    let mut st = bare();
    assert_eq!(startmenu::ROOT_ITEMS, 6, "root の項目数が 6 でない");
    startmenu::toggle_start(&mut st);
    let want: [&[u8]; 6] = [
        b"Programs",
        b"File Manager",
        b"Run...",
        b"Settings...",
        b"CUI mode",
        b"Shut Down",
    ];
    for (i, w) in want.iter().enumerate() {
        assert_eq!(
            startmenu::root_label(i),
            *w,
            "root の {} 行目が違う (gui_gate の ROW_* がずれる)",
            i
        );
    }
    /* Settings... は Run... の次 (index 3)、CUI / Halt は 1 行下がった。 */
    assert_eq!(startmenu::root_label(3), b"Settings...");
    assert_eq!(startmenu::root_label(4), b"CUI mode");
    assert_eq!(startmenu::root_label(5), b"Shut Down");
}

#[test]
fn s07b_choosing_settings_only_reserves_the_request() {
    let mut st = bare();
    startmenu::toggle_start(&mut st);
    /* Programs(0) → ... → Settings(3) までカーソルを下げて決定。 */
    for _ in 0..3 {
        startmenu::on_key(&mut st, SC_DOWN);
    }
    assert!(startmenu::on_key(&mut st, SC_RETURN), "メニューが閉じていない");
    assert!(settings::pending(), "予約が立っていない");
    assert_eq!(mocks::cfg_calls(), vec![], "メニューの文脈で DB に触った");
    assert!(!modal::is_open(), "メニューの文脈でダイアログを開いた");
}

/* ================================================================ */
/*  (8) desktop::fill が適用値を使う                                 */
/* ================================================================ */

#[test]
fn s08_desktop_fill_uses_the_configured_color() {
    use crate::desktop;
    let mut st = bare();
    st.cfg.desktop_color = 3;
    mocks::clear(0);
    desktop::fill(&st, wm::Rect::new(0, 0, 16, 16));
    let px = mocks::pixels();
    assert_eq!(px[0], 3, "背景に GUI_COLOR_DESKTOP を焼き付けている");
    assert_eq!(px[10 * mocks::W + 10], 3);

    /* 2 色モード (リース中) は従来どおり色を使わない (TEXT / WINDOW の市松)。 */
    st.lease_applied = 0x0001_0000;
    mocks::clear(9);
    desktop::fill(&st, wm::Rect::new(0, 0, 16, 16));
    let px = mocks::pixels();
    for y in 0..16 {
        for x in 0..16 {
            let c = px[y * mocks::W + x];
            assert!(
                c == 0 || c == 7,
                "2 色モードで色 {} を使った ({}, {})",
                c,
                x,
                y
            );
        }
    }
}

/* ================================================================ */
/*  (9) アプリが Ctx::Wait のときの Start → Settings (B1)            */
/* ================================================================ */

#[test]
fn s09_settings_from_an_app_wait_context_parks_and_runs_at_top_level() {
    let mut st = bare();
    let shm = mocks::Shm::new();
    st.shm_base = shm.base();
    /* アプリ 1 本 (id 2) が居て `OP_WAIT` 中。 */
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.w = 200;
    w.h = 100;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;
    multiapp::adopt_running(2);
    multiapp::mark_resumed(2);

    assert!(
        !multiapp::should_park(&st, 2),
        "前提が違う: 何も無いのに譲っている"
    );
    /* handler の文脈 = ここでは DB に触らない。 */
    settings::request_open();
    assert_eq!(mocks::cfg_calls(), vec![], "予約の時点で DB に触った");
    assert!(
        multiapp::should_park(&st, 2),
        "予約があるのに top-level へ戻らない (B1)"
    );

    /* top-level の周回で初めて load が走り、ダイアログが開く。 */
    settings::consume(&mut st);
    assert_eq!(
        mocks::cfg_calls(),
        vec![Open(0), get_int(K_COLOR), get_int(K_CLOCK), Close]
    );
    assert!(modal::is_settings());
    assert!(!settings::pending(), "消費したのに予約が残っている");
    assert!(
        !multiapp::should_park(&st, 2),
        "予約を消費したのに譲り続けている"
    );
}

/// 通知だけが残っている状態 (`req = None, notice = Some`) でも、スケジューラの
/// 判断が top-level へ制御を返すこと (票 §3 の往復 3 non-blocker 2)。
///
/// `consume` を直に呼ぶだけの試験にしない — 見たいのは「アプリが `OP_WAIT` に
/// 戻っても通知を出しに行ける」という**判断の連なり**のほう。
///
/// **限定** (S4 実装レビュー往復 2 の non-blocker): ここで通すのは
/// `should_park` → `note_parked` → `pick` → `consume` という
/// **スケジューラ関数を順に直接呼ぶ**筋であって、`handler` → 実 park
/// (`exec_park`) → `standalone_loop` の**実遷移は通していない**。制御の流れ
/// そのものはカーネルの領分で、ホストの `exec_park` は longjmp できない
/// (`mocks.rs` の注記)。実遷移はゲスト受入 (票 §6) の担当。
#[test]
fn s09b_notice_only_pending_returns_control_to_top_level_through_the_scheduler() {
    let mut st = bare();
    let shm = mocks::Shm::new();
    st.shm_base = shm.base();
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    crate::slot::init_header(&st, 0);
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.w = 200;
    w.h = 100;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;
    multiapp::adopt_running(2);
    multiapp::mark_resumed(2);

    /* 起動時通知だけが残っている (予約は無い)。 */
    mocks::cfg(|c| c.status = CFG_MISSING);
    st.cfg = settings::load(0);
    settings::startup_notice(&st.cfg);
    assert!(settings::pending(), "通知が pending() に入っていない");

    /* (a) 走っているアプリは譲る。 */
    assert!(
        multiapp::should_park(&st, 2),
        "通知だけでは top-level へ戻らない (should_park に notice が無い)"
    );
    multiapp::note_parked(2, None);
    /* (b) 誰も ready でない周なので `pick` は `pick_poll` まで降りるが、
     *     設定の仕事が残っているので**誰も起こさない** = WM の番。 */
    assert_eq!(
        multiapp::pick(&st),
        0,
        "設定の仕事が残っているのにアプリを起こした (pick_poll の門)"
    );
    /* (c) top-level の周回で通知が出る。 */
    settings::consume(&mut st);
    assert!(modal::is_open(), "top-level へ戻ったのに通知が出ない");
    assert_eq!(
        modal_msg(),
        b"Settings: MISSING - defaults in use. Run 'cfg init' in CUI.".to_vec()
    );
    /* (d) 出し終われば門は閉じ、アプリがまた走れる。 */
    assert!(!settings::pending());
    multiapp::mark_resumed(2);
    assert!(
        !multiapp::should_park(&st, 2),
        "通知が済んでも譲り続けている"
    );
}

/* ================================================================ */
/*  (10) OK は予約だけ / (11) close 失敗の 2 経路                    */
/* ================================================================ */

#[test]
fn s11_commit_ok_but_close_failed_applies_and_warns() {
    let mut st = bare();
    st.cfg.desktop_color = 3;
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(1);
    });
    open_settings(&mut st);
    modal::on_key(&mut st, SC_RIGHT, 0, 0); /* 3 → 4 */
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    mocks::cfg(|c| {
        c.close_ret = -1;
        c.last_close_error = -5;
    });
    settings::consume(&mut st);
    assert_eq!(st.cfg.desktop_color, 4, "commit 済みなのに適用していない");
    assert_eq!(st.cfg.close_error, -5, "close 失敗が残っていない");
    assert!(modal::is_open());
    assert_eq!(modal_msg(), b"saved, but close failed (-5)".to_vec());
}

/* ================================================================ */
/*  (12) 起動時通知                                                  */
/* ================================================================ */

#[test]
fn s12_startup_notice_appears_once_and_only_when_needed() {
    /* --- OK かつ close 成功なら出ない --- */
    let mut st = bare();
    st.cfg = settings::load(0);
    settings::startup_notice(&st.cfg);
    assert!(!settings::pending(), "OK なのに通知を仕込んだ");
    settings::consume(&mut st);
    assert!(!modal::is_open());
    assert!(
        printed(b"gshell: cfg OK color=12 clock24=1"),
        "kprintf の 1 行が違う: {:?}",
        mocks::kprint_lines()
            .iter()
            .map(|l| String::from_utf8_lossy(l).into_owned())
            .collect::<Vec<_>>()
    );

    /* --- MISSING は 1 回だけ出る --- */
    let mut st = bare();
    mocks::cfg(|c| c.status = CFG_MISSING);
    st.cfg = settings::load(0);
    settings::startup_notice(&st.cfg);
    assert!(settings::pending());
    settings::consume(&mut st);
    assert!(modal::is_open());
    assert_eq!(
        modal_msg(),
        b"Settings: MISSING - defaults in use. Run 'cfg init' in CUI.".to_vec()
    );
    /* 閉じたら二度と出ない。 */
    modal::on_key(&mut st, SC_RETURN, 0, 0);
    assert!(!modal::is_open());
    settings::consume(&mut st);
    assert!(!modal::is_open(), "起動時通知が 2 回出た");
    assert!(!settings::pending());

    /* --- VERSION は版数つき --- */
    let mut st = bare();
    mocks::cfg(|c| {
        c.status = CFG_VERSION;
        c.schema = 2;
    });
    st.cfg = settings::load(0);
    settings::startup_notice(&st.cfg);
    settings::consume(&mut st);
    assert_eq!(modal_msg(), b"Settings: VERSION 2 (read only)".to_vec());

    /* --- ERROR は sqlite コードつき --- */
    let mut st = bare();
    mocks::cfg(|c| {
        c.status = CFG_ERROR;
        c.sqlite = 26;
    });
    st.cfg = settings::load(0);
    settings::startup_notice(&st.cfg);
    settings::consume(&mut st);
    assert_eq!(
        modal_msg(),
        b"Settings: ERROR sqlite=26 - defaults in use".to_vec()
    );

    /* --- status は OK でも close 失敗なら出る、かつ再 load で消えない --- */
    let mut st = bare();
    mocks::cfg(|c| {
        c.close_ret = -1;
        c.last_close_error = -3;
    });
    st.cfg = settings::load(0);
    assert_eq!(st.cfg.close_error, -3);
    settings::startup_notice(&st.cfg);
    settings::consume(&mut st);
    assert_eq!(modal_msg(), b"Settings: close failed (-3)".to_vec());
    /* 再 load が成功しても close_error は残る (利用者が見るまで)。 */
    mocks::cfg(|c| {
        c.close_ret = 0;
        c.last_close_error = 0;
    });
    let again = settings::load(st.cfg.close_error);
    assert_eq!(again.close_error, -3, "再 load 成功で close 失敗を消した");
}

/* ================================================================ */
/*  (13) 時計 8 → 7 文字の dirty が旧 ∪ 新                           */
/* ================================================================ */

#[test]
fn s13_shrinking_clock_dirties_the_union_of_old_and_new() {
    let mut st = bare();
    st.cfg.clock_24h = false;
    /* 12:59 PM (8 文字) を作ってから 1:00 PM (7 文字) へ。 */
    mocks::set_sys_time(12 * 3600 + 59 * 60);
    taskbar::x3_cycle(&mut st);
    assert_eq!(taskbar::clock_len(), 8, "8 文字になっていない");
    let wide = taskbar::clock_rect_for(&st, 8);
    st.screen_dirty = wm::RectSet::EMPTY;

    mocks::set_sys_time(13 * 3600);
    st.now = st.now.wrapping_add(1000);
    taskbar::x3_cycle(&mut st);
    assert_eq!(taskbar::clock_len(), 7, "7 文字に縮んでいない");

    /* 旧矩形の左端 8px が損傷に入っていること (新矩形だけでは残る。B4)。 */
    let mut covered = false;
    for i in 0..st.screen_dirty.len {
        let d = st.screen_dirty.rects[i];
        if d.x <= wide.x && d.right() >= wide.x + 8 && d.y <= wide.y && d.bottom() >= wide.bottom() {
            covered = true;
        }
    }
    assert!(
        covered,
        "旧矩形の左端が dirty に入っていない: {:?}",
        (0..st.screen_dirty.len)
            .map(|i| {
                let r = st.screen_dirty.rects[i];
                (r.x, r.y, r.w, r.h)
            })
            .collect::<Vec<_>>()
    );
}

/* ================================================================ */
/*  (14) X4 / handler の文脈では cfg_* を 1 本も呼ばない             */
/* ================================================================ */

/// **限定** (S4 実装レビュー往復 2 の non-blocker): `wm_cycle(Ctx::Pump)` /
/// `wm_cycle(Ctx::Wait)` と `settings::consume` を**順に直接呼ぶ**試験であり、
/// `handler` → 実 park (`exec_park`) → `standalone_loop` の**実遷移は通して
/// いない** (S09b と同じ限定)。ここで固定するのは「どの文脈が DB に触ってよいか」
/// という判断だけ。
#[test]
fn s14_pump_and_handler_contexts_never_touch_the_db() {
    let mut st = bare();
    let shm = mocks::Shm::new();
    st.shm_base = shm.base();
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    crate::slot::init_header(&st, 0);
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.w = 200;
    w.h = 100;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;

    /* 予約を立てたうえで X4 (ポンプ) と X3 の UI 周期を回す。 */
    settings::request_open();
    crate::wm::wm_cycle(&mut st, crate::input::Ctx::Pump);
    crate::wm::wm_cycle(&mut st, crate::input::Ctx::Wait);
    assert_eq!(
        mocks::cfg_calls(),
        vec![],
        "X4 / X3 の文脈で設定 DB に触った (契約 S8)"
    );
    assert!(settings::pending(), "予約が消えている");
    /* top-level (単独ループの消費) で初めて触る。 */
    settings::consume(&mut st);
    assert_eq!(mocks::cfg_open_calls(), 1);
}

/* ================================================================ */
/*  (15) マウス操作                                                  */
/* ================================================================ */

#[test]
fn s15_mouse_selects_rows_bumps_on_reclick_and_presses_buttons() {
    let mut st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(1);
    });
    open_settings(&mut st);
    let (r0, r1, ok, cancel) = modal::settings_hit_rects();

    /* 別の行をクリック = 選択だけ (値は変わらない)。 */
    modal::on_button(&mut st, r1.x + 4, r1.y + 4);
    assert_eq!(modal::settings_values(), (3, true, 1), "行が選ばれていない");
    /* 同じ行の再クリック = 値が進む。 */
    modal::on_button(&mut st, r1.x + 4, r1.y + 4);
    assert_eq!(modal::settings_values(), (3, false, 1), "再クリックで進まない");
    /* 行 0 をクリック → 再クリックで色が 1 進む。 */
    modal::on_button(&mut st, r0.x + 4, r0.y + 4);
    assert_eq!(modal::settings_values().2, 0);
    modal::on_button(&mut st, r0.x + 4, r0.y + 4);
    assert_eq!(modal::settings_values().0, 4);

    /* Cancel ボタン = 何も書かない。 */
    assert!(modal::on_button(&mut st, cancel.x + 4, cancel.y + 4));
    assert!(!modal::is_open());
    settings::consume(&mut st);
    assert_eq!(mocks::cfg_open_calls(), 1, "Cancel で書きに行った");

    /* OK ボタン = 予約が立つ。 */
    let mut st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(1);
    });
    open_settings(&mut st);
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    assert!(modal::on_button(&mut st, ok.x + 4, ok.y + 4));
    assert!(settings::pending(), "OK ボタンで予約が立たない");
    settings::consume(&mut st);
    assert_eq!(st.cfg.desktop_color, 4);
}

/* ================================================================ */
/*  (16) モーダル枠の競合 (R1)                                       */
/* ================================================================ */

#[test]
fn s16_open_is_retried_while_an_app_modal_holds_the_frame() {
    let mut st = bare();
    let shm = mocks::Shm::new();
    st.shm_base = shm.base();
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    crate::slot::init_header(&st, 0);
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.w = 200;
    w.h = 100;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;
    multiapp::adopt_running(2);
    multiapp::mark_resumed(2);

    settings::request_open();
    /* アプリが自分のモーダルを開いた (X3 の MODAL_OPEN)。**拒否しない**。 */
    assert!(modal::open(&mut st, 0, 2, 0, modal::GUI_MODAL_OK, b"app\0", 3) > 0);

    /* 枠が塞がっている周回では消費されず、予約が残る。 */
    settings::consume(&mut st);
    assert_eq!(mocks::cfg_calls(), vec![], "枠が塞がっているのに load した");
    assert!(settings::pending(), "開けなかった予約を捨てた");
    assert!(
        multiapp::should_park(&st, 2),
        "予約が残っているのに top-level へ戻らない"
    );
    assert!(!modal::is_settings(), "アプリのモーダルを置き換えた");

    /* アプリのモーダルが閉じた次の周回で開く。 */
    modal::on_key(&mut st, SC_RETURN, 0, 0);
    assert!(!modal::is_open());
    settings::consume(&mut st);
    assert!(modal::is_settings(), "枠が空いても開かない");
    assert_eq!(mocks::cfg_open_calls(), 1, "load はこの周回で 1 回だけ");
}

#[test]
fn s16b_save_runs_but_its_notice_waits_for_a_free_frame() {
    let mut st = bare();
    let shm = mocks::Shm::new();
    st.shm_base = shm.base();
    st.slots[0].used = true;
    st.slots[0].owner = 2;
    crate::slot::init_header(&st, 0);
    let mut w = wm::Win::EMPTY;
    w.used = true;
    w.visible = true;
    w.owner = 2;
    w.gen = 1;
    w.w = 200;
    w.h = 100;
    st.windows[0] = w;
    st.zorder[0] = 0;
    st.z_count = 1;

    st.cfg.desktop_color = 3;
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(1);
    });
    open_settings(&mut st);
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    /* 予約と消費の間にアプリがモーダルを開いた。 */
    assert!(modal::open(&mut st, 0, 2, 0, modal::GUI_MODAL_OK, b"app\0", 3) > 0);
    mocks::cfg(|c| {
        c.close_ret = -1;
        c.last_close_error = -5;
    });
    settings::consume(&mut st);
    /* DB 操作と適用は**枠と無関係に**この周回で終わる。 */
    assert_eq!(
        mocks::cfg_calls()[4..],
        [Open(1), Begin, set_int(K_COLOR, 4), Commit, Close]
    );
    assert_eq!(st.cfg.desktop_color, 4, "枠が塞がっていると適用しない");
    /* 通知だけは保持され、アプリのモーダルは置き換えられていない。 */
    assert!(settings::pending(), "通知を捨てた (open_wm の戻りを見ていない)");
    assert_eq!(modal_msg(), b"app".to_vec(), "アプリのモーダルを潰した");

    /* 枠が空いた周回で 1 回だけ出る。 */
    modal::on_key(&mut st, SC_RETURN, 0, 0);
    settings::consume(&mut st);
    assert_eq!(modal_msg(), b"saved, but close failed (-5)".to_vec());
    modal::on_key(&mut st, SC_RETURN, 0, 0);
    settings::consume(&mut st);
    assert!(!modal::is_open(), "通知が 2 回出た");
    assert!(!settings::pending());
}

/* ================================================================ */
/*  (17) 16 色リース中の描画は TEXT / WINDOW だけ                    */
/* ================================================================ */

#[test]
fn s17_settings_dialog_in_mono_uses_only_text_and_window() {
    let mut st = bare();
    mocks::cfg(|c| c.color = Some(3));
    open_settings(&mut st);
    /* リース中 (契約 G8)。 */
    st.lease_applied = 0x0001_0000;
    mocks::clear(9);
    let r = modal::rect();
    modal::draw(&st, r);
    let px = mocks::pixels();
    for y in r.y..r.bottom() {
        for x in r.x..r.right() {
            let c = px[y as usize * mocks::W + x as usize];
            assert!(
                c == 0 || c == 7 || c == 9,
                "2 色モードでシステム色 {} を使った ({}, {})",
                c,
                x,
                y
            );
        }
    }
    /* 数値だけで、色見本 (色 3 の 16px 角) は描かない
     * (上の走査が 0 / 7 / 9 以外を弾いているので、ここは念押し)。 */
    let mut swatch = false;
    for y in r.y..r.bottom() {
        for x in r.x..r.right() {
            if px[y as usize * mocks::W + x as usize] == 3 {
                swatch = true;
            }
        }
    }
    assert!(!swatch, "リース中に色見本を描いた");

    /* 16 色が使えるときは見本を描く (回帰の裏返し)。 */
    st.lease_applied = 0;
    mocks::clear(9);
    modal::draw(&st, r);
    let px = mocks::pixels();
    let mut swatch = false;
    for y in r.y..r.bottom() {
        for x in r.x..r.right() {
            if px[y as usize * mocks::W + x as usize] == 3 {
                swatch = true;
            }
        }
    }
    assert!(swatch, "16 色でも色見本が出ない");
}

/* ================================================================ */
/*  (18) status は get の後に採る                                    */
/* ================================================================ */

#[test]
fn s18_status_is_sampled_after_the_gets_not_at_open() {
    /* --- 反例 (実装レビュー往復 1 の B1): **1 本目の get は成功して 3 を返し**、
     *     2 本目の prepare / step が I/O で落ちて status が ERROR になる。
     *     先に読めた 3 を採ると「ERROR・defaults in use と通知しながら背景は 3」
     *     という食い違いが出る。**両キーとも既定へ倒す**のが正。
     *
     *     贋物の遷移は **2 本目の `cfg_get_int` を返した後**に起こす
     *     (`status_after_get`)。`cfg_status` の呼び出し回数で切り替えると
     *     「status を get より前に採る」退行でも ERROR が返ってしまい、
     *     この試験が見張るべき順序 (get → status) を取り逃がす。 --- */
    let _st = bare();
    mocks::cfg(|c| {
        c.color = Some(3); /* 1 本目は本物の値が返る */
        c.status = CFG_OK; /* open 直後は OK だった */
        c.status_after_get = Some((2, CFG_ERROR));
        c.sqlite = 10;
    });
    let c = settings::load(0);
    assert_eq!(
        c.status, CFG_ERROR,
        "open 直後の status を採っている (get 後の失敗を取り逃がす)"
    );
    assert_eq!(c.sqlite, 10, "sqlite コードを採っていない");
    assert_eq!(
        c.desktop_color,
        settings::DEFAULT_DESKTOP_COLOR,
        "途中で ERROR になったのに先に読めた値を適用した (B1)"
    );
    assert_eq!(
        c.clock_24h,
        settings::DEFAULT_CLOCK_24H,
        "ERROR なのに時計の値を信じた"
    );

    /* --- MISSING / CORRUPT / open 負でも同じ (両キー既定) --- */
    for status in [CFG_MISSING, CFG_CORRUPT] {
        let _st = bare();
        mocks::cfg(|c| {
            c.color = Some(3);
            c.clock = Some(0);
            c.status = status;
        });
        let c = settings::load(0);
        assert_eq!(
            (c.desktop_color, c.clock_24h),
            (settings::DEFAULT_DESKTOP_COLOR, settings::DEFAULT_CLOCK_24H),
            "status={} で取得値を採った",
            status
        );
    }

    /* --- VERSION は「読める値をそのまま使う」(票 §2) ので採る --- */
    let _st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.clock = Some(0);
        c.status = CFG_VERSION;
        c.schema = 2;
    });
    let c = settings::load(0);
    assert_eq!(
        (c.desktop_color, c.clock_24h),
        (3, false),
        "VERSION で読める値を捨てた"
    );
    assert_eq!(c.schema_version, 2);

    /* --- 診断と close_error は既定へ倒しても保つ --- */
    let _st = bare();
    mocks::cfg(|c| {
        c.color = Some(3);
        c.status = CFG_ERROR;
        c.sqlite = 26;
        c.close_ret = -1;
        c.last_close_error = -5;
    });
    let c = settings::load(0);
    assert_eq!(c.desktop_color, settings::DEFAULT_DESKTOP_COLOR);
    assert_eq!(c.status, CFG_ERROR);
    assert_eq!(c.sqlite, 26, "診断を落とした");
    assert_eq!(c.close_error, -5, "close 診断を落とした");
}

/* ================================================================ */
/*  (19) 版面が文字を枠外へ出さない (実装レビュー往復 1 の B2 / B3)   */
/* ================================================================ */

/// ダイアログの**外側**に見張り色を敷いて `modal::draw` を通し、1 画素も
/// 書き換わっていないことを見る。`kcg_draw_utf8` にクリップは無いので、
/// 版面の幅 / 高さが文字に足りないとここが崩れる。
fn assert_nothing_outside(st: &wm::GuiState, what: &str) {
    const SENTINEL: u8 = 200;
    mocks::clear(SENTINEL);
    let r = modal::rect();
    modal::draw(st, r);
    let px = mocks::pixels();
    for y in 0..st.screen_h {
        for x in 0..st.screen_w {
            if r.contains(x, y) {
                continue;
            }
            assert_eq!(
                px[y as usize * mocks::W + x as usize],
                SENTINEL,
                "{}: 枠外 ({}, {}) に描いた (rect = {:?})",
                what,
                x,
                y,
                (r.x, r.y, r.w, r.h)
            );
        }
    }
    /* 空振りでないこと: 枠の中は実際に描かれている。 */
    assert_ne!(
        px[(r.y + 1) as usize * mocks::W + (r.x + 1) as usize],
        SENTINEL,
        "{}: そもそも描いていない",
        what
    );
}

#[test]
fn s19_dialog_never_draws_outside_its_frame() {
    /* --- B2: リース中の説明文 (48 文字 = 384px) が 360px の枠を超えていた --- */
    let mut st = bare();
    mocks::cfg(|c| c.color = Some(12));
    open_settings(&mut st);
    assert_nothing_outside(&st, "16 色");
    st.lease_applied = 0x0001_0000;
    assert_nothing_outside(&st, "リース中 (B2)");
    /* 開いたまま色を 2 桁へ進めても (9 → 10) 伸びない。 */
    for _ in 0..4 {
        modal::on_key(&mut st, SC_RIGHT, 0, 0);
    }
    assert_nothing_outside(&st, "リース中 + 色 2 桁");
    st.lease_applied = 0;
    assert_nothing_outside(&st, "16 色へ戻した");

    /* --- B3: MISSING + close 失敗 + 計測を 1 行に連結すると 78 文字 = 624px --- */
    let mut st = bare();
    mocks::cfg(|c| {
        c.status = CFG_MISSING;
        c.close_ret = -1;
        c.last_close_error = 5;
    });
    open_settings(&mut st);
    assert_nothing_outside(&st, "MISSING + close 失敗 + 計測 (B3)");
    /* 3 行に分かれていること (状態 / close 診断 / 計測)。 */
    assert_eq!(
        modal::settings_status_lines(),
        3,
        "状態行が 1 本に連結されたまま"
    );

    /* --- 桁数の多い診断でも枠外へ出ない --- */
    let mut st = bare();
    mocks::cfg(|c| {
        c.status = CFG_ERROR;
        c.sqlite = i32::MIN;
        c.close_ret = -1;
        c.last_close_error = i32::MIN;
    });
    open_settings(&mut st);
    assert_nothing_outside(&st, "最長の診断");
}

/* ================================================================ */
/*  (20) 保存の残りの分岐 (実装レビュー往復 1 の non-blocker)         */
/* ================================================================ */

/// 色 `base` / 時計 `clock` の DB を読んでダイアログを開く。
fn open_with(st: &mut wm::GuiState, base: i32, clock: i32) {
    mocks::cfg(|c| {
        c.color = Some(base);
        c.clock = Some(clock);
    });
    open_settings(st);
}

#[test]
fn s20_remaining_save_branches() {
    /* --- (a) 保存のときに `cfg_open` が負 --- */
    let mut st = bare();
    st.cfg.desktop_color = 3;
    open_with(&mut st, 3, 1);
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    mocks::cfg(|c| c.open_ret = -9);
    settings::consume(&mut st);
    assert_eq!(st.cfg.desktop_color, 3, "open 負で適用値が動いた");
    assert_eq!(
        mocks::cfg_calls().last(),
        Some(&Open(1)),
        "開けなかったのに触った"
    );
    assert_eq!(modal_msg(), b"save failed: open (-9)".to_vec());

    /* --- (b) 予約から消費までの間に DB が非 OK になった --- */
    let mut st = bare();
    st.cfg.desktop_color = 3;
    open_with(&mut st, 3, 1);
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    let before = mocks::cfg_calls().len();
    mocks::cfg(|c| c.status = CFG_MISSING); /* CUI で rm された */
    settings::consume(&mut st);
    assert_eq!(st.cfg.desktop_color, 3, "非 OK で書いた");
    assert_eq!(
        mocks::cfg_calls()[before..],
        [Open(1), Close],
        "非 OK なのに begin / set まで進んだ"
    );
    assert_eq!(modal_msg(), b"cannot save: MISSING".to_vec());

    /* --- (c1) 1 本目の set で落ちれば 2 本目は呼ばない (両キー変更) --- */
    let mut st = bare();
    st.cfg.desktop_color = 3;
    st.cfg.clock_24h = true;
    open_with(&mut st, 3, 1);
    modal::on_key(&mut st, SC_RIGHT, 0, 0); /* color 3 → 4 */
    modal::on_key(&mut st, SC_DOWN, 0, 0);
    modal::on_key(&mut st, SC_SPACE, 0, 0); /* clock 24h → 12h */
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    let before = mocks::cfg_calls().len();
    mocks::cfg(|c| c.set_ret = -1);
    settings::consume(&mut st);
    assert_eq!(
        (st.cfg.desktop_color, st.cfg.clock_24h),
        (3, true),
        "set 失敗で適用値が動いた"
    );
    assert_eq!(
        mocks::cfg_calls()[before..],
        [Open(1), Begin, set_int(K_COLOR, 4), Close],
        "set が落ちた後も続けた"
    );

    /* --- (c2) **2 本目**の set だけ失敗 (両キー変更) --- */
    let mut st = bare();
    st.cfg.desktop_color = 3;
    st.cfg.clock_24h = true;
    open_with(&mut st, 3, 1);
    modal::on_key(&mut st, SC_RIGHT, 0, 0); /* color 3 → 4 */
    modal::on_key(&mut st, SC_DOWN, 0, 0);
    modal::on_key(&mut st, SC_SPACE, 0, 0); /* clock 24h → 12h */
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    let before = mocks::cfg_calls().len();
    /* 1 本目は通り、2 本目だけ落ちる。 */
    mocks::cfg(|c| c.set_ret_script = vec![0, -1]);
    settings::consume(&mut st);
    assert_eq!(
        (st.cfg.desktop_color, st.cfg.clock_24h),
        (3, true),
        "2 本目の set が落ちたのに適用値が動いた"
    );
    /* 2 本とも呼ばれ、**commit せずに** close する (rollback は close の仕事)。 */
    assert_eq!(
        mocks::cfg_calls()[before..],
        [
            Open(1),
            Begin,
            set_int(K_COLOR, 4),
            set_int(K_CLOCK, 0),
            Close
        ],
        "2 本目の set が落ちたのに commit した / 呼ぶ本数が違う"
    );
    assert_eq!(modal_msg(), b"save failed: set (-1)".to_vec());

    /* --- (d) 時計だけの保存 (色は変えない) --- */
    let mut st = bare();
    st.cfg.desktop_color = 3;
    st.cfg.clock_24h = true;
    open_with(&mut st, 3, 1);
    modal::on_key(&mut st, SC_DOWN, 0, 0);
    modal::on_key(&mut st, SC_SPACE, 0, 0);
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    let before = mocks::cfg_calls().len();
    settings::consume(&mut st);
    assert_eq!(
        mocks::cfg_calls()[before..],
        [Open(1), Begin, set_int(K_CLOCK, 0), Commit, Close],
        "変えていない色まで書いた"
    );
    assert_eq!((st.cfg.desktop_color, st.cfg.clock_24h), (3, false));

    /* --- (e) 両キーの保存は color → clock の順 --- */
    let mut st = bare();
    open_with(&mut st, 3, 1);
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    modal::on_key(&mut st, SC_DOWN, 0, 0);
    modal::on_key(&mut st, SC_SPACE, 0, 0);
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    let before = mocks::cfg_calls().len();
    settings::consume(&mut st);
    assert_eq!(
        mocks::cfg_calls()[before..],
        [
            Open(1),
            Begin,
            set_int(K_COLOR, 4),
            set_int(K_CLOCK, 0),
            Commit,
            Close
        ],
        "両キーの set の順が違う"
    );

    /* --- (f) 適用値と再読込値が違う (CUI で書き換わった後に開いた) --- */
    let mut st = bare();
    st.cfg.desktop_color = 3; /* 画面は 3 */
    open_with(&mut st, 7, 1); /* DB は 7 に変わっていた */
    assert_eq!(modal::settings_values().0, 7, "編集の起点が再読込値でない");
    /* 編集せずに OK = 再読込値と同じなので**何も書かない** (画面の 3 とは違う)。 */
    let opens = mocks::cfg_open_calls();
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    settings::consume(&mut st);
    assert_eq!(mocks::cfg_open_calls(), opens, "再読込値と同じなのに書いた");
    assert_eq!(st.cfg.desktop_color, 3, "書いていないのに適用値が動いた");
    assert!(printed(b"cfg write skipped"));

    /* 1 つ進めて OK すれば、再読込値 7 からの差分として 8 が書かれる。 */
    open_with(&mut st, 7, 1);
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    let before = mocks::cfg_calls().len();
    settings::consume(&mut st);
    assert_eq!(
        mocks::cfg_calls()[before..],
        [Open(1), Begin, set_int(K_COLOR, 8), Commit, Close]
    );
    assert_eq!(st.cfg.desktop_color, 8, "保存後に適用していない");
}

/* ================================================================ */
/*  (21) TAB は焦点表示を動かさない (実装レビュー往復 2 の non-blocker) */
/* ================================================================ */

/// 設定ダイアログの RETURN は **常に OK** (`settings_key` の `SC_RETURN`)。
/// TAB で焦点表示だけ Cancel へ動くと、「太枠は Cancel なのに RETURN を押すと
/// 保存される」という食い違いになる。Input ダイアログ (契約 M4) と同じく
/// **TAB ではボタン焦点を動かさない**。
#[test]
fn s21_tab_never_moves_the_button_focus_in_settings() {
    let mut st = bare();
    st.cfg.desktop_color = 3;
    open_with(&mut st, 3, 1);
    assert_eq!(modal::focus_btn(), 0, "開いた直後の焦点が OK でない");

    /* --- TAB を何度打っても焦点は OK のまま (閉じもしない) --- */
    for i in 0..3 {
        assert!(
            !modal::on_key(&mut st, SC_TAB, 0, 0),
            "TAB {} 回目でダイアログが閉じた",
            i + 1
        );
        assert_eq!(
            modal::focus_btn(),
            0,
            "TAB {} 回目で焦点表示が OK から動いた",
            i + 1
        );
        assert!(modal::is_settings(), "TAB でダイアログが差し替わった");
    }

    /* --- TAB は編集値にも触らない --- */
    assert_eq!(modal::settings_values(), (3, true, 0), "TAB が値を動かした");

    /* --- TAB の後の RETURN は OK = 変更を保存する (表示と一致) --- */
    modal::on_key(&mut st, SC_RIGHT, 0, 0); /* color 3 → 4 */
    modal::on_key(&mut st, SC_TAB, 0, 0);
    assert_eq!(modal::focus_btn(), 0, "TAB で焦点表示が動いた");
    let before = mocks::cfg_calls().len();
    assert!(modal::on_key(&mut st, SC_RETURN, 0, 0));
    assert_eq!(modal::focus_btn(), 0, "OK で閉じたのに焦点が OK でない");
    settings::consume(&mut st);
    assert_eq!(
        mocks::cfg_calls()[before..],
        [Open(1), Begin, set_int(K_COLOR, 4), Commit, Close],
        "TAB の後の RETURN が OK として働いていない"
    );
    assert_eq!(st.cfg.desktop_color, 4, "保存後に適用していない");

    /* --- ESC は TAB の後でも Cancel (1 バイトも書かない) --- */
    let mut st = bare();
    st.cfg.desktop_color = 3;
    open_with(&mut st, 3, 1);
    modal::on_key(&mut st, SC_RIGHT, 0, 0);
    modal::on_key(&mut st, SC_TAB, 0, 0);
    let before = mocks::cfg_calls().len();
    assert!(modal::on_key(&mut st, SC_ESC, 0, 0));
    settings::consume(&mut st);
    assert_eq!(
        mocks::cfg_calls().len(),
        before,
        "TAB の後の ESC で DB に触った"
    );
    assert_eq!(st.cfg.desktop_color, 3, "ESC で適用値が動いた");
}
