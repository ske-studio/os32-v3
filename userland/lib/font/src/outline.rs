//! One-glyph streaming bridge to the unmodified ttf-parser public table API.
//! Coordinates remain unhinted font units. Edges are lines/quadratics; a5 owns
//! pixel scaling and curve subdivision. Call under the a2 x87 guard on OS32.
use crate::{
    limits::*,
    reader::{Error, Face, Result, Source},
};
use core::num::NonZeroU16;
use ttf_parser::{glyf, head::IndexToLocationFormat, loca, GlyphId, OutlineBuilder};

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    pub x: f32,
    pub y: f32,
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
/// An empty glyph succeeds with empty slices and no bbox (as does Face::outline).
pub struct Outline<'a> {
    pub points: &'a [Point],
    pub edges: &'a [Edge],
    pub contours: &'a [Contour],
    pub bounds: Option<ttf_parser::Rect>,
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
                points: [Point { x: 0.0, y: 0.0 }; MAX_OUTLINE_POINTS],
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
        let count = NonZeroU16::new(usage.glyphs as u16).ok_or(Error::Invalid)?;
        // Short (u16, offset/2) mini loca: ttf-parser's Long format reads u32,
        // which compiles to BSWAP (486+, D36). Offsets are kept even below.
        let locations = loca::Table::parse(
            count,
            IndexToLocationFormat::Short,
            self.loca
                .get(..(usage.glyphs + 1) * 2)
                .ok_or(Error::Bounds)?,
        )
        .ok_or(Error::Invalid)?;
        let table = glyf::Table::parse(
            locations,
            self.input.get(..usage.input_bytes).ok_or(Error::Bounds)?,
        )
        .ok_or(Error::Invalid)?;
        let mut builder = Builder {
            output: &mut self.output,
            points: 0,
            edges: 0,
            contours: 0,
            current: None,
            first_edge: 0,
            error: None,
        };
        let bounds = table.outline(GlyphId(0), &mut builder);
        if let Some(error) = builder.error {
            return Err(error);
        }
        // ttf-parser also returns None for empty outlines. All input syntax was
        // checked before it ran; any emitted but unfinished outline is an error.
        if builder.current.is_some() || (bounds.is_none() && builder.edges != 0) {
            return Err(Error::Invalid);
        }
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
        cursor.skip(8)?; // bbox, recalculated by ttf-parser
        if contours >= 0 {
            simple(&mut cursor, contours as usize, &mut self.usage)?;
        } else {
            // Point-number attachment and scaled offsets are not implemented by
            // the selected public parser. Explicitly reject rather than misplace.
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

struct Builder<'a> {
    output: &'a mut Output,
    points: usize,
    edges: usize,
    contours: usize,
    current: Option<u16>,
    first_edge: usize,
    error: Option<Error>,
}
impl Builder<'_> {
    fn point(&mut self, x: f32, y: f32) -> Result<u16> {
        let index = self.points;
        *self.output.points.get_mut(index).ok_or(Error::Limit)? = Point { x, y };
        self.points += 1;
        Ok(index as u16)
    }
    fn edge(&mut self, control: Option<Point>, x: f32, y: f32) -> Result<()> {
        let from = self.current.ok_or(Error::Invalid)?;
        let control = match control {
            Some(p) => Some(self.point(p.x, p.y)?),
            None => None,
        };
        let to = self.point(x, y)?;
        *self.output.edges.get_mut(self.edges).ok_or(Error::Limit)? = Edge { from, to, control };
        self.edges += 1;
        self.current = Some(to);
        Ok(())
    }
    fn record(&mut self, result: Result<()>) {
        if self.error.is_none() {
            self.error = result.err();
        }
    }
}
impl OutlineBuilder for Builder<'_> {
    fn move_to(&mut self, x: f32, y: f32) {
        if self.error.is_some() {
            return;
        }
        let result = if self.current.is_some() {
            Err(Error::Invalid)
        } else {
            self.point(x, y).map(|p| {
                self.current = Some(p);
                self.first_edge = self.edges;
            })
        };
        self.record(result);
    }
    fn line_to(&mut self, x: f32, y: f32) {
        if self.error.is_some() {
            return;
        }
        let result = self.edge(None, x, y);
        self.record(result);
    }
    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        if self.error.is_some() {
            return;
        }
        let result = self.edge(Some(Point { x: x1, y: y1 }), x, y);
        self.record(result);
    }
    fn curve_to(&mut self, _: f32, _: f32, _: f32, _: f32, _: f32, _: f32) {
        if self.error.is_none() {
            self.error = Some(Error::Unsupported);
        }
    }
    fn close(&mut self) {
        if self.error.is_some() {
            return;
        }
        let result = self
            .output
            .contours
            .get_mut(self.contours)
            .ok_or(Error::Limit)
            .map(|c| {
                *c = Contour {
                    first_edge: self.first_edge as u16,
                    end_edge: self.edges as u16,
                };
                self.contours += 1;
                self.first_edge = self.edges;
                self.current = None;
            });
        self.record(result);
    }
}
