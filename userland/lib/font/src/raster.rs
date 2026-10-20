//! T7b-a5: one glyph -> complete 1bpp bitmap. The outline (a4) is rasterised
//! once at RASTER_PPEM, unhinted, with FreeType's 26.6 point scaling and
//! control-box placement; the 32px image is the 16px bitmap doubled by nearest
//! neighbour (no 32ppem outline pass). TASK_FONTPERF F5: integers only — the
//! signed area/cover of every flattened line is accumulated per cell in
//! 1/RASTER_SUBPIXEL px (the integer area method of FreeType `ftgrays.c` and
//! the running-sum accumulation of font-rs; rules only, no code copied) and a
//! pixel is black at COVERAGE_BLACK_NUM / COVERAGE_BLACK_DEN. Bitmap rows are top to bottom, byte
//! aligned, MSB = left pixel, unused tail bits 0 (glyph cache design §1bpp).
//! Nothing reaches the caller's buffer until every input was read, validated
//! and rasterised; any Err leaves the destination and the exposed coverage
//! untouched/empty. No floating point (the a2 x87 guard was removed in F6).
use crate::{
    fixed::{div_fix, div_round, mul_fix, root4_ceil},
    limits::*,
    outline::{GlyphScratch, Outline, Point},
    reader::{Error, Face, Result, Source},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PixelSize {
    Px16,
    Px32,
}
impl PixelSize {
    /// Nearest-neighbour factor applied to the RASTER_PPEM bitmap.
    pub const fn factor(self) -> usize {
        match self {
            PixelSize::Px16 => 1,
            PixelSize::Px32 => MAX_PIXEL_FACTOR,
        }
    }
}

/// Bitmap placement relative to the pen on the baseline (y grows down in the
/// bitmap). `top` is the number of rows above the baseline; `advance` is 26.6.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Placement {
    pub width: u16,
    pub height: u16,
    pub pitch: u16,
    pub left: i16,
    pub top: i16,
    pub advance: i32,
}
impl Placement {
    pub fn bytes(&self) -> usize {
        usize::from(self.pitch) * usize::from(self.height)
    }
}

const ACCUM_LEN: usize = MAX_BBOX_PX * MAX_BBOX_PX + 2;
const BITS16_BYTES: usize = MAX_BBOX_PX.div_ceil(8) * MAX_BBOX_PX;

/// Caller-owned work area for one glyph at a time (outline + raster). No heap,
/// FD or registry. Share serially between faces.
pub struct RasterScratch {
    glyph: GlyphScratch,
    accum: [i32; ACCUM_LEN],
    bits: [u8; BITS16_BYTES],
    out: [u8; MAX_BITMAP_BYTES],
    coverage_len: usize,
    edges: usize,
}
const _: () = assert!(core::mem::size_of::<RasterScratch>() <= MAX_GLYPH_WORK_BYTES);
const _: () = assert!(MAX_BITMAP_BYTES >= (MAX_BBOX_PX * MAX_PIXEL_FACTOR).div_ceil(8) * MAX_BBOX_PX * MAX_PIXEL_FACTOR);

impl Default for RasterScratch {
    fn default() -> Self {
        Self::new()
    }
}

/// FreeType's 16.16 scale for RASTER_PPEM: FT_DivFix(ppem * 64, upem).
fn scale_16_16(units_per_em: u16) -> Result<i64> {
    let upem = i64::from(units_per_em);
    if upem == 0 {
        return Err(Error::Invalid);
    }
    Ok(div_fix(i64::from(RASTER_PPEM) * 64, upem))
}

/// Font units -> 26.6 at `size` exactly as FreeType scales unhinted metrics
/// (FT_MulFix with FT_DivFix's scale); 32px doubles the 16px value.
pub fn units_to_26_6(value: i32, units_per_em: u16, size: PixelSize) -> Result<i32> {
    let scaled = mul_fix(i64::from(value), scale_16_16(units_per_em)?);
    let base = i32::try_from(scaled).map_err(|_| Error::Bounds)?;
    base.checked_mul(size.factor() as i32).ok_or(Error::Bounds)
}

/// Outline coordinate (half font units) -> 26.6: FT_MulFix of value/2, i.e.
/// (|halves| * scale + 2^16) >> 17, half away from zero. Out of range is rejected.
fn coord_26_6(halves: i32, scale: i64) -> Result<i32> {
    let magnitude = i64::from(halves).abs();
    if magnitude > 2 * i64::from(MAX_COORD_UNITS) {
        return Err(Error::Invalid);
    }
    let rounded = (magnitude * scale + 0x10000) >> 17;
    i32::try_from(if halves < 0 { -rounded } else { rounded }).map_err(|_| Error::Bounds)
}

/// Bitmap-relative point in 1/RASTER_SUBPIXEL px, y down.
#[derive(Clone, Copy, PartialEq, Eq)]
struct Sub {
    x: i32,
    y: i32,
}

struct Canvas<'a> {
    accum: &'a mut [i32],
    width: usize,
    height: usize,
    edges: usize,
}
impl Canvas<'_> {
    fn add(&mut self, at: usize, value: i32) -> Result<()> {
        let cell = self.accum.get_mut(at).ok_or(Error::Bounds)?;
        *cell = cell.checked_add(value).ok_or(Error::Invalid)?;
        Ok(())
    }

    /// One piece inside a single pixel column of one row: `dy` (signed
    /// subpixels) of cover; the area left of the piece's mean x stays in this
    /// cell, the rest carries to the next (the running sum adds `dy` to every
    /// pixel further right). Units: one full pixel = 2 * RASTER_SUBPIXEL^2.
    fn cell(&mut self, row: usize, xa: i32, xb: i32, dy: i32) -> Result<()> {
        let column = (xa.min(xb) >> RASTER_SUBPIXEL_BITS).clamp(0, self.width as i32 - 1);
        let twice_mid = xa + xb - 2 * column * RASTER_SUBPIXEL;
        let at = row * self.width + column as usize;
        self.add(at, dy * (2 * RASTER_SUBPIXEL - twice_mid))?;
        self.add(at + 1, dy * twice_mid)
    }

    fn line(&mut self, a: Sub, b: Sub) -> Result<()> {
        self.edges += 1;
        if self.edges > MAX_EDGES {
            return Err(Error::Limit);
        }
        if a.y == b.y {
            return Ok(());
        }
        let (dir, top, bottom) = if a.y < b.y { (1, a, b) } else { (-1, b, a) };
        let (dx, dy) = (i64::from(bottom.x - top.x), i64::from(bottom.y - top.y));
        let x_at = |y: i32| -> Result<i32> {
            let x = i64::from(top.x) + div_round(i64::from(y - top.y) * dx, dy);
            i32::try_from(x).map_err(|_| Error::Bounds)
        };
        let rows = self.height as i32;
        let last = ((bottom.y + RASTER_SUBPIXEL - 1) >> RASTER_SUBPIXEL_BITS).clamp(0, rows);
        let mut row = (top.y >> RASTER_SUBPIXEL_BITS).clamp(0, rows);
        while row < last {
            let y0 = top.y.max(row << RASTER_SUBPIXEL_BITS);
            let y1 = bottom.y.min((row + 1) << RASTER_SUBPIXEL_BITS);
            if y1 > y0 {
                self.span(row as usize, x_at(y0)?, x_at(y1)?, (y1 - y0) * dir)?;
            }
            row += 1;
        }
        Ok(())
    }

    /// Split a row's sub-segment at pixel columns; each piece gets its share of
    /// `dy` from the cumulative rounding, so the pieces sum to `dy` exactly.
    fn span(&mut self, row: usize, xa: i32, xb: i32, dy: i32) -> Result<()> {
        let w = self.width as i32 * RASTER_SUBPIXEL;
        let (xa, xb) = (xa.clamp(0, w), xb.clamp(0, w));
        let (lo, hi) = if xa < xb { (xa, xb) } else { (xb, xa) };
        if lo == hi || lo >> RASTER_SUBPIXEL_BITS == (hi - 1) >> RASTER_SUBPIXEL_BITS {
            return self.cell(row, xa, xb, dy);
        }
        let width = i64::from(hi - lo);
        let mut x = lo;
        let mut done = 0;
        let mut pieces = 0;
        while x < hi {
            pieces += 1;
            if pieces > self.width + 1 {
                return Err(Error::Invalid);
            }
            let next = (((x >> RASTER_SUBPIXEL_BITS) + 1) << RASTER_SUBPIXEL_BITS).min(hi);
            let share = div_round(i64::from(dy) * i64::from(next - lo), width);
            let share = i32::try_from(share).map_err(|_| Error::Bounds)?;
            self.cell(row, x, next, share - done)?;
            done = share;
            x = next;
        }
        Ok(())
    }

    fn quad(&mut self, a: Sub, c: Sub, b: Sub) -> Result<()> {
        // a1 edge model: n^4 >= QUAD_FLATNESS_SQ * d2 (px^2; <= 1/8 px).
        let dx = i64::from(a.x - 2 * c.x + b.x);
        let dy = i64::from(a.y - 2 * c.y + b.y);
        let d2 = u64::try_from(dx * dx + dy * dy).map_err(|_| Error::Bounds)?;
        let unit = (RASTER_SUBPIXEL as u64) * (RASTER_SUBPIXEL as u64);
        let n = root4_ceil(QUAD_FLATNESS_SQ * d2, unit, MAX_EDGES as u32).ok_or(Error::Limit)?;
        let n = i64::from(n);
        let nn = n * n;
        let mut from = a;
        for i in 1..=n {
            let to = if i == n {
                b
            } else {
                let (t, u) = (i, n - i);
                let at = |p: i32, q: i32, r: i32| -> Result<i32> {
                    let v = u * u * i64::from(p) + 2 * u * t * i64::from(q) + t * t * i64::from(r);
                    i32::try_from(div_round(v, nn)).map_err(|_| Error::Bounds)
                };
                Sub {
                    x: at(a.x, c.x, b.x)?,
                    y: at(a.y, c.y, b.y)?,
                }
            };
            self.line(from, to)?;
            from = to;
        }
        Ok(())
    }
}

impl RasterScratch {
    pub const fn new() -> Self {
        Self {
            glyph: GlyphScratch::new(),
            accum: [0; ACCUM_LEN],
            bits: [0; BITS16_BYTES],
            out: [0; MAX_BITMAP_BYTES],
            coverage_len: 0,
            edges: 0,
        }
    }

    /// Coverage (0..=COVERAGE_ONE, row-major, RASTER_PPEM bitmap size) of the
    /// last successful render; empty after any failure. Diagnostic/comparison only.
    pub fn coverage(&self) -> &[i32] {
        self.accum.get(..self.coverage_len).unwrap_or(&[])
    }

    /// Flattened line edges of the last successful render (MAX_EDGES bound).
    pub fn flattened_edges(&self) -> usize {
        self.edges
    }

    /// Render glyph `gid` completely, then copy it into `dst` (needs
    /// Placement::bytes(); MAX_BITMAP_BYTES always suffices). On Err nothing
    /// is written to `dst`.
    pub fn render<S: Source>(
        &mut self,
        face: &mut Face<S>,
        gid: u16,
        size: PixelSize,
        dst: &mut [u8],
    ) -> Result<Placement> {
        self.coverage_len = 0;
        self.edges = 0;
        let upem = face.metrics().units_per_em;
        let advance = units_to_26_6(i32::from(face.advance(gid)?), upem, PixelSize::Px16)?;
        let outline = self.glyph.outline(face, gid)?;
        let (base, len, edges) = raster(&outline, upem, &mut self.accum, &mut self.bits)?;
        let factor = size.factor();
        let width = usize::from(base.width) * factor;
        let height = usize::from(base.height) * factor;
        let pitch = width.div_ceil(8);
        let needed = pitch.checked_mul(height).ok_or(Error::Bounds)?;
        let placement = Placement {
            width: u16::try_from(width).map_err(|_| Error::Bounds)?,
            height: u16::try_from(height).map_err(|_| Error::Bounds)?,
            pitch: u16::try_from(pitch).map_err(|_| Error::Bounds)?,
            left: base.left.checked_mul(factor as i16).ok_or(Error::Bounds)?,
            top: base.top.checked_mul(factor as i16).ok_or(Error::Bounds)?,
            advance: advance.checked_mul(factor as i32).ok_or(Error::Bounds)?,
        };
        let out = self.out.get_mut(..needed).ok_or(Error::Limit)?;
        out.fill(0);
        let src_pitch = usize::from(base.pitch);
        for row in 0..height {
            for column in 0..width {
                let src = (row / factor) * src_pitch + (column / factor) / 8;
                let bit = 0x80u8 >> ((column / factor) % 8);
                if self.bits.get(src).ok_or(Error::Bounds)? & bit != 0 {
                    *out.get_mut(row * pitch + column / 8).ok_or(Error::Bounds)? |=
                        0x80 >> (column % 8);
                }
            }
        }
        // Commit point: everything above may fail; the copy below cannot.
        let target = dst.get_mut(..needed).ok_or(Error::Limit)?;
        target.copy_from_slice(out);
        self.coverage_len = len;
        self.edges = edges;
        Ok(placement)
    }
}

/// Rasterise at RASTER_PPEM into `accum` (coverage) and `bits` (1bpp, 16px).
fn raster(
    outline: &Outline<'_>,
    upem: u16,
    accum: &mut [i32; ACCUM_LEN],
    bits: &mut [u8; BITS16_BYTES],
) -> Result<(Placement, usize, usize)> {
    let scale = scale_16_16(upem)?;
    let mut bounds: Option<(i32, i32, i32, i32)> = None;
    // Control box (all points, as FreeType's FT_Outline_Get_CBox) in 26.6.
    let mut visit = |index: u16| -> Result<()> {
        let p = outline.points.get(usize::from(index)).ok_or(Error::Bounds)?;
        let (x, y) = (coord_26_6(p.x, scale)?, coord_26_6(p.y, scale)?);
        bounds = Some(match bounds {
            None => (x, y, x, y),
            Some((a, b, c, d)) => (a.min(x), b.min(y), c.max(x), d.max(y)),
        });
        Ok(())
    };
    for e in outline.edges {
        visit(e.from)?;
        if let Some(c) = e.control {
            visit(c)?;
        }
        visit(e.to)?;
    }
    let Some((x0, y0, x1, y1)) = bounds else {
        return Ok((Placement::default(), 0, 0));
    };
    // Pixel-grid floor/ceil of the 26.6 box (arithmetic shifts floor).
    let (left, right, bottom, top) = (x0 >> 6, (x1 + 63) >> 6, y0 >> 6, (y1 + 63) >> 6);
    let width = usize::try_from(right - left).map_err(|_| Error::Invalid)?;
    let height = usize::try_from(top - bottom).map_err(|_| Error::Invalid)?;
    if width > MAX_BBOX_PX || height > MAX_BBOX_PX {
        return Err(Error::Limit);
    }
    let placement = Placement {
        width: width as u16,
        height: height as u16,
        pitch: width.div_ceil(8) as u16,
        left: i16::try_from(left).map_err(|_| Error::Bounds)?,
        top: i16::try_from(top).map_err(|_| Error::Bounds)?,
        advance: 0,
    };
    if width == 0 || height == 0 {
        // Zero-area control box (e.g. a lone line): no ink, nothing to draw.
        return Ok((Placement { width: 0, height: 0, pitch: 0, ..placement }, 0, 0));
    }
    let len = width * height;
    accum.get_mut(..len + 2).ok_or(Error::Bounds)?.fill(0);
    let mut canvas = Canvas {
        accum: accum.as_mut_slice(),
        width,
        height,
        edges: 0,
    };
    // Bitmap-relative subpixels (26.6 x 64; |offsets| <= RASTER_SUBPIXEL * MAX_BBOX_PX).
    let per_26_6 = RASTER_SUBPIXEL / 64;
    let to_sub = |p: &Point| -> Result<Sub> {
        Ok(Sub {
            x: (coord_26_6(p.x, scale)? - left * 64) * per_26_6,
            y: (top * 64 - coord_26_6(p.y, scale)?) * per_26_6,
        })
    };
    for c in outline.contours {
        let edges = outline
            .edges
            .get(usize::from(c.first_edge)..usize::from(c.end_edge))
            .ok_or(Error::Bounds)?;
        let mut first = None;
        let mut last = None;
        for e in edges {
            let point = |i: u16| outline.points.get(usize::from(i)).ok_or(Error::Bounds).and_then(to_sub);
            let from = point(e.from)?;
            let to = point(e.to)?;
            first.get_or_insert(from);
            match e.control {
                None => canvas.line(from, to)?,
                Some(c) => canvas.quad(from, point(c)?, to)?,
            }
            last = Some(to);
        }
        // glyf contours are closed explicitly; close defensively anyway.
        if let (Some(f), Some(l)) = (first, last) {
            if f != l {
                canvas.line(l, f)?;
            }
        }
    }
    let edges = canvas.edges;
    let pitch = width.div_ceil(8);
    bits.get_mut(..pitch * height).ok_or(Error::Bounds)?.fill(0);
    let mut sum = 0i32;
    for row in 0..height {
        for column in 0..width {
            let cell = accum.get_mut(row * width + column).ok_or(Error::Bounds)?;
            sum = sum.checked_add(*cell).ok_or(Error::Invalid)?;
            let area = sum.unsigned_abs();
            *cell = (area >> (2 * RASTER_SUBPIXEL_BITS + 1 - COVERAGE_BITS)).min(COVERAGE_ONE as u32) as i32;
            if u64::from(area) * COVERAGE_BLACK_DEN >= COVERAGE_BLACK_NUM * RASTER_AREA_ONE {
                *bits.get_mut(row * pitch + column / 8).ok_or(Error::Bounds)? |= 0x80 >> (column % 8);
            }
        }
    }
    Ok((placement, len, edges))
}
