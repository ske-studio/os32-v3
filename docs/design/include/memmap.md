# include/memmap.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・API_CONTRACTS・DESIGN・GFX_MODES_DESIGN (D1 で移した)
ソース: `include/memmap.h`

**カーネル・シェル・通常 RAM は恒等写像 (supervisor)**、**アプリだけ 0x80000000〜 の私有写像**

物理池の境界定数とアプリ仮想配置定数を分離

アプリ帯の起点 0x80000000 (RAM の登録上限 2GB)

| 番地 (物理 = 仮想。アプリ帯と lease 窓だけ仮想) | PDE | 種別 | 大きさ | 中身 |
|---|---|---|---|---|
| 0x000000–0x000FFF | 0 | 固定 NP | 4KB | NULL ガード |

**0x001000–0x09FFFF**
**V86 アリーナ** (D13)
起動時だけ bootinfo 0x7E00。
0x90000 の自動プレイ mailbox は残す

| 番地 (物理 = 仮想。アプリ帯と lease 窓だけ仮想) | PDE | 種別 | 大きさ | 中身 |
|---|---|---|---|---|
| 0x0A0000–0x0EFFFF | 0 | 固定 **supervisor** | 320KB | VRAM (テキスト + 4 プレーン + PEGC のバンク窓)。**アプリには常時 USER で見せない** — CUI の TVRAM 直書き・全画面は lease 窓 (下) 経由。**V86 は低位そのもの** (ゲストの `A800:0000` は 0xA8000) なのでセッション中だけ低位に USER を立てる (§2-2) |
| 0x0F0000–0x0FFFFF | 0 | 固定 RO | 64KB | BIOS ROM |

**0x100000–(鎖の終端)**
固定 (浮動の鎖)
KHEAP 192KB → KAPI 4KB → ガード → SHM 224KB (末尾 64KB GUI) → ガード。**リンカ ASSERT: 鎖の終端 ≤ 0x3E0000**
トランポリンページ (RO+USER) と SHM (RW+USER) だけが USER
**(鎖の終端)–0x3DFFFF**
**池 (KERNEL_SLACK)**
未使用ページをブート時に台帳へ RAM (source=KERNEL_SLACK) で登録。present・supervisor なので張り直し不要。モジュールの第一候補

| 番地 (物理 = 仮想。アプリ帯と lease 窓だけ仮想) | PDE | 種別 | 大きさ | 中身 |
|---|---|---|---|---|
| 0x3E0000–0x3E0FFF | 0 | NP | 4KB | 固定ページ表の手前のガード |
| 0x3E1000–0x3FAFFF | 0 | 固定 supervisor | 104KB | 画像外の固定 PD/PT 枠26枚。使用11枚 (PD 1 + boot PT 8 + device PT 1 + 最上位 MMIO PT 1)、予備15枚も恒久予約 |
| 0x3FB000–0x3FBFFF | 0 | NP | 4KB | ガード |
| 0x3FC000–0x3FFFFF | 0 | 固定 | 16KB | カーネルスタック |
| **0x400000–0x4FFFFF** | 1 | 固定 supervisor | 1MB | **常駐シェル帯 (1 箱)**: 画像 (gshell 219KB) → sbrk ↑ … exec_heap ↓ ← ガード ← スタック 40KB。全 PD 共有。gshell は CPL=0 なので池・VRAM 窓を直接触れる (D8) |

| 番地 (物理 = 仮想。アプリ帯と lease 窓だけ仮想) | PDE | 種別 | 大きさ | 中身 |
|---|---|---|---|---|
| 0xF00000–0xFFFFFF | 3 | 固定 NP / sup+PCD | 1MB | システム空間 (PEGC リニア窓)。RAM として配らない (D6) |
| 0x1000000–RAM 上端 | 4– | 池 (高位) | 検出量 | K6 の経路。`MEM_PHYS_RAM_CEILING = 0x80000000` |
| 0x20000000 帯 | 128– | 固定 sup+PCD | BAR ごと | PCI の BAR (Ra266)。PT は列挙時 (`exec_init` より前) に作る |

| 番地 (物理 = 仮想。アプリ帯と lease 窓だけ仮想) | PDE | 種別 | 大きさ | 中身 |
|---|---|---|---|---|
| **0xF0000000–0xFDFFFFFF** | **960–1015** | **lease 窓 (仮想、PD ごと私有)** | 4MB 単位、要る PT だけ | **サーフェスの別名写像**: 主記憶サーフェス (BB、OpenType の共有キャッシュ等) と VRAM (全画面 / CUI の TVRAM) を、**その AS の私有 PT に** USER で張る (V86 は対象外、低位を直接使う)。低位の恒等は supervisor のまま。同じ物理を 2 つの AS に貸せば 2 つの別名ができる |
| 0xFE000000–0xFEFFFFFF | 1016–1019 | 固定 sup+PCD | 16MB | デバイス窓の帯 (v2.1 のまま) |
| 0xFF000000– | 1020–1023 | 固定 | 16MB | ROM ミラー / MMIO |

**0x500000–0xEFFFFF**
`MEM_POOL_BASE = 0x500000`。
**0x7F0000–0x7FFFFF は DMA 専用64KBを固定予約** (一般池へ配らない)

**0x80000000–0xEFFFFFFF**
**512–959**
**アプリ帯 (仮想、PD ごと私有)**
shlib 0x80000000〜 (仮想 1MB、物理は実ページ数) → exec 0x80100000 → sbrk → ガード → exec_heap → スタック (**既定 256KB、OS32X ヘッダで可変**)。master には PDE を持たない

### 3-4. P2V / V2P への集約

- **規則**: カーネルが物理番地をポインタとして触る箇所は `P2V(phys)` を通し、ポインタを装置 (DMA・ページ表・窓) に渡す箇所は `V2P(ptr)` を通す。恒等の v3 では両方とも**何もしない関数 (`static inline`) / マクロ** (`include/memmap.h` に 1 か所)。台帳の API は**物理 PFN / 物理番地を返す**ことにし、呼び手が P2V で触る形に揃える。

`P2V` / `V2P` が使えるのは**恒等の supervisor 領域だけ** (カーネル帯・シェル帯・池・MMIO の恒等窓)。
**アプリ帯と lease 窓は v3 でも非恒等**
**AS のページと lease は台帳 (PTE / SURFACE) から物理へ変換する別の関数**
**DMA には物理の記述子 (PFN 区間) を渡す** — 仮想ポインタを受け取る DMA API を作らない (`dma_alloc` の戻りは物理と仮想の組)。

**現行の区間表 (集積拡張は凍結、案5の配置契約を維持)**:

| 区間 | 用途 | 上限 | 寿命 |
|---|---|---|---|
| 0x010000–0x08EFFF | 現行ローダの集積 | 508KiB | 展開・CRC完了まで |
| 0x0009FFFC 以下 | ローダのstack・FAT buffer・bootinfo | — | boot consumer退去まで |
| 0x100000–0x3DFFFF | coreと常駐鎖の配置枠 | 2,944KiB | 実際の常駐終端/SLACKを同一mapで計測 |
| 0x500000–0x7EFFFF | 池 (集積移動・旧同梱予約は作らない) | — | 通常のowner割当 |
| [0x7F0000,0x800000) | DMA専用予約 | 64KiB | 常駐、一般池へ配らない |

SHM の容量と GUI 予約範囲の正典は [`include/memmap.h:167・192–194`](../../../include/memmap.h)
(`MEM_SHM_SIZE` / `MEM_SHM_GUI_SIZE` / `MEM_SHM_GUI_OFFSET`)。

- **16MB システム空間**: 16MB 以上積める機種では F00000h〜FFFFFFh をシステム用 (PEGC VRAM /
  アクセラレータ窓) か通常 RAM かを 043Bh bit2 で選ぶ (`0` = システム使用)。RAM にすると
  PEGC VRAM は F00000h では見えなくなるが FFF00000h〜FFF7FFFFh で使える (UNDOCUMENTED
  `io_mem.md`)。**OS32 の物理メモリ上限計算は F00000h〜FFFFFFh を RAM に含めない**。

OS32 の物理メモリ上限計算は
0xF00000〜0xFFFFFF を RAM として扱ってはならない (16MB 以上積んだ 9821 の
システム空間)。

- MEM_GFX_BB8_HEIGHT=480 は「現在の高さ」でなく「現行予約の上限」の互換名として残す。

- memmap は番地・予約上限の正典、mode は表示能力の正典。静的上限を実行時選択値に置き換えない。
