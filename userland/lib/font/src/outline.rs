//! One glyph, streamed: the glyf bytes of a glyph and its components are read
//! into a bounded work area, then turned into edges by our own integer glyf
//! outliner (TASK_FONTPERF F5; ttf-parser stays only as the host test oracle).
//! Points are unhinted font units in **halves** (x2), so the implicit on-curve
//! midpoints are exact; composite transforms are F2Dot14 fixed point. Edges are
//! lines/quadratics; a5 owns pixel scaling and curve subdivision. The command
//! order and the bbox follow ttf-parser 0.21 `glyf` (the oracle in tests/outline.rs).
use crate::{
    fixed::{f2dot14_dot, F2DOT14_ONE},
    limits::*,
    reader::{Error, Face, Result, Source},
};

/// Outline point in half font units (2 = one font unit).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

/// Integer bbox in font units, truncated toward zero like ttf-parser's `Rect`.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub x_min: i16,
    pub y_min: i16,
    pub x_max: i16,
    pub y_max: i16,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Edge {
    pub from: u16,
    pub to: u16,
    /// None for a line, Some for a quadratic Bézier.
    pub control: Option<u16>,
}

/// Edge range [first_edge, end_edge); a close-only degenerate contour is empty.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Contour {
    pub first_edge: u16,
    pub end_edge: u16,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Usage {
    pub input_bytes: usize,
    pub glyphs: usize,
    pub components: usize,
    pub depth: usize,
    /// Stored glyf points, including repeated components, before implicit points.
    pub points: usize,
    pub contours: usize,
}

/// Only a completely collected, validated and outlined glyph can be borrowed.
/// An empty glyph succeeds with empty slices and no bbox.
pub struct Outline<'a> {
    pub points: &'a [Point],
    pub edges: &'a [Edge],
    pub contours: &'a [Contour],
    pub bounds: Option<Rect>,
    pub usage: Usage,
}

struct Output {
    points: [Point; MAX_OUTLINE_POINTS],
    edges: [Edge; MAX_EDGES],
    contours: [Contour; MAX_CONTOURS],
}

/// Caller-owned, reusable work area; share serially across faces. No allocation,
/// FD, global registry or bitmap cache. Failed calls expose zero committed items.
pub struct GlyphScratch {
    input: [u8; MAX_TOTAL_INPUT_BYTES],
    loca: [u8; (MAX_COMPONENTS + 2) * 2],
    output: Output,
    committed: (usize, usize, usize),
}
const _: () = assert!(core::mem::size_of::<GlyphScratch>() <= MAX_GLYPH_WORK_BYTES);

impl Default for GlyphScratch {
    fn default() -> Self {
        Self::new()
    }
}
impl GlyphScratch {
    pub const fn new() -> Self {
        Self {
            input: [0; MAX_TOTAL_INPUT_BYTES],
            loca: [0; (MAX_COMPONENTS + 2) * 2],
            output: Output {
                points: [Point { x: 0, y: 0 }; MAX_OUTLINE_POINTS],
                edges: [Edge {
                    from: 0,
                    to: 0,
                    control: None,
                }; MAX_EDGES],
                contours: [Contour {
                    first_edge: 0,
                    end_edge: 0,
                }; MAX_CONTOURS],
            },
            committed: (0, 0, 0),
        }
    }

    pub fn committed_counts(&self) -> (usize, usize, usize) {
        self.committed
    }

    pub fn outline<'a, S: Source>(
        &'a mut self,
        face: &mut Face<S>,
        gid: u16,
    ) -> Result<Outline<'a>> {
        self.committed = (0, 0, 0);
        let mut collect = Collector {
            input: &mut self.input,
            loca: &mut self.loca,
            ancestors: [0; MAX_COMPONENT_DEPTH + 1],
            usage: Usage::default(),
        };
        collect.glyph(face, gid, 0)?;
        let usage = collect.usage;
        if usage.glyphs == 0 {
            return Err(Error::Invalid);
        }
        let mut builder = Builder {
            output: &mut self.output,
            input: self.input.get(..usage.input_bytes).ok_or(Error::Bounds)?,
            loca: self.loca.get(..(usage.glyphs + 1) * 2).ok_or(Error::Bounds)?,
            transforms: [IDENTITY; MAX_COMPONENT_DEPTH + 1],
            depth: 0,
            points: 0,
            edges: 0,
            contours: 0,
            current: None,
            first_edge: 0,
            bbox: None,
            pen: Pen::default(),
        };
        builder.glyph(0, 0)?;
        if builder.current.is_some() {
            return Err(Error::Invalid);
        }
        let bounds = match builder.bbox {
            None => None,
            Some((x0, y0, x1, y1)) => Some(Rect {
                x_min: units(x0)?,
                y_min: units(y0)?,
                x_max: units(x1)?,
                y_max: units(y1)?,
            }),
        };
        let counts = (builder.points, builder.edges, builder.contours);
        let points = self.output.points.get(..counts.0).ok_or(Error::Bounds)?;
        let edges = self.output.edges.get(..counts.1).ok_or(Error::Bounds)?;
        let contours = self.output.contours.get(..counts.2).ok_or(Error::Bounds)?;
        self.committed = counts;
        Ok(Outline {
            points,
            edges,
            contours,
            bounds,
            usage,
        })
    }
}

/// Halves -> font units truncated toward zero (ttf-parser's float -> i16 bbox);
/// a bbox outside i16 is invalid (ttf-parser returns no outline then).
fn units(halves: i32) -> Result<i16> {
    i16::try_from(halves / 2).map_err(|_| Error::Invalid)
}

struct Cursor<'a> {
    data: &'a [u8],
    at: usize,
}
impl<'a> Cursor<'a> {
    fn skip(&mut self, len: usize) -> Result<()> {
        let end = self.at.checked_add(len).ok_or(Error::Bounds)?;
        self.data.get(self.at..end).ok_or(Error::Invalid)?;
        self.at = end;
        Ok(())
    }
    fn byte(&mut self) -> Result<u8> {
        let value = *self.data.get(self.at).ok_or(Error::Invalid)?;
        self.skip(1)?;
        Ok(value)
    }
    fn word(&mut self) -> Result<u16> {
        let high = self.byte()?;
        let low = self.byte()?;
        Ok(u16::from_be_bytes([high, low]))
    }
}

struct Collector<'a> {
    input: &'a mut [u8],
    loca: &'a mut [u8],
    ancestors: [u16; MAX_COMPONENT_DEPTH + 1],
    usage: Usage,
}
impl Collector<'_> {
    fn location(&mut self, index: usize, offset: usize) -> Result<()> {
        if offset % 2 != 0 {
            return Err(Error::Invalid);
        }
        let half = u16::try_from(offset / 2).map_err(|_| Error::Limit)?;
        self.loca
            .get_mut(index * 2..(index + 1) * 2)
            .ok_or(Error::Limit)?
            .copy_from_slice(&half.to_be_bytes());
        Ok(())
    }

    fn glyph<S: Source>(&mut self, face: &mut Face<S>, gid: u16, depth: usize) -> Result<u16> {
        if depth > MAX_COMPONENT_DEPTH {
            return Err(Error::Limit);
        }
        if self
            .ancestors
            .get(..depth)
            .ok_or(Error::Limit)?
            .contains(&gid)
        {
            return Err(Error::Invalid);
        }
        *self.ancestors.get_mut(depth).ok_or(Error::Limit)? = gid;
        let local = self.usage.glyphs;
        if local > MAX_COMPONENTS {
            return Err(Error::Limit);
        }
        // Ends are padded to an even byte for the short loca, so starts are even.
        let start = self.usage.input_bytes;
        let len = face.read_glyph(gid, self.input.get_mut(start..).ok_or(Error::Limit)?)?;
        let end = start + len;
        let padded = end.next_multiple_of(2);
        self.input.get(..padded).ok_or(Error::Limit)?;
        self.usage.input_bytes = padded;
        self.usage.glyphs += 1;
        self.usage.components = local;
        self.usage.depth = self.usage.depth.max(depth);
        self.location(local, start)?;
        self.location(local + 1, padded)?;
        if len == 0 {
            return Ok(local as u16);
        }
        let data = self.input.get(start..end).ok_or(Error::Bounds)?;
        let mut cursor = Cursor { data, at: 0 };
        let contours = cursor.word()? as i16;
        cursor.skip(8)?; // bbox, recalculated from the emitted points
        if contours >= 0 {
            simple(&mut cursor, contours as usize, &mut self.usage)?;
        } else {
            // Point-number attachment and scaled offsets are not implemented
            // (as in ttf-parser). Explicitly reject rather than misplace.
            let mut at = cursor.at;
            let mut instructions = false;
            loop {
                let mut c = Cursor {
                    data: self.input.get(start..end).ok_or(Error::Bounds)?,
                    at,
                };
                let flags = c.word()?;
                let patch = start + c.at;
                let child = c.word()?;
                if flags & 0xe010 != 0 {
                    return Err(Error::Invalid);
                }
                if flags & 2 == 0 || flags & 0x0800 != 0 {
                    return Err(Error::Unsupported);
                }
                let transforms = flags & (0x0008 | 0x0040 | 0x0080);
                if transforms.count_ones() > 1 {
                    return Err(Error::Invalid);
                }
                c.skip(if flags & 1 != 0 { 4 } else { 2 })?;
                c.skip(match transforms {
                    0x0008 => 2,
                    0x0040 => 4,
                    0x0080 => 8,
                    _ => 0,
                })?;
                at = c.at;
                instructions |= flags & 0x0100 != 0;
                let rewritten = self.glyph(face, child, depth + 1)?;
                self.input
                    .get_mut(patch..patch + 2)
                    .ok_or(Error::Bounds)?
                    .copy_from_slice(&rewritten.to_be_bytes());
                if flags & 0x0020 == 0 {
                    break;
                }
            }
            if instructions {
                let mut c = Cursor {
                    data: self.input.get(start..end).ok_or(Error::Bounds)?,
                    at,
                };
                let len = c.word()? as usize;
                c.skip(len)?;
            }
        }
        Ok(local as u16)
    }
}

fn simple(c: &mut Cursor<'_>, contours: usize, usage: &mut Usage) -> Result<()> {
    if contours == 0 {
        return Ok(());
    }
    usage.contours += contours;
    if usage.contours > MAX_CONTOURS {
        return Err(Error::Limit);
    }
    let mut previous = None;
    let mut points = 0;
    for _ in 0..contours {
        let end = c.word()? as usize;
        if previous.is_some_and(|p| end <= p) {
            return Err(Error::Invalid);
        }
        previous = Some(end);
        points = end + 1;
    }
    usage.points += points;
    if usage.points > MAX_POINTS {
        return Err(Error::Limit);
    }
    let instructions = c.word()? as usize;
    c.skip(instructions)?;
    let mut left = points;
    let mut coordinates = 0;
    while left != 0 {
        let flag = c.byte()?;
        let run = if flag & 8 != 0 {
            c.byte()? as usize + 1
        } else {
            1
        };
        if run > left {
            return Err(Error::Invalid);
        }
        let x = if flag & 2 != 0 {
            1
        } else if flag & 0x10 == 0 {
            2
        } else {
            0
        };
        let y = if flag & 4 != 0 {
            1
        } else if flag & 0x20 == 0 {
            2
        } else {
            0
        };
        coordinates += run * (x + y);
        left -= run;
    }
    c.skip(coordinates)
}


/// Composite transform: a..d F2Dot14 (x' = a x + c y + e, y' = b x + d y + f,
/// ttf-parser's `Transform` layout), e/f in halves.
#[derive(Clone, Copy)]
struct Transform {
    a: i32,
    b: i32,
    c: i32,
    d: i32,
    e: i32,
    f: i32,
}
const IDENTITY: Transform = Transform {
    a: F2DOT14_ONE,
    b: 0,
    c: 0,
    d: F2DOT14_ONE,
    e: 0,
    f: 0,
};
impl Transform {
    fn apply(&self, p: Point) -> Result<Point> {
        let x = f2dot14_dot(self.a, p.x, self.c, p.y) + i64::from(self.e);
        let y = f2dot14_dot(self.b, p.x, self.d, p.y) + i64::from(self.f);
        Ok(Point {
            x: i32::try_from(x).map_err(|_| Error::Invalid)?,
            y: i32::try_from(y).map_err(|_| Error::Invalid)?,
        })
    }
}

/// ttf-parser's per-contour pen state (raw, untransformed halves).
#[derive(Clone, Copy, Default)]
struct Pen {
    first_on: Option<Point>,
    first_off: Option<Point>,
    last_off: Option<Point>,
}

fn mid(a: Point, b: Point) -> Point {
    // Raw points are whole units (even halves): the midpoint is exact.
    Point {
        x: (a.x + b.x) / 2,
        y: (a.y + b.y) / 2,
    }
}

struct Flags<'a> {
    cursor: Cursor<'a>,
    repeats: u8,
    flags: u8,
}
impl Flags<'_> {
    fn next(&mut self) -> Result<u8> {
        if self.repeats == 0 {
            self.flags = self.cursor.byte()?;
            if self.flags & 8 != 0 {
                self.repeats = self.cursor.byte()?;
            }
        } else {
            self.repeats -= 1;
        }
        Ok(self.flags)
    }
}

/// One delta-coded coordinate (glyf simple-glyph flags; i16 wrapping sum).
fn delta(c: &mut Cursor<'_>, prev: &mut i16, short: bool, same_or_positive: bool) -> Result<()> {
    let n = if short {
        let v = i16::from(c.byte()?);
        if same_or_positive {
            v
        } else {
            -v
        }
    } else if same_or_positive {
        0
    } else {
        c.word()? as i16
    };
    *prev = prev.wrapping_add(n);
    Ok(())
}

struct Builder<'a> {
    output: &'a mut Output,
    input: &'a [u8],
    loca: &'a [u8],
    transforms: [Transform; MAX_COMPONENT_DEPTH + 1],
    depth: usize,
    points: usize,
    edges: usize,
    contours: usize,
    current: Option<u16>,
    first_edge: usize,
    bbox: Option<(i32, i32, i32, i32)>,
    pen: Pen,
}
impl Builder<'_> {
    fn location(&self, index: usize) -> Result<usize> {
        let at = index * 2;
        let b = self.loca.get(at..at + 2).ok_or(Error::Bounds)?;
        Ok(usize::from(u16::from_be_bytes([
            *b.first().ok_or(Error::Bounds)?,
            *b.last().ok_or(Error::Bounds)?,
        ])) * 2)
    }

    /// Outline local glyph `index` (the collector's mini loca) at `depth`.
    fn glyph(&mut self, index: usize, depth: usize) -> Result<()> {
        let (start, end) = (self.location(index)?, self.location(index + 1)?);
        if start >= end {
            return Ok(()); // empty glyph or component: nothing emitted
        }
        let input = self.input;
        let data = input.get(start..end).ok_or(Error::Bounds)?;
        let mut cursor = Cursor { data, at: 0 };
        let contours = cursor.word()? as i16;
        cursor.skip(8)?;
        self.depth = depth;
        if contours > 0 {
            return self.simple(&mut cursor, contours as usize);
        }
        if contours == 0 {
            return Ok(());
        }
        loop {
            let flags = cursor.word()?;
            let child = usize::from(cursor.word()?);
            let (e, f) = if flags & 1 != 0 {
                (cursor.word()? as i16, cursor.word()? as i16)
            } else {
                (i16::from(cursor.byte()? as i8), i16::from(cursor.byte()? as i8))
            };
            let mut t = Transform {
                e: i32::from(e) * 2,
                f: i32::from(f) * 2,
                ..IDENTITY
            };
            let mut f2dot14 = || -> Result<i32> { Ok(i32::from(cursor.word()? as i16)) };
            if flags & 0x0080 != 0 {
                t.a = f2dot14()?;
                t.b = f2dot14()?;
                t.c = f2dot14()?;
                t.d = f2dot14()?;
            } else if flags & 0x0040 != 0 {
                t.a = f2dot14()?;
                t.d = f2dot14()?;
            } else if flags & 0x0008 != 0 {
                t.a = f2dot14()?;
                t.d = t.a;
            }
            *self.transforms.get_mut(depth + 1).ok_or(Error::Limit)? = t;
            self.glyph(child, depth + 1)?;
            self.depth = depth;
            if flags & 0x0020 == 0 {
                return Ok(());
            }
        }
    }

    fn simple(&mut self, c: &mut Cursor<'_>, contours: usize) -> Result<()> {
        let ends = c.at;
        c.skip(contours * 2)?;
        let mut last = Cursor {
            data: c.data,
            at: c.at - 2,
        };
        let total = usize::from(last.word()?) + 1;
        if total == 1 {
            return Ok(()); // a lone point is ignored (ttf-parser)
        }
        let instructions = usize::from(c.word()?);
        c.skip(instructions)?;
        // Coordinate array lengths from the flags (the collector validated runs).
        let data = c.data;
        let mut flags = Flags {
            cursor: Cursor { data, at: c.at },
            repeats: 0,
            flags: 0,
        };
        let (mut x_len, mut y_len) = (0, 0);
        for _ in 0..total {
            let flag = flags.next()?;
            x_len += if flag & 2 != 0 { 1 } else if flag & 0x10 == 0 { 2 } else { 0 };
            y_len += if flag & 4 != 0 { 1 } else if flag & 0x20 == 0 { 2 } else { 0 };
        }
        let mut xs = Cursor {
            data,
            at: flags.cursor.at,
        };
        let mut ys = Cursor {
            data,
            at: xs.at + x_len,
        };
        data.get(..ys.at + y_len).ok_or(Error::Invalid)?;
        let mut flags = Flags {
            cursor: Cursor { data, at: c.at },
            repeats: 0,
            flags: 0,
        };
        let mut ends = Cursor { data, at: ends };
        let mut end = usize::from(ends.word()?);
        let (mut x, mut y) = (0i16, 0i16);
        for i in 0..total {
            let flag = flags.next()?;
            delta(&mut xs, &mut x, flag & 2 != 0, flag & 0x10 != 0)?;
            delta(&mut ys, &mut y, flag & 4 != 0, flag & 0x20 != 0)?;
            let last_point = i == end;
            if last_point && i + 1 < total {
                end = usize::from(ends.word()?);
            }
            let p = Point {
                x: i32::from(x) * 2,
                y: i32::from(y) * 2,
            };
            self.push(p, flag & 1 != 0, last_point)?;
        }
        Ok(())
    }

    /// ttf-parser 0.21 `glyf::Builder::push_point` (implicit midpoints).
    fn push(&mut self, p: Point, on_curve: bool, last_point: bool) -> Result<()> {
        let pen = self.pen;
        if pen.first_on.is_none() {
            if on_curve {
                self.pen.first_on = Some(p);
                self.move_to(p)?;
            } else if let Some(off) = pen.first_off {
                let m = mid(off, p);
                self.pen.first_on = Some(m);
                self.pen.last_off = Some(p);
                self.move_to(m)?;
            } else {
                self.pen.first_off = Some(p);
            }
        } else {
            match (pen.last_off, on_curve) {
                (Some(off), true) => {
                    self.pen.last_off = None;
                    self.quad_to(off, p)?;
                }
                (Some(off), false) => {
                    self.pen.last_off = Some(p);
                    self.quad_to(off, mid(off, p))?;
                }
                (None, true) => self.line_to(p)?,
                (None, false) => self.pen.last_off = Some(p),
            }
        }
        if last_point {
            self.finish()?;
        }
        Ok(())
    }

    fn finish(&mut self) -> Result<()> {
        let pen = self.pen;
        let mut last_off = pen.last_off;
        if let (Some(off1), Some(off2)) = (pen.first_off, pen.last_off) {
            last_off = None;
            self.quad_to(off2, mid(off2, off1))?;
        }
        match (pen.first_on, pen.first_off, last_off) {
            (Some(p), Some(off1), _) => self.quad_to(off1, p)?,
            (Some(p), None, Some(off2)) => self.quad_to(off2, p)?,
            (Some(p), None, None) => self.line_to(p)?,
            _ => {}
        }
        self.pen = Pen::default();
        self.close()
    }

    /// Raw halves -> transformed halves (innermost component first) + bbox.
    fn place(&mut self, raw: Point) -> Result<Point> {
        let mut p = raw;
        for t in self.transforms.get(1..=self.depth).ok_or(Error::Bounds)?.iter().rev() {
            p = t.apply(p)?;
        }
        self.bbox = Some(match self.bbox {
            None => (p.x, p.y, p.x, p.y),
            Some((a, b, c, d)) => (a.min(p.x), b.min(p.y), c.max(p.x), d.max(p.y)),
        });
        Ok(p)
    }

    fn point(&mut self, raw: Point) -> Result<u16> {
        let p = self.place(raw)?;
        let index = self.points;
        *self.output.points.get_mut(index).ok_or(Error::Limit)? = p;
        self.points += 1;
        Ok(index as u16)
    }

    fn move_to(&mut self, p: Point) -> Result<()> {
        if self.current.is_some() {
            return Err(Error::Invalid);
        }
        let index = self.point(p)?;
        self.current = Some(index);
        self.first_edge = self.edges;
        Ok(())
    }

    fn line_to(&mut self, p: Point) -> Result<()> {
        self.edge(None, p)
    }

    fn quad_to(&mut self, control: Point, p: Point) -> Result<()> {
        self.edge(Some(control), p)
    }

    fn edge(&mut self, control: Option<Point>, p: Point) -> Result<()> {
        let from = self.current.ok_or(Error::Invalid)?;
        let control = match control {
            Some(c) => Some(self.point(c)?),
            None => None,
        };
        let to = self.point(p)?;
        *self.output.edges.get_mut(self.edges).ok_or(Error::Limit)? = Edge { from, to, control };
        self.edges += 1;
        self.current = Some(to);
        Ok(())
    }

    fn close(&mut self) -> Result<()> {
        *self.output.contours.get_mut(self.contours).ok_or(Error::Limit)? = Contour {
            first_edge: self.first_edge as u16,
            end_edge: self.edges as u16,
        };
        self.contours += 1;
        self.first_edge = self.edges;
        self.current = None;
        Ok(())
    }
}
