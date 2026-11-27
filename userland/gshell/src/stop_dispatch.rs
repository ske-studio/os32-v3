use super::{abort_target, kill_for_request, m, APP_ID_SHELL};
use crate::{fullscreen, multiapp, wm::GuiState};

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
pub(crate) fn top_level_abort(st: &mut GuiState) -> i32 {
    if !st.abort_seen {
        return 0;
    }
    st.abort_seen = false;
    if fullscreen::active() {
        multiapp::redirect_abort(st, 0);
        return 0;
    }
    abort_at_top_level(st)
}

/// top-level (owner 1) で受けた CTRL+STOP を**その場で実行する**
/// (票 T9 D8、実機受入 S6 の修正 2026-09-13)。呼ぶのは単独ループの
/// [`crate::top_level_abort`] だけ。戻り値は畳んだ ID (0 = 何も畳まなかった)。
///
/// ここは既に owner 1 の文脈なので、`exec_abort_clear` も `exec_kill` も
/// その場で通る (`drain_top_level` と同じ地点)。
///
/// `exec_abort_clear()` を**必ず先に**呼ぶ (決裁 A1 と同じ順序)。IRQ1 は宛先を
/// 選べないので、走っていた別の 1 本に要求が載っていることがある — 降ろさずに
/// 畳むと、その 1 本が次の syscall の出口で巻き添えで死ぬ。
fn abort_at_top_level(st: &GuiState) -> i32 {
    /* `op_wait` の中で見た周は `handler.rs` が [`redirect_abort`] で予約を
     * 積んでいて、同じ 1 周の [`drain_top_level`] が実行する。ここで重ねると
     * 連鎖の末尾を 2 本ぶん畳む。 */
    if m().abort_clear_req {
        return 0;
    }
    let target = abort_target(st);
    /* SAFETY: KAPI の関数表は `os32_init` が据えた有効なポインタ。
     * `exec_abort_clear` は引数なし・戻りは値で、owner 1 から呼んでいる。 */
    unsafe {
        (os32api::api().exec_abort_clear)();
    }
    if target == 0 || target == APP_ID_SHELL {
        /* 宛先が居ない (窓もスロットも無い) / シェル帯。要求を降ろしただけ。 */
        return 0;
    }
    kill_for_request(target);
    target
}
