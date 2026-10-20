//! font_tests.rs — T7b-b3 S3: gshell の font engine の持ち主 (src/font.rs) のホスト TDD。
//!
//! service 表 v2 は `mocks::fontsvc` の贋物で、lease は実物の標準 set
//! (JF-Dot-Shinonome16 / Inconsolata Regular、integration.py が fetch_fonts で揃える)。
//! 見るもの:
//! - lease の open/close は guard の外、guard の中は read_at だけ (b12 §3-3)
//! - 失敗の理由 (NoFpu / OldShell / OpenFailed / Nospc / Busy / Config / Stale) と後始末
//! - 失敗の再試行は次の consume で 1 回だけ
//! - text() の入口: 検証は face ごとに 1 回、cache に無いときだけ guard を 1 回
//! - trim: cache は空になり arena と lease は残る、次の描画で作り直す (b3④)
use super::*;
use crate::mocks::{self, fontsvc};
use crate::{settings, text, wm};
use os32_font::engine::{Reason, State};

fn bare() -> wm::GuiState {
    mocks::init();
    let mut st = wm::GuiState::NEW;
    st.screen_w = 640;
    st.screen_h = 400;
    st
}

/// 表を置いて標準 set を開く。
fn opened() -> wm::GuiState {
    let st = bare();
    fontsvc::install(SHELL_VERSION);
    assert_eq!(open_at_safe_point(&st, 0), 0, "標準 set が開けない");
    st
}

const SHELL_VERSION: u32 = crate::ffi::SHELL_SERVICE_VERSION;

fn kprinted(needle: &[u8]) -> bool {
    mocks::kprint_lines().iter().any(|l| l.windows(needle.len()).any(|w| w == needle))
}

#[test]
fn f10_open_uses_leases_outside_guard_and_reads_only_inside() {
    let _st = opened();
    assert_eq!(engine().state(), State::Ready);
    assert_eq!(engine().set_slot(), 1);
    assert_eq!(fontsvc::open_count(), 2, "lease 2 本 (latin / ja、束は無い = NOTFOUND)");
    let calls = fontsvc::calls();
    assert_eq!(&calls[..4], &[("open", false), ("open", false), ("open", false), ("guard", false)],
               "open (latin / ja / 束) は guard の前、guard の外");
    assert!(calls.iter().all(|c| c.1 == (c.0 == "read_at")),
            "read_at は guard の中だけ、他は guard の外だけ: {calls:?}");
    assert_eq!(fontsvc::with(|f| f.violations), 0);
    assert!(engine().cache().has_arena(), "arena を付けていない");
    let s = unsafe { gshell_font_stats };
    assert_eq!((s.state, s.opens, s.open_fail, s.last_rc, s.set_slot), (ST_READY, 1, 0, 0, 1));
    assert!(kprinted(b"font: gen 0->1 open=OK set=0 rc=0"), "open のログ");
    /* 見本 1 字 ('亜') は東雲の bitmap (b3⑦ の比較材料)。 */
    let sample = unsafe { gshell_font_sample };
    assert_eq!((sample.cp, sample.kind), (0x4E9C, 1));
    assert!(sample.width > 0 && sample.height > 0 && sample.bits.iter().any(|b| *b != 0));
}

#[test]
fn f11_reopen_closes_old_leases_first_and_bumps_slot() {
    let st = opened();
    let n = fontsvc::calls().len();
    assert_eq!(open_at_safe_point(&st, 0), 0);
    let calls = fontsvc::calls();
    assert_eq!(&calls[n..n + 4], &[("close", false), ("close", false), ("open", false), ("open", false)],
               "旧 lease を閉じてから開く");
    assert_eq!(fontsvc::open_count(), 2);
    assert_eq!(engine().set_slot(), 2);
    /* 設定で KCG のみ: lease を全部閉じ、開かない。 */
    assert_eq!(open_at_safe_point(&st, 1), 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::Config));
    assert_eq!(fontsvc::open_count(), 0);
    assert!(!retry_armed(), "設定の KCG は再試行しない");
    /* 終了は全部閉じる。 */
    assert_eq!(open_at_safe_point(&st, 0), 0);
    close_all();
    assert_eq!(fontsvc::open_count(), 0);
    assert_eq!(engine().state(), State::Closed);
}

#[test]
fn f12_open_failures_pick_the_reason_and_retry_once() {
    /* ja の open 失敗: latin は guard の外で閉じ、次の consume で 1 回だけ開き直す。 */
    let mut st = bare();
    fontsvc::install(SHELL_VERSION);
    fontsvc::with(|f| f.open_fail = Some(("Shinonome16.ttf", -2)));
    assert_eq!(open_at_safe_point(&st, 0), -2);
    assert_eq!(engine().state(), State::KcgOnly(Reason::OpenFailed));
    assert_eq!(fontsvc::open_count(), 0, "開けた latin を閉じていない");
    assert!(fontsvc::calls().iter().all(|c| !c.1), "guard に入っていない");
    assert!(retry_armed());
    assert!(consume_retry(&mut st), "再試行していない");
    assert_eq!(fontsvc::count("open"), 4);
    assert!(!retry_armed(), "再試行の失敗で再び予約した (1 回だけ)");
    assert!(!consume_retry(&mut st));
    /* 再試行で開ければ Ready。 */
    fontsvc::with(|f| f.open_fail = None);
    assert_eq!(open_at_safe_point(&st, 0), 0);
    assert_eq!(engine().state(), State::Ready);

    /* NOSPC (app の lease で埋まっている) も再試行の対象。 */
    let st = bare();
    fontsvc::install(SHELL_VERSION);
    fontsvc::with(|f| f.open_fail = Some(("Regular.ttf", crate::ffi::OS32_ERR_NOSPC)));
    open_at_safe_point(&st, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::Nospc));
    assert!(retry_armed());

    /* shell open の NOSYS = FPU 無し: 恒久、再試行しない。 */
    let st = bare();
    fontsvc::install(SHELL_VERSION);
    fontsvc::with(|f| f.open_fail = Some(("Regular.ttf", crate::ffi::OS32_ERR_NOSYS)));
    open_at_safe_point(&st, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::NoFpu));
    assert!(!retry_armed());

    /* guard の NOSYS: callback は走らず、lease は guard の外で閉じる。 */
    let st = bare();
    fontsvc::install(SHELL_VERSION);
    fontsvc::with(|f| f.guard_ret = Some(crate::ffi::OS32_ERR_NOSYS));
    open_at_safe_point(&st, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::NoFpu));
    assert_eq!(fontsvc::open_count(), 0);
    assert!(!retry_armed());

    /* guard の BUSY: KcgOnly(Busy)、次の consume で 1 回。 */
    let st = bare();
    fontsvc::install(SHELL_VERSION);
    fontsvc::with(|f| f.guard_ret = Some(crate::ffi::OS32_ERR_BUSY));
    open_at_safe_point(&st, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::Busy));
    assert_eq!(fontsvc::open_count(), 0);
    assert!(retry_armed());

    /* 版 1 の表: 開かず KcgOnly(OldShell)。表が無い (app と同じ 0): OpenFailed、再試行なし。 */
    let st = bare();
    fontsvc::install(SHELL_VERSION - 1);
    open_at_safe_point(&st, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::OldShell));
    assert_eq!(fontsvc::count("open"), 0);
    let st = bare();
    open_at_safe_point(&st, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::OpenFailed));
    assert!(!retry_armed());
}

#[test]
fn f13_engine_open_failure_inside_guard_defers_the_close() {
    let st = bare();
    fontsvc::install(SHELL_VERSION);
    fontsvc::with(|f| f.read_fail = true);
    assert!(open_at_safe_point(&st, 0) < 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::OpenFailed));
    assert_eq!(fontsvc::with(|f| f.violations), 0, "guard の中で close した");
    assert_eq!(fontsvc::open_count(), 0, "Face::open の失敗で lease が残った");
    let calls = fontsvc::calls();
    let closes: Vec<_> = calls.iter().filter(|c| c.0 == "close").collect();
    assert_eq!(closes.len(), 2);
    assert!(closes.iter().all(|c| !c.1));
}

#[test]
fn f14_text_checks_once_and_guards_only_on_miss() {
    let _st = opened();
    let clip = wm::Rect::new(0, 0, 640, 400);
    let n_guard = fontsvc::count("guard");
    let n_validate = fontsvc::count("validate");
    text::draw(clip, 0, 0, "Ag亜あ¥".as_bytes(), 1, 0);
    assert_eq!(fontsvc::count("validate") - n_validate, 2, "face ごとに 1 回");
    assert_eq!(fontsvc::count("guard") - n_guard, 1, "miss の raster は guard 1 回");
    assert_eq!(fontsvc::with(|f| f.violations), 0);
    /* 2 回目は全部 hit: guard に入らず read_at もしない。 */
    let reads = fontsvc::with(|f| f.reads);
    text::draw(clip, 0, 20, "Ag亜あ¥".as_bytes(), 1, 0);
    assert_eq!(fontsvc::count("guard") - n_guard, 1, "hit で guard に入った");
    assert_eq!(fontsvc::with(|f| f.reads), reads, "hit で read_at した");
    /* TTF に行った字は KCG を引かない (¥ は ROM の ANK)。 */
    assert_eq!(mocks::kcg_reads(), vec![('a', 0x5C), ('a', 0x5C)]);
    let s = unsafe { gshell_font_stats };
    assert_eq!(s.texts, 2);
    assert_eq!(s.over_slot, 0);
    assert!(s.cache.hit_bitmap >= 4);
}

#[test]
fn f15_guard_busy_draws_this_call_with_kcg_only() {
    let _st = opened();
    let clip = wm::Rect::new(0, 0, 640, 400);
    /* open の ASCII 先読み (F3) を捨てて、Q / Z を miss にする。 */
    assert!(trim());
    fontsvc::with(|f| f.guard_ret = Some(crate::ffi::OS32_ERR_BUSY));
    text::draw(clip, 0, 0, b"Q", 1, 0);
    assert_eq!(mocks::kcg_reads(), vec![('a', b'Q' as u16)], "BUSY の回は KCG");
    assert_eq!(engine().state(), State::Ready, "BUSY は恒久にしない");
    assert_eq!(unsafe { gshell_font_stats }.cache.guard_busy, 1);
    fontsvc::with(|f| f.guard_ret = None);
    text::draw(clip, 0, 0, b"Q", 1, 0);
    assert_eq!(mocks::kcg_reads().len(), 1, "次の回は TTF");
    /* guard の NOSYS は以後ずっと KCG (lease は閉じる)。 */
    fontsvc::with(|f| f.guard_ret = Some(crate::ffi::OS32_ERR_NOSYS));
    text::draw(clip, 0, 0, b"Z", 1, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::NoFpu));
    assert_eq!(fontsvc::open_count(), 0);
}

#[test]
fn f16_stale_lease_falls_back_to_kcg_and_closes() {
    let _st = opened();
    let clip = wm::Rect::new(0, 0, 640, 400);
    fontsvc::with(|f| f.validate_ret = os32api::gui::proto::OS32_ERR_STALE);
    text::draw(clip, 0, 0, b"A", 1, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::Stale));
    assert_eq!(mocks::kcg_reads(), vec![('a', b'A' as u16)]);
    assert_eq!(fontsvc::open_count(), 0, "STALE の lease を閉じていない");
    assert!(fontsvc::calls().iter().filter(|c| c.0 == "close").all(|c| !c.1));
}

/// Fable の S3 レビュー (軽): WM の handler / pump の中で Stale になると service は
/// close を INVAL で断る。断られた close は保留し、次の `settings::consume` の頭
/// (`consume_retry`、top-level) で 1 回だけ閉じ直す。top-level でも断られたら諦める
/// (retire が閉じる、毎周叩かない)。
#[test]
fn f16b_stale_inside_wm_defers_the_rejected_close_to_consume() {
    use os32api::gui::proto::{OS32_ERR_INVAL, OS32_ERR_STALE};
    let mut st = opened();
    let clip = wm::Rect::new(0, 0, 640, 400);
    fontsvc::with(|f| {
        f.validate_ret = OS32_ERR_STALE;
        f.close_fail = Some(OS32_ERR_INVAL);
    });
    let closes0 = unsafe { gshell_font_stats }.closes;
    text::draw(clip, 0, 0, b"A", 1, 0);
    assert_eq!(engine().state(), State::KcgOnly(Reason::Stale));
    assert_eq!(fontsvc::open_count(), 2, "断られた close で lease が閉じたことになっている");
    assert_eq!(deferred_closes(), 2, "断られた close を保留していない");
    assert_eq!(unsafe { gshell_font_stats }.closes, closes0, "断られた close を数えた");
    /* top-level に戻った: consume の頭で閉じ直す (retry は立っていない)。 */
    fontsvc::with(|f| f.close_fail = None);
    assert!(!retry_armed());
    settings::consume(&mut st);
    assert_eq!(fontsvc::open_count(), 0, "consume で保留の close を流していない");
    assert_eq!(deferred_closes(), 0);
    assert_eq!(unsafe { gshell_font_stats }.closes, closes0 + 2);
    assert!(fontsvc::calls().iter().filter(|c| c.0 == "close").all(|c| !c.1));

    /* top-level でも断られる close は 1 回で諦める。 */
    let mut st = opened();
    fontsvc::with(|f| {
        f.validate_ret = OS32_ERR_STALE;
        f.close_fail = Some(OS32_ERR_INVAL);
    });
    text::draw(clip, 0, 0, b"A", 1, 0);
    assert_eq!(deferred_closes(), 2);
    let n = fontsvc::count("close");
    settings::consume(&mut st);
    assert_eq!(fontsvc::count("close") - n, 2, "保留の close を 1 回ずつ");
    assert_eq!(deferred_closes(), 0, "top-level で断られた close を積み直した");
    settings::consume(&mut st);
    assert_eq!(fontsvc::count("close") - n, 2, "次の周も同じ id を叩いた");
}

#[test]
fn f17_trim_empties_cache_keeps_arena_and_leases_then_rebuilds() {
    let _st = opened();
    let clip = wm::Rect::new(0, 0, 640, 400);
    text::draw(clip, 0, 0, "Ag亜".as_bytes(), 1, 0);
    let first = mocks::pixels();
    assert!(engine().cache().n_valid() > 0);
    assert!(trim());
    assert_eq!(engine().cache().n_valid(), 0, "trim で cache が空にならない");
    assert!(engine().cache().has_arena(), "trim で arena を手放した (gshell は固定 heap)");
    assert_eq!(fontsvc::open_count(), 2, "trim で lease を閉じた");
    mocks::clear(9);
    let n_guard = fontsvc::count("guard");
    text::draw(clip, 0, 0, "Ag亜".as_bytes(), 1, 0);
    assert_eq!(fontsvc::count("guard") - n_guard, 1, "trim の後に作り直していない");
    assert_eq!(mocks::pixels(), first, "trim の前後で字形が違う");
    assert!(mocks::kcg_reads().is_empty());
}

#[test]
fn f18_settings_change_reconfigures_through_consume() {
    /* 起動時は標準 set、設定で KCG のみ → 標準に戻す: 毎回 close → open。 */
    let mut st = opened();
    mocks::cfg(|c| c.font_gen = Some(3));
    settings::request_open();
    settings::consume(&mut st);
    for _ in 0..2 {
        crate::modal::on_key(&mut st, 0x3D, 0, 0); /* ↓ */
    }
    crate::modal::on_key(&mut st, 0x3C, 0, 0); /* → (KCG のみ) */
    assert!(crate::modal::on_key(&mut st, 0x1C, 0, 0)); /* RETURN */
    settings::consume(&mut st);
    assert_eq!(st.cfg.font_set, 1);
    assert_eq!(engine().state(), State::KcgOnly(Reason::Config));
    assert_eq!(fontsvc::open_count(), 0);
    assert!(kprinted(b"font: gen 1->1 open=KCG(config) set=1"));
}

/// TASK_FONTPERF F3: 束 (.g16) の 3 本目の lease。open の guard の中で束を開き、
/// 先読み節 (SHELL_PRELOAD_MAX) と ASCII 95 字を cache に入れる。設定変更 20 回で
/// lease の数は 3 のまま (F-8: face と同じ経路で閉じる)、Stale・close_all で全部閉じる。
#[test]
fn f19_bundle_lease_preload_and_close_paths() {
    use os32_font::limits::{PRELOAD_ASCII_FIRST, PRELOAD_ASCII_LAST, SHELL_PRELOAD_MAX};
    let mut st = bare();
    fontsvc::install(SHELL_VERSION);
    fontsvc::with(|f| f.g16 = true);
    assert_eq!(open_at_safe_point(&st, 0), 0);
    assert_eq!(fontsvc::open_count(), 3, "束の lease");
    let calls = fontsvc::calls();
    assert_eq!(&calls[..4], &[("open", false), ("open", false), ("open", false), ("guard", false)]);
    assert!(calls.iter().all(|c| c.1 == (c.0 == "read_at")), "束も read_at は guard の中だけ: {calls:?}");
    let s = unsafe { gshell_font_stats };
    let (n_all, n_pre) = engine().bundle_counts();
    let ascii = (PRELOAD_ASCII_FIRST..=PRELOAD_ASCII_LAST).count() as u32;
    assert_eq!((s.bundle.ok, s.bundle.rejected), (1, 0));
    assert_eq!(s.preloaded, n_pre.min(SHELL_PRELOAD_MAX as u32) + ascii, "先読みの字数");
    assert!(kprinted(format!("font: g16 ok n_all={n_all} n_pre={n_pre} pre={n_pre}+{ascii}").as_bytes()));
    println!("gshell g16: n_all={n_all} n_pre={n_pre} preloaded={}", s.preloaded);
    /* 先読みの字は guard にも read_at にも行かない。 */
    let clip = wm::Rect::new(0, 0, 640, 400);
    let (n_guard, reads) = (fontsvc::count("guard"), fontsvc::with(|f| f.reads));
    text::draw(clip, 0, 0, "Agあア・".as_bytes(), 1, 0);
    assert_eq!((fontsvc::count("guard"), fontsvc::with(|f| f.reads)), (n_guard, reads));
    /* 設定変更 20 回 (close → open): lease は 3 本のまま、閉じた数は 3 ずつ。 */
    let closes0 = unsafe { gshell_font_stats }.closes;
    for round in 0..20 {
        reconfigure(&mut st, 0);
        assert_eq!((fontsvc::open_count(), deferred_closes()), (3, 0), "round {round}");
    }
    assert_eq!(unsafe { gshell_font_stats }.closes - closes0, 60);
    /* Stale: face と一緒に束も閉じる。 */
    fontsvc::with(|f| f.validate_ret = os32api::gui::proto::OS32_ERR_STALE);
    text::draw(clip, 0, 0, b"A", 1, 0);
    assert_eq!((engine().state(), fontsvc::open_count()), (State::KcgOnly(Reason::Stale), 0));
    fontsvc::with(|f| f.validate_ret = 0);
    assert_eq!(open_at_safe_point(&st, 0), 0);
    assert_eq!(fontsvc::open_count(), 3);
    close_all();
    assert_eq!(fontsvc::open_count(), 0);
    assert_eq!(fontsvc::with(|f| f.violations), 0);
}
