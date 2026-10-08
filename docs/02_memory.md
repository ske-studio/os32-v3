## 第2部 メモリマップ

### 開発方針と現在の実装上限

- **最低動作環境の数値は未確定**。旧物理固定配置での9.6MB下限説明
  ([tasks/gui/DESIGN.md §9.3](tasks/gui/DESIGN.md)、2026-09-04) は当時の設計記録。
  現行は高位 VA と物理 pool を分離し、旧ホットデプロイ窓も撤去済み。
  f12 の8MBホスト証拠をゲストや全バックエンドの最低動作環境へ一般化しない。
- **CPL=3 は高位 VA の私有 AS**。image/libc は `MEM_EXEC_LOAD_ADDR` (0x80100000)、
  exec_heap は `MEM_EXEC_HEAP_BASE` (0x88000000)、可変 stack は `MEM_APP_STACK_TOP` の下に置く。
  PT は使用 PDE ごとに疎確保し、PD + lease 先頭 PT + 使用 PDE 集合 (重複は1回) +
  shlib data + 実 data pages が起動予算。旧 `paging_app_band_pdes()` と物理0x00C00000天井は f12 で撤去。
  VA または実 free pages が不足すれば切り詰めず `EXEC_ERR_NOMEM`、失敗時は全返却する。
  8MB はホストで起動予算を確認済み、ゲスト構成確認は H-4 に持越し。
- **物理 pool の供給と USER の VA は別管理**。装置窓は RAM と区別し、
  boot の `gfx_boot_reserve` がバックバッファを池から確保する。旧物理末尾の
  ホットデプロイ窓は撤去済み。PEGC 640x480 の9MB下限という2026-09-09の実測は
  旧物理固定配置での記録であり、現行構成の下限は構成別ゲスト確認 (H-4) で判断する。
- GUI の必要 RAM はバックエンド・常駐領域・アプリの実測から別途定義する。
  GUI 開発を特定の容量に収めることや、8MB GUI 互換維持を暗黙の受入条件にしない。
- 32bit フラットアドレス空間の設計対象は 4GiB（0x00000000〜0xFFFFFFFF）。
  これは全域が利用可能 RAM、単一アプリの利用可能領域、または実装済み容量を意味しない。
  ROM・MMIO・予約帯・ガード・カーネル領域を区別し、物理 RAM の利用可能範囲を管理する。
- 下記の 16MB RAM 管理・32MB マッピングは現行実装の制約であり、製品の設計上限ではない。
  大容量対応には RAM 検出、ページ割当、ページ表、exec 配置、デバイス窓、媒体・転送用予約、
  境界演算を一貫して拡張し、検証する。定数だけの拡大で対応済みとは扱わない。
- 4GiB の exclusive end は u32 に格納できない。上端・長さの計算は広い整数または
  明示的なページ数表現を用い、wrap による範囲検査の通過を禁止する。
- 現開発段階では ABI 変更を許容し、kernel・SDK・userland・apps・game の
  クリーン再ビルドと配備整合性で揃える。既存データの保全とは別の方針である。

### §2-1 物理メモリ配置

> 番地の定義の正典は `include/memmap.h`。ここが食い違ったらそちらが正しい。
> カーネルスタックは 0x90000 → 0x1FC000 (V86 ゲストに 640KB を渡すため) →
> **0x2FC000** (2026-09-17 決裁 D1) と動いてきた。最後の移設は
> 「浮いた番地と固定番地を同じ帯で隣り合わせにしない」ため —
> カーネル帯域は `KHEAP_BASE` 以降が `__bss_end` 由来で浮くので、カーネルが
> 育つと共有メモリが固定番地のスタックへ食い込む
> (票 [`archive/kernel_v21/TASK_KSTACK_USER.md`](archive/kernel_v21/TASK_KSTACK_USER.md))。
> 0x9FFFC は今もローダー段の ESP として使われるが、カーネルは `kentry.asm` で
> `MEM_KSTACK_TOP` に張り替える。
>
> カーネル本体の上限は `MEM_KERNEL_IMAGE_MAX` で、**`build/os32.ld` の
> `ASSERT` がリンク時に守る**。超えるとリンクが失敗し、何をすればよいかを
> メッセージが言う。`make check` の `check-memmap` は、その予算・帯どうしの
> 重なり・範囲の逆転・写しのずれ・定数の整合をまとめて見る。生成文書との一致は検査しない。

実ビルドの地図は `build/out/MEMMAP.md` に生成する
([生成文書の見方](INDEX.md#生成文書の見方))。定義は `include/memmap.h`、
実ビルドの数値は `build/out/kernel.map` の `__bss_end` / SQLite の境界から求める。
表は全て絶対番地とし、カーネル本体の使用量・残り予算・成長時に動く帯も表示する。
`__bss_end` が伸びると `KHEAP_BASE` 以降の KAPI / SHM / ガードも動くため、
数値の地図はコミットせず、説明をこの節に残す。

#### 動的レイアウト (番地が実行時に決まるので生成できない部分)

生成される地図に出るのは `include/memmap.h` と `kernel.map` で決まる固定の番地だけ。
以下は搭載メモリ量・バイナリの大きさ・起動の仕方で変わるので手で持つ。

```
[ USER の高位 VA (memory_layout 世代3) ]
0x80000000 - 0x800FFFFF            shlib text RO共有 / data 私有複製
0x80100000 - img_end               .text + .data + .bss
img_end - sbrk_end                 libc 初期量: BSS 端の端数 + 1 page、RW
sbrk_end = page_up(img_end)+PAGE_SIZE = guard_a (1 page NP)
0x88000000 - exec_heap_end         exec_heap 初期量: 未指定64KiB、明示値はページ丸め/最低64KiB
stack_top-stack_size-PAGE_SIZE     guard_b (1 page NP)
stack_top-stack_size - stack_top   可変 stack 全量 RW
```

実行中の libc EXACT 伸長・副 arena・大塊と exec_heap の追加領域は `mem_map` を使う。
固定初期域と extent の回収を分け、終了後の leftover は0。
resident shell/gshell は sbrk 上限0x375000、exec_heap 452KiB (小さい明示値のみ縮小) を維持する。
`mem` の `実測: Shell band sbrk上限` は CPL0 の常駐 shell だけに出る実際の上限値。
`実測: ... resident heap total / used` は常駐 exec_heap の総量 / 使用量 (B) であり、
定数の予約帯や USER の AS 側使用量とは別。表示の詳細は [mem.1](manpages/mem.1) を参照。

> シェルは newlib の sbrk ヒープと KAPI `mem_alloc` の exec_heap の 2 系統を
> 持つ。かつては両方が BSS 終端から始まり互いを上書きしていた
> (`ls > file` の化け、`pipe: out of memory`、double free 警告)。
> 2026-09-03 に exec_heap を 0x380000 (旧 NP ギャップ) へ分離した。
> PTE に USER は立てないので CPL=3 のアプリからは見えない。

#### ページング (H3b 2026-09-06 / K6-RAM 2026-09-11)

静的 PT (bootstrap) の守備範囲は 32MB (PAGING_BOOT_MAP_SIZE = PAGING_MAP_SIZE、PT 8 枚 = +16KB BSS)。
**実 RAM の人為的な上限は無い** (K6-RAM で PAGING_RAM_LIMIT を撤廃)。paging_init が恒等で張るのは
min(検出量, 32MB) — その上端が paging_boot_identity_end() — で、それより上の RAM は
pgalloc_stage_online() が paging_map_phys() で張り、PT はブート workspace から動的に取る。
15〜16MB (F00000h-FFFFFFh) は PC-98 のシステム空間で RAM にはしない (MEM_SYSTEM_SPACE_*)。
16MB 超で RAM が載っていない範囲は既定 Not-Present で、必要な範囲だけ paging_map_phys() で張る。
**OS が番地を決めるデバイス窓は v3 のデバイス窓の帯 `[0xFE000000, 0xFF000000)`** (`MEM_DEVICE_APERTURE_*`、
2026-09-29) に置く。RAM の量に関係なく RAM と重ならないよう物理地図で MMIO にし (RAM として登録できる上端 =
`MEM_PHYS_RAM_CEILING` = 2GB、D11 — `[2GB, 4GB)` は窓の帯も含めて RAM にしない、T1a)、帯の先頭 4MB の PT を paging_init が**静的に 1 枚** (+4KB BSS) 持つ —
新しい PDE は live AS が 0 の間しか足せないが、gfx の init は exec の後にも走るため。
帯を物理地図で MMIO に登録するのは 16MB 超の RAM を登録する経路 (`memory_boot_add_high`) だけで、高位 RAM の無い
構成 (8MB〜15MB、台帳の置き場は FIXED / ARENA_TOP — TASK_T1_LEDGER §3-3) では帯は明示の MMIO 登録でなく UNKNOWN のまま — RAM にはならないので現状の RAM 判定・窓のマップには
支障は無く、明示の登録は v3 の資源割当で引き継ぐ:

```
0x00F00000 - 0x00F4AFFF          PEGC のリニア窓 (H2、9821 で PEGC 有効時のみ)  supervisor + PCD
0x00F60000 - 0x00F67FFF          WAB (Cirrus Xe10) のバンク窓 (ITF の既定。backend は使わない)
0xFE000000 - 0xFE1FFFFF          WAB (Cirrus Xe10) の 2MB リニア窓 (NP21/W は 4MB を窓として出す) supervisor + PCD
  +000000h 表示面 / +04B000h クライアント面 (300KB) / +096000h 塗りパターン
0xFF000000 - 0xFFFFFFFF          ROM ミラー / PCI 機の MMIO (MEM_PHYS_MMIO_TOP、RAM にしない)
```

> リニア窓は 2026-09-29 まで 0x01000000 (16MB 直上) だったが、16MB 超の RAM と重なる (NP21/W 17MB、
> 実機 64MB) ので帯へ移した。512MB 付近は PCI の BIOS が BAR を置く帯 (Ra266 の実測 0x20000000〜) なので
> 避けた。**Cirrus は NP21/W 互換のためだけ**で、auto の probe は NP21/W の上でだけボードの ID を読む
> (実機 Ra266 のアクセラレータは PCI の Trident)。経緯は `docs/POLICY_DEBUG.md` §4-34。

> **デバイス窓の貸し出し規則 (e11b2)**
> master のデバイス窓は **supervisor + PCD**、Unicode/主記憶 BB は supervisor + WB を維持する。
> CPL=3 の描画は CLIENT lease の私有 VA、Unicode は RO lease だけを使う。
> Cirrus DISPLAY の RW は授権された lease に限定し、旧 alias の PTE/PDE に USER を立てない。
> 通常の AS map/unmap は共有 PT に掛かる要求を全範囲無変更で拒否する。
> kselftest (`paging_map_user_keep_selftest`) は拒否と PFN/属性/PDE の不変を検査する。
> `ring3_guard bb` は低位 0x6A000 への書込みで PF error=7、kill を期待し、
> 正規 CLIENT lease の生存対照は `ring3_guard lease` が担う。

> **CR0.WP = 0 で走る** (`arch/x86/arch_cpu.h` の MMU 有効化)。`kernel/shlib.c` が
> 「カーネルからは RO の USER ページにも書ける」前提で共有ライブラリを張るため。
> 裏返しとして **CPL=0 (KAPI の wrapper) は PTE の RO 保護を受けない** ので、CPL=3 が
> 出力引数に共有ライブラリの `.text`/`.rodata` を渡しても #PF は起きない。出力ポインタは
> 書く前に `exec/exec.c` の `ring3_user_ranges_writable()` で present + RW + USER を
> 確かめる (生成される wrapper の先頭。票
> [archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md](archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md))。

> **0x90000 の自動プレイ観測メールボックス**: ゲーム側が毎フレーム状態ブロックを書き、
> ホストが `GET /api/mem?addr=0x90000&space=phys` で読む。**レイアウトを変えたら
> `game/tools/autoplay/driver.py` の `read_mailbox()` と `EXPORT_VERSION` を同じコミットで直す**
> — 片方だけ変えるとホスト側が黙って古い解釈で読み続ける。

### §2-2 DMA 64KB境界制約

PC-98のDMAコントローラ(8237相当)は16ビットアドレスカウンタとページレジスタを持つ。  
DMA転送が64KB物理アドレス境界 (0x10000, 0x20000, ...) を**またぐ**場合、カウンタがラップアラウンドしてデータが壊れる。

**ルール**: INT 1BhによるFDD読み込みにおいて、`ES:BP`で指定するバッファの開始アドレスから転送バイト数分のアドレスが同じ64KBページ内に収まるようにすること。

```
64KBページ = 物理アドレス >> 16
条件: (start >> 16) == ((start + size - 1) >> 16)

例 NG: 0xFC00 + 8192 = 0x11C00 → ページ0とページ1をまたぐ
例 OK: 0x10000 + 8192 = 0x12000 → ページ1内に収まる
```

### §2-3 セグメント方式によるDMA境界回避

0x10000以降のアドレスへの読み込みにはES:BPセグメント方式を使用する。  
ESを0x200ずつ増加させる (= 物理アドレス +8192) ことで、各読み込みが64KBページ内に安全に収まる。

```
ES=0x1000 → 物理 0x10000 (ページ1先頭, OK)
ES=0x1200 → 物理 0x12000 (ページ1内, OK)
ES=0x1E00 → 物理 0x1E000 (ページ1末尾, 0x1E000+8192=0x20000, ぎりぎり収まる)
ES=0x2000 → 物理 0x20000 (ページ2先頭, OK)
```

---
