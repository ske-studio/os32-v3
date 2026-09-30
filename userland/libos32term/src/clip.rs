#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ClipError {
    InvalidRect,
    InvalidGrid,
    Overflow,
}

/// Half-open pixel rectangle. Fields are validated at the API boundary.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Rect {
    pub x0: i64,
    pub y0: i64,
    pub x1: i64,
    pub y1: i64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Geometry for the visible viewport. Row indices returned by clip are local to it.
pub struct Grid {
    pub origin: (i64, i64),
    pub cols: usize,
    pub rows: usize,
    pub cell_width: i64,
    pub cell_height: i64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Clip {
    /// Strict pixel permission: never expand this when drawing a wide glyph.
    pub write: Rect,
    /// Conservative cell read range, with a one-column halo for wide pairs.
    pub columns: core::ops::Range<usize>,
    pub rows: core::ops::Range<usize>,
}

impl Grid {
    /// Validate grid extents, intersect pixels, then derive bounded cell read ranges.
    pub fn clip(self, requested: Rect) -> Result<Option<Clip>, ClipError> {
        if self.cols == 0 || self.rows == 0 || self.cell_width <= 0 || self.cell_height <= 0 {
            return Err(ClipError::InvalidGrid);
        }
        let width = i64::try_from(self.cols)
            .map_err(|_| ClipError::Overflow)?
            .checked_mul(self.cell_width)
            .ok_or(ClipError::Overflow)?;
        let height = i64::try_from(self.rows)
            .map_err(|_| ClipError::Overflow)?
            .checked_mul(self.cell_height)
            .ok_or(ClipError::Overflow)?;
        let bounds = Rect {
            x0: self.origin.0,
            y0: self.origin.1,
            x1: self
                .origin
                .0
                .checked_add(width)
                .ok_or(ClipError::Overflow)?,
            y1: self
                .origin
                .1
                .checked_add(height)
                .ok_or(ClipError::Overflow)?,
        };
        let Some(write) = bounds.intersection(requested)? else {
            return Ok(None);
        };
        // Intersection bounds these differences by the checked positive extents.
        let first_col = ((write.x0 - bounds.x0) / self.cell_width) as usize;
        let end_col = ((write.x1 - bounds.x0 - 1) / self.cell_width + 1) as usize;
        let first_row = ((write.y0 - bounds.y0) / self.cell_height) as usize;
        let end_row = ((write.y1 - bounds.y0 - 1) / self.cell_height + 1) as usize;
        Ok(Some(Clip {
            write,
            columns: first_col.saturating_sub(1)..if end_col < self.cols {
                end_col + 1
            } else {
                end_col
            },
            rows: first_row..end_row,
        }))
    }
}

impl Rect {
    pub fn intersection(self, other: Self) -> Result<Option<Self>, ClipError> {
        if self.x0 > self.x1 || self.y0 > self.y1 || other.x0 > other.x1 || other.y0 > other.y1 {
            return Err(ClipError::InvalidRect);
        }
        let result = Self {
            x0: self.x0.max(other.x0),
            y0: self.y0.max(other.y0),
            x1: self.x1.min(other.x1),
            y1: self.y1.min(other.y1),
        };
        Ok(if result.x0 >= result.x1 || result.y0 >= result.y1 {
            None
        } else {
            Some(result)
        })
    }
}
