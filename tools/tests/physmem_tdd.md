# physmem synthetic source 隔離 — 専用 TDD 記録

## 範囲・契約

変更対象は `kernel/physmem.c`、`kernel/physmem.h`、
`tools/tests/test_physmem.py` と本記録のみ。
既存の未コミット変更には触れていない。

- `PHYSMEM_SOURCE_SYNTHETIC` の受理は、`physmem.c` を明示的に
  `-DPHYSMEM_HOST_TEST=1` でコンパイルし、かつ `__KERNEL_BUILD__` が
  **未定義**のホスト試験に限定する。公開ヘッダーでフラグを定義しない。
- production (`-D__KERNEL_BUILD__`)、フラグなし、ホストフラグ `=0` は拒否。
- kernel と host の両フラグ指定はコンパイル可能だが **kernel 優先で拒否**。
  コンパイル失敗方式ではなく、`physmem_add_trusted()` が 0 を返す方式。
- `SYNTHETIC | MACHINE` も拒否。入力は単一 source の従来契約を維持する。
- 拒否は overlay より前。モデル全バイトが不変で、count/find が拒否した
  synthetic 領域を RAM として公開しないことを試験する。
- MACHINE の呼出側による保証、legacy の上限・予約領域・source は維持。
  RAM 検出器も実機 RAM の検証も追加していない。

## RED（製品コード編集前）

1. `python3 tools/tests/test_physmem.py -v`
   - 既存 **7 tests OK**（1.301s）。
2. テスト側のみ変更。既存試験のコンパイルに `-DPHYSMEM_HOST_TEST=1` を
   明示し、回帰試験では flags を置換して production 条件から実行。
3. `python3 tools/tests/test_physmem.py PhysmemTests.test_non_host_builds_reject_synthetic_atomically -v`
   - **exit 1 / Ran 1 test / FAILED (failures=4)**（1.417s）。
   - 最初の失敗は `mode='kernel', source='PHYSMEM_SOURCE_SYNTHETIC'`。
   - コンパイルは成功し、生成 C の 20 行目
     `CHECK(!physmem_add_trusted(&m, 4096, 6000, PHYSMEM_SOURCE_SYNTHETIC));`
     が失敗。`AssertionError: 20 != 0 : C CHECK failed at line 20`。
   - default、kernel_and_host、host_disabled も同じ単独 synthetic 入力で失敗。
   - mixed source のサブテストは修正前から成功（既存の拒否契約）。

これにより `__KERNEL_BUILD__` を付けても synthetic が受理される欠陥を、
製品コード変更前の実行で再現した。

## GREEN

source 判定だけを条件コンパイルで制限し、ヘッダーにフラグと競合時の契約を記載。

`python3 tools/tests/test_physmem.py -v`

- **exit 0 / Ran 8 tests in 2.652s / OK**。
- 既存7件＋新規1件。新規試験は4ビルド条件 × 2 source のサブテスト。
- legacy bootstrap、MACHINE RAM、MMIO を含むモデルに対して拒否の原子性を確認。
- `physmem_count()` の RAM 数不変、synthetic 専用区間の RAM 数0、
  `physmem_find()` の失敗と出力 PFN 不変を確認。
- 既存試験は明示的 host フラグ付きで synthetic を実際に受理し、
  重複正規化、容量失敗、予約、穴、legacy 上限等を引き続き検証。
- 実行はホスト GCC `-m32 -std=gnu89`、実際の ILP32 `types.h`、
  freestanding / no libc のバイナリ。エミュレータ実行ではない。

## ターゲット GNU89 コンパイル

`~/opt/cross/bin/i386-elf-gcc -dumpmachine` → `i386-elf`。
同コンパイラで `kernel/physmem.c` 単体を一時ディレクトリにコンパイルした。
共通オプション:

```
-std=gnu89 -Wall -Wextra -Werror -Wdeclaration-after-statement
-ffreestanding -fno-builtin -fno-pie -fno-stack-protector
-Iinclude -Ikernel -c kernel/physmem.c -o <temporary-object>
```

出力（exit 0、警告なし）:

```
target: i386-elf
kernel: GNU89 compile PASS
default: GNU89 compile PASS
host: GNU89 compile PASS
kernel_and_host: GNU89 compile PASS
host_disabled: GNU89 compile PASS
```

条件は順に `-D__KERNEL_BUILD__`、追加フラグなし、
`-DPHYSMEM_HOST_TEST=1`、両方、`-DPHYSMEM_HOST_TEST=0`。

## 未実施・禁止範囲

paging / pgalloc / sys への統合・変更、既存 docs 編集、フル kernel ビルド、
エミュレータ・実機・配備試験は未実施。ネットワーク、環境設定変更、秘密情報、
`docs/hw/`、コミット、別エージェント起動は扱っていない。
ターゲットの確認は単体コンパイルであり、起動動作や検出 RAM を保証しない。
