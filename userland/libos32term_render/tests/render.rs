use libos32term::model::Width;
use libos32term_render::*;
#[test]
fn widths() {
    for n in 0..=0x10ffff {
        if let Some(c) = char::from_u32(n) {
            let single = n <= 0x7f || n == 0xa5 || (0xff61..=0xff9f).contains(&n);
            assert_eq!(
                width(c),
                if single { Width::Single } else { Width::Wide },
                "{n:x}"
            );
        }
    }
}
#[test]
fn ank_mapping() {
    for n in 0..=0x10ffff {
        if let Some(c) = char::from_u32(n) {
            let expected = match n {
                0x20..=0x7e => Some(n as u8),
                0xa5 => Some(0x5c),
                0xff61..=0xff9f => Some((n - 0xff61 + 0xa1) as u8),
                0xff01..=0xff5e => Some((n - 0xff01 + 0x21) as u8),
                _ => None,
            };
            assert_eq!(ank(c), expected, "{n:x}");
        }
    }
}

use libos32term::model::{Cell, Model};
#[derive(Default)]
struct Font {
    calls: usize,
    reads: usize,
    mapping: Option<u16>,
    blank: bool,
}
impl Glyphs for Font {
    fn jis(&mut self, _: char) -> Option<u16> {
        self.calls += 1;
        self.mapping
    }
    fn ank(&mut self, _: u8) -> [u8; 16] {
        self.calls += 1;
        self.reads += 1;
        if self.blank {
            [0; 16]
        } else {
            [0b01110100; 16]
        }
    }
    fn kanji(&mut self, _: u16) -> [u8; 32] {
        self.calls += 1;
        self.reads += 1;
        let mut a = [0; 32];
        if !self.blank {
            for y in 0..16 {
                a[2 * y] = 0x80 >> (y % 8);
                a[2 * y + 1] = 0xf0 | (1 << (y % 4));
            }
        }
        a
    }
}
#[derive(Default)]
struct Output {
    runs: Vec<(i32, i32, i32, bool)>,
}
impl Sink for Output {
    fn run(&mut self, x: i32, y: i32, n: i32, f: bool) {
        self.runs.push((x, y, n, f));
    }
}
fn rect(x0: i64, y0: i64, x1: i64, y1: i64) -> Rect {
    Rect { x0, y0, x1, y1 }
}
fn view() -> View {
    View {
        top: 0,
        height: 1,
        origin: (0, 0),
        clip: rect(0, 0, 16, 16),
    }
}
#[test]
fn invalid_geometry_has_no_callbacks() {
    let mut cells = [Cell::Single(' '); 2];
    let model = Model::new(&mut cells, 2, 1, width).unwrap();
    let cases = [
        (
            View {
                height: 0,
                ..view()
            },
            Error::InvalidDimensions,
        ),
        (View { top: 2, ..view() }, Error::InvalidTop),
        (
            View {
                top: 1,
                height: usize::MAX,
                ..view()
            },
            Error::Overflow,
        ),
        (
            View {
                height: usize::MAX,
                ..view()
            },
            Error::Overflow,
        ),
        (
            View {
                origin: (i64::MAX, 0),
                ..view()
            },
            Error::Overflow,
        ),
        (
            View {
                origin: (0, i64::MAX),
                ..view()
            },
            Error::Overflow,
        ),
        (
            View {
                clip: rect(2, 0, 1, 1),
                ..view()
            },
            Error::InvalidClip,
        ),
        (
            View {
                clip: rect(0, 2, 1, 1),
                ..view()
            },
            Error::InvalidClip,
        ),
        (
            View {
                origin: (i64::MIN, 0),
                ..view()
            },
            Error::CoordinateRange,
        ),
        (
            View {
                origin: (i32::MAX as i64, 0),
                ..view()
            },
            Error::CoordinateRange,
        ),
    ];
    let mut failures = Vec::new();
    for (v, e) in cases {
        let mut font = Font::default();
        let mut out = Output::default();
        let got = render(&model, v, &mut font, &mut out);
        if got != Err(e) {
            failures.push(format!("{v:?}: expected {e:?}, got {got:?}"));
        }
        assert_eq!(font.calls, 0);
        assert!(out.runs.is_empty());
    }
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

#[test]
fn background_and_empty() {
    let mut cells = [Cell::Single(' '); 2];
    let model = Model::new(&mut cells, 2, 1, width).unwrap();
    let mut font = Font::default();
    let mut out = Output::default();
    let v = View {
        top: 1,
        height: 3,
        origin: (-5, -7),
        clip: rect(-2, -4, 7, 35),
    };
    let stats = render(&model, v, &mut font, &mut out).unwrap();
    assert_eq!(
        out.runs,
        (-4..35).map(|y| (-2, y, 9, false)).collect::<Vec<_>>()
    );
    assert_eq!(
        stats,
        Stats {
            runs: 39,
            glyph_reads: 0
        }
    );
    assert_eq!(font.calls, 0);
    for clip in [
        rect(11, 0, 20, 9),
        rect(1, 1, 1, 2),
        rect(0, 0, 2, 0),
        rect(-30, -30, -20, -20),
    ] {
        let mut out = Output::default();
        assert_eq!(
            render(&model, View { clip, ..v }, &mut font, &mut out),
            Ok(Stats::default())
        );
        assert!(out.runs.is_empty());
        assert_eq!(font.calls, 0);
    }
}

// Independent pixel oracle: no renderer clip, run, classification or bitmap decoder.
fn oracle_pixel(c: char, wide: bool, x: i64, y: i64, valid_jis: bool, blank: bool) -> bool {
    let n = c as u32;
    let mark = n < 32
        || n == 127
        || n == 0xfffd
        || (!(0x20..=0x7e).contains(&n)
            && n != 0xa5
            && !((0xff01..=0xff5e).contains(&n) || (0xff61..=0xff9f).contains(&n))
            && !valid_jis);
    if mark {
        let w = if wide { 16 } else { 8 };
        return x == 0 || x == w - 1 || y == 0 || y == 15 || x == y * (w - 1) / 15;
    }
    if blank {
        return false;
    }
    if (0x20..=0x7e).contains(&n)
        || n == 0xa5
        || ((0xff01..=0xff5e).contains(&n) || (0xff61..=0xff9f).contains(&n))
    {
        let offset = if wide { 4 } else { 0 };
        return [1, 2, 3, 5].contains(&(x - offset));
    }
    x == y % 8 || (8..12).contains(&x) || x == 15 - y % 4
}
fn check_pixels(
    out: &Output,
    v: View,
    cols: usize,
    placed: &[(usize, usize, char, bool)],
    valid_jis: bool,
    blank: bool,
) {
    // Rasterize the full small scene first, then select requested pixels independently.
    let w = cols * 8;
    let h = v.height * 16;
    let mut full = vec![false; w * h];
    for &(col, row, c, wide) in placed {
        if row < v.top || row >= v.top + v.height {
            continue;
        }
        for y in 0..16 {
            for x in 0..if wide { 16 } else { 8 } {
                full[((row - v.top) * 16 + y) * w + col * 8 + x] =
                    oracle_pixel(c, wide, x as i64, y as i64, valid_jis, blank);
            }
        }
    }
    // A surrounding sentinel canvas also detects writes outside the body.
    let cw = w + 8;
    let ch = h + 8;
    let mut actual = vec![2u8; cw * ch];
    for &(x, y, n, f) in &out.runs {
        assert!(n > 0);
        for dx in 0..n {
            let px = x as i64 + dx as i64;
            let py = y as i64;
            assert!(px >= v.clip.x0 && px < v.clip.x1 && py >= v.clip.y0 && py < v.clip.y1);
            assert!(
                px >= v.origin.0
                    && px < v.origin.0 + w as i64
                    && py >= v.origin.1
                    && py < v.origin.1 + h as i64
            );
            actual[(py - v.origin.1 + 4) as usize * cw + (px - v.origin.0 + 4) as usize] =
                u8::from(f);
        }
    }
    for yy in 0..ch {
        for xx in 0..cw {
            let x = xx as i64 - 4;
            let y = yy as i64 - 4;
            let px = x + v.origin.0;
            let py = y + v.origin.1;
            let expected = if x >= 0
                && x < w as i64
                && y >= 0
                && y < h as i64
                && px >= v.clip.x0
                && px < v.clip.x1
                && py >= v.clip.y0
                && py < v.clip.y1
            {
                u8::from(full[y as usize * w + x as usize])
            } else {
                2
            };
            assert_eq!(actual[yy * cw + xx], expected, "pixel ({px},{py}), {v:?}");
        }
    }
}
#[test]
fn ascii_pixels_runs_counts() {
    let mut cells = [Cell::Single(' '); 1];
    let mut model = Model::new(&mut cells, 1, 1, width).unwrap();
    model.write_char('A').unwrap();
    let mut font = Font::default();
    let mut out = Output::default();
    let stats = render(&model, view(), &mut font, &mut out).unwrap();
    check_pixels(&out, view(), 1, &[(0, 0, 'A', false)], false, false);
    let fg = out.runs.iter().copied().filter(|r| r.3).collect::<Vec<_>>();
    assert_eq!(
        fg,
        (0..16)
            .flat_map(|y| [(1, y, 3, true), (5, y, 1, true)])
            .collect::<Vec<_>>()
    );
    assert_eq!(
        stats,
        Stats {
            runs: 48,
            glyph_reads: 1
        }
    );
    assert_eq!(stats.runs, out.runs.len() as u64);
    assert_eq!(font.reads, 1);
}
#[test]
fn width_mismatch_preflight() {
    let mut cells = [Cell::Single(' '); 2];
    let mut model = Model::new(&mut cells, 2, 1, |_| Width::Single).unwrap();
    model.write_char('A').unwrap();
    model.write_char('日').unwrap();
    let mut font = Font::default();
    let mut out = Output::default();
    assert_eq!(
        render(&model, view(), &mut font, &mut out),
        Err(Error::WidthMismatch)
    );
    assert_eq!(font.calls, 0);
    assert!(out.runs.is_empty());
}

fn one_char(c: char, wide: bool, mapping: Option<u16>, blank: bool, reads: u64) {
    let cols = if wide { 2 } else { 1 };
    let mut cells = vec![Cell::Single(' '); cols];
    let mut model = Model::new(&mut cells, cols, 1, width).unwrap();
    model.write_char(c).unwrap();
    let mut font = Font {
        mapping,
        blank,
        ..Font::default()
    };
    let mut out = Output::default();
    let stats = render(&model, view(), &mut font, &mut out).unwrap();
    let valid = mapping
        .is_some_and(|j| (0x21..=0x7e).contains(&(j >> 8)) && (0x21..=0x7e).contains(&(j & 255)));
    check_pixels(&out, view(), cols, &[(0, 0, c, wide)], valid, blank);
    assert_eq!(stats.glyph_reads, reads);
    assert_eq!(font.reads as u64, reads);
    assert_eq!(stats.runs, out.runs.len() as u64);
}
#[test]
fn fullwidth_centered() {
    for c in ['！', 'Ａ', 'ｚ', '～'] {
        one_char(c, true, None, false, 1);
    }
}
#[test]
fn control_marks() {
    for n in (0..32).chain([127]) {
        if [8, 9, 10, 13].contains(&n) {
            continue;
        }
        one_char(char::from_u32(n).unwrap(), false, None, false, 0);
    }
}
#[test]
fn replacement_marks() {
    for c in ['\u{fffd}', '\u{301}', '😀', '日'] {
        one_char(c, true, None, false, 0);
    }
}
#[test]
fn replacement_ignores_mapping() {
    one_char('\u{fffd}', true, Some(0x2121), false, 0);
}

#[test]
fn kanji_layout() {
    for j in [0x2121, 0x7e7e] {
        one_char('日', true, Some(j), false, 1);
    }
}
#[test]
fn zero_glyph_is_blank() {
    one_char('A', false, None, true, 1);
    one_char('日', true, Some(0x2121), true, 1);
}
#[test]
fn invalid_jis_bytes() {
    // Each invalid byte, including values inside the naive integer interval.
    for j in [0x2021, 0x7f21, 0x2120, 0x217f, 0x2200, 0x21ff, 0, 0xffff] {
        one_char('日', true, Some(j), false, 0);
    }
    // Require conversion actually to run, while rejecting the bitmap read.
    let mut cells = [Cell::Single(' '); 2];
    let mut model = Model::new(&mut cells, 2, 1, width).unwrap();
    model.write_char('日').unwrap();
    let mut font = Font {
        mapping: Some(0x2200),
        ..Font::default()
    };
    let mut out = Output::default();
    render(&model, view(), &mut font, &mut out).unwrap();
    assert_eq!(font.calls, 1);
    assert_eq!(font.reads, 0);
}

fn mixed_model_tests(clips: &[Rect], origin: (i64, i64), tops: &[usize]) {
    let mut cells = [Cell::Single(' '); 24];
    let mut model = Model::new(&mut cells, 6, 4, width).unwrap();
    let mut placed = Vec::new();
    for row in 0..4 {
        if row != 0 {
            model.write_char('\n').unwrap();
        }
        for (col, c, wide) in [
            (0, ['日', 'Ａ', '\u{fffd}', '日'][row], true),
            (2, 'A', false),
            (3, '\0', false),
            (4, 'Ａ', true),
        ] {
            model.write_char(c).unwrap();
            placed.push((col, row, c, wide));
        }
    }
    assert_eq!(model.write_char('A'), Err(libos32term::model::Error::Full));
    let before = model.cells().to_vec();
    let state = model.state();
    let mut previous = None;
    for &top in tops {
        for &clip in clips {
            let v = View {
                top,
                height: 3,
                origin,
                clip,
            };
            let mut font = Font {
                mapping: Some(0x3021),
                ..Font::default()
            };
            let mut out = Output::default();
            let stats = render(&model, v, &mut font, &mut out).unwrap();
            check_pixels(&out, v, 6, &placed, true, false);
            assert_eq!(stats.runs, out.runs.len() as u64);
            assert_eq!(stats.glyph_reads, font.reads as u64);
            assert_eq!(model.cells(), before);
            assert_eq!(model.state(), state);
            if top == 0 && clip == clips[0] {
                if let Some(ref runs) = previous {
                    assert_eq!(&out.runs, runs);
                }
                previous = Some(out.runs);
            }
        }
    }
}
#[test]
fn japanese_halves_and_pixel_clips() {
    mixed_model_tests(
        &[
            rect(0, 0, 48, 48),
            rect(0, 0, 8, 16),
            rect(8, 0, 16, 16),
            rect(0, 0, 16, 1),
            rect(0, 15, 16, 16),
            rect(9, 3, 13, 29),
            rect(7, 14, 42, 33),
            rect(0, 0, 0, 16),
            rect(49, 0, 60, 20),
        ],
        (0, 0),
        &[0],
    );
}
#[test]
fn negative_origin_top_roundtrip_and_reexposure() {
    mixed_model_tests(
        &[
            rect(-7, -9, 41, 39),
            rect(2, -5, 13, 22),
            rect(-20, -20, 50, 50),
        ],
        (-7, -9),
        &[0, 2, 3, 4, 0],
    );
}
#[test]
fn i32_valid_edges_and_huge_clipped_height() {
    let mut cells = [Cell::Single(' '); 1];
    let model = Model::new(&mut cells, 1, 1, width).unwrap();
    for origin in [
        (i32::MIN as i64, i32::MIN as i64),
        (i32::MAX as i64 - 7, i32::MAX as i64 - 15),
    ] {
        let v = View {
            top: 1,
            height: 1,
            origin,
            clip: rect(origin.0, origin.1, origin.0 + 8, origin.1 + 16),
        };
        let mut font = Font::default();
        let mut out = Output::default();
        assert_eq!(
            render(&model, v, &mut font, &mut out),
            Ok(Stats {
                runs: 16,
                glyph_reads: 0
            })
        );
        assert_eq!(out.runs[0], (origin.0 as i32, origin.1 as i32, 8, false));
        assert_eq!(
            out.runs[15],
            (origin.0 as i32, (origin.1 + 15) as i32, 8, false)
        );
    }
    // 268M display rows but only one visible pixel; no scan of hidden rows.
    let v = View {
        top: 0,
        height: 268_435_456,
        origin: (0, i32::MIN as i64),
        clip: rect(1, 0, 2, 1),
    };
    let mut font = Font::default();
    let mut out = Output::default();
    assert_eq!(
        render(&model, v, &mut font, &mut out),
        Ok(Stats {
            runs: 1,
            glyph_reads: 0
        })
    );
    assert_eq!(out.runs, vec![(1, 0, 1, false)]);
    assert_eq!(font.calls, 0);
}
#[test]
fn only_visible_glyph_read_with_halo() {
    let mut cells = [Cell::Single(' '); 8];
    let mut model = Model::new(&mut cells, 8, 1, width).unwrap();
    for c in "A日AA日A".chars() {
        model.write_char(c).unwrap();
    }
    for clip in [rect(16, 2, 24, 3), rect(17, 15, 18, 16), rect(9, 0, 10, 1)] {
        let mut font = Font {
            mapping: Some(0x2121),
            ..Font::default()
        };
        let mut out = Output::default();
        let stats = render(&model, View { clip, ..view() }, &mut font, &mut out).unwrap();
        assert_eq!(stats.glyph_reads, 1);
        assert_eq!(font.calls, 2);
        check_pixels(
            &out,
            View { clip, ..view() },
            8,
            &[
                (0, 0, 'A', false),
                (1, 0, '日', true),
                (3, 0, 'A', false),
                (4, 0, 'A', false),
                (5, 0, '日', true),
                (7, 0, 'A', false),
            ],
            true,
            false,
        );
    }
}
#[test]
fn kana_yen_and_ascii_ank_frames() {
    for c in [' ', '~', '｡', 'ｿ', 'ﾟ', '¥'] {
        one_char(c, false, None, false, 1);
    }
}

#[test]
fn model_rejects_invalid_dimensions_and_capacity() {
    use libos32term::model::Error as ModelError;
    let mut cells = [Cell::Single('X'); 1];
    assert!(matches!(
        Model::new(&mut cells, 0, 1, width),
        Err(ModelError::InvalidDimensions)
    ));
    assert!(matches!(
        Model::new(&mut cells, 2, 1, width),
        Err(ModelError::InsufficientStorage)
    ));
    assert!(matches!(
        Model::new(&mut cells, usize::MAX, 2, width),
        Err(ModelError::Overflow)
    ));
    assert_eq!(cells, [Cell::Single('X')]);
}
#[test]
fn forwarded_glyph_arguments_and_forbidden_lookups() {
    struct Exact {
        expected_ank: Option<u8>,
        expected_jis: Option<u16>,
        converts: usize,
        reads: usize,
    }
    impl Glyphs for Exact {
        fn jis(&mut self, c: char) -> Option<u16> {
            assert_eq!(c, '日');
            self.converts += 1;
            self.expected_jis
        }
        fn ank(&mut self, c: u8) -> [u8; 16] {
            assert_eq!(Some(c), self.expected_ank);
            self.reads += 1;
            [0; 16]
        }
        fn kanji(&mut self, j: u16) -> [u8; 32] {
            assert_eq!(Some(j), self.expected_jis);
            self.reads += 1;
            [0; 32]
        }
    }
    for (c, a, j) in [
        ('A', Some(65), None),
        ('｡', Some(0xa1), None),
        ('ﾟ', Some(0xdf), None),
        ('¥', Some(0x5c), None),
        ('Ａ', Some(65), None),
        ('日', None, Some(0x467c)),
        ('\0', None, None),
        ('\u{fffd}', None, None),
    ] {
        let cols = if width(c) == Width::Wide { 2 } else { 1 };
        let mut cells = vec![Cell::Single(' '); cols];
        let mut model = Model::new(&mut cells, cols, 1, width).unwrap();
        model.write_char(c).unwrap();
        let mut font = Exact {
            expected_ank: a,
            expected_jis: j,
            converts: 0,
            reads: 0,
        };
        let mut out = Output::default();
        render(&model, view(), &mut font, &mut out).unwrap();
        assert_eq!(font.converts, usize::from(j.is_some()));
        assert_eq!(font.reads, usize::from(a.is_some() || j.is_some()));
    }
}
#[test]
fn no_fixed_row_cutoff() {
    let mut cells = vec![Cell::Single(' '); 1025];
    let mut model = Model::new(&mut cells, 1, 1025, width).unwrap();
    for _ in 0..1025 {
        model.write_char('A').unwrap();
    }
    let v = View {
        height: 1025,
        clip: rect(0, 0, 8, 16400),
        ..view()
    };
    let mut font = Font::default();
    let mut out = Output::default();
    let stats = render(&model, v, &mut font, &mut out).unwrap();
    assert_eq!(
        stats,
        Stats {
            runs: 1025 * 48,
            glyph_reads: 1025
        }
    );
    assert_eq!(stats.runs, out.runs.len() as u64);
    assert!(out.runs.contains(&(1, 16399, 3, true)));
}
