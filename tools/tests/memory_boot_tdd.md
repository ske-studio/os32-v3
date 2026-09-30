# memory_boot 起動アダプタ 検証記録

## この文書の性格

**RED → GREEN の実施ログではない。** 他の 13 組 (`*_host.c` + `test_*.py` + `*_tdd.md`) と違い、
`memory_boot` は TDD 記録だけが残されないまま作業が中断していた
([RETROSPECTIVE_2026-09-09](../../docs/archive/agents/RETROSPECTIVE_2026-09-09.md) §6)。
後から RED→GREEN の経過を書き起こすと偽の履歴になるので、ここには
**2026-09-09 に実際に走らせて確認した内容だけ**を書く ([V4])。

## 実行

```
python3 tools/tests/test_memory_boot.py      # 実測: 6 tests OK
make check-memory-host                        # 上記を含む登録済みターゲット
```

`kernel/paging.c` / `pgalloc.c` / `sys.c` を ILP32 (`gcc -m32 -march=i386 -std=gnu89
-Wall -Wextra -Werror -Wdeclaration-after-statement`) で実コンパイルし、`kernel/physmem.c` は
無改変でリンクする。host へ置換するのは privileged な CR0/CR3 と IRQ save/restore だけ。

## 検査している振る舞い

| ケース | 内容 |
|---|---|
| `test_8m_legacy_preinit` | 8MiB では新モデルを諦めて `pgalloc_init(mem_kb)` にフォールバックし、`sys_usable_mem_end()` が従来どおり hotdeploy 窓を除いた値になる |
| `test_16m_safe_tail_online` | 16MiB では BOOTSTRAP → ONLINE を通り、`sys_hotdeploy_base()` / `sys_usable_mem_end()` / workspace 範囲 / `pgalloc_limit_pfn()` が期待値になる |
| `test_huge_hint_no_promotion` | `mem_kb = 0xffffffff` を渡しても実在しない RAM へ昇格しない |
| `test_no_fallback_after_bootstrap_or_stage_failure` | bootstrap / stage のどちらが失敗しても**旧アロケータへ黙って戻らない**。実際の `kernel.c` のゲートを切り出して実行し、下流 (`shm_init`) に到達したら失敗とする |
| `test_actual_pte_verification_before_write` | metadata / workspace / hotdeploy の PTE を壊した状態では書き込む前に失敗し、既存メモリの内容が保たれる |
| `test_kernel_boot_order_and_failstop` | `paging_init` → `memory_boot_init` → 下流の順序、`pgalloc_init(mem_kb)` が `kernel.c` から消えていること、失敗時に `for (;;) { _stop(); }` (io.h の原始命令 = `cli; hlt`) で止まること、`build/kernel.mk` に登録されていることを静的に照合 |

## 未検証

ゲスト (NP21/W) 上での起動は本記録の範囲外。実 CR3 / 実 PTE に依存する
`paging_boot_context()` と `paging_verify_identity()` は、ホストでは置換された環境で動いている。
