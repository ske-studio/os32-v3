//! prompt.rs — 端末のプロンプト行と起動語の解釈 (票 T7 E2〜E5) の純粋部分。
//!
//! `sink.rs` / `inject.rs` と同じ流儀で、ここは `no_std` の純関数だけ。KAPI も
//! GUI も触らないので、ホスト試験が `host_tests/src/lib.rs` からそのまま
//! 取り込める。存在確認 (`sys_open`) と `launch_req` の呼び出しは
//! `guest.rs` の担当で、こちらは「どのパスを、どの順に試すか」までを決める。
//!
//! ## 役割の分け方 (票 §2)
//!
//! - E2 プロンプト行: [`Line`] がローカル編集 (印字可能 ASCII / `GUI_EV_TEXT`
//!   の UTF-8 / BS) を持つ。プロンプト表示中は 1 バイトも `kbd_inject` しない。
//! - E3 起動: [`decide`] が行を解釈し、[`candidates`] が探す順の絶対パスを作る。
//!   見つかったパスと残りの引数を [`command_line`] が `launch_req` の値へ
//!   (票 T9 D4 で行き先が `session_launch` から要求表に変わった。長さの上限は
//!   どちらも 255B なので `PATH_MAX` はそのまま)。
//! - E4 接続モード: [`step`] が `Mode` の遷移だけを決める (副作用は guest 側)。
//!   票 T9 D4 で「戻る合図」は con_sink の `EXIT` から要求表の `launch_poll` に
//!   移った ([`crate::launch`])。ここは結果 (`Done` / `Failed` / `Lost`) と
//!   ESC (`CancelRequested`) を受けるだけ。
//! - E5 空行 / `exit`: [`Decision::Empty`] / [`Decision::Exit`]。
//! - T8 D7 入口: [`classify`] が OS32X ヘッダの宣言ビットを読み、`--cpl0` の
//!   プログラム (v86 / VDM) は起動しない (`cui only: <名>`)。読み出しは `guest.rs`。

/// 編集中の行が持てるバイト数。`command_line` が 255B に収まるよう
/// `PATH_MAX` より小さく取る。
pub const LINE_MAX: usize = 160;
/// `launch_req` が受ける cmdline の最大 (NUL 込み 256B = `LAUNCH_CMDLINE_MAX`、
/// 中身は 1〜255B)。`session_launch` の契約 S4 と同じ長さ。
pub const PATH_MAX: usize = 255;
/// 最下行 / ローカル出力 1 本が持てるバイト数。
pub const MSG_MAX: usize = 128;

/// プロンプトの飾り。行頭の 2 桁を使う。
pub const PREFIX: &[u8] = b"> ";
/// 編集位置を示す 1 桁。
pub const CARET: &[u8] = b"_";
/// 接続モード中に最下行へ出す案内の頭 (票 T9 D9: ESC は取消)。
pub const RUNNING: &[u8] = b"[running";
/// 取消を頼んだ後の頭 (ESC はもう効かない — `DONE` を待っている)。
pub const CANCELLING: &[u8] = b"[cancelling";
/// `[running …` の締めと案内。
pub const RUNNING_HINT: &[u8] = b"] ESC=cancel";
/// `[cancelling …` の締め。
pub const CANCELLING_HINT: &[u8] = b"] wait";

/// 名前だけで打たれたときに探す場所 (票 E3 の順)。
pub const DIRS: [&[u8]; 2] = [b"/usr/bin/", b"/bin/"];
/// 名前に付ける拡張子。すでに付いていれば重ねない。
pub const SUFFIX: &[u8] = b".bin";

/* ================================================================ */
/*  UTF-8 の切り方                                                   */
/* ================================================================ */

fn is_continuation(b: u8) -> bool {
    b & 0xC0 == 0x80
}

/// 先頭から `max` バイト以内に収まるよう、**UTF-8 の境界で**切る
/// (CLAUDE.md §4-27: 途中で切ると□が出る)。
pub fn truncate_utf8(bytes: &[u8], max: usize) -> &[u8] {
    if bytes.len() <= max {
        return bytes;
    }
    let mut end = max;
    while end > 0 && is_continuation(bytes[end]) {
        end -= 1;
    }
    &bytes[..end]
}

/* ================================================================ */
/*  E2: 編集中の行                                                   */
/* ================================================================ */

/// プロンプトで編集中の 1 行。中身は常に妥当な UTF-8 (追加は文字単位)。
#[derive(Clone, Copy)]
pub struct Line {
    buf: [u8; LINE_MAX],
    len: usize,
}

impl Default for Line {
    fn default() -> Self {
        Self::new()
    }
}

impl Line {
    pub const fn new() -> Self {
        Self {
            buf: [0; LINE_MAX],
            len: 0,
        }
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }
    pub fn len(&self) -> usize {
        self.len
    }
    /// ホスト試験だけが使う (guest は `row()` 越しにしか見ない)。
    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn clear(&mut self) {
        self.len = 0;
    }

    /// 打鍵 (印字可能 ASCII / `GUI_EV_TEXT` の UTF-8) を足す。
    ///
    /// 制御文字は落とす (Enter / BS は呼ぶ側が先に捌く)。残りが足りなければ
    /// **1 バイトも入れない** — 多バイト文字を割って壊れた UTF-8 を作らない
    /// ため。戻り値は入らずに捨てたバイト数 (0 なら全部入った)。
    pub fn push(&mut self, bytes: &[u8]) -> usize {
        let mut want = 0;
        for &b in bytes {
            if b >= 0x20 && b != 0x7F {
                want += 1;
            }
        }
        if want == 0 {
            return 0;
        }
        if self.len + want > LINE_MAX {
            return want;
        }
        for &b in bytes {
            if b >= 0x20 && b != 0x7F {
                self.buf[self.len] = b;
                self.len += 1;
            }
        }
        0
    }

    /// 末尾の 1 **文字**を消す (多バイト文字はまとめて)。消せたら true。
    pub fn backspace(&mut self) -> bool {
        if self.len == 0 {
            return false;
        }
        self.len -= 1;
        while self.len > 0 && is_continuation(self.buf[self.len]) {
            self.len -= 1;
        }
        true
    }

    /// 表示に使う末尾 `max` バイト (UTF-8 境界で切る)。行が長いときは
    /// **末尾**を見せる — 打っている場所が見えないと編集できない。
    pub fn tail(&self, max: usize) -> &[u8] {
        let bytes = self.as_bytes();
        if bytes.len() <= max {
            return bytes;
        }
        let mut start = bytes.len() - max;
        while start < bytes.len() && is_continuation(bytes[start]) {
            start += 1;
        }
        &bytes[start..]
    }
}

/* ================================================================ */
/*  固定長の文字列 (ローカル出力と最下行)                            */
/* ================================================================ */

/// 端末が自分で出す 1 行 (`command not found: …` など)。入り切らない分は
/// UTF-8 境界で捨てる — 状態行と同じく、失敗しても panic させない。
#[derive(Clone, Copy)]
pub struct Msg {
    buf: [u8; MSG_MAX],
    len: usize,
}

impl Default for Msg {
    fn default() -> Self {
        Self::new()
    }
}

impl Msg {
    pub const fn new() -> Self {
        Self {
            buf: [0; MSG_MAX],
            len: 0,
        }
    }
    pub fn bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }
    pub fn len(&self) -> usize {
        self.len
    }
    /// ホスト試験だけが使う。
    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    /// 入るところまで足す (UTF-8 境界で切る)。
    pub fn push(&mut self, bytes: &[u8]) {
        let room = MSG_MAX - self.len;
        let take = truncate_utf8(bytes, room);
        self.buf[self.len..self.len + take.len()].copy_from_slice(take);
        self.len += take.len();
    }
}

/// 数を混ぜた行を組むため (`launch failed (-13)` / `[running id=3]`)。
/// 入り切らない分は [`Msg::push`] が黙って落とすので、書式の失敗では
/// panic しない (状態行と同じ扱い)。
impl core::fmt::Write for Msg {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        self.push(s.as_bytes());
        Ok(())
    }
}

/// ローカル出力 1 行 (`prefix` + `body` + 改行)。con_sink は通らない。
pub fn message(prefix: &[u8], body: &[u8]) -> Msg {
    let mut out = Msg::new();
    out.push(prefix);
    /* 改行の 1 バイトは必ず残す。 */
    out.push(truncate_utf8(body, MSG_MAX.saturating_sub(out.len() + 1)));
    out.push(b"\n");
    out
}

/// ローカル出力 1 行に負の戻り値を添える (`launch failed (-13)`、票 T9 D4)。
pub fn message_rc(prefix: &[u8], rc: i32) -> Msg {
    use core::fmt::Write;
    let mut out = Msg::new();
    /* 改行の 1 バイトと `(-2147483648)` の 13 桁は必ず残す。 */
    out.push(truncate_utf8(prefix, MSG_MAX.saturating_sub(14)));
    let _ = write!(out, "({})", rc);
    out.push(b"\n");
    out
}

/// 接続モードの最下行に出す中身 (票 T9 D4 / D9)。`child` は `launch_poll` が
/// `RUNNING` で教えた子 ID (0 = まだ分からない)。
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Running {
    pub child: i32,
    /// `launch_cancel` を出した後 (ESC はもう受けない)。
    pub cancelling: bool,
}

/// 最下行に描く 1 行 (票 E2 / E4、T9 D9)。`cols` は桁数の上限。
pub fn row(mode: Mode, line: &Line, cols: usize, run: &Running) -> Msg {
    use core::fmt::Write;
    let mut out = Msg::new();
    if mode == Mode::Attached {
        /* 一度組んでから桁で切る — 途中で切ると案内が消えるだけで壊れない。 */
        let mut all = Msg::new();
        all.push(if run.cancelling { CANCELLING } else { RUNNING });
        if run.child > 0 {
            let _ = write!(all, " id={}", run.child);
        }
        all.push(if run.cancelling {
            CANCELLING_HINT
        } else {
            RUNNING_HINT
        });
        out.push(truncate_utf8(all.bytes(), cols.min(MSG_MAX)));
        return out;
    }
    let cap = cols.min(MSG_MAX);
    out.push(truncate_utf8(PREFIX, cap));
    /* 全角は 1 文字 3 バイトで 2 桁なので、バイト数で抑えれば桁は溢れない。 */
    let room = cap.saturating_sub(PREFIX.len() + CARET.len());
    out.push(line.tail(room));
    if out.len() + CARET.len() <= cap {
        out.push(CARET);
    }
    out
}

/* ================================================================ */
/*  E3: 行の解釈と候補パス                                           */
/* ================================================================ */

/// 確定した行の意味。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Decision<'a> {
    /// 空行 (空白だけを含む)。プロンプトを出し直すだけ (票 E5)。
    Empty,
    /// `exit` — 端末自身の終了 (ESC と同じ、票 E5)。
    Exit,
    /// 先頭トークンが起動する名前、残りがそのまま渡す引数。
    Run { name: &'a [u8], args: &'a [u8] },
}

fn is_space(b: u8) -> bool {
    b == b' ' || b == b'\t'
}

/// 確定した行を解釈する。引数は**空白区切りのまま**渡す (票 E3)。
pub fn decide(line: &[u8]) -> Decision<'_> {
    let mut i = 0;
    while i < line.len() && is_space(line[i]) {
        i += 1;
    }
    let mut end = i;
    while end < line.len() && !is_space(line[end]) {
        end += 1;
    }
    if end == i {
        return Decision::Empty;
    }
    let name = &line[i..end];
    if name == b"exit" {
        return Decision::Exit;
    }
    let mut rest = end;
    while rest < line.len() && is_space(line[rest]) {
        rest += 1;
    }
    /* 末尾の空白は落とす (`launch_req` の値に無駄を積まない)。 */
    let mut tail = line.len();
    while tail > rest && is_space(line[tail - 1]) {
        tail -= 1;
    }
    Decision::Run {
        name,
        args: &line[rest..tail],
    }
}

/// NUL 終端の絶対パス。`sys_open` へそのまま渡せる。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Path {
    buf: [u8; PATH_MAX + 1],
    len: usize,
}

impl Default for Path {
    fn default() -> Self {
        Self::new()
    }
}

impl Path {
    pub const fn new() -> Self {
        Self {
            buf: [0; PATH_MAX + 1],
            len: 0,
        }
    }
    /// 入り切るときだけ足す。溢れたら false を返して**中身は変えない**。
    pub fn push(&mut self, bytes: &[u8]) -> bool {
        if self.len + bytes.len() > PATH_MAX {
            return false;
        }
        self.buf[self.len..self.len + bytes.len()].copy_from_slice(bytes);
        self.len += bytes.len();
        self.buf[self.len] = 0;
        true
    }
    /// NUL を含まないバイト列。`launch_req` は NUL 終端 (`as_ptr`) を取るので、
    /// guest では使わずホスト試験だけが読む。
    #[allow(dead_code)]
    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }
    /// NUL 終端の先頭 (`sys_open` 用)。
    pub fn as_ptr(&self) -> *const u8 {
        self.buf.as_ptr()
    }
    /// ホスト試験だけが使う (guest は `as_bytes` / `as_ptr` しか要らない)。
    #[allow(dead_code)]
    pub fn len(&self) -> usize {
        self.len
    }
    /// ホスト試験だけが使う。
    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
}

/// 探す順の絶対パス。`/` 始まりなら 1 本、名前なら `/usr/bin` → `/bin` の 2 本。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Candidates {
    items: [Path; DIRS.len()],
    len: usize,
}

impl Candidates {
    pub fn as_slice(&self) -> &[Path] {
        &self.items[..self.len]
    }
    /// ホスト試験だけが使う (guest は `as_slice` を回すだけ)。
    #[allow(dead_code)]
    pub fn len(&self) -> usize {
        self.len
    }
    /// ホスト試験だけが使う。
    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
}

fn ends_with(bytes: &[u8], suffix: &[u8]) -> bool {
    bytes.len() >= suffix.len() && &bytes[bytes.len() - suffix.len()..] == suffix
}

/// 先頭トークンから候補パスを作る (票 E3)。
///
/// - `/` 始まり: そのまま 1 本だけ。
/// - それ以外: `/usr/bin/<名>.bin` → `/bin/<名>.bin`。名前がすでに `.bin` で
///   終わっていれば重ねない (`kbd_echo.bin` と打っても同じ場所を探す)。
///
/// 255B に収まらない候補は黙って落とす (`launch_req` が受けないため)。
pub fn candidates(name: &[u8]) -> Candidates {
    let mut out = Candidates {
        items: [Path::new(); DIRS.len()],
        len: 0,
    };
    if name.is_empty() {
        return out;
    }
    if name[0] == b'/' {
        let mut p = Path::new();
        if p.push(name) {
            out.items[0] = p;
            out.len = 1;
        }
        return out;
    }
    let suffix: &[u8] = if ends_with(name, SUFFIX) { b"" } else { SUFFIX };
    for dir in DIRS {
        let mut p = Path::new();
        if p.push(dir) && p.push(name) && p.push(suffix) {
            out.items[out.len] = p;
            out.len += 1;
        }
    }
    out
}

/// 見つかった絶対パスと残りの引数を `launch_req` の値にする (票 E3 / T9 D4)。
/// 255B に収まらなければ `None` — 黙って切ると別のコマンドを起動しかねない。
pub fn command_line(path: &Path, args: &[u8]) -> Option<Path> {
    let mut out = *path;
    if args.is_empty() {
        return Some(out);
    }
    if out.push(b" ") && out.push(args) {
        Some(out)
    } else {
        None
    }
}

/* ================================================================ */
/*  E4: モード                                                       */
/* ================================================================ */

/// 端末の打鍵の行き先 (票 E4)。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Mode {
    /// プロンプト。打鍵はローカル編集、**注入しない**。
    Prompt,
    /// 接続モード。打鍵は `kbd_inject` で子へ。プロンプトは消える。
    Attached,
}

/// モードを動かしうる出来事。
///
/// 票 T9 D4 で `Exit` (con_sink の `EXIT` レコード) は**落とした** — `EXIT` は
/// 表示だけに使い、モードは要求表 (`launch_poll`) の答えで動かす。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Event {
    /// `launch_req` が token を返した。
    Launched,
    /// `launch_poll` が `DONE` を返した (取消で畳まれた場合も含む)。
    Done,
    /// `launch_poll` が `FAILED` / 負の戻り値だった。
    Failed,
    /// 接続モードで ESC を受けた = `launch_cancel` の要求 (票 D9)。
    CancelRequested,
    /// プロンプトで ESC を受けた (端末自身の終了)。
    Escape,
}

/// 遷移の答え。副作用 (行の消去・注入・終了・`launch_cancel`) は呼ぶ側の仕事。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Next {
    /// 何も変えない。
    Stay,
    Prompt,
    Attached,
    /// `launch_cancel(token)` を出す。**接続モードのまま**で、プロンプトへは
    /// `DONE` を見てから戻る (票 T9 D9)。
    Cancel,
    /// 端末自身の終了。
    Quit,
}

/// 票 E4 + T9 D4 / D9 の遷移表。
///
/// - プロンプトの ESC は**自分の終了** (従来どおり)。子が生きていても取消は
///   出さない — 要求者の退場はカーネルが孤児回収する (票 §10 non-blocker 1)。
/// - 接続モードの ESC は `launch_cancel` (T9 D9)。子には注入しない。出した後も
///   接続モードのままで、`DONE` を見てからプロンプトへ戻る。
/// - プロンプトで受けた完了 (`Done` / `Failed`) は捨てる (取りこぼし)。
pub fn step(mode: Mode, event: Event) -> Next {
    match (mode, event) {
        (Mode::Prompt, Event::Launched) => Next::Attached,
        (Mode::Prompt, Event::Escape) => Next::Quit,
        (Mode::Prompt, _) => Next::Stay,
        (Mode::Attached, Event::Escape) => Next::Cancel,
        (Mode::Attached, Event::CancelRequested) => Next::Stay,
        (Mode::Attached, Event::Done) => Next::Prompt,
        (Mode::Attached, Event::Failed) => Next::Prompt,
        (Mode::Attached, Event::Launched) => Next::Stay,
    }
}

/* ================================================================ */
/*  OS32X ヘッダの宣言ビット (票 T8 D7 の入口)                        */
/*                                                                  */
/*  VRAM を直接触る CPL=0 プログラム (v86 / VDM = `mkos32x --cpl0`)   */
/*  は GUI からは起動しない。カーネルも GUI 中は `OS32_ERR_INVAL` で   */
/*  拒むが、端末はその前に理由 (`cui only: <名>`) を出して接続モード    */
/*  にも入らない。                                                    */
/*                                                                  */
/*  同じ判定は gshell (`userland/gshell/src/os32x.rs`) にもある。      */
/*  共有ライブラリを 1 本増やすより、この 20 行の写しの方が安い。       */
/* ================================================================ */

/// `OS32X_MAGIC` (`sdk/include/os32/os32_kapi_shared.h`)。'OS32' の LE。
pub const OS32X_MAGIC: u32 = 0x4F53_3332;
/// v1 ヘッダのサイズ (`OS32X_HDR_V1_SIZE`)。読むのはこの 40B だけ。
pub const OS32X_HDR_SIZE: usize = 40;
/// `OS32X_FLAG_GFX` — 全画面 GFX を使う宣言 (端末は起動を止めない)。
pub const OS32X_FLAG_GFX: u32 = 0x0001;
/// `OS32X_FLAG_FORCE_CPL0` — CPL=0 強制。GUI からは起動しない。
pub const OS32X_FLAG_FORCE_CPL0: u32 = 0x0004;
/// `OS32X_FLAG_CUI_ONLY` — CUI 専用の宣言。GUI からは起動しない。
///
/// `v86.bin` は `--cpl0` ではなく **flags 0x0 の CPL=3 プログラム**で、V86 へは
/// KAPI 越しに入る (受入 F5 不合格の原因)。値はリテラル — 票 T8-2 の K/B が
/// `OS32X_FLAG_CUI_ONLY 0x0010` を `sdk/include/os32/os32_kapi_shared.h` へ
/// 足したら、そちらが正典になる。
pub const OS32X_FLAG_CUI_ONLY: u32 = 0x0010;

/// 起動してよいか (票 T8 D4 / D7)。
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Kind {
    /// ふつうの CPL=3 プログラム。
    Plain,
    /// 全画面 GFX の宣言つき。端末は起動する (画面は gshell が譲る)。
    FullScreen,
    /// CPL=0 強制。GUI からは起動しない。
    CuiOnly,
}

fn le32(b: &[u8], off: usize) -> u32 {
    (b[off] as u32)
        | ((b[off + 1] as u32) << 8)
        | ((b[off + 2] as u32) << 16)
        | ((b[off + 3] as u32) << 24)
}

/// **純関数**: OS32X ヘッダの先頭 40B → 起動の可否。
///
/// 読めなかった / OS32X でない / 短い ものは [`Kind::Plain`] に倒す —
/// 立てない側へ倒せば既存の起動経路は 1 つも変わらない (起動要求の
/// `FAILED` として従来どおり出る)。`FORCE_CPL0` / `CUI_ONLY` は `FLAG_GFX` より強い。
pub fn classify(hdr: &[u8]) -> Kind {
    if hdr.len() < OS32X_HDR_SIZE {
        return Kind::Plain;
    }
    if le32(hdr, 0) != OS32X_MAGIC || (le32(hdr, 4) as usize) < OS32X_HDR_SIZE {
        return Kind::Plain;
    }
    let flags = le32(hdr, 12);
    if flags & (OS32X_FLAG_FORCE_CPL0 | OS32X_FLAG_CUI_ONLY) != 0 {
        Kind::CuiOnly
    } else if flags & OS32X_FLAG_GFX != 0 {
        Kind::FullScreen
    } else {
        Kind::Plain
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn os32x_header(flags: u32) -> [u8; OS32X_HDR_SIZE] {
        let mut h = [0u8; OS32X_HDR_SIZE];
        h[0..4].copy_from_slice(&OS32X_MAGIC.to_le_bytes());
        h[4..8].copy_from_slice(&(OS32X_HDR_SIZE as u32).to_le_bytes());
        h[8..12].copy_from_slice(&2u32.to_le_bytes());
        h[12..16].copy_from_slice(&flags.to_le_bytes());
        h
    }

    /// 票 T8 D7: 起動前に読む OS32X ヘッダの判定 (cpl0 / gfx / どちらも無し)。
    #[test]
    fn os32x_flags_decide_whether_the_terminal_launches() {
        /* CPL=0 強制 (v86 / VDM) は起動しない。 */
        assert_eq!(
            classify(&os32x_header(OS32X_FLAG_FORCE_CPL0)),
            Kind::CuiOnly
        );
        /* 両方立っていても CPL=0 が勝つ。 */
        assert_eq!(
            classify(&os32x_header(OS32X_FLAG_GFX | OS32X_FLAG_FORCE_CPL0)),
            Kind::CuiOnly
        );
        /* 票 T8-2: v86.bin は CPL=3 (FORCE_CPL0 が無い) なので CUI_ONLY
         * 単独でも起動しない。 */
        assert_eq!(
            classify(&os32x_header(OS32X_FLAG_CUI_ONLY)),
            Kind::CuiOnly,
            "CUI_ONLY だけでも cui only"
        );
        assert_eq!(
            classify(&os32x_header(OS32X_FLAG_CUI_ONLY | 0x0002 /* RING3 */)),
            Kind::CuiOnly
        );
        /* CUI_ONLY は FLAG_GFX より強い。 */
        assert_eq!(
            classify(&os32x_header(OS32X_FLAG_GFX | OS32X_FLAG_CUI_ONLY)),
            Kind::CuiOnly,
            "CUI_ONLY が GFX に勝つ"
        );
        /* 全画面 GFX は起動する (画面は gshell が譲る)。 */
        assert_eq!(classify(&os32x_header(OS32X_FLAG_GFX)), Kind::FullScreen);
        /* 宣言の無い CUI プログラムは従来どおり。 */
        assert_eq!(classify(&os32x_header(0x0002 /* RING3 */)), Kind::Plain);
        /* 読めなかった / OS32X でない / 短い → 止めない側へ倒す。 */
        assert_eq!(classify(&[]), Kind::Plain);
        assert_eq!(
            classify(&os32x_header(OS32X_FLAG_FORCE_CPL0)[..39]),
            Kind::Plain
        );
        let mut bad = os32x_header(OS32X_FLAG_FORCE_CPL0);
        bad[0] ^= 0xFF;
        assert_eq!(classify(&bad), Kind::Plain, "magic 違いは触らない");
        let mut short_hdr = os32x_header(OS32X_FLAG_FORCE_CPL0);
        short_hdr[4] = 8;
        assert_eq!(classify(&short_hdr), Kind::Plain, "header_size が小さい");
    }

    fn line_of(s: &str) -> Line {
        let mut l = Line::new();
        assert_eq!(l.push(s.as_bytes()), 0);
        l
    }

    #[test]
    fn line_edits_by_character_and_drops_control_bytes() {
        let mut l = Line::new();
        assert!(l.is_empty());
        /* 印字可能 ASCII はそのまま。 */
        assert_eq!(l.push(b"ls"), 0);
        assert_eq!(l.push(b" -l"), 0);
        assert_eq!(l.as_bytes(), b"ls -l");
        /* 制御文字は落とす (Enter / BS / TAB は呼ぶ側が先に捌く)。 */
        assert_eq!(l.push(b"\r\n\x08\x09\x1b\x7f"), 0);
        assert_eq!(l.as_bytes(), b"ls -l");
        /* GUI_EV_TEXT の多バイトはそのまま入る。 */
        assert_eq!(l.push("あ".as_bytes()), 0);
        assert_eq!(l.as_bytes(), "ls -lあ".as_bytes());
        /* BS は 1 文字単位 — 全角を割らない。 */
        assert!(l.backspace());
        assert_eq!(l.as_bytes(), b"ls -l");
        for _ in 0..5 {
            assert!(l.backspace());
        }
        assert!(l.is_empty());
        /* 空行で BS を押しても何も起きない。 */
        assert!(!l.backspace());
        assert!(l.is_empty());
    }

    #[test]
    fn line_refuses_to_split_a_character_at_the_limit() {
        let mut l = Line::new();
        for _ in 0..LINE_MAX {
            assert_eq!(l.push(b"a"), 0);
        }
        assert_eq!(l.len(), LINE_MAX);
        /* 上限では捨てたバイト数を返し、行は伸びない。 */
        assert_eq!(l.push(b"b"), 1);
        assert_eq!(l.len(), LINE_MAX);
        /* 1 文字ぶんの隙間しかないところへ全角は入れない (割らない)。 */
        l.backspace();
        assert_eq!(l.push("あ".as_bytes()), 3);
        assert_eq!(l.len(), LINE_MAX - 1);
        assert!(core::str::from_utf8(l.as_bytes()).is_ok());
        /* 隙間が空けば入る。 */
        l.backspace();
        l.backspace();
        assert_eq!(l.push("あ".as_bytes()), 0);
        assert_eq!(l.len(), LINE_MAX);
        assert!(core::str::from_utf8(l.as_bytes()).is_ok());
    }

    #[test]
    fn tail_shows_the_end_on_a_character_boundary() {
        let l = line_of("あいう");
        assert_eq!(l.tail(9), "あいう".as_bytes());
        assert_eq!(l.tail(100), "あいう".as_bytes());
        /* 8 バイトでは「あ」が割れるので「いう」だけ。 */
        assert_eq!(l.tail(8), "いう".as_bytes());
        assert_eq!(l.tail(6), "いう".as_bytes());
        assert_eq!(l.tail(2), b"");
        let l = line_of("abcdef");
        assert_eq!(l.tail(3), b"def");
    }

    #[test]
    fn row_shows_the_prompt_or_the_running_marker() {
        let l = line_of("kbd_echo");
        let idle = Running::default();
        assert_eq!(row(Mode::Prompt, &l, 40, &idle).bytes(), b"> kbd_echo_");
        /* 空行でもプロンプトと編集位置は出る。 */
        assert_eq!(row(Mode::Prompt, &Line::new(), 40, &idle).bytes(), b"> _");
        /* 桁が足りなければ末尾を見せる (打っている場所が見えなくならない)。 */
        assert_eq!(row(Mode::Prompt, &l, 8, &idle).bytes(), b"> _echo_");
        assert_eq!(row(Mode::Prompt, &l, 3, &idle).bytes(), b"> _");
        /* 桁がプロンプトにも足りない極端な窓でも壊れない。 */
        assert_eq!(row(Mode::Prompt, &l, 1, &idle).bytes(), b">");
        assert_eq!(row(Mode::Prompt, &l, 0, &idle).bytes(), b"");
    }

    /// 票 T9 D4 / D9: 接続モードの最下行は ESC = 取消を案内し、`launch_poll` が
    /// 教えた子 ID を出す。
    fn attached_row(child: i32, cancelling: bool, cols: usize) -> Msg {
        row(
            Mode::Attached,
            &Line::new(),
            cols,
            &Running { child, cancelling },
        )
    }

    #[test]
    fn attached_row_names_the_child_and_offers_cancel() {
        /* 子 ID がまだ分からない (PENDING / TAKEN) 間は id を出さない。 */
        assert_eq!(attached_row(0, false, 40).bytes(), b"[running] ESC=cancel");
        assert_eq!(
            attached_row(3, false, 40).bytes(),
            b"[running id=3] ESC=cancel"
        );
        /* 取消を出した後は ESC の案内を下げる (もう受けない)。 */
        assert_eq!(attached_row(3, true, 40).bytes(), b"[cancelling id=3] wait");
        assert_eq!(attached_row(0, true, 40).bytes(), b"[cancelling] wait");
        /* 桁が足りなければ頭から入るだけ。壊れない。 */
        assert_eq!(attached_row(3, false, 8).bytes(), b"[running");
        assert_eq!(attached_row(3, false, 0).bytes(), b"");
    }

    #[test]
    fn message_rc_prints_the_negative_return_value() {
        assert_eq!(
            message_rc(b"launch failed ", -13).bytes(),
            b"launch failed (-13)\n"
        );
        assert_eq!(
            message_rc(b"launch_req failed ", -9).bytes(),
            b"launch_req failed (-9)\n"
        );
        /* 長すぎる前置きでも数と改行は残る。 */
        let long = vec![b'x'; MSG_MAX];
        let m = message_rc(&long, -2147483648);
        assert!(m.len() <= MSG_MAX);
        assert_eq!(&m.bytes()[m.len() - 14..], b"(-2147483648)\n");
    }

    #[test]
    fn message_keeps_the_newline_and_a_character_boundary() {
        assert_eq!(
            message(b"command not found: ", b"nosuch").bytes(),
            b"command not found: nosuch\n"
        );
        /* 長すぎる名前は UTF-8 境界で切り、改行は必ず残る。 */
        let long = "あ".repeat(MSG_MAX);
        let m = message(b"command not found: ", long.as_bytes());
        assert!(m.len() <= MSG_MAX);
        assert_eq!(m.bytes()[m.len() - 1], b'\n');
        assert!(core::str::from_utf8(&m.bytes()[..m.len() - 1]).is_ok());
    }

    #[test]
    fn decide_splits_the_first_token_from_the_arguments() {
        assert_eq!(decide(b""), Decision::Empty);
        assert_eq!(decide(b"   \t "), Decision::Empty);
        assert_eq!(decide(b"exit"), Decision::Exit);
        assert_eq!(decide(b"  exit  "), Decision::Exit);
        assert_eq!(
            decide(b"kbd_echo"),
            Decision::Run {
                name: b"kbd_echo",
                args: b""
            }
        );
        assert_eq!(
            decide(b"  cat /etc/system.cfg  "),
            Decision::Run {
                name: b"cat",
                args: b"/etc/system.cfg"
            }
        );
        /* 引数は空白区切りのまま渡す (再分割しない)。 */
        assert_eq!(
            decide(b"ls -l  /usr/bin"),
            Decision::Run {
                name: b"ls",
                args: b"-l  /usr/bin"
            }
        );
        assert_eq!(
            decide(b"/usr/bin/filer.bin x"),
            Decision::Run {
                name: b"/usr/bin/filer.bin",
                args: b"x"
            }
        );
        /* `exit` で始まるだけの名前は内蔵ではない。 */
        assert_eq!(
            decide(b"exitfoo"),
            Decision::Run {
                name: b"exitfoo",
                args: b""
            }
        );
    }

    fn paths(c: &Candidates) -> Vec<&[u8]> {
        c.as_slice().iter().map(|p| p.as_bytes()).collect()
    }

    #[test]
    fn candidates_follow_usr_bin_then_bin() {
        assert_eq!(
            paths(&candidates(b"kbd_echo")),
            [&b"/usr/bin/kbd_echo.bin"[..], &b"/bin/kbd_echo.bin"[..]]
        );
        /* すでに .bin なら重ねない。 */
        assert_eq!(
            paths(&candidates(b"kbd_echo.bin")),
            [&b"/usr/bin/kbd_echo.bin"[..], &b"/bin/kbd_echo.bin"[..]]
        );
        /* `/` 始まりはそのまま 1 本だけ (拡張子も足さない)。 */
        assert_eq!(
            paths(&candidates(b"/etc/system.cfg")),
            [&b"/etc/system.cfg"[..]]
        );
        assert_eq!(paths(&candidates(b"")), Vec::<&[u8]>::new());
        /* NUL 終端されている (sys_open へそのまま渡せる)。 */
        let c = candidates(b"ls");
        let p = c.as_slice()[0];
        // SAFETY: Path は常に len の次に 0 を置く。
        assert_eq!(unsafe { *p.as_ptr().add(p.len()) }, 0);
    }

    #[test]
    fn candidates_drop_what_launch_req_cannot_take() {
        let long = vec![b'a'; PATH_MAX];
        assert!(candidates(&long).is_empty());
        let mut absolute = vec![b'/'];
        absolute.extend(core::iter::repeat(b'a').take(PATH_MAX));
        assert!(candidates(&absolute).is_empty());
        /* ちょうど 255B は通る。 */
        let name = vec![b'a'; PATH_MAX - DIRS[0].len() - SUFFIX.len()];
        let c = candidates(&name);
        assert_eq!(c.len(), 2);
        assert_eq!(c.as_slice()[0].len(), PATH_MAX);
    }

    #[test]
    fn command_line_joins_the_path_and_the_arguments() {
        let c = candidates(b"cat");
        let p = c.as_slice()[0];
        assert_eq!(
            command_line(&p, b"").unwrap().as_bytes(),
            b"/usr/bin/cat.bin"
        );
        assert_eq!(
            command_line(&p, b"-n /etc/system.cfg").unwrap().as_bytes(),
            b"/usr/bin/cat.bin -n /etc/system.cfg"
        );
        /* 255B に収まらなければ黙って切らずに諦める。 */
        let args = vec![b'x'; PATH_MAX];
        assert_eq!(command_line(&p, &args), None);
    }

    #[test]
    fn mode_transitions_follow_the_ticket() {
        /* 票 T9 D4: prompt → launch_req → attached → DONE → prompt。 */
        assert_eq!(step(Mode::Prompt, Event::Launched), Next::Attached);
        assert_eq!(step(Mode::Attached, Event::Done), Next::Prompt);
        /* FAILED / poll の異常もプロンプトへ戻る (固まらない)。 */
        assert_eq!(step(Mode::Attached, Event::Failed), Next::Prompt);
        /* 票 T9 D9: 接続モードの ESC は取消。**まだ**プロンプトへは戻らない。 */
        assert_eq!(step(Mode::Attached, Event::Escape), Next::Cancel);
        assert_eq!(step(Mode::Attached, Event::CancelRequested), Next::Stay);
        /* プロンプトの ESC は従来どおり自分の終了。 */
        assert_eq!(step(Mode::Prompt, Event::Escape), Next::Quit);
        /* 迷子の完了と二重の Launched は何も変えない。 */
        assert_eq!(step(Mode::Prompt, Event::Done), Next::Stay);
        assert_eq!(step(Mode::Prompt, Event::Failed), Next::Stay);
        assert_eq!(step(Mode::Prompt, Event::CancelRequested), Next::Stay);
        assert_eq!(step(Mode::Attached, Event::Launched), Next::Stay);
    }
}
