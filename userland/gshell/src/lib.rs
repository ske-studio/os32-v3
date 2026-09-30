//! gshell — OS32 GUI シェル / ウィンドウマネージャ本体 (票 W1)。
//!
//! シェル帯 (0x300000, CPL=0, owner 1) に常駐する WM。`shell.bin` と排他で、
//! 切替はカーネルのシェル起動ループ (`sys_switch_shell`、契約 T9) が行う。
//!
//! ```text
//!  起動  gfx_init → screen_info (G5) → G6 パレット → gui_register → デスクトップ
//!  単独  自分のループで X3 の周期を回す (入力 → WM の UI → present → sys_halt)
//!  アプリ実行中は WM はアプリの gui_call の中でだけ走る (契約 T8):
//!         X1 ハンドラ / X2 COMMIT / X3 WAIT / X4 ポンプ
//!  終了  Start → CUI mode / Shut Down → 確認 → SessionAction → top-level で実行
//!         (**これが唯一の経路**。ESC の即時切替と上部バーは G5 で製品から撤去、
//!          デバッグ時だけ DEBUG_SHORTCUTS で戻せる)
//! ```
//!
//! モジュールの役割:
//!
//! | ファイル | 役割 |
//! |---|---|
//! | `wm.rs`      | Window 表 (16)・Z 順・フォーカス・所有者・合成・present の集約 |
//! | `slot.rs`    | SHM スロット (T2) の番地解決とヘッダ / 要求 / 応答 / 取り込み tick |
//! | `ring.rs`    | イベントリング 128×16B (T3)。Pointer の畳み込み、OVERFLOW |
//! | `damage.rs`  | 損傷 dirty / issued (G4)。32px 境界・隣接結合・上限 8 |
//! | `visible.rs` | 可視領域 (互いに素な矩形 ≤16) と露出の再計算 |
//! | `chrome.rs`  | 枠・タイトルバー・閉じるボタン・ドラッグ枠 |
//! | `desktop.rs` | 背景と手引き |
//! | `taskbar.rs` | タスクバー (Start / 窓ボタン / 時計) と作業領域 (V12-D の D1/D3) |
//! | `startmenu.rs` | Start メニューと右クリックメニュー (D2 / D4) |
//! | `session.rs` | SessionAction・sticky Quit・トップレベル handoff (V12-S) |
//! | `settings.rs` | 設定レジストリ (`/etc/settings.db`) の読み書きと予約 (S4) |
//! | `cursor.rs`  | マウスカーソル (損傷とは別経路の退避・再描画) |
//! | `input.rs`   | 入力取り込み → Key / Text / Pointer / Button (T3 / U2a) |
//! | `kbdnav.rs`  | キーボードだけの WM 操作とマウスキー (票 KBD_NAV) |
//! | `fep.rs`     | 日本語入力 (U2a)。cooked 待ち行列 = FEP、未確定行と候補窓 |
//! | `lease.rs`   | 14 色パレットのリースとフォーカス追従 (G8) |
//! | `modal.rs`   | モーダルと標準ダイアログ (U4)。入れ子ループなし |
//! | `timer.rs`   | アプリタイマ 8 本 (U5) |
//! | `handler.rs` | `gui_call` ハンドラ (op → 関数表)。X1 / X2 / X3 |
//! | `multiapp.rs` | アプリ 4 本の譲り合い (D11)。park / resume / 音の排他 |
//! | `fullscreen.rs` | 全画面 GFX (T8 D4)。所有者 ≠ WM の間は描かない |
//! | `os32x.rs`   | OS32X ヘッダの宣言ビット (T8 D4 の入口: cpl0 拒否 / gfx 宣言) |
//! | `pump.rs`    | syscall 境界ポンプ (X4) |
//! | `reqs.rs`    | 要求 / 応答構造体 (C `os32_gui_shared.h` の写し) |

#![no_std]

extern crate os32api;

mod chrome;
mod cursor;
mod damage;
mod desktop;
mod fep;
mod ffi;
mod fullscreen;
mod handler;
mod input;
mod kbdnav;
mod lease;
mod modal;
mod multiapp;
mod os32x;
mod pump;
mod reqs;
mod ring;
mod session;
mod settings;
mod slot;
mod startmenu;
mod taskbar;
mod timer;
mod visible;
mod wm;

use os32api::gfx;
use os32api::gui::proto::{
    GUI_MODAL_OK, GUI_SESSION_LAUNCH, GUI_SESSION_SHUTDOWN, GUI_SESSION_SWITCH_CUI, OS32_ERR_FULL,
    OS32_ERR_INVAL,
};
use os32api::KernelAPI;

/// 「CUI へ」で戻る先 (契約 T9)。
static CUI_SHELL: &[u8] = b"/sys/shell.bin\0";

/// [`run_program`] が入口 (票 T8 D4) で起動を断ったときの戻り値 —
/// **[`LaunchVia::Wm`] の経路だけ**。
/// `0` = 「起動しなかったがエラー表示は済んでいる」— `rc < 0` の一般エラー
/// (`Launch failed ...`) を呼ぶ側に出させないため、`0` (park 前に終了) と同じ扱い。
///
/// 要求表経由 ([`LaunchVia::Table`]) はこれを使わない: `0` は表では `DONE`
/// (= 正常終了) なので、断ったことが要求者に伝わらない (実装レビュー 1)。
const RUN_REFUSED: i32 = 0;

/// 起動設定 (`GUI=0/1`)。CUI へ戻すときにここを書き換える (契約 S6 の 4)。
/// 正典は `include/config.h` の `SYS_SYSTEM_CFG`。
static SYSTEM_CFG: &[u8] = b"/etc/system.cfg\0";

/// **デバッグ専用スイッチ** (票 W3 §4.1、ユーザー決定 2026-09-06)。
///
/// v1.1 の「ESC で即 CUI」「上部の `OS32 GUI shell ESC:CUI F1..F5` バー」
/// 「F1〜F5 のランチャ」を出す。**G5 (v1.2 完成) で `false` に倒し、製品の
/// 出荷形はこの 3 つを持たない** (契約 S6、票 W3 §4.1〜4.2)。CUI へ戻る経路は
/// Start → "CUI mode" → 確認ダイアログ → SessionAction (`GUI=0` を永続化) だけ。
///
/// コード本体は**デバッグ設備として残してある** — 実機で GUI が起動直後に
/// 固まったときに `true` へ戻せば、Start メニューを操作できなくても ESC で
/// CUI へ抜けられる。`true` に倒すのは手元の調査中だけで、コミットはしない。
/// 参照先 ([`LAUNCH_APPS`] / [`input::standalone_key`] / [`desktop::draw_hint`] /
/// [`launch_app`]) はこの定数から到達可能なので、`false` でも警告は出ない。
pub(crate) const DEBUG_SHORTCUTS: bool = false;

/// F1〜F4 で起動する確認用アプリ (ゲート G2 / G3 の検証用、[`DEBUG_SHORTCUTS`])。
/// gshell 単独時 (窓が 1 枚も無いとき) に [`input::standalone_key`] が横取りする。
/// パスは `userland/deploy.yaml` の登録 (`/usr/bin/`) に合わせてある。
/// F5 は WM 自身のファイル選択ダイアログ (任意の .bin を選んで起動)。
pub(crate) static LAUNCH_APPS: [&[u8]; 4] = [
    b"/usr/bin/gui_demo.bin\0",   /* F1 — C2 のデモ (窓 2 枚・ウィジェット) */
    b"/usr/bin/gui_bench.bin\0",  /* F2 — 契約 P2 の入力→表示の測定器 */
    b"/usr/bin/gui_busy.bin\0",   /* F3 — K2 の計算中ポンプ / CTRL+STOP 脱出 */
    b"/usr/bin/lease_test.bin\0", /* F4 — 契約 G8 の 14 色リース */
];

/* ================================================================ */
/*  エントリ (crt0.asm が main(argc, argv, api) を呼ぶ)              */
/* ================================================================ */
#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, api: *mut KernelAPI) -> i32 {
    if api.is_null() {
        return -1;
    }
    os32api::os32_init(api);

    let st = wm::g();
    *st = wm::GuiState::NEW;
    /* SHM の先頭 (KAPI のデータフィールド)。スロットは +192KB から 16KB ずつ。 */
    st.shm_base = unsafe { (*api).shm_base };

    /* GFX モードへ (libos32gfx_init → gfx_init + framebuffer + surface/sprite)。 */
    gfx::init();
    /* 640×400 / 16 色を決め打ちしない (契約 G5)。起動時に 1 回だけ読む。 */
    wm::read_screen_info(st);
    /* G6 のシステムパレットを入れる。 */
    wm::install_system_palette();
    /* FEP (契約 U2a)。GFX 版の IME_Render を用意する (K の KAPI 待ち)。 */
    fep::install();

    unsafe {
        let a = os32api::api();
        (a.mouse_set_bounds)(0, 0, (st.screen_w - 1) as i16, (st.screen_h - 1) as i16);
        /* ハード/テキストのカーソルは使わない (自前で描く)。 */
        (a.mouse_cursor_hide)();
    }
    st.mouse_x = st.screen_w / 2;
    st.mouse_y = st.screen_h / 2;
    st.real_x = st.mouse_x;
    st.real_y = st.mouse_y;
    st.cursor.x = st.mouse_x;
    st.cursor.y = st.mouse_y;
    /* カナをロックしたまま GUI に入った場合 (票 KBD_NAV §1-5)。 */
    kbdnav::init(st);
    st.inited = true;

    /* WM を登録する (契約 T1)。以後アプリの gui_call がここへ来る。
     * 失敗 (シェル帯以外から起動された等) でも単独 WM としては動く。 */
    unsafe {
        let a = os32api::api();
        (a.gui_register)(
            handler::gshell_gui_handler as *const () as *mut u8,
            pump::gshell_gui_pump as *const () as *mut u8,
        );
    }

    /* 設定レジストリ (票 S4 §2)。**gfx attach の後・最初の合成の前**に 1 回。
     * DB が無い / 壊れている / 版が新しいときも既定値で起動を続ける
     * (DESIGN §5 の fallback)。 */
    st.cfg = settings::load(0);

    /* デスクトップ。全画面 present は WM だけ (契約 G4)。 */
    wm::composite_full(st);

    /* 起動時通知 (票 S4 §2 の B3)。**最初の合成の後**に仕込み、実際に出るのは
     * top-level の最初の周回 (`settings::consume`)。gshell 実行中の `kprintf`
     * は con_sink に入って CUI 画面には残らないので、WM のメッセージが利用者に
     * 届く唯一の経路。 */
    settings::startup_notice(&st.cfg);

    /* ---- 単独ループ (契約 T8: アプリが居ないときは gshell が X3 を回す) ---- */
    if !standalone_loop(st) {
        return 0;
    }

    /* ---- 「CUI へ」(デバッグ用の ESC = DEBUG_SHORTCUTS。契約 T9)。
     *      製品では `st.quit` が立たないのでここは通らない — 出荷形の CUI 復帰は
     *      下の `switch_cui` (SessionAction) 側だけ。 ---- */
    cursor::hide(st);
    st.inited = false;
    unsafe {
        let a = os32api::api();
        /* FEP の描画先を TVRAM 版へ戻す (レビュー #4 ①)。gshell が抜けると同じ
         * 0x300000 帯に shell.bin が載るので、GFX 版の関数表を残すとカーネル FEP が
         * 上書き済みコードへ間接 call する。gfx_shutdown より前に行う。 */
        (a.ime_set_render)(core::ptr::null_mut());
        (a.gfx_shutdown)();
        (a.tvram_clear)();
        (a.sys_switch_shell)(CUI_SHELL.as_ptr());
    }
    0
}

/* ================================================================ */
/*  アプリの起動 (単独ループから。G2 の目視確認用)                    */
/* ================================================================ */

/// 単独ループ (契約 T8)。CUI への handoff が成立して gshell を抜けるときだけ
/// false を返す。
fn standalone_loop(st: &mut wm::GuiState) -> bool {
    while !st.quit {
        wm::wm_cycle(st, input::Ctx::Standalone);
        /* top-level (owner 1) で受けた CTRL+STOP (票 T8 D4d / T9 受入 S6)。 */
        top_level_abort(st);
        if st.launch_pending {
            st.launch_pending = false;
            launch_app(st);
        }
        /* 設定レジストリの予約と通知 (票 S4 §3)。**DB に触れるのはここだけ** —
         * `Req::Open` / `Req::Save` の open 〜 close を 1 周回の中で閉じ、
         * 間に yield / 合成 / 他イベント処理を挟まない。走っているアプリは
         * `multiapp::should_park` の (a) が `settings::pending()` を見て譲る
         * ので、予約が立てば必ずここへ来る。 */
        settings::consume(st);
        /* SessionAction のトップレベル handoff (契約 §7)。`launch_app` から
         * 戻った直後と、park で戻った周回でここを通る。**WM の文脈
         * (X1/X3/X4) からは絶対に来ない** = 入れ子 exec_run にならない。
         *
         * K5b-W: `LAUNCH` は「1 本増やす」なので他のアプリが生きていても
         * 実行してよい。`SWITCH_CUI` / `SHUTDOWN` だけが全回収を待つ
         * (判定は `session::ready_to_run`、契約 S2 の読み替え)。 */
        if session::ready_to_run(st) && !session_handoff(st) {
            /* SWITCH_CUI が成立した (shell 切替済み)。ここで gshell を抜ける。 */
            return false;
        }
        /* 起動要求表 (KAPI v49、票 T9 D3 (3)(4))。`session_handoff` と**同じ
         * 地点** = `exec_start` / `exec_resume` から戻った後、park 判定
         * (`multiapp::resume_one`) の前で、WM が自分の文脈 (owner 1) で走って
         * いる唯一の場所。`launch_take` / `launch_report` / `exec_kill` は
         * どれもここでしか通らない。 */
        drain_launch_requests(st);
        /* 全画面の後始末の保険 (票 T8-3、PM 実測 2026-09-12)。所有者が
         * `exec_kill` / fault で畳まれた経路は `exec_start` / `exec_resume` の
         * 復帰点を通らないので、所有者の問い合わせが 1 度も走らないことが
         * ある。全画面中は入力もタイマも実質止まる = 誰も ready にならない
         * ので、そのまま下の `sys_halt` へ落ちると画面が凍ったまま永久に
         * 待つ。**全画面中だけ** KAPI 1 本 (`gfx_screen_owner`) で見る。
         * ここは top-level (owner 1) なので復帰の `gfx_init` を呼んでよい。 */
        if fullscreen::active() {
            after_exec(st);
        }
        /* 止めてあるアプリのうち 1 本を起こす (D11-3 の (2))。起こす相手が
         * 居る間は halt しない — halt すると次の PIT まで誰も進めない。
         * ポーリングで譲った 1 本 (`WAIT_POLL`) もここで起きる (D8 の最下位:
         * `resume_one` → `pick` → `pick_poll`) ので、この `sys_halt` の側に
         * 別の判断は要らない。 */
        if multiapp::resume_one(st) {
            continue;
        }
        /* 待ちは sys_halt のみ (get_tick スピン禁止)。 */
        unsafe { (os32api::api().sys_halt)() };
    }
    true
}

/// top-level (owner 1) で受けた CTRL+STOP を処理する (票 T8 D4d / T9 受入 S6)。
///
/// `abort_seen` を消費する点は 3 つある:
///
/// | 点 | どんな周か |
/// |---|---|
/// | `handler.rs` の `op_wait` | アプリが待っていて WM がその中 (X3) で回っている |
/// | ここ (全画面) | 全画面 GFX プログラムが park していて WM は描かない (票 T8 D4d) |
/// | ここ (ウィンドウ) | **`WAIT_POLL` のアプリが居て WM が単独ループで回っている** |
///
/// 3 つ目が抜けていて実機の受入 S6 が落ちた (2026-09-13)。端末 → `sh` → 子の
/// 連鎖では、`sh` の `sys_yield` が `should_park` に端末を譲らせるので WM は
/// ずっと単独ループに居る。IRQ1 の `appslot_abort_request()` は「走っている
/// **アプリ**」にしか要求を立てられず (`g_cur` がシェル帯なら何もしない =
/// `ring3_abort_count` が増えない) 、raw リングから来る `abort_seen` だけが
/// 手がかりなのに、それを見る経路がここに無かった。
///
/// 全画面の枝は実機で通っている形 (T8 F8) をそのまま残す — `redirect_abort`
/// が予約を積み、同じ 1 周の `resume_one` → `drain_top_level` が実行する。
/// ウィンドウモードの枝は [`multiapp::abort_at_top_level`] が直に実行する
/// (予約は「表に載っている ID」にしか積めないが、連鎖の末尾が WM の表から
/// 落ちていても CTRL+STOP は届かなければならない)。
///
/// 戻り値は畳んだ ID (0 = 何も畳まなかった)。
fn top_level_abort(st: &mut wm::GuiState) -> i32 {
    if !st.abort_seen {
        return 0;
    }
    st.abort_seen = false;
    if fullscreen::active() {
        multiapp::redirect_abort(st, 0);
        return 0;
    }
    multiapp::abort_at_top_level(st)
}

/// デバッグ用の F1〜F5 経路 ([`DEBUG_SHORTCUTS`])。製品では `launch_pending` が
/// 立たないので呼ばれない (出荷形の起動は Start → Run... = `GUI_SESSION_LAUNCH`)。
fn launch_app(st: &mut wm::GuiState) {
    /* F1〜F4 / F5 のファイル選択が置いたパス。無ければ既定のデモ。 */
    let mut path = [0u8; 256];
    if st.launch_path_len > 0 {
        let mut i = 0;
        while i <= st.launch_path_len && i < 256 {
            path[i] = st.launch_path[i];
            i += 1;
        }
    } else {
        let p = LAUNCH_APPS[0];
        let mut i = 0;
        while i < p.len() && i < 256 {
            path[i] = p[i];
            i += 1;
        }
    }
    st.launch_path_len = 0;
    run_program(st, &path, LaunchVia::Wm);
}

/// 起動の依頼元 (票 T9 実装レビュー 1、2026-09-13)。**拒否と失敗の見せ方**が違う。
#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum LaunchVia {
    /// WM 自身の経路 (Start → Run... / Programs、デバッグの F1〜F5)。
    /// 断った理由は WM がモーダルで出し、戻りは [`RUN_REFUSED`] = `0`。
    Wm,
    /// 起動要求表 (KAPI v49、票 T9 D3)。**モーダルは出さない** — 要求者が
    /// `launch_poll` で `FAILED(rc)` を受けて自分の画面に出す。だから断った
    /// ときも `0` (= `DONE` = 正常終了) ではなく**負**を返さなければならない。
    Table,
}

/// 外部プログラムを 1 本**増やす** (K5b-W、決裁 D9-5)。
///
/// `exec_start` は塞がない起動で、戻ってくる条件は 2 つ:
///
/// | 戻り値 | 意味 | ここでやること |
/// |---|---|---|
/// | `> 0` | app_id。最初の `OP_WAIT` まで進んで park した | 譲り合いの表に載せる |
/// | `0` | park より前に終了した (回収済み、`gui_owner_exit` 配送済み) | 何もしない |
/// | `< 0` | 起動しなかった (`ERR_FULL` = 5 本目 / `ERR_NOMEM` / 見つからない) | 呼び出し元がエラー表示 |
///
/// 入口で断った CPL=0 プログラム (票 T8 D4) は [`RUN_REFUSED`] = `0` を返す —
/// 理由 (`cui only: <名>`) はここで出したので、呼ぶ側は何も足さない。
///
/// アプリが 1 本しか居ない間は誰も park しない (D11-3 の (1) の 1 行目) ので、
/// `exec_start` は従来の `exec_run` と同じく**アプリが終わるまで戻らない** —
/// これが「1 本のときは回帰ゼロ」の実体。
///
/// `path` は**この関数の呼び出し元が用意した NUL 終端の私有バッファ**
/// (契約 §7.1 の 3)。
fn run_program(st: &mut wm::GuiState, path: &[u8; 256], via: LaunchVia) -> i32 {
    cursor::hide(st);
    /* 前のアプリ宛の CTRL+STOP を持ち越さない (カーネル側 g_ring3_abort_req と同じ扱い)。 */
    st.abort_seen = false;
    /* フルスクリーン GFX プログラムに備えてパレット全体を退避する (契約 G6/G8)。 */
    let saved = wm::save_palette();
    /* 入口 (票 T8 D4): OS32X ヘッダの宣言を見る。`--cpl0` は GUI から起動せず
     * 理由を出す (カーネルの `OS32_ERR_INVAL` が最後の砦)。`--gfx` は
     * `exec_start` の前に全画面モードの印を立てる — 所有者が付くまでの隙間に
     * WM が上書きしないため (所有者の問い合わせと二重)。 */
    match os32x::classify_path(path) {
        os32x::Kind::CuiOnly => {
            /* 票 T9 実装レビュー 1 (blocker 1): 要求表経由では **負** を返す。
             * `RUN_REFUSED` = 0 をそのまま `launch_report` へ渡すと表は `DONE`
             * になり、要求者 (`sh`) は「起動して正常終了した」と読む — 実際は
             * 1 バイトも走っていない。理由はモーダルではなく `FAILED(rc)` で
             * 要求者へ渡し、要求者が自分の画面に出す。 */
            if via == LaunchVia::Table {
                return OS32_ERR_INVAL;
            }
            notify_cui_only(st, path);
            return RUN_REFUSED;
        }
        os32x::Kind::FullScreen => fullscreen::arm(&saved),
        os32x::Kind::Plain => {}
    }
    /* `exec_start` は「アプリが最初に park する」まで戻らない。その間、走って
     * いる ID はまだ分かっていない (戻り値そのものなので) ので、譲り合いの表に
     * 「起動が進行中」の印を立てておく (`multiapp::begin_start` の注記)。 */
    multiapp::begin_start();
    /* KAPI v45 (K5c) で生成器が `i32` を吐くようになったので、u32 経由の
     * 往復は要らない。生成物は手で触らない ([ABI1])。 */
    let rc = unsafe { (os32api::api().exec_start)(path.as_ptr()) };
    multiapp::end_start(rc);
    /* 所有者の問い合わせ (票 T8 D3/D4)。全画面に入った / 戻ったはここで決まる。
     * `rc > 0` (park した) でも全画面はありうる — K7 以降、GFX プログラムは
     * `kbd_getchar` で park するので `exec_start` は走り切る前に戻る。 */
    match fullscreen::observe() {
        /* 画面はプログラムのもの。WM は 1 画素も出さない (門は `wm` 側)。 */
        fullscreen::Change::Fullscreen => return rc,
        /* 所有者が WM に戻った = プログラムが抜けた。`rc <= 0` と同じ復帰。 */
        fullscreen::Change::Restored => {
            restore_screen(st, &fullscreen::palette());
            return rc;
        }
        fullscreen::Change::None => {}
    }
    /* 描画モードの復帰は **アプリが抜けたときだけ** (不具合 W-1、2026-09-11)。
     *
     * `gfx_init` は VRAM の両ページをゼロクリアする (`gfx/gfx_core.c`)。
     * 塞ぐ `exec_run` の時代はアプリが**終わってから**しか戻らなかったので、
     * 消えるのは死んだアプリの画だけだった。`exec_start` は park した時点で
     * 戻る (決裁 D9-5) ので、同じことをすると**生きているアプリのクライアント
     * 面まで消える**。WM はクライアント面を持たない (契約 G4) ので、消したら
     * 本人に描き直させるしか無いが、遮蔽は露出を生まないので
     * `recompute_and_expose` は dirty を 1 つも足さない = `derived_ready` が
     * 偽のまま = 誰も `exec_resume` しない = 露出部が黒のまま残る。
     *
     * ここへ来る `rc > 0` は「アプリが最初の `OP_WAIT` まで進んで park した」
     * = WM に attach 済みの GUI アプリ (画面は取っていない、上の問い合わせで
     * 確認済み。契約 T1 / gotcha §4-20)。復帰処理は要らない。 */
    if rc <= 0 {
        restore_screen(st, &saved);
        return rc;
    }
    /* 退避しておいた 16 色をそのまま戻し、念のためシステム色を入れ直してから、
     * まだ生きているリースがあれば再適用する。 */
    repaint_full(st, &saved);
    rc
}

/// 画面を WM の手に戻す (票 T8 D4 の復帰、および不具合 W-1 の `rc <= 0` の枝)。
///
/// `gfx_init` は VRAM の両ページをゼロクリアする (`gfx/gfx_core.c`) ので、
/// **消した以上は生き残っているアプリ全部に全面を描き直させる**
/// (`invalidate_all_clients`)。遮蔽は露出を生まないので
/// `recompute_and_expose` だけでは dirty が 1 つも増えず、露出部が黒のまま残る。
///
/// 呼ぶのは 2 か所だけ:
///
/// - `exec_start` が `rc <= 0` (起動しなかった / park より前に終わった) で戻った
/// - 全画面 GFX プログラムが抜けて所有者が WM (1) に戻った (`exec_start` /
///   `exec_resume` のどちらから戻った直後でも)
fn restore_screen(st: &mut wm::GuiState, saved: &[u8; 48]) {
    unsafe { (os32api::api().gfx_init)() };
    damage::invalidate_all_clients(st);
    repaint_full(st, saved);
}

/// パレットとリースを戻し、露出を配り直して全面を合成する (`gfx_init` は伴わない)。
fn repaint_full(st: &mut wm::GuiState, saved: &[u8; 48]) {
    wm::restore_palette(saved);
    wm::install_system_palette();
    lease::reapply(st);
    visible::recompute_and_expose(st);
    wm::composite_full(st);
}

/// `exec_start` / `exec_resume` から戻った直後の所有者の問い合わせ (票 T8 D3/D4)。
///
/// 戻り値 `true` = 全画面中 (WM は描かない)。所有者が WM に戻っていれば
/// [`restore_screen`] まで済ませて `false` を返す。`exec_resume` の側
/// (`multiapp::resume_one`) からはこれを呼ぶ。
pub(crate) fn after_exec(st: &mut wm::GuiState) -> bool {
    match fullscreen::observe() {
        fullscreen::Change::Fullscreen => true,
        fullscreen::Change::Restored => {
            restore_screen(st, &fullscreen::palette());
            false
        }
        fullscreen::Change::None => false,
    }
}

/// `cui only: <名>` (票 T8 D4 の入口)。VRAM を直接触る CPL=0 プログラムは
/// CUI に降りてから実行してもらう。
fn notify_cui_only(st: &mut wm::GuiState, path: &[u8; 256]) {
    const HEAD: &[u8] = b"cui only: ";
    let mut msg = [0u8; 96];
    let mut n = 0;
    while n < HEAD.len() {
        msg[n] = HEAD[n];
        n += 1;
    }
    let name = os32x::basename(path);
    let mut i = 0;
    while i < name.len() && n < msg.len() - 1 {
        msg[n] = name[i];
        n += 1;
        i += 1;
    }
    msg[n] = 0;
    modal::open_wm_message(st, GUI_MODAL_OK, &msg[..=n], modal::WM_PURPOSE_NOTIFY);
}

/* ================================================================ */
/*  SessionAction のトップレベル handoff (契約 V12-S §7、票 W3 §7)   */
/* ================================================================ */

/// 保留中の SessionAction を 1 件実行する。
///
/// 戻り値 `true` = デスクトップを続ける、`false` = gshell の `main` を抜ける
/// (SWITCH_CUI が成立し `sys_switch_shell` 済み)。SHUTDOWN は返らない。
///
/// **呼べるのは gshell の単独ループ (top-level) だけ**。ここが「WM の文脈から
/// `exec_run()` を呼ばない」(契約 S2) の担保になっている。
fn session_handoff(st: &mut wm::GuiState) -> bool {
    /* Programs の cache は世代が変わるので捨てる (契約 D2)。 */
    startmenu::invalidate_cache();
    match session::pending_action() {
        GUI_SESSION_LAUNCH => {
            /* §7.1: path を NUL 終端の私有バッファへ写し、**先に consume してから**
             * 起動する (失敗した path を無限 retry しない)。 */
            let mut path = [0u8; session::PATH_MAX + 1];
            let n = session::copy_path(&mut path);
            session::clear();
            if n == 0 {
                return true;
            }
            let mut buf = [0u8; 256];
            let mut i = 0;
            while i <= n && i < 256 {
                buf[i] = path[i];
                i += 1;
            }
            /* K5b-W: 5 本目は `ERR_FULL` で起動されない (受入 G3)。既存の
             * 4 本は無事なので、そのことが分かる文言を出す。 */
            let rc = run_program(st, &buf, LaunchVia::Wm);
            if rc < 0 {
                let msg: &[u8] = if rc == OS32_ERR_FULL {
                    b"Too many programs (4 max) - close one first\0"
                } else {
                    b"Launch failed (not found, not executable, or out of memory)\0"
                };
                modal::open_wm_message(st, GUI_MODAL_OK, msg, modal::WM_PURPOSE_NOTIFY);
            }
            true
        }
        GUI_SESSION_SWITCH_CUI => switch_cui(st),
        GUI_SESSION_SHUTDOWN => shutdown(st),
        _ => {
            session::clear();
            true
        }
    }
}

/// 起動要求表 (KAPI v49) を 1 周ぶん捌く (票 T9 §1 D3 (3)(4)、§10 non-blocker 4)。
///
/// **呼べるのは単独ループ (top-level) だけ** — `launch_take` / `launch_report` /
/// `exec_kill` はどれも owner 1 専用で (`res_owner_get()` が `op_wait` の中では
/// アプリ ID になる)、`exec_start` に至っては WM top-level からしか通らない
/// (契約 S2)。走っているアプリは [`multiapp::should_park`] の (a) が
/// `launch_pending()` を見て譲るので、要求が積まれれば必ずここへ来る。
///
/// | `kind` | ここでやること |
/// |---|---|
/// | `LAUNCH` | [`run_program`] を [`LaunchVia::Table`] で通す (`begin_start` / `end_start`、全画面の判定と復帰)。戻り (子 ID / 0 / 負) を**そのまま** `launch_report` へ — 入口の拒否もここでは負になる |
/// | `KILL` | [`multiapp::kill_for_request`] (`exec_kill` は子孫ごと末尾から畳む) → `launch_report(token, 0)` |
///
/// 2 つの約束:
///
/// - **失敗しても WM はモーダルを出さない**。要求者が `launch_poll` で
///   `FAILED(rc)` を受けて自分の画面に出す (D3 (3))。WM が割り込むと、端末の
///   プロンプトの前に WM のダイアログが乗る。Start → Run... の
///   `GUI_SESSION_LAUNCH` は従来どおり ([`session_handoff`])。
/// - **`launch_report` の `OS32_ERR_STALE` は正常** (§10 non-blocker 2)。KILL は
///   「take → `exec_kill` → 子の回収通知で `DONE` → report」の順になるので、
///   report が届くときには `TAKEN` ではない。再試行しない。
///
/// 戻り値 `true` = 1 件以上捌いた。
fn drain_launch_requests(st: &mut wm::GuiState) -> bool {
    let mut did = false;
    let mut guard = 0;
    /* 1 周で捌くのは要求表の本数ぶん (要求者 ID ごとに 1 本 = 4) まで。
     * `launch_take` が 0 を返さない壊れ方をしても単独ループを止めない。 */
    while guard < multiapp::MAX_APPS {
        guard += 1;
        /* SAFETY: KAPI の関数表は `os32_init` が入口で据えた有効なポインタ。
         * `launch_pending` は引数なし・戻りは値で、誰でも呼べる。 */
        if unsafe { (os32api::api().launch_pending)() } <= 0 {
            break;
        }
        /* `cap` は `LAUNCH_CMDLINE_MAX` 以上でなければ `OS32_ERR_INVAL` (§1a)。
         * カーネルは `kstrncpy` で NUL 終端して書くので、`run_program` の
         * 「NUL 終端の私有バッファ」(契約 §7.1 の 3) をそのまま満たす。 */
        let mut buf = [0u8; multiapp::LAUNCH_CMDLINE_MAX];
        let mut requester: i32 = 0;
        let mut kind: i32 = 0;
        let mut arg: i32 = 0;
        /* SAFETY: `buf` はこのスタックフレームの 256B で、`cap` にその実長を
         * 渡す (`LAUNCH_CMDLINE_MAX` 未満なら `OS32_ERR_INVAL`)。出力 3 本も
         * 同じフレームの `i32`。カーネルは `kstrncpy` で NUL 終端まで含めて
         * `cap` に収めて書く。呼べるのは owner 1 = ここ (top-level) だけ。 */
        let token = unsafe {
            (os32api::api().launch_take)(
                buf.as_mut_ptr(),
                buf.len() as u32,
                &mut requester,
                &mut kind,
                &mut arg,
            )
        };
        if token <= 0 {
            break; /* 0 = 無し、負 = 取りに来られない文脈 (どちらも次の周へ) */
        }
        did = true;
        /* 孤児回収 (要求者が退場済み) の表も `kind` は KILL だが、票 D3 (4) の
         * 「`LAUNCH_REQ_ORPHAN` も KILL として同じに扱う」を明示で読めるように
         * 印そのものも見る — 起動だけは要求者が生きている表に限る。 */
        let orphan = requester == multiapp::LAUNCH_REQ_ORPHAN;
        if kind == multiapp::LAUNCH_KIND_LAUNCH && !orphan {
            let rc = run_program(st, &buf, LaunchVia::Table);
            /* SAFETY: `launch_report` は値 2 つだけ。owner 1 から呼んでいる。 */
            unsafe { (os32api::api().launch_report)(token, rc) };
            continue;
        }
        if kind == multiapp::LAUNCH_KIND_KILL || orphan {
            multiapp::kill_for_request(arg);
        }
        /* 知らない `kind` (表が壊れた) もここへ落として報告だけする — 表を
         * `TAKEN` のまま残すと、その要求者は二度と `launch_req` できない。
         * 回収通知で先に `DONE` になっていれば `OS32_ERR_STALE` で、正常。
         * SAFETY: 値 2 つだけ。owner 1 から呼んでいる。 */
        unsafe { (os32api::api().launch_report)(token, 0) };
    }
    did
}

/// CUI へ戻す (契約 S6 の 1〜6)。cfg 更新に失敗したら**切替を実行せず**
/// デスクトップへ戻し、エラーを表示する (永続設定と実 shell の不一致を作らない)。
fn switch_cui(st: &mut wm::GuiState) -> bool {
    cursor::hide(st);
    st.inited = false;
    unsafe {
        let a = os32api::api();
        /* FEP の描画先を TVRAM 版へ戻す (gfx_shutdown より前)。 */
        (a.ime_set_render)(core::ptr::null_mut());
        (a.gfx_shutdown)();
    }
    if !cfg_set_gui(b"0") {
        /* 失敗: GFX を戻してデスクトップへ復帰し、action は捨てる。 */
        unsafe { (os32api::api().gfx_init)() };
        fep::install();
        st.inited = true;
        wm::install_system_palette();
        lease::reapply(st);
        visible::recompute_and_expose(st);
        wm::composite_full(st);
        session::clear();
        modal::open_wm_message(
            st,
            GUI_MODAL_OK,
            b"Cannot write /etc/system.cfg - staying in GUI\0",
            modal::WM_PURPOSE_NOTIFY,
        );
        return true;
    }
    session::clear();
    unsafe {
        let a = os32api::api();
        (a.tvram_clear)();
        (a.sys_switch_shell)(CUI_SHELL.as_ptr());
    }
    false
}

/// system halt (契約 S7)。`sys_halt()` は 1 回の `hlt` で IRQ 後に戻るので、
/// **必ず無限ループで呼ぶ**。通常コードへは二度と戻らない。
fn shutdown(st: &mut wm::GuiState) -> ! {
    cursor::hide(st);
    st.inited = false;
    unsafe {
        let a = os32api::api();
        (a.ime_set_render)(core::ptr::null_mut());
        (a.gfx_shutdown)();
        (a.tvram_clear)();
    }
    tvram_print(0, 0, b"System halted. Reset to restart.");
    loop {
        unsafe { (os32api::api().sys_halt)() };
    }
}

/// TVRAM へ ANK 文字列を 1 行置く (GFX を落とした後の最後の表示)。
fn tvram_print(x: i32, y: i32, s: &[u8]) {
    /* ATTR_WHITE (os32_kapi_shared.h)。 */
    const ATTR_WHITE: u8 = 0xE1;
    let a = unsafe { os32api::api() };
    let mut i = 0;
    while i < s.len() && s[i] != 0 {
        unsafe { (a.tvram_putchar_at)(x + i as i32, y, s[i], ATTR_WHITE) };
        i += 1;
    }
}

/* ================================================================ */
/*  /etc/system.cfg の GUI= 書き換え (契約 S6 の 4)                  */
/*                                                                  */
/*  userland/shell/cmd_sys.c の `cfg_set_key()` (os32gui on/off) と  */
/*  同じ規則: 既存の他キー行はそのまま残し、GUI= 行だけ差し替える。   */
/* ================================================================ */

/// `KEY=VALUE` 1 行分として確保しておく余白 (cmd_sys.c の CFG_LINE_RESERVE)。
const CFG_LINE_RESERVE: usize = 64;
const CFG_BUF: usize = 1024;
const CFG_OUT: usize = 1152;

fn cfg_set_gui(val: &[u8]) -> bool {
    let a = unsafe { os32api::api() };
    let mut buf = [0u8; CFG_BUF];
    let mut out = [0u8; CFG_OUT];

    /* 既存内容を読む。**存在しない**なら空から作ってよいが、**読めなかった**
     * のと**入り切らなかった**のは別で、そのまま進むと他のキーを道連れに
     * GUI= だけのファイルで上書きしてしまう (レビュー指摘 P3、2026-09-10)。
     * どちらも書き込みを中止する。 */
    let mut n = 0usize;
    unsafe {
        let fd = (a.sys_open)(SYSTEM_CFG.as_ptr(), 0 /* KAPI_O_RDONLY */);
        if fd >= 0 {
            let r = (a.sys_read)(fd, buf.as_mut_ptr(), (CFG_BUF - 1) as u32);
            (a.sys_close)(fd);
            if r < 0 {
                /* 開けたのに読めない。既存内容が分からないので触らない。 */
                return false;
            }
            n = r as usize;
            if n >= CFG_BUF - 1 {
                /* 上限まで読めた = 続きがあるかもしれない。切り捨てて
                 * 書き戻すと末尾のキーが消えるので中止する。 */
                return false;
            }
        }
        /* fd < 0 は「まだ無い」とみなして空から作る (既定の初回起動)。 */
    }

    /* 行ごとにコピー。旧 GUI= 行だけ捨てる。 */
    let mut o = 0usize;
    let mut i = 0usize;
    while i < n {
        let ls = i;
        while i < n && buf[i] != b'\n' {
            i += 1;
        }
        let len = i - ls;
        if i < n {
            i += 1; /* 改行を飛ばす */
        }
        if cfg_line_is_gui(&buf[ls..ls + len]) {
            continue;
        }
        let mut k = 0;
        while k < len && o < CFG_OUT - CFG_LINE_RESERVE {
            out[o] = buf[ls + k];
            o += 1;
            k += 1;
        }
        if o < CFG_OUT - 2 {
            out[o] = b'\n';
            o += 1;
        }
    }

    /* 新しい GUI=VALUE 行を追記。 */
    let key = b"GUI";
    let mut k = 0;
    while k < key.len() && o < CFG_OUT - 2 {
        out[o] = key[k];
        o += 1;
        k += 1;
    }
    if o < CFG_OUT - 2 {
        out[o] = b'=';
        o += 1;
    }
    let mut v = 0;
    while v < val.len() && val[v] != 0 && o < CFG_OUT - 2 {
        out[o] = val[v];
        o += 1;
        v += 1;
    }
    if o < CFG_OUT - 1 {
        out[o] = b'\n';
        o += 1;
    }

    unsafe {
        /* KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC (os32_kapi_shared.h)。 */
        let fd = (a.sys_open)(SYSTEM_CFG.as_ptr(), 0x01 | 0x0100 | 0x0200);
        if fd < 0 {
            return false;
        }
        let w = (a.sys_write)(fd, out.as_ptr(), o as u32);
        (a.sys_close)(fd);
        w == o as i32
    }
}

/// 行が `GUI=` の代入行か (前後空白許容。cmd_sys.c の `cfg_line_is_key`)。
fn cfg_line_is_gui(line: &[u8]) -> bool {
    let key = b"GUI";
    let mut i = 0usize;
    while i < line.len() && (line[i] == b' ' || line[i] == b'\t') {
        i += 1;
    }
    let mut k = 0usize;
    while k < key.len() {
        if i >= line.len() || line[i] != key[k] {
            return false;
        }
        i += 1;
        k += 1;
    }
    while i < line.len() && (line[i] == b' ' || line[i] == b'\t') {
        i += 1;
    }
    i < line.len() && line[i] == b'='
}
