//! GUI シェル v1.1 共有定義 (Rust 側) — 契約 [API_CONTRACTS.md] / TASKS §4。
//!
//! - [`proto`] … C `sdk/include/os32/os32_gui_shared.h` の写し (op 番号・イベント種別・
//!   Style フラグ・システム色・上限・SHM オフセット・エラー番号・16B イベント)。
//!   PM の `tools/check_gui_proto.py` が突き合わせる。**末尾追記のみ (契約 T5)。**
//! - [`types`] … G 描画 API の基本型 (Rect / Color / Style / SurfaceId / BitmapId /
//!   FontId / ScreenInfo / Stats)。
//!
//! 描画の実体 (libos32gfx を FFI で呼ぶ) は `userland/rust/libos32gui` にある。
//! ここには値と `#[repr(C)]` 型だけを置く。

//! - [`stub`] … 共有ライブラリ `libos32gui.shlib` のジャンプ表 ABI (票 C3)。
//!   ヘッダの形・エントリ番号・`App` / `Ui` / `AppVTable` はライブラリ側と
//!   アプリ側の両方がこの 1 か所を見る。
pub mod proto;
pub mod stub;
pub mod types;

pub use proto::*;
pub use types::*;
