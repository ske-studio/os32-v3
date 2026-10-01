## 第2部 メモリマップ

### 開発方針と現在の実装上限

- **最低動作環境の数値は未確定**。「CUI 最低 8MB」という記述が 2026-09-09 に
  一時入っていたが、根拠が無く、既存の設計制約と矛盾するため撤回した。
  現時点で言えるのは設計上の下限が **物理 9.6MB 構成以上**であること
  ([tasks/gui/DESIGN.md §9.3](tasks/gui/DESIGN.md)、2026-09-04)。
  理由は CPL=3 アプリのスタックが物理 0x7C0000〜0x7FFFFF に固定で、
  8MB ちょうどではアプリ帯がホットデプロイ窓と重なるため。
- **CPL=3 のアプリ帯は 4MB (PDE) 単位で伸びる** (2026-09-10、票
  [tasks/memory/APP_BAND_PDE.md](tasks/memory/APP_BAND_PDE.md))。
  2026-09-09 までは PDE 1 枚固定の 3MB 窓 (0x500000〜0x800000) で、
  `RING3_USTACK_TOP` も `RING3_HEAP_TOP` も定数だったため **RAM を増やしても
  1 アプリの利用可能量は増えなかった** (8MB でも 15MB でも `heap_test` の
  レイアウトは完全に同一、avail 2,605,056 バイト)。
  現在はアプリ固有 PDE を **1〜2 枚** (`MEM_APP_BAND_MAX_PDES`) 取れる:

  | 枚数 | 帯 | 条件 |
  |---|---|---|
  | 1 (既定) | 0x400000〜0x7FFFFF | OS32X ヘッダの `heap_size` が 0、または 1 枚に収まる。**従来と完全に同じレイアウト** |
  | 2 | 0x400000〜0xBFFFFF | `heap_size` が 1 枚に収まらず、かつ空き RAM が届く (子の claim 範囲 A の末尾まで) |

  上限が 2 枚なのは PDE 3 (0xC00000〜0xFFFFFF) に PEGC のリニア窓 0xF00000 が
  入るため (`MEM_APP_BAND_DEVICE_FLOOR`)。枚数は `paging_app_band_pdes()` が
  決め、`exec/exec.c` の `RING3_USTACK_TOP` / `RING3_HEAP_TOP` は
  そこから導かれる実行時の値になった。空き RAM が足りず要求が入らないときは
  切り詰めずに `EXEC_ERR_NOMEM` で拒否する (スワップは持たない)。
- **ユーザーメモリを連続させる** (2026-09-09 方針)。目的は「1 アプリに渡せる
  連続領域を最大化すること」であって、物理末尾を空けておくことではない。

  ```
  システム - ユーザー - システム   OK  (末尾 1MB が予約でも構わない)
  ユーザー - システム - ユーザー   NG  (ユーザー帯に穴が開く)
  ```

  末尾側の予約 (旧 `sys_reserve_top`、T1e 以後は起動時の ⑥ が池のアリーナ内の上端から
  BB を取り `sys_usable_mem_end()` をその下に凍結する — TASK_T1_LEDGER §3-6) はこの形を保つ限り問題ない。禁じるのは
  **ユーザー帯の内側を割ること**。したがって予約は
  「使用可能上限の直下から連続して」取り、アプリ帯 (CPL=3 は 0x500000 から
  枚数ぶん、最大 0xC00000) に食い込ませない。食い込む構成は**その RAM 量を非対応とする**方が、
  帯に穴を開けるより良い。
  この方針でホットデプロイの物理末尾 256KB 窓を撤去した — 窓は CPL=3 スタック帯と
  完全に同じ範囲で、8MB 構成ではユーザー帯を割っていた。ユーザーランドの配送は
  HostDrv (`make deploy` → ゲストの `hsync`) に一本化した。
  装置が実際に占める帯 (PEGC の 0xF00000、Cirrus の 0x1000000) は装置側の事実なので
  `physmem` のモデルで MMIO として扱う。
- **PEGC (640x480) を使う GUI の下限は物理 9MB** (2026-09-09 実測)。バックバッファ
  300KB を末尾から取る (当時は `sys_reserve_top`、今は ⑥ の池からの確保) ので、それがアプリ帯 (既定 1 枚なら
  0x500000〜0x800000) の外に収まる必要がある。8MB では収まらず食い込む (実測: 予約が 0x7B5000 から
  始まりアプリ帯と重なる)。9MB では 0x8B3000 から始まりアプリ帯の外
  (`hal_test` = `backend pegc 640x480 bpp=8`、`heap_test` の overlap check OK)。
  PEGC VRAM は 512KB しかなく (Bible 3-2)、640x480x8 = 307200 の 2 面は入らないので
  バックバッファは主記憶に置く。640x400 なら 2 面が VRAM に収まり
  `I/O 00A4h` のハードウェアページフリップが使える (UNDOCUMENTED io_disp 452-460、846)
  ため主記憶は不要になるが、480 ラインは採らない選択になる。
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
> 重なり・範囲の逆転・写しのずれ・この表の鮮度をまとめて見る。

<!-- 生成: tools/gen_memmap.py --write (この印の中は手で書かない) -->

番地の定義の正典は [`include/memmap.h`](../include/memmap.h)。この表はそこと
`build/out/kernel.map` の `__bss_end` から [`tools/gen_memmap.py`](../tools/gen_memmap.py) が起こす。
手で直しても次の `--write` で消える。**表記は全て絶対番地** — 相対表記 (`+4KB`) は
読み手に暗算させ、2026-09-17 の「SHM がカーネルスタックに食い込んでいた」穴を隠していた。

```
__bss_end      = 0x18B9F0   (カーネル本体 558.5KB)
__sqlite_start = 0x200000
__sqlite_end   = 0x2BC200   (SQLite 本体 752.5KB)

[ コンベンショナルメモリ (0x00000-0xFFFFF) ]
0x000000 - 0x000FFF 4KB      NULL ポインタ検出ガード  (ブート後は R/O (BDA 参照のため。paging.c の [DEBUG] 注記)) NP
0x001000 - 0x049FFF 292KB    フォントキャッシュ  (kcg.c がブート後に配置)                  RW
0x007E00 - 0x007EFF 256B     ブート情報域 (ローダ → kernel_main)  (INT 1Bh AH=84h の結果 (include/bootinfo.h)。kernel_main の最初で写した後はフォントキャッシュが上書きしてよい) RW
0x04A000 - 0x069FFF 128KB    Unicode-JIS 変換表  (utf8.c)                                  RW
0x06A000 - 0x089FFF 128KB    GFX バックバッファ (4 プレーン)  (CPL=3 からは常に USER (レビュー #6)) RW
0x08A000 - 0x09FFFF 88KB     空き / V86 ゲスト窓の一部  (0x90000 は自動プレイ観測メールボックス (memmap.h に定義は無い)) RW
0x0A0000 - 0x0EFFFF 320KB    VRAM (テキスト + グラフィック)  (CPL=3 からは USER)           RW
0x0F0000 - 0x0FFFFF 64KB     BIOS ROM                                                      RO

[ カーネル帯域 (0x100000-0x1FFFFF) ]
0x100000 - 0x18B9EF 558.5KB  カーネル .text/.data/.bss  (kernel.map の __bss_end まで)     RW
0x18B9F0 - 0x18BFFF 1.5KB    空き
0x18C000 - 0x1BBFFF 192KB    カーネルヒープ (kmalloc)  (__bss_end を 4KB に切り上げた位置から) RW
0x1BC000 - 0x1BCFFF 4KB      KernelAPI テーブル  (KAPI_ADDR)                               RW
0x1BD000 - 0x1BDFFF 4KB      SHM 前方ガード                                                NP
0x1BE000 - 0x1F5FFF 224KB    共有メモリ本体  (16KB x SHM_BLOCK_COUNT。CPL=3 アプリの起動時に USER へ昇格 (exec.c、PDE 0 は全 PD 共有)) RW
0x1E6000 - 0x1F5FFF 64KB     GUI 予約 (末尾 4 ブロック)  (契約 T2。SDK の GUI_SHM_OFFSET = MEM_SHM_GUI_OFFSET) RW
0x1F6000 - 0x1F6FFF 4KB      SHM 後方ガード                                                NP
0x1F7000 - 0x1FFFFF 36KB     SHM 後方予約  (カーネルが予算いっぱいなら空になる (それは正しい)) NP

[ SQLite 帯域 (0x200000-0x2FFFFF) ]
0x200000 - 0x2BC1FF 752.5KB  SQLite code+BSS  (kernel.map の __sqlite_start / __sqlite_end) RW
0x2BC200 - 0x2BCFFF 3.5KB    空き
0x2BD000 - 0x2DCFFF 128KB    SQLite 代替スタック                                           RW
0x2DD000 - 0x2E7FFF 44KB     カーネル予約 (下)  (DMA プールの下側ガード)                   NP
0x2E8000 - 0x2F7FFF 64KB     DMA プール  (予約域に開けた穴。present / supervisor / R/W。**USER は立てない** (kselftest の MM 検査が MM_RW と MM_RWU を分けて見る)) RW
0x2F8000 - 0x2F8FFF 4KB      カーネル予約 (上)  (DMA プールの上側ガード)                   NP
0x2F9000 - 0x2FAFFF 8KB      台帳 backing (FIXED 型)  (metadata 1 ページ + PT workspace 1 ページ。FIXED 型のときだけ present / supervisor / R/W (USER なし)、ほかの構成では予約域 (NP) のまま) NP / RW
0x2FB000 - 0x2FBFFF 4KB      カーネルスタックガード  (2026-09-17 に 0x1FB000 から移設 (決裁 D1)) NP
0x2FC000 - 0x2FFFFF 16KB     カーネルスタック  (ESP 初期値 = MEM_KSTACK_TOP (kentry.asm / TSS.ESP0)) RW

[ シェル常駐帯域 (0x300000-0x3FFFFF) ]
0x300000 - 0x374FFF 468KB    シェル .text/.data/.bss + newlib sbrk  (MEM_SHELL_MAX_SIZE が上限 = sbrk の天井) RW
0x375000 - 0x375FFF 4KB      シェルスタックガード                                          NP
0x376000 - 0x37FFFF 40KB     シェルスタック  (下向き成長)                                  RW
0x380000 - 0x3F0FFF 452KB    シェル exec_heap (KAPI mem_alloc)  (newlib の sbrk とは別領域 (2026-09-03)) RW
0x3F1000 - 0x3F1FFF 4KB      固定 master PD  (恒久FIXED / supervisor / WB)                 RW
0x3F2000 - 0x3F9FFF 32KB     固定 bootstrap PT (8枚)  (恒久FIXED / supervisor / WB)        RW
0x3FA000 - 0x3FAFFF 4KB      固定 device aperture PT  (backing は WB、MMIO PTE は PCD/PWT) RW
0x3FB000 - 0x3FFFFF 20KB     上端残余予約  (恒久FIXED、T3 の guard+kstack 用)              NP

[ 仮想アプリ帯 (0x80000000-) ]
0x80000000 - 0x800FFFFF 1MB      共有ライブラリ帯 (libos32gui.shlib)  (.text は全 PD 共有、.data/.bss はアプリごとの物理) RO+USER / RW+USER
0x80100000 -       動的     外部プログラム空間  (image/sbrk、exec_heap は 0x88000000、可変 stack は 0x90000000 直下。物理ページの池の下端は MEM_POOL_BASE) RW+USER

[ 物理 RAM とデバイス窓 ]
0x500000 - 0x5FFFFF 1MB      集積域 (ブート時)  (圧縮画像の読み込み先 (T6b 以後)。展開が終われば池へ。T1 ではローダは未使用) 池
0x600000 - 0x6FFFFF 1MB      同梱域 (ブート時)  (ブート必須モジュールの展開先 (T5b 以後、台帳が owner=bundle で予約)。T1 ではローダは未使用) 池
0x700000 - 0xEFFFFF 8MB      空き
0xF00000 - 0xFFFFFF 1MB      PC-98 システム空間 (PEGC リニア窓)  (RAM として配らない)      NP / supervisor+PCD
0x1000000 - 0x7FFFFFFF 2032MB   16MB 以上の実 RAM (の置き場)  (検出量ぶんだけ pgalloc の池に入る (K6-RAM)。上端は MEM_PHYS_RAM_CEILING (D11)) RW
0x80000000 - 0xFFFFFFFF 2048MB   RAM の登録上限 (2GB) 以上  (RAM として登録しない (D11)。PCI の BAR・デバイス窓の帯 (0xFE000000〜)・最上位の ROM / MMIO) NP / supervisor+PCD

  属性: RW=読み書き / RO=読み取り専用 / NP=Not-Present (ガード)
  USER=CPL=3 から見える。`<<<` の行は下の「地図の矛盾」に出る帯。
  アプリ固有 PDE (0x80000000 から 4MB 単位) は最大 0x90000000 まで伸びる。
```

**地図の矛盾: 0 件** (重なりも逆転も無い。`--check` が毎回確かめる)

**カーネル本体の予算**: 596KB 中 558.5KB を使用 (残り 37.5KB)。

**カーネルがあと何 KB 育つと何が壊れるか** (`__bss_end` が伸びると `KHEAP_BASE` 以降が芋づるで動く)

- `__bss_end` +1.5KB で KHEAP_BASE が 1 ページ上がる。0x18C000 → 0x18D000。以降の KAPI / SHM / ガードが全部 4KB 動く
- `__bss_end` +37.5KB で **build/os32.ld の ASSERT がリンクを止める** (予算 MEM_KERNEL_IMAGE_MAX 超過)。止めるのが目的。超えたぶんだけ SHM 帯が カーネル帯域 0x1FFFFF を突き抜ける

<!-- /生成: tools/gen_memmap.py -->

#### 動的レイアウト (番地が実行時に決まるので生成できない部分)

上の表に出るのは `include/memmap.h` と `kernel.map` で決まる固定の番地だけ。
以下は搭載メモリ量・バイナリの大きさ・起動の仕方で変わるので手で持つ。

```
[ 共有ライブラリ帯域 (0x400000 - 0x4FFFFF, K3 2026-09-06) ]
0x400000 - text_end                libos32gui.shlib の先頭 4KB ジャンプ表 + .text/.rodata  RO, USER, 全 PD 共有
data_vaddr - data_end              .data/.bss (アプリごとに物理ページを複製)         R/W, USER
0x4FFFFF 直下                      .data/.bss の原本 (ロード時に退避)

[ プログラム空間 (0x500000 - mem_end) ]
0x500000 - code_end                .text + .data + .bss (固定上限なし)        R/W
code_end - guard_a                 newlib sbrk (最低 MEM_EXEC_SBRK_MIN=256KB)  R/W
guard_a  (4KB)                     ★ GUARD A: sbrk上限ガード (位置は動的)     NP
guard_a+4KB - heap_top             exec_heap (KAPI mem_alloc)                 R/W
                                   heap_top = CPL=3: アプリ帯上端のスタックガード
                                   直下 (1 枚なら 0x7BF000、2 枚なら 0xBBF000)
                                   / CPL=0: GUARD B - 動的確保リザーブ
                                   大きさ: OS32X ヘッダ heap_size 指定があれば
                                   それ、0 なら空きを sbrk と折半 (2026-09-04)
  ...    - (mem_end-260KB)         (CPL=0 のみ) 動的確保リザーブの穴 1MB
(mem_end-260KB) - (-256KB) 4KB     ★ GUARD B: スタックovrflowガード           NP
(mem_end-256KB) - mem_end  256KB   プログラムスタック (下向き展開)            R/W
```

  ※ mem_end = `sys_usable_mem_end()`。PEGC/Cirrus の 8bpp バックバッファを使う構成では
    起動時の ⑥ (`gfx_boot_reserve`) が PEGC の BB 300KB をアリーナの上端から池で確保し
    (owner = boot → gshell、台帳の SURFACE)、`sys_usable_mem_end()` をその下端に凍結する
    (`ledger_arena_top`、TASK_T1_LEDGER §3-6。旧 `sys_reserve_top` は T1e で撤去)。
    Cirrus の面はリニア窓の中 (MMIO) なので引かない
  ※ カーネル帯域内の KAPI / SHM の番地は `__bss_end` を基点に動的算出される。
    **だから上の表は `kernel.map` を読まないと書けない** (票 TASK_KSTACK_USER §4-bis)
  ※ 入れ子起動は子として走り終了で親へ戻る (最大 4 段)。CPL=3 のプログラムは
    PD ごとに独立したアプリ帯を持つ (09_exec.md)。帯は 0x400000 から 4MB (PDE)
    単位で 1〜2 枚 (tasks/memory/APP_BAND_PDE.md)。0x400000 帯の shlib .text は共有、.data はアプリごと

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

> **デバイス窓の貸し出し規則 (レビュー #5 ②③、2026-09-06)**
> バックエンドが master PD に張る窓は **supervisor + PCD** で、PTE_USER を付けない。
> `paging_addrspace_create()` は master の PDE を 1024 本すべて写すので、master で
> USER にすると CPL=3 アプリが**表示面**に直接書けてしまい、契約 G4 (commit 前の
> 描画は表示面に出ない) が崩れる。CPL=3 に見せるのは **クライアント面だけ** で、
> `gfx_bb_phys_range()` が返す範囲 (Cirrus: リニア窓 +04B000h の 300KB、PEGC/9801:
> 主記憶のバックバッファ) を exec が `paging_addrspace_map_user_keep()` で
> **アプリ PD ごとに** USER へ昇格させる。この 300KB の PTE は共有 PT にあるので
> master からも USER に見えるが、master 側の PDE には USER を伝播させないため
> 実効権限は supervisor のまま (C2 の「共有 + USER」と同じ模型)。
> `_keep` は既存 PTE の **PCD/PWT を引き継ぐ** — 落とすと CPU が書いた画素が
> キャッシュに残り、Cirrus の BLT エンジンが古い VRAM を読む。
> 不変条件はブート時の kselftest (`paging_map_user_keep_selftest`) が毎回検査する。
> 9801 の主記憶バックバッファ (0x6A000、128KB) は選ばれているバックエンドに関わらず
> **常に** USER にする (レビュー #6、2026-09-06): アプリの gfx_init でアクセラレータの
> setup が失敗すると HAL は 9801 へ落ち、以後 `gfx_get_framebuffer()` が 0x6A000 を返す
> ため。写していないとフォールバック直後の最初の描画で #PF になる (`ring3_guard bb` が
> 「書けて生き残る」ことを確認する)。

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
