use crate::{state::Display, view::Layout};
use libos32term_render::{Error, Glyphs, Rect, Sink, Stats};
/// Pure app-to-renderer boundary. Does not grant GUI Paint authority.
pub fn body(
    s: &Display<'_>,
    layout: Layout,
    clip: Rect,
    glyphs: &mut impl Glyphs,
    sink: &mut impl Sink,
) -> Result<Stats, Error> {
    let view = layout
        .view(s.top, clip)
        .map_err(|_| Error::CoordinateRange)?;
    libos32term_render::render(s.terminal.model(), view, glyphs, sink)
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::state::{Fixture, CAPACITY};
    use libos32term::model::BLANK;
    #[derive(Default)]
    struct Font {
        reads: usize,
    }
    impl Glyphs for Font {
        fn jis(&mut self, _: char) -> Option<u16> {
            Some(0x467c)
        }
        fn ank(&mut self, _: u8) -> [u8; 16] {
            self.reads += 1;
            [0x81; 16]
        }
        fn kanji(&mut self, _: u16) -> [u8; 32] {
            self.reads += 1;
            [0xa5; 32]
        }
    }
    #[derive(Default)]
    struct Canvas {
        runs: Vec<(i32, i32, i32, bool)>,
    }
    impl Sink for Canvas {
        fn run(&mut self, x: i32, y: i32, n: i32, fg: bool) {
            self.runs.push((x, y, n, fg));
        }
    }
    fn pixels(c: &Canvas) -> std::collections::BTreeMap<(i32, i32), bool> {
        let mut out = std::collections::BTreeMap::new();
        for &(x, y, n, fg) in &c.runs {
            for xx in x..x + n {
                out.insert((xx, y), fg);
            }
        }
        out
    }
    #[test]
    fn partial_japanese_reexposure_matches_full_and_preserves_model() {
        let mut cells = [BLANK; CAPACITY];
        let mut s = Display::load(&mut cells, Fixture::Normal).unwrap();
        s.top = 1;
        let before = s.terminal.model().cells().to_vec();
        let state_before = s.terminal.model().state();
        let l = Layout::new(340, 300).unwrap();
        let mut font = Font::default();
        let mut full = Canvas::default();
        let stats = body(
            &s,
            l,
            Rect {
                x0: 0,
                y0: 0,
                x1: 340,
                y1: 300,
            },
            &mut font,
            &mut full,
        )
        .unwrap();
        assert!(stats.runs > 0);
        assert_eq!(stats.runs as usize, full.runs.len());
        assert_eq!(stats.glyph_reads as usize, font.reads);
        let full_pixels = pixels(&full);
        for clip in [
            Rect {
                x0: 8,
                y0: 88,
                x1: 16,
                y1: 104,
            },
            Rect {
                x0: 16,
                y0: 88,
                x1: 24,
                y1: 104,
            },
            Rect {
                x0: 9,
                y0: 89,
                x1: 23,
                y1: 90,
            },
            Rect {
                x0: 8,
                y0: 103,
                x1: 24,
                y1: 104,
            },
        ] {
            let mut cut = Canvas::default();
            body(&s, l, clip, &mut font, &mut cut).unwrap();
            let expected = full_pixels
                .iter()
                .filter(|((x, y), _)| {
                    (*x as i64) >= clip.x0
                        && (*x as i64) < clip.x1
                        && (*y as i64) >= clip.y0
                        && (*y as i64) < clip.y1
                })
                .map(|(k, v)| (*k, *v))
                .collect();
            assert_eq!(pixels(&cut), expected);
            let mut again = Canvas::default();
            body(&s, l, clip, &mut font, &mut again).unwrap();
            assert_eq!(again.runs, cut.runs);
        }
        assert_eq!(s.terminal.model().cells(), before);
        assert_eq!(s.terminal.model().state(), state_before);
    }
    #[test]
    fn invalid_geometry_has_no_callbacks() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::Normal).unwrap();
        let mut font = Font::default();
        let mut out = Canvas::default();
        assert!(body(
            &s,
            Layout::new(340, 300).unwrap(),
            Rect {
                x0: 0,
                y0: 0,
                x1: i64::MAX,
                y1: 300
            },
            &mut font,
            &mut out
        )
        .is_err());
        assert_eq!(font.reads, 0);
        assert!(out.runs.is_empty());
    }
    #[test]
    fn missing_saved_rows_are_background() {
        let mut cells = [BLANK; CAPACITY];
        let s = Display::load(&mut cells, Fixture::TooWide).unwrap();
        let mut font = Font::default();
        let mut out = Canvas::default();
        body(
            &s,
            Layout::new(340, 300).unwrap(),
            Rect {
                x0: 8,
                y0: 104,
                x1: 16,
                y1: 120,
            },
            &mut font,
            &mut out,
        )
        .unwrap();
        assert_eq!(out.runs.len(), 16);
        assert!(out.runs.iter().all(|r| !r.3));
        assert_eq!(font.reads, 0);
    }
}
