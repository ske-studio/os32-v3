# メモリ地図の生成と照合 — TDD 記録 (2026-09-17)

票: [`docs/archive/kernel_v21/TASK_KSTACK_USER.md`](../../docs/archive/kernel_v21/TASK_KSTACK_USER.md) §4 の 1・2・3 と §4-bis

対象: `tools/gen_memmap.py` / `kernel/paging.c` / `kernel/kselftest.c` / `include/memmap.h`
試験: `tools/tests/test_memmap_gen.py` / `tools/tests/test_memmap_boot.py` + `memmap_boot_host.c`
登録: `make check` → `check-memmap-host` / `check-memmap` (2026-09-17 決裁 D1/D2 の後に登録)

## 症状

`userland/tests/crash.c` がカーネルスタックガード `0x1FB000` へ書いても落ちない。
生きているゲストのページ表を読むと、設計と実装がこうずれていた。

| 区間 | `memmap.h` の地図 | 実物 |
|---|---|---|
| `0x1FB000` スタックガード | NP | present (SHM 本体) |
| `0x1FC000`-`0x1FDFFF` スタック下 8KB | present | present (SHM 本体) |
| `0x1FE000` スタックの真ん中 | present | **NP** (SHM 後方ガード) |

原因は番地が浮いていること。`KHEAP_BASE` は `__bss_end` から計算され、
ヒープ → KAPI → SHM が芋づるで動く。カーネルが育った結果、SHM の終端が
固定番地のスタックに達した。地図は 3 か所 (`memmap.h` 先頭コメント /
`docs/02_memory.md` / `CLAUDE.md`) に手書きされ、3 つとも
「カーネル本体 ~220KB」(実測 432KB) と書いていた。
しかも `docs/02_memory.md` は SHM を「`+4KB` - `+260KB`」と**相対表記**で
書いていたので、読み手が暗算しない限り重なりは見えなかった。

## なぜ誰も気づかなかったか

1. `STATIC_ASSERT` は「ガードとスタックが隣接」しか見ていない。
   **SHM がスタックと重ならないことを誰も検査していなかった。**
2. `paging_set_not_present(MEM_SHM_RESV_START, MEM_SHM_RESV_END)` は
   `0x1FF000 > 0x1FAFFF` で逆転している。関数は `-1` を返していたが、
   **呼び側が戻り値を見ていない**ので空振りが成功に見えた。
3. 地図が 3 か所にあり、どれも実値ではなく相対表記だった。

## RED → GREEN

### 1. `tools/gen_memmap.py` (静的 — 設計が矛盾していないか)

`include/memmap.h` の `#define` を解き、`build/out/kernel.map` の `__bss_end`
から実値の地図を起こす。**実物の `memmap.h` を使い、`kernel.map` だけ差し替える**
ので、番地を直しても試験は腐らない。

| 場面 | `__bss_end` | 期待 |
|---|---|---|
| BROKEN | `0x180000` (予算超過) | `--check` = 1。予算超過 1 + 逆転 1 + 重なり 3 を名指し |
| CLEAN | `0x140000` (予算内) | `--write` → `--check` = 0 |
| ARTIFACT | `0x140000` | `build/out/MEMMAP.md` の改変・欠落でも `--check` = 0。再生成で復元し、説明文書を変えない |
| MIRROR | `0x140000` | `build/os32.ld` の写しをずらす / `kentry.asm` を数値直書きに戻すと `--check` = 1 |
| NOMAP | (`kernel.map` 無し) | **推測せず** 2 で止まり、地図を 1 行も出さない |

RED だったこと: どれも道具そのものが無かった。とくに NOMAP —
`__bss_end` を決め打ちにすると「それらしい嘘の地図」が出る。今回の穴と
まったく同じ失敗をもう一度書くことになるので、**止まる**のが正しい。

否定側 (`--mutate`、3/3 RED):

| 変異 | 何を外すか | 落ちる場所 |
|---|---|---|
| `no-overlap` | 重なりを見ない | BROKEN が「矛盾なし」になる |
| `no-reversed` | 逆転を見ない | `MEM_SHM_RESV` の逆転を見逃す |
| `no-mirror` | 写しを照合しない | `memmap.h` だけ直して `os32.ld` が古いまま通る |
| `guess-map` | `kernel.map` が無いとき `__bss_end` を決め打ち | NOMAP で地図を出してしまう |

**空 (`start == end + 1`) と逆転 (`start > end + 1`) を区別する。** 前者は予約域を
使い切っただけで正常、後者は範囲の指定が壊れている。2026-09-17 の
`MEM_SHM_RESV` は `0x1FF000 > 0x1FAFFF` で 4 ページぶん逆転していた。

### 2. `paging_memmap_selftest()` (動的 — 実装が設計どおりか)

PDE 0 の PTE 1024 本を `memmap.h` から導いた期待値と比べ、食い違った区間を
`paging_memmap_bad[]` に積んで `kselftest_fail` に数える。
`tools/tests/memmap_boot_host.c` が `kernel/paging.c` を**そのまま** `#include` し、
`paging_init` → `paging_reclaim_conventional` → `shm_init` 相当 → KAPI 踏み台、
というカーネルの起動順を再生してから呼ぶ。ゲストを起動せずに件数と区間が確定する。

| 場面 | `__bss_end` | 撥ねた逆転 | 食い違い |
|---|---|---|---|
| 典型 (今のカーネルとほぼ同じ) | `0x16C560` | 0 | **0 本** |
| 予算ちょうど (後方予約が空) | `0x175000` | 0 | **0 本** |
| 予算を 1 ページ超過 | `0x176000` | 1 | 1 本 (`0x200000` 期待 RW / 実物 NP) |

3 つめが肝心。**リンカの `ASSERT` が本来ここまで来させない形**だが、もし来たら
SHM 後方ガードがカーネル帯域を突き抜けて SQLite 帯の先頭ページを not-present に
する。自己診断がそれを見られるのは、期待値が **「帯の境界 `MEM_KERNEL_BAND_END`
は固定番地」** を守っていて、育ちすぎた浮動番地に境界を越えさせないから。
変異 `band-unbounded` はまさにそこを崩す。

### 決裁 D1/D2 の前は

同じ試験を移設前の配置で回すと、`__bss_end = 0x16BF20` (2026-09-17 の実測) で
食い違い 2 本 (`0x1FB000` 期待 NP/実物 RW、`0x1FE000` 期待 RW/実物 NP) と
逆転 1 回が出た。さらに **あと 224 バイト育つと** SHM 後方ガードが
「実際に使っているスタックページ」`0x1FF000` に乗り、`shm_init` の直後の
`push` で三重フォルトするところまで来ていた。

否定側 (`--mutate`、3/3 RED):

| 変異 | 何を壊すか | なぜ落ちるべきか |
|---|---|---|
| `no-reject` | 逆転を撥ねても数えない | 空振りが成功に見えていた元の姿 |
| `band-unbounded` | 浮動番地の規則を帯の外へも広げる | 自己診断そのものが穴を隠す |
| `resv-blind` | 空と逆転を区別せず空の予約域にも範囲指定を投げる | 正常な形を異常として数える |
| `no-coalesce` | 食い違いを 1 本にまとめない | 「4KB のずれ」と「帯ごと違う」が同じ顔になる |

`band-unbounded` が**この試験のいちばん大事な一本**。期待値の 1 行から
`a <= MEM_KERNEL_BAND_END` を外すだけで、期待値が壊れた定数から作られるように
なり、自己診断は予算超過を「期待どおり」と読む。検査を足しても、
**期待値の作り方を間違えれば何も見えない**。

否定側は **全部の場面で** 回す。1 場面だけだと、その場面では無害な変異を
「試験が見逃した」と取り違える (`resv-blind` は「予算ちょうど」でしか差が出ない)。

### 3. `shm_init()` の写しについて

ホストの gcc 15 は `(u32)&__bss_end` を含む定数式を畳まなくなっており
(クロスの gcc 13.2 も `-Werror` では同じ)、`kernel/shm.c` の `STATIC_ASSERT` が
`variably modified at file scope` になるので `#include` できない。
ページ表に対する 3 つの呼び出しだけをハーネスに写し、
**写しが本物とずれていないことを `test_memmap_boot.py` が
`kernel/shm.c` の `shm_init()` 本文と突き合わせる** (`check_shm_replay`)。

## 決裁 D1 / D2 (2026-09-17) で何が変わったか

### D1 — 配置

| | 前 | 後 |
|---|---|---|
| 共有メモリ | 256KB (16 ブロック) | **224KB (14 ブロック)** |
| GUI 予約 | `MEM_SHM_BASE + 0x30000` の決め打ち | **常に末尾 4 ブロック** (`MEM_SHM_GUI_OFFSET`) |
| カーネルスタック | `0x1FC000`-`0x1FFFFF` | **`0x2FC000`-`0x2FFFFF`** |
| スタックガード | `0x1FB000` | **`0x2FB000`** |

要点は「浮いた番地 (`KHEAP_BASE` 以降) と固定番地を同じ帯で隣り合わせにしない」。
カーネル帯域は丸ごと浮動側に渡し、スタックは固定番地しか無い SQLite 帯域の
末尾へ出した。予算 468KB に対して本体 433KB。

### D2 — 門をリンカに置く

`KHEAP_BASE` は浮かせたまま。代わりに `build/os32.ld` の

```
ASSERT(__bss_end <= KERNEL_LOAD_ADDR + MEM_KERNEL_IMAGE_MAX, "...")
```

が上限をリンク時に保証する。**上限が保証されれば「予算いっぱいまで育った
場合の配置」が定数式で書ける**ので、重なりの `STATIC_ASSERT` が
`#ifdef` 無しで有効にできる。実値 (`KHEAP_BASE`) は定数式ではないので
`STATIC_ASSERT` では扱えない — 条件が真でも
`variably modified at file scope` で落ちる。

生き返った表明:

| ファイル | 表明 | 何を見るか |
|---|---|---|
| `kernel/shm.c` | `shm_gui_base_aligned` | GUI 予約の先頭がブロック境界に乗る |
| `kernel/shm.c` | `shm_gui_within_band` | GUI 予約が SHM 帯に収まる |
| `kernel/shm.c` | `shm_gui_is_last_blocks` | 予約が帯の **末尾** にある (新規) |
| `kernel/paging.c` | `shm_band_within_kernel_band` | 最悪配置でも SHM 帯がカーネル帯域に収まる |
| `kernel/paging.c` | `shm_resv_not_reversed` | 最悪配置でも後方予約が逆転しない (空は許す) |
| `kernel/paging.c` | `shm_band_below_kstack_guard` | 最悪配置でも SHM はスタックガードに届かない |
| `kernel/paging.c` | `kstack_outside_shm_band` | スタック 16KB 全部が SHM 帯の外 |
| `kernel/paging.c` | `kstack_outside_kernel_band` | スタックが浮動番地の帯の外に居る (D1 の要点) |

`shm.c` の 2 本は 2026-09-17 まで **黙って死んでいた**。条件に
`(u32)&__bss_end` 由来の値が入っていたため、GCC が可変長配列として警告だけ
出して通していた (本番ビルドのログに毎回出ていた)。
`MEM_SHM_GUI_OFFSET` (純粋な定数式) で書き直して生き返らせた。
**表明の条件に浮動番地を混ぜない** — これが今回いちばん再発しやすい形。

### 値を二重に持つ場所と、その照合

C のヘッダを読めない相手 (リンカスクリプト / NASM) と、SDK として外へ出る
写しにだけ二重定義を許し、一致は `gen_memmap.py --check` (`make check`) が見る。

| 場所 | 写している値 |
|---|---|
| `build/os32.ld` | `KERNEL_LOAD_ADDR` / `MEM_KSTACK_TOP` / `MEM_KERNEL_IMAGE_MAX` |
| `kernel/kentry.asm` | 値ではなく `os32.ld` の絶対シンボルを `extern` で引く (数値直書きに戻したら検出) |
| `sdk/include/os32/os32_gui_shared.h` | `GUI_SHM_OFFSET` = `MEM_SHM_GUI_OFFSET` |
| `sdk/rust/os32api/src/gui/proto.rs` | 同上 (C ⇄ Rust は `make check-gui-proto` も見る) |

## 確かめていないこと

- **ゲストで動かしていない。** 上の表はホストで起動順を再生した結果で、
  NP21/W 上の kselftest 出力ではない。
- 票の受入 G1 (`crash` が落ちる) / G4 (スタックを深く使う 16KB) / G7 (回帰) は
  実機でしか確かめられないので試験していない。`crash` の書き込み先が
  `MEM_STACK_GUARD` (= `0x2FB000`) を指すことはコンパイルで担保されているだけ。
- `make all` / `make check` 全体は回していない (`make kernel` と
  `make programs` と個別ターゲットのみ)。
