## 第2部 メモリマップ

### 開発方針と現在の実装上限

- **最低動作環境の数値は未確定**。旧物理固定配置での9.6MB下限説明
  (決定 `decisions/gui/DESIGN.md` §9.3、2026-09-04) は当時の設計記録。
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
  旧物理固定配置での記録であり、現行構成の下限は v3 の最終段の構成別ゲスト確認で判断する。
- GUI の必要 RAM はバックエンド・常駐領域・アプリの実測から別途定義する。
  v3 の最低条件「8MB 機で GUI + 私有 2MB のアプリ」は目標として維持する (V3_GOALS §3)。構成別の判定は v3 の最終段、それまでのゲスト受入は 64MB の 1 構成 (DEFERRED_TESTS)。
- 32bit フラットアドレス空間の設計対象は 4GiB（0x00000000〜0xFFFFFFFF）。
  これは全域が利用可能 RAM、単一アプリの利用可能領域、または実装済み容量を意味しない。
  ROM・MMIO・予約帯・ガード・カーネル領域を区別し、物理 RAM の利用可能範囲を管理する。
- 実 RAM の人為的な上限は無い (K6-RAM、2026-09-11 に 16MB の RAM 管理を撤廃)。起動時の静的な恒等写像は 32MB までで、それより上は検出量だけ動的に張る (下の「ページング」)。RAM として登録できる上端は 2GB (D11、`MEM_PHYS_RAM_CEILING`)。これらは現行実装の値であり、製品の設計上限ではない (2GB の上端は v4 で見直す — ROADMAP「他アーキテクチャへの移植」)。
  大容量対応には RAM 検出、ページ割当、ページ表、exec 配置、デバイス窓、媒体・転送用予約、
  境界演算を一貫して拡張し、検証する。定数だけの拡大で対応済みとは扱わない。
- 4GiB の exclusive end は u32 に格納できない。上端・長さの計算は広い整数または
  明示的なページ数表現を用い、wrap による範囲検査の通過を禁止する。
- 現開発段階では ABI 変更を許容し、kernel・SDK・userland・apps・game の
  クリーン再ビルドと配備整合性で揃える。既存データの保全とは別の方針である。

### §2-1 物理メモリ配置

> 番地の定義の正典は `include/memmap.h`。ここが食い違ったらそちらが正しい。
> カーネルスタックは 0x90000 → 0x1FC000 (V86 ゲストに 640KB を渡すため) →
> 0x2FC000 (2026-09-17 決裁 D1) → **0x3FC000** (T3ab、固定 PD/PT 枠の後) と動いてきた。D1 の移設は
> 「浮いた番地と固定番地を同じ帯で隣り合わせにしない」ため —
> カーネル帯域は `KHEAP_BASE` 以降が `__bss_end` 由来で浮くので、カーネルが
> 育つと共有メモリが固定番地のスタックへ食い込む
> (票 `docs/archive/kernel_v21/TASK_KSTACK_USER.md`)。
> 0x9FFFC は今もローダー段の ESP として使われるが、カーネルは `kentry.asm` で
> `MEM_KSTACK_TOP` に張り替える。
>
> カーネル本体の上限は `MEM_KERNEL_IMAGE_MAX` で、**`build/os32.ld` の
> `ASSERT` がリンク時に守る**。超えるとリンクが失敗し、何をすればよいかを
> メッセージが言う。`make check` の `check-memmap` は、その予算・帯どうしの
> 重なり・範囲の逆転・写しのずれ・定数の整合をまとめて見る。生成文書との一致は検査しない。

実ビルドの地図は `build/out/MEMMAP.md` に生成する
([生成文書の見方](INDEX.md#生成文書の見方))。定義は `include/memmap.h`、
実ビルドの数値は `build/out/kernel.map` の `__bss_end` / `__kernel_resident_end`から求める。
表は全て絶対番地とし、カーネル本体の使用量・残り予算・成長時に動く帯も表示する。
`__bss_end` が伸びると `KHEAP_BASE` 以降の KAPI / SHM / ガードも動くため、
数値の地図はコミットせず、説明をこの節に残す。

T7a-2e の低位 `[0, MEM_CONV_END)` (640KiB、160 PFN) は、用途 V86_LOW の
単一 `FIXED/kernel/WB` 永久予約。一般 pool への供給は0で、V86 session の前後も
型・owner・PFN 数は変わらない。通常 page0 はNP、残りはsupervisor/RW。
V86 は低位を直接 RW/USER 写像し、mailbox 868B を既存KHEAPへ退避してから
640KiB をゼロ化する。終了時は不変のboot原本からpage0全4096Bとmailboxを戻し、
保存したPTEを復元する。旧159頁のbacking確保は0 (sessionのpool peakは636KiB減)。
planar BBはboot時pool確保の32頁・別SURFACE_BACKINGで、boot→gshellの既存移譲を使う。
旧font/cache・KCG scratch・低位BBの予約と番地定数は撤去済み。bootinfoとboot stackは退去済み。

T4c-1 では `__kernel_resident_end` は core BSS 末尾のページ境界。
SQLite は `/boot/sqlite.mod` の body と512KiB pool・32KiB stackを同じ module owner で
連続割当し、prefixだけ起動成功時に返す。stack 直下に追加の1頁を NP guard として
保持し、owner 回収前に supervisor/RW に戻す。動的な kernel VA は
`paging_sqlite_stack_guard` と `[sqlite] stack_guard=... p=...` で観測し、boot/live audit で NP を照合する。
SQLite の全頁（隔離時も含む）は
SLACK/pool の内数であり、別枠として再加算しない。`[sqlite] owner=... state=... pages=...`
は起動後の独立観測で、既存の boot audit 回数を増やさず総和の diff を表示する。
memory_layout は4を維持する。変更はリンク時に浮く core 内部の常駐鎖と owner 割当であり、
USER/shlib VA、固定 paging/stack/shell、公開 KAPI slot とデータ配置は変わらない。
部品の新旧は独立した ops版2・import/facade ABI印・VK32の対CRCで拒否する。

T5-0b の #DF (vector 8) は専用 task gate (GDT末尾 idx6 / selector 0x30) で受ける。
既存 idx3 の ring3/V86 TSS・idx4/5 の利用者セグメントは維持する。core BSS の
104B TSS・4KiB stack と固定 master CR3 を使い、GDT/IDT/旧新TSS/入口は各PD共有の
核の恒等写像に残す。前の EIP/ESP、CR2、ソフトウェアが控えた前の CR3 と
kernel/engine/unknown をシリアル・TVRAM に1行出して停止し、復帰・回収しない。
EIP は #DF の仕様上再開点ではなく、CR2 は例外フレーム書込み失敗の番地にもなる。
engine stack は製品設定の深度100のhost実測24,388Bに対して32KiB (残余 8,380 B、上限 100 を維持 — ユーザー決定 2026-10-18) を選択 (定義は
`kernel/sqlite_engine.h`)、NP guard 1頁は変更しない。実guard配送の受入は台帳 KSTK-DF。

`sys_usable_mem_end()` は `MEM_PHYS_EXEC_FLOOR` から最初の PERSIST 所有頁までの
低位連続上端を返す（起動時の `ledger_arena_freeze()` で確定）。検出 RAM の最高番地ではない。
DMA の恒久予約もここで止めるため、BB がさらに上に置かれても、この上端は DMA 開始番地になる。

起動時は kdebug 読込と BB 予約の後に `[mem] pages ...` を1行出す (T3b1)。
`fixed` は固定占有、`slack` / `pool` は通常の使用中ページも含む供給量から
BB と kdebug を除いたもの、`meta` は metadata と workspace (ARENA_TOP 時も残す固定2頁を含む)、
`bb` は固定/RAM の BB、`kdebug` は採用した部品の本体、`other` は DMA 等。
VRAM/MMIO の表示面は BB に加えない。領域台帳と allocator の供給量から求めた総和を
`memory_boot_ram_kb()` のページ数と照合する。後者は従来どおり低位640KiB〜1MiBを含み、
15〜16MiBを除く。実行回数と不一致の回数は専用の `boot_pages_audit_runs` /
`boot_pages_audit_fail` に数え、それぞれ `MemStat.audit_runs` / `MemStat.audit_fail` に加算する。
不一致は `ng diff=総和−物理頁数` と表示し、起動は続ける。
KDEBUG=1 の自己試験は部品が RUNNING の場合だけ呼び、post_exec の `slack_cycle` は
空き頁を1頁ずつ確保して再割当拒否・使用中 owner の保持・返却後の空き頁数を確かめる。
T3b1 のゲスト受入は 2026-10-18 に完了した (経緯: `docs/archive/v3/TASK_T3_LAYOUT.md` §4)。


Unicode-JIS は `/sys/unicode.bin` の昇順 7,063 組 (28,252 B) を起動時に一度読み、
検証成功時だけ kernel owner の連続7頁 (28,672 B) に保持する。一般割当域の
SLACK または pool の内数で、表ありは owner 使用頁 +7・空き頁 −7、KHEAP 常駐消費0。
頁末の420 Bは探索しない。失敗時は確保頁を返し、かな算術だけで起動を続ける。
表・表ポインタはユーザーへ公開せず、変換は KAPI `unicode_to_jis` を呼ぶ。

旧低位予約は T7a-2e で V86_LOW に統合。

#### 動的レイアウト (番地が実行時に決まるので生成できない部分)

生成される地図に出るのは `include/memmap.h` と `kernel.map` で決まる固定の番地だけ。
以下は搭載メモリ量・バイナリの大きさ・起動の仕方で変わるので手で持つ。

```
[ USER の高位 VA (memory_layout 世代4) ]
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
resident shell/gshell は sbrk 上限0x4B5000、exec_heap 256KiB (小さい明示値のみ縮小) を維持する。
像の上限は `MEM_SHELL_MAX_SIZE` (660KiB) で、像の末尾から sbrk 用に `MEM_SHELL_IMAGE_HEADROOM` (64KiB) を
必ず残す (`sdk/link/app_sys.ld` の ASSERT と `exec_shell_layout`)。FONTPERF F3b (2026-10-20) で余裕を
128→64KiB に下げ、像の上限を 596→660KiB に広げた: sbrk の実測 peak は gshell 0 B (Rust の割り当ては
mem_alloc)・CUI shell 1,032 B (NP21/W、filer・edit_gui と FEP 変換・設定・trim の後)。実行時の境界
(sbrk 上限 = exec_heap の起点) は変えないので、配置の世代は上げない。gshell の exec_heap peak は 131,656 B。
`mem` の `実測: Shell band sbrk上限` は CPL0 の常駐 shell だけに出る実際の上限値。
`実測: ... resident heap total / used` は常駐 exec_heap の総量 / 使用量 (B) であり、
定数の予約帯や USER の AS 側使用量とは別。表示の詳細は [mem.1](manpages/mem.1) を参照。

> シェルは newlib の sbrk ヒープと KAPI `mem_alloc` の exec_heap の 2 系統を
> 持つ。かつては両方が BSS 終端から始まり互いを上書きしていた
> (`ls > file` の化け、`pipe: out of memory`、double free 警告)。
> 2026-09-03 に exec_heap を 0x380000 (旧 NP ギャップ) へ分離した (今はシェル帯の
> `MEM_SHELL_HEAP_BASE` から)。
> PTE に USER は立てないので CPL=3 のアプリからは見えない。

#### ページング (H3b 2026-09-06 / K6-RAM 2026-09-11)

静的 PT (bootstrap) の守備範囲は 32MB (PAGING_BOOT_MAP_SIZE = PAGING_MAP_SIZE、固定 PT 8 枚、画像外)。
**実 RAM の人為的な上限は無い** (K6-RAM で PAGING_RAM_LIMIT を撤廃)。paging_init が恒等で張るのは
min(検出量, 32MB) — その上端が paging_boot_identity_end() — で、それより上の RAM は
pgalloc_stage_online() が paging_map_phys() で張り、PT はブート workspace から動的に取る。高位 RAM のある構成では、GeForce の BAR0 16MiB と DISPLAY 300KiB の写像用に PT 5 頁を追加予約する。FIXED 型の低メモリ構成は従来の workspace 1 頁を保ち、GF の写像に不足すれば 98 へフォールバックする。
15〜16MB (F00000h-FFFFFFh) は PC-98 のシステム空間で RAM にはしない (MEM_SYSTEM_SPACE_*)。ただしセットアップで「16MB システム空間: 切り離す」(I/O 043Bh bit2=1) の機械では、そこは通常 RAM で PEGC の VRAM は F00000h に出ない (FFF00000h の別名でだけ見える — undocumented io_mem.md)。実機 Ra266 はこの構成 (R7-1、2026-10-14)。v3 ではこの構成でも RAM にしない (損失 1MB、v4 の物理地図で見直す)。
PEGC 候補は F00000h と FFF00000h の両窓 (各512KB) を supervisor + PCD で恒等に張り、043Bh bit2=1 または `GFXDIAG=altwin` なら別名から双方向の別名試験を行う。最初に成立した窓を DISPLAY に使い、両方不成立または DISPLAY 付け替え失敗なら pc98 へ落ちる。Ra266 の別名は実機未確認。
16MB 超で RAM が載っていない範囲は既定 Not-Present で、必要な範囲だけ paging_map_phys() で張る。
**OS が番地を決めるデバイス窓は v3 のデバイス窓の帯 `[0xFE000000, 0xFF000000)`** (`MEM_DEVICE_APERTURE_*`、
2026-09-29) に置く。RAM の量に関係なく RAM と重ならないよう物理地図で MMIO にし (RAM として登録できる上端 =
`MEM_PHYS_RAM_CEILING` = 2GB、D11 — `[2GB, 4GB)` は窓の帯も含めて RAM にしない、T1a)、帯の先頭 4MB の PT を paging_init が**静的に 1 枚** (画像外の固定域) 持つ —
新しい PDE は live AS が 0 の間しか足せないが、gfx の init は exec の後にも走るため。
最上位4MB (FFC00000h〜FFFFFFFFh、PDE 0x3FF) にも静的 PT (003EB000h) を持ち、全アプリ PD に共有する。固定 PD/PT は 1/8/1/1 の11ページ。003FB000h はスタックガード (NP)、003FC000h〜003FFFFFh の4ページはカーネルスタック (RW)。定義は [include/memmap.h](../include/memmap.h)、保護属性の適用は `mm/paging.c:349` を参照。
**固定 PD/PT 枠の並び (T3ab、半開区間)**: 見張り `[0x3E0000,0x3E1000)` (NP)、枠 `[0x3E1000,0x3FB000)` 26 頁 (`MEM_FIXED_PAGING_FRAME_PAGES`)、見張り `[0x3FB000,0x3FC000)` (NP)、カーネルスタック `[0x3FC000,0x400000)` 4 頁。枠のうち使うのは先頭の 11 頁 (PD `0x3E1000`、起動時の PT 8 枚 `0x3E2000`〜、デバイス窓の PT `0x3EA000`、最上位 4MB の PT `0x3EB000`) で、残りの予備 15 頁もカーネル所有のまま持ち、SLACK へ返さない。枠全体は恒等の supervisor/RW、NP は両側の見張りだけ。
**DMA の池は固定 `[0x7F0000,0x800000)` の 64KiB** (`MEM_DMA_POOL_BASE`、[HW2] の 64KB 境界をまたがない)。一般の池や BB を配る前に台帳へ `LEDGER_R_DMA` / カーネル所有 / WB の恒久区間として登録し、通常 RAM として二重に配らない (`mm/pgalloc.c`)。検出した RAM がこの区間に届かず登録できなかった機械では、池の初期化・自己診断・PCI の結び付け・PCM を行わず、`[dma] RAM short; pool/PCI/PCM skipped` を 1 行出して起動を続ける (`init/kernel.c`)。
**FIXED 型の台帳の置き場 (metadata 1 頁 + workspace 1 頁) はカーネルの BSS の末尾の専用区画 `.ledger_fixed`** (NOLOAD、頁整列) で、`MEM_LEDGER_META_BASE` はその先頭のリンカ記号 `__ledger_fixed_start`。起動時から恒等の supervisor/RW で present。ARENA_TOP 型の機械では動的な置き場を使い、この 2 頁は使わずにカーネル所有のまま持つ。最上位 PT は初期には全 NP、PEGC 候補の写像成功後だけ FFF00000h〜FFF7FFFFh の128ページを公開する。
監査は候補の有無でなく台帳の公開済み map 範囲に一致させる。正常時は両窓 RW|PCD、F00 成功後の FFF 失敗時は F00 の予約・写像・公開を残して FFF は NP、候補なしなら最上位 PT の全 PTE は NP。
帯を物理地図で MMIO に登録するのは 16MB 超の RAM を登録する経路 (`memory_boot_add_high`) だけで、高位 RAM の無い
構成 (8MB〜15MB、台帳の置き場は FIXED / ARENA_TOP — TASK_T1_LEDGER §3-3) では帯は明示の MMIO 登録でなく UNKNOWN のまま — RAM にはならないので現状の RAM 判定・窓のマップには
支障は無く、明示の登録は v3 の資源割当で引き継ぐ:

```
0x00F00000 - 0x00F7FFFF          PEGC のリニア窓 (H2、043Bh bit2=0 のとき)  supervisor + PCD
0x00F60000 - 0x00F67FFF          WAB (Cirrus Xe10) のバンク窓 (ITF の既定。backend は使わない)
0xFE000000 - 0xFE1FFFFF          WAB (Cirrus Xe10) の 2MB リニア窓 (NP21/W は 4MB を窓として出す) supervisor + PCD
  +000000h 表示面 / +04B000h クライアント面 (300KB) / +096000h 塗りパターン
0xFFF00000 - 0xFFF7FFFF          PEGC 別名窓 (043Bh bit2=1 では15〜16MBは通常 RAM、OS32 はこの窓を使用) supervisor + PCD
0xFF000000 - 0xFFFFFFFF          ROM ミラー / PCI 機の MMIO (MEM_PHYS_MMIO_TOP、RAM にしない)
```

> リニア窓は 2026-09-29 まで 0x01000000 (16MB 直上) だったが、16MB 超の RAM と重なる (NP21/W 17MB、
> 実機 64MB) ので帯へ移した。512MB 付近は PCI の BIOS が BAR を置く帯 (Ra266 の実測 0x20000000〜) なので
> 避けた。**Cirrus は NP21/W 互換のためだけ**で、auto の probe は NP21/W の上でだけボードの ID を読む
> (実機 Ra266 のアクセラレータは PCI の Trident)。経緯は [pgalloc「落とし穴」](design/mm/pgalloc.md#落とし穴)。

> **デバイス窓の貸し出し規則 (e11b2)**
> master のデバイス窓は **supervisor + PCD**、主記憶 BB は supervisor + WB を維持する。
> CPL=3 の描画は CLIENT lease の私有 VA を使い、Unicode 変換は KAPI を呼ぶ。
> Cirrus DISPLAY の RW は授権された lease に限定し、旧 alias の PTE/PDE に USER を立てない。
> 通常の AS map/unmap は共有 PT に掛かる要求を全範囲無変更で拒否する。
> kselftest (`paging_map_user_keep_selftest`) は拒否と PFN/属性/PDE の不変を検査する。
> `ring3_guard bb` は旧BB番地 (現在はV86_LOW) 0x6A000 への書込みで PF error=7、kill を期待し、
> 正規 CLIENT lease の生存対照は `ring3_guard lease` が担う。

> **CR0.WP = 0 で走る** (`arch/x86/arch_cpu.h` の MMU 有効化)。`mm/shlib.c` が
> 「カーネルからは RO の USER ページにも書ける」前提で共有ライブラリを張るため。
> 裏返しとして **CPL=0 (KAPI の wrapper) は PTE の RO 保護を受けない** ので、CPL=3 が
> 出力引数に共有ライブラリの `.text`/`.rodata` を渡しても #PF は起きない。出力ポインタは
> 書く前に `exec/exec.c` の `ring3_user_ranges_writable()` で present + RW + USER を
> 確かめる (生成される wrapper の先頭。票
> `docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md`)。

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
