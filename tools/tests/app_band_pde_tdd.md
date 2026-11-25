# app_band_pde — アプリ帯の AS (疎な私有 PT) のホスト TDD

契約: `docs/design/mm/paging.md` の「アプリ帯の AS」(出所は票 APP_BAND_PDE)
実行: `make check-memory-host` / `python3 -B tools/tests/test_app_band_pde.py`
三点セット: `app_band_pde_host.c` (検査本体) / `test_app_band_pde.py` (ビルドと実行) /
このファイル (何を見ているか)。

## 何を検証するか

アプリ帯 (`MEM_APP_BAND_BASE` から `MEM_APP_BAND_MAX_PDES` 枚の PDE) の AS を、
実物の `sys/kern/mm/paging.c` (`paging_addrspace.inc`・`paging_lease.inc` を含む) をホストで
動かして検査する。ゲスト (NP21/W) の検証ではない。

当初の票は「PDE を 1 枚から要求量に応じて 2 枚へ増やす」(`paging_app_band_pdes()` が
枚数を決める) 設計だった。その機構は撤去され、今は AS の作成で PD だけを取り、
アプリ帯の私有 PT は写像のときに 1 枚ずつ確保する (疎)。この試験は今の形を見る。

## 作り

- `sys/kern/mm/paging.c` を読み、特権命令 (`mov %cr3` / `mov %cr0`) だけを
  ホスト変数への代入に置換して `paging_host_source.c` として書き出す。
  `pgalloc_alloc_phys` は失敗の位置を指定できる差し替え (`fail_at`) を通す
- `sys/kern/mm/pgalloc.c` は `irq_save` / `irq_restore` を潰して同様に書き出す
- `sys/arch/x86/kdebug/kselftest/ledger.inc` の台帳の自己診断も同じ実行物へ取り込む
- `-m32 -march=i386 -std=gnu11 -ffreestanding` で ILP32 としてビルドし、
  libc に依存せず直接走らせる

17MB 構成の池の backing を `mmap` (int 0x80 #90) で恒等に張り、
`paging_init` と池の起動を 17MB・8MB・16MB の各構成で行う。

## 検査項目

### A. 作成直後の AS (17MB / 8MB の両構成)

- `paging_addrspace_create` の直後、所有者のページは PD 1 枚だけ。
  アプリ帯の PT も lease 窓の PT も 0 枚
- 破棄でデータ以外が返り、台帳の回収で残り 2 枚 (データ) が戻る。池の空きは作成前と同じ
- `paging_addrspace_create_lease` は PD + lease 窓の先頭 PT の 2 枚
- 台帳の自己診断 (`test_ledger`) が通り、空きが元へ戻る

### B. 疎な私有 PT と master の隔離

- 2 つの AS を作り、片方の帯の 3 か所 (プログラムの先頭・exec_heap の起点・stack の最上ページ)
  に `map_user(PTE_USER)` すると、それぞれの PDE に PT が 1 枚ずつ確保される。
  もう一方の AS と master の PDE は 0 のまま
- 私有 PT の PTE は `phys | RW | USER`。所有者のページは PD + lease PT + PT 3 枚 + データ 3 枚
- 各ページを `free_user_range` で返し、両 AS を破棄すると空きが元へ戻る
- `paging_app_band_selftest()` == 0 (kselftest に載せているもの)。実行後に空きが元へ戻る

### C. 確保の失敗

- `create_lease` の 1 枚目・2 枚目の確保を失敗させると -1、所有者のページは 0、空きは不変
- PDE 境界をまたぐ `map_user_range_phys` で 1 枚目・2 枚目の PT の確保を失敗させると -1。
  部分的な PT は残らず (`app_pt_phys[0]`・`[1]` が 0)、PD の帯の PDE も 0

### 変異

`single-PDE` (帯の枚数を 1 に)・`master-USER` (USER を master の PDE に)・
`PT-destroy-leak`・`partial-PT-leak`・`kselftest-old-AS-count` (自己診断の旧い枚数の期待)
をそれぞれ実行時に検出する。

## RED → GREEN の経過 (2026-09-10、当初の 1〜2 枚の版)

1. 実装前: `paging_app_band_pdes` / `paging_app_band_selftest` が未宣言、
   `MEM_APP_BAND_MAX_PDES` / `MEM_APP_BAND_PDE_SIZE` が未定義、
   `struct addrspace` に `app_pde_count` が無い — コンパイルエラーで RED
2. `paging.c` / `paging.h` / `memmap.h` 実装後: 「2 枚目で尽きる」だけが赤。
   ホストの `pgalloc_alloc_n_pfn` 差し替えでは `pgalloc_alloc_page()` を
   止められなかった (pgalloc.c 内部の呼び出しは差し替え前の名前で解決される)
   ため。空きを実際に吸い出す形に検査を書き直して GREEN

## この試験が見ていないもの

- `sys/kern/exec/exec.c` の起動時のレイアウト決定と、アプリ帯への初期写像
- ゲスト上の実挙動 (kselftest、`ring3_guard` の否定試験、8MB / 15MB 構成)。PM が NP21/W で行う
