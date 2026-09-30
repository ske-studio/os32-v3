//! doc.rs — エディタの**本文**。GUI にも KAPI の描画にも依存しない。
//!
//! 票 TASK_EDIT_GUI §2 の切り分けの片側:
//!
//! - **本文はここが持つ**。`WK_TEXTAREA` は持たない。
//! - 折り返しも縦位置もここが決める。部品へ渡すのは
//!   「いま見えている行」だけ ([`Doc::fill_view`])。
//!   だから 1 画面に入らないファイルでも部品の側は一定で破綻しない。
//!
//! 受入 E8: この段は**ゲスト抜きで**確かめる (`tools/tests/test_edit_doc.py`)。
//! 桁の数え方と折り返しの位置は `libos32gui` の `textcore` を通す — 写しを
//! 作ると「3 バイトで 2 桁」がずれ、日本語で必ず露見する (§4-27)。
#![allow(dead_code)]

use libos32gui::textcore;

/* ================================================================ */
/*  上限 ([C4]: 数値をコードへ散らさない)                             */
/* ================================================================ */

/// 持てる論理行の本数。
pub const MAX_LINES: usize = 160;
/// 論理行 1 本のバイト数 (`GUI_TEXTAREA_ROW_CAP` と同じにしてある —
/// 折り返した 1 本が部品の 1 行に必ず収まるように)。
pub const LINE_CAP: usize = 192;
/// `fill_view` が返せる見える行の上限 (部品のプールと同じ)。
pub const MAX_VIEW_ROWS: usize = 32;

/* 読み込みの打ち切り理由 ([V4]: 黙って切らない)。 */
/// 行数が `MAX_LINES` を超えた。
pub const TRUNC_LINES: u8 = 0x01;
/// 1 行が `LINE_CAP` を超えた。
pub const TRUNC_COLS: u8 = 0x02;

/* ================================================================ */
/*  見える 1 行 (論理行のどこからどこまでか)                          */
/* ================================================================ */

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct VRow {
    /// 論理行の番号。
    pub line: usize,
    /// その論理行の中の [start, end) バイト。
    pub start: usize,
    pub end: usize,
}

impl VRow {
    pub const EMPTY: VRow = VRow { line: 0, start: 0, end: 0 };
}

/* ================================================================ */
/*  本文                                                            */
/* ================================================================ */

pub struct Doc {
    lines: [[u8; LINE_CAP]; MAX_LINES],
    lens: [u16; MAX_LINES],
    /// 使っている論理行の本数 (空の本文でも 1)。
    pub nlines: usize,
    /// キャレットの論理行。
    pub cy: usize,
    /// キャレットの位置 (その行の先頭からのバイト数。必ず UTF-8 境界)。
    pub cx: usize,
    /// 最後に保存してから変わったか。
    pub dirty: bool,
    /// 読み込みで落とした分 (`TRUNC_*` のビット和)。0 でなければ**言う**。
    pub truncated: u8,
}

impl Doc {
    /// **全ビット 0**。1 か所でも非 0 にすると 30KB の表がまるごと `.data` へ
    /// 移り、アプリの .bin がその分太る (`nlines: 1` にしていて実測 +31KB)。
    /// 使う前に [`Doc::reset`] を呼ぶこと。
    pub const NEW: Doc = Doc {
        lines: [[0; LINE_CAP]; MAX_LINES],
        lens: [0; MAX_LINES],
        nlines: 0,
        cy: 0,
        cx: 0,
        dirty: false,
        truncated: 0,
    };

    /* ---- 参照 ---- */

    #[inline]
    pub fn line(&self, i: usize) -> &[u8] {
        if i >= self.nlines {
            return &[];
        }
        &self.lines[i][..self.lens[i] as usize]
    }

    #[inline]
    pub fn line_len(&self, i: usize) -> usize {
        if i >= self.nlines {
            0
        } else {
            self.lens[i] as usize
        }
    }

    /// 本文全体のバイト数 (行の区切り `\n` を含む。最終行の後ろにも 1 つ)。
    pub fn byte_len(&self) -> usize {
        let mut n = 0usize;
        let mut i = 0usize;
        while i < self.nlines {
            n += self.lens[i] as usize + 1;
            i += 1;
        }
        n
    }

    pub fn reset(&mut self) {
        self.nlines = 1;
        self.lens[0] = 0;
        self.cy = 0;
        self.cx = 0;
        self.dirty = false;
        self.truncated = 0;
    }

    /* ---- 読み込み ---- */

    /// バイト列を行に割る。`\r\n` と `\n` の両方を区切りとして扱う。
    ///
    /// 入り切らなかったものは [`Doc::truncated`] に立てる。**捨てたことを
    /// 黙らない** ([V4]) — 呼び手はこれを見て利用者に言う。
    pub fn load(&mut self, bytes: &[u8]) {
        self.reset();
        let mut i = 0usize;
        let mut li = 0usize;
        let mut len = 0usize;
        while i < bytes.len() {
            let b = bytes[i];
            if b == b'\n' {
                self.lens[li] = len as u16;
                len = 0;
                li += 1;
                if li >= MAX_LINES {
                    self.truncated |= TRUNC_LINES;
                    self.nlines = MAX_LINES;
                    return;
                }
                self.lens[li] = 0;
                i += 1;
                continue;
            }
            if b == b'\r' {
                i += 1;
                continue;
            }
            /* 1 文字ずつ入れる — 途中で切れたら UTF-8 が割れる。 */
            let step = textcore::seq_len(b);
            let step = if i + step > bytes.len() { 1 } else { step };
            if len + step > LINE_CAP {
                self.truncated |= TRUNC_COLS;
                /* この論理行の残りは捨てる (次の改行まで読み飛ばす)。 */
                while i < bytes.len() && bytes[i] != b'\n' {
                    i += 1;
                }
                continue;
            }
            let mut k = 0usize;
            while k < step {
                self.lines[li][len + k] = bytes[i + k];
                k += 1;
            }
            len += step;
            i += step;
        }
        self.lens[li] = len as u16;
        /* **末尾の改行は最後の行の終端**であって、空行を 1 本作るものではない。
         * 数えてしまうと `save_file` がその空行にも改行を書き、**開いて保存する
         * たびにファイルが 1 バイト伸びる** (穴 H16、2026-09-18 に実測: 270 → 271)。
         * 空行が本当に末尾にある `"a\n\n"` は 2 行のまま残る (`len == 0` でも
         * その行の終端の改行は 1 つ前で数え終えている)。 */
        self.nlines = if len == 0 && li > 0 { li } else { li + 1 };
        self.dirty = false;
    }

    /* ---- 編集 ---- */

    /// キャレットへ 1 文字以上の UTF-8 を入れる。入らなければ `false`。
    pub fn insert(&mut self, seq: &[u8]) -> bool {
        if seq.is_empty() {
            return false;
        }
        let mut any = false;
        let mut i = 0usize;
        while i < seq.len() {
            let n = textcore::seq_len(seq[i]);
            if i + n > seq.len() {
                break;
            }
            if seq[i] == b'\n' {
                if !self.newline() {
                    return any;
                }
                any = true;
                i += 1;
                continue;
            }
            let len = self.lens[self.cy] as usize;
            let cx = self.cx;
            match textcore::insert(&mut self.lines[self.cy], len, cx, &seq[i..i + n]) {
                Some(newlen) => {
                    self.lens[self.cy] = newlen as u16;
                    self.cx = cx + n;
                    any = true;
                }
                None => break, /* この行がいっぱい */
            }
            i += n;
        }
        if any {
            self.dirty = true;
        }
        any
    }

    /// キャレットで行を割る。
    pub fn newline(&mut self) -> bool {
        if self.nlines >= MAX_LINES {
            return false;
        }
        let y = self.cy;
        let len = self.lens[y] as usize;
        let cx = if self.cx > len { len } else { self.cx };
        /* 後ろの行を 1 本ずらす。 */
        let mut i = self.nlines;
        while i > y + 1 {
            self.lines[i] = self.lines[i - 1];
            self.lens[i] = self.lens[i - 1];
            i -= 1;
        }
        self.nlines += 1;
        /* 割った後ろを新しい行へ。 */
        let tail = len - cx;
        let mut k = 0usize;
        while k < tail {
            self.lines[y + 1][k] = self.lines[y][cx + k];
            k += 1;
        }
        self.lens[y + 1] = tail as u16;
        self.lens[y] = cx as u16;
        self.cy = y + 1;
        self.cx = 0;
        self.dirty = true;
        true
    }

    /// キャレットの手前の 1 文字を消す (行頭なら前の行と繋ぐ)。
    pub fn backspace(&mut self) -> bool {
        if self.cx > 0 {
            let len = self.lens[self.cy] as usize;
            let p = textcore::prev_boundary(&self.lines[self.cy][..len], self.cx);
            let n = self.cx - p;
            let newlen = textcore::remove(&mut self.lines[self.cy], len, p, n);
            self.lens[self.cy] = newlen as u16;
            self.cx = p;
            self.dirty = true;
            return true;
        }
        if self.cy == 0 {
            return false;
        }
        self.join_with_next(self.cy - 1)
    }

    /// キャレットの 1 文字を消す (行末なら次の行と繋ぐ)。
    pub fn delete(&mut self) -> bool {
        let len = self.lens[self.cy] as usize;
        if self.cx < len {
            let q = textcore::next_boundary(&self.lines[self.cy][..len], self.cx);
            let newlen = textcore::remove(&mut self.lines[self.cy], len, self.cx, q - self.cx);
            self.lens[self.cy] = newlen as u16;
            self.dirty = true;
            return true;
        }
        if self.cy + 1 >= self.nlines {
            return false;
        }
        let y = self.cy;
        self.join_with_next(y)
    }

    /// `y` 行と `y+1` 行を繋ぐ。キャレットは継ぎ目へ。
    fn join_with_next(&mut self, y: usize) -> bool {
        if y + 1 >= self.nlines {
            return false;
        }
        let a = self.lens[y] as usize;
        let b = self.lens[y + 1] as usize;
        if a + b > LINE_CAP {
            return false; /* 繋ぐと入らない — **黙って切らない** */
        }
        let mut k = 0usize;
        while k < b {
            self.lines[y][a + k] = self.lines[y + 1][k];
            k += 1;
        }
        self.lens[y] = (a + b) as u16;
        let mut i = y + 1;
        while i + 1 < self.nlines {
            self.lines[i] = self.lines[i + 1];
            self.lens[i] = self.lens[i + 1];
            i += 1;
        }
        self.nlines -= 1;
        self.cy = y;
        self.cx = a;
        self.dirty = true;
        true
    }

    /* ---- キャレットの移動 (必ず UTF-8 境界に留まる) ---- */

    pub fn move_left(&mut self) {
        if self.cx > 0 {
            let len = self.lens[self.cy] as usize;
            self.cx = textcore::prev_boundary(&self.lines[self.cy][..len], self.cx);
        } else if self.cy > 0 {
            self.cy -= 1;
            self.cx = self.lens[self.cy] as usize;
        }
    }

    pub fn move_right(&mut self) {
        let len = self.lens[self.cy] as usize;
        if self.cx < len {
            self.cx = textcore::next_boundary(&self.lines[self.cy][..len], self.cx);
        } else if self.cy + 1 < self.nlines {
            self.cy += 1;
            self.cx = 0;
        }
    }

    pub fn move_up(&mut self) {
        if self.cy > 0 {
            self.cy -= 1;
            self.clamp_cx();
        }
    }

    pub fn move_down(&mut self) {
        if self.cy + 1 < self.nlines {
            self.cy += 1;
            self.clamp_cx();
        }
    }

    pub fn move_home(&mut self) {
        self.cx = 0;
    }

    pub fn move_end(&mut self) {
        self.cx = self.lens[self.cy] as usize;
    }

    /// 行を移ったあとのキャレットを、その行の**長さと UTF-8 境界**に収める。
    fn clamp_cx(&mut self) {
        let len = self.lens[self.cy] as usize;
        if self.cx > len {
            self.cx = len;
            return;
        }
        /* 継続バイトの上に乗ったら手前の境界へ戻す。 */
        if self.cx < len && (self.lines[self.cy][self.cx] & 0xC0) == 0x80 {
            self.cx = textcore::prev_boundary(&self.lines[self.cy][..len], self.cx);
        }
    }

    /* ================================================================ */
    /*  折り返しと「見える範囲」 (票 §2: 部品へ渡すのはここだけ)         */
    /* ================================================================ */

    /// 論理行 `i` が `cols` 桁で何本の見える行になるか (空行も 1 本)。
    pub fn wrap_count(&self, i: usize, cols: usize) -> usize {
        textcore::wrap_count(self.line(i), cols)
    }

    /// 本文全体の見える行の本数。
    pub fn vrow_total(&self, cols: usize) -> usize {
        let mut n = 0usize;
        let mut i = 0usize;
        while i < self.nlines {
            n += self.wrap_count(i, cols);
            i += 1;
        }
        n
    }

    /// 通し番号 `index` の見える行。範囲外は `None`。
    pub fn vrow_at(&self, cols: usize, index: usize) -> Option<VRow> {
        let mut seen = 0usize;
        let mut i = 0usize;
        while i < self.nlines {
            let k = self.wrap_count(i, cols);
            if index < seen + k {
                return Some(self.vrow_in_line(i, cols, index - seen));
            }
            seen += k;
            i += 1;
        }
        None
    }

    /// 論理行 `i` の中の `nth` 本目の見える行。
    fn vrow_in_line(&self, i: usize, cols: usize, nth: usize) -> VRow {
        let s = self.line(i);
        let cols = if cols == 0 { 1 } else { cols };
        let mut at = 0usize;
        let mut n = 0usize;
        loop {
            let e = at + textcore::wrap_end(&s[at..], cols);
            let e = if e == at && at < s.len() {
                textcore::next_boundary(s, at)
            } else {
                e
            };
            if n == nth {
                return VRow { line: i, start: at, end: e };
            }
            n += 1;
            at = e;
            if at >= s.len() {
                return VRow { line: i, start: s.len(), end: s.len() };
            }
        }
    }

    /// キャレットが乗っている見える行の通し番号。
    pub fn caret_vrow(&self, cols: usize) -> usize {
        let mut seen = 0usize;
        let mut i = 0usize;
        while i < self.cy && i < self.nlines {
            seen += self.wrap_count(i, cols);
            i += 1;
        }
        let s = self.line(self.cy);
        let cols = if cols == 0 { 1 } else { cols };
        let mut at = 0usize;
        let mut n = 0usize;
        loop {
            let e = at + textcore::wrap_end(&s[at..], cols);
            let e = if e == at && at < s.len() {
                textcore::next_boundary(s, at)
            } else {
                e
            };
            /* 行末のキャレットは**その行**に置く (次の行の頭ではない)。 */
            if self.cx < e || e >= s.len() {
                return seen + n;
            }
            n += 1;
            at = e;
            if at >= s.len() {
                return seen + n;
            }
        }
    }

    /// `top` から最大 `out.len()` 本の見える行を切り出す。戻りは本数。
    ///
    /// **これが部品へ渡すもの**。本文の大きさに関係なく `out.len()` 本で
    /// 止まるので、部品の側の使用量は一定 (票 §2)。
    pub fn fill_view(&self, cols: usize, top: usize, out: &mut [VRow]) -> usize {
        let cols = if cols == 0 { 1 } else { cols };
        /* `top` の論理行と行内位置を 1 回の走査で見つける。 */
        let mut seen = 0usize;
        let mut i = 0usize;
        let mut nth = 0usize;
        let mut found = false;
        while i < self.nlines {
            let k = self.wrap_count(i, cols);
            if top < seen + k {
                nth = top - seen;
                found = true;
                break;
            }
            seen += k;
            i += 1;
        }
        if !found {
            return 0;
        }
        let mut n = 0usize;
        while n < out.len() && i < self.nlines {
            let s = self.line(i);
            /* 論理行 i の nth 本目から順に取る。 */
            let mut at = 0usize;
            let mut cur = 0usize;
            loop {
                let e = at + textcore::wrap_end(&s[at..], cols);
                let e = if e == at && at < s.len() {
                    textcore::next_boundary(s, at)
                } else {
                    e
                };
                if cur >= nth {
                    if n >= out.len() {
                        return n;
                    }
                    out[n] = VRow { line: i, start: at, end: e };
                    n += 1;
                }
                cur += 1;
                at = e;
                if at >= s.len() {
                    break;
                }
            }
            nth = 0;
            i += 1;
        }
        n
    }

    /// キャレットが見える範囲に入るように `top` を寄せる。新しい `top`。
    pub fn scroll_to_caret(&self, cols: usize, height: usize, top: usize) -> usize {
        let h = if height == 0 { 1 } else { height };
        let c = self.caret_vrow(cols);
        if c < top {
            return c;
        }
        if c >= top + h {
            return c + 1 - h;
        }
        top
    }
}

/* ================================================================ */
/*  ファイル (KAPI。ホスト試験は sys_* を差し替えて踏む)              */
/* ================================================================ */

/* sys_open の mode (os32_kapi_shared.h KAPI_O_*)。数値を散らさない。 */
pub const O_RDONLY: i32 = 0x00;
pub const O_WRONLY: i32 = 0x01;
pub const O_CREAT: i32 = 0x0100;
pub const O_TRUNC: i32 = 0x0200;

/// 書けなかった理由を呼び手へそのまま返す (`OS32_ERR_*` / 負)。
pub const ERR_IO: i32 = -1;

/// `path` (NUL 終端) を読んで本文にする。戻りは読んだバイト数か負のエラー。
///
/// **読めなかったら負を返す。** 0 バイト読めた (= 空ファイル) と
/// 「開けなかった」を混ぜない。
pub fn load_file(doc: &mut Doc, path: &[u8], scratch: &mut [u8]) -> i32 {
    let fd = unsafe { (os32api::api().sys_open)(path.as_ptr(), O_RDONLY) };
    if fd < 0 {
        return fd;
    }
    let mut total = 0usize;
    loop {
        if total >= scratch.len() {
            break;
        }
        let n = unsafe {
            (os32api::api().sys_read)(
                fd,
                scratch[total..].as_mut_ptr(),
                (scratch.len() - total) as u32,
            )
        };
        if n < 0 {
            unsafe { (os32api::api().sys_close)(fd) };
            return n;
        }
        if n == 0 {
            break;
        }
        total += n as usize;
    }
    unsafe { (os32api::api().sys_close)(fd) };
    doc.load(&scratch[..total]);
    total as i32
}

/// 本文を `path` (NUL 終端) へ書く。戻りは書いたバイト数か負のエラー。
///
/// **書けなかったのに成功と答えない** ([V4] / 受入 E7)。open の失敗、
/// 短い write、write の負、どれでも負を返す。
pub fn save_file(doc: &Doc, path: &[u8]) -> i32 {
    let fd = unsafe { (os32api::api().sys_open)(path.as_ptr(), O_WRONLY | O_CREAT | O_TRUNC) };
    if fd < 0 {
        return fd;
    }
    let nl = [b'\n'];
    let mut total = 0usize;
    let mut i = 0usize;
    while i < doc.nlines {
        let line = doc.line(i);
        if !line.is_empty() {
            let n = unsafe { (os32api::api().sys_write)(fd, line.as_ptr(), line.len() as u32) };
            if n < 0 {
                unsafe { (os32api::api().sys_close)(fd) };
                return n;
            }
            if (n as usize) != line.len() {
                unsafe { (os32api::api().sys_close)(fd) };
                return ERR_IO; /* 短い write は失敗 */
            }
            total += n as usize;
        }
        let n = unsafe { (os32api::api().sys_write)(fd, nl.as_ptr(), 1) };
        if n < 0 {
            unsafe { (os32api::api().sys_close)(fd) };
            return n;
        }
        if n != 1 {
            unsafe { (os32api::api().sys_close)(fd) };
            return ERR_IO;
        }
        total += 1;
        i += 1;
    }
    unsafe { (os32api::api().sys_close)(fd) };
    total as i32
}
