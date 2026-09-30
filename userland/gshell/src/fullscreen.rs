//! fullscreen.rs — 全画面 GFX モード (票 T8 D4)。
//!
//! 画面の所有者はカーネルが 1 つ持つ (`gfx_screen_owner`、KAPI v48: 1 = シェル帯
//! = WM、2〜5 = アプリ)。所有者が WM でない間、gshell は**画面に 1 画素も出さない**:
//!
//! | | 全画面中の振る舞い |
//! |---|---|
//! | (a) 合成・present | しない (`wm::composite_*` / `queue_present` / `flush_present` が入口で戻る)。dirty は溜めてよい |
//! | (b) キー入力 | いつもどおりフォーカス窓へ配る (端末が `kbd_inject` で子へ渡す) |
//! | (c) マウス | タスクバー / Start / 窓を含めて無視 (位置と押下だけ同期する) |
//! | (d) CTRL+STOP | 宛先は**所有者** (`multiapp::abort_target`) |
//!
//! 入り方は 2 つあり、どちらも「所有者 ≠ 1」になるまでの隙間を塞ぐためにある:
//!
//! 1. **宣言** ([`arm`]): OS32X ヘッダに `FLAG_GFX` があれば `exec_start` の前に印を
//!    立てる。パレットの退避もこの時点で取る (プログラムが書き換えた後では遅い)。
//! 2. **問い合わせ** ([`observe`]): `exec_start` / `exec_resume` から戻った直後に
//!    `gfx_screen_owner()` を見る。宣言を忘れたプログラムはここで拾う (拾えるのは
//!    CUI 中だけ — GUI 中は宣言の無い `gfx_init` をカーネルが拒む、票 T8 D1a)。
//!
//! 抜けるのは所有者が 1 に戻ったときだけで、そこから先 (`gfx_init` → パレット →
//! リース → 全面再合成) は `crate::restore_screen`。

/// `gfx_screen_owner()` が返す「シェル帯 (WM)」。
pub const OWNER_WM: i32 = 1;

struct Fs {
    /// 全画面モード中 (= WM は描かない)。
    active: bool,
    /// 画面を持っている ID (0 = 宣言だけで所有者はまだ分からない)。
    owner: i32,
    /// 入る時点で控えた 16 色 (`wm::save_palette`)。復帰でそのまま戻す。
    palette: [u8; 48],
}

impl Fs {
    const NEW: Fs = Fs {
        active: false,
        owner: 0,
        palette: [0; 48],
    };
}

struct FsCell(core::cell::UnsafeCell<Fs>);
unsafe impl Sync for FsCell {}
static FS: FsCell = FsCell(core::cell::UnsafeCell::new(Fs::NEW));

#[inline]
fn f() -> &'static mut Fs {
    unsafe { &mut *FS.0.get() }
}

/// 全画面モード中か。**描画の門はすべてこれ 1 つを見る**。
#[inline]
pub fn active() -> bool {
    f().active
}

/// 画面を持っているアプリ ID (0 = WM、または宣言だけでまだ分からない)。
#[inline]
pub fn owner() -> i32 {
    let fs = f();
    if fs.active {
        fs.owner
    } else {
        0
    }
}

/// 入る時点で控えたパレット (復帰で戻す 16 色)。
#[inline]
pub fn palette() -> [u8; 48] {
    f().palette
}

/// **宣言**で先に全画面モードへ入る (`FLAG_GFX`、`exec_start` の前)。
///
/// `saved` は WM が持っている今のパレット。`exec_start` から戻った時点では
/// プログラムがもう書き換えているので、控えるのはここでなければならない。
pub fn arm(saved: &[u8; 48]) {
    let fs = f();
    if !fs.active {
        fs.active = true;
        fs.owner = 0;
        fs.palette = *saved;
    }
}

/// [`observe`] が見つけた変化。
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Change {
    /// 全画面ではない (ふだん)。
    None,
    /// 全画面中 (入った / 続いている)。WM は描かない。
    Fullscreen,
    /// 所有者が WM に戻った。呼ぶ側が復帰処理を行う。
    Restored,
}

/// `exec_start` / `exec_resume` から戻った直後の所有者の問い合わせ (票 T8 D3)。
///
/// KAPI は `gfx_screen_owner` の 1 本だけ — `exec_resume` のたびに通る点なので、
/// パレットの控え (16 回の `gfx_get_palette`) は**宣言が無いまま所有者を取られて
/// いた**ときにしか取らない (宣言つきは [`arm`] が入る前に控えてある)。
pub fn observe() -> Change {
    let now = unsafe { (os32api::api().gfx_screen_owner)() };
    let fs = f();
    if now != OWNER_WM {
        if !fs.active {
            fs.active = true;
            fs.palette = crate::wm::save_palette();
        }
        fs.owner = now;
        Change::Fullscreen
    } else if fs.active {
        /* 宣言だけ立てて `gfx_init` を呼ばずに終わったプログラムもここへ来る
         * (画面は誰も壊していないが、復帰処理は無害で 1 周期で終わる)。 */
        fs.active = false;
        fs.owner = 0;
        Change::Restored
    } else {
        Change::None
    }
}

/// 試験用 / GUI の作り直し用に印を落とす。
#[allow(dead_code)] /* 試験・診断用 (ゲストからは呼ばない) */
pub fn reset() {
    *f() = Fs::NEW;
}
