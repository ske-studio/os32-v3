use libos32term::model::{Cell, Model};
use libos32term_render::*;
struct NoGlyph;
impl Glyphs for NoGlyph {
    fn jis(&mut self, _: char) -> Option<u16> {
        panic!("unexpected conversion")
    }
    fn ank(&mut self, _: u8) -> [u8; 16] {
        panic!("unexpected ANK")
    }
    fn kanji(&mut self, _: u16) -> [u8; 32] {
        panic!("unexpected kanji")
    }
}
#[derive(Default)]
struct Runs(Vec<(i32, i32, i32, bool)>);
impl Sink for Runs {
    fn run(&mut self, x: i32, y: i32, n: i32, f: bool) {
        self.0.push((x, y, n, f));
    }
}
fn edge(origin: (i64, i64)) {
    let mut storage = [Cell::Single(' '); 1];
    let model = Model::new(&mut storage, 1, 1, width).unwrap();
    let mut out = Runs::default();
    let result = render(
        &model,
        View {
            top: 1,
            height: 1,
            origin,
            clip: Rect {
                x0: origin.0,
                y0: origin.1,
                x1: origin.0 + 8,
                y1: origin.1 + 16,
            },
        },
        &mut NoGlyph,
        &mut out,
    );
    assert_eq!(
        result,
        Ok(Stats {
            runs: 16,
            glyph_reads: 0
        })
    );
    assert_eq!(
        out.0,
        (0..16)
            .map(|dy| (origin.0 as i32, (origin.1 + dy) as i32, 8, false))
            .collect::<Vec<_>>()
    );
}
#[test]
fn accept_i32_max_pixel() {
    edge((i32::MAX as i64 - 7, i32::MAX as i64 - 15));
}
#[test]
fn accept_i32_min_pixel() {
    edge((i32::MIN as i64, i32::MIN as i64));
}

struct Pattern;
impl Glyphs for Pattern {
    fn jis(&mut self, c: char) -> Option<u16> {
        assert_eq!(c, '日');
        Some(0x467c)
    }
    fn ank(&mut self, code: u8) -> [u8; 16] {
        [code; 16]
    }
    fn kanji(&mut self, code: u16) -> [u8; 32] {
        assert_eq!(code, 0x467c);
        let mut data = [0; 32];
        for y in 0..16 {
            data[2 * y] = 0x80 >> (y % 8);
            data[2 * y + 1] = 0xf0;
        }
        data
    }
}
fn r(x0: i64, y0: i64, x1: i64, y1: i64) -> Rect {
    Rect { x0, y0, x1, y1 }
}
// Independently describes the full saved image. Does not call renderer helpers.
fn saved_pixel(row: usize, x: usize, y: usize) -> bool {
    match row {
        0 | 2 if x < 16 => {
            if x < 8 {
                x == y % 8
            } else {
                x < 12
            }
        }
        0 | 2 => {
            let code = match (row, x < 24) {
                (0, true) => 65,
                (0, false) => 66,
                (2, true) => 67,
                _ => 68,
            };
            code & (128 >> (x % 8)) != 0
        }
        1 if x < 16 => (4..12).contains(&x) && 67 & (128 >> (x - 4)) != 0,
        1 => {
            if x < 24 {
                x - 16 == y % 8
            } else {
                x < 28
            }
        }
        _ => false,
    }
}
fn cropped(top: usize, origin: (i64, i64), clip: Rect) {
    let mut storage = [Cell::Single(' '); 12];
    let mut model = Model::new(&mut storage, 4, 3, width).unwrap();
    for c in "日AB\nＣ日\n日CD".chars() {
        model.write_char(c).unwrap();
    }
    let state = model.state();
    let before = model.cells().to_vec();
    let view = View {
        top,
        height: 3,
        origin,
        clip,
    };
    let mut out = Runs::default();
    let stats = render(&model, view, &mut Pattern, &mut out).unwrap();
    assert_eq!(stats.runs, out.0.len() as u64);
    let mut pixels = [2u8; 36 * 52];
    for &(x, y, n, fg) in &out.0 {
        assert!(n > 0);
        for dx in 0..n {
            let px = i64::from(x) + i64::from(dx);
            let py = i64::from(y);
            assert!(px >= clip.x0 && px < clip.x1 && py >= clip.y0 && py < clip.y1);
            let lx = px - origin.0;
            let ly = py - origin.1;
            assert!((0..32).contains(&lx) && (0..48).contains(&ly));
            pixels[(ly as usize + 2) * 36 + lx as usize + 2] = u8::from(fg);
        }
    }
    for cy in 0..52 {
        for cx in 0..36 {
            let x = cx as i64 - 2;
            let y = cy as i64 - 2;
            let expected = if (0..32).contains(&x)
                && (0..48).contains(&y)
                && x + origin.0 >= clip.x0
                && x + origin.0 < clip.x1
                && y + origin.1 >= clip.y0
                && y + origin.1 < clip.y1
            {
                u8::from(saved_pixel(
                    top + y as usize / 16,
                    x as usize,
                    y as usize % 16,
                ))
            } else {
                2
            };
            assert_eq!(
                pixels[cy * 36 + cx],
                expected,
                "local ({x},{y}), top={top}, {clip:?}"
            );
        }
    }
    assert_eq!(model.cells(), before);
    assert_eq!(model.state(), state);
}
#[test]
fn crop_left_half() {
    cropped(0, (0, 0), r(0, 0, 8, 16));
}
#[test]
fn crop_right_half_from_halo() {
    cropped(0, (0, 0), r(8, 0, 16, 16));
}
#[test]
fn crop_top_pixel() {
    cropped(0, (0, 0), r(0, 0, 16, 1));
}
#[test]
fn crop_bottom_pixel() {
    cropped(0, (0, 0), r(0, 15, 16, 16));
}
#[test]
fn crop_unaligned() {
    cropped(0, (0, 0), r(9, 3, 29, 29));
}
#[test]
fn crop_negative_origin() {
    cropped(0, (-7, -9), r(2, -5, 23, 22));
}
#[test]
fn crop_partial_saved_shortage() {
    cropped(2, (0, 0), r(0, 0, 32, 48));
}
#[test]
fn crop_local_row_offset_and_top() {
    cropped(1, (-7, -9), r(-7, 9, 25, 39));
}

// First GREEN regression: pins the existing README scope, not a new TDD claim.
#[test]
fn width_mismatch_visible_rows_and_halo_scope_regression() {
    let mut storage = [Cell::Single(' '); 16];
    let mut model = Model::new(&mut storage, 8, 2, |_| libos32term::model::Width::Single).unwrap();
    for index in 0..16 {
        model
            .write_char(if index == 12 { '日' } else { '\0' })
            .unwrap();
    }
    for (clip, error) in [
        (r(0, 0, 64, 16), false),
        (r(0, 16, 8, 32), false),
        (r(24, 16, 32, 32), true),
        (r(32, 16, 40, 32), true),
        (r(32, 16, 32, 32), false),
    ] {
        let mut out = Runs::default();
        let result = render(
            &model,
            View {
                top: 0,
                height: 2,
                origin: (0, 0),
                clip,
            },
            &mut NoGlyph,
            &mut out,
        );
        if error {
            assert_eq!(result, Err(Error::WidthMismatch));
            assert!(out.0.is_empty());
        } else {
            assert!(result.is_ok());
        }
    }
}
