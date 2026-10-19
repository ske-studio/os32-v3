//! T7b-a5: one glyph -> complete 1bpp bitmap. The outline (a4) is rasterised
//! once at RASTER_PPEM, unhinted, with FreeType's 26.6 point scaling and
//! control-box placement, exact signed-area coverage and COVERAGE_THRESHOLD; the 32px image is the 16px bitmap doubled by nearest
//! neighbour (no 32ppem outline pass). Bitmap rows are top to bottom, byte
//! aligned, MSB = left pixel, unused tail bits 0 (glyph cache design §1bpp).
//! Nothing reaches the caller's buffer until every input was read, validated
//! and rasterised; any Err leaves the destination and the exposed coverage
//! untouched/empty. Call under the a2 x87 guard on OS32 (f32 arithmetic).
use crate::{
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
            PixelSize::Px32 => 2,
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
    accum: [f32; ACCUM_LEN],
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
    Ok(((i64::from(RASTER_PPEM) * 64 << 16) + upem / 2) / upem)
}

/// FT_MulFix: round half away from zero.
fn mul_fix(value: i64, scale: i64) -> i64 {
    let magnitude = (value.abs() * scale + 0x8000) >> 16;
    if value < 0 {
        -magnitude
    } else {
        magnitude
    }
}

/// Font units -> 26.6 at `size` exactly as FreeType scales unhinted metrics
/// (FT_MulFix with FT_DivFix's scale); 32px doubles the 16px value.
pub fn units_to_26_6(value: i32, units_per_em: u16, size: PixelSize) -> Result<i32> {
    let scaled = mul_fix(i64::from(value), scale_16_16(units_per_em)?);
    let base = i32::try_from(scaled).map_err(|_| Error::Bounds)?;
    base.checked_mul(size.factor() as i32).ok_or(Error::Bounds)
}

/// Outline coordinate (font units, f32 from the parser) -> 26.6. Integral and
/// half units (implicit on-curve midpoints) are exact FT_MulFix; any other
/// fraction (scaled components) rounds to nearest. NaN/overflow is rejected.
fn coord_26_6(value: f32, scale: i64) -> Result<i32> {
    if !(abs(value) <= MAX_COORD_UNITS) {
        return Err(Error::Invalid);
    }
    let twice = value * 2.0;
    let halves = twice as i64;
    let magnitude = if halves as f32 == twice {
        (halves.abs() * scale + 0x10000) >> 17
    } else {
        (abs(value) * (scale as f32 / 65536.0) + 0.5) as i64
    };
    let signed = if value < 0.0 { -magnitude } else { magnitude };
    i32::try_from(signed).map_err(|_| Error::Bounds)
}

fn floor(v: f32) -> i32 {
    let i = v as i32;
    if (i as f32) > v {
        i - 1
    } else {
        i
    }
}
fn ceil(v: f32) -> i32 {
    let i = v as i32;
    if (i as f32) < v {
        i + 1
    } else {
        i
    }
}
fn abs(v: f32) -> f32 {
    if v < 0.0 {
        -v
    } else {
        v
    }
}

struct Canvas<'a> {
    accum: &'a mut [f32],
    width: usize,
    height: usize,
    edges: usize,
}
impl Canvas<'_> {
    fn add(&mut self, row: usize, column: usize, value: f32) -> Result<()> {
        let at = row * self.width + column;
        *self.accum.get_mut(at).ok_or(Error::Bounds)? += value;
        Ok(())
    }

    /// One piece inside a single pixel column of one row.
    fn cell(&mut self, row: usize, xa: f32, xb: f32, d: f32) -> Result<()> {
        let mid = (xa + xb) * 0.5;
        let column = floor(mid).clamp(0, self.width as i32 - 1);
        let frac = mid - column as f32;
        let column = column as usize;
        self.add(row, column, d * (1.0 - frac))?;
        self.add(row, column + 1, d * frac)
    }

    fn line(&mut self, a: Point, b: Point) -> Result<()> {
        self.edges += 1;
        if self.edges > MAX_EDGES {
            return Err(Error::Limit);
        }
        if a.y == b.y {
            return Ok(());
        }
        let (dir, top, bottom) = if a.y < b.y { (1.0, a, b) } else { (-1.0, b, a) };
        let dxdy = (bottom.x - top.x) / (bottom.y - top.y);
        let last = (ceil(bottom.y).clamp(0, self.height as i32)) as usize;
        let mut row = (floor(top.y).clamp(0, self.height as i32)) as usize;
        while row < last {
            let y0 = top.y.max(row as f32);
            let y1 = bottom.y.min((row + 1) as f32);
            if y1 > y0 {
                let xa = top.x + (y0 - top.y) * dxdy;
                let xb = top.x + (y1 - top.y) * dxdy;
                self.span(row, xa, xb, (y1 - y0) * dir)?;
            }
            row += 1;
        }
        Ok(())
    }

    /// Split a row's sub-segment at integer x so every piece is in one column.
    fn span(&mut self, row: usize, xa: f32, xb: f32, d: f32) -> Result<()> {
        let w = self.width as f32;
        let xa = xa.clamp(0.0, w);
        let xb = xb.clamp(0.0, w);
        if floor(xa) == floor(xb) || ceil(xa) == ceil(xb) {
            return self.cell(row, xa, xb, d);
        }
        let (lo, hi) = if xa < xb { (xa, xb) } else { (xb, xa) };
        let width = hi - lo;
        let mut x = lo;
        let mut pieces = 0;
        while x < hi {
            pieces += 1;
            if pieces > self.width + 1 {
                return Err(Error::Invalid);
            }
            let next = ((floor(x) + 1) as f32).min(hi);
            self.cell(row, x, next, d * ((next - x) / width))?;
            x = next;
        }
        Ok(())
    }

    fn quad(&mut self, a: Point, c: Point, b: Point) -> Result<()> {
        // a1 edge model: n = ceil(sqrt(2 |a - 2c + b|)) segments (<= 1/8 px).
        let dx = a.x - 2.0 * c.x + b.x;
        let dy = a.y - 2.0 * c.y + b.y;
        let d2 = dx * dx + dy * dy;
        let mut n = 1usize;
        while ((n * n) as f32) * ((n * n) as f32) < 4.0 * d2 {
            n += 1;
            if n > MAX_EDGES {
                return Err(Error::Limit);
            }
        }
        let mut from = a;
        for i in 1..=n {
            let to = if i == n {
                b
            } else {
                let t = i as f32 / n as f32;
                let u = 1.0 - t;
                Point {
                    x: u * u * a.x + 2.0 * u * t * c.x + t * t * b.x,
                    y: u * u * a.y + 2.0 * u * t * c.y + t * t * b.y,
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
            accum: [0.0; ACCUM_LEN],
            bits: [0; BITS16_BYTES],
            out: [0; MAX_BITMAP_BYTES],
            coverage_len: 0,
            edges: 0,
        }
    }

    /// Coverage (0..=1, row-major, RASTER_PPEM bitmap size) of the last
    /// successful render; empty after any failure. Diagnostic/comparison only.
    pub fn coverage(&self) -> &[f32] {
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
    accum: &mut [f32; ACCUM_LEN],
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
    accum.get_mut(..len + 2).ok_or(Error::Bounds)?.fill(0.0);
    let mut canvas = Canvas {
        accum: accum.as_mut_slice(),
        width,
        height,
        edges: 0,
    };
    // Bitmap-relative pixels; exact in f32 (|26.6 offsets| <= 64 * MAX_BBOX_PX).
    let to_px = |p: &Point| -> Result<Point> {
        Ok(Point {
            x: (coord_26_6(p.x, scale)? - left * 64) as f32 / 64.0,
            y: (top * 64 - coord_26_6(p.y, scale)?) as f32 / 64.0,
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
            let point = |i: u16| outline.points.get(usize::from(i)).ok_or(Error::Bounds).and_then(to_px);
            let from = point(e.from)?;
            let to = point(e.to)?;
            first.get_or_insert(from);
            match e.control {
                None => canvas.line(from, to)?,
                Some(c) => canvas.quad(from, point(c)?, to)?,
            }
            last = Some(to);
        }
        // ttf-parser closes glyf contours explicitly; close defensively anyway.
        if let (Some(f), Some(l)) = (first, last) {
            if f != l {
                canvas.line(l, f)?;
            }
        }
    }
    let edges = canvas.edges;
    let pitch = width.div_ceil(8);
    bits.get_mut(..pitch * height).ok_or(Error::Bounds)?.fill(0);
    let mut sum = 0.0f32;
    for row in 0..height {
        for column in 0..width {
            let cell = accum.get_mut(row * width + column).ok_or(Error::Bounds)?;
            sum += *cell;
            let coverage = abs(sum).min(1.0);
            *cell = coverage;
            if coverage >= COVERAGE_THRESHOLD {
                *bits.get_mut(row * pitch + column / 8).ok_or(Error::Bounds)? |= 0x80 >> (column % 8);
            }
        }
    }
    Ok((placement, len, edges))
}
