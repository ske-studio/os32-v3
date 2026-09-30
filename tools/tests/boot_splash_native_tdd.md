# Boot splash: native PC98 dispatch evidence

## Scope and finding

Read `kernel/kernel.c`, `kernel/boot_splash.c/.h`, `gfx/gfx_core.c`,
`gfx/gfx_internal.h`, `include/gfx_hal.h`, PC98 backend and optional backend
init/shutdown paths before implementation. No `system.cfg` exists in this
checkout (`git ls-files '*system*cfg*'` and recursive `Path.glob` returned none).
The actual guest configuration was not read: emulator/network access is out of
scope. The kernel's existing GFX config parser and preference forwarding were
read and left unchanged.

`gfx_init()` already selects the backend on every call; there is no global
initialized latch to clear. PC98 shutdown already stops the GDC and resets
flip/page/height state. Therefore no gfx core change is needed. In particular,
do not clear the optional backend descriptors on shutdown: existing Cirrus
code deliberately retains the client framebuffer identity for exec mapping.

The boot-local static helper saves the configured preference, temporarily uses
PC98 for the existing real dispatch, then restores the preference before any
splash drawing or early exit. It performs no optional-device probes, config
writes, reservations, or exported ABI changes. This is a synchronous boot-only
helper, not a concurrent backend-switch API.

## RED -> GREEN (actual execution)

Command: `python3 tools/tests/test_boot_splash_native.py -v`

1. Initial harness compile had three setup errors (host libc `strchr` macro,
   raster argument const mismatch, nonexistent TVRAM_SIZE). Fixed the harness
   before counting RED.
2. RED: real dispatch failed the assertion
   `boot must not probe or initialize optional devices` for AUTO, PEGC and
   Cirrus. PC98 already passed.
3. Added only the boot-local preference helper/call. GREEN: all four preference
   cases passed, including rendering, native shutdown, repeated boot, later GUI
   selection, repeated GUI init/shutdown, and optional probe/init failure fallback.
4. Added an invalid-native-buffer injection after real native init (scroll
   boundary stub). RED: all four preference cases exited with SIGSEGV (-11).
5. Added a missing-buffer/backend guard before drawing. GREEN: both unittest
   methods passed (four preference subprocesses each). The fault case verifies
   text cleanup, preference preservation, zero optional probes/inits, and retry.

Harness includes the real `boot_splash.c`, `gfx_core.c`, and `backend_pc98.c`.
It substitutes port I/O, raster timing/presentation, palette/scroll, and optional
backend hardware operations. Native backing memory is anonymous host mmap,
using MAP_FIXED_NOREPLACE. Thus dispatch, software drawing, and PC98 shutdown
execute; actual hardware display, timing, device reservation, or optional
hardware initialization are **not** verified.

## Target compile and isolation

`i386-elf-gcc` compiled `kernel/boot_splash.c` and unchanged `gfx/gfx_core.c` to a
private TemporaryDirectory with repository kernel include paths and GNU89,
i386 freestanding flags, plus `-Wall -Wextra -Werror
-Wdeclaration-after-statement`. Both compiled without diagnostics.
`git diff --check -- kernel/boot_splash.c kernel/boot_splash.h` passed.
No shared object/build output, full build, deployment or emulator run was used.

## Outstanding lifecycle work (not implemented here)

- No unconditional 15–18 MiB holes were added. No paging/physmem/pgalloc/exec or
  optional backend code was modified.
- Safe later GUI activation still needs coordinated optional-device reservation:
  claim conflicting physical apertures before a probe enables/maps/writes them;
  handle busy ranges/failure, PEGC backing-buffer allocation, and retained
  Cirrus window/descriptor ownership across shutdown and retry.
- Exec currently uses the selected backend's client-buffer identity before the
  app calls gfx_init. After native boot that identity is PC98, not a prewarmed
  optional backend. Later GUI/client address-space mapping and reserve-before-
  enable ordering must be integrated by the memory/exec owners. This patch does
  not claim that optional GUI activation is memory-safe by itself.
- Native PC98 hardware is guaranteed on target, but the void gfx_init API does
  not report I/O failures or recover faults inside initialization. The new guard
  only catches missing post-init software state before splash drawing.
- kernel.c's existing comment saying the splash selects the configured backend
  is now stale; kernel.c edits were outside this task's allowed scope.
