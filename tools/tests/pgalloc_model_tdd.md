# physmem → allocator 結合: 実行記録

## legacy mark/free 回帰の訂正（この節が旧移行方針に優先）

旧記録の `mark = permanent` は互換 API の契約を誤って変更していた。
exec/shlib を永久 claim に移行して吸収する方針は撤回する。
`pgalloc_mark_used()` は eligible なページへの冪等・解放可能な claim、
`pgalloc_reserve_pfn()` と model metadata は永久予約として分離する。

製品変更前に、実 `pgalloc.c + physmem.c + sys.c` を使う次の RED を確認した。
exec は `exec/exec.c` から **実際の `exec_child_claim` 関数と定数定義を抽出してコンパイル**し、式をテスト内に複製していない。

- 16MiB exec: A=`0x500000`/2431ページ、B=`0xf7f000`/65ページ、上端=`0xfc0000`。
  mark の重複後に A/B を free しても baseline に戻らず RED。
  A/B 間の動的確保ページは保持し、その個別解放後に baseline へ戻ることを検査。
- shlib: `MEM_SHLIB_BASE` の1MiB/256ページについて、`shlib_init` の
  mark → 読込失敗時 free と同じ allocator 呼出しを実行し、baseline 復帰が RED。
  VFS/ライブラリローダ全体は実行していない。
- free/live/permanent の混在 mark: 既存 live と衝突して他の eligible ページを claim できず RED。
- metadata と隣接 RAM の混在 mark: RAM を free できず RED。UNKNOWN・最終PFNも検査。
- range の `generic_regression`: 誤って変更されていた永久化期待値を
  「mark したページも解放され total/free が元に戻る」へ訂正して RED。
  permanent/free の既存試験は削除せず、予約の入口を明示的 `pgalloc_reserve_pfn` に訂正。

その後 `pgalloc_mark_used` だけを IRQ 保護下の eligible/live ビット更新へ変更して GREEN。
free 全件検証、永久予約、metadata、high RAM capacity/generic 制約の実装は変更なし。
exec/shlib/PEGC 呼出元は変更なし。PEGC の `sys_reserve_top` が永久予約したページへの
後続 mark は no-op のまま。

実行結果:

- `python3 tools/tests/test_pgalloc_model.py -v`: RED 12件中4 failure → GREEN 12件全通過。
- `python3 tools/tests/test_pgalloc_range.py`: RED generic baseline assertion → 全ケース GREEN。
- 同 range runner の target GNU89 `-Werror` compile PASS。
- `i386-elf-gcc -std=gnu89 -m32 -march=i386 -O2 -Wall -Wextra -Werror -Wdeclaration-after-statement -ffreestanding -fno-pie -fno-stack-protector -D__KERNEL_BUILD__ -Iinclude -Ikernel -Ilib -c kernel/pgalloc.c`: 一時出力先で PASS。
- 対象製品ファイルの `git diff --check`: PASS。
- RED の実出力は `pgalloc_legacy_red_model.log` / `pgalloc_legacy_red_range.log`（要点抜粋）に保存。

共有ビルド・pagingテスト・エミュレータ・配備・ネットワーク・commit はこの訂正では実施していない。
以下の旧実行記録は当時の結果として保持するが、旧 mark 契約と永久 claim 移行方針は上記の通り無効。

## 今回の範囲

`physmem.c` を製品 C_KERNEL に登録し、旧 `pgalloc_init()` も実 model → 同一 PFN core を通す。新 `pgalloc_init_model()` は呼出側の verified bootstrap backing を使用する。`sys_memory_init_model()` はその tail 配置を検査し、低位 exec 上端と旧 hotdeploy 基点を固定する。

製品の新 boot provider / high RAM mapping / exec・shlib の所有移行 / GFX activation broker は未接続。この記録は Phase 2 全体完了や実機 high RAM 検出成功を意味しない。

## RED → GREEN（実行済みの順序）

1. `test_permanent_and_atomic_free`: 旧実装は mark 後の free_page で予約を解除し、assertion failure。eligible/allocated 分離、atomic free、one-shot init を実装して PASS。
2. `test_dynamic_sparse_final`: weak reference による `pgalloc_init_model != 0` が assertion failure（リンクエラーではない）。checked backing handoff と実 PFN allocation を追加して PASS。32/64MiB 区間、疎な穴、最終 PFN を実確保・解放。
3. `test_sys_low_stable`: 最初は harness の SDK include 不足を修正。その後、旧 unchecked `sys_mem_kb * 1024` の hotdeploy base assertion が RED。clamp/page 切捨てと allocator 連動 top 予約で GREEN。
4. `test_sys_model_handoff`: weak reference の存在 assertion が RED。tail metadata → frozen exec/hotdeploy の結合で GREEN。
5. `test_generic_never_returns_unmapped_machine_ram`: 8MiB bootstrap + 12MiB MACHINE 区間の fixture で、低位使切り後 generic alloc が未マップの12MiBを返して RED。generic の範囲を model の実 legacy contiguous end に限定して GREEN。明示的 PFN API からの同ページ取得は維持。

追加検証: metadata の容量不足・pointer alignment・UNKNOWN/予約/shlib帯・verify拒否・model self overlap・range table capacity・再初期化拒否・製品 synthetic/host pointer拒否・metadata sentinel。容量試験の初稿は fixture の range 数を誤って期待し失敗したため、実際に64区間となる境界を構築して修正した。製品側を弱めて通していない。

## 最終実行結果

- `python3 tools/tests/test_pgalloc_model.py`: 8 test methods PASS（32/64MiB、production flag 組合せを含む）。実 `physmem.c + pgalloc.c + sys.c`、ILP32/GNU89、libc不要。
- `-finstrument-functions` で init_core / bitmap read/write の IF=0 を検査。各 irq_restore 前に全PFNの eligible/allocated と counters を照合。IF=0/1 の復元、失敗時 output/counter/metadata/model 不変を検査。
- `python3 tools/tests/test_pgalloc_range.py`: 全ケース PASS、target GNU89 `-Werror` PASS。
- `python3 tools/tests/test_physmem.py`: 8 test methods PASS。
- `python3 tools/tests/test_paging_bounds.py` と `--rebuild nonmaster` / `--rebuild rollback`: 全 PASS。
- F1/F2: `test_sqlite_groups.py` 32/32、`test_vfs_fd_sqlite.py` 15/15、`test_kapi_db_owned.py` PASS。
- `make kernel`, `make check`: exit 0。
- `physmem.c`, `pgalloc.c`, `sys.c`: `i386-elf-gcc -std=gnu89 -m32 -march=i386 -O2 -Wall -Wextra -Werror -Wdeclaration-after-statement -D__KERNEL_BUILD__` compile PASS。

既存 harness の適応は、fresh boot fixture で private initialized を reset、bitmap添字を絶対PFNに変更、上端を limit query に変更、永久予約を free しない期待値へ更新、paging harness の実 physmem link 追加だけ。製品 reset API は追加していない。

## 製品リンク・配置

最終 `kernel.elf` の nm で `physmem_bootstrap_legacy`, `pgalloc_init_model`, `sys_memory_init_model` を確認。実 `pgalloc.o` の physmem 呼出 relocation も確認した。

- `__bss_start=0x12f880`, `__bss_end=0x153d80`
- 旧互換用 `legacy_metadata`: 0x400 bytes。全4GiB分の静的bitmapではない。
- `KHEAP_BASE=0x154000`, SHM高側guard終端 `0x1e7000 < stack guard 0x1fb000`
- `kernel.bin`: 194684 bytes。新bootでは必要量だけ caller-backed metadata、最大262144 bytes。

build は成功したが無警告ではない。範囲外既存コードに TVRAM_BPR 再定義、kernel kprintf / exec sys_usable_mem_end の暗黙宣言、GNU-stack/RWX linker warning。SQLite bundled build に used-but-never-defined warning。対象3ファイルのstrict compileは無警告。

## 次担当への正確な入口

- `pgalloc_metadata_bytes(model)` → 必要なpage-rounded backing容量。
- `sys_memory_init_model(model, backing, capacity, first_pfn, verify)` → tail配置、mapping確認callback、allocator初期化、低位getter固定。旧 entry はまだこれを呼ばない。
- `pgalloc_init_model(...)` → lower-level checked初期化。callerは最終exec arenaとmetadataの非重複を管理すること。
- `pgalloc_alloc_n_pfn(n, first, end, &pfn)` / `pgalloc_free_n_pfn(first, n)` → explicit physical-only API。最終ページは `[1048575,1048576)`。
- `pgalloc_reserve_pfn(first,end)` → permanent・live allocation衝突で全件拒否。デバイスowner/exec全arena衝突brokerの代替ではない。
- `pgalloc_limit_pfn()` → high-water。`total_pages()` はeligible union数でありアドレス上端ではない。

paging担当は total由来の上端計算を廃止し、mapped-safe backing探索・追加PT workspace・RAM mapping完了後の一般高位公開を接続する。exec/shlib担当は旧 mark/free call site を永久claim所有へ移行する。GFX担当は destructive probe 前の予約brokerを接続する。これらを行う前に高位bootを有効化しない。

エミュレータ・配備・ネットワーク・機種検出・独立review agent・commit は実施していない。
