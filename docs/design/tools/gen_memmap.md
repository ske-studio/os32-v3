# tools/gen_memmap.py — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_KSTACK_USER (§4 の 1、§4-bis) の契約を今のソースと照らして要約
ソース: `tools/gen_memmap.py` (`read_symbols`・`load`・`defects`・`reversed_ranges`・`overlaps`・`escaped_children`・`budget`・`mirrors`・`render`・`main`)

番地の定義 (`sys/include/memmap.h` と `sys/platform/pc98/platform_boot_layout.h`) と、実ビルドの `build/out/kernel.map` のリンカシンボル (`__bss_end` など) から、絶対番地の地図を起こして矛盾を検査する。地図の説明の正典は `docs/02_memory.md` §2-1、ビルドの中での位置は `docs/08_build.md`。PC-98 の起動と低位の定義の読み方は `docs/design/platform/pc98/platform_boot_layout.md`。

`KHEAP_BASE` 以降 (ヒープ → KAPI → SHM → ガード) は `__bss_end` から芋づるで動く浮動番地で、カーネルが育つと固定番地の帯へ食い込む。手書きの地図や相対表記ではこの重なりが見えないので、実値で地図を起こして機械で見る。

## 出力と終了コード

| 呼び方 | すること | 終了コード |
|---|---|---|
| (無し) | 地図を標準出力へ | 0 |
| `--write` | `build/out/MEMMAP.md` を書く (コミットしない)。矛盾は警告として標準エラーに出すだけ | 0 |
| `--check` | 矛盾を `gen_memmap: <種類>: …` の行で名指しする | 矛盾があれば 1、無ければ 0 |
| `--headroom` | カーネルがあと何 KB 育つと何が壊れるか | 0 |
| `kernel.map` が無い・必要なシンボルが無い | **推測しない**。理由を標準エラーに出し、地図を 1 行も出さない | 2 |

- 地図の表の行は必ず絶対番地 (`0x…`) で始める。相対表記 (`+4KB`) の行を出さない。
- `--check` は生成物 `build/out/MEMMAP.md` と説明文書の中身に依らない (改変・欠落しても配置の検査の結果は変わらない。説明文書を書き換えない)。

## `--check` が見る矛盾

- **予算超過** (`budget`): カーネル本体 (`__kernel_resident_end` − 読み込み番地) が `MEM_KERNEL_IMAGE_MAX` を超える。予算の式は `memmap.h` の値をそのまま使い、ここで引き算をやり直さない。
- **逆転** (`reversed_ranges`): `start > end + 1` の帯。**空 (`start == end + 1`) は正常** (予約を使い切っただけ) で、逆転とは区別する。逆転は範囲の指定が壊れている印で、`paging_set_not_present` のような範囲指定が空振りする。
- **重なり** (`overlaps`): 帯どうしの交差。親子 (帯の内側の予約) と、アプリの仮想帯と物理の装置帯の組は数えない。
- **はみ出し** (`escaped_children`): 子の帯が親の帯の外にある (例: GUI 予約が SHM 本体の外)。
- **写しのずれ** (`mirrors`): C のヘッダを読めない相手と SDK に出る写し (`MIRRORS` の表: `build/os32.ld`・`sdk/link/*.ld`・`sdk/os32x_hdr.py`・`tools/mkshlib.py`・Rust のスタブ・ブートのヘッダとローダ) の値が `memmap.h` と違う。`sys/arch/x86/kernel/kentry.asm` は値を持たず生成ヘッダの名前 (`LAYOUT_MEM_KSTACK_TOP`) を引く形で、数値の直書きに戻ったら名指しで落とす。

検査: `make check-memmap-host` (`tools/tests/test_memmap_gen.py`)。実物の `sys/include/memmap.h` をそのまま使い、`kernel.map` だけを差し替えて壊れた地図 (予算超過・逆転・重なり) と直った地図を作る。ほかに、はみ出し・生成物の改変と欠落・写しのずれ・`kernel.map` 無し。`--mutate` は検査を 1 つずつ外した版で落ちることを見る。
