# TASK_MAGIC_NUM — 低レベルのマジックナンバーに名前を付ける ([C4]、v3 で足された行が中心)

> 上位: [ROADMAP](../../ROADMAP.md)・規則 [C4] ([POLICY_DEV](../../POLICY_DEV.md) §2「定数定義の3層管理」)

> 状態: **受入完了 (2026-10-25)** — M1〜M5 が main。機械語は sqlite_engine の契約違反の直し (2 関数) と CD の ABI 世代の値 (fs/vfs.h の全文ハッシュ) のほかは変わっていない。洗い出しの表 (file:line・値・直し) は `~/os32-tmp/run/magic/survey_{kernel,exec,hwdev,kdebug}.md`。

## 0. ユーザー指示 (2026-10-25)

「低レベル処理を中心にマジックナンバーの洗い出し。修正箇所を洗い出してから修正票を書き出し、修正してください。特に v3 から追加されたものなど。」

## 1. 洗い出しの結果

範囲は fork (a238ab1) 以後に足された・変わった行。対象は kernel・exec・kapi・kdebug・hwdev・drivers・gfx・fs・include・arch・boot で、生成物と試験は除いた。区分は次の 3 つ。

- **A**: 名前が既にあるのに、数字か別名で書いている。
- **B**: 名前が無く、同じ値が 2 か所以上にあるか、意味が読めない。
- **C**: 1 か所だけの局所の値。

| 領域 | A | B | C | 目立つもの |
|---|---|---|---|---|
| kernel | 9 | 22 | 11 | **sqlite_engine.c:32,41 が irq_save() の戻り値の IF (0x200) を読む (io.h:96 の「不透明な値」の契約に反する)**、paging.c の `>> 22` が 28 か所、モジュール形式の欄の位置と FNV の定数、TVRAM/KCG の値、例外ベクタ 8/14 |
| exec・kapi | 6 | 9 | 13 | `MEM_APP_BAND_BASE >> 22` を APP_BAND_PDE で書ける、`~0xfffUL`、`frame[7]` を APP_FRAME_EAX で書ける、KAPI 表の先頭 2 語とスタブ 8 バイト、スタックの 16 バイト揃え |
| hwdev・drivers・gfx・fs・include | 12 | 17 | 10 | 同じ番地や値に複数の名前がある (0x054C/0x054D、MFF2 と PEGC_FF2、ページの大きさ 4 名、Ethernet の定数が SFS_LAN/E82557/NE2K に分かれている)、e82557 の stats の添字・記述子の欄・SCB のシフト、iso9660 の PVD の欄、vfs の `fd < 3` |
| kdebug | 15 | 24 | 12 | recover_gf.c の targets[] に名前の有るポートと番地を数字で書いている、init script の命令コード 27 種、BAR の数 3 が 32 か所、回復の予算の値が 3 ファイルに重複、PLL のビット、GPU の位置と ID が 4 か所に定義されている |

## 2. 方針 (PM 決定)

- **直すのは A と B。C は段の表に挙げたものだけ** (asm の構造体の位置など、ずれると壊れるもの)。
- **機械語は変えない**。sqlite_engine の契約違反の直しだけが例外。各段では、変える前と変えた後の像 (vmkernel・kdebug.bin・*.mod・触ったユーザーランド) の機械語を `objdump -d` で比べる。差は 0 か、意図したものとして説明が付くものだけにする。この比べが受入の主な証拠で、名前の付け替えは試験より強くこれで確かめる。
- 1 つの意味に名前は 1 つにする。重複している名前は正典を 1 つ決め、もう片方は正典を使って定義するか消す。番地の正典は memmap.h、PC-98 の I/O とワークエリアは pc98.h、PCI は drivers/pci_decode.h、Ethernet は新しい include/net_eth.h、回復処理は kdebug/recover.h。
- ABI と生成物には手で触らない ([ABI1])。sdk/kapi.json と生成物は対象外。module_schema.h の名前は生成器 `tools/gen_module_schema.py` から出す。
- 直さないもの:
  - memmap.h で同じ値が別の名前で並んでいる番地。gen_memmap.py と STATIC_ASSERT が関係を検査しているので、地図として直書きのままにする。
  - 配置を固定する STATIC_ASSERT の期待値。
  - カーネル内の自己試験の失敗ビット。
  - recover_gf.c の表の中身の数 (約 7,000)。ただし表の外から添字で参照している所は M5 で直す。
  - kernel.c の起動画面の座標。
  - fork 前からある行。例外として、同じ群の直しで同じファイルに並ぶ行は一緒に直す (`kprintf(0x07` は v3 で足された行と、段で触るファイルの行だけ。リポジトリ全体の約 170 か所は送る)。

## 3. 段

段は触るファイルが重ならないように分けた。M1 が共有ヘッダの名前を作るので先に入れ、M2〜M4 は M1 の後に並べて出す。

| 段 | 中身 | 触る所 | 実装 | 状態 |
|---|---|---|---|---|
| M1 | 共有の名前と kernel・exec・kapi | include/{types,kprintf,cpu,memmap,pc98}.h、kernel/paging.h・idt.h・v86.h、drivers/kcg.h、lib/utf8.h、kernel/ (module_image.c・module_runtime.c を除く)、exec/、kapi/ | sol | **main** (a23754f6、astra 取り込み可・PM の直し: asm の依存、変異の目印 6 本) — 差は sqlite_engine の 2 関数だけ |
| M2 | drivers・fs・gfx とヘッダの重複 | drivers/ (pci_decode.h を除く)、fs/、gfx/、include/{crt_diag,pegc,net_eth}.h と pc98.h の BIOS ワークの番地 | sol | **main** (a23754f6、astra P2 の直し: 変異の目印 2 本) — 12 ELF で差 0、CD の ABI 世代の値だけ変わる (PM 判断で可) |
| M3 | モジュール形式の数 | tools/gen_module_schema.py → kernel/module_schema.h、kernel/module_image.c・module_runtime.c、tools/mkmod.py・gen_kdebug_imports.py の FNV の定数 | sol | **main** (a23754f6、astra 取り込み可) — 12 ELF と *.mod・kdebug.bin の出力が同じ |
| M4 | kdebug の A と重複定義 | kdebug/ (gfinit_decode.*・gfinit_pll.*・gfinit_plan.*・recover_gf.* を除く)、drivers/pci_decode.h | sol | **main** (30a92a0f、astra 取り込み可・PM の直し: 変異の目印 1 本) — 12 ELF で差 0 |
| M5 | kdebug の init script と PLL | kdebug/gfinit_decode.*・gfinit_exec.c・gfinit_pll.*・gfinit_plan.*・recover_gf.*: 命令コードの enum、状態の符号化、PLL の表とビット (名前は envytools の rnndb と nouveau に合わせる)、plan と REC_GF_OP の enum、shift の判定の重複を 1 つに、targets[] と rules[] を外から添字で参照している所の enum | sol (多ければ astra) | **main** (250b8beb、M5a と M5b に分けた、どちらも astra 取り込み可) — 名前は #define (HWDEV の契約が enum の列挙子を数えるので)。12 ELF の .text・.rodata・.data が一致、変異の RED 356 で同じ。意味を確かめられない PLL のビットは数字のまま |

各段の完了条件は次のとおり。

- `make all` が rc=0。
- 機械語の比べ (§2) の結果。
- `check_select.py --select --files <差分>` の候補をすべて `MUTATE=0` で rc=0。
- `check_select.py --lint` が rc=0。

レビューは astra。取り込みは check-changed (BASE=e4681ff)。

## 4. 送り

- 取り込みで分かったこと: 名前の置き換えは、変異の置換の目印 (試験の中の元の式の文字列) を壊す。MUTATE=0 の候補検査では見つからず、取り込みの変異込みの検査で初めて落ちた (M1 で 5 本)。確かめる道具は `~/os32-tmp/bin/stale_anchor.py <基点>` — 試験の文字列のうち、基点では変えたファイルにあり今は無いものを挙げる。M4 以後は実装者が最後に回した。

- リポジトリ全体の `kprintf(0x07` 約 170 か所 (fork 前の行) を `KLOG_ATTR` にする。機械的に置き換えられるので、M1〜M5 の後に 1 段で出す候補。

## 5. 追補: fork 前からある `kprintf(0x07, …)` (ユーザー指示 2026-10-25「出してください。タスクの粒度が大きく成りすぎないように」)

残りは約 145 か所 (受入完了 2026-10-25、残り 0 か所)。次の 2 つは除く。

- `lib/sqlite3/sqlite3.c`: 取り込んだ SQLite の合本で、手を入れる行を増やさない。
- `apps/`: H-7 で再開待ち。

段は触るファイルが重ならないように 3 つに分け、並べて出す。機械語の比べは §2 と同じ。

PM 決定: 名前は 1 つにする。`KLOG_ATTR` を共有の層 `sdk/include/os32/os32_kapi_shared.h` (`ATTR_*` の隣) へ移し、`include/kprintf.h` はそれを使う。FEP の ABI 世代の値は、このヘッダの全文から作るので変わってよい (M2 の CD と同じ扱い)。

| 段 | 中身 | 触る所 | 実装 | 状態 |
|---|---|---|---|---|
| K1 | kdebug の自己試験 25 か所 | kdebug/kselftest.c | sol | **main** (2ac5fc6c、astra 取り込み可・PM の直し: 試験の偽ヘッダに定義 1 行ずつ) — .text は差 0 (K3 は FEP の ABI 世代の値だけ) |
| K2 | カーネル側 約 35 か所 | kernel/・drivers/・fs/・net/・lib/os32_sqlite_vfs.c・lib/kprintf_attr.c (コメント) | sol | **main** (2ac5fc6c、astra 取り込み可・PM の直し: 試験の偽ヘッダに定義 1 行ずつ) — .text は差 0 (K3 は FEP の ABI 世代の値だけ) |
| K3 | 名前を共有の層へ移し、ユーザーランド側 約 70 か所 | sdk/include/os32/os32_kapi_shared.h・include/kprintf.h・userland/・sdk/example/・tools/tests/ の host の .c | sol | **main** (2ac5fc6c、astra 取り込み可・PM の直し: 試験の偽ヘッダに定義 1 行ずつ) — .text は差 0 (K3 は FEP の ABI 世代の値だけ) |
