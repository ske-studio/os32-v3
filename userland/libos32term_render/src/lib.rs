#![no_std]
#![forbid(unsafe_code)]

//! Pure fixed-cell renderer. Model storage stays owned by the caller.
//! See README.md for coordinate, glyph and callback contracts.

use libos32term::model::Width;
/// T4-compatible width callback; fullwidth ANK fallback remains Wide.
pub fn width(c: char) -> Width {
    match c as u32 {
        0..=0x7f | 0xa5 | 0xff61..=0xff9f => Width::Single,
        _ => Width::Wide,
    }
}
/// Arithmetic ANK mapping, including fullwidth ASCII for centered rendering.
pub fn ank(c: char) -> Option<u8> {
    match c as u32 {
        n @ 0x20..=0x7e => Some(n as u8),
        0xa5 => Some(0x5c),
        n @ 0xff61..=0xff9f => Some((n - 0xff61 + 0xa1) as u8),
        n @ 0xff01..=0xff5e => Some((n - 0xff01 + 0x21) as u8),
        _ => None,
    }
}

pub use libos32term::clip::Rect;
use libos32term::{
    clip::{ClipError, Grid},
    model::{Cell, Model},
};
pub const CELL_WIDTH: i64 = 8;
pub const CELL_HEIGHT: i64 = 16;
#[derive(Clone, Copy, Debug)]
/// Pixel rectangles are half-open; height and top are measured in rows.
pub struct View {
    pub top: usize,
    pub height: usize,
    pub origin: (i64, i64),
    pub clip: Rect,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidDimensions,
    InvalidTop,
    InvalidCapacity,
    InvalidClip,
    Overflow,
    CoordinateRange,
    WidthMismatch,
}
#[derive(Clone, Copy, Default, Debug, Eq, PartialEq)]
pub struct Stats {
    pub runs: u64,
    pub glyph_reads: u64,
}
/// Infallible injected conversion and fixed-size glyph supply.
/// Bitmap bytes have their most significant bit on the left.
pub trait Glyphs {
    fn jis(&mut self, c: char) -> Option<u16>;
    fn ank(&mut self, code: u8) -> [u8; 16];
    fn kanji(&mut self, jis: u16) -> [u8; 32];
}
/// Infallible horizontal run output; true is foreground, false is background.
pub trait Sink {
    fn run(&mut self, x: i32, y: i32, length: i32, foreground: bool);
}
/// Preflight all detectable input errors before invoking either callback trait.
/// Only visible rows and the T4 column halo are inspected.
pub fn render(
    model: &Model<'_>,
    view: View,
    glyphs: &mut impl Glyphs,
    sink: &mut impl Sink,
) -> Result<Stats, Error> {
    if view.height == 0 {
        return Err(Error::InvalidDimensions);
    }
    view.top.checked_add(view.height).ok_or(Error::Overflow)?;
    if view.top > model.state().retained_rows.end {
        return Err(Error::InvalidTop);
    }
    let cols = model
        .viewport(0, 1)
        .map_err(|_| Error::InvalidCapacity)?
        .len();
    checked_axis(view.origin.0, cols, CELL_WIDTH)?;
    checked_axis(view.origin.1, view.height, CELL_HEIGHT)?;
    let grid = Grid {
        origin: view.origin,
        cols,
        rows: view.height,
        cell_width: CELL_WIDTH,
        cell_height: CELL_HEIGHT,
    };
    let Some(clip) = grid.clip(view.clip).map_err(|e| match e {
        ClipError::InvalidRect => Error::InvalidClip,
        ClipError::InvalidGrid => Error::InvalidDimensions,
        ClipError::Overflow => Error::Overflow,
    })?
    else {
        return Ok(Stats::default());
    };
    let write = clip.write;
    let length = i32::try_from(write.x1 - write.x0).map_err(|_| Error::CoordinateRange)?;
    let (first_row, saved) = saved_rows(model, view.top, clip.rows.clone())?;
    for row in saved.chunks_exact(cols) {
        for col in clip.columns.clone() {
            let valid = match row[col] {
                Cell::Single(c) => width(c) == Width::Single,
                Cell::Wide(c) => width(c) == Width::Wide,
                Cell::Continuation => true,
            };
            if !valid {
                return Err(Error::WidthMismatch);
            }
        }
    }
    let mut stats = Stats::default();
    for y in write.y0..write.y1 {
        sink.run(write.x0 as i32, y as i32, length, false);
        stats.runs += 1;
    }
    for (offset, row) in saved.chunks_exact(cols).enumerate() {
        let y = view.origin.1 + (first_row + offset) as i64 * CELL_HEIGHT;
        for col in clip.columns.clone() {
            let x = view.origin.0 + col as i64 * CELL_WIDTH;
            if let Some((c, frame_width)) = visible_head(row[col], x, write) {
                let bits = glyph_bits(c, frame_width, glyphs, &mut stats);
                draw(&bits, x, y, frame_width, write, sink, &mut stats);
            }
        }
    }
    Ok(stats)
}

fn checked_axis(origin: i64, cells: usize, pixels: i64) -> Result<(), Error> {
    let extent = i64::try_from(cells)
        .map_err(|_| Error::Overflow)?
        .checked_mul(pixels)
        .ok_or(Error::Overflow)?;
    let end = origin.checked_add(extent).ok_or(Error::Overflow)?;
    // Convert the last included pixel, not the half-open endpoint.
    i32::try_from(origin).map_err(|_| Error::CoordinateRange)?;
    i32::try_from(end - 1).map_err(|_| Error::CoordinateRange)?;
    Ok(())
}

// Retained pure bitmap operations: no glyph dispatch or clipping here.
fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    let mut bits = [0; 16];
    for (dst, src) in bits.iter_mut().zip(data) {
        *dst = (src as u16) << if wide { 4 } else { 8 };
    }
    bits
}
fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    let mut bits = [0; 16];
    for (dst, pair) in bits.iter_mut().zip(data.chunks_exact(2)) {
        *dst = u16::from_be_bytes([pair[0], pair[1]]);
    }
    bits
}
fn mark_bits(width: i64) -> [u16; 16] {
    let mut bits = [0; 16];
    for py in 0..CELL_HEIGHT {
        for px in 0..width {
            if px == 0
                || px == width - 1
                || py == 0
                || py == CELL_HEIGHT - 1
                || px == py * (width - 1) / (CELL_HEIGHT - 1)
            {
                bits[py as usize] |= 0x8000 >> px;
            }
        }
    }
    bits
}

fn saved_rows<'a>(
    model: &'a Model<'_>,
    top: usize,
    rows: core::ops::Range<usize>,
) -> Result<(usize, &'a [Cell]), Error> {
    // Current Model retains rows from zero; top was validated by render.
    let available = model.state().retained_rows.end - top;
    let first = rows.start.min(available);
    let count = rows.end.min(available) - first;
    let cells = model
        .viewport(top + first, count)
        .map_err(|_| Error::InvalidCapacity)?;
    Ok((first, cells))
}

#[cfg(test)]
mod rework_tests;

fn visible_head(cell: Cell, x: i64, write: Rect) -> Option<(char, i64)> {
    let head = match cell {
        Cell::Single(c) => (c, CELL_WIDTH),
        Cell::Wide(c) => (c, 2 * CELL_WIDTH),
        Cell::Continuation => return None,
    };
    (x < write.x1 && x + head.1 > write.x0).then_some(head)
}
fn glyph_bits(c: char, width: i64, glyphs: &mut impl Glyphs, stats: &mut Stats) -> [u16; 16] {
    if let Some(code) = ank(c) {
        let data = glyphs.ank(code);
        stats.glyph_reads += 1;
        return ank_bits(data, width == CELL_WIDTH * 2);
    }
    if c > '\u{7f}' && c != '\u{fffd}' {
        if let Some(code) = glyphs.jis(c).filter(|code| {
            (0x21..=0x7e).contains(&(code >> 8)) && (0x21..=0x7e).contains(&(code & 255))
        }) {
            let data = glyphs.kanji(code);
            stats.glyph_reads += 1;
            return kanji_bits(data);
        }
    }
    mark_bits(width)
}
fn draw(
    bits: &[u16; 16],
    x: i64,
    y: i64,
    width: i64,
    write: Rect,
    sink: &mut impl Sink,
    stats: &mut Stats,
) {
    let left = x.max(write.x0);
    let right = (x + width).min(write.x1);
    for py in y.max(write.y0)..(y + CELL_HEIGHT).min(write.y1) {
        let bits = bits[(py - y) as usize];
        let mut start = None;
        // The exclusive right edge is a sentinel that flushes a pending run.
        for px in left..=right {
            let foreground = px < right && bits & (0x8000 >> (px - x)) != 0;
            if foreground {
                if start.is_none() {
                    start = Some(px);
                }
            } else if let Some(first) = start.take() {
                sink.run(first as i32, py as i32, (px - first) as i32, true);
                stats.runs += 1;
            }
        }
    }
}
