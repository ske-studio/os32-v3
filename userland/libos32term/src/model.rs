#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidDimensions,
    Overflow,
    InsufficientStorage,
    Full,
    TooWide,
    OutOfBounds,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Width {
    Single,
    Wide,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Blank cells are Single(' '); continuations never carry a separate scalar.
pub enum Cell {
    Single(char),
    Wide(char),
    Continuation,
}

pub const BLANK: Cell = Cell::Single(' ');

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct State {
    /// (column, saved row). Column == cols denotes a pending wrap.
    pub cursor: (usize, usize),
    /// Initialized history, including empty rows reached by LF.
    pub retained_rows: core::ops::Range<usize>,
    /// Last write_char error; reset on success except a TAB at pending wrap.
    pub limit: Option<Error>,
}

pub struct Model<'a> {
    cells: &'a mut [Cell],
    cols: usize,
    rows: usize,
    width: fn(char) -> Width,
    state: State,
}

impl<'a> Model<'a> {
    /// Validate before clearing exactly cols * rows cells; leave extra storage alone.
    pub fn new(
        cells: &'a mut [Cell],
        cols: usize,
        rows: usize,
        width: fn(char) -> Width,
    ) -> Result<Self, Error> {
        if cols == 0 || rows == 0 {
            return Err(Error::InvalidDimensions);
        }
        let size = cols.checked_mul(rows).ok_or(Error::Overflow)?;
        let cells = cells.get_mut(..size).ok_or(Error::InsufficientStorage)?;
        cells.fill(BLANK);
        Ok(Self {
            cells,
            cols,
            rows,
            width,
            state: State {
                cursor: (0, 0),
                retained_rows: 0..1,
                limit: None,
            },
        })
    }

    pub fn state(&self) -> State {
        self.state.clone()
    }
    /// Entire configured storage, including blank rows not yet in retained_rows.
    pub fn cells(&self) -> &[Cell] {
        self.cells
    }
    /// Row-major retained cells, available repeatedly without a dirty prerequisite.
    /// Empty views at the retained end are valid; other out-of-range views fail.
    pub fn viewport(&self, top: usize, height: usize) -> Result<&[Cell], Error> {
        let end = top.checked_add(height).ok_or(Error::Overflow)?;
        if end > self.state.retained_rows.end {
            return Err(Error::OutOfBounds);
        }
        Ok(&self.cells[top * self.cols..end * self.cols])
    }
    /// Last min(height, retained row count) rows; does not discard history.
    pub fn tail(&self, height: usize) -> Result<&[Cell], Error> {
        let height = height.min(self.state.retained_rows.end);
        self.viewport(self.state.retained_rows.end - height, height)
    }
    /// Explicit cell position within retained rows, including either wide half.
    /// Not a state restore: column == cols (pending wrap) cannot be set.
    /// Does not reset limit; on error the complete state is unchanged.
    pub fn set_cursor(&mut self, x: usize, y: usize) -> Result<(), Error> {
        if x >= self.cols || y >= self.state.retained_rows.end {
            return Err(Error::OutOfBounds);
        }
        self.state.cursor = (x, y);
        Ok(())
    }
    /// Apply one scalar/control atomically. Failure only changes state.limit.
    /// TAB at pending wrap succeeds without changing any state or cells.
    pub fn write_char(&mut self, ch: char) -> Result<(), Error> {
        if ch == '\t' && self.state.cursor.0 == self.cols {
            return Ok(());
        }
        let result = self.write_inner(ch);
        self.state.limit = result.err();
        result
    }

    fn write_inner(&mut self, ch: char) -> Result<(), Error> {
        if ch == '\t' {
            const TAB_STOP: usize = 4;
            let x = self.state.cursor.0;
            let count = (TAB_STOP - x % TAB_STOP).min(self.cols - x);
            for _ in 0..count {
                self.put(' ', 1)?;
            }
            return Ok(());
        }
        if ch == '\x08' {
            let (mut x, mut y) = self.state.cursor;
            if x > 0 {
                x -= 1;
            } else if y > 0 {
                // A line longer than cols continues on the next row (see put),
                // so column 0 is only the start of an edit when y == 0. Going
                // back to the previous row's last column lets a caller erase
                // across the wrap with the usual BS, space, BS without knowing
                // the width. The model keeps no wrap flag, so a BS at column 0
                // after an explicit newline lands there too; that sequence
                // erases nothing by itself and callers only backspace over what
                // they just wrote.
                y -= 1;
                x = self.cols - 1;
            }
            if x > 0 && self.cells[y * self.cols + x] == Cell::Continuation {
                x -= 1;
            }
            self.state.cursor = (x, y);
            return Ok(());
        }
        if ch == '\r' {
            self.state.cursor.0 = 0;
            return Ok(());
        }
        if ch == '\n' {
            let y = self.state.cursor.1;
            if y == self.rows - 1 {
                return Err(Error::Full);
            }
            self.state.cursor = (0, y + 1);
            self.state.retained_rows.end = self.state.retained_rows.end.max(y + 2);
            return Ok(());
        }
        let width = match (self.width)(ch) {
            Width::Single => 1,
            Width::Wide => 2,
        };
        self.put(ch, width)
    }

    fn put(&mut self, ch: char, width: usize) -> Result<(), Error> {
        if width > self.cols {
            return Err(Error::TooWide);
        }
        let (mut x, mut y) = self.state.cursor;
        if width > self.cols - x {
            if y == self.rows - 1 {
                return Err(Error::Full);
            }
            x = 0;
            y += 1;
        }
        let index = y * self.cols + x;
        self.clear_pair(index);
        if width == 2 {
            self.clear_pair(index + 1);
        }
        self.cells[index] = if width == 2 {
            Cell::Wide(ch)
        } else {
            Cell::Single(ch)
        };
        if width == 2 {
            self.cells[index + 1] = Cell::Continuation;
        }
        self.state.cursor = (x + width, y);
        self.state.retained_rows.end = self.state.retained_rows.end.max(y + 1);
        Ok(())
    }

    fn clear_pair(&mut self, index: usize) {
        match self.cells[index] {
            Cell::Wide(_) => self.cells[index + 1] = BLANK,
            Cell::Continuation => self.cells[index - 1] = BLANK,
            Cell::Single(_) => {}
        }
        self.cells[index] = BLANK;
    }
}
