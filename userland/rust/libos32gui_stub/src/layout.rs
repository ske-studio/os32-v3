//! layout.rs — 箱レイアウト (契約 U7) の指定だけ。計算はライブラリ側。
//!
//! `SizeSpec` は境界で `(kind, v, rect)` に開いて渡す
//! (`os32api::gui::stub::SIZE_*`)。列挙の判別子には依存しない。

use os32api::gui::types::Rect;

/// 子の主軸の決め方 (契約 U7)。
#[derive(Clone, Copy)]
pub enum SizeSpec {
    /// 固定 px。
    Fixed(i16),
    /// 余りを重みで分ける。
    Flex(u16),
    /// 絶対座標配置 (親の内側原点からの相対矩形)。フローから外れる。
    Absolute(Rect),
}
