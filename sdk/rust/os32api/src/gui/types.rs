//! G 描画 API の基本型 (契約 G1 / G5) — Rust 側。
//!
//! `Rect` / `Color` / `Style` / `SurfaceId` / `BitmapId` / `FontId` と、能力問い合わせ
//! (`ScreenInfo` = C `GFX_ScreenInfo`) / カウンタ (`Stats` = C `GFX_Stats`) の写し。
//! いずれも `#[repr(C)]`。描画の実装は `userland/rust/libos32gui` にある。
#![allow(dead_code)]

use core::mem::size_of;

/* ======================================================================== */
/*  Color                                                                    */
/* ======================================================================== */
/// パレットインデックス。0〜15 はシステム色 (G6)、16〜255 は 256 色バックエンドのみ。
pub type Color = u8;

/* ======================================================================== */
/*  Rect — 座標は描画先サーフェスのローカル (G1)。C `GuiRect16` と同一。      */
/* ======================================================================== */
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Rect {
    pub x: i16,
    pub y: i16,
    pub w: i16,
    pub h: i16,
}

impl Rect {
    pub const EMPTY: Rect = Rect { x: 0, y: 0, w: 0, h: 0 };

    #[inline]
    pub const fn new(x: i16, y: i16, w: i16, h: i16) -> Rect {
        Rect { x, y, w, h }
    }

    /// 空か (w<=0 || h<=0)。
    #[inline]
    pub const fn is_empty(&self) -> bool {
        self.w <= 0 || self.h <= 0
    }

    #[inline]
    pub const fn right(&self) -> i32 {
        self.x as i32 + self.w as i32
    }
    #[inline]
    pub const fn bottom(&self) -> i32 {
        self.y as i32 + self.h as i32
    }

    /// 点を含むか (右下は排他)。
    #[inline]
    pub fn contains(&self, px: i32, py: i32) -> bool {
        px >= self.x as i32 && px < self.right() && py >= self.y as i32 && py < self.bottom()
    }

    /// 別矩形を完全に包含するか (クリップ判定用)。空矩形は「包含される」。
    #[inline]
    pub fn contains_rect(&self, r: &Rect) -> bool {
        if r.is_empty() {
            return true;
        }
        r.x as i32 >= self.x as i32
            && r.y as i32 >= self.y as i32
            && r.right() <= self.right()
            && r.bottom() <= self.bottom()
    }

    /// 交差。重ならなければ空矩形 (w=h=0)。i32 で計算し i16 に丸める。
    pub fn intersect(&self, o: &Rect) -> Rect {
        let x0 = core::cmp::max(self.x as i32, o.x as i32);
        let y0 = core::cmp::max(self.y as i32, o.y as i32);
        let x1 = core::cmp::min(self.right(), o.right());
        let y1 = core::cmp::min(self.bottom(), o.bottom());
        if x1 <= x0 || y1 <= y0 {
            return Rect::EMPTY;
        }
        Rect {
            x: x0 as i16,
            y: y0 as i16,
            w: (x1 - x0) as i16,
            h: (y1 - y0) as i16,
        }
    }
}

/* ======================================================================== */
/*  Style — ステートレス描画。呼び出しごとに渡す (G1)。                        */
/* ======================================================================== */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Style {
    pub fg: Color,
    pub bg: Color,
    pub flags: u8, /* GUI_STYLE_* (proto) */
}

impl Style {
    /// 前景/背景のみ (フラグなし)。
    #[inline]
    pub const fn new(fg: Color, bg: Color) -> Style {
        Style { fg, bg, flags: 0 }
    }
    /// 前景色だけ (線・枠用。bg は無視)。
    #[inline]
    pub const fn pen(fg: Color) -> Style {
        Style { fg, bg: 0, flags: 0 }
    }
    #[inline]
    pub const fn with_flags(mut self, flags: u8) -> Style {
        self.flags = flags;
        self
    }
    #[inline]
    pub const fn has(&self, flag: u8) -> bool {
        (self.flags & flag) != 0
    }
}

/* ======================================================================== */
/*  ハンドル (index:16 | generation:16、0 = 無効) — 契約 G1 / T4             */
/* ======================================================================== */
macro_rules! define_handle {
    ($name:ident) => {
        #[repr(transparent)]
        #[derive(Clone, Copy, PartialEq, Eq)]
        pub struct $name(pub u32);
        impl $name {
            pub const NULL: $name = $name(0);
            #[inline]
            pub const fn new(index: u16, generation: u16) -> $name {
                $name((index as u32) | ((generation as u32) << 16))
            }
            #[inline]
            pub const fn index(&self) -> u16 {
                (self.0 & 0xFFFF) as u16
            }
            #[inline]
            pub const fn generation(&self) -> u16 {
                (self.0 >> 16) as u16
            }
            #[inline]
            pub const fn is_null(&self) -> bool {
                self.0 == 0
            }
            #[inline]
            pub const fn raw(&self) -> u32 {
                self.0
            }
        }
    };
}

define_handle!(SurfaceId);
define_handle!(BitmapId);
define_handle!(FontId);

/// v1 の KCG 固定フォント (自動選択されるので実際は目安)。
impl FontId {
    /// ANK 8x16 (半角)。
    pub const ANK: FontId = FontId::new(1, 1);
    /// 漢字 16x16 (全角)。
    pub const KANJI: FontId = FontId::new(2, 1);
}

/* ======================================================================== */
/*  能力問い合わせ (G5) — C `GFX_ScreenInfo` と同一レイアウト                 */
/* ======================================================================== */
pub const GFX_FMT_PLANAR4: u8 = 0;
pub const GFX_FMT_PACKED8: u8 = 1;

pub const GFX_CAP_TEXT_OVERLAY: u32 = 0x0001;
pub const GFX_CAP_HW_FILL: u32 = 0x0002;
pub const GFX_CAP_HW_BLT: u32 = 0x0004;
pub const GFX_CAP_PAGE_FLIP: u32 = 0x0008;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ScreenInfo {
    pub width: u16,       /* @0 */
    pub height: u16,      /* @2 */
    pub bpp: u8,          /* @4 */
    pub format: u8,       /* @5  GFX_FMT_* */
    pub flags: u32,       /* @8  GFX_CAP_* (u32 整列で @6,7 はパディング) */
    pub lease_mask: u16,  /* @12 */
    pub lease_first: u16, /* @14 */
    pub lease_count: u16, /* @16 */
    pub reserved: [u16; 5],
}

impl ScreenInfo {
    pub const ZERO: ScreenInfo = ScreenInfo {
        width: 0,
        height: 0,
        bpp: 0,
        format: 0,
        flags: 0,
        lease_mask: 0,
        lease_first: 0,
        lease_count: 0,
        reserved: [0; 5],
    };
    #[inline]
    pub const fn has_cap(&self, cap: u32) -> bool {
        (self.flags & cap) != 0
    }
}

/* ======================================================================== */
/*  カウンタ (G7) — C `GFX_Stats` と同一レイアウト                           */
/* ======================================================================== */
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Stats {
    pub present_bytes: u32, /* VRAM へ転送したバイト数の累計 */
    pub hw_ops: u32,        /* アクセラレータの塗り/転送回数 */
    pub io_accesses: u32,   /* I/O ポートアクセス回数 */
    pub commits: u32,       /* commit 回数 */
}

impl Stats {
    pub const ZERO: Stats = Stats {
        present_bytes: 0,
        hw_ops: 0,
        io_accesses: 0,
        commits: 0,
    };
}

/* 静的表明 — C 側 GFX_ScreenInfo (28B) / GFX_Stats (16B) と一致 */
const _: () = assert!(size_of::<Rect>() == 8);
const _: () = assert!(size_of::<ScreenInfo>() == 28);
const _: () = assert!(size_of::<Stats>() == 16);
const _: () = assert!(size_of::<SurfaceId>() == 4);
