//! T7b-b3 glyph cache (glyph cache design ⑥, b3/b4 design b4⑥) with the
//! TASK_FONTPERF F2 packed cells (fontperf design §3, §7 ①): capacity of the
//! 64/128KiB layouts, the cell fit boundaries and the cell copy, the clock
//! (second chance, fill first, G-1 reset on invalidate, G-2 commit unreferenced,
//! the amortised probe bound when full), hash-chain unlink, class independence,
//! setting generation, fit limits of the big classes and reuse.
use os32_font::{
    cache::{bucket_of, Cell, Entry, FaceId, GlyphCache, GlyphKey, Kind, SlotHdr},
    limits::*,
};

fn arena(bytes: usize) -> &'static mut [u8] {
    Box::leak(vec![0u8; bytes].into_boxed_slice())
}
fn cache(n_cell: usize, n16: usize, n32: usize) -> GlyphCache {
    let mut c = GlyphCache::new();
    c.attach(arena(GlyphCache::arena_bytes(n_cell, n16, n32)), n_cell, n16, n32).unwrap();
    c
}
fn standard(arena_bytes: usize) -> GlyphCache {
    let (n_cell, n16) = layout(arena_bytes);
    let mut c = GlyphCache::new();
    c.attach(arena(arena_bytes), n_cell, n16, N32).unwrap();
    c
}
fn key(cp: u32) -> GlyphKey {
    GlyphKey { set: 1, cp, px: 16 }
}
fn bitmap(face: FaceId, left: i16, top: i16, w: u16, h: u16) -> Entry {
    Entry {
        kind: Kind::Bitmap,
        face,
        left,
        top,
        width: w,
        height: h,
        pitch: w.div_ceil(8),
    }
}
/// A ja glyph that fits the full cell (rows 1..11).
fn small() -> Entry {
    bitmap(FaceId::Ja, 1, 13, 8, 10)
}
/// A 16px glyph that only fits the Big16 slot.
fn wide() -> Entry {
    bitmap(FaceId::Ja, 1, 13, 20, 21)
}
fn pattern(e: &Entry, seed: u8) -> Vec<u8> {
    (0..e.bytes()).map(|i| (i as u8).wrapping_mul(37) ^ seed).collect()
}
fn put(c: &mut GlyphCache, k: GlyphKey) -> u16 {
    let e = small();
    c.commit(k, &e, &pattern(&e, k.cp as u8), Cell::Full).unwrap()
}
fn put_big(c: &mut GlyphCache, k: GlyphKey) -> u16 {
    let e = wide();
    c.commit(k, &e, &pattern(&e, k.cp as u8), Cell::Full).unwrap()
}
/// Pixel-by-pixel placement of a bitmap in a cell (the drawing contract of
/// gshell text.rs / libos32gui draw.rs: column `left`, row `ascent - top`).
fn placed(e: &Entry, bits: &[u8], cw: usize, ascent: i16) -> Vec<u8> {
    let cp = cw / 8;
    let mut out = vec![0u8; cp * CELL_H_PX];
    let pitch = usize::from(e.pitch);
    for r in 0..usize::from(e.height) {
        for x in 0..usize::from(e.width) {
            if bits[r * pitch + x / 8] & (0x80 >> (x % 8)) != 0 {
                let (px, py) = (e.left as usize + x, (ascent - e.top) as usize + r);
                out[py * cp + px / 8] |= 0x80 >> (px % 8);
            }
        }
    }
    out
}

#[test]
fn layout_and_budget() {
    assert_eq!(std::mem::size_of::<SlotHdr>(), 32);
    assert_eq!((CELL_SLOT_BYTES, SLOT16_BYTES, SLOT32_BYTES), (40, 96, 224));
    assert_eq!((N16, N32), (32, 0));
    assert_eq!(layout(64 * 1024), (1_561, 32));
    assert_eq!(layout(ARENA_BYTES_SHELL), (3_200, 32));
    assert_eq!(layout(ARENA_BYTES_APP), (3_200, 32));
    assert_eq!(layout(256 * 1024), (6_476, 32));
    assert_eq!(GlyphCache::arena_bytes(1_561, 32, 0), 65_512);
    assert_eq!(GlyphCache::arena_bytes(3_200, 32, 0), 131_072);
    // Static context (buckets, clock state, counters) within its budget.
    assert!(std::mem::size_of::<GlyphCache>() <= CACHE_STATIC_BYTES);
    println!("GlyphCache {} B, BUCKETS {BUCKETS}", std::mem::size_of::<GlyphCache>());
    let mut c = GlyphCache::new();
    assert!(!c.has_arena());
    let small_arena = c.attach(arena(GlyphCache::arena_bytes(2, 1, 0) - 1), 2, 1, 0).unwrap_err();
    assert_eq!(small_arena.len(), 2 * CELL_SLOT_BYTES + SLOT16_BYTES - 1, "rejected arena is handed back");
    let (n_cell, n16) = layout(ARENA_BYTES_SHELL);
    c.attach(arena(ARENA_BYTES_SHELL), n_cell, n16, N32).unwrap();
    assert_eq!(c.layout(), (3_200, 32, 0));
    assert!(c.attach(arena(ARENA_BYTES_SHELL), n_cell, n16, N32).is_err(), "second attach");
    let back = c.detach().unwrap();
    assert_eq!(back.len(), ARENA_BYTES_SHELL);
    assert_eq!((c.n_valid(), c.layout(), c.has_arena()), (0, (0, 0, 0), false));
    assert!(c.lookup(key(1)).is_none());
    let e = small();
    assert_eq!(c.commit(key(1), &e, &pattern(&e, 0), Cell::Full), Err(os32_font::reader::Error::Limit));
}

#[test]
fn capacity_64k_and_128k() {
    for (bytes, want) in [(64 * 1024, 1_561usize), (ARENA_BYTES_SHELL, 3_200)] {
        let mut c = standard(bytes);
        for cp in 0..want as u32 {
            assert!((put(&mut c, key(0x4e00 + cp)) as usize) < want, "cells first");
        }
        assert_eq!((c.n_valid(), c.stats().evict), (want, 0), "{bytes}: {want} glyphs without eviction");
        for cp in 0..want as u32 {
            assert!(c.contains(key(0x4e00 + cp)));
        }
        // The big class keeps its own slots beside the full cells.
        let s = put_big(&mut c, key(0x10_0000));
        assert_eq!((s as usize, c.stats().evict), (want, 0));
        put(&mut c, key(0x10_0001));
        assert_eq!((c.n_valid(), c.stats().evict), (want + 1, 1));
    }
}

#[test]
fn cell_fit_boundaries() {
    let mut c = cache(32, 32, 0);
    let asc_ja = ASCENT_JA_PX;
    let asc_la = ASCENT_LATIN_PX;
    let cases: [(Entry, Cell, bool, &str); 13] = [
        (bitmap(FaceId::Ja, 0, asc_ja, 16, 16), Cell::Full, true, "full 16x16 at the ascent"),
        (bitmap(FaceId::Ja, -1, asc_ja, 15, 16), Cell::Full, false, "left = -1"),
        (bitmap(FaceId::Ja, 1, asc_ja, 16, 10), Cell::Full, false, "left + width = 17"),
        (bitmap(FaceId::Ja, 1, asc_ja, 15, 10), Cell::Full, true, "left + width = 16"),
        (bitmap(FaceId::Ja, 0, asc_ja + 1, 8, 4), Cell::Full, false, "above row 0"),
        (bitmap(FaceId::Ja, 0, asc_ja - 2, 8, 14), Cell::Full, true, "last row is row 15"),
        (bitmap(FaceId::Ja, 0, asc_ja - 2, 8, 15), Cell::Full, false, "16th row below the cell"),
        (bitmap(FaceId::Latin, 0, asc_la, 8, 16), Cell::Half, true, "half 8x16"),
        (bitmap(FaceId::Latin, 1, asc_la, 8, 16), Cell::Half, false, "half 9th column"),
        (bitmap(FaceId::Latin, 0, asc_la, 9, 16), Cell::Half, false, "half width 9"),
        (bitmap(FaceId::Latin, 0, asc_la, 8, 16), Cell::Full, true, "latin in a full cell"),
        (bitmap(FaceId::Latin, 0, asc_la + 1, 8, 4), Cell::Half, false, "latin ascent 13"),
        (bitmap(FaceId::None, 0, asc_ja, 8, 8), Cell::Full, false, "no face, no ascent"),
    ];
    for (i, (e, cell, fits, what)) in cases.iter().enumerate() {
        let slot = c.commit(key(i as u32), e, &pattern(e, 0), *cell).unwrap();
        assert_eq!((slot as usize) < 32, *fits, "{what}");
        let got = c.lookup(key(i as u32)).unwrap().1;
        if *fits {
            let cw = cell.width() as u16;
            let asc = if e.face == FaceId::Latin { asc_la } else { asc_ja };
            assert_eq!(got, Entry { left: 0, top: asc, width: cw, height: 16, pitch: cw / 8, ..*e }, "{what}");
        } else {
            assert_eq!(got, *e, "{what}: the big slot keeps the placement");
        }
    }
    // Space/Kcg/Missing at 16px take a cell, 0 B, face None.
    for (cp, kind) in [(100, Kind::Space), (101, Kind::Kcg), (102, Kind::Missing)] {
        let e = Entry { kind, face: FaceId::Latin, left: 1, top: 9, ..Entry::default() };
        let slot = c.commit(key(cp), &e, &[], Cell::Half).unwrap();
        assert!((slot as usize) < 32);
        assert_eq!(c.lookup(key(cp)).unwrap().1, Entry { kind, ..Entry::default() });
        assert_eq!(c.bits(slot).unwrap(), &[] as &[u8]);
    }
    // 32px keys never take a cell (no Big32 here: uncached).
    let e = bitmap(FaceId::Ja, 0, asc_ja, 8, 8);
    assert!(c.commit(GlyphKey { px: 32, ..key(200) }, &e, &pattern(&e, 0), Cell::Full).is_err());
    let space = Entry { kind: Kind::Space, ..Entry::default() };
    assert!(c.commit(GlyphKey { px: 32, ..key(201) }, &space, &[], Cell::Full).is_err());
}

#[test]
fn cell_copy_matches_placement() {
    let mut c = cache(8, 0, 0);
    let cases = [
        (bitmap(FaceId::Ja, 3, 12, 11, 9), Cell::Full, ASCENT_JA_PX),
        (bitmap(FaceId::Ja, 0, ASCENT_JA_PX, 16, 16), Cell::Full, ASCENT_JA_PX),
        (bitmap(FaceId::Ja, 7, 3, 9, 5), Cell::Full, ASCENT_JA_PX),
        (bitmap(FaceId::Latin, 1, 10, 6, 13), Cell::Half, ASCENT_LATIN_PX),
        (bitmap(FaceId::Latin, 0, ASCENT_LATIN_PX, 8, 16), Cell::Half, ASCENT_LATIN_PX),
        (bitmap(FaceId::Latin, 5, 2, 3, 2), Cell::Full, ASCENT_LATIN_PX),
    ];
    for (i, (e, cell, asc)) in cases.iter().enumerate() {
        // Pattern bytes also set the unused bits past the width: they must not leak.
        let bits = pattern(e, 0xa5 ^ i as u8);
        let slot = c.commit(key(i as u32), e, &bits, *cell).unwrap();
        let want = placed(e, &bits, cell.width(), *asc);
        assert_eq!(c.bits(slot).unwrap(), &want[..], "case {i}");
        let (hit, got) = c.lookup(key(i as u32)).unwrap();
        assert_eq!((hit, c.entry(slot)), (slot, Some(got)));
        assert_eq!(placed(&got, c.bits(slot).unwrap(), cell.width(), *asc), want, "drawn the same as uncached");
    }
}

#[test]
fn big_bits_round_trip() {
    let mut c = cache(4, 4, 0);
    let e = wide();
    let bits = pattern(&e, 0x5a);
    let slot = c.commit(key(0x8b70), &e, &bits, Cell::Full).unwrap();
    assert!(slot >= 4, "Big16");
    let (hit, got) = c.lookup(key(0x8b70)).unwrap();
    assert_eq!((hit, got), (slot, e));
    assert_eq!(c.bits(slot).unwrap(), &bits[..]);
}

#[test]
fn clock_second_chance() {
    // Fill A, B, C; A is used; D passes A (bit cleared) and takes B.
    let mut c = cache(3, 0, 0);
    let [a, b, cc, d, e] = ['A', 'B', 'C', 'D', 'E'].map(|x| key(x as u32));
    assert_eq!([put(&mut c, a), put(&mut c, b), put(&mut c, cc)], [0, 1, 2], "free slots in order");
    assert!(c.lookup(a).is_some());
    assert_eq!(put(&mut c, d), 1);
    assert!(c.contains(a) && c.contains(cc) && c.contains(d) && !c.contains(b));
    // A's bit was cleared by the pass: the sweep from the hand (slot 2) takes C, then A.
    assert_eq!(put(&mut c, e), 2);
    assert_eq!(put(&mut c, b), 0);
    assert!(!c.contains(a) && c.contains(d) && c.contains(e) && c.contains(b));
    assert_eq!(c.stats().evict, 3);
}

#[test]
fn clock_on_big_slots() {
    let mut c = cache(0, 2, 0);
    let [a, b, d] = ['A', 'B', 'D'].map(|x| key(x as u32));
    put_big(&mut c, a);
    put_big(&mut c, b);
    assert!(c.lookup(a).is_some());
    put_big(&mut c, d);
    assert!(c.contains(a) && c.contains(d) && !c.contains(b));
}

#[test]
fn commit_leaves_reference_clear() {
    // G-2: preloaded but unused glyphs go first.
    let mut c = cache(3, 0, 0);
    for cp in 1..=3 {
        put(&mut c, key(cp));
    }
    assert!(c.lookup(key(2)).is_some());
    assert_eq!(put(&mut c, key(4)), 0, "unreferenced slot 0 at the hand");
    assert_eq!(put(&mut c, key(5)), 2, "slot 1 passed once (referenced), 3 was committed unreferenced");
    assert!(c.contains(key(2)) && !c.contains(key(3)));
}

#[test]
fn invalidate_resets_fill_and_hand() {
    // G-1: fill = hand = 0 and every bit 0; the first commit after it is slot 0.
    let mut c = cache(3, 0, 0);
    for cp in 1..=4 {
        put(&mut c, key(cp)); // 4 takes slot 0, hand -> 1
    }
    for cp in 2..=4 {
        assert!(c.lookup(key(cp)).is_some());
    }
    c.invalidate_all();
    assert_eq!(c.n_valid(), 0);
    assert!(!c.contains(key(2)) && !c.contains(key(4)));
    assert_eq!([put(&mut c, key(10)), put(&mut c, key(11)), put(&mut c, key(12))], [0, 1, 2]);
    assert_eq!(put(&mut c, key(13)), 0, "hand back at slot 0, no stale reference bit");
    assert_eq!(c.stats().evict, 2);
}

#[test]
fn full_commit_probe_bound() {
    // §7 ①: at most n_cell + 2 x commit probes for 1,000 commits into a full
    // 128KiB arena whose every cell is referenced (the worst case).
    let mut c = standard(ARENA_BYTES_SHELL);
    let (n_cell, _, _) = c.layout();
    for cp in 0..n_cell as u32 {
        put(&mut c, key(0x4e00 + cp));
    }
    for cp in 0..n_cell as u32 {
        assert!(c.lookup(key(0x4e00 + cp)).is_some());
    }
    let before = c.clock_probes();
    let t = std::time::Instant::now();
    let commits = 1_000u32;
    for cp in 0..commits {
        put(&mut c, key(0x2_0000 + cp));
        // Some of the new glyphs are used again.
        if cp % 3 == 0 {
            assert!(c.lookup(key(0x2_0000 + cp)).is_some());
        }
    }
    let probes = c.clock_probes() - before;
    let per = t.elapsed() / commits;
    println!("full 128KiB: {commits} commits, {probes} probes (bound {}), host {per:?}/commit", n_cell as u32 + 2 * commits);
    assert!(probes <= n_cell as u32 + 2 * commits, "{probes} probes");
    assert_eq!(c.n_valid(), n_cell);
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
    let mut c = cache(4, 0, 0);
    // Slots 0..2 = k0, k1, k2 (chain k2 -> k1 -> k0), slot 3 = other.
    for x in &k[..3] {
        put(&mut c, *x);
    }
    put(&mut c, other);
    // k0 referenced: the clock passes slot 0 and takes slot 1 (the middle, k1).
    assert!(c.lookup(k[0]).is_some());
    assert_eq!(put(&mut c, k[3]), 1);
    assert!(!c.contains(k[1]), "middle unlinked");
    assert!(c.contains(k[0]) && c.contains(k[2]) && c.contains(k[3]));
    // Chain k3 -> k2 -> k0, hand at slot 2: pass 2, 3, 0 and take slot 1 (the head, k3).
    for x in [k[2], other, k[0]] {
        assert!(c.lookup(x).is_some());
    }
    assert_eq!(put(&mut c, far), 1);
    assert!(!c.contains(k[3]), "head unlinked");
    assert!(c.contains(k[2]) && c.contains(far) && c.contains(other) && c.contains(k[0]));
    // Chain k2 -> k0, hand at slot 2: pass 2, 3 and take slot 0 (the tail, k0).
    for x in [k[2], other] {
        assert!(c.lookup(x).is_some());
    }
    assert_eq!(put(&mut c, k[4]), 0);
    assert!(!c.contains(k[0]), "tail unlinked");
    assert!(c.contains(k[2]) && c.contains(k[4]) && c.contains(far) && c.contains(other));
    assert_eq!(c.n_valid(), 4);
}

#[test]
fn classes_independent() {
    let mut c = cache(1, 1, 1);
    let k32 = GlyphKey { px: 32, ..key('A' as u32) };
    let e32 = bitmap(FaceId::Ja, 1, 13, 36, 37);
    let s32 = c.commit(k32, &e32, &pattern(&e32, 1), Cell::Full).unwrap();
    let sbig = put_big(&mut c, key('W' as u32));
    put(&mut c, key('A' as u32));
    put(&mut c, key('B' as u32));
    assert!(!c.contains(key('A' as u32)));
    assert!(c.contains(key('B' as u32)) && c.contains(key('W' as u32)));
    assert_eq!((s32, sbig), (2, 1));
    assert_eq!(c.lookup(k32).unwrap().0, s32);
    assert_eq!(c.bits(s32).unwrap(), &pattern(&e32, 1)[..]);
    // Other pixel sizes have no class.
    let e = small();
    assert!(c.commit(GlyphKey { px: 24, ..key(1) }, &e, &pattern(&e, 0), Cell::Full).is_err());
}

#[test]
fn fit_limits_per_glyph() {
    use os32_font::reader::Error::Limit;
    let mut c = cache(2, 2, 1);
    for (w, h) in [(21, 10), (8, 22)] {
        let e = bitmap(FaceId::Ja, 1, 13, w, h);
        assert_eq!(c.commit(key(1), &e, &pattern(&e, 0), Cell::Full), Err(Limit), "{w}x{h}");
    }
    let e = bitmap(FaceId::Ja, 1, 13, 37, 10);
    assert_eq!(c.commit(GlyphKey { px: 32, ..key(1) }, &e, &pattern(&e, 0), Cell::Full), Err(Limit));
    let bad_pitch = Entry { pitch: 1, ..bitmap(FaceId::Ja, 0, 14, 16, 16) };
    assert_eq!(c.commit(key(1), &bad_pitch, &[0; 16], Cell::Full), Err(Limit));
    assert_eq!(c.n_valid(), 0, "rejected glyphs leave no entry");
    // Classes without slots.
    let mut none = cache(1, 0, 0);
    let e = small();
    assert_eq!(none.commit(GlyphKey { px: 32, ..key(1) }, &e, &pattern(&e, 0), Cell::Full), Err(Limit));
    let w = wide();
    assert_eq!(none.commit(key(1), &w, &pattern(&w, 0), Cell::Full), Err(Limit), "no Big16");
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
    let mut c = cache(4, 0, 0);
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
    let mut c = cache(2, 0, 0);
    put(&mut c, key(1));
    put(&mut c, key(2));
    c.invalidate_all();
    let e = bitmap(FaceId::Ja, 0, 14, 16, 16);
    let s = c.commit(key(3), &e, &pattern(&e, 9), Cell::Full).unwrap();
    assert_eq!(s, 0);
    assert_eq!(c.bits(s).unwrap(), &pattern(&e, 9)[..]);
    assert!(c.commit(key(3), &e, &pattern(&e, 9), Cell::Full).is_err(), "no duplicate key");
    assert_eq!(c.n_valid(), 1);
}
