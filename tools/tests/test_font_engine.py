"""T7b-b3 S1: font engine host tests (b3/b4 design §2-1..§2-4, §5) and mutations.

Runs userland/lib/font/tests/engine.rs: cmap search order (half ASCII latin->ja, half Latin-1 latin
only, full ja only), fallback entries, complete-bitmap commit under injected Err/short
reads (cache design ③), warm hit == cold miss == after trim (④), borrowing
(b3⑤), the once-per-text() lease check and Stale, reopen order (b3②), the
all-zero Engine == Engine::new() (P2-10), and the per-face baseline measured on
the real Inconsolata Regular 3.000 / JF Dot Shinonome 16 (U1 (a): ascent 13/14,
standard corpus fits its slot).

TASK_FONTPERF F3: also runs userland/lib/font/tests/bundle.rs over a .g16 bundle the
product generators make from the same TTF (g16_fixture.py): resolve == TTF with one
read_at per record, the rejections, preload cp check, 8 failed reads close, 32px never
reads it, every close path drops its source (20 reopenings), preload counts."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

MUTATIONS = [
    # b3⑤: the borrow check.
    ('busy-unchecked', 'src/engine.rs', '    fn not_busy(&self) -> Result<(), EngineError> {\n        if self.busy {',
     '    fn not_busy(&self) -> Result<(), EngineError> {\n        if false {', 'borrowed_bitmap_blocks_release_paths'),
    # b3②: closing/reopening must invalidate every entry.
    ('close-keeps-entries', 'src/engine.rs', '        self.bundle.close();\n        self.cache.invalidate_all();',
     '        self.bundle.close();', 'reopen_closes_invalidates_then_opens'),
    ('stale-ignored', 'src/engine.rs', 'if latin.is_err() || ja.is_err() {', 'if false {', 'entry_check_once_and_stale'),
    ('full-falls-to-latin', 'src/engine.rs', 'Cell::Full => &[FaceId::Ja],',
     'Cell::Full => &[FaceId::Ja, FaceId::Latin],', 'search_order_half_latin_full_ja'),
    # T7b-b5: a half cell above ASCII must not take the ja face's full-width glyph.
    ('half-latin1-falls-to-ja', 'src/engine.rs', 'Cell::Half => &[FaceId::Latin],',
     'Cell::Half => &[FaceId::Latin, FaceId::Ja],', 'search_order_half_latin_full_ja'),
    ('fallback-not-committed', 'src/engine.rs', 'let slot = self.cache.commit(key, &entry, &[], cell).ok();',
     'let slot: Option<u16> = None;', 'fallback_entries_are_committed_and_hit'),
    # b4④ baseline: ascent 14 for Inconsolata cuts g/j/p/q/y.
    ('latin-ascent-14', 'src/limits.rs', 'pub const ASCENT_LATIN_PX: i16 = 13;',
     'pub const ASCENT_LATIN_PX: i16 = 14;', 'real_fonts_ascent_and_slot_fit'),
]

# TASK_FONTPERF F3 (fontperf design §7 ⓐ ⓑ): the bundle reader and the preload.
BUNDLE_MUTATIONS = [
    ('bundle-skip-head-check', 'src/bundle.rs', '            _ => return Err(Reject::Head),',
     '            _ => {}', 'rejected_bundles_drop_the_source_and_keep_the_ttf'),
    ('bundle-skip-index-crc', 'src/bundle.rs', 'if !crc != rank_crc {', 'if false {',
     'rejected_bundles_drop_the_source_and_keep_the_ttf'),
    ('bundle-px-any', 'src/engine.rs', 'if px == PixelSize::Px16 && cell == Cell::Full && self.bundle.is_ok() {',
     'if cell == Cell::Full && self.bundle.is_ok() {', 'px32_never_reads_the_bundle'),
    ('bundle-preload-cp-unchecked', 'src/bundle.rs', '                if self.lookup(cp).is_none() {',
     '                if false {', 'preload_skips_records_whose_cp_the_index_lacks'),
    ('bundle-errors-never-close', 'src/bundle.rs', 'if self.errs >= G16_ERR_CLOSE {', 'if false {',
     'eight_failed_reads_close_the_bundle'),
    ('bundle-kept-on-close', 'src/engine.rs', '        self.bundle.close();\n        self.cache.invalidate_all();',
     '        self.cache.invalidate_all();', 'every_close_path_drops_the_bundle_source'),
    ('bundle-rank-off-by-one', 'src/bundle.rs', '        n += (byte & ((1u8 << bit) - 1)).count_ones();',
     '        n += (byte & ((1u8 << bit) | ((1u8 << bit) - 1))).count_ones();',
     'bundle_resolves_like_the_ttf_with_one_read'),
    ('preload-off', 'src/bundle.rs', 'let total = (self.n_pre as usize).min(max);',
     'let total = (self.n_pre as usize).min(0);', 'preload_counts_and_hits'),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    # Existing consent/cache path; no missing-font SKIP.
    subprocess.run(['python3', str(ROOT / 'tools/fetch_fonts.py'), '--family', 'inconsolata'], check=True)
    from test_font_cache import run
    from test_font_outline import shinonome
    from g16_fixture import g16_fixture
    with tempfile.TemporaryDirectory(prefix='font-engine-jf-') as directory:
        jf = shinonome(Path(directory))
        env = {'OS32_FONT_SHINONOME': str(jf)}
        rc = run('engine', MUTATIONS, args.mutate, env)
        if rc:
            return rc
        env['OS32_FONT_G16'] = str(g16_fixture(jf, Path(directory) / 'g16'))
        return run('bundle', BUNDLE_MUTATIONS, args.mutate, env)


if __name__ == '__main__':
    raise SystemExit(main())
