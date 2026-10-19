//! T7b-b3 glyph cache (glyph cache design ⑥, b3/b4 design b4⑥): small layouts
//! for LRU by age, hash-chain unlink, class independence, invalid-slot
//! preference, age ties, age wrap, setting change, fit limits and reuse.
use os32_font::{
    cache::{bucket_of, Entry, FaceId, GlyphCache, GlyphKey, Kind, SlotHdr},
    limits::*,
};

fn arena(bytes: usize) -> &'static mut [u8] {
    Box::leak(vec![0u8; bytes].into_boxed_slice())
}
fn cache(n16: usize, n32: usize) -> GlyphCache {
    let mut c = GlyphCache::new();
    c.attach(arena(GlyphCache::arena_bytes(n16, n32)), n16, n32).unwrap();
    c
}
fn key(cp: u32) -> GlyphKey {
    GlyphKey { set: 1, cp, px: 16 }
}
fn bitmap(w: u16, h: u16) -> Entry {
    Entry {
        kind: Kind::Bitmap,
        face: FaceId::Ja,
        left: 1,
        top: 13,
        width: w,
        height: h,
        pitch: w.div_ceil(8),
    }
}
fn pattern(e: &Entry, seed: u8) -> Vec<u8> {
    (0..e.bytes()).map(|i| (i as u8).wrapping_mul(37) ^ seed).collect()
}
fn put(c: &mut GlyphCache, k: GlyphKey) -> u16 {
    let e = bitmap(8, 10);
    c.commit(k, &e, &pattern(&e, k.cp as u8)).unwrap()
}
fn has(c: &mut GlyphCache, k: GlyphKey) -> bool {
    c.lookup(k).is_some()
}

#[test]
fn layout_and_budget() {
    assert_eq!(std::mem::size_of::<SlotHdr>(), 32);
    assert_eq!((SLOT16_BYTES, SLOT32_BYTES), (96, 224));
    assert!(GlyphCache::arena_bytes(N16, N32) <= ARENA_BYTES);
    assert_eq!(N16 * SLOT16_BYTES, 65_472);
    // Static context <= 2KiB beside the arena (cache design §1, 68KiB total).
    assert!(std::mem::size_of::<GlyphCache>() <= CACHE_STATIC_BYTES);
    assert!(ARENA_BYTES + CACHE_STATIC_BYTES + 16 <= 68 * 1024);
    let mut c = GlyphCache::new();
    assert!(!c.has_arena());
    let small = c.attach(arena(GlyphCache::arena_bytes(2, 0) - 1), 2, 0).unwrap_err();
    assert_eq!(small.len(), SLOT16_BYTES * 2 - 1, "rejected arena is handed back");
    c.attach(arena(ARENA_BYTES), N16, N32).unwrap();
    assert_eq!(c.layout(), (N16, N32));
    assert!(c.attach(arena(ARENA_BYTES), N16, N32).is_err(), "second attach");
    for cp in 0..N16 as u32 {
        put(&mut c, key(cp));
    }
    assert_eq!(c.n_valid(), N16);
    assert_eq!(c.stats().evict, 0);
    put(&mut c, key(100_000));
    assert_eq!((c.n_valid(), c.stats().evict), (N16, 1));
    let back = c.detach().unwrap();
    assert_eq!(back.len(), ARENA_BYTES);
    assert_eq!((c.n_valid(), c.layout(), c.has_arena()), (0, (0, 0), false));
    assert!(c.lookup(key(1)).is_none());
    assert_eq!(c.commit(key(1), &bitmap(8, 10), &[0; 10]), Err(os32_font::reader::Error::Limit));
}

#[test]
fn bits_round_trip() {
    let mut c = cache(4, 0);
    let e = bitmap(20, 21);
    let bits = pattern(&e, 0x5a);
    let slot = c.commit(key(0x8b70), &e, &bits).unwrap();
    let (hit, got) = c.lookup(key(0x8b70)).unwrap();
    assert_eq!((hit, got), (slot, e));
    assert_eq!(c.bits(slot).unwrap(), &bits[..]);
}

#[test]
fn lru_a_b_a_c_evicts_b() {
    let mut c = cache(2, 0);
    put(&mut c, key('A' as u32));
    put(&mut c, key('B' as u32));
    assert!(has(&mut c, key('A' as u32)));
    put(&mut c, key('C' as u32));
    assert!(has(&mut c, key('A' as u32)));
    assert!(has(&mut c, key('C' as u32)));
    assert!(!has(&mut c, key('B' as u32)));
    assert_eq!(c.stats().evict, 1);
}

#[test]
fn invalid_slot_preferred() {
    let mut c = cache(3, 0);
    // Slots carry stale ages from before the invalidation.
    for cp in 1..=3 {
        put(&mut c, key(cp));
    }
    for _ in 0..5 {
        assert!(has(&mut c, key(3)));
    }
    c.invalidate_all();
    assert_eq!(c.n_valid(), 0);
    assert_eq!(put(&mut c, key(10)), 0);
    assert_eq!(put(&mut c, key(11)), 1);
    assert_eq!(put(&mut c, key(12)), 2, "invalid slot before the smallest age");
    assert!(has(&mut c, key(10)) && has(&mut c, key(11)) && has(&mut c, key(12)));
    assert_eq!(c.stats().evict, 0);
}

#[test]
fn age_tie_lowest_slot() {
    let mut c = cache(3, 0);
    put(&mut c, key(1)); // slot 0, age 1
    put(&mut c, key(2)); // slot 1, age 2
    c.set_age_for_test(0);
    assert_eq!(put(&mut c, key(3)), 2); // invalid slot 2, age 1
    put(&mut c, key(4));
    assert!(!has(&mut c, key(1)), "tie at age 1: lower slot evicted");
    assert!(has(&mut c, key(3)));
}

/// Keys sharing one bucket (head insertion: newest first in the chain).
fn colliding(n: usize) -> Vec<GlyphKey> {
    let b = bucket_of(key(0x4e00));
    let v: Vec<_> = (0x4e00..0x10_0000).map(key).filter(|k| bucket_of(*k) == b).take(n).collect();
    assert_eq!(v.len(), n);
    v
}

#[test]
fn collision_unlink_middle_head_tail() {
    let k = colliding(5);
    let free: Vec<GlyphKey> = (1..).map(key).filter(|x| bucket_of(*x) != bucket_of(k[0])).take(2).collect();
    let (other, far) = (free[0], free[1]);
    let mut c = cache(4, 0);
    for x in &k[..3] {
        put(&mut c, *x);
    }
    put(&mut c, other);
    // Chain k2 -> k1 -> k0; make the middle (k1) the oldest.
    assert!(has(&mut c, k[0]) && has(&mut c, k[2]) && has(&mut c, other));
    put(&mut c, k[3]);
    assert!(!has(&mut c, k[1]), "middle unlinked");
    assert!(has(&mut c, k[0]) && has(&mut c, k[2]));
    // Chain k3 -> k2 -> k0; make the head (k3) the oldest.
    assert!(has(&mut c, other));
    put(&mut c, far);
    assert!(!has(&mut c, k[3]), "head unlinked");
    assert!(has(&mut c, k[2]) && has(&mut c, far) && has(&mut c, other));
    // Chain k2 -> k0; make the tail (k0) the oldest.
    put(&mut c, k[4]);
    assert!(!has(&mut c, k[0]), "tail unlinked");
    assert!(has(&mut c, k[2]) && has(&mut c, k[4]) && has(&mut c, far) && has(&mut c, other));
    assert_eq!(c.n_valid(), 4);
}

#[test]
fn classes_independent() {
    let mut c = cache(1, 1);
    let k32 = GlyphKey { px: 32, ..key('A' as u32) };
    let e32 = bitmap(36, 37);
    let s32 = c.commit(k32, &e32, &pattern(&e32, 1)).unwrap();
    put(&mut c, key('A' as u32));
    put(&mut c, key('B' as u32));
    assert!(!has(&mut c, key('A' as u32)));
    assert!(has(&mut c, key('B' as u32)));
    assert_eq!(c.lookup(k32).unwrap().0, s32);
    assert_eq!(c.bits(s32).unwrap(), &pattern(&e32, 1)[..]);
    // Other pixel sizes have no class.
    assert!(c.commit(GlyphKey { px: 24, ..key(1) }, &bitmap(8, 8), &[0; 8]).is_err());
}

#[test]
fn fit_limits_per_glyph() {
    use os32_font::reader::Error::Limit;
    let mut c = cache(2, 1);
    for (w, h) in [(21, 10), (8, 22)] {
        let e = bitmap(w, h);
        assert_eq!(c.commit(key(1), &e, &pattern(&e, 0)), Err(Limit), "{w}x{h}");
    }
    let e = bitmap(37, 10);
    assert_eq!(c.commit(GlyphKey { px: 32, ..key(1) }, &e, &pattern(&e, 0)), Err(Limit));
    let bad_pitch = Entry { pitch: 1, ..bitmap(16, 16) };
    assert_eq!(c.commit(key(1), &bad_pitch, &[0; 16]), Err(Limit));
    assert_eq!(c.n_valid(), 0, "rejected glyphs leave no entry");
    // Class without slots.
    let none = cache(1, 0);
    let mut none = none;
    let e = bitmap(8, 8);
    assert_eq!(none.commit(GlyphKey { px: 32, ..key(1) }, &e, &pattern(&e, 0)), Err(Limit));
    // Fallback kinds are 0-byte entries, distinct from an inkless Space.
    for (cp, kind) in [(2, Kind::Kcg), (3, Kind::Missing), (4, Kind::Space)] {
        let e = Entry { kind, ..Entry::default() };
        let slot = c.commit(key(cp), &e, &[]).unwrap();
        assert_eq!(c.lookup(key(cp)).unwrap().1.kind, kind);
        assert_eq!(c.bits(slot).unwrap(), &[] as &[u8]);
    }
}

#[test]
fn age_wrap_invalidates_all() {
    let mut c = cache(2, 0);
    put(&mut c, key(1));
    c.set_age_for_test(u32::MAX - 1);
    put(&mut c, key(2)); // age MAX
    assert!(c.lookup(key(2)).is_none(), "hit candidate becomes a miss at the wrap");
    assert_eq!((c.n_valid(), c.stats().age_wrap), (0, 1));
    assert!(c.age() <= 1);
    assert!(c.lookup(key(1)).is_none());
    put(&mut c, key(2));
    assert!(has(&mut c, key(2)));
}

#[test]
fn setting_change_old_entries_never_hit() {
    // b4⑥: a key of the previous set generation is not drawn, even when both
    // generations share one bucket.
    let k = key(0x4e9c);
    let newer = (2..=u16::MAX)
        .map(|set| GlyphKey { set, ..k })
        .find(|x| bucket_of(*x) == bucket_of(k))
        .unwrap();
    let mut c = cache(4, 0);
    put(&mut c, k);
    assert!(c.lookup(newer).is_none());
    assert!(!c.contains(newer));
    put(&mut c, newer);
    assert_eq!(c.n_valid(), 2);
    c.invalidate_all();
    assert!(c.lookup(k).is_none() && c.lookup(newer).is_none());
}

#[test]
fn reuse_after_invalidate() {
    let mut c = cache(2, 0);
    put(&mut c, key(1));
    put(&mut c, key(2));
    c.invalidate_all();
    let e = bitmap(16, 16);
    let s = c.commit(key(3), &e, &pattern(&e, 9)).unwrap();
    assert_eq!(s, 0);
    assert_eq!(c.bits(s).unwrap(), &pattern(&e, 9)[..]);
    assert!(c.commit(key(3), &e, &pattern(&e, 9)).is_err(), "no duplicate key");
    assert_eq!(c.n_valid(), 1);
}
