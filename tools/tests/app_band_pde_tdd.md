# app_band_pde — アプリ帯の可変 PDE 化 のホスト TDD

対象票: [`docs/tasks/memory/APP_BAND_PDE.md`](../../docs/tasks/memory/APP_BAND_PDE.md)
実行: `make check-memory-host` / `python3 -B tools/tests/test_app_band_pde.py`
三点セット: `app_band_pde_host.c` (検査本体) / `test_app_band_pde.py` (ビルドと実行) /
このファイル (何を見ているか)。

## 何を検証するか

1 アプリに渡せるメモリが搭載 RAM に依らず約 2.5MB で頭打ちになっていた原因は、
アプリ固有のページディレクトリエントリ (PDE) が **1 枚 (4MB) しかない**ことだった。
これを要求量に応じて 4MB 単位で増やす変更を、実物の `kernel/paging.c` を
ホストで動かして検査する。ゲスト (NP21/W) の検証ではない。

## 作り

`test_paging_bounds.py` と同じ仕掛け:

- `kernel/paging.c` を読み、特権命令 (`mov %cr3` / `mov %cr0`) だけを
  ホスト変数への代入に置換して `paging_host_source.c` として書き出す
- `kernel/pgalloc.c` は `irq_save` / `irq_restore` を潰して同様に書き出す
- `kernel/physmem.c` はそのままリンクする
- `-m32 -march=i386 -std=gnu89 -Werror -Wdeclaration-after-statement` で
  ILP32 としてビルドし、`-nostdlib -static` で直接走らせる (libc に依存しない)
- 最後に **クロスコンパイラ (`i386-elf-gcc -O2`) でも** `kernel/paging.c` を
  コンパイルして、ホストだけで通る C になっていないことを確かめる

恒等で読み書きできる実メモリは `mmap` (int 0x80 #90) で `0x400000` から 12MB
張る。`paging_init(16384)` + `pgalloc_init(16384)` の後に検査に入る。

## 検査項目

### A. 枚数計算 `paging_app_band_pdes(code_end, heap_req, ram_top)`

純関数。票 §4-1 の規則そのもの。

| 入力 | 期待 | 意図 |
|---|---|---|
| `heap_req == 0` | 常に **1** | heap_size を指定しないプログラムのレイアウトを 1 バイトも動かさない (回帰ゼロの要) |
| 1 枚に収まる要求 | 1 | 境界ちょうど (`fit`) は 1、`fit + 1 ページ` で 2 |
| 4MB 要求 | 2 | 帯を伸ばす本題 |
| 非常に大きい要求 | `MEM_APP_BAND_MAX_PDES` | 上限で頭打ち (PEGC のリニア窓を踏まない) |
| `ram_top` が小さい (8MB 構成相当) | 1 | 空き RAM が無ければ伸ばさない。入らなければ exec が `EXEC_ERR_NOMEM` で拒否する |
| `ram_top == 0` / 帯の底以下 | 1 | 最低 1 枚は必ず返す (従来の挙動を壊さない) |
| `code_end` が桁あふれするほど大きい | 1〜MAX の範囲内 | 算術のあふれで大きい枚数を返さない |

### B. 枚数 1 = 現行と完全に同じレイアウト (最重要)

`paging_addrspace_create(as)` (= `_n(as, 1)`) で:

- `app_pde == APP_BAND_PDE` / `app_pde_count == 1`
- 先頭 PDE **だけ**がアプリ PT に差し替わる (`PRESENT|RW`、USER はまだ立たない)
- 2 枚目の PDE は master と同じ値のまま = 共有
- アプリ PT は master の同帯 PT と同一 identity で始まる (V1)
- 2 枚目の帯への `map_user` は「共有帯」として master の PT に書かれる — 従来の動作
- 破棄で PD 1 + PT 1 がきっちり返る

### C/D. 枚数 2

- `used` が PD + PT 2 枚ぶんだけ増える。2 枚の PT は別物
- 両方の PDE がアプリ PT を指し、**master の PDE は無傷**
- 2 枚目のアプリ PT も master の同帯 PT の identity コピーで始まる
- 2 枚目に `map_user(PTE_USER)` → 書かれるのはアプリ固有 PT。
  **master の PT は 1 ビットも変わらない**
- USER は当該アプリの PD の PDE にだけ立ち、master の PDE には伝播しない (票 §2)
- PDE 境界をまたぐ `map_user_range` が両方のアプリ PT に落ちる
- 帯の外 (VRAM 0xA8000) は従来どおり master の共有 PT に書かれ、
  master の PDE には USER が付かない
- 破棄後、master の PDE / PT が元の値に戻っている

### E. 引数不正・物理ページ不足

- `pde_count == 0` / `> MEM_APP_BAND_MAX_PDES` / `as == NULL` は `-1`。1 ページも取らない
- 空きをちょうど 2 ページまで吸い出した状態で `_n(as, 2)` は `-1` を返し、
  **確保済みの PD と 1 枚目を返して**空きが 2 のままであること
  (途中確保を握ったまま失敗すると、失敗するたびに RAM が減る)
- その状態でも `_n(as, 1)` は成功する (PD + PT ちょうど 2 ページ)

### F/G. 自己診断

- `paging_app_band_selftest()` == 0 (ブート時 `kselftest` に載せているもの)
- 既存の `paging_map_user_keep_selftest()` / `paging_pd_clone_selftest()` が
  引き続き 0 (回帰ゼロ)
- どれも実行後に確保ページ数が元に戻る

## RED → GREEN の経過 (2026-09-10)

1. 実装前: `paging_app_band_pdes` / `paging_app_band_selftest` が未宣言、
   `MEM_APP_BAND_MAX_PDES` / `MEM_APP_BAND_PDE_SIZE` が未定義、
   `struct addrspace` に `app_pde_count` が無い — コンパイルエラーで RED
2. `paging.c` / `paging.h` / `memmap.h` 実装後: E の「2 枚目で尽きる」だけが赤。
   ホストの `pgalloc_alloc_n_pfn` 差し替えでは `pgalloc_alloc_page()` を
   止められなかった (pgalloc.c 内部の呼び出しは差し替え前の名前で解決される)
   ため。空きを実際に吸い出す形に検査を書き直して GREEN

## この試験が見ていないもの

- `exec/exec.c` のレイアウト決定 (ホストで動かすには依存が多すぎる)。
  純粋な算術部分だけを `paging_app_band_pdes()` に切り出して A で見ている
- ゲスト上の実挙動 (kselftest、`ring3_guard` の否定試験、8MB / 15MB 構成)。
  票 §5 のとおり PM が NP21/W で行う
