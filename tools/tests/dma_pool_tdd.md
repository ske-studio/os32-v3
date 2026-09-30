# dma_pool — RED → GREEN の記録

票: [`docs/tasks/v3/TASK_HAL_WIRING.md`](../../docs/tasks/v3/TASK_HAL_WIRING.md) §1-3
対象: `kernel/dma_pool_math.c` (表の算数) / `kernel/dma_pool.c` (唯一の池)
試験: `tools/tests/test_dma_pool.py` + `tools/tests/dma_pool_host.c`
実行: `make check-dma-pool-host` (`check-par` の列)

## なぜ普通のヒープでは駄目か

82557 の CB/RFD と CS4231 の PCM リングを置く場所が要る。`kmalloc` では
3 つ足りない。

1. **64KB バンクをまたぐと 8237 が折り返す** ([HW2])。ヒープは揃えを約束しない。
2. **割り込み文脈から取る / 返す**。82557 の巻き戻しは IRQ callback の中で
   `dma_pool_free` を呼ぶので、動的確保を挟めない。
3. **番地が全 PD で同じ**でなければならない。割り込みは「そのときの CR3」、
   つまりアプリの PD で走る。全 PD が共有するのは PDE 0 (0〜4MB) だけ
   (`kernel/paging.h` の契約 C2)。

だから 0〜4MB の中の**固定番地**を 4KB × 16 ページのビットマップで配る。
置き場は SQLite 帯の予約域 (NOT PRESENT) の中に開ける穴
`MEM_DMA_POOL_BASE` = 0x2E8000、64KB (0x2E8000〜0x2F7FFF)。

**この池は 0x2F0000 の 64KB 境界を跨ぐ。** 跨がない池なら以下の試験は全部
素通しなので、跨いでいること自体を `layout` で固定してある。
結果として **受付は 1〜32KB** — 境界の両側がそれぞれ 32KB なので、
33〜64KB は空の池でも必ず失敗する。「断片化のせいで今は取れない」と
読まれないよう、入口で `ERR_ARG` として断る (断片化は `ERR_NOSPC`)。

## RED

### RED 1 — 実装が無い

```
$ python3 -B tools/tests/test_dma_pool.py
tools/tests/dma_pool_host.c:17:10: fatal error: ../../kernel/dma_pool_math.c: No such file or directory
```

### RED 2 — またぐ候補で諦める (変異 2)

`if (dma_crosses_64k(addr, bytes)) continue;` を `return ERR_NOSPC;` にすると、
28KB を置いた後の 16KB が取れなくなる (0x2EF000 が跨ぐので、そこで探索を
やめてしまう)。**飛ばして後半 (0x2F0000) に置く**のが正しい。
`sizes` / `skip_boundary` / `frees` の 3 件が落ちる。

### RED 3 — LEAKED を空きに戻す (変異 6)

`mark_leaked` が span を `DMA_SPAN_FREE` にすると、**装置がまだ書いている
かもしれないページ**が配り直される。番地だけ見ていると気づけない
(`used[]` はまだ 1 なので次の alloc には出ない) ので、`leaked` ケースは
**span の表そのもの**を数える。

## GREEN

```
$ python3 -B tools/tests/test_dma_pool.py --target --mutate
HOST GNU89 -Werror compile PASS (real kernel/dma_pool_math.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT layout=0
EXIT sizes=0
EXIT skip_boundary=0
EXIT alignment=0
EXIT frees=0
EXIT leaked=0
EXIT exhaust=0
SUMMARY 7/7 PASS
MUTATION 1 RED (1 件): 64KB またぎの候補を飛ばさない (0x2F0000 を跨いだ番地を装置へ渡す)
MUTATION 2 RED (3 件): またぐ候補で**探索を諦める** (後半が空いていても取れない)
MUTATION 3 RED (1 件): 33KB〜64KB を受ける (空の池でも必ず失敗するのに呼び手を待たせる)
MUTATION 4 RED (1 件): span の先頭でなくても解放する (隣の span を巻き添えに外す)
MUTATION 5 RED (1 件): LEAKED / 二重解放を通す (装置がまだ書いているページを配り直す)
MUTATION 6 RED (1 件): mark_leaked が空きに戻す (止まった証拠の無いページを再利用する)
MUTATION 7 RED (1 件): 整列の要求を無視する
MUTATION 8 RED (compile): 2 の冪でない整列を受ける (剰余の意味が壊れる)
MUTATION 9 RED (1 件): dma_pool_init より前に配る (写像が張られていないページを装置へ渡す)
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `layout` | 0x2E8000 / 64KB / 16 ページ。**池が 0x2F0000 を跨ぐこと**。カーネルスタックガード (0x2FB000) を越えないこと。受付上限が境界の片側ぶん (32KB) であること |
| `sizes` | 16KB は空の池に必ず入り境界を跨がない / **33KB・64KB は `ERR_ARG`** (`ERR_NOSPC` ではない) / **32KB × 2 が空の池に入る** / 0 バイトと非 2 冪の整列を断る |
| `skip_boundary` | 28KB の次の 16KB が、跨ぐ 0x2EF000 を**飛ばして** 0x2F0000 に乗る。飛ばした 1 ページは後で 4KB として出る |
| `alignment` | 既定 4KB / 8KB 整列 / 32KB 整列 (池の中に 2 か所だけ) |
| `frees` | 3 本並べて**真ん中だけ**返す → そこに 8KB が入る。途中ポインタ・ページ境界でない番地・池の外・二重解放・**隣接 span の先頭でない番地**をすべて `bad_free` に数える |
| `leaked` | LEAKED は span 表に残り (`FREE` に戻らない)、free も二度目の mark も断る。残り半分だけが配られる |
| `exhaust` | 16 ページ全部出て 17 本目は無い → 1 本返すと**そこが**再び出る。初期化前は `ERR_STATE` (`ERR_NOSPC` ではない) |

## 実機でしか確かめられない残り

ここは表の算数だけ。**写像** (0x2E8000〜0x2F7FFF が present / RW /
supervisor で、上下が NP、USER が立っていないこと) は `kernel/paging.c` が
張り、`kernel/kselftest.c` の MM 検査が起動ごとに見る (`MM_RWU` を
`MM_RW` と分けたのはそのため)。プールを実際に DMA に渡した結果は W2 / L-B。
