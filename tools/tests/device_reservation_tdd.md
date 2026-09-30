# Device reservation CORE — TDD / host evidence

## Scope

Changes are limited to `kernel/sys.c`, `include/sys.h`, `kernel/pgalloc.c`,
`kernel/pgalloc.h`, and the dedicated tests/this record. The pre-existing dirty
memory-model/staging work was retained. No backend, exec, paging implementation,
boot activation, KAPI, device probe, deployment, emulator or network changes.

## Observed RED → GREEN slices

Each new behavior was exercised against real ILP32 GNU89 sys/pgalloc sources
before its implementation, then rerun successfully:

1. Missing `sys_device_reserve_core` weak reference failed `CHECK(... != 0)`.
   Added optional UNKNOWN aperture acquisition and owner-exact retry.
2. Two-span live collision fixture rejected the later collision, then failed
   the success check after freeing it. Added full bitmap preflight and commit;
   both spans' eligibility/counts are now updated together.
3. Fixed-region rejection failed on a low fixed PFN. Added private boot
   provenance and full original legacy-arena protection, including the A/B
   dynamic hole, metadata, whole PT workspace, hotdeploy and generic MMIO.
4. Xe10 request with an overlapping duplicate failed `device_claims == 2`.
   Added sorted/coalesced immutable request sets and exact order-independent
   retries; capacity applies to the whole normalized transaction.
5. RAM BB + aperture transaction failed the success check with a valid trusted
   mapping capability. Added RAM-kind eligibility and mapped-interval checks;
   UNKNOWN apertures remain distinct from acquired BB RAM.
6. An expanded sys low extent failed rejection; after fixing sys-to-allocator
   checked fixed-end handoff, a permanently reserved UNKNOWN PFN failed rejection.
   Retained raw permanent reservation provenance with atomic bounded overlay.

Initial RED logs were captured under `/tmp/os32-device-red*.log`; the tests are
self-contained and do not depend on those temporary records.

## Final verification performed

- `python3 tools/tests/test_device_reservation.py`: **8 tests PASS**. Ownership
  fixtures execute with IF=1 and IF=0, instrument actual bitmap operations,
  and independently verify allocator counts before restoring IF. Failure of
  a two-span transaction preserves eligibility/allocated storage and ledger.
  One-free-ledger-slot/two-span transaction failure leaves the RAM allocatable;
  retry of an existing owner also works at ledger capacity.
- `device_reservation_stage_host.c`: actual `paging_init` →
  `sys_memory_bootstrap_model` → `sys_memory_stage_online` → broker path PASS.
  Real page tables establish the RAM mapping capability. A collision at the
  second aperture's final page preserves both bitmaps, every ledger entry and
  statistics; after freeing it, RAM BB + aperture commit and retry succeed.
  Aperture PTEs deliberately remain unchanged: reservation is not MMIO mapping.
- The other synthetic ownership fixtures set private ONLINE state explicitly;
  they are ownership arithmetic tests, not mapping/publication evidence. The
  separate staged test above supplies the end-to-end public-path evidence.
- `test_physmem.py` (8), `test_pgalloc_model.py` (12), `test_highram_stage.py` (8),
  `test_boot_splash_native.py` (2), plus `test_pgalloc_range.py` and
  `test_paging_bounds.py`: PASS. Legacy releasable exec/shlib claims remain
  releasable; device/permanent claims cannot be freed or resurrected by mark.
- Both changed C units compiled with i386-elf GNU89, `-Wall -Wextra -Werror
  -Wdeclaration-after-statement`, kernel build flag: PASS.
- `make kernel`: exit 0. kernel.bin 198364 bytes; VK32 448538 bytes.
  Warnings remain outside these C units: TVRAM_BPR redefinition, implicit
  declarations in kernel.c/generated KAPI, GNU-stack and RWX linker warnings.
- `make check`: exit 0 (includes existing manifest informational exclusions).
- Product `nm` confirms `sys_device_reserve_core`, `pgalloc_device_reserve`,
  `device_ledger`, `device_claims`, and private provenance linked. `objdump`
  confirms the sys entry calls the real allocator with saved/disabled IRQs.
- Scoped `git diff --check`: PASS.

## Interfaces and exact limitations

`sys_device_reserve_core(owner, spans, count, capability)` returns reserved/not
reserved, never hardware-ready. `sys_device_span` uses PFN half-open intervals
and MMIO/RAM kinds; 4GiB-exclusive PFN and checked oversized counts are supported.
The bounded ledger holds 16 normalized spans total and has no reset/free API.
The live truth is allocator eligibility plus the ledger; caller boot models are
not modified as a device-reservation mechanism. `pgalloc_reserve_pfn` now also
preflights its private PHYSMEM_MAX_RANGES provenance snapshot, including UNKNOWN
claims, and can fail unchanged when that representation is full.

IDLE and RAM_MAPPED are **trusted internal caller attestations**, not an
implemented authentication or exec-idle detector. No production caller currently
mints them. RAM acquisition takes an exact caller-selected extent, not automatic
BB search. It rejects unknown/out-of-range RAM, all fixed regions and the entire
original legacy arena; no numeric-only BB ownership is returned. Same-owner
exact repeat reuses the acquired RAM; matching aperture bytes cannot become RAM.

The low fixed extent is conservatively protected through hotdeploy, retaining
original boot arena protection even after top carve-outs. No low PC98 display
resource loan, candidate-ID/constant whitelist, mapping/cache transition,
probe/enable authorization, prepared/active status, or GUI activation integration
is supplied by this core. In particular legacy 16MiB PEGC/Xe10 bank windows still
collide with the arena, and legacy-only PEGC has no newly found BB outside it.
There are no default aperture holes and no optional backend call sites enabled.

Next integration must provide the real idle/mapping capability and validated
candidate constants; choose BB placement safely; commit before any device map,
probe, enable, or client publication; and retain reservations after later
mapping/hardware failures. Guest/native hardware behavior remains unverified.
