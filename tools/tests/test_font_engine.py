"""T7b-b3 S1: font engine host tests (b3/b4 design §2-1..§2-4, §5) and mutations.

Runs userland/lib/font/tests/engine.rs: cmap search order (half ASCII latin->ja, half Latin-1 latin
only, full ja only), fallback entries, complete-bitmap commit under injected Err/short
reads (cache design ③), warm hit == cold miss == after trim (④), borrowing
(b3⑤), the once-per-text() lease check and Stale, reopen order (b3②), the
all-zero Engine == Engine::new() (P2-10), and the per-face baseline measured on
the real Inconsolata Regular 3.000 / JF Dot Shinonome 16 (U1 (a): ascent 13/14,
standard corpus fits its slot)."""
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
    ('close-keeps-entries', 'src/engine.rs', '        self.ja.close();\n        self.cache.invalidate_all();',
     '        self.ja.close();', 'reopen_closes_invalidates_then_opens'),
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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    # Existing consent/cache path; no missing-font SKIP.
    subprocess.run(['python3', str(ROOT / 'tools/fetch_fonts.py'), '--family', 'inconsolata'], check=True)
    from test_font_cache import run
    from test_font_outline import shinonome
    with tempfile.TemporaryDirectory(prefix='font-engine-jf-') as directory:
        env = {'OS32_FONT_SHINONOME': str(shinonome(Path(directory)))}
        return run('engine', MUTATIONS, args.mutate, env)


if __name__ == '__main__':
    raise SystemExit(main())
