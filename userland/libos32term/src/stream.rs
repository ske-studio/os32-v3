use crate::model::{Error, Model, State};
use crate::utf8::Decoder;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Report {
    /// Input bytes accepted this call, including bytes producing pending output.
    pub consumed: usize,
    pub state: State,
    /// Error for this call. state.limit separately remembers the last scalar error.
    pub error: Option<Error>,
}

/// Streaming decoder and display model. No input or decoded output is allocated.
pub struct Terminal<'a> {
    model: Model<'a>,
    decoder: Decoder,
    pending: [Option<char>; 2],
}

impl<'a> Terminal<'a> {
    pub fn new(model: Model<'a>) -> Self {
        Self {
            model,
            decoder: Decoder::default(),
            pending: [None; 2],
        }
    }
    pub fn model(&self) -> &Model<'a> {
        &self.model
    }
    /// Explicit edits/repositioning for the caller, including recovery from Full.
    /// Does not alter the decoder or pending output ordering.
    pub fn model_mut(&mut self) -> &mut Model<'a> {
        &mut self.model
    }
    pub fn pending(&self) -> [Option<char>; 2] {
        self.pending
    }
    /// Flush a final incomplete prefix. Retry errors before sending another segment.
    pub fn finish(&mut self) -> Report {
        if let Err(error) = self.drain() {
            return self.report(0, Some(error));
        }
        self.pending = self.decoder.finish().0;
        let error = self.drain().err();
        self.report(0, error)
    }
    /// Drain old output first, then accept bytes until the first display error.
    /// Resume at bytes[report.consumed..]; an empty slice retries pending output.
    pub fn feed(&mut self, bytes: &[u8]) -> Report {
        if let Err(error) = self.drain() {
            return self.report(0, Some(error));
        }
        for (index, byte) in bytes.iter().enumerate() {
            self.pending = self.decoder.push(*byte).0;
            if let Err(error) = self.drain() {
                return self.report(index + 1, Some(error));
            }
        }
        self.report(bytes.len(), None)
    }

    fn drain(&mut self) -> Result<(), Error> {
        while let Some(ch) = self.pending[0] {
            self.model.write_char(ch)?;
            self.pending = [self.pending[1], None];
        }
        Ok(())
    }

    fn report(&self, consumed: usize, error: Option<Error>) -> Report {
        Report {
            consumed,
            state: self.model.state(),
            error,
        }
    }
}
