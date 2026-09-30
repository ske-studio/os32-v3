//! sink.rs — con_sink のワイヤ形式 (バイト列 → レコード列) の純パーサ。
//!
//! 票 K6C-A §2-3: ここは `no_std` の純関数だけで、KAPI も GUI も触らない。
//! ホスト試験は `host_tests/src/lib.rs` からこのファイルを直接取り込む。
//!
//! 正典は `sdk/include/os32/os32_kapi_shared.h` の `CON_SINK_*`:
//!
//! ```text
//!   PRINT  : [type=1][color u8][len u8][UTF-8 バイト len 個]   (len <= 200)
//!   CLEAR  : [type=2]
//!   CURSOR : [type=3][x u8][y u8]
//!   EXIT   : [type=4][id u8]
//! ```
//!
//! `EXIT` は票 T7 E1 の「子の終了通知」。値は `os32_kapi_shared.h` の
//! `CON_SINK_REC_EXIT` (4) / `CON_SINK_HDR_EXIT` (2) と同じで、
//! `CON_SINK_REC_MAX` は変わらない (PRINT が最長のまま)。
//!
//! `con_sink_read()` は**レコード境界でしか切らない**ので、途中で終わる
//! バイト列はカーネル側の異常を意味する。黙って詰めずに `Stop` で報せる。

/* --- レコード型 (CON_SINK_REC_*) --- */
pub const REC_PRINT: u8 = 1;
pub const REC_CLEAR: u8 = 2;
pub const REC_CURSOR: u8 = 3;
/// 票 T7 E1: 子の終了 (payload は回収された id 1 バイト)。`CON_SINK_REC_EXIT`。
pub const REC_EXIT: u8 = 4;

/* --- ヘッダ長 (CON_SINK_HDR_*) --- */
pub const HDR_PRINT: usize = 3;
pub const HDR_CLEAR: usize = 1;
pub const HDR_CURSOR: usize = 3;
/// 票 T7 E1 (`CON_SINK_HDR_EXIT`)。
pub const HDR_EXIT: usize = 2;

/// PRINT 1 本が運ぶ UTF-8 バイト数の上限 (`CON_SINK_PRINT_MAX`)。
pub const PRINT_MAX: usize = 200;
/// レコード 1 本の最大バイト数 (`CON_SINK_REC_MAX`)。`con_sink_read` の
/// `cap` はこれ以上でなければ `OS32_ERR_INVAL` で弾かれる。
pub const REC_MAX: usize = HDR_PRINT + PRINT_MAX;
const _: () = assert!(REC_MAX == 203);
const _: () = assert!(HDR_CLEAR == 1 && HDR_CURSOR == 3 && HDR_EXIT == 2);

/// `con_sink_read` が返しうる負の戻り値 (`os32_kapi_shared.h` が正典)。
/// `os32api` 側は GUI 用の一部しか公開していないので、ここで名前を付ける。
///
/// - `ERR_EXIST` は「読み手はすでに別のアプリ」。**直りうる** (先客が終われば
///   読めるようになる) ので、状態行に出したまま次の周も試す。
/// - それ以外は引数や版の誤りで直らない。以後は読まない。
pub const ERR_EXIST: i32 = -5;
pub const ERR_INVAL: i32 = -9;

/// 負の戻り値が「また試す価値があるか」。
pub fn retryable(rc: i32) -> bool {
    rc == ERR_EXIST
}

/// 1 本のレコード。`Print` の中身は入力バッファを借りるだけで写さない。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Record<'a> {
    Print {
        color: u8,
        bytes: &'a [u8],
    },
    Clear,
    Cursor {
        x: u8,
        y: u8,
    },
    /// 子が回収された (票 T7 E1)。端末はこれでプロンプトへ戻る。id は
    /// 「どれが終わったか」を出す手掛かりで、**自分の子かは判定しない**
    /// (v1.3 は端末 1 本 / 子 1 本、票 E4)。
    Exit(u8),
}

/// 走査が終わった理由。`Done` 以外は**残りを捨てた**ことを表す。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Stop {
    /// バッファをちょうど使い切った (空バッファも含む)。
    Done,
    /// 知らない型 (または形式違反の len)。以降の境界が導けないので捨てる。
    Unknown(u8),
    /// レコードの途中でバッファが尽きた (`con_sink_read` の契約違反)。
    Truncated,
}

/// バイト列を 1 本ずつ取り出す反復子。`Record` は入力を借りるので、
/// 反復子自身ではなくバッファの寿命 `'a` に縛られる。
pub struct Records<'a> {
    rest: &'a [u8],
    stop: Stop,
}

/// `con_sink_read` が書いたバイト列を走査する。
pub fn records(bytes: &[u8]) -> Records<'_> {
    Records {
        rest: bytes,
        stop: Stop::Done,
    }
}

impl<'a> Records<'a> {
    /// 走査を終えた理由。反復を尽くすまでは `Done` のまま。
    pub fn stop(&self) -> Stop {
        self.stop
    }

    /// 捨てた残りバイト数 (`stop != Done` のときだけ 0 でない)。
    /// 打ち切ったレコードの先頭バイトも含む — 進めていないため。
    pub fn discarded(&self) -> usize {
        match self.stop {
            Stop::Done => 0,
            _ => self.rest.len(),
        }
    }

    /// 打ち切って残りを捨てる。`rest` は進めないので `next()` は以後も `None`。
    fn halt(&mut self, stop: Stop) -> Option<Record<'a>> {
        self.stop = stop;
        None
    }
}

impl<'a> Iterator for Records<'a> {
    type Item = Record<'a>;

    fn next(&mut self) -> Option<Record<'a>> {
        let (&type_byte, tail) = self.rest.split_first()?;
        match type_byte {
            REC_PRINT => {
                /* ヘッダ 3 バイトと本体 len バイトが揃って 1 本。len == 0 も
                 * 正当なレコードで、空の Print を返して次へ進む (詰めない)。 */
                let Some((&color, tail)) = tail.split_first() else {
                    return self.halt(Stop::Truncated);
                };
                let Some((&len, tail)) = tail.split_first() else {
                    return self.halt(Stop::Truncated);
                };
                if len as usize > PRINT_MAX {
                    /* 上限を超える len は形式違反。長さが信用できない以上、
                     * 次の境界も導けないので残り全部を捨てる。 */
                    return self.halt(Stop::Unknown(type_byte));
                }
                let Some(bytes) = tail.get(..len as usize) else {
                    return self.halt(Stop::Truncated);
                };
                self.rest = &tail[len as usize..];
                Some(Record::Print { color, bytes })
            }
            REC_CLEAR => {
                self.rest = tail;
                Some(Record::Clear)
            }
            REC_CURSOR => {
                let Some((&x, tail)) = tail.split_first() else {
                    return self.halt(Stop::Truncated);
                };
                let Some((&y, tail)) = tail.split_first() else {
                    return self.halt(Stop::Truncated);
                };
                self.rest = tail;
                Some(Record::Cursor { x, y })
            }
            REC_EXIT => {
                let Some((&id, tail)) = tail.split_first() else {
                    return self.halt(Stop::Truncated);
                };
                self.rest = tail;
                Some(Record::Exit(id))
            }
            other => self.halt(Stop::Unknown(other)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn all(bytes: &[u8]) -> (Vec<Record<'_>>, Stop, usize) {
        let mut it = records(bytes);
        let out: Vec<_> = it.by_ref().collect();
        (out, it.stop(), it.discarded())
    }

    #[test]
    fn empty_buffer_yields_nothing_and_stops_done() {
        let (recs, stop, left) = all(&[]);
        assert!(recs.is_empty());
        assert_eq!(stop, Stop::Done);
        assert_eq!(left, 0);
    }

    #[test]
    fn print_len_zero_is_a_record_and_does_not_desync() {
        /* len 0 の PRINT を挟んでも、その次のレコードが読めること。 */
        let buf = [REC_PRINT, 0xE1, 0, REC_CLEAR, REC_CURSOR, 3, 4];
        let (recs, stop, left) = all(&buf);
        assert_eq!(
            recs,
            [
                Record::Print {
                    color: 0xE1,
                    bytes: &[]
                },
                Record::Clear,
                Record::Cursor { x: 3, y: 4 },
            ]
        );
        assert_eq!(stop, Stop::Done);
        assert_eq!(left, 0);
    }

    #[test]
    fn exit_record_carries_one_id_byte_and_keeps_the_boundary() {
        /* 票 T7 E1: [type=4][id u8]。前後のレコードとずれないこと。 */
        let buf = [
            REC_CLEAR, REC_EXIT, 7, REC_PRINT, 0xE1, 2, b'o', b'k', REC_EXIT, 0,
        ];
        let (recs, stop, left) = all(&buf);
        assert_eq!(
            recs,
            [
                Record::Clear,
                Record::Exit(7),
                Record::Print {
                    color: 0xE1,
                    bytes: b"ok"
                },
                Record::Exit(0),
            ]
        );
        assert_eq!(stop, Stop::Done);
        assert_eq!(left, 0);
        /* id が来る前に尽きたら詰めない (カーネル側の契約違反)。 */
        let (recs, stop, left) = all(&[REC_EXIT]);
        assert!(recs.is_empty());
        assert_eq!(stop, Stop::Truncated);
        assert_eq!(left, 1);
        /* 型 4 は既知になったが、5 以降は今も未知。 */
        assert_eq!(all(&[5]).1, Stop::Unknown(5));
        /* 最長レコードは PRINT のまま (CON_SINK_REC_MAX は不変)。 */
        assert!(HDR_EXIT < REC_MAX);
    }

    #[test]
    fn unknown_type_discards_the_remainder() {
        let buf = [REC_CLEAR, 0x77, REC_CLEAR, REC_CLEAR];
        let (recs, stop, left) = all(&buf);
        assert_eq!(recs, [Record::Clear]);
        assert_eq!(stop, Stop::Unknown(0x77));
        /* 0x77 とその後ろ 2 本、合わせて 3 バイトを捨てた。 */
        assert_eq!(left, 3);
    }

    #[test]
    fn oversized_print_length_is_rejected_as_unknown() {
        let mut buf = vec![REC_PRINT, 0xE1, (PRINT_MAX + 1) as u8];
        buf.extend(core::iter::repeat(b'x').take(PRINT_MAX + 1));
        let (recs, stop, left) = all(&buf);
        assert!(recs.is_empty());
        assert_eq!(stop, Stop::Unknown(REC_PRINT));
        assert_eq!(left, buf.len());
    }

    #[test]
    fn three_byte_utf8_survives_the_record_boundary() {
        /* 「日本語」= 9 バイト。本体はそのまま借りて渡すだけで、切らない。 */
        let text = "日本語".as_bytes();
        assert_eq!(text.len(), 9);
        let mut buf = vec![REC_PRINT, 0x81, text.len() as u8];
        buf.extend_from_slice(text);
        buf.push(REC_CLEAR);
        let (recs, stop, left) = all(&buf);
        assert_eq!(
            recs,
            [
                Record::Print {
                    color: 0x81,
                    bytes: text
                },
                Record::Clear,
            ]
        );
        assert_eq!(stop, Stop::Done);
        assert_eq!(left, 0);
    }

    #[test]
    fn record_ending_exactly_at_the_buffer_end_is_complete() {
        for tail in [
            vec![REC_CLEAR],
            vec![REC_CURSOR, 79, 24],
            vec![REC_PRINT, 0xE1, 1, b'Z'],
        ] {
            let mut buf = vec![REC_CURSOR, 0, 0];
            buf.extend_from_slice(&tail);
            let (recs, stop, left) = all(&buf);
            assert_eq!(recs.len(), 2, "{tail:?}");
            assert_eq!(stop, Stop::Done, "{tail:?}");
            assert_eq!(left, 0, "{tail:?}");
        }
        /* 最長レコードがバッファちょうどで終わる場合 (REC_MAX = 203)。 */
        let mut buf = vec![REC_PRINT, 0xE1, PRINT_MAX as u8];
        buf.extend(core::iter::repeat(b'a').take(PRINT_MAX));
        assert_eq!(buf.len(), REC_MAX);
        let (recs, stop, left) = all(&buf);
        assert_eq!(recs.len(), 1);
        assert_eq!(stop, Stop::Done);
        assert_eq!(left, 0);
    }

    #[test]
    fn truncated_records_stop_without_inventing_bytes() {
        for buf in [
            vec![REC_PRINT],
            vec![REC_PRINT, 0xE1],
            vec![REC_PRINT, 0xE1, 4, b'a', b'b'],
            vec![REC_CURSOR],
            vec![REC_CURSOR, 9],
        ] {
            let (recs, stop, left) = all(&buf);
            assert!(recs.is_empty(), "{buf:?}");
            assert_eq!(stop, Stop::Truncated, "{buf:?}");
            assert_eq!(left, buf.len(), "{buf:?}");
        }
        /* 途中で切れる前のレコードはちゃんと返る。 */
        let (recs, stop, _) = all(&[REC_CLEAR, REC_PRINT, 0xE1, 2, b'a']);
        assert_eq!(recs, [Record::Clear]);
        assert_eq!(stop, Stop::Truncated);
    }

    #[test]
    fn only_the_reader_conflict_is_worth_retrying() {
        assert!(retryable(ERR_EXIST));
        for rc in [ERR_INVAL, -1, -10, -13] {
            assert!(!retryable(rc));
        }
    }

    #[test]
    fn iteration_is_fused_after_a_stop() {
        let buf = [0x55, REC_CLEAR];
        let mut it = records(&buf);
        assert_eq!(it.next(), None);
        assert_eq!(it.stop(), Stop::Unknown(0x55));
        assert_eq!(it.next(), None);
        assert_eq!(it.stop(), Stop::Unknown(0x55));
    }
}
