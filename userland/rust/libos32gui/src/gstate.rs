//! G 描画レイヤの内部グローバル状態 (サーフェステーブル + クリップスタック)。
//!
//! 単一スレッド前提 (CPL=3 の 1 アプリ)。`clip` / `surface` / `draw` が共有する。
//! 公開 API はサブモジュールが `#[no_mangle]`/Rust 関数として出す。
use crate::ffi::{self, GfxSurface};
use core::cell::UnsafeCell;
use os32api::gui::proto::GUI_MAX_CLIP_DEPTH;
use os32api::gui::types::{Rect, ScreenInfo, SurfaceId};

pub const MAX_SURFACES: usize = os32api::gui::proto::GUI_MAX_SURFACES;

/// サーフェスの実体。
#[derive(Clone, Copy)]
pub enum SurfaceKind {
    /// 全画面バックバッファの部分矩形 (絶対原点 ox,oy)。窓のクライアント面 or フルスクリーン。
    Screen { ox: i32, oy: i32 },
    /// オフスクリーン (主記憶、既存 GFX_Surface)。
    Offscreen { surf: *mut GfxSurface },
}

#[derive(Clone, Copy)]
pub struct SurfaceEnt {
    pub used: bool,
    pub generation: u16,
    pub w: i16,
    pub h: i16,
    pub kind: SurfaceKind,
    /// 窓のクライアント面か。true のとき、Paint の基底クリップ未設定での描画を拒む (G2)。
    pub is_window: bool,
}

impl SurfaceEnt {
    const EMPTY: SurfaceEnt = SurfaceEnt {
        used: false,
        generation: 0,
        w: 0,
        h: 0,
        kind: SurfaceKind::Screen { ox: 0, oy: 0 },
        is_window: false,
    };
    /// サーフェスのローカル境界矩形 (0,0,w,h)。
    #[inline]
    pub fn bounds(&self) -> Rect {
        Rect::new(0, 0, self.w, self.h)
    }
}

pub struct GState {
    pub surfaces: [SurfaceEnt; MAX_SURFACES],

    /* --- クリップ (処理中の 1 サーフェスに対して有効) --- */
    pub active: SurfaceId,     /* クリップスタックが属するサーフェス (NULL=未設定) */
    pub have_base: bool,       /* set_base_clip 済みか */
    pub clip: [Rect; GUI_MAX_CLIP_DEPTH],
    pub depth: usize,          /* clip[depth] が現在の実効クリップ。clip[0]=基底 */

    /* --- 能力キャッシュ (screen_info)。lazy に埋める --- */
    pub screen: ScreenInfo,
    pub screen_valid: bool,

    /// フルスクリーンサーフェス (gdi_test / デスクトップ用) のキャッシュ。
    pub screen_surf: SurfaceId,

    /// 一度でも debug で基底未設定違反を踏んだか (テスト観測用)。
    pub base_violations: u32,
}

impl GState {
    const NEW: GState = GState {
        surfaces: [SurfaceEnt::EMPTY; MAX_SURFACES],
        active: SurfaceId::NULL,
        have_base: false,
        clip: [Rect::EMPTY; GUI_MAX_CLIP_DEPTH],
        depth: 0,
        screen: ScreenInfo::ZERO,
        screen_valid: false,
        screen_surf: SurfaceId::NULL,
        base_violations: 0,
    };

    /// SurfaceId からスロット添字を引く (generation 検査つき)。
    pub fn resolve(&self, id: SurfaceId) -> Option<usize> {
        if id.is_null() {
            return None;
        }
        let idx = id.index() as usize;
        if idx >= MAX_SURFACES {
            return None;
        }
        let e = &self.surfaces[idx];
        if e.used && e.generation == id.generation() {
            Some(idx)
        } else {
            None
        }
    }

    /// 空きスロットを確保して SurfaceId を返す。
    pub fn alloc(&mut self, ent: SurfaceEnt) -> SurfaceId {
        let mut i = 0;
        while i < MAX_SURFACES {
            if !self.surfaces[i].used {
                let gen = self.surfaces[i].generation.wrapping_add(1).max(1);
                let mut e = ent;
                e.used = true;
                e.generation = gen;
                self.surfaces[i] = e;
                return SurfaceId::new(i as u16, gen);
            }
            i += 1;
        }
        SurfaceId::NULL
    }

    pub fn free(&mut self, idx: usize) {
        /* generation は据え置き (再確保時に +1 されて stale ID を弾く) */
        let gen = self.surfaces[idx].generation;
        self.surfaces[idx] = SurfaceEnt::EMPTY;
        self.surfaces[idx].generation = gen;
        /* 破棄したサーフェスがアクティブなら基底を落とす */
        if self.active.index() as usize == idx {
            self.active = SurfaceId::NULL;
            self.have_base = false;
            self.depth = 0;
        }
    }
}

struct Cell(UnsafeCell<GState>);
unsafe impl Sync for Cell {}
static STATE: Cell = Cell(UnsafeCell::new(GState::NEW));

#[inline]
pub fn st() -> &'static mut GState {
    unsafe { &mut *STATE.0.get() }
}

/// KAPI から screen_info を取り直してキャッシュする。
pub fn refresh_screen_info() -> ScreenInfo {
    let s = st();
    let mut info = ScreenInfo::ZERO;
    unsafe {
        let a = os32api::api();
        (a.gfx_screen_info)(&mut info as *mut ScreenInfo as *mut u8);
    }
    /* バックエンドが何も埋めない (旧カーネル) 場合の保険: 全画面バックバッファから補う。 */
    if info.width == 0 || info.height == 0 {
        let fb = unsafe { ffi::gfx_fb };
        info.width = fb.width as u16;
        info.height = fb.height as u16;
        if info.bpp == 0 {
            info.bpp = 4;
            info.format = os32api::gui::types::GFX_FMT_PLANAR4;
        }
    }
    s.screen = info;
    s.screen_valid = true;
    info
}

#[inline]
pub fn screen_info_cached() -> ScreenInfo {
    let s = st();
    if s.screen_valid {
        s.screen
    } else {
        refresh_screen_info()
    }
}
