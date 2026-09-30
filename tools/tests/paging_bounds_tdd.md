# Phase 1 paging bounds — host TDD evidence

Command: `python3 tools/tests/test_paging_bounds.py`

The harness compiles actual `kernel/paging.c` and `kernel/pgalloc.c` as freestanding static ILP32 GNU89. CR0/CR3 instructions and allocator IRQ save/restore are replaced only in temporary host copies; Linux mmap provides the backing physical-address range. The ranged allocator wrapper injects allocation failure, but successful allocation/free and bitmap accounting use the real allocator. No Make outputs are shared. The runner also compiles unmodified target paging.c with i386-elf-gcc, `-Werror` and `-Wdeclaration-after-statement`.

## Observed RED → GREEN slices

Exit statuses below were the failing assertion's source line at that stage, not stable test IDs:

- Final page: RED exit 33; GREEN after sparse bootstrap/dynamic PT and count-based mapping.
- Physical range overflow: RED exit 39; GREEN after range delegates to checked PFN/count mapping.
- Inclusive final-page readonly: RED exit 43; GREEN after PFN attribute loops, preserving flags and absent pages.
- Live AS/new PDE: RED exit 56; GREEN after successful AS creation/destruction updates live count.
- Invalid USER range: RED exit 75; GREEN after whole-range preflight and PFN iteration.
- Backing PDE validity: RED exit 90; GREEN after validating the master PDE as well as its identity supervisor PTE.

Initial harness setup also encountered pre-existing signedness warnings, missing linker symbols, and a host-address/linker-symbol SIGSEGV. These were harness problems, not accepted RED assertions. Linker symbols are now fixed absolute target addresses. Final builds need no warning suppression.

Additional checks (rollback, multiple/active AS destruction, missing PT, explicit MMIO, invalid-span no-write) were added as regression coverage and passed on first execution; these are not claimed as separate test-first RED cycles. Strict per-assertion RED for every safety property was therefore not achieved.

## Final verified coverage

- 0xFFFFF000 mapping one page succeeds; two pages rejects; physical overflow and oversized count reject.
- Byte end 0xFFFFFFFF range terminates; reversed ranges reject before writes.
- Sparse missing PT is NP; readonly never creates RAM or sets P, and preserves frame/cache/user flags by clearing RW only.
- USER ranges preflight all PDEs, preserve PCD, and do not promote the master PDE.
- New PDE requests while any AS lives fail before allocator calls. Existing PDE mappings still work.
- Multiple AS destruction, active-AS destroy refusal and repeated destroy preserve live count and allocator usage.
- Uninitialized allocator and non-master CR3 reject dynamic backing allocation.
- Backing is outside APP_BAND and requires valid writable identity master PDE/PTE.
- Injected second-PT failure restores allocation accounting, removes unpublished pointers, and leaves existing PTEs/PDEs untouched.
- Bootstrap static PT storage remains eight pages plus alignment padding.
- Explicit 2MiB Cirrus-style supervisor PCD mapping still succeeds.

Observed final output:

```
PASS: final-page, virtual/physical overflow, range preflight
PASS: sparse NP, attribute flags, USER/PCD, user-range preflight
PASS: real allocator rollback, live-AS lifecycle, master-only backing
HOST ILP32 + TARGET GNU89 PASS
```

`git diff --check` for the changed paging/test files passed.

## Scope / integration handoff

- RAM clamp remains 16MiB; physical-memory/allocator expansion is phase 2.
- `PAGING_MAP_SIZE` remains only a documented bootstrap-size compatibility alias because existing Cirrus/PEGC code uses it. Paging APIs use `PAGING_PFN_COUNT=1048576`, not that alias.
- The old bootstrap-end selftest uses `PAGING_BOOT_MAP_SIZE`; no wrapped 4GiB byte endpoint is introduced.
- The earlier unconditional 15–18MiB guard directive was withdrawn. No optional aperture policy or backend changes were made.
- `make kernel` / `make check` are deferred to the parent after coordination with the other worker; this subagent ran only isolated compilation. No emulator/deploy/hardware verification was performed.

## レビュー指摘の再構築（今回の実測証拠）

上記の過去の RED 欠落記録は保存する。今回の再構築をもって過去の手順まで
TDD だったとは扱わない。独立レビューは親への引継ぎ事項であり、未実施。

### 再初期化の RED → GREEN

- 本番変更前に `paging_bounds_host.c` に実ソースの再初期化回帰を追加。
  動的 PDE 1023 と生存 AS を作り、AS の CR3 を有効にしたまま
  `paging_init(1024)` を再度呼んだ。
- `python3 tools/tests/test_paging_bounds.py`:
  `FAIL: page_tables[1023] == pt`、実行バイナリ exit 168（runner exit 1）。
- `paging_init` の宣言直後、最初の処理として `if (pg_enabled) return;` を追加。
  同コマンドで GREEN。動的 PT/PTE/PDE、live count、allocator 使用数・呼出数、
  CR3、AS 側の共有 PDE が保持され、AS 破棄後も使用数が正常に戻ることを確認。
- ヘッダに一回限りの初期化契約を明記。旧190/200行の「範囲内分は適用済み」は
  全範囲未変更の失敗契約へ訂正。

### 旧実装の削除範囲と再構築順序

1. `kernel/paging.c` の **`alloc_master_pt` 関数全体を削除**し、
   **`prepare_tables` の実装全体も削除**。既存回帰テストは維持した。
   保存コピーからの復元や条件反転後の復元ではなく、残した実体は次だけ:

   ```c
   static int prepare_tables(u32 first, u32 count)
   {
       (void)first;
       (void)count;
       return 0;
   }
   ```

2. 専用 `paging_rebuild_host.c` を追加。公開 API を呼ぶと未実装 PT を参照して
   SIGSEGV となるため、この段階は実ソースの狭い `prepare_tables` を直接検査。
   `python3 tools/tests/test_paging_bounds.py --rebuild nonmaster`:
   `FAIL: prepare_tables(0x2000000 >> PAGE_SHIFT, 1) == -1`、exit 17。
   コンパイルエラーやクラッシュではなく、スタブの成功戻り値に対する assertion RED。
3. 不在 PT に対する非 master CR3 拒否のみを実装して同コマンド GREEN。
   allocator 呼出数/使用数、未公開 PDE/PT、CR3、live count も未変更。
4. 続いて `python3 tools/tests/test_paging_bounds.py --rebuild rollback`:
   `FAIL: prepare_tables(0x23FF000 >> PAGE_SHIFT, 2) == -1`、exit 24。
   これは master CR3 で隣接する不在 PT を2枚要求し、1枚分しか確保できない場合。
5. 新規 `reserve_table` と未公開 PT 内の一時連結リストで再実装。
   全確保成功まで `page_tables`/PDE は一切公開せず、失敗ならリストを全解放。
   旧 bitmap と公開ポインタを巻き戻す実装は復元していない。
   rollback の GREEN では失敗時使用数復元・両 PT/PDE 不在、制限解除後の
   2枚確保成功・正しいフレーム/PCD・隣接 PTE のゼロ初期化も検査。

### 最終実行

```
python3 tools/tests/test_paging_bounds.py --rebuild rollback
PASS: rebuild multi-PT rollback and successful retry
HOST ILP32 + TARGET GNU89 PASS
python3 tools/tests/test_paging_bounds.py --rebuild nonmaster
PASS: rebuild nonmaster rejection before allocation
HOST ILP32 + TARGET GNU89 PASS
python3 tools/tests/test_paging_bounds.py
PASS: one-shot init preserves dynamic PT, live AS, CR3, allocator
PASS: final-page, virtual/physical overflow, range preflight
PASS: sparse NP, attribute flags, USER/PCD, user-range preflight
PASS: real allocator rollback, live-AS lifecycle, master-only backing
HOST ILP32 + TARGET GNU89 PASS
```

すべて実行 exit 0。実ソース host ILP32 と未置換 target `i386-elf-gcc`
GNU89 (`-march=i386 -Werror -Wdeclaration-after-statement`) を専用一時領域で検証。
最終ページ、仮想/物理 overflow、sparse NP、属性保持、既存 PTE を含む複数 PT
失敗、live AS と既存全回帰が通過。共有ビルド・emitter・エミュレータ・配備・
ネットワーク・環境/秘密情報・ハード資料・commit・他 agent は操作していない。

## PFN 上端への PT 探索移行（実測）

- 実 `pgalloc.h/c` の PFN API と eligible union の契約を確認。
- 本番変更前に `--rebuild sparse` を追加。実 physmem/pgalloc を初期化し、
  metadata と低位 RAM を永久予約。唯一の空きは 20MiB の安全な共有 identity
  PTE に対応する1ページで、`BASE + total_pages * PAGE_SIZE` より上にある。
  RED: `FAIL: prepare_tables(0x2000000UL / PAGE_SIZE, 1) == 0`、binary exit 39。
  PFN 上端探索と単一 exact PFN 確保に変更して同ケース GREEN。
- dispatch 変更後、旧 byte allocator hook では rollback テストが失敗
  （`prepare_tables(...) == -1`、binary exit 65）。fault injection を実際の
  PFN 呼出しに移し、実 allocator の確保・解放を保ったまま rollback GREEN。
  CHECK の終了値は行番号の256剰余による偽成功を防ぐ固定1へ変更。
- `--rebuild attrs` の PDE PCD ケースで上記失敗戻り値 assertion RED（exit 1）。
  PDE 検査に PCD/PWT を追加。PDE の PS/NP/RO/未知 table と PTE の
  NP/非identity/RO/USER/PCD/PWT も検査し、allocator 呼出し・公開 PDE/PT・
  候補の全1024 word が未変更であることを確認。
  初回 GREEN 確認時の呼出数 assertion は予約済み低位 RAM への合法な探索も
  数えていたため失敗。fixture の低位 backing PDE を不在にして候補を分離した。
- `--rebuild final` は追加回帰（初回から PASS、独立 RED とは主張しない）。
  実 PFN allocator の limit=1048576、最後の PFN を確保・枯渇・解放。
  Linux i386 で最後の物理ページ自体は dereference せず、host に実在する
  mock 共有 PT metadata のみ読む。最終ページへの prepare/書込みの実機証明ではない。
- 最終実行: bounds と rebuild の attrs/sparse/final/rollback/nonmaster すべて
  exit 0。各 runner の未置換 `i386-elf-gcc` GNU89 `-Werror` target compile も PASS。
  対象限定 `git diff --check` PASS。共有ビルド、pgalloc/sys 編集、実機・emulator・
  配備・network・環境/秘密・docs/hw・commit・agent 操作なし。
