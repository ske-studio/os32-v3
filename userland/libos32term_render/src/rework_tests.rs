use super::*;

#[test]
fn saved_partial_shortage_and_top_once() {
    let mut storage = [Cell::Single(' '); 4];
    let mut model = Model::new(&mut storage, 1, 4, width).unwrap();
    for c in "ABCD".chars() {
        model.write_char(c).unwrap();
    }
    let (local, cells) = saved_rows(&model, 2, 0..3).unwrap();
    assert_eq!(cells, &[Cell::Single('C'), Cell::Single('D')]);
    assert_eq!(local, 0);
    let (local, cells) = saved_rows(&model, 1, 1..4).unwrap();
    assert_eq!(cells, &[Cell::Single('C'), Cell::Single('D')]);
    assert_eq!(local, 1);
    let (local, cells) = saved_rows(&model, 3, 0..3).unwrap();
    assert_eq!(cells, &[Cell::Single('D')]);
    assert_eq!(local, 0);
}
#[test]
fn saved_clip_below_storage_and_top_roundtrip() {
    let mut storage = [Cell::Single(' '); 3];
    let mut model = Model::new(&mut storage, 1, 3, width).unwrap();
    for c in "ABC".chars() {
        model.write_char(c).unwrap();
    }
    for (top, expected) in [(0, 'A'), (2, 'C'), (0, 'A')] {
        assert_eq!(
            saved_rows(&model, top, 0..1).unwrap().1,
            &[Cell::Single(expected)]
        );
    }
    assert!(saved_rows(&model, 2, 1..3).unwrap().1.is_empty());
    assert!(saved_rows(&model, 3, 0..3).unwrap().1.is_empty());
}

fn rect(x0: i64, y0: i64, x1: i64, y1: i64) -> Rect {
    Rect { x0, y0, x1, y1 }
}
#[test]
fn halo_wide_head_for_right_half() {
    let clip = Grid {
        origin: (0, 0),
        cols: 8,
        rows: 1,
        cell_width: 8,
        cell_height: 16,
    }
    .clip(rect(16, 2, 24, 3))
    .unwrap()
    .unwrap();
    assert!(clip.columns.contains(&1));
    assert_eq!(
        visible_head(Cell::Wide('日'), 8, clip.write),
        Some(('日', 16))
    );
    assert_eq!(visible_head(Cell::Continuation, 16, clip.write), None);
    assert_eq!(visible_head(Cell::Single('A'), 24, clip.write), None);
    assert_eq!(visible_head(Cell::Single('A'), 8, clip.write), None);
}
#[test]
fn halo_single_and_left_half_at_negative_origin() {
    let write = rect(-6, -2, 0, 1);
    assert_eq!(visible_head(Cell::Single('A'), -7, write), Some(('A', 8)));
    assert_eq!(visible_head(Cell::Wide('日'), -7, write), Some(('日', 16)));
    assert_eq!(visible_head(Cell::Wide('日'), -22, write), None);
}

struct ExactGlyph {
    character: char,
    ank: u8,
    jis: Option<u16>,
    converts: usize,
    reads: usize,
}
impl Glyphs for ExactGlyph {
    fn jis(&mut self, c: char) -> Option<u16> {
        assert_eq!(c, self.character);
        self.converts += 1;
        self.jis
    }
    fn ank(&mut self, code: u8) -> [u8; 16] {
        assert_eq!(code, self.ank);
        self.reads += 1;
        [0x80; 16]
    }
    fn kanji(&mut self, code: u16) -> [u8; 32] {
        assert_eq!(Some(code), self.jis);
        self.reads += 1;
        [0x81; 32]
    }
}
#[test]
fn dispatch_ank_arguments() {
    for (c, code, frame, pixel) in [
        ('A', 65, 8, 0x8000),
        ('｡', 0xa1, 8, 0x8000),
        ('ﾟ', 0xdf, 8, 0x8000),
        ('¥', 0x5c, 8, 0x8000),
        ('Ａ', 65, 16, 0x0800),
    ] {
        let mut font = ExactGlyph {
            character: c,
            ank: code,
            jis: None,
            converts: 0,
            reads: 0,
        };
        let mut stats = Stats::default();
        let bits = glyph_bits(c, frame, &mut font, &mut stats);
        assert_eq!(font.reads, 1, "ANK {c}");
        assert_eq!(font.converts, 0);
        assert_eq!(stats.glyph_reads, 1);
        assert_eq!(bits, [pixel; 16]);
    }
}
#[test]
fn dispatch_unicode_and_jis_arguments() {
    for (c, jis) in [('日', 0x467c), ('本', 0x4b5c), ('語', 0x386c)] {
        let mut font = ExactGlyph {
            character: c,
            ank: 0,
            jis: Some(jis),
            converts: 0,
            reads: 0,
        };
        let mut stats = Stats::default();
        let bits = glyph_bits(c, 16, &mut font, &mut stats);
        assert_eq!(font.converts, 1);
        assert_eq!(font.reads, 1);
        assert_eq!(stats.glyph_reads, 1);
        assert_eq!(bits, [0x8181; 16]);
    }
}
#[test]
fn dispatch_fallback_without_bitmap_read() {
    for (c, jis, converts) in [
        ('\0', Some(0x2121), 0),
        ('\u{fffd}', Some(0x2121), 0),
        ('日', None, 1),
        ('日', Some(0x2021), 1),
        ('日', Some(0x217f), 1),
    ] {
        let mut font = ExactGlyph {
            character: c,
            ank: 0,
            jis,
            converts: 0,
            reads: 0,
        };
        let mut stats = Stats::default();
        let frame = if c == '\0' { 8 } else { 16 };
        let bits = glyph_bits(c, frame, &mut font, &mut stats);
        assert_eq!(bits[0], if frame == 8 { 0xff00 } else { 0xffff });
        assert_eq!(font.converts, converts);
        assert_eq!(font.reads, 0);
        assert_eq!(stats.glyph_reads, 0);
    }
}
