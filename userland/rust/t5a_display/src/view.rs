use libos32term_render::{Rect, View, CELL_HEIGHT, CELL_WIDTH};
pub const MARGIN: i64 = 8;
pub const BODY_Y: i64 = MARGIN + crate::status::LINE_COUNT as i64 * CELL_HEIGHT;
/// 端末が自分で使う行数 (票 T7 E2 のプロンプト行)。出力 (con_sink) の領域とは
/// 分ける — 最下行は端末のもので、T4 モデルは 1 行も使わない。
pub const PROMPT_ROWS: usize = 1;
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Layout {
    width: i64,
    rows: usize,
}
impl Layout {
    pub fn new(width: i64, height: i64) -> Option<Self> {
        /* 出力 1 行 + プロンプト行が入らない窓は受けない。 */
        if !(2 * MARGIN + CELL_WIDTH..=i16::MAX as i64).contains(&width)
            || !(BODY_Y + (1 + PROMPT_ROWS as i64) * CELL_HEIGHT + MARGIN..=i16::MAX as i64)
                .contains(&height)
        {
            return None;
        }
        Some(Self {
            width,
            rows: ((height - BODY_Y - MARGIN) / CELL_HEIGHT) as usize,
        })
    }
    /// 本文 + プロンプト行の合計 (窓に入る行数)。ホスト試験だけが使う
    /// — 表示も追従も `body_rows()` で数える。
    #[allow(dead_code)]
    pub fn rows(self) -> usize {
        self.rows
    }
    /// con_sink の出力に使う行数 (最下行はプロンプトの分だけ空ける)。
    pub fn body_rows(self) -> usize {
        self.rows - PROMPT_ROWS
    }
    /// プロンプト行の上端 (client 座標)。
    pub fn prompt_y(self) -> i64 {
        BODY_Y + self.body_rows() as i64 * CELL_HEIGHT
    }
    /// プロンプト行に入る桁数。
    pub fn prompt_cols(self) -> usize {
        ((self.width - 2 * MARGIN) / CELL_WIDTH) as usize
    }
    pub fn view(self, top: usize, clip: Rect) -> Result<View, ()> {
        if clip.x0 < 0
            || clip.y0 < 0
            || clip.x1 > i16::MAX as i64
            || clip.y1 > i16::MAX as i64
            || clip.x1 < clip.x0
            || clip.y1 < clip.y0
        {
            return Err(());
        }
        let x0 = clip.x0.max(MARGIN);
        let y0 = clip.y0.max(BODY_Y);
        let clip = Rect {
            x0,
            y0,
            x1: clip.x1.min(self.width - MARGIN).max(x0),
            y1: clip.y1.min(self.prompt_y()).max(y0),
        };
        Ok(View {
            top,
            height: self.body_rows(),
            origin: (MARGIN, BODY_Y),
            clip,
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn client_rows_and_gui_preflight() {
        assert_eq!(Layout::new(340, 300).unwrap().rows, 12);
        for (w, h) in [
            (0, 300),
            (15, 300),
            (340, 103),
            (32768, 300),
            (340, i64::MAX),
        ] {
            assert_eq!(Layout::new(w, h), None);
        }
    }
    #[test]
    fn the_bottom_row_belongs_to_the_prompt_not_to_the_output() {
        let l = Layout::new(340, 300).unwrap();
        assert_eq!(l.rows(), 12);
        assert_eq!(l.body_rows(), 11);
        assert_eq!(l.prompt_y(), BODY_Y + 11 * CELL_HEIGHT);
        assert_eq!(l.prompt_cols(), (340 - 2 * MARGIN) as usize / 8);
        /* 本文はプロンプト行に食い込まない。 */
        let v = l
            .view(
                0,
                Rect {
                    x0: 0,
                    y0: 0,
                    x1: 340,
                    y1: 300,
                },
            )
            .unwrap();
        assert_eq!(v.height, 11);
        assert_eq!(v.clip.y1, l.prompt_y());
        /* 出力 1 行 + プロンプト行が入らない高さは受けない。 */
        assert_eq!(Layout::new(340, BODY_Y + CELL_HEIGHT + MARGIN), None);
        assert!(Layout::new(340, BODY_Y + 2 * CELL_HEIGHT + MARGIN).is_some());
    }

    #[test]
    fn paint_intersects_body_and_rejects_bad_coordinates() {
        let l = Layout::new(340, 300).unwrap();
        let v = l
            .view(
                7,
                Rect {
                    x0: 3,
                    y0: 80,
                    x1: 20,
                    y1: 110,
                },
            )
            .unwrap();
        assert_eq!(
            v.clip,
            Rect {
                x0: 8,
                y0: 88,
                x1: 20,
                y1: 110
            }
        );
        assert_eq!(v.top, 7);
        assert!(l
            .view(
                0,
                Rect {
                    x0: 0,
                    y0: 0,
                    x1: i64::MAX,
                    y1: 2
                }
            )
            .is_err());
        assert!(l
            .view(
                0,
                Rect {
                    x0: 2,
                    y0: 0,
                    x1: 1,
                    y1: 2
                }
            )
            .is_err());
    }
}
