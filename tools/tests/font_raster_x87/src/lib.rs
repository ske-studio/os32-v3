//! Host fixture (not product): render a codepoint list with the OS32-target
//! code generation (x87 f32, CW from the a2 guard) and emit binary records.
#![no_std]
use os32_font::{
    limits::MAX_BITMAP_BYTES,
    raster::{PixelSize, RasterScratch},
    reader::{Error, Face, Result, Source},
};

#[repr(C)]
pub struct Probe {
    font: *const u8,
    font_len: u32,
    cps: *const u32,
    count: u32,
    out: *mut u8,
    cap: u32,
    len: u32,
}

struct Memory(&'static [u8]);
impl Source for Memory {
    fn file_size(&self) -> u64 {
        self.0.len() as u64
    }
    fn read_at(&mut self, offset: u64, dst: &mut [u8]) -> Result<usize> {
        let start = usize::try_from(offset).map_err(|_| Error::Bounds)?;
        let end = start.checked_add(dst.len()).ok_or(Error::Bounds)?;
        dst.copy_from_slice(self.0.get(start..end).ok_or(Error::Bounds)?);
        Ok(dst.len())
    }
}

static mut SCRATCH: RasterScratch = RasterScratch::new();
static mut BITS: [u8; MAX_BITMAP_BYTES] = [0; MAX_BITMAP_BYTES];

struct Out<'a> {
    buf: &'a mut [u8],
    at: usize,
}
impl Out<'_> {
    fn put(&mut self, bytes: &[u8]) -> Result<()> {
        let end = self.at + bytes.len();
        self.buf.get_mut(self.at..end).ok_or(Error::Limit)?.copy_from_slice(bytes);
        self.at = end;
        Ok(())
    }
}

fn run(p: &mut Probe) -> Result<()> {
    // SAFETY: the C harness passes valid, non-overlapping buffers.
    let font = unsafe { core::slice::from_raw_parts(p.font, p.font_len as usize) };
    let cps = unsafe { core::slice::from_raw_parts(p.cps, p.count as usize) };
    let buf = unsafe { core::slice::from_raw_parts_mut(p.out, p.cap as usize) };
    let work = unsafe { &mut *core::ptr::addr_of_mut!(SCRATCH) };
    let bits = unsafe { &mut *core::ptr::addr_of_mut!(BITS) };
    let mut out = Out { buf, at: 0 };
    let mut face = Face::open(Memory(font))?;
    for &cp in cps {
        let gid = face.glyph_index(cp)?.unwrap_or(0);
        out.put(&cp.to_le_bytes())?;
        out.put(&gid.to_le_bytes())?;
        match work.render(&mut face, gid, PixelSize::Px16, bits) {
            Ok(q) => {
                out.put(&0i32.to_le_bytes())?;
                for v in [q.width, q.height, q.pitch] {
                    out.put(&v.to_le_bytes())?;
                }
                out.put(&q.left.to_le_bytes())?;
                out.put(&q.top.to_le_bytes())?;
                out.put(&q.advance.to_le_bytes())?;
                out.put(bits.get(..q.bytes()).ok_or(Error::Bounds)?)?;
                for &c in work.coverage() {
                    out.put(&[(c * 255.0 + 0.5) as u8])?;
                }
            }
            Err(e) => out.put(&(1 + e as i32).to_le_bytes())?,
        }
    }
    p.len = out.at as u32;
    Ok(())
}

/// font_guard_run callback: 0 or 1000 + Error discriminant.
#[no_mangle]
pub extern "C" fn font_raster_probe(context: *mut core::ffi::c_void) -> i32 {
    // SAFETY: context is the harness's Probe.
    let p = unsafe { &mut *(context as *mut Probe) };
    match run(p) {
        Ok(()) => 0,
        Err(e) => 1000 + e as i32,
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
