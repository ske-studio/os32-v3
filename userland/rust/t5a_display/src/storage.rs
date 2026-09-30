use crate::state::CAPACITY;
use core::{
    cell::UnsafeCell,
    mem::MaybeUninit,
    sync::atomic::{AtomicBool, Ordering},
};
use libos32term::model::{Cell, BLANK};
/// Static backing storage is lent at most once for the lifetime of the process.
pub struct Storage {
    taken: AtomicBool,
    cells: UnsafeCell<MaybeUninit<[Cell; CAPACITY]>>,
}
// SAFETY: the atomic claim grants one caller exclusive access; never released.
unsafe impl Sync for Storage {}
impl Storage {
    pub const fn new() -> Self {
        Self {
            taken: AtomicBool::new(false),
            cells: UnsafeCell::new(MaybeUninit::uninit()),
        }
    }
    pub fn take(&'static self) -> Option<&'static mut [Cell]> {
        if self.taken.swap(true, Ordering::AcqRel) {
            return None;
        }
        // SAFETY: static storage, unique atomic winner, no references existed before
        // initialization. Initialize each Cell in place; never form a stack array.
        unsafe {
            let ptr = (*self.cells.get()).as_mut_ptr().cast::<Cell>();
            for i in 0..CAPACITY {
                ptr.add(i).write(BLANK);
            }
            Some(core::slice::from_raw_parts_mut(ptr, CAPACITY))
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn one_owner_and_repeated_initialization_rejected() {
        static STORAGE: Storage = Storage::new();
        let first = STORAGE.take();
        assert!(first.is_some());
        let first = first.unwrap();
        assert_eq!(first.len(), CAPACITY);
        assert!(first.iter().all(|c| *c == BLANK));
        first[0] = Cell::Single('X');
        assert!(STORAGE.take().is_none());
        assert_eq!(first[0], Cell::Single('X'));
    }
}
