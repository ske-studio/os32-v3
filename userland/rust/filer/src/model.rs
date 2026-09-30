//! model.rs — filer の非表示側 (パス規則 / VFS / ツリー / コピー) — 票 C5 §2・§5・§6。
//!
//! 大きい固定長バッファは**すべてここの `static`** に置く。`sys_ls` の
//! コールバックは C ABI の生関数なので閉包を渡せず、収集先はグローバルに
//! 取るしかない (gshell の `startmenu.rs` と同じ作法)。`static mut` の参照は
//! 2024 版で禁止されるので `UnsafeCell` + アクセサ 1 本に閉じる。
//!
//! # 鉄則 (票 C5)
//! - パスは**絶対パスだけ**。最大 255B。超えるものは [`ERR_PATH_LONG`] で拒否し、
//!   黙って切り詰めない (§2)。
//! - `sys_ls` のコールバックの中では **FS を触らない** (CLAUDE.md の
//!   `ext2_g_aux` 注意)。名前を自前のバッファへ写すだけ。
//! - コピーは 4096B バッファで、1 周につき最大 4 チャンク (16KB)。short write を
//!   進め、失敗時は両 fd を閉じ、自分が作った出力は消す (§6)。

use core::cell::UnsafeCell;

/* ================================================================ */
/*  上限                                                             */
/* ================================================================ */

/// パスバッファ (末尾 NUL を置くので有効長 + 1)。
pub const PATH_CAP: usize = 256;
/// 有効なパスの最大バイト数 (契約 S4 / 票 C5 §2)。
pub const PATH_MAX: usize = 255;
/// 右ペインの 1 エントリ名に許すバイト数。
pub const NAME_CAP: usize = 128;
/// 右ペインに並べる最大件数。
pub const MAX_ENTRIES: usize = 96;
/// 左ツリーのノード数上限。
pub const MAX_TREE: usize = 64;
/// ツリーノード名のバイト数上限。
pub const TREE_NAME_CAP: usize = 48;
/// コピーの 1 チャンク (§6)。
pub const COPY_CHUNK: usize = 4096;
/// 1 イベントループ周あたりのチャンク数 (= 16KB)。
pub const COPY_CHUNKS_PER_TICK: usize = 4;

/* ================================================================ */
/*  エラー番号                                                       */
/* ================================================================ */

/* VFS 側 (os32_kapi_shared.h OS32_ERR_*)。握り潰さずそのまま出す (§9)。 */
pub const OS32_ERR_NOTFOUND: i32 = -2;
pub const OS32_ERR_EXIST: i32 = -5;
pub const OS32_ERR_ISDIR: i32 = -8;

/// filer 内で作る番号 (VFS の -1..-13 と重ならない範囲)。
pub const ERR_PATH_LONG: i32 = -100;
pub const ERR_BAD_NAME: i32 = -101;
pub const ERR_NOT_ABS: i32 = -102;
pub const ERR_TOO_MANY: i32 = -103;
/// コピー元とコピー先が同じ実体を指している。
/// 先に弾かないと、`CopyJob::start` の `O_TRUNC` が**読む前に元を空にする**
/// (2026-09-10 のレビュー指摘 P1)。判定は st_dev / st_ino で行う。
pub const ERR_SAME_FILE: i32 = -104;

/// エラー番号 → 短い名前 (MessageBox に数値と一緒に出す、§9)。
pub fn err_name(code: i32) -> &'static [u8] {
    match code {
        0 => b"ok",
        -1 => b"I/O error",
        OS32_ERR_NOTFOUND => b"not found",
        -3 => b"no mount",
        -4 => b"no space",
        OS32_ERR_EXIST => b"already exists",
        -6 => b"not a directory",
        -7 => b"directory not empty",
        OS32_ERR_ISDIR => b"is a directory",
        -9 => b"invalid argument",
        -10 => b"not supported",
        -11 => b"stale",
        -12 => b"version",
        -13 => b"full",
        ERR_PATH_LONG => b"path too long (max 255B)",
        ERR_BAD_NAME => b"bad name",
        ERR_NOT_ABS => b"not an absolute path",
        ERR_TOO_MANY => b"too many entries",
        ERR_SAME_FILE => b"same file",
        _ => b"error",
    }
}

/* sys_open の mode (os32_kapi_shared.h KAPI_O_*)。数値を散らさない。 */
pub const O_RDONLY: i32 = 0x00;
pub const O_WRONLY: i32 = 0x01;
pub const O_CREAT: i32 = 0x0100;
pub const O_TRUNC: i32 = 0x0200;

/* ファイル種別 (OS32_FILE_TYPE_*)。 */
const FILE_TYPE_DIR: u8 = 2;
/* st_mode のディレクトリ判定 (OS_S_IFMT / OS_S_IFDIR)。 */
const S_IFMT: u16 = 0xF000;
const S_IFDIR: u16 = 0x4000;

/* ================================================================ */
/*  KAPI の生の型 (os32_kapi_shared.h と同一レイアウト)               */
/* ================================================================ */

#[repr(C)]
struct DirEntryExt {
    name: [u8; 256],
    size: u32,
    ftype: u8,
}

/// `sys_stat` が書く `OS32_Stat` (32B)。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Stat {
    pub st_dev: u32,
    pub st_ino: u32,
    pub st_mode: u16,
    pub st_nlink: u16,
    pub st_uid: u16,
    pub st_gid: u16,
    pub st_size: u32,
    pub st_atime: u32,
    pub st_mtime: u32,
    pub st_ctime: u32,
}

impl Stat {
    pub const ZERO: Stat = Stat {
        st_dev: 0,
        st_ino: 0,
        st_mode: 0,
        st_nlink: 0,
        st_uid: 0,
        st_gid: 0,
        st_size: 0,
        st_atime: 0,
        st_mtime: 0,
        st_ctime: 0,
    };

    #[inline]
    pub fn is_dir(&self) -> bool {
        (self.st_mode & S_IFMT) == S_IFDIR
    }
}

/* ================================================================ */
/*  データ                                                           */
/* ================================================================ */

/// 右ペインの 1 行。
#[derive(Clone, Copy)]
pub struct Entry {
    pub name: [u8; NAME_CAP],
    pub nlen: u8,
    pub size: u32,
    pub is_dir: bool,
}

impl Entry {
    pub const EMPTY: Entry = Entry { name: [0; NAME_CAP], nlen: 0, size: 0, is_dir: false };

    #[inline]
    pub fn name_slice(&self) -> &[u8] {
        &self.name[..self.nlen as usize]
    }
}

/// 左ツリーの 1 行。パスは祖先を辿って組み直す ([`tree_path`])。
#[derive(Clone, Copy)]
pub struct TreeNode {
    pub name: [u8; TREE_NAME_CAP],
    pub nlen: u8,
    pub depth: u8,
    pub expanded: bool,
}

impl TreeNode {
    pub const EMPTY: TreeNode =
        TreeNode { name: [0; TREE_NAME_CAP], nlen: 0, depth: 0, expanded: false };

    #[inline]
    pub fn name_slice(&self) -> &[u8] {
        &self.name[..self.nlen as usize]
    }
}

/// `sys_ls` の収集先の切り替え。
const LS_ENTRIES: u8 = 0;
const LS_DIRS: u8 = 1;

pub struct FsState {
    /// 現在地 (絶対パス、NUL 終端)。
    pub cwd: [u8; PATH_CAP],
    pub cwd_len: usize,

    /* --- 右ペイン --- */
    pub entries: [Entry; MAX_ENTRIES],
    /// 表示順 (ディレクトリ先 → ファイル、それぞれ名前順)。`entries` の添字。
    pub order: [u8; MAX_ENTRIES],
    pub nent: usize,
    /// `MAX_ENTRIES` を超えて捨てた件があるか。
    pub ent_overflow: bool,

    /* --- 左ツリー --- */
    pub tree: [TreeNode; MAX_TREE],
    pub ntree: usize,
    pub tree_overflow: bool,

    /* --- 一時領域 --- */
    /// 展開したい枝の子ディレクトリ名 (挿入前の受け皿)。
    tmp: [[u8; TREE_NAME_CAP]; MAX_TREE],
    tmp_len: [u8; MAX_TREE],
    tmp_n: usize,
    ls_mode: u8,

    /// コピーの読み書きバッファ (§6)。
    pub copy_buf: [u8; COPY_CHUNK],
}

impl FsState {
    const NEW: FsState = FsState {
        cwd: [0; PATH_CAP],
        cwd_len: 0,
        entries: [Entry::EMPTY; MAX_ENTRIES],
        order: [0; MAX_ENTRIES],
        nent: 0,
        ent_overflow: false,
        tree: [TreeNode::EMPTY; MAX_TREE],
        ntree: 0,
        tree_overflow: false,
        tmp: [[0; TREE_NAME_CAP]; MAX_TREE],
        tmp_len: [0; MAX_TREE],
        tmp_n: 0,
        ls_mode: LS_ENTRIES,
        copy_buf: [0; COPY_CHUNK],
    };

    #[inline]
    pub fn cwd_slice(&self) -> &[u8] {
        &self.cwd[..self.cwd_len]
    }

    /// 表示行 `row` に対応する [`Entry`]。
    #[inline]
    pub fn row_entry(&self, row: usize) -> Option<&Entry> {
        if row >= self.nent {
            return None;
        }
        Some(&self.entries[self.order[row] as usize])
    }
}

struct Holder(UnsafeCell<FsState>);
unsafe impl Sync for Holder {}
static STATE: Holder = Holder(UnsafeCell::new(FsState::NEW));

/// 唯一の可変状態 (シングルスレッド、割り込みからは触らない)。
#[inline]
#[allow(clippy::mut_from_ref)]
pub fn fs() -> &'static mut FsState {
    unsafe { &mut *STATE.0.get() }
}

/* ================================================================ */
/*  パス規則 (§2)                                                    */
/* ================================================================ */

/// NUL 手前までの長さ。
pub fn cstr_len(s: &[u8]) -> usize {
    let mut n = 0;
    while n < s.len() && s[n] != 0 {
        n += 1;
    }
    n
}

/// 絶対パスとして妥当か (`/` 始まり、1〜255B)。
pub fn check_abs(path: &[u8]) -> Result<usize, i32> {
    let n = cstr_len(path);
    if n == 0 || path[0] != b'/' {
        return Err(ERR_NOT_ABS);
    }
    if n > PATH_MAX {
        return Err(ERR_PATH_LONG);
    }
    Ok(n)
}

/// VFS と同じ規則で絶対パスを正規化する (`fs/vfs.c` の `vfs_resolve_path`)。
///
/// 重複 `/` を畳み、`.` を捨て、`..` で 1 段戻る (根で `..` は no-op)。
/// 深さ上限も VFS に合わせる (`VFS_MAX_PATH_DEPTH` = 32)。超える分は VFS 側でも
/// 落ちるので、ここでも同じく落として同じ結果に寄せる。
///
/// **なぜ要るか**: filer の `check_abs` は先頭 `/` と長さしか見ず正規化しない。
/// 一方 VFS は正規化する。生パスのまま比べると `/host/a.txt` と `/host/./a.txt`
/// が別物に見えるのに、開く先は同じファイルになる。inode を返さない
/// ファイルシステム (`fs/hostdrvfs.c`) では inode 判定も効かないので、
/// ここを揃えないと同一ファイルへのコピーが素通りする
/// (2026-09-10 のレビュー指摘 P1/blocker)。
///
/// 戻り値は `out` に書いた長さ。入力が絶対パスでなければ `None`。
pub fn normalize_abs(path: &[u8], out: &mut [u8; PATH_CAP]) -> Option<usize> {
    let n = cstr_len(path);
    if n == 0 || path[0] != b'/' {
        return None;
    }
    /* 各成分の [開始, 終了) を覚える。VFS と同じ 32 段まで。 */
    let mut starts = [0usize; 32];
    let mut ends = [0usize; 32];
    let mut np = 0usize;
    let mut i = 0usize;
    while i < n {
        if path[i] == b'/' {
            i += 1;
            continue;
        }
        let s = i;
        while i < n && path[i] != b'/' {
            i += 1;
        }
        let len = i - s;
        if len == 1 && path[s] == b'.' {
            /* nop */
        } else if len == 2 && path[s] == b'.' && path[s + 1] == b'.' {
            if np > 0 {
                np -= 1;
            }
        } else if np < 32 {
            starts[np] = s;
            ends[np] = i;
            np += 1;
        }
    }
    let mut o = 0usize;
    out[o] = b'/';
    o += 1;
    let mut k = 0usize;
    while k < np {
        if k > 0 {
            if o >= PATH_CAP - 1 {
                return None;
            }
            out[o] = b'/';
            o += 1;
        }
        let mut j = starts[k];
        while j < ends[k] {
            if o >= PATH_CAP - 1 {
                return None;
            }
            out[o] = path[j];
            o += 1;
            j += 1;
        }
        k += 1;
    }
    out[o] = 0;
    Some(o)
}

/// basename に使えるか (空でなく `/` を含まない、`.` `..` でない)。
pub fn check_name(name: &[u8]) -> Result<usize, i32> {
    let n = cstr_len(name);
    if n == 0 || n >= NAME_CAP {
        return Err(ERR_BAD_NAME);
    }
    let mut i = 0;
    while i < n {
        if name[i] == b'/' {
            return Err(ERR_BAD_NAME);
        }
        i += 1;
    }
    if (n == 1 && name[0] == b'.') || (n == 2 && name[0] == b'.' && name[1] == b'.') {
        return Err(ERR_BAD_NAME);
    }
    Ok(n)
}

/// `dir` + `/` + `name` を `out` へ組む (NUL 終端)。255B を超えたら拒否 (§2)。
pub fn path_join(dir: &[u8], name: &[u8], out: &mut [u8; PATH_CAP]) -> Result<usize, i32> {
    let dn = cstr_len(dir);
    let nn = cstr_len(name);
    /* ルート直下は区切りを重ねない。 */
    let sep = if dn == 1 && dir[0] == b'/' { 0 } else { 1 };
    let total = dn + sep + nn;
    if total > PATH_MAX {
        return Err(ERR_PATH_LONG);
    }
    let mut i = 0;
    while i < dn {
        out[i] = dir[i];
        i += 1;
    }
    if sep == 1 {
        out[i] = b'/';
        i += 1;
    }
    let mut k = 0;
    while k < nn {
        out[i] = name[k];
        i += 1;
        k += 1;
    }
    out[i] = 0;
    Ok(total)
}

/// 親ディレクトリ (`/` より上へは行かない、§2)。`out` は NUL 終端。
pub fn path_parent(path: &[u8], out: &mut [u8; PATH_CAP]) -> usize {
    let n = cstr_len(path);
    let mut cut = n;
    while cut > 1 && path[cut - 1] != b'/' {
        cut -= 1;
    }
    /* `cut` は末尾の `/` の次。ルート以外では `/` 自身を落とす。 */
    if cut > 1 {
        cut -= 1;
    }
    if cut == 0 {
        cut = 1;
    }
    let mut i = 0;
    while i < cut {
        out[i] = path[i];
        i += 1;
    }
    out[i] = 0;
    cut
}

/// `src` を NUL 終端つきで `dst` へ写す (最大 255B)。返り値: 長さ。
pub fn copy_path(dst: &mut [u8; PATH_CAP], src: &[u8]) -> usize {
    let mut n = cstr_len(src);
    if n > PATH_MAX {
        n = PATH_MAX;
    }
    dst[..n].copy_from_slice(&src[..n]);
    dst[n] = 0;
    n
}

/// `path` の basename の開始位置。
pub fn basename_off(path: &[u8]) -> usize {
    let n = cstr_len(path);
    let mut i = n;
    while i > 0 && path[i - 1] != b'/' {
        i -= 1;
    }
    i
}

/// `name` が `.bin` で終わるか (起動できる候補、§4)。
pub fn is_bin(name: &[u8]) -> bool {
    let n = cstr_len(name);
    n > 4
        && name[n - 4] == b'.'
        && name[n - 3] == b'b'
        && name[n - 2] == b'i'
        && name[n - 1] == b'n'
}

/* ================================================================ */
/*  VFS の薄い包み                                                   */
/* ================================================================ */

/// `path` (NUL 終端) を stat する。0 で成功。
pub fn stat(path: &[u8], out: &mut Stat) -> i32 {
    unsafe {
        (os32api::api().sys_stat)(path.as_ptr(), out as *mut Stat as *mut u8)
    }
}

pub fn mkdir(path: &[u8]) -> i32 {
    unsafe { (os32api::api().sys_mkdir)(path.as_ptr()) }
}

pub fn unlink(path: &[u8]) -> i32 {
    unsafe { (os32api::api().sys_unlink)(path.as_ptr()) }
}

pub fn rmdir(path: &[u8]) -> i32 {
    unsafe { (os32api::api().sys_rmdir)(path.as_ptr()) }
}

pub fn rename(old: &[u8], new: &[u8]) -> i32 {
    unsafe { (os32api::api().sys_rename)(old.as_ptr(), new.as_ptr()) }
}

pub fn open(path: &[u8], mode: i32) -> i32 {
    unsafe { (os32api::api().sys_open)(path.as_ptr(), mode) }
}

pub fn close(fd: i32) {
    if fd >= 0 {
        unsafe { (os32api::api().sys_close)(fd) }
    }
}

fn read(fd: i32, buf: *mut u8, size: u32) -> i32 {
    unsafe { (os32api::api().sys_read)(fd, buf, size) }
}

fn write(fd: i32, buf: *const u8, size: u32) -> i32 {
    unsafe { (os32api::api().sys_write)(fd, buf, size) }
}

/* ---- sys_ls (コールバックは C ABI の生関数) ---- */

extern "C" fn ls_cb(entry: *const DirEntryExt, _ctx: *mut u8) {
    if entry.is_null() {
        return;
    }
    let e = unsafe { &*entry };
    let n = cstr_len(&e.name);
    /* `.` / `..` は自前で出す (§3 の `..` 行)。二重に並べない。 */
    if n == 0
        || (n == 1 && e.name[0] == b'.')
        || (n == 2 && e.name[0] == b'.' && e.name[1] == b'.')
    {
        return;
    }
    let is_dir = e.ftype == FILE_TYPE_DIR;
    let st = fs();
    if st.ls_mode == LS_DIRS {
        if !is_dir {
            return;
        }
        if st.tmp_n >= MAX_TREE || n >= TREE_NAME_CAP {
            st.tree_overflow = true;
            return;
        }
        let k = st.tmp_n;
        let mut i = 0;
        while i < n {
            st.tmp[k][i] = e.name[i];
            i += 1;
        }
        st.tmp_len[k] = n as u8;
        st.tmp_n += 1;
        return;
    }
    /* LS_ENTRIES */
    if st.nent >= MAX_ENTRIES || n >= NAME_CAP {
        st.ent_overflow = true;
        return;
    }
    let k = st.nent;
    st.entries[k] = Entry::EMPTY;
    let mut i = 0;
    while i < n {
        st.entries[k].name[i] = e.name[i];
        i += 1;
    }
    st.entries[k].nlen = n as u8;
    st.entries[k].size = e.size;
    st.entries[k].is_dir = is_dir;
    st.nent = k + 1;
}

fn ls(path: &[u8]) -> i32 {
    unsafe {
        (os32api::api().sys_ls)(
            path.as_ptr(),
            ls_cb as *const () as *mut u8,
            core::ptr::null_mut(),
        )
    }
}

/* ================================================================ */
/*  右ペインの読み直し (§3: パスが変わったときだけ)                   */
/* ================================================================ */

/// `fs().cwd` の中身を読み直して並べ替える。戻り値は `sys_ls` の結果。
pub fn reload_entries() -> i32 {
    let st = fs();
    st.nent = 0;
    st.ent_overflow = false;
    st.ls_mode = LS_ENTRIES;

    /* ルート以外は先頭に `..` を置く (§3)。並べ替えからは外す。 */
    let has_parent = st.cwd_len > 1;
    if has_parent {
        st.entries[0] = Entry::EMPTY;
        st.entries[0].name[0] = b'.';
        st.entries[0].name[1] = b'.';
        st.entries[0].nlen = 2;
        st.entries[0].is_dir = true;
        st.nent = 1;
    }

    let cwd_copy_len = st.cwd_len;
    let mut cwd = [0u8; PATH_CAP];
    cwd[..cwd_copy_len + 1].copy_from_slice(&st.cwd[..cwd_copy_len + 1]);
    let rc = ls(&cwd);

    let st = fs();
    let first = if has_parent { 1 } else { 0 };
    let n = st.nent;
    let mut i = 0;
    while i < n {
        st.order[i] = i as u8;
        i += 1;
    }
    sort_order(first, n);
    rc
}

/// `order[first..n]` を「ディレクトリ先 → 名前順」に挿入ソートする (§1)。
fn sort_order(first: usize, n: usize) {
    let st = fs();
    let mut i = first + 1;
    while i < n {
        let key = st.order[i];
        let mut j = i;
        while j > first && entry_gt(st.order[j - 1], key) {
            st.order[j] = st.order[j - 1];
            j -= 1;
        }
        st.order[j] = key;
        i += 1;
    }
}

/// `a` が `b` より後ろに来るべきか。
fn entry_gt(a: u8, b: u8) -> bool {
    let st = fs();
    let ea = &st.entries[a as usize];
    let eb = &st.entries[b as usize];
    if ea.is_dir != eb.is_dir {
        /* ディレクトリが先。 */
        return !ea.is_dir;
    }
    name_gt(ea.name_slice(), eb.name_slice())
}

fn name_gt(a: &[u8], b: &[u8]) -> bool {
    let n = if a.len() < b.len() { a.len() } else { b.len() };
    let mut i = 0;
    while i < n {
        if a[i] != b[i] {
            return a[i] > b[i];
        }
        i += 1;
    }
    a.len() > b.len()
}

/* ================================================================ */
/*  左ツリー (§1: 必要な枝だけ lazy load)                             */
/* ================================================================ */

/// ルート `/` 1 本だけの状態に戻す。
pub fn tree_init() {
    let st = fs();
    st.ntree = 1;
    st.tree_overflow = false;
    st.tree[0] = TreeNode::EMPTY;
    st.tree[0].name[0] = b'/';
    st.tree[0].nlen = 1;
    st.tree[0].depth = 0;
    st.tree[0].expanded = false;
}

/// ノード `idx` の絶対パスを組む (祖先を遡る)。
pub fn tree_path(idx: usize, out: &mut [u8; PATH_CAP]) -> Result<usize, i32> {
    let st = fs();
    if idx >= st.ntree {
        return Err(ERR_NOT_ABS);
    }
    /* 祖先の添字を集める (深さは高々 MAX_TREE)。 */
    let mut chain = [0usize; MAX_TREE];
    let mut depth = st.tree[idx].depth;
    let mut n = 0usize;
    let mut i = idx;
    loop {
        chain[n] = i;
        n += 1;
        if depth == 0 {
            break;
        }
        /* 直前で depth-1 のノードが親。 */
        let want = depth - 1;
        let mut found = false;
        while i > 0 {
            i -= 1;
            if st.tree[i].depth == want {
                found = true;
                break;
            }
        }
        if !found {
            return Err(ERR_NOT_ABS);
        }
        depth = want;
    }
    /* chain は末端 → ルートの順。 */
    out[0] = b'/';
    let mut len = 1usize;
    let mut k = n - 1;
    while k > 0 {
        k -= 1;
        let node = &st.tree[chain[k]];
        let nl = node.nlen as usize;
        if len > 1 {
            if len + 1 > PATH_MAX {
                return Err(ERR_PATH_LONG);
            }
            out[len] = b'/';
            len += 1;
        }
        if len + nl > PATH_MAX {
            return Err(ERR_PATH_LONG);
        }
        let mut b = 0;
        while b < nl {
            out[len] = node.name[b];
            len += 1;
            b += 1;
        }
    }
    out[len] = 0;
    Ok(len)
}

/// `idx` の枝を畳む (子ノードを配列から抜く)。
pub fn tree_collapse(idx: usize) {
    let st = fs();
    if idx >= st.ntree {
        return;
    }
    let d = st.tree[idx].depth;
    let mut end = idx + 1;
    while end < st.ntree && st.tree[end].depth > d {
        end += 1;
    }
    let drop = end - (idx + 1);
    if drop > 0 {
        let mut i = idx + 1;
        while i + drop < st.ntree {
            st.tree[i] = st.tree[i + drop];
            i += 1;
        }
        st.ntree -= drop;
    }
    st.tree[idx].expanded = false;
}

/// `idx` の枝を開く (その 1 階層だけ `sys_ls`)。戻り値は `sys_ls` の結果。
pub fn tree_expand(idx: usize) -> i32 {
    let mut path = [0u8; PATH_CAP];
    if let Err(e) = tree_path(idx, &mut path) {
        return e;
    }

    {
        let st = fs();
        st.tmp_n = 0;
        st.ls_mode = LS_DIRS;
    }
    let rc = ls(&path);
    {
        let st = fs();
        st.ls_mode = LS_ENTRIES;
    }
    if rc < 0 {
        return rc;
    }

    sort_tmp();

    let st = fs();
    let d = st.tree[idx].depth;
    let add = st.tmp_n;
    if st.ntree + add > MAX_TREE {
        st.tree_overflow = true;
    }
    let room = MAX_TREE - st.ntree;
    let add = if add > room { room } else { add };
    if add > 0 {
        /* idx+1 以降を後ろへずらす。 */
        let mut i = st.ntree;
        while i > idx + 1 {
            i -= 1;
            st.tree[i + add] = st.tree[i];
        }
        let mut k = 0;
        while k < add {
            let slot = idx + 1 + k;
            st.tree[slot] = TreeNode::EMPTY;
            let nl = st.tmp_len[k] as usize;
            let mut b = 0;
            while b < nl {
                st.tree[slot].name[b] = st.tmp[k][b];
                b += 1;
            }
            st.tree[slot].nlen = nl as u8;
            st.tree[slot].depth = d + 1;
            k += 1;
        }
        st.ntree += add;
    }
    st.tree[idx].expanded = true;
    0
}

fn sort_tmp() {
    let st = fs();
    let n = st.tmp_n;
    let mut i = 1;
    while i < n {
        let mut j = i;
        while j > 0 {
            let la = st.tmp_len[j - 1] as usize;
            let lb = st.tmp_len[j] as usize;
            let gt = {
                let a = &st.tmp[j - 1][..la];
                let b = &st.tmp[j][..lb];
                name_gt(a, b)
            };
            if !gt {
                break;
            }
            st.tmp.swap(j - 1, j);
            st.tmp_len.swap(j - 1, j);
            j -= 1;
        }
        i += 1;
    }
}

/* ================================================================ */
/*  コピー (§6) — 1 周 16KB まで。fd は必ず閉じる                     */
/* ================================================================ */

/// 進行中のコピー 1 本。
pub struct CopyJob {
    pub active: bool,
    pub src_fd: i32,
    pub dst_fd: i32,
    /// 出力を filer が新規作成したか (途中失敗で消す対象)。
    pub created: bool,
    pub copied: u32,
    pub dst: [u8; PATH_CAP],
    pub dst_len: usize,
}

impl CopyJob {
    pub const NEW: CopyJob = CopyJob {
        active: false,
        src_fd: -1,
        dst_fd: -1,
        created: false,
        copied: 0,
        dst: [0; PATH_CAP],
        dst_len: 0,
    };

    /// コピーを開始する。`created` は「出力が存在しなかった」= 失敗時に消してよい。
    pub fn start(&mut self, src: &[u8], dst: &[u8], created: bool) -> i32 {
        self.abort();
        let sfd = open(src, O_RDONLY);
        if sfd < 0 {
            return sfd;
        }
        let dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
        if dfd < 0 {
            close(sfd);
            return dfd;
        }
        let n = cstr_len(dst);
        self.dst[..n + 1].copy_from_slice(&dst[..n + 1]);
        self.dst_len = n;
        self.src_fd = sfd;
        self.dst_fd = dfd;
        self.created = created;
        self.copied = 0;
        self.active = true;
        0
    }

    /// 1 周分 (最大 16KB) 進める。
    ///
    /// 戻り値: `1` = 継続中、`0` = 完了、負値 = エラー番号。
    pub fn step(&mut self) -> i32 {
        if !self.active {
            return 0;
        }
        let st = fs();
        let mut chunk = 0;
        while chunk < COPY_CHUNKS_PER_TICK {
            let n = read(self.src_fd, st.copy_buf.as_mut_ptr(), COPY_CHUNK as u32);
            if n < 0 {
                self.fail();
                return n;
            }
            if n == 0 {
                self.finish();
                return 0;
            }
            /* short write を進める (§6)。 */
            let total = n as usize;
            let mut off = 0usize;
            while off < total {
                let w = write(
                    self.dst_fd,
                    unsafe { st.copy_buf.as_ptr().add(off) },
                    (total - off) as u32,
                );
                if w <= 0 {
                    let e = if w < 0 { w } else { -1 };
                    self.fail();
                    return e;
                }
                off += w as usize;
            }
            self.copied = self.copied.wrapping_add(total as u32);
            chunk += 1;
        }
        1
    }

    /// 正常終了。両 fd を閉じる。
    fn finish(&mut self) {
        close(self.src_fd);
        close(self.dst_fd);
        self.src_fd = -1;
        self.dst_fd = -1;
        self.active = false;
    }

    /// 失敗。両 fd を閉じ、自分が作った出力は消す (§6 の partial cleanup)。
    fn fail(&mut self) {
        close(self.src_fd);
        close(self.dst_fd);
        self.src_fd = -1;
        self.dst_fd = -1;
        self.active = false;
        if self.created && self.dst_len > 0 {
            let mut p = [0u8; PATH_CAP];
            p[..self.dst_len + 1].copy_from_slice(&self.dst[..self.dst_len + 1]);
            let _ = unlink(&p);
        }
    }

    /// 中断 (ESC / 閉じる / Session Quit / タイマが張れない)。fd を残さず、
    /// **自分が作った出力も消す** (§6 の partial cleanup)。読み書きエラーの
    /// [`fail`] だけが消していると、中断では途中まで書いたファイルが正常な
    /// ファイルとして残る。
    ///
    /// 消すのは**進行中だった場合だけ**。`finish` / `fail` の後や 2 回目の
    /// `abort` では `active` が false なので、成功したコピーは消さない。
    pub fn abort(&mut self) {
        let was_active = self.active;
        if self.src_fd >= 0 || self.dst_fd >= 0 {
            close(self.src_fd);
            close(self.dst_fd);
            self.src_fd = -1;
            self.dst_fd = -1;
        }
        self.active = false;
        if was_active && self.created && self.dst_len > 0 {
            let mut p = [0u8; PATH_CAP];
            p[..self.dst_len + 1].copy_from_slice(&self.dst[..self.dst_len + 1]);
            let _ = unlink(&p);
        }
    }
}
