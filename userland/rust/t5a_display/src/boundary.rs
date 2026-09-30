use libos32term_render::Rect;
pub fn valid_jis(code: u16) -> Option<u16> {
    ((0x21..=0x7e).contains(&(code >> 8)) && (0x21..=0x7e).contains(&(code & 255))).then_some(code)
}
pub fn windows(sw: i64, sh: i64) -> Option<[Rect; 2]> {
    const BORDER_MARGIN: i64 = 8;
    const MAIN_WIDTH: i64 = 352;
    const MAIN_HEIGHT: i64 = 352;
    const COVER_WIDTH: i64 = 240;
    const COVER_HEIGHT: i64 = 160;
    if !(256..=i16::MAX as i64).contains(&sw) || !(176..=i16::MAX as i64).contains(&sh) {
        return None;
    }
    let first = Rect {
        x0: BORDER_MARGIN,
        y0: BORDER_MARGIN,
        x1: BORDER_MARGIN + MAIN_WIDTH.min(sw - 2 * BORDER_MARGIN),
        y1: BORDER_MARGIN + MAIN_HEIGHT.min(sh - 2 * BORDER_MARGIN),
    };
    let x = first.x1 / 2;
    let y = first.y1 / 2;
    Some([
        first,
        Rect {
            x0: x,
            y0: y,
            x1: (x + COVER_WIDTH).min(sw - BORDER_MARGIN),
            y1: (y + COVER_HEIGHT).min(sh - BORDER_MARGIN),
        },
    ])
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn jis_checks_both_bytes() {
        for code in [0x2121, 0x467c, 0x7e7e] {
            assert_eq!(valid_jis(code), Some(code));
        }
        for code in [0, 0x217f, 0x2200, 0x207e, 0x7f21] {
            assert_eq!(valid_jis(code), None);
        }
    }
    #[test]
    fn two_windows_fit_supported_screens() {
        for (sw, sh) in [(640, 400), (640, 480), (1024, 768), (256, 176)] {
            let wins = windows(sw, sh).unwrap_or(
                [Rect {
                    x0: 0,
                    y0: 0,
                    x1: 0,
                    y1: 0,
                }; 2],
            );
            for r in wins {
                assert!(r.x1 > r.x0 && r.y1 > r.y0);
                assert!(r.x0 >= 0 && r.y0 >= 0 && r.x1 <= sw && r.y1 <= sh);
            }
            assert!(wins[1].x0 < wins[0].x1 && wins[1].y0 < wins[0].y1);
        }
        assert!(windows(255, 400).is_none());
        assert!(windows(640, i64::MAX).is_none());
    }
}
