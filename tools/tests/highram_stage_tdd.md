# High-RAM staged publish TDD

## 実行した RED → GREEN

- MODEL 初期化直後の公開 `pgalloc_alloc_page()` が 0 を返す検査を先行。
  RED: `CHECK ... pgalloc_alloc_page() == 0`。MODEL の ONLINE barrier を追加して GREEN。
  最初の実行は `python` が無く失敗し、以後 `python3` を使用。
- 実 `physmem.c + pgalloc.c + paging.c + sys.c` の ILP32 結合ハーネスを先行。
  SDK include / linker symbol のハーネス不足を修正後、弱参照で
  `FAIL sys_memory_stage_online != 0` を確認。layout/bootstrap/map/publish を接続し GREEN。
- 高 RAM PDE の PCD を残したまま publish してはならない検査を先行。
  RED: `FAIL !sys_memory_stage_online()`。マップの実効属性検証を追加し GREEN。
- model が PT workspace 内にある alias の拒否検査を先行。
  RED: `FAIL !sys_memory_bootstrap_model(alias, &l, paging_verify_identity)`。
  workspace と model/backing の重複検査を追加し GREEN。
- layout が zero 対象の metadata backing 内にあるケースを先行。
  RED: `FAIL sys_usable_mem_end() == l.workspace_first * PAGE_SIZE`。
  sys が layout をローカルに snapshot してから初期化するようにして GREEN。

## 結合試験の範囲

`python3 tools/tests/test_highram_stage.py`

- 実 target の `u32` / GNU89 を `gcc -m32 -march=i386 -nostdlib -static` で実行。
  32/64 はモデル化した MiB 数。LP64 の代用ではない。
- privileged CR0/CR3 と IRQ save/restore だけを host へ置換。
  Linux mmap は低い metadata/workspace の実書込みを可能にするため。
- 32/64MiB RAM、MMIO hole、UNKNOWN tail、最終 PFN `[1048575,1048576)`。
  最終物理ページ自体は Linux から dereference しない。PTE と実 allocator は検査。
- 低域 PTE 全体の不変、既存 16–32MiB PT と新規 PDE、workspace 内 PT 配置。
- metadata 容量、workspace 空/重複/APP 域/NP/USER/PCD の失敗は model・backing・sys・allocator 未変更。
- BOOTSTRAP の公開 byte/PFN allocation 拒否、workspace/metadata の一般確保と free 拒否。
- stage 全体の nonmaster/live-AS 拒否。これら二つは CR3/live カウンタを
  fixture として設定し、AS を BOOTSTRAP allocation で作れるとは主張しない。
- workspace OOM の個別 map rollback、最終 range での OOM は先行 map を保持しても未 ONLINE。
- workspace が後から NP になった場合、高 RAM/一般空きページへの fallback は無し。
- IF=1/0 復元。既存 allocator instrumentation の IRQ/ownership 検査も維持。

## 旧試験の扱い

- `test_pgalloc_model.py` の容量/疎 PFN 算術試験は非公開 `alloc_n_pfn` core を明示的に使う。
  公開 MODEL allocation は以前と違って BOOTSTRAP 中拒否することを検査する。
  exec A/B claim、shared-library failed load、mark/free の旧意味は変更しない。
- `paging_rebuild_host.c` の sparse/attrs/final は残した legacy mapped-candidate scanner の
  単体 fixture として `online=1, model_mode=0` を設定し、元の成功/拒否条件を維持。
  MODEL の publish 証拠には数えない。新しい結合ハーネスにはこの bypass はない。
- `test_pgalloc_range.py` は新しい未使用 stage 関数のリンク依存を gc-sections で除外。
  paging を fake success stub に置換していない。

## 検証コマンド

- `python3 tools/tests/test_highram_stage.py`
- `python3 tools/tests/test_pgalloc_model.py`
- `python3 tools/tests/test_pgalloc_range.py`
- `python3 tools/tests/test_paging_bounds.py`
- `python3 tools/tests/test_paging_bounds.py --rebuild nonmaster`（rollback / sparse / attrs / final も）
- `python3 tools/tests/test_physmem.py`
- `make kernel && make check`

上記は実行して成功。kernel build は変更対象外の既存警告（TVRAM_BPR、implicit declaration、
V86 の低番地アクセス、SHM static assert、linker RWX/GNU-stack 等）を出した。
新しい pgalloc/paging/sys は `i386-elf-gcc -O2 -std=gnu89 -Wextra -Werror
-Wdeclaration-after-statement -D__KERNEL_BUILD__` でも個別にコンパイル成功。

## 制限 / 次段

- 現在の `kernel_main` の boot 順序は変更していない。検出/provider をつながず、
  production に SYNTHETIC 入力を追加していない。KAPI/生成物の ABI 変更も無し。
- 新入口は `sys_memory_bootstrap_model` → `sys_memory_stage_online`。
  前段は未確保・既に mapped の低い workspace と metadata が必要。
  旧 metadata-only handoff は互換保持するが workspace 無しでは ONLINE にできない。
- stage 失敗後は fail-stop 契約。個別 map は rollback、先に成功した range は残り得る。
  全 stage rollback / SMP / device aperture と live allocation の調停は未実装。
- 次段は authoritative machine RAM provider、kernel boot hook/order の移行、
  legacy exec と live allocations を扱う device broker。エミュレータ/実機/配備は未実施。
