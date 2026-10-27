# L18 fixed pre-move output

`emergency.trace` was captured from `b400acb6` (the design's survey base),
using the same MMIO seam and input sequence as the verifier. The full source
revision, source hashes, harness hash, trace hash, inputs and record format
are in `emergency.json`. Verification uses these files without invoking git.

Each store records address, access width, value, and the resulting 16-bit
cell. Before each case both planes contain byte sentinel `0xa5`. This checks
boot attribute upper-byte preservation, fault upper-byte replacement, and
character-before-attribute ordering. Cases cover right-edge wrap, bottom
clipping, empty output, legacy exception attributes/hex formatting, all three
DF guard labels/serial ordering, and V86 counts plus space-filled tail.
CPU delivery/task switching and visible output remain guest acceptance work.

The separate capture script and capture log are retained in
`~/os32-tmp/run/depdir-l18/`. The verifier's `--compile-mutations` runs normal
controls first, then compiles width replacements with `-Werror` without
executing them; `--mutate` is reserved for PM acceptance.
