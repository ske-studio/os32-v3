//! launch.rs — 起動要求表 (KAPI v49) を端末側から読む純粋部分 (票 T9 D4 / D9)。
//!
//! `prompt.rs` / `sink.rs` と同じ流儀で、ここには `no_std` の純関数と小さな
//! 状態だけを置く。KAPI (`launch_req` / `launch_poll` / `launch_cancel`) を
//! 実際に呼ぶのは `guest.rs` で、こちらは「返ってきた値をどう読むか」と
//! 「次に何をするか」だけを決める。ホスト試験がそのまま取り込める。
//!
//! ## なぜ要求表なのか (票 T9 §0 / D4)
//!
//! GUI 中の CPL=3 アプリは入れ子 `exec_run` を使えない (子が park できず協調型
//! 全体が止まる)。端末は `session_launch` で「1 本増やす」ことはできたが、
//! **子 ID も終了も分からない**ので、con_sink の `EXIT` レコードをモード判定に
//! 使っていた。要求表は要求者ごとに 1 本あり、token で照合して子 ID と完了を
//! 問い合わせられる (票 §7 blocker: 制御情報をリングに載せない)。
//!
//! ## この端末が守ること
//!
//! - `EXIT` レコードは**表示だけ** (D4)。モードは `launch_poll` の答えで動かす。
//! - 接続モードの ESC は `launch_cancel` (D9)。子には注入しない。
//! - `OS32_ERR_AGAIN` (WM がまだ取っていない) は次のタイマで自動再試行。
//! - `exit` / 端末自身の終了では取消を出さない — 要求者の退場はカーネルが
//!   孤児回収する (`launch_owner_exit` が KILL を積む、票 §10 non-blocker 1)。

/* ================================================================ */
/*  ワイヤ定数 (sdk/include/os32/os32_kapi_shared.h が正典)          */
/* ================================================================ */

/// `LAUNCH_ST_PENDING` — 要求は積まれたが WM がまだ取っていない。
pub const ST_PENDING: i32 = 0x000;
/// `LAUNCH_ST_TAKEN` — WM が取った (`exec_start` 中)。
pub const ST_TAKEN: i32 = 0x001;
/// `LAUNCH_ST_RUNNING` — 下位に子 ID が乗る。
pub const ST_RUNNING: i32 = 0x100;
/// `LAUNCH_ST_DONE` — 子が終わった (短命な子は最初の poll でこれ)。
pub const ST_DONE: i32 = 0x200;
/// `LAUNCH_ST_FAILED` — 下位に `-rc` が乗る。
pub const ST_FAILED: i32 = 0x300;
/// status の下位 (子 ID / `-rc`) を取り出す遮蔽。
pub const ST_LOW: i32 = 0xFF;
/// status の上位 (種別)。
pub const ST_HIGH: i32 = 0xFF00;

/// `OS32_ERR_STALE` — 表はもう IDLE (完了を 1 度渡した後など)。
/// guest は負の戻り値を理由で分けない (どれもプロンプトへ戻る) ので、
/// 名前を持つのは試験と読み手のため。
#[allow(dead_code)]
pub const ERR_STALE: i32 = -11;
/// `OS32_ERR_FULL` — 自分の表がまだ空いていない (前の要求が残っている)。
pub const ERR_FULL: i32 = -13;
/// `OS32_ERR_AGAIN` — `PENDING` / `TAKEN` への取消。WM がまだ取っていない。
pub const ERR_AGAIN: i32 = -14;

/* ================================================================ */
/*  status の読み方                                                  */
/* ================================================================ */

/// `launch_poll` が書いた status の意味 (§1a の ABI 表)。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase {
    /// 積んだだけ。WM はまだ取っていない。
    Pending,
    /// WM が取った (`exec_start` 中)。
    Taken,
    /// 子が走っている。中身は子 ID。
    Running(i32),
    /// 子が終わった。表はこの poll で IDLE に戻っている。
    Done,
    /// 起動できなかった。中身は元の負の `rc`。
    Failed(i32),
    /// 知らない値。固まらないようプロンプトへ戻す材料にする。
    Unknown,
}

/// **純関数**: status → [`Phase`]。
///
/// `DONE` の下位は規約上 0 だが、来ても `Done` に倒す — 知らないビットで
/// 接続モードに居座る方が害が大きい。負の status は `Unknown`。
pub fn phase(status: i32) -> Phase {
    if status < 0 {
        return Phase::Unknown;
    }
    let low = status & ST_LOW;
    match status & ST_HIGH {
        0 => match low {
            ST_PENDING => Phase::Pending,
            ST_TAKEN => Phase::Taken,
            _ => Phase::Unknown,
        },
        ST_RUNNING => Phase::Running(low),
        ST_DONE => Phase::Done,
        /* FAILED は `0x300 + (-rc)` なので符号を戻す。0x300 ちょうど
         * (rc = 0) は FAILED にならないはずだが、来たら 0 のまま渡す。 */
        ST_FAILED => Phase::Failed(-low),
        _ => Phase::Unknown,
    }
}

/* ================================================================ */
/*  接続中の 1 件                                                    */
/* ================================================================ */

/// 取消 (D9) の進み具合。ESC 連打で二重に `launch_cancel` を出さないための印。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Cancel {
    /// 頼まれていない。
    No,
    /// ESC を受けたが `OS32_ERR_AGAIN` だった (WM がまだ取っていない)。
    /// 次のタイマでもう一度出す。
    Retry,
    /// `launch_cancel` が 0 を返した = 取消が積まれた。`DONE` を待つだけ。
    Armed,
}

/// `launch_req` が受け付けた 1 件 (token とそれに紐づく子 / 取消の進み具合)。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Attach {
    /// 表の照合鍵。要求者 ID ではなく token で照合する (票 §9 4)。
    pub token: i32,
    /// `RUNNING` で分かった子 ID。0 = まだ分からない (表示にだけ使う)。
    pub child: i32,
    /// 取消の進み具合。
    pub cancel: Cancel,
}

/// 1 回の `launch_poll` / `launch_cancel` の後に端末がすること。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Step {
    /// 何も変わらない (描き直しも要らない)。
    Idle,
    /// 表示が変わった (子 ID が分かった等)。最下行を描き直す。
    Redraw,
    /// `launch_cancel` を (もう一度) 出す。
    Retry,
    /// 接続モードを畳んでプロンプトへ戻る。
    Finish(Outcome),
}

/// プロンプトへ戻る理由。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Outcome {
    /// 子が終わった (取消で畳まれた場合もここ)。何も出さない。
    Done,
    /// 起動できなかった。`launch failed (<rc>)` を出す。
    Failed(i32),
    /// `launch_poll` 自体が負 / 意味不明な status。異常系だが固まらない。
    Lost(i32),
}

impl Attach {
    /// `launch_req` が返した token (> 0) から始める。
    pub fn new(token: i32) -> Self {
        Self {
            token,
            child: 0,
            cancel: Cancel::No,
        }
    }

    /// 取消を頼まれているか (最下行の表示に使う)。
    pub fn cancelling(&self) -> bool {
        self.cancel != Cancel::No
    }

    /// `launch_poll(token, &status)` の結果を読む (D4)。
    ///
    /// `rc` が負なら理由を問わずプロンプトへ戻す — `OS32_ERR_STALE` (表はもう
    /// IDLE) も含めて、接続モードに居座る理由が無い。
    pub fn poll(&mut self, rc: i32, status: i32) -> Step {
        if rc < 0 {
            return Step::Finish(Outcome::Lost(rc));
        }
        match phase(status) {
            Phase::Done => Step::Finish(Outcome::Done),
            Phase::Failed(code) => Step::Finish(Outcome::Failed(code)),
            Phase::Unknown => Step::Finish(Outcome::Lost(status)),
            Phase::Running(child) => {
                let changed = self.child != child;
                self.child = child;
                /* 取消が AGAIN で止まっていたなら、RUNNING になった今こそ
                 * 通る (D9: RUNNING → KILL(child) の PENDING)。 */
                if self.cancel == Cancel::Retry {
                    Step::Retry
                } else if changed {
                    Step::Redraw
                } else {
                    Step::Idle
                }
            }
            /* PENDING / TAKEN は何もしない。取消待ちなら再試行だけ出す。 */
            Phase::Pending | Phase::Taken => {
                if self.cancel == Cancel::Retry {
                    Step::Retry
                } else {
                    Step::Idle
                }
            }
        }
    }

    /// 接続モードで ESC を受けた (D9)。戻り値が真なら `launch_cancel` を出す。
    ///
    /// 既に出してある (`Retry` / `Armed`) なら何もしない — 連打しても表には
    /// `AGAIN` / `STALE` が返るだけで害は無いが、呼ぶ回数は 1 回にまとめる。
    pub fn escape(&mut self) -> bool {
        if self.cancel == Cancel::No {
            self.cancel = Cancel::Retry;
            true
        } else {
            false
        }
    }

    /// `launch_cancel(token)` の戻り値を読む (D9)。
    ///
    /// - `0`: 取消が積まれた。接続モードのまま `DONE` を待つ (子には注がない)。
    /// - `OS32_ERR_AGAIN`: WM がまだ取っていない。次のタイマで再試行。
    /// - それ以外 (`OS32_ERR_STALE` = **もう `DONE` / `FAILED` になっている**、
    ///   または不一致): **token は捨てない**。K の契約 (`include/launch.h`) では
    ///   完了した表は `launch_poll` が消費して初めて `IDLE` に戻るので、ここで
    ///   プロンプトへ戻ると誰も消費せず、同じ端末の次の `launch_req` が
    ///   `OS32_ERR_FULL` で固着する (実装レビュー往復 1/3 の blocker)。取消待ちの
    ///   まま次のタイマで poll を続け、`DONE` / `FAILED` を消費してから戻る。
    ///   本当に不一致なら poll が負を返すので [`Outcome::Lost`] で戻る。
    pub fn cancelled(&mut self, rc: i32) -> Step {
        if rc == ERR_AGAIN {
            /* WM がまだ取っていない。次の poll でもう一度出す。 */
            self.cancel = Cancel::Retry;
        } else {
            /* 0 (取消が積まれた) も STALE (もう完了している) も、あとは
             * `launch_poll` が `DONE` / `FAILED` を渡すのを待つだけ。 */
            self.cancel = Cancel::Armed;
        }
        Step::Redraw
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_decodes_into_the_abi_phases() {
        assert_eq!(phase(ST_PENDING), Phase::Pending);
        assert_eq!(phase(ST_TAKEN), Phase::Taken);
        assert_eq!(phase(ST_RUNNING + 3), Phase::Running(3));
        assert_eq!(phase(ST_RUNNING), Phase::Running(0));
        assert_eq!(phase(ST_DONE), Phase::Done);
        /* FAILED は `0x300 + (-rc)` — 符号を戻して元の rc にする。 */
        assert_eq!(phase(ST_FAILED + 13), Phase::Failed(-13));
        assert_eq!(phase(ST_FAILED + 9), Phase::Failed(-9));
        /* 知らない値と負の status は Unknown (固まらせない)。 */
        assert_eq!(phase(0x002), Phase::Unknown);
        assert_eq!(phase(0x400), Phase::Unknown);
        assert_eq!(phase(-11), Phase::Unknown);
        /* DONE に下位が乗って来ても DONE に倒す。 */
        assert_eq!(phase(ST_DONE + 1), Phase::Done);
    }

    #[test]
    fn a_short_lived_child_is_done_on_the_first_poll() {
        /* 票 §5 blocker 1: `rc == 0` の子は RUNNING を通らず DONE になる。 */
        let mut a = Attach::new(7);
        assert_eq!(a.poll(0, ST_DONE), Step::Finish(Outcome::Done));
        assert_eq!(a.child, 0);
    }

    #[test]
    fn running_then_done_walks_the_child_id_into_the_status_row() {
        let mut a = Attach::new(11);
        assert!(!a.cancelling());
        /* 取られるまでは何も起きない。 */
        assert_eq!(a.poll(0, ST_PENDING), Step::Idle);
        assert_eq!(a.poll(0, ST_TAKEN), Step::Idle);
        /* 子 ID が分かった周だけ描き直す。 */
        assert_eq!(a.poll(0, ST_RUNNING + 3), Step::Redraw);
        assert_eq!(a.child, 3);
        assert_eq!(a.poll(0, ST_RUNNING + 3), Step::Idle);
        assert_eq!(a.poll(0, ST_DONE), Step::Finish(Outcome::Done));
    }

    #[test]
    fn a_failed_launch_carries_the_negative_rc_back() {
        let mut a = Attach::new(2);
        assert_eq!(a.poll(0, ST_TAKEN), Step::Idle);
        assert_eq!(a.poll(0, ST_FAILED + 12), Step::Finish(Outcome::Failed(-12)));
    }

    #[test]
    fn a_negative_poll_returns_to_the_prompt_instead_of_hanging() {
        /* 既に IDLE (STALE) でも、知らない status でも接続モードに居座らない。 */
        let mut a = Attach::new(5);
        assert_eq!(a.poll(ERR_STALE, 0), Step::Finish(Outcome::Lost(ERR_STALE)));
        let mut a = Attach::new(5);
        assert_eq!(a.poll(0, 0x777), Step::Finish(Outcome::Lost(0x777)));
    }

    #[test]
    fn escape_cancels_once_and_waits_for_done() {
        /* D9: ESC → cancel 0 → 接続モードのまま DONE を待つ。 */
        let mut a = Attach::new(21);
        assert_eq!(a.poll(0, ST_RUNNING + 4), Step::Redraw);
        assert!(a.escape());
        assert!(a.cancelling());
        assert_eq!(a.cancelled(0), Step::Redraw);
        assert_eq!(a.cancel, Cancel::Armed);
        /* 2 度目の ESC は `launch_cancel` を出さない (1 回にまとめる)。 */
        assert!(!a.escape());
        /* 取消を積んだ後の poll は再試行を出さない。 */
        assert_eq!(a.poll(0, ST_RUNNING + 4), Step::Idle);
        assert_eq!(a.poll(0, ST_PENDING), Step::Idle);
        assert_eq!(a.poll(0, ST_DONE), Step::Finish(Outcome::Done));
    }

    #[test]
    fn again_retries_the_cancel_on_the_next_timer() {
        /* D9: PENDING / TAKEN への取消は AGAIN。次のタイマで自動再試行する。 */
        let mut a = Attach::new(33);
        assert!(a.escape());
        assert_eq!(a.cancelled(ERR_AGAIN), Step::Redraw);
        assert_eq!(a.cancel, Cancel::Retry);
        /* 取られていない間は poll のたびに再試行を促す。 */
        assert_eq!(a.poll(0, ST_PENDING), Step::Retry);
        assert_eq!(a.cancelled(ERR_AGAIN), Step::Redraw);
        assert_eq!(a.poll(0, ST_TAKEN), Step::Retry);
        /* RUNNING になれば通る。子 ID も同じ周で控える。 */
        assert_eq!(a.poll(0, ST_RUNNING + 6), Step::Retry);
        assert_eq!(a.child, 6);
        assert_eq!(a.cancelled(0), Step::Redraw);
        assert_eq!(a.poll(0, ST_RUNNING + 6), Step::Idle);
        assert_eq!(a.poll(0, ST_DONE), Step::Finish(Outcome::Done));
    }

    #[test]
    fn stale_cancel_keeps_the_token_and_waits_for_the_poll() {
        /* D9 + 実装レビュー往復 1/3 の blocker: `STALE` = 「もう完了している」
         * であって「表が空いた」ではない。token を捨てず、取消待ちのまま
         * 次の poll で `DONE` を消費する。 */
        let mut a = Attach::new(44);
        assert!(a.escape());
        assert_eq!(a.cancelled(ERR_STALE), Step::Redraw);
        assert_eq!(a.cancel, Cancel::Armed);
        assert_eq!(a.token, 44, "token は捨てない");
        assert_eq!(a.poll(0, ST_DONE), Step::Finish(Outcome::Done));
        /* 不一致の token なら poll が負を返すので、そこで戻る。 */
        let mut a = Attach::new(45);
        assert!(a.escape());
        assert_eq!(a.cancelled(ERR_STALE), Step::Redraw);
        assert_eq!(a.poll(ERR_STALE, 0), Step::Finish(Outcome::Lost(ERR_STALE)));
    }

    /* ============================================================ */
    /*  カーネルの要求表の写し (include/launch.h / §1a の契約)       */
    /*                                                              */
    /*  Codex の反例「ESC の STALE で表が残り、以後の launch_req が  */
    /*  FULL に固着する」を端末側だけでは再現できない (表の寿命が    */
    /*  見えない) ので、契約のうち **完了は launch_poll が消費して   */
    /*  初めて IDLE に戻る** ところを写した最小の模型を置く。        */
    /* ============================================================ */

    #[derive(Clone, Copy, PartialEq, Debug)]
    enum Row {
        Idle,
        Pending,
        Taken,
        Running(i32),
        /* 取消 (KILL) が積まれた。child は保持される。 */
        Killing(i32),
        Done,
        Failed(i32),
    }

    struct Table {
        row: Row,
        token: i32,
        next: i32,
    }

    impl Table {
        fn new() -> Self {
            Self {
                row: Row::Idle,
                token: 0,
                next: 100,
            }
        }
        /// `launch_req`: 自分の表が IDLE でなければ `OS32_ERR_FULL`。
        fn req(&mut self) -> i32 {
            if self.row != Row::Idle {
                return ERR_FULL;
            }
            self.row = Row::Pending;
            self.token = self.next;
            self.next += 1;
            self.token
        }
        /// WM の `launch_take` + `launch_report(rc > 0)`。
        fn start(&mut self, child: i32) {
            assert_eq!(self.row, Row::Pending);
            self.row = Row::Taken;
            self.row = Row::Running(child);
        }
        /// WM の `launch_report(rc < 0)`: 起動できなかった。
        fn fail(&mut self, rc: i32) {
            assert_eq!(self.row, Row::Pending);
            self.row = Row::Failed(rc);
        }
        /// 子の回収通知 (`launch_owner_exit`): `DONE` になるが **IDLE ではない**。
        fn child_exit(&mut self) {
            self.row = match self.row {
                Row::Running(_) | Row::Killing(_) => Row::Done,
                other => other,
            };
        }
        /// `launch_poll`: 完了を 1 度だけ渡し、渡した時点で IDLE に戻す。
        fn poll(&mut self, token: i32) -> (i32, i32) {
            if token != self.token || self.row == Row::Idle {
                return (ERR_STALE, 0);
            }
            match self.row {
                Row::Pending => (0, ST_PENDING),
                Row::Taken => (0, ST_TAKEN),
                Row::Running(c) | Row::Killing(c) => (0, ST_RUNNING + c),
                Row::Done => {
                    self.row = Row::Idle;
                    (0, ST_DONE)
                }
                Row::Failed(rc) => {
                    self.row = Row::Idle;
                    (0, ST_FAILED + (-rc))
                }
                Row::Idle => (ERR_STALE, 0),
            }
        }
        /// `launch_cancel`: `RUNNING` だけが通る。完了済みは `STALE`。
        fn cancel(&mut self, token: i32) -> i32 {
            if token != self.token {
                return ERR_STALE;
            }
            match self.row {
                Row::Pending | Row::Taken => ERR_AGAIN,
                Row::Running(c) => {
                    self.row = Row::Killing(c);
                    0
                }
                _ => ERR_STALE,
            }
        }
    }

    /// タイマ 1 周ぶん (`guest.rs::poll_launch` と同じ順序)。
    fn tick(table: &mut Table, attach: &mut Attach) -> Step {
        let (rc, status) = table.poll(attach.token);
        let step = attach.poll(rc, status);
        if step == Step::Retry {
            let rc = table.cancel(attach.token);
            return attach.cancelled(rc);
        }
        step
    }

    #[test]
    fn escape_after_the_child_already_exited_does_not_wedge_the_table() {
        /* 反例そのもの: 子が終わった直後 (poll より前) の ESC。 */
        let mut t = Table::new();
        let token = t.req();
        assert!(token > 0);
        t.start(3);
        let mut a = Attach::new(token);
        assert_eq!(tick(&mut t, &mut a), Step::Redraw);
        assert_eq!(a.child, 3);
        /* 子が終了。まだ誰も poll していないので表は DONE のまま。 */
        t.child_exit();
        /* ここで ESC。`launch_cancel` は STALE を返す。 */
        assert!(a.escape());
        assert_eq!(a.cancelled(t.cancel(a.token)), Step::Redraw);
        /* この時点で表はまだ空いていない — token を捨てていたら誰も消費せず、
         * 次の `launch_req` は永久に FULL (Codex の blocker)。 */
        assert_eq!(t.req(), ERR_FULL, "完了は poll が消費して初めて空く");
        /* 次のタイマで DONE を消費してプロンプトへ。 */
        assert_eq!(tick(&mut t, &mut a), Step::Finish(Outcome::Done));
        /* 表が空いたので次の起動が通る。 */
        let next = t.req();
        assert!(next > 0, "ESC のあとも起動できる (FULL に固着しない)");
        assert_ne!(next, token);
    }

    #[test]
    fn a_failed_launch_also_frees_the_table_only_through_the_poll() {
        /* `FAILED` も完了なので、消費するまで表は空かない (D4 / §1a)。 */
        let mut t = Table::new();
        let token = t.req();
        let mut a = Attach::new(token);
        t.fail(-2);
        assert_eq!(t.req(), ERR_FULL);
        assert_eq!(tick(&mut t, &mut a), Step::Finish(Outcome::Failed(-2)));
        assert!(t.req() > 0, "FAILED を消費した後は起動できる");
    }

    #[test]
    fn escape_while_pending_retries_until_the_kill_is_queued() {
        /* AGAIN → 再試行 → RUNNING で通る → 回収通知 → DONE を消費。 */
        let mut t = Table::new();
        let token = t.req();
        let mut a = Attach::new(token);
        assert!(a.escape());
        assert_eq!(a.cancelled(t.cancel(token)), Step::Redraw);
        assert_eq!(a.cancel, Cancel::Retry);
        /* PENDING のあいだは毎周やり直す (表はまだ取られていない)。 */
        assert_eq!(tick(&mut t, &mut a), Step::Redraw);
        assert_eq!(a.cancel, Cancel::Retry);
        t.start(4);
        /* RUNNING になった周で取消が積まれる。 */
        assert_eq!(tick(&mut t, &mut a), Step::Redraw);
        assert_eq!(a.cancel, Cancel::Armed);
        assert_eq!(a.child, 4);
        /* WM が畳むまでは RUNNING のまま (何も起きない)。 */
        assert_eq!(tick(&mut t, &mut a), Step::Idle);
        t.child_exit();
        assert_eq!(tick(&mut t, &mut a), Step::Finish(Outcome::Done));
        assert!(t.req() > 0);
    }
}
