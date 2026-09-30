/// At most two scalars: replacement for a prefix, then the reprocessed byte.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Decoded(pub [Option<char>; 2]);

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
/// Retains only a valid incomplete UTF-8 prefix between calls.
pub struct Decoder {
    bytes: [u8; 4],
    len: usize,
}

impl Decoder {
    /// Replace a remaining prefix once; the decoder can then accept a new segment.
    pub fn finish(&mut self) -> Decoded {
        let ch = if self.len != 0 {
            Some('\u{fffd}')
        } else {
            None
        };
        self.len = 0;
        Decoded([ch, None])
    }

    /// Always accepts one byte. Invalid prefixes use the README replacement units.
    pub fn push(&mut self, byte: u8) -> Decoded {
        let had_prefix = self.len != 0;
        self.bytes[self.len] = byte;
        self.len += 1;
        match core::str::from_utf8(&self.bytes[..self.len]) {
            Ok(text) => {
                let ch = text.chars().next();
                self.len = 0;
                Decoded([ch, None])
            }
            Err(error) => {
                if error.error_len().is_some() {
                    self.len = 0;
                    // Reprocessing occurs at most once: the recursive call has no prefix.
                    let next = if had_prefix {
                        self.push(byte).0[0]
                    } else {
                        None
                    };
                    return Decoded([Some('\u{fffd}'), next]);
                }
                Decoded::default()
            }
        }
    }
}
