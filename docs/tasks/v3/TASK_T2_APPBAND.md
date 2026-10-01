# TASK_T2_APPBAND — T2: アプリ帯 + lease 窓 (設計票)

> 状態: **実装中 (2026-10-01)** — 独立レビュー (Fable 5.1) Approve、固定 PT の置き場は 0x3F1000 (ユーザー決定・PM の実測)。**T2a・T2a′ 着地** (`ce5a2a9`・`279272d`、NP21/W 8/17MB の受入は §5-1 — park → resume 後、R1 panic の故障ゲスト、Ra266 64MB / PCM は未実施)。次は T2b。D1〜D36 を変更せず、T2a〜T2h の間に T2a′ を加える (§5)。
>
> 発行・改訂: GPT-6-astra / Codex (設計者)。初稿調査基点 `b5cd920`、1 回目の確認基点 `705a227`、2 回目の改訂 GPT-6 / Codex、確認基点 `8a7bf4c` (`wt/t2-design`)。以下の `file:line` は初回の実コード調査を引き継ぎ、2 回目の対象箇所は `8a7bf4c` で再確認した。今回の作業はこの worktree の文書だけ、commit / push・配備・NP21/W・NHD・ini・Windows 側の操作なし。
> 決定の正典: [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §0・§2-2・§2-3 ⑥・§3-5・§6 T2・§7・§8-4。位置づけ: [V3_PLAN](V3_PLAN.md) P1 / P7。引継ぎ: [TASK_T1_LEDGER](TASK_T1_LEDGER.md) §1-2・§4-1-R〜§4-6-N。B1: [FEP_BOUNDARY](../settings/FEP_BOUNDARY.md) §4 (D31 で T2 に移管)。
> 番地の正典は `include/memmap.h`、実装後の地図は [02_memory](../../02_memory.md) §2-1 (生成)。本票は T2 の実装契約・段・受入を持ち、上位の決定本文を置き換えない。[C1]〜[C5]・[ABI1]〜[ABI3] は [CONSTRAINTS](../../CONSTRAINTS.md)、状態行は [POLICY_DEV](../../POLICY_DEV.md) §8。

## 1. 範囲と引継ぎ

### 1-1. TASK_MEMMAP_V3 §6 T2 の対応表

| # | T2 の項目 | 担当段 | 契約 / 受入 |
|---|---|---|---|
| 1 | lease 契約を実装より先に確定 | 本設計 → 独立レビュー、T2b | §3。SURFACE 参照と AS 仮想番地を分離 |
| 2 | アプリ帯 0x80000000、exec 0x80100000、lease 窓 0xF0000000、物理定数から分離 | T2b・T2c | §4-1、非恒等・複数 AS / PDE |
| 3 | claim / EXEC_DYN_RESERVE / DEVICE_FLOOR / `--cpl0` / `ring3_band_ram_top` 撤去 | T2c | 旧形式を入口前に拒否、物理を AS owner で配る |
| 4 | exec の共有 USER 写像 (SHM・トランポリン以外) と paging の共有 PT 書換え経路撤去 | T2e | §3-3、master・無関係 AS が不変 |
| 5 | shlib を池の実ページ + AS ごとの text RO / data 私有写像へ | T2c | §4-2、data 分離、text owner=shlib を回収しない |
| 6 | 既定 heap 最小化、可変スタック (OS32X ヘッダ) | T2c (stack)・T2f (heap) | §4-1・§4-4、既定 256KB と指定 512KB |
| 7 | 検査 3 段 (master / AS / 毎起動) と lease 回帰 (d) | T2b で部品、T2e で全適用、T2h | §5-2。V86 セッション外 |
| 8 | app.ld / app_sys.ld / shlib.ld / mkshlib / Rust stub / CRT | T2c、T2e、T2f | §2・§4-6。app_sys の住所変更自体は T3 |
| 9 | 全再ビルド・旧形式拒否・4 種の世代識別 (D7 / D35、P7 と共有) | T2c で拒否基盤、各 ABI 段、T2h | §4-6。常駐シェル・旧 `.o` も例外なし |
| 10 | BB を gshell 所有 + lease に (b) | T2e | §3-4。T1 の移譲を継承、3 backend 描画 / present |
| 11 | R1 の移譲 / 回収 2 段化、panic 化、IRQ broker の深さ統一 | T2a | §4-3、純ループ・PCM の STOP と再 open |
| 12 | `mem_map` / `mem_unmap`、`_sbrk` 末尾 map、64KB 閾値 | T2f | §4-4、伸長 → unmap → 終了で owner 0 |
| 13 | 池不足時だけ trim 通知、起動予約なし | T2g (予約なしは T2c から) | §4-5、池 0 でも閉じる / 次アプリ起動 |
| 14 | R3-e: lease 先頭 PT 1 枚事前確保、追加 PT と全体巻き戻し | T2b、T2c、T2f | PT 枯渇 / lease 表満杯 / 途中失敗で全不変 |
| 15 | FEP_BOUNDARY B1: 新帯の `ring3_ptr_ok`、共通 bounded copy、overflow | T2d (帯判定の切替は T2c・T2e) | §4-7、page 末 NUL / 次 page NP / 未終端、SQLite 進入 0 |
| 16 | R5: AS 終了後ゼロ、永続 owner は別勘定 | T2a 以降全段、T2h | lease / shlib / maps / PD の順、取り残しを隠さない |

### 1-2. T1 の実装結果の上に載せるもの

- T1a は 8MB も model / ONLINE。物理の登録上限は 2GB、`MEM_POOL_BASE` は **現時点 0x400000**。0x500000 に上げるのはシェルが動く T3。T2 の仮想定数を物理検査に流用しない。
- T1b の L2 owner、永続 owner、`ledger_reclaim_owner` / retire、全 IRQ / 全例外の `kctx_*_depth`、8 語 jmpbuf を継承。T1 の `ledger_irq_ops` / `ledger_exc_ops` は計数だけで、STOP と fault で増えるのが観測済み。T2a が両方をゼロにする。
- T1c の DMA API と物理記述子を維持。池の返却と固定 DMA プール内の返却を混同しない。PCM の IRQ 解除は T2a の通常文脈で行う。
- T1d の `ledger_reserve_set` は検証済み resource の span ごとの包含検査・一括 commit・永久保持。RAW PCI BAR は予約権限にならない。lease は DEVICE の予約を解除しない。
- T1e の `ledger_surface_create/find/transfer` と gfx 識別 → 予約 → 写像 → BB を継承。選択中 CLIENT の boot → gshell 移譲は RAM なら L2 ごと、固定 RAM は区間も、MMIO は SURFACE だけ。GUI 終了でも backing を保持する。auto では Cirrus 選択時にも PEGC の予備 BB 300KB がある。
- **8MB + PEGC の修正を取り消す順序**: T1 は `ring3_band_set` の私有上端を `min(帯上端, sys_usable_mem_end())` にし、BB が私有 PTE を上書きする 74 ページの漏れを閉じた。T2c で私有帯を高位へ移した後にこの物理上端依存を外し、T2e で BB の旧共有写像を lease に替える。先に切り詰めだけ外す段は作らない。
- T1f の [C5] を維持。恒等の backing は P2V / P2V_IO、PD/PT の物理は V2P、アプリ / lease の仮想は **AS / SURFACE による翻訳**。`as_va_to_pa` は現状 USER+RW 用であり、B1 の RO 入力にはそのまま使えない。

T1 の残件は消さない: PCM STOP → 再 open は実機で未確認、`cirrus-off` + `GFX=cirrus` 未実施、T1f 実機未実施、HostDrv の番地解釈 24 件の実機確認未実施。前 3 件は T2a / T2e / T2h の回帰でも追う。HostDrv の非恒等ユーザー VA をハイパーコールへ渡す懸念はコード監査で閉じた (§6 R2)。既存例外一覧と実機未確認を、コード上の欠陥疑いとは分けて継承する。

### 1-3. 後続との境界

| 後続 | T2 が渡すもの | T2 では行わないこと |
|---|---|---|
| T3 カーネル帯 | 高位アプリと物理池の分離、**T2a′ で済ませる X15 固定 PD/PT 40KiB の画像外化と最終起点0x3F1000への配置**、縮小したshell heap・予約境界と実測 (§6-1) | カーネル 3MB 化、shell 0x400000、KERNEL_SLACK、DMA 0x3E0000、SQLite 連結、Unicode 組表。固定 PD/PT は画像外化も再移設も不要。下表の周辺領域変更と地図/初期化/予算の再検証を引き継ぐ |
| T4 SQLite | B1 の共通 copy、R1 通常文脈回収、世代検査部品 | モジュールローダ、MEMSYS5 512KB / FEP 予約、F2 / F3、B3 の旧 db_exec/prepare 結線・copy-before-finalize、B4 の engine in-flight fail-stop |
| T5a FEP | B1 の API と失敗時未公開バッファ | B2 の staging → finalize → copyout、RESIDENT 接続 2 本 |
| T5b / T6b | 物理 staging / bundle 定数を仮想定数から切り離す | 起動順・ローダ区間表・同梱・508KiB 解除。T2 の圧縮画像は現上限以内 |
| T5c | owner 回収と D35 検査部品 | ドライバモジュール化、スプラッシュのアプリ化。lease の借用契約だけ先に用意 |
| T7a / T7b | planar のポインタを SURFACE 起点に、低位 USER を撤去 | planar BB の物理移動、低位解放、OpenType、共有グリフキャッシュの選定。暫定 Unicode RO lease は T7 で撤去 / 更新 |
| P4 / Trident | 私有 lease API、台帳由来の属性 | BAR sizing、装置同一性再確認、probe / enable の GUI 境界への移動。T1 の起動時順序を維持 |
| P6 | R1 の安全点・非再入 trim 配送境界 | 自動番犬の方式、長い KAPI の取消期限・最大応答時間の確定、アプリごとの x87 保存復元。T2 の合格を P6 完了とは扱わない |
| P7 | T2c の4世代検査、コンパイル単位の識別、混在試験 | 全 KAPI の整理を独自に広げない。fork 時の並替えは P7 と一組でのみ行う |
| サーフェス層 / P8 / P9 | (b) gshell の CLIENT と全画面 lease | (c) 私有面 + WM 合成、Video HAL、SDL、音・入力の層 |

T3 票へ渡す物理境界 (半開区間。固定 PD/PT 以外の最終配置は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §2-1):

| 範囲 | T2a′ の占有・属性 | T3 の引継ぎ |
|---|---|---|
| [0x380000,0x3F1000) | 常駐shell exec_heap、452KiB、sup/RW/WB | shellを0x400000へ移して解放し、最終地図の鎖/SLACK・DMA [0x3E0000,0x3F0000)・NP guard [0x3F0000,0x3F1000)へ分割 |
| [0x3F1000,0x3FB000) | 固定PD/PT 10枚、sup/RW/WB、恒久FIXED | 同じ番地・枚数を維持。再移設なし、周辺NP化後の再写像は再検証 |
| [0x3FB000,0x400000) | 未使用20KiB、恒久FIXED・NP | guard [0x3FB000,0x3FC000) と kstack [0x3FC000,0x400000)へ。T2ではstackを動かさない |
| [0x400000,0x500000) | T2c以後は物理池の一部 | 常駐shellの新しい1箱。MEM_POOL_BASEを0x500000へ変更 |

## 2. 現状調査 (実コード、初回705a227 / 2回目8a7bf4c)

`0x500000` は exec のロード位置で、**現アプリ PDE の開始は 0x400000** (先頭 1MB が shlib)。定数の置換だけでは動かない。

| 場所 (file:line) | 現状と T2 での処置 |
|---|---|
| `include/memmap.h:352`、`:365`、`:397`、`:416`、`:449`、`:462`、`:472` | APP 0x400000 / 最大2 PDE、DEVICE_FLOOR 0xF00000、SYSTEM_SPACE がその別名、POOL 0x400000、exec=SHLIB_END、固定スタック256KB。SYSTEM_SPACE を独立した物理定数にしてから DEVICE_FLOOR を消す |
| `sdk/link/app.ld:8`、`sdk/link/app_sys.ld:5`、`sdk/link/shlib.ld:20`・`:64` | exec / shell / shlib の直書き。app / shlib を高位へ、app_sys は世代 stamp のみ (住所は T3) |
| `tools/mkshlib.py:48`・`:224`・`:277`、`sdk/rust/os32api/src/gui/stub.rs:64` | shlib 0x00400000 とヘッダ / entry / data の検算・bind。mkshlib の OS32X 包装も変更 |
| `sdk/mkos32x.py:99`・`:145`・`:158`、`exec/os32x_hdr.c:22`、`sdk/include/os32/os32_kapi_shared.h:163`・`:213` | FORCE_CPL0、ELF から load_addr、v3 ヘッダ48B。現在は version>=3、data offset、min API 片方向。未知形式も通る |
| `sdk/crt/crt0.asm:15`、`sdk/crt/crt0_c.c:8`・`:14`、`sdk/crt/syscalls.c:162` | BSS / 引数積み・CRT の kapi 名と stamp・固定 sbrk 上限。スタック実サイズ / 各単位 stamp / 実行中 map へ |
| `sdk/kapi_rust_gen.py:45` | 未知の非ポインタ型を u32 にする fallback。D35 の生成失敗へ |
| `exec/exec.c:301`・`:728`、`exec/appslot.h:113` | 物理上端で band_top を切り詰め、ring3_ptr_ok は低位 VRAM も許す。AppSlot の stack_top / band_top と固定 RING3_USTACK_SIZE を分離 |
| `exec/exec.c:964`・`:990`・`:1013`・`:1039` | exec_child_claim / ring3_band_ram_top / CPL0 claim/release。T2c で撤去 |
| `exec/exec.c:1869`・`:1928`・`:1949`・`:2034` | RAM 上端で PDE 枚数、空きの半分を exec_heap、sbrk の二段先取り、3 領域 map。仮想容量と実ページ供給を分離 |
| `exec/exec.c:2049`・`:2052`・`:2056`・`:2060`・`:2067`・`:2074`・`:2079` | VRAM / SHM / フォント / Unicode / planar BB / 選択 BB / trampoline を USER 化。最終形では SHM・trampoline のみ master に事前作成、他は削除または lease |
| `exec/exec.c:1182`・`:1214` | BB の恒等 USER と領域3本の teardown。可変 map と lease の台帳を回収する方式へ |
| `kernel/paging.h:181`・`:183`、`kernel/paging.c:108`・`:121`・`:127`・`:132` | APP_BAND_PDE=1、PT 配列2本、bootstrap PT 数 / DEVICE_FLOOR / 32MB 未満の ASSERT。高位・疎な私有 PT の ASSERT へ |
| `kernel/paging.c:686`・`:714`・`:755` | RAM 量で仮想帯を制限、master の同帯 PT 必須、identity PTE 複写。高位 PT はゼロから作る |
| `kernel/paging.c:805`・`:820`・`:826`・`:845` | app 外なら共有 page_tables を書き、keep_cache は宛先 PTE から読む。通常 API では拒否し SURFACE 属性を指定 |
| `kernel/paging.c:415`、`kernel/pgalloc.c:174`・`:197`・`:894`、`kernel/memory_boot.c:28`・`:165`・`:333`、`kernel/sys.c:72`・`:122`、`gfx/gfx_core.c:265`、`kernel/kselftest.c:633`、`kernel/physmem.c:97`・`:103`・`:111`・`:119` | **物理 metadata / workspace / L0 RAM / 最低量 / BB 探索・凍結 / 起動試験まで APP/EXEC 定数に依存**。T2c で物理専用の境界へ。PEGC BB は下限 PFN が 0x80100 になると確保不能で候補落ちし、pool:exec range も毎起動 fail になる。L0 の供給開始も仮想帯へ動かさない。T1 の FIXED/ARENA_TOP 経路を維持する |
| `kernel/shlib.c:76`・`:97`・`:214`・`:224`・`:258`・`:284`・`:317` | 1MB 全域 claim、末尾の data 原本、master USER text、連続 data 複製、detach で先に free。ページ単位の池 + 私有 PT、unmap/TLB 後 free へ |
| `exec/exec.c:1621`・`:1629`・`:1483`、`exec/appslot.c:609`・`:643`、`kernel/pgalloc.c:348`、`kernel/irq.c:23` | fault/STOP は深さを下げる前に exec_exit の回収。計数と irq_in_irq は別。T2a で移譲 / 通常回収へ |
| `exec/ring3_str.c:18`・`:36`・`:42`・`:63`、`kernel/paging.c:622`、`kapi/kapi_db.c:210` | ring3_str は返却文字列と RW 判定。db_user_str_copy は1 byteごとだが帯中心の検査。共通 copy は caller PD と RO/RW を区別する |
| `gfx/gfx_core.c:23`・`:30`・`:119`・`:130`・`:135`・`:439` | planar の bb_b/r/g/i 固定初期値、fb は kernel ポインタをそのまま返す。SURFACE 由来の kernel pointer と AS lease VA を分離 |
| `gfx/gfx_core.c:183`・`:297`、`kernel/pgalloc.h:173`、`kernel/pgalloc.c:821`・`:854` | T1 の SURFACE は24B・8本、gen/lease_count は8bit、create は boot 限定。T2 で寿命・属性・ページ占有を強化 |
| `userland/lib/gfx/libos32gfx_core.c:25`、`userland/gshell/src/lib.rs:138`・`:446`・`:644`、`userland/gshell/src/handler.rs:358` | attach で返った fb を保存、WM 復帰で gfx_init、OP_WAIT 内にも WM がある。再 init の revoke と再 attach、trim の安全な配送点が必要 |
| `lib/utf8.c:32`・`:65`、`userland/lib/gfx/text/gfx_kcg.c:32`・`:50`・`:98` | Unicode 表の低位直読は実在 (lib/utf8.c はユーザー側にもリンク)。字形は kcg_read_* KAPI で取得。USER を消すだけでは日本語が fault |
| `userland/lib/gfx/text/lconsole.c:297`、`userland/lib/filer/filer_draw.c:33` | lconsole はローカル配列から gfx 描画、filer は tvram_* KAPI。名前やコメントと異なり、この2経路に TVRAM 物理直書きは無い (U20 の部分調査) |
| `kernel/paging.c:1113`〜`:1213` | 帯定数で AS を作り、master の恒等 PT の複写を期待する selftest。T2c で高位・空 PT・非恒等 backing の試験へ変更し、master 不変 / owner 回収は維持 |
| `exec/exec.c:668`・`:1540`・`:1594` | abort_req 判定 / dispatcher 入口の user_esp 取得 / 引数コピー窓の stack 上端依存。T2a で移譲、T2c で窓を caller AppSlot.stack_top で切り B1 の全範囲検査と併用 (関数定義自体は :665 / :1537) |
| `kernel/isr_handlers.c:353`、`include/config.h:52`、`userland/shell/cmd_sys.c:60`、`exec/appslot.h:173`・`:188`、`exec/exec_heap.h:4`、`userland/lib/rt/dbgserial.c:287` | shlib 障害診断 / 常駐注記 / mem の旧帯表示 / CPL0・固定帯の注記 / 0x500000〜0x500FFF 判定。T2c の変更対象。注記も仮想帯と物理 backing を分け、dbgserial は実際の guard 配置に従う (U8) |
| `userland/shell/rshell.c:877`、`userland/tests/nop.c:12`、`userland/tests/ring3_hello.c:39`・`ring3_fault.c:39`・`ring3_guard.c:63` | tvdump は SHELL_AS_APP の除外外で sh.bin にも入り、TVRAM 0xA0000/0xA2000 を直読。nop は同 TVRAM に OK を直書き、ring3 3本は 0xA8000 マーカー。T2e で tvdump を KAPI、試験マーカーを SHM へ。ring3 3本は deploy.yaml 登録済み、nop は未登録なので受入に使う段で登録 [V2] |
| `gfx/backend_pegc.c:52` | `STATIC_ASSERT(MEM_APP_BAND_DEVICE_FLOOR == PEGC_LINEAR_BASE)` と直前の注記を、T2cで独立した物理定数 `MEM_SYSTEM_SPACE_BASE` との一致へ付け替える |
| `exec/exec.h:31` | `EXEC_LOAD_ADDR` は `MEM_EXEC_LOAD_ADDR` の別名で利用者0 (定義以外の参照なし)。T2cで別名を撤去 |
| `userland/tests/db_v50_test.c:28`・`:160` | `VRAM_END=0x0C0000` は旧許可帯の末端という期待値。T2eでは未貸与VRAM自体の拒否と、実際の許可済みRAM範囲末端を跨ぐ拒否を別ケースにし、旧許可帯の説明を撤去 (§4-7) |

T2c の物理専用境界は、L0供給開始に既存 `MEM_POOL_BASE=0x400000`、BB探索/legacy arena最低位置に物理floorの新定数案 `MEM_PHYS_EXEC_FLOOR=0x500000`、metadata/workspaceの旧最大帯境界に `MEM_PHYS_WORKSPACE_FLOOR=0xC00000` を使い、仮想APP/EXEC定数の別名にはしない。sys/kselftestの最低量は物理floor+必要byte数として検査する。T2c〜eの暫定heap予算helperもこの物理側だけを使う。実装時は §2 の各ファイルとhost試験を一組で検索し、残る APP/EXEC 参照を仮想用途として説明できることを受入条件にする [C4]。

`EXEC_DYN_RESERVE` は `exec/exec.c:176` の256ページの定義と `:972`・`:1751` のCPL=0用レイアウト計算に残る。claimと一緒にT2cで撤去し、残存検査を行う。`tools/mkos32x.py` ではなく **`sdk/mkos32x.py`**、stub は **`sdk/rust/os32api/src/gui/stub.rs`** が現実の場所。

## 3. lease の契約 (T2 の設計として固定する案)

以下を独立レビューの対象とする。物理を直接指定する公開 map、旧 `{virt, phys, size}`、共有 PT を昇格する案は採らない (X3)。

### 3-1. 記述子・容量・公開 API

**記述子は SURFACE 台帳への参照 `{sid, generation}`**。物理番地も kernel pointer も利用者へ公開しない。内部 SURFACE は owner、backing、ページ範囲、backend/role、geometry、プレーン配置、最大権限、cache、generation、lease_count を持つ。`gen` は u32、参照数は u16 へ拡張する。世代は再定義時に増やし、周回時はその slot を再利用不能にする (同じ boot で古い参照を復活させない)。owner ID 再利用と独立に判定する。

初期上限は **SURFACE 16本、lease 8本/AS、mem extent 32本/AS、アプリ PT 64本、lease PT 56本**。PT の配列は物理ポインタの控えだけで、未使用要素は0。lease 表は AS の固定配列で、実行中に増設しない。SURFACE は 1 本が連続したページ区間を占有し、planar BB の plane はその内側の byte offset で表す。互いに不連続な表示 VRAM 4 plane は4本の SURFACE を1操作でまとめて lease する (8本の上限の内側)。MMIO の余白を跨ぐ巨大な連続 SURFACE を作らない。

公開 ABI の形 (名前は実装時に KAPI 正典に登録、構造体は C89 互換、u32 欄で固定):

```c
/* 物理とカーネルポインタを含まない参照 */
typedef struct { u32 sid, generation; } OS32_SurfaceRef;
/* query は geometry/plane offset/許可権限を含む値の写しも返す */
int gfx_surface_query(u32 role, OS32_SurfaceDesc *out);
/* 成功時だけ out に {token, base, bytes, planes[4]} を返す */
int surface_lease(const OS32_SurfaceRef *ref, u32 access, OS32_LeaseView *out);
int surface_unlease(u32 token);
```

- query は **選択中 backend の、呼出元に許可した role だけ** (Unicode表はbackend非依存のシステムRO roleとして別判定)。単なる sid の知識は権限にならず、lease 時も caller / role / generation を照合する。`access` は RO / RW だけ、PWT/PCD や任意の物理は渡させない。ユーザー指定 AS は受け取らず、保存済み caller AS に貸す。
- token は boot 内の単調 u32 (0無効)、AS に紐付く。周回時は新規貸与を拒否し、再 boot まで再利用しない。別 AS の token、二重解除、revoke 済み token は負の誤りで無変更。
- **lease 結果の VA は記述子とは別**の `OS32_LeaseView` に返す。同じ参照でも AS ごとに異なる VA になり得る。同じ AS の明示 lease 呼出しは別 token。SDK の attach は保持中 token を再利用し、繰返し query だけで参照数を増やさない。
- out / ref は B1 の checked copy で kernel 内に確定し、出力範囲を事前検査する。失敗時は out・PTE・refcount・owner 会計がすべて元のまま。誤りは INVAL (参照/権限/範囲)、NOMEM (物理/PT)、FULL (固定表)、旧世代は INVAL と診断理由で区別する。実際の `OS32_ERR_*` への対応を生成時に固定する。
- `gfx_get_framebuffer` のソース互換は SDK の薄い橋で保つ。CPL=3 には lease view の plane VA を返す。新しい失敗可能な attach を SDK 内で使い、失敗を無視して NULL 面へ描かない。古い void API のカーネル入口を残す場合は失敗を明示的なアプリ終了に変換する。trusted kernel / WM には内部 `gfx_kernel_framebuffer` で恒等のポインタを返す。公開 query の出力と内部 framebuffer の型を流用しない。
- API は `sdk/kapi.json` から C/Rust とラッパを生成 ([C3][ABI1])、既存 slot は通常追記 ([ABI2])。P7 が D35 の1回の整理を実施するときだけ、世代を切る同じ成果物集合で統合する。T2 が独自にスロットを並べ替えない。各追加で version と KAPI_VERSION を揃え全再ビルド ([ABI3])。

### 3-2. ページ占有・属性・貸与トランザクション

1. SURFACE 登録時に `[first, first+npages)` の全ページの backing / owner または DEVICE の検証済み資源と写像範囲を検査する。geometry の乗算 / offset+長さ / 丸めの overflow を加算前に拒否し、plane が span 内に収まることを検査。ページ端の未使用 byte は同じ SURFACE の占有として扱い、別用途・別 owner・表示面を同居させない。RAM の padding は公開前にゼロ。DEVICE の decode と実際に貸す面の範囲は別。
2. cache は backing 台帳が正: RAM=WB、MMIO/VRAM=UC (既存 PCD/PWT の表現に統一)。SURFACE の cache は根拠との一致を検査する写しであり、caller が上書きできない。kernel alias・全 AS alias が一致しなければ貸与を拒否。新しい PTE の値から `keep_cache` で継承しない。
3. 通常文脈で caller owner を固定し、slot / lease VA の空き / 全 plane の権限 / 出力先を先に検査。先頭 PT は AS 生成で1枚事前確保 (R3-e)。足りない PT は AS owner でページ単位に準備し、ゼロ化。4 plane の束は **全本数**の slot / PT を先に用意する。
4. 準備中の PTE は AS に公開しない。新規 PT への下書き、既存 PT への変更予定を走査可能に保持し、最後の短い IRQ 保存区間で PTE・lease 表・refcount を一括公開。準備中に WM / callback / AS 切替を行わない。失敗で新規 PT と今回の予約を戻す。既存 PT / 既存 lease / master / 他 AS / 永続 reservation は変更しない。
5. active AS の TLB は CR3 再ロードで反映 (386 下限のため invlpg を前提にしない)。非 active AS は再開前の CR3 ロードが必須。private PT を共有したり、global TLB にして保持する最適化は導入しない。成功後にのみ出力値を公開する。

### 3-3. 解除・revoke・隔離

解除順は **当該 AS の PTE を NP → TLB 反映 → lease 表を失効・参照数減算 → owner が返却済みかつ参照0なら backing 回収**。非 active AS は現在の CR3 が当該 AS でないことと次回ロードを保証してから参照を落とす。先頭 lease PT は AS 終了まで残し、追加 PT は空になれば同じ順で返す。DEVICE の予約と kernel alias は永久保持。

owner が返却を要求した面は新規貸与を拒否する状態にし、全 lease が消えるまで backing を保持する。通常 AS の終了では全 lease を先に revoke し、shlib data / private map / PT / PD の回収を行う。gshell の永続 CLIENT は AS 回収の対象外。片方だけの revoke は他 AS の表・PTE・TLB・参照を変えない。

**「2 AS に貸して互いに見えない」**は、未貸与面と lease 操作の波及を防ぐ意味。同じ面を A/B に明示的に貸せば画素は共有する。試験では S を A だけ、T を B だけ、U を両方に貸し、未貸与 S/T の拒否、U の画素共有、A の U revoke 後も B が使えることを分けて測る。

通常 map / unmap API は private PT の所有確認と対象仮想帯の確認を行い、**共有 PT を書く要求を無変更で拒否**。master を書く `paging_set_page` / `paging_map_range` / `paging_map_phys` も監査し、live AS 後の USER 昇格を禁止する。SHM / trampoline の共有 USER は最初の AS より前に専用の初期化口で作る。V86 は専用 session 口だけが低位 USER を変更し、正常・異常脱出で復元する。V86 中に通常 AS の生成・実行・地図検査をしない (CUI の呼出元 AS は停止中)。

### 3-4. gfx・再 init・過渡的な低位データ

- 通常 GUI には **CLIENT** を貸し、DISPLAY / VRAM は貸さない。CLIENT は (b) の共有 BB、アプリ私有面ではない。Cirrus の CLIENT は MMIO backing でも表示領域とページで分離した面なので許可し、DISPLAY と区別する。通常 GUI から禁止する「VRAM」は低位 VRAM / デバイス窓の直アクセスと DISPLAY lease を含み、正当に貸与した CLIENT alias は含めない。
- DISPLAY (planar / PEGC / Cirrus) は GFX 宣言 + 現在の全画面所有者にだけ。T1 の Cirrus DISPLAY `perm_max=NONE` は T2e で「全画面授権時のみ RW」にする。TVRAM は CUI 全画面所有者へ `[0xA0000,0xA4000)` だけ。通常 GUI へは拒否。backend 変更や所有権剥奪で先に revoke。U20 の未監査アプリには物理直書きを残さず KAPI か TVRAM lease に直す。
- kernel の bb_b/r/g/i と bb[] は選択 CLIENT の backing + plane offset から毎回設定する。planar の plane stride は **pitch × height** (400行なら32000B) で、ページ境界ごとに勝手に丸めない。128KB の占有と有効画素範囲は別。packed は plane0 のみ。kernel は P2V/P2V_IO、アプリは lease_base+offset。
- T2 の planar backing は低位固定のまま (移動は T7a)。それでも固定初期値への依存を消し、再 init / 200行・400行の変更で pointer と geometry が一致するようにする。PEGC の連続 BB は起動時確保を維持。gshell への移譲前の CUI 全画面 / スプラッシュは boot owner の面を借り、終了で lease だけ返す。
- 再 init は **新規貸与停止 → 全関連 lease revoke → TLB → refcount0 → backend 初期化/geometry更新 → generation更新 → 面公開**。失敗時は失効した旧 token を復活させず、予約済み fallback の面を新世代として公開する。GUI → CUI → GUI でも RAM backing の確保し直しは不要。SDK は保存済み framebuffer を捨て、resume / attach 時に再取得してから描く。生ポインタの失効を魔法で検出できるとはしない: 旧 mapping は NP、旧 token での操作は拒否、後の VA 再利用は通常の解放後ポインタと同じ禁止契約。
- **libos32gfx の2実体**: アプリ静的リンク側と libos32gui.shlib 内側は保存状態が別 (`userland/lib/gfx/libos32gfx_core.c:25`、`userland/rust/libos32gui/src/client.rs:185` (attach_gfx、shlib側からの呼出しは `shlib.rs:437`))。両方を使う GUI アプリは同一 AS の CLIENT lease を **2本**消費し、8本/AS の内数。token の再利用は各実体内で行う。Unicode の各実体分も含め容量を検査し、満杯時は一括巻戻し。再 init / resume 後は両方の保存 pointer/token を捨て、双方を再 attach してから各描画経路を再開する。受入は両方で描画し、片方だけ再取得する変異を検出する。
- **U20 の確定対象**: tvdump は TVRAM の文字/属性を値で返す checked-copy KAPI に置換し、`tools/tvdump_recv.py` の TVDM 形式を保つ。通常 GUI の権限を広げず、CUI 全画面所有者だけへ提供 (非所有者は明示拒否)。nop / ring3_hello / ring3_fault / ring3_guard の判定用マーカーは、テストに割り当てた SHM 枠へ移す。SHM の番地取得・初期化を CRT 非依存試験にも用意し、観測側も同時に更新。保護違反を試す VRAM アクセス自体は残し、直前マーカーと目的の fault を区別する。
- **Unicode の移行穴を閉じる**: `lib/utf8.c` のユーザー用ビルドは、低位固定表でなく起動時取得した **RO lease** のポインタを使う (CRT と shlib 初期化の両方)。kernel 版は恒等のまま。表128KBは kernel 所有の固定 RAM SURFACE とし、T7 の新表 / 廃止までの橋とする。これは共有グリフキャッシュ U21 の採用ではない。字形は既存 `kcg_read_*` を用い、フォント低位全域の USER を消す。apps/game を含め直読が残るなら同じ移行段で直す。

## 4. exec / 仮想メモリの設計

### 4-1. 配置と可変スタック

| 仮想帯 | T2 の方針 |
|---|---|
| 0x00000000〜0x7FFFFFFF | 恒等 supervisor、USER は SHM / trampoline だけ (V86 session 例外)。物理 RAM 上限 2GB |
| 0x80000000〜0x800FFFFF | shlib の予約仮想1MB、text RO共有・data AS私有 |
| 0x80100000〜 | exec image + BSS → libc heap、下から伸長 |
| 0x88000000〜 | exec_heap の予約起点、上向き伸長。既定64KBだけ map。仮想予約は物理を先取りしない |
| 0x90000000 の直下 | 可変スタック (既定256KB) と直下1ページNP guard。大きな map は guard より下から下向き、exec_heap と衝突したら ENOMEM |
| 0x90000000〜0xEFFFFFFF | T2 では未使用 (アプリ帯上限64 PDE=256MB)。勝手に master MMIO を継承しない |
| 0xF0000000〜0xFDFFFFFF | AS 私有 lease 窓、先頭4MBのPTだけ先取り |
| 0xFE000000〜 | 既存 device / ROM 窓、sup+PCD 等の台帳属性、AS は master のまま共有 |

上記 heap の分け目は T2 の仮想配置方針として `memmap.h` に置き、リンカ / Rust の写しは生成か一致検査で管理 [C4]。2MB は私有総量の受入例で上限ではない。64 PDE 全部の PT は確保せず、image・heap最小・stack が触る分だけ作る。metadata / workspace は T1 の物理配置を維持する独立定数 (旧12MB境界など) にして、0x90000000 との比較をさせない。将来の T3 で物理配置方針を改める。

OS32Header の**既存欄 `stack_size` (offset 0x20、byte) を有効化**する (`sdk/include/os32/os32_kapi_shared.h:196`、現包装は `sdk/os32x_hdr.py:266` で予約値0)。0=256KB の既定はそのまま、0以外はページへ切り上げ、最低16KB、最大は仮想配置に収まる量とする。負数相当・加算/切上げ overflow・image/heap/guard との重複を拒否。指定量は起動時に全 map し、demand paging / stack 自動成長はしない。argv / alignment / entry frame が stack 内に収まることも先に検査。AppSlot は `stack_base, stack_size, stack_top` を保存し、全 kill / park / resume / teardown でその値を使う。

`heap_size==0` は exec_heap 64KB、libc sbrk の初期追加量は1ページ (BSS末尾の端数は利用可)。指定 heap は切り上げた指定量 (最低64KB)。旧「空きの半分」・sbrk 先取り二段を撤去。初期物理不足なら完全に巻き戻して起動拒否し、起動用の予約は設けない。CPL=0 は常駐シェルだけとし、通常アプリのフラグで昇格できない。

### 4-2. shlib と loader

shlib の **仮想**1MB は予約したまま、物理1MB claim を消す。ヘッダ・text/rodata・data原本は owner=shlib の実ページ数をページごとに確保し、VFSからページ単位で読み、検証完了まで公開しない。物理連続を要求しない。entry は shlib の実行範囲内、data 範囲・ページ数・OS32X と内側 shlib の世代の一致を検査。原本は恒等 supervisor、AS の text alias だけ RO+USER、data/BSS 複製は AS owner。途中失敗は原本 / attach の準備済みページを全返却する。

master の高位アプリ PDE は **空**。CPL=0 の gshell は現コードでは shlib stub 呼出しを確認しなかった (自身の描画実装 + libos32gfx)。T2 では常駐シェルは静的リンクを契約とし、ビルド検査で高位 shlib への参照を拒否する。shell を走らせるために master に shlib USER を残さない。将来 shell が shlib を使うなら専用 AS の設計が別途必要。

アプリロードも仮想と物理を区別する。master のまま高位 VA へ vfs_read / memcpy しない。AS のページを翻訳し恒等 backing にページ単位で読み込む。argv / image / shlib data のコピーも同じ helper。実行開始直前だけ CR3 を AS へ切替。子の終了では parent CR3 / heap state を正しく復元して WM callback を呼び、master のまま親の高位 heap に触らない。

### 4-3. R1: 移譲 → 通常文脈の回収

T2a は高位化より先に旧配置で着地できる。

1. IRQ の要求は `abort_req` と理由だけ。EOI 済みかつ被割込み CS.RPL=3 の脱出点で状態を ABORT_PENDING にし、対象ID/終了種別/復帰点を控えて longjmp する。**FD・PCM・shlib・lease・AS・池・kfree をここで触らない**。カーネルの KAPI 実行中は要求を残し、安全点まで移譲しない。
2. **exec_launch の setjmp 着地 (`exec/exec.c:2094`、exec_run / exec_start) と、exec_resume が取り直す別の setjmp 着地 (`:2648`) の2か所**を共通の pending 回収 helper に接続する。両方で trusted stack / CR3 / owner を整え、jmpbuf の深さ復元を確認し **IF=1 に戻してから**既存 `exec_exit` 相当の共通回収へ。park と ABORT_PENDING / FAULT_PENDING を分岐し、park では回収しない。再 longjmp で同じ pending を処理しないよう先に一度だけ消費する。正常 sys_exit / WM exec_kill は通常文脈からこの回収へ直行する。
3. #PF/#GP だけでなく #DE/#UD 等、**従来 app-kill と分類していた全例外**も FAULT_PENDING の移譲 → 着地回収にする。kernel の障害を一律 app-kill に変えない。B4 の engine fail-stop は T4 の境界で、SQLite の途中を安全に巻き戻せると T2 では主張しない。
4. 回収は対象 owner を明示: 装置停止・IRQ解除・FD等の利用終了、lease revoke、shlib detach (PTE消去/TLB→data free)、全 private extent / stack / image free、PT/PD破棄、owner pages=0 / retire。WM通知は parent の文脈で行う。既存 cleanup の実順と依存を T2a の trace 試験で固定する。永続 boot/gshell/shlib/DEVICE owner は返さない。
5. 深さは全 IRQ / 全例外の `kctx_*_depth` に一本化。broker の `irq_register/unregister` も全 IRQ 深さを見る。`irq_in_irq` を別の真実として残さず、診断互換が必要なら同じ深さの読み口にする。**経路の移譲が完了してから** `ledger_note` の違反を panic に変える。IF=0 は深さの代用にしない (通常の短い IRQ 保存区間は許可)。boot の broker 自己診断による violations と STOP による差分を区別する。

P6 に渡す前提: KAPI 更新途中の AS 切替なし、owner 明示、trim は安全点、x87 は別要件。純 CPL=3 loop と KAPI 連打の強制停止の受入を持つが、kernel 自身の無限待ちの救済や停止時間上限の保証はしない。

### 4-4. R2: map / unmap と allocator

```c
void *mem_map(u32 bytes, void *hint, u32 flags);
int mem_unmap(void *base, u32 bytes);
```

伸長用の公開口はこの2本だけ。`flags` は既定0 (hint優先・空いていなければ別の穴) と EXACT (希望位置が取れなければ失敗)、TOPDOWN (大塊用) の定義済み組だけ。保護属性・物理・owner は指定させない。全て匿名ゼロ済み RAM、USER+RW、AS owner。size0、非ページ整列hint/base、32bit overflow、帯外 / guard / image / shlib / lease / SHM との衝突、未知 flag を拒否。bytes は map では安全にページ切上げ、unmap ではページ倍数必須。失敗の map は NULL、unmap は負値。失敗で既存写像と heap limit を動かさない。

- 物理は1ページずつ。VA探索・extent slot・必要PT・全物理を用意し、公開直前まで未完 map を呼出元に見せない。既存 PTE を上書きする MAP_FIXED は持たない。準備途中のページは未公開 PT の PTE 等で追跡し、失敗時に全件返す (巨大なスタック上 PFN 配列は作らない)。
- unmap は **呼出元の mem_map extent と allocator用初期heap extent** だけ。image / stack / shlib / lease を混ぜた範囲は全拒否。部分解除を許可し、分割で記録が足りなければ無変更で FULL。NP→TLB→owner free。隣接した同種 extent は併合し、伸長ごとに32本を消費しない。ポインタ再利用後の二重freeは allocator の責任で、カーネルは記録と owner を照合する。
- `_sbrk` は signed incr / pointer overflow を検査し、現 break より先が必要ならページ末尾を hint に EXACT map。**_sbrk は非連続の結果を連続と偽って返さない**。続きが取れない場合、上の malloc が flags=0 の別 extent を新しい arena として扱う。newlib の MORECORE と map arena の接続を SDK 内で実装し、_sbrk 単体は −1 / ENOMEM。負の incr は break を下げるだけで、ページ返却は allocator が末尾の生存参照なしを確認した trim の時に行う。
- `malloc` / `calloc` / `realloc` / `free` と Rust allocator の対応を揃える。**要求64KB以上**は TOPDOWN map、free で即unmap。headerを足した結果で閾値を変えない。overflow の calloc、realloc失敗で旧内容保持、大小を跨ぐreallocを試験する。64KB未満は heap、完全に空いた末尾ページを trim 可能にする。
- 既存 `exec_heap_alloc` / mem_alloc も共通の map 実体で伸長し、大塊は同閾値。shell の固定heapは従来のままで、poolを使い切っても閉じる操作が動く。user heap のメタ情報を kernel が無検証で辿って他 owner を返さない。KHeap の部分trim/複数arenaが必要なら明示的な検証・境界を足す (汎用 KHEAP を池へ伸長しない)。allocator の具体的な実装選択 (TLSFを含む) は T2f の実装設計で実サイズと試験から決める。
- R5: extent の回収後 owner 0。`ledger_reclaim_owner` の最後の掃除だけで漏れを隠さず、`exec_as_leftover_pages==0` を要求する。起動失敗でも PD/PT・lease・shlib data を含め全返却する。

### 4-5. D24: trim 配送は非同期

**mem_map の失敗時には要求 bit / epoch を記録して NULL を返し、KAPI の内部で WM を回さない**。通知は「物理池不足」のときだけ (VA断片化・固定表満杯・引数不正では出さない)。back GUI の AppSlot に1 bitを合成して保持し、追加メモリを使わない。CUIと終了中のASは対象外。

SDK の GUI イベントループが、安全な既存 park / resume の配送で trim event を受け、malloc の末尾空きページと未使用map arenaを返す。任意キャッシュ解放 hook は allocator が操作中でないときだけ1回呼び、再入bitで二重配送を防ぐ。応答しないアプリを待たない。front の SDK は map失敗後、allocatorの操作を終えて安全に yield できる GUI 文脈なら **1巡だけ yield → 1回再試行**し、できない文脈ならそのまま ENOMEM。retry のための新しいメモリ KAPI は作らない。

**端末配下の CUI 前景**では D24 の帰結として、裏 GUI が park したままなら通知を記録してもその場で返却させられず、前景に ENOMEM を返す。CUI の失敗を隠す同期 WM pump は追加しない。CUI が戻って次の安全点で裏 GUI が配送を受けること、応答後の別要求は成功し得ることを T2g で受け入れる。

trim 専用の要求・配送・完了を別々に記録し、KAPI 全体の更新中を示す状態と `ring3_wm_depth` を混同しない。syscall出口でも未完の caller wrapper / allocator がある間は他ASを実行させず、既存の安全な park に戻してから配送する。これは P6 の「KAPI内でASを切り替えない」の実装要件。pool 0 のときも bit・イベントの受け皿・閉じる/STOP経路は既確保。起動予約は無し (D25)。

### 4-6. D35 / P7: 世代と全再ビルド

4つを別の欄として持ち、相互の代用品にしない。

| 識別 | 検査 |
|---|---|
| OS32X 形式版 | 新版の完全なヘッダを読めたこと、既知の**完全一致**。未知の上位版、短いヘッダ、未知flagを拒否 |
| KAPI ABI 世代 | 呼出規約・slot / data配置を識別、完全一致。同じ世代の中だけ min_api_ver <= kernel機能版 |
| メモリ配置世代 | high app / lease / stack契約を識別、完全一致。T3 の shell 再配置時も改訂 |
| shlib プロトコル | GUI/shlib 呼出規約とentry配置の世代を完全一致。依存しないバイナリは明示の「依存なし」、依存があるのに0は拒否 |

T2c で OS32X 次版 (現v3の次) に3世代欄を追加し、既存 stack_size 欄を有効化する。具体的な世代値は P7 と共通の正典から生成し、番号を別々のツールに手書きしない。image種別ごとに load_addr と entry範囲を検査し、**shellも `!is_shell` の外で照合**、load_addr=0 の警告続行を廃止。全拒否は entry を呼ぶ前、できるだけAS確保前。shlib は公開前。常駐shell不一致は起動停止と再構築案内で、旧shellから更新する想定を置かない。

各 C コンパイル単位へ世代付き参照と note、Rust の各 crate / codegen object へ同等の識別をビルドから強制付与。CRT1個のstampだけで済ませない。リンクに使った object / archive member の欠落・旧値・混在を入力検査し、KEEPした最終ELFの印・旧シンボル・配置と包装時に再照合する。未使用memberと実際に取り込んだmemberを区別し、vendor newlib等の世代非依存部品はビルド台帳で列挙する。asm CRT・LTOも対象。**新SDKで旧.oを包む**テストを必須にする。Rust生成器の未知非ポインタ型は生成失敗、構造体戻り値は追加せず int/u32 + checked out pointer (EAX戻り) に揃える。

各 ABI 変更後は `make clean` → `make all`、さらに **clean-external を明示して apps/game を再ビルド** (`make clean` だけでは消えない)。kernel・loader・SDK・CRT・ライブラリ・shell・shlib・その段で存在するモジュール・in-tree・apps/game のビルドID / 4世代 / hash を一組のmanifestにする。**CPL=3 の `userland/sh.bin` と `userland/tests/*.bin` も列挙**し、常駐 shell.bin / gshell.bin で代用しない。private submoduleを取得できなければ完全な成果物とは報告しない。

v2戻り先とv3の成果物/配備先を分離し、PMが停止中に一組を更新してから起動する [D1][D2][V1]。既存 settings/辞書/ユーザーデータを保つ。T2の今回作業では配備しない。P7との共有受入は **旧アプリ / 旧shell / 旧shlib / 新SDK+旧.o / v2 SDK製apps/game / HostDrvとNHD食違い / 未知形式版**。入口直前の到達カウンタ0で拒否を確認し、単にクラッシュしたことを拒否の証拠にしない。旧.kcgfontの廃止はT7a、モジュール世代の実ローダ適用はT4以降。

### 4-7. FEP_BOUNDARY B1

`exec/ring3_str.[ch]` に `copy_caller_cstr` / `check_caller_write_range` / `copy_to_caller` を共通化し、`kapi_db.c:210` の db_user_str_copy を移す。callerの出自をsyscall入口に保存し、WMがtrustedとして呼ぶ経路を明示する。現在CPLやCR3==masterやポインタの低さでtrustedと推測しない。

`ring3_ptr_ok` は NULL (個別APIが判定)、新app/shlib/stack/SHMと**現在のASに存在する許可済みlease**を早期分類する。これだけで読み書き可能とはしない。B1 の文字列・DB/FEPのデータコピーは通常RAMとSHMを対象とし、MMIO/VRAM lease を拒否 (一般のgfxポインタと別の方針)。RO RAM lease / shlib rodata は入力可、出力不可。

- `[start,start+len)` は `len > limit-start` 等で **加算前**に検査。NULL非ゼロ長・件数積・page丸め overflow・帯跨ぎを拒否。保存したcaller PDと実行時CR3の一致、PD/PT frameが管理下でpresent、PDEのPS拒否、PDE/PTEのPRESENT|USER、出力は両方RWを確認。T1fのRW専用walkはread/writeモードを分離し、既存出力ガードを弱めない。
- T2c 以後は低位の物理 backing が全 AS で恒等 supervisor のため、現 `ring3_user_range_writable` の master CR3 往復 (`exec/exec.c:913`・`:919`) は不要。T2d で caller PD の walk を直接行う形に整理できる。過渡的に残しても権限検査を省略せず、IF/CR3 を必ず復元する。
- NULまで **1バイトずつ検証してから読む**。page末NULなら次pageを触らない。容量はNUL込み、未終端/長過ぎは失敗し切り捨てない。未完成bufferはSQLite/VFSへ渡さない。trustedでも容量とNULは確認する。
- 短いwalk/copyだけEFLAGS保存→cli→入口IF復元、全エラー出口を同じ規約にする。allocation / callback / GUI pump / SQLite / VFSは区間外。copyoutは全範囲検証後に書き、通常の範囲エラーで部分出力しない。IF=0のI/O facade拒否はT4/T5aの責務。
- T2d は **既にdb_user_str_copyを使う入口**を共通関数に置換し、正常と不正の両方を実callerで試験する。旧db_exec/db_prepareの全結線と旧stmt finalize前コピーはT4 B3、FEP facade全体はT5a B2。T2の受入「SQLite進入中の#PFにならない」はB1を通る対象入口についての保証で、未移行の全APIへの保証としない。

## 5. 段の分割と受入

### 5-1. 単独着地の単位

以下は**将来の実装・試験計画**で、今回実行した結果ではない。各段は前段までの正常起動を保ち、実ソースILP32ホスト試験でRED→GREENを記録する。追加試験は tests inventory / check_map / build に登録し、起動用guestは自層deploy.yamlへ [V2]。変異は写しの木で行い、コンパイル失敗を実行時REDに数えない (意図したASSERT拒否は別集計)。各段の `make check-changed`、PM着地の `make all` + `make check` を行う。

| 段 / 依存 | 主な変更ファイル (実装時) | kselftest・ホスト・変異の受入 | NP21/W / 実機の受入 |
|---|---|---|---|
| **T2a R1** / T1 | exec/exec.c、appslot.[ch]、kernel/isr_stub.asm・isr_handlers.c・setjmp.asm・irq.[ch]・pgalloc.c、PCM回収の呼出境界 | 移譲traceでfree/cleanup=0、着地IF=1/depth0・対象ID/親復元・二重回収なし、全例外と全longjmp入口。初回 launch と park→resume 後の fault/STOP を別々に通す。kselftest通常深さ0/owner往復、panicはホスト捕捉と別故障起動。変異: IRQ中teardown、IF復帰削除、brokerの旧深さ、pending再消費 | 8/17MB旧配置でfaulttest pf/gp/de/ud/loop/kloop、STOP後free baseline。Ra266 PCM STOP→tick進行→再open/再生、irq_ctx_violations差分0。V86往復 |
| **T2a′ X15 前倒し** / a | memmap.h、build/os32.ld、paging.[ch]、memory_boot.cの固定区間、tools/gen_memmap.py・地図host試験、02_memory §2-1生成ブロック | §6-1。PD1+boot PT8+device PT1を画像外へ、10枚の恒久予約・sup/RW/WB・CR3/PDE物理照合・NP後再写像・再初期化禁止。shell heap上端/非整列/重複/写しずれをASSERT/地図変異で拒否。irq_saveからPG有効化・IF復元までとSQLiteの既存伸び代維持を検査。前後size実測が必須、合格前にbへ進まない | 8/17MB旧配置でboot/selftest/GUI/CUI/V86/STOP、8MB FIXED台帳2枚が無傷。64MBは実機でboot/ONLINE/32MB超写像と動的PT併存、全構成で固定10枚が配布されない。ホスト8/17/64地図も必須 (guest代用不可) |
| **T2b 私有lease基盤** / a′ | kernel/paging.[ch]・pgalloc.[ch]、新 exec/lease.[ch]、kselftest、gfxのSURFACE型板 | 公開callerはまだ切替えず合成ASで新lease窓だけを試す。先頭PT1枚、追加PT、属性/ページ端/世代/権限/8本満杯/複数plane一括。master+他ASのPTE全比較。変異: sharedPT書込、PCD落とし、TLB前free、途中rollback欠落、世代無視。kselftest S/T/Uの2AS往復 | 17MBの既存GUI/CUI/V86回帰、新boot selftest fail0。旧USER経路はまだ利用中なので最終地図検査とは数えない |
| **T2c 高位配置と形式切替** / b | memmap.h、paging (帯 selftest 含む)・physmem・pgalloc・memory_boot・sys・kselftest・gfx_core・backend_pegc.c・shlib、exec/appslot/exec_heap.h・exec.h、isr_handlers・config.h・cmd_sys・dbgserial、OS32X共有ヘッダ・os32x_hdr、sdk/crt、sdk/link/3本、sdk/mkos32x.py、tools/mkshlib.py、Rust stub/shlib、生成器・build・app.conf・cpl0_probe撤去 | 物理定数切離し、PEGCのASSERT付替え・未使用EXEC_LOAD_ADDR撤去、疎PT、可変stack、shlib page供給、D35の全入口。kselftest高位AS/owner、hostで断片化pool・2PDE超・shell load照合・旧.o混入・起動失敗全段。変異: RAM上限でVA判定、固定stack teardown、master高位写像、未知形式受入 | 全再ビルド一式で8/17MB CUI/GUI/入れ子/park/fault、256/512KB stack。旧CPL0/旧shell/旧shlib拒否。**T2c〜T2dは旧低位共有USERを継続** (旧gfx consumerの回帰を保つ)。最終T2保護の受入はT2e |
| **T2d B1 共通copy** / c | exec/ring3_str.[ch]・exec.c、kernel/paging.[ch]、kapi/kapi_db.cの既存checked入口、関連host | §4-7。kselftest RO入力/RW出力、hostでcaller/master相違・page末NUL・次NP・未終端・全overflow・PS・MMIO・IF両値・失敗out不変。変異:masterで検査、RW条件欠落、NUL後先読み、先にstrlen、無条件sti | 未終端/跨ぎ入力KAPIが失敗しSQLite entry0、既存正常DB/FEP・GUI描画。帯外dispatcher killとcopy失敗を区別 |
| **T2e gfx/低位USER切替** / d | gfx/gfx_core.c・backend3本、exec/exec.c・lease、paging共有経路撤去・v86_mem.cの通常PDE権限復元、sdk/kapi.json・生成物、userland/lib/gfx、lib/utf8.cユーザー版、CRT・Rust shlib初期化、gshell lib/handler、rshell.c・tests/nop・ring3_hello/fault/guard・db_v50_test.c・マーカー観測側・deploy.yaml、kselftest | §3のAPI/BB再取得 (gfx両実体)/Unicode RO lease、tvdump KAPI/SHMマーカー、db_v50の旧VRAM許可帯期待値撤去と拒否/境界試験、SHM/tramp事前作成、3段検査全適用。hostは実backend選択→fb出力まで (T1eの模擬選択だけで済ませない)、予約/登録/写像失敗・fallback/reinit。変異:旧bb pointer、plane offset誤り、revoke他AS、余白別owner露出、無条件VRAM授権 | 8MB planar/PEGC、17MB planar/PEGC/Cirrusで描画/present/日本語、GFX全画面とGUI、通常GUIのVRAM kill、片方revoke、GUI→CUI→GUI、cirrus-off強制指定fallback、V86後復元 |
| **T2f map/allocator** / e | 新exec/appmem.[ch]、exec/exec_heap.[ch]・exec.c・appslot、paging、sdk/kapi.json・CRT syscalls・malloc接続・Rust allocator、kselftest、memコマンド | §4-4。kselftest map/unmap/owner0、hostで各ページ/PT/extent不足注入、partial unmap、hint衝突・非連続arena、64KB境界3値、calloc/realloc、可変stack残存。変異:別owner free、zero省略、失敗heap上端更新、初期reserve追加 | 8/17MB伸長→unmap→終了、物理枯渇/VA断片化を区別、pool0でもSTOP/閉じる→再起動。複数ASで片方free後もう片方が内容保持 |
| **T2g trim** / f | exec/appslot・appmem、安全点、GUI proto/SDKイベントループ・allocator、gshell handler/WM、関連host | §4-5。固定bit合成・backだけ配送・再入拒否・一巡一再試行・未応答/終了AS。kselftest要求管理の無確保性、hostでA map途中にB hookが入らないtrace。変異:mem_map中pump、常時trim、CUI配送、無制限retry | 8MBでbackが末尾を返しfrontの再試行成功、返せなければENOMEM、端末配下CUI前景は裏GUIを同期実行せずENOMEM→CUI復帰後の配送、trim配送中STOPでもowner0・WM生存。pool0から閉じる経路 |
| **T2h 統合受入** / g | guest試験・deploy.yaml・host検査の結線・本票の結果追記・正典の実装説明/生成地図 | 全段のkselftestとhost/変異、§5-2地図、D35混在セット、[C5]走査、target sizeofとsize予算。新機能の一括追加段にはしない | §5-3の構成表、R5、旧新混在、T1回帰の残件を結果付きで閉じるか理由付き残件に。未検証をPASSとしない |

T2b の新APIは内部のみ、T2c の旧共有USERは既存機能を保つ過渡状態で、T2e 完了まで最終隔離の合格を宣言しない。T2c の形式・配置・shlib・CRT切替は相互依存する **最小の同時更新単位**。準備のhost検証はファイル群ごとに分けられるが、半分だけの成果物を配備しない。T2c以降もABI変更のたびに世代/機能版と成果物を一式で揃える。

**heapの切替はT2fで一括**: T2c〜T2eでは実行中の伸長がまだ無いので、初期量を64KB/1ページへ縮めない。T1の起動時予算計算を物理専用の暫定helperとして残し、旧配置のcode/stack/guardを引いた容量から得たsbrk/exec_heapの「byte数」だけを新しい仮想予約へ写す (256MBの仮想余白を半分にして物理を先取りしてはいけない)。高位VAをこの予算helperへ入力しない。T2fでmap・allocatorと最小初期量を同時に有効化し、この暫定helperと旧二段先取りを撤去する。これは旧バイナリ互換層ではなく、各段の起動を維持する実装順序である。

#### T2a-R. R1 の実装結果 (2026-10-01、`wt/t2a`、GPT-6 / Codex)

**実装**: `exec_pending_transfer` は対象 ID / 終了種別 / 当該 AppSlot の復帰点を保持し、ABORT_PENDING / FAULT_PENDING にして longjmp するだけ。`ring3_kill_kind` と従来の `exec_fault_recover` を接続した。IRQ/例外上では FD・PCM・shlib・AS・池・kfree を触らない。通常の syscall 入口の STOP 安全点、正常 sys_exit は共通 `exec_finish` へ直行する。例外の app-kill / 従来の CPL=0 recovery / halt の分類は変更していない。

launch と resume の両 setjmp 着地が `exec_pending_finish` を呼ぶ。深さ 0 を検査し、pending を一度だけ消費して master CR3 / IF=1 に戻してから、明示した ID を回収する。park は回収しない。回収は DB → redirects → FD → pipe → SHM → sound → PCM の利用終了を私有ページ返却より先へ移し、既存の shlib detach → image/heap/stack → PD/PT → owner 回収/retire、その後の親 CR3/owner/heap 復元 → WM 通知を維持する。WM exec_kill も資源利用終了 → AS/owner/slot 回収 → WM 文脈の通知の順に揃え、現在の WM CR3/owner を切り替えない。永続 owner・T1 の BB 保護は変更なし。

broker は `kctx_irq_depth` を直接読み、独自の `irq_in_irq` 加減算/記憶域を撤去した。ソース互換名は同じ深さへのマクロ。全 IRQ/全例外の入口・復帰と全 longjmp の深さ復元は T1 の asm / 8語 jmpbuf をそのまま使う。`ledger_note` は診断 (op/owner/EIP・irq/exc 件数) を残して `ledger_check_tag="R1 context"` とし、会計変更前に `_stop` (cli/hlt) で panic。通常文脈の短い IF=0 は禁止しない。既存 kselftest の通常深さ 0 / AS owner 往復と IRQ broker 故障診断を維持し、panic は host の停止捕捉で試験した (故障ゲスト起動は PM 未実施)。

**大きさ** (同じ `CROSS_DIR=/home/hight/opt/cross` で前後ビルド、`readelf -SW` / `nm` / `gen_memmap --headroom`):

| 観測 | T2a 前 | T2a 後 | 増分 |
|---|---:|---:|---:|
| `.text` | 316,942B | 317,678B | +736B |
| `.data` | 32,755B | 32,771B | +16B |
| `.data` 開始 | 0x14D620 | 0x14D900 | +736B |
| `.bss` 開始 | 0x156000 | 0x156000 | 0 |
| `.bss` サイズ | 254,948B | 254,948B | 0 |
| `__bss_end` | 0x1943E4 | 0x1943E4 | 0 |
| ASSERT 0x195000 まで | 3,100B | 3,100B | 0 |
| `.got.plt` 末尾 → `.bss` | 2,528B | 1,776B | −752B |
| 圧縮 `vmkernel.lz4` | 471,626B | 472,002B | +376B |

画像外 PT 移設・ASSERT 緩和・帯変更・診断削除なしで現予算内。T2a′ 以降は未着手。

**ホスト検証**: `test_exec_r1.py` は実 exec の移譲・両着地・共通回収関数と実 `setjmp.asm` を ILP32 で実行し、移譲時 cleanup/free=0、対象 ID、IF=1/depth0、親復元、park 非回収、二重消費なし、正常 sys_exit / syscall STOP / 従来 recovery、実 broker の固定 IRQ 深さによる拒否を検査する。資源/CR3/IF は記録用の足場で、PCM 実機や loader 全体を模擬して合格とはしない。T2a 変異 (IRQ 中 teardown / IF 復帰削除 / pending 再消費 / launch・resume の各着地欠落 / broker 旧深さ / WM kill 通知先行) **7/7 コンパイル成功後の実行時 RED**。`test_ledger.py` は panic を IRQ alloc / 例外 free / 入れ子 reclaim の会計変更前に捕捉、**11試験 PASS、7/7 RED、コンパイル失敗0** (既存の入口/復帰欠落2本は asm の静的検査で検出)。新試験を `check-memory-host` / 変更時選択 / TESTS 生成へ結線した。

**実装時の訂正**: §6-1 の基準 `.data` 32,759B / BSS 前余白 2,524B に対し、この worktree の変更前ビルドは 32,755B / 2,528B (4B 差)。`.text` / BSS / ASSERT は一致。設計・上限を変更せず今回の実測を上表に記録する。PCM の既存コメントに IF=0 fault 回収とあったが、今回は呼出境界を通常文脈へ移した。装置 abort 自体の実装は変更しない。既存 WM exec_kill は通知が AS 破棄より先だったため、§4-3 の共通回収順へ揃えた。追加 host trace は変更前の実コードで実行失敗、順序を揃えた写しで PASS。

**コマンドと結果**: 前後の `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` は rc=0 (実行環境 `NP21W_DIR=/tmp/t2a-images`、FD コピー2件は警告・失敗。Windows側へのコピーなし)。`PYTHONPATH=/tmp/t2a-python` で ELF32 の実行だけ qemu-i386 へ送った。直接実行の ledger 試験はサンドボックスの SIGSYS (rc=-31) で失敗、qemu 経由は上記の PASS。途中の `make check-memory-host` は rc=2 (既存ハーネスの `_stop` 宣言不足、次回は panic 無効化変異の unused-function でコンパイル失敗) を修正し、コンパイル失敗を RED に数えていない。`python3 tools/gen_tests_inventory.py --write` / `python3 tools/gen_memmap.py --check` / `git diff --check` は rc=0。最初の `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は **rc=2**: 全変異選択で、既存 `test_kapi_db_v50.py` の回収順静的検査が関数分割後の DB/FD 文を旧 helper 内に探して失敗した。他の検査は終了まで実行し、T2a 専用/台帳変異も PASS。検査を新しい資源回収 helper と通知前の呼出順へ追従させた。修正後の `CROSS_DIR=/home/hight/opt/cross make check-db-v50-host < /dev/null` は **rc=0、24/24 PASS**。最終再ビルド (`/tmp/t2a-final3-all.log`) も **rc=0**、上表はその ELF の値 (`/tmp/t2a-final3-{sections,nm}.txt`)。二回目の全検査も **rc=2**: 後続の `test_net_link.py` にも同じ旧 helper 内への直接呼出しを前提にした静的検査があり、通知 helper の参照へ追従させた。関数分割を参照する既存 Python 検査を全検索した。Host Services の修正後 `make check-net-link-host` は **rc=0、35/35 PASS**。三回目の全検査は **rc=2**: `check-map` が新しい header/link 入力の5件の漏れを検出した。irq.c の不要な pgalloc.h include を除去し、check-memory-host に irq_math.c を登録した。修正後の `make check-map` は **rc=0、108検査の入力漏れ0**。WM kill を含む最終 host trace / 7変異と再ビルドも **rc=0**。**最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は rc=0** (`PYTHONPATH=/tmp/t2a-python`、ログ `/tmp/t2a-check-changed-final3.log`)。build/sdk.mk の変更により全108ターゲットを変異込みで選択し、末尾のソース不変検査も成功。既存の T1 配置境界2本のコンパイル拒否は NOT COUNTED、コンパイルエラーを RED に数えていない。NP21/W/実機の受入は引き続き未実施。ログ/測定は `/tmp/t2a-*.log`、`/tmp/t2a-{before,after}-{sections,nm}.txt`。

**PM の受入 (未実施)**: 新しい kernel.map から `g_pending_id` / `g_pending_kind` / `g_longjmp_reason` / `kctx_irq_depth` / `kctx_exc_depth` / `ledger_irq_ops` / `ledger_exc_ops` / `ledger_check_tag` / `irq_ctx_violations` / `exec_as_leftover_pages` / `used_pages` / `ledger_owners` / `appslot_reclaim_count` / `fault_kill_count` を読む。8/17MB で faulttest gp/de/ud/pf と loop・kloop + CTRL+STOP、park → resume 後にも fault/STOP を別に通し、終了種別・pending0・深さ0・owner/free baseline・取り残し0・STOPによる broker violations差分0、続く起動・V86 往復を確認する。boot broker自己診断の violationsは別勘定。別の故障ゲスト起動では IRQ/例外上の池操作が `R1 context` と op/owner/EIP を残して会計変更前に停止することを確認する。NP21/W に PCM が無いので音の PASS にせず、host trace の PCM 回収位置/IF/深さと既存 CS4231 模擬試験を代替の呼出境界証拠に限定する。Ra266 では PCM 再生中 STOP → tick進行 → IRQ解除 (violations差分0)・DMA/owner返却 → 再open/再生を確認する。配備・NP21/W・NHD・ini・Windows側・実機は本作業で触っていない。commit/push なし、PM の独立実装レビュー待ち。

**T2a 着地と PM の NP21/W 受入 (2026-10-01)**: 独立実装レビュー Codex `gpt-6-astra` は P1〜P3 なしで Approve。`ce5a2a9` で main に入れ、PM の `make all` / `make check` は rc=0 (配備を挟まない流し直しで。途中の rc=2 は 3 回とも T2a 外 — 並行負荷の下で `check-serialfs-host` / `check-lan-bridge-host` が落ち、単独では通る。もう 1 回は検査中の `deploy-kernel` がカーネルを組み直して ISO/FD が古くなった `check-packages-host`)。`g_pending_*` / `g_longjmp_reason` は static で map に無く、`used_pages` は関数なので、読んだのは残りの記号。

| 構成 | 試験 | 結果 |
|---|---|---|
| 17MB (`ver` Commit `ce5a2a9`) | 起動 | `kselftest_fail`=0 (pass 258)。`irq_ctx_violations`=1 は kselftest 自身の +1 (`kselftest.c:1377`、T1 と同じ起動時の値) |
| 17MB | `faulttest gp` / `de` / `ud` / `pf` | 各 `[ring3] ... -> kill app` → `[Process crashed]`。`fault_kill_count` と `appslot_reclaim_count` が 1 ずつ増えて 4、**`ledger_exc_ops`=0** (T1 では > 0 — 例外の上の回収が着地点へ移った)、`kctx_irq_depth` / `kctx_exc_depth`=0、`exec_as_leftover_pages`=0、`irq_ctx_violations`=1 のまま |
| 17MB | `faulttest loop` + CTRL+STOP (`/api/key` を **POST** で `seq=CTRL%2BSTOP&hold=300`) | CS=0x23 EIP=0x5002A0 から kill。**`ledger_irq_ops`=0** (T1 は 0x302)、kill / 回収 5、深さ 0、取り残し 0、violations 1 のまま |
| 17MB | `faulttest kloop` + CTRL+STOP | CS=0x08 (get_tick の中) から kill。`ledger_irq_ops`=0、kill / 回収 6、深さ 0、取り残し 0 |
| 17MB | `v86 -t` | `result : OK`、`$?`=0、回収 7 (kill は増えない)、`ledger_irq_ops`=0 |
| 8MB (`ram-8mb`、終了後 `restore` で 16 へ) | 上の 7 本すべて | 17MB と同じ (kill / 回収 4 → 6、V86 で回収 7、深さ 0、取り残し 0、`ledger_*_ops`=0、violations 1) |
| 17MB GUI (PEGC 480) | gui_demo + Run... で `faulttest` (引数なし) → Start → CUI mode | CUI へ戻る途中の WM の kill で回収 6 → 9 (gshell・gui_demo・park 中の faulttest)、取り残し 0、深さ 0 — **park 中のアプリの WM kill 回収は通った** |

**未実施 — park → resume 後の fault / STOP**: GUI の Run... は引数を渡せず (`modal.rs:784`)、アプリが 1 本のときは park しない (D11-3、`lib.rs`)。そこで `faulttest` に `wait <kind>` (`250de46`) と引数なしの 1 キー選択 (`9a596fc`) を足した。ただし Run... から起動した CUI プログラムはスロットも窓も持たず、キーは手前の窓へ配られ、注入リングに届かない。キーを渡せるのは端末アプリから起動した場合だけ (`multiapp.rs:576`) で、その端末アプリはこの木に無い (`apps/` は空、凍結した os32 の apps にも無い)。そのため resume させられず、`exec_resume` の setjmp 着地を NP21/W では通していない。着地そのものはホストの `test_exec_r1.py` (実 `setjmp.asm`) が見ている。**T2c の GUI 回帰 (park / fault を含む) の前に、注入リングへキーを渡す手段 (端末アプリ、またはゲスト側の注入) を用意して通す**。
**未実施 — 別の故障ゲストでの R1 panic** (IRQ / 例外の上の池操作が `R1 context` で止まること) と **Ra266 の PCM 再生中 STOP → 再 open / 再生**。前者はホストの `test_ledger.py` が会計変更前の停止を見ている。

**PCM 再生中の STOP → 再 open / 再生 (NP21/W、2026-10-01、検証担当 `claude-opus-5-5`、ゲストは `279272d` = T2a′ まで、17MB)**: NP21/W を PC-9801-118 (`SNDboard=8`、CS4231A 相当) にして `[pcm] CS4231 v=101 irq 10 dma 1 fmt 0x5B`。CUI の `pcm_test` を再生中 (PEN=1・IEN=1、PI が開始から ≒1 秒分進んだ後) に `/api/key` POST `seq=CTRL%2BSTOP&hold=300` で 3 回 kill → 3 回とも `[Process crashed]`、PEN=0・IEN=0・PI 停止、スレーブ IMR 0xF3 → 0xF7 (IRQ10 解除)、`sleep 3` が戻る (tick 進行)、`fault_kill_count` / `appslot_reclaim_count` は 1 回ごとに +1、**`ledger_irq_ops`=`ledger_exc_ops`=0**、深さ 0、`exec_as_leftover_pages`=0、**`irq_ctx_violations`=1 のまま (STOP による差分 0)**。続く `pcm_test` は 3 回とも 5 秒 `under=0 rep=0 resync=0` → `pcm_close -> 0`、PI +109。終わりの DMA プールは 16 ページ全部空き・`leaked`=0・`bad_free`=0。音は聞いていない (判定はログ・カウンタ・NP21/W の CS4231 状態)。NP21/W の CS4231 の DMA 経路は実機と同じではないので、**Ra266 での同じ試験は残す**。詳細は [TASK_T1_LEDGER](TASK_T1_LEDGER.md) §4-6-N2。

#### T2a′-R. X15 前倒しの実装結果 (2026-10-01、`wt/t2a2`、GPT-6 / Codex)

**実装**: `include/memmap.h` の導出式で shell exec_heap を `[0x380000,0x3F1000)` (452KiB) に縮小し、固定 PD / boot PT8枚 / device PT を `[0x3F1000,0x3FB000)` に画像外化した。`page_tables[1024]` と動的 PT は従来どおり。`memory_boot_fixed` は shell 行を3区間へ分割 (12→14行)、中央10枚と上端5枚を kernel/WB/PERMANENT/FIXED として登録し、pool 開始は0x400000を維持する。SQLite/DMA/metadata/kstack は移動しない。exec の shell 初期化は既存の `MEM_SHELL_HEAP_SIZE` を参照しており、親復元は保存した base/size/used を使うため、呼出側の変更は不要だった。

`paging_init` は irq_save → 再初期化ガード → P2V_BOOT で全10枚を構築 → heap末尾をNP → 固定10枚を明示再写像 → frame/属性とSQLite・DMA・stack・heap・残余NPの照合 → CR3/PG → pg_enabled → 入口IF復元。再呼出しとPG前の失敗もIFを復元し、失敗時はPGを立てず既存のmemory_bootゲートで停止する。`paging_memmap_selftest` は固定10枚のframe/CR3/cache/実効supervisorと上端NPを照合する。`build/os32.ld` の絶対シンボルとASSERT、`tools/gen_memmap.py` のMIRRORS/境界検査/生成行を一組で追加した。削除したalign4096のP2V例外を除去。ホストの画像外backing/IF模型と既存pagingハーネスを追従させ、check_mapを登録した。02_memory §2-1はビルド後に生成器で更新。T2b以後・KAPI・SDK ABIは変更していない。

**大きさ**: 同じ `/home/hight/opt/cross` の `readelf -SW` / `nm -Sn` / `size -A` / kernel.map で測定。T2a前の列は上のT2a-Rの既存記録 (今回は再ビルドしていない)、T2a後/T2a′後はこのworktreeの前後実測。

| 観測 | T2a前 (既存記録) | T2a後 (今回の変更前) | T2a′後 | 今回の増分 |
|---|---:|---:|---:|---:|
| `.text` 開始 / サイズ | 0x100000 / 316,942B | 0x100000 / 317,678B | 0x100000 / 318,398B | +720B |
| `.data` 開始 / サイズ | 0x14D620 / 32,755B | 0x14D900 / 32,767B | 0x14DBC0 / 32,803B | +36B |
| `.bss` 開始 / サイズ | 0x156000 / 254,948B | 0x156000 / 254,948B | 0x155C00 / 208,964B | 開始−1,024B / サイズ−45,984B |
| `__bss_end` | 0x1943E4 | 0x1943E4 | 0x188C44 | −47,008B |
| ASSERT 0x195000まで | 3,100B | 3,100B | 50,108B | +47,008B |
| `.got.plt` 末尾→`.bss`余白 | 2,528B | 1,780B | 16B | −1,764B |
| 圧縮 `vmkernel.lz4` | 471,626B | 472,003B | 472,447B | +444B |
| 固定PD/PT backing | BSS内40,960B | BSS内40,960B | 画像外40,960B | 実占有不変 |

**実装時の訂正**: §6-1の「BSS開始の4KB整列」はリンカスクリプト自身の指定ではなく、除去した3配列のaligned(4096)属性によるものだった。除去後の `.bss` は32B整列となり、内部パディングも5,024B減った。配列40,960Bの単純減算と正味実測は一致しない。設計の番地/枚数/ASSERT予算は変更せず、この事実だけを記録する。今回のT2a後は既存T2a-Rの `.data` より4B小さく、BSS前余白が4B大きく、圧縮画像が1B大きい (原因未断定)。SQLite末尾は前後とも0x2BC200、代替stack [0x2BD000,0x2DD000)、下予約44KiBは保持。ASSERT残り50,108BにはT2bのtext/data見込み6KiBと追加管理表1KiBを引いても42,940B残る (後続の実測を代替しない)。BSS前余白16Bはこの残りと加算しない。508KiB圧縮上限まで47,745B。

**ホスト検証**: `test_memmap_boot.py` は8/17/64MB×入口IF=0/1で構築時IF=0、CR3/PG前照合、IF復元、再呼出し時のlive AS/別CR3/表内容の不変を検査。失敗出口も入口IF双方で検査した。実exec_heap/kheapで452KiB末端までの割当・枯渇・親保存/復元を通し、固定表と残余のhash不変を確認。通常ASでは固定10枚のUSER翻訳を拒否し、destroy後もbacking不変。地図の典型/予算ちょうど/1ページ超過の既存3場面も維持。動作変異 **13/13コンパイル成功後の実行時RED** (再写像欠落・USER・frame違い・PCD・失敗時IF復元欠落・再初期化・irq_save欠落・無条件stiを含む)。`test_memmap_gen.py` は512KiB heap復活/非整列/1ページ重複/PT枚数変更を地図と実リンカの両方で拒否し、固定PDの写しずれも拒否。SQLite末尾0x2C0000 (旧PT案の成長上限越え)はリンク許可、0x2C7001は既存DMA ASSERTで拒否。生成器の変異 **5/5実行時RED**。ASSERT拒否は動作変異のREDへ数えない。

`test_memory_boot.py` は **19試験PASS**、追加の8/17/64MBで専用FIXED区間・WB/PERMANENT・FIXED/ARENA_TOPを照合し、池の全配布/返却で固定10枚と残余が配られずhash不変。既存の64MB high試験で32MB超のPTがworkspace由来であることも確認 (固定10枚と別勘定)。起動時のmaster動的PT実占有は8/17MB=0枚、64MB=8枚32KiB。8MBの台帳backingはmetadata1+workspace1、17MBはmetadata2+workspace1、64MBはmetadata5+workspace9 (metadata枚数は上端PFNに対するPGALLOC_META_BYTESの丸め)。固定10枚の画像外化によるpool freeの増加とは数えない。

**コマンドと結果**: 変更前および変更後の `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` はrc=0、最終ログ `/tmp/t2ap-final3-all.log`。NP21/W向け画像コピー失敗の警告は依頼どおり許容。`python3 tools/gen_tests_inventory.py --write` (MUT結線に追従)、`python3 tools/gen_memmap.py --write` / `--check` / `--headroom`、`python3 tools/check_select.py --lint`、`python3 tools/check_p2v.py`、`git diff --check` はrc=0。最初のcheck-changedはrc=2 (削除済みalign4096の例外登録残存)。二回目もrc=2 (既存試験のpd_raw/aperture_pt_raw参照残存と、追加AS検査がas_va_to_paの成功0/拒否理由という戻り値を逆に判定)。試験を固定番地と既存API契約へ追従させた。地図2本は従来makeレシピがMUTを渡していなかったため、今回の変異もcheck-changedで実行するようbuild/sdk.mkへ結線し、TESTSを再生成した。**最終 `PYTHONPATH=/tmp/t2ap-python CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` はrc=0** (`/tmp/t2ap-check-changed-final3.log`、build/sdk.mk変更により全108検査を変異込みで選択)。ソース不変検査も成功。対象を絞った `make check-memmap-host check-memory-host MUT=--mutate < /dev/null` もrc=0 (`/tmp/t2ap-focused-mut.log`)。既存の台帳配置境界2本はコンパイル拒否としてNOT COUNTED、memory_bootの残り8/8は実行時RED。生成器5/5・paging13/13は最終全検査でも再確認した。最初の素のILP32実行は環境の32bit syscall制限でSIGSYSとなったため、T2aと同じ `/tmp` のsitecustomizeでqemu-i386を実行補助 (`PYTHONPATH=/tmp/t2ap-python`)。途中の試験追加でowner種別名の宣言誤りとリンカfixtureの空SQLite object不足を修正した。いずれもREDへ数えていない。前後測定は `/tmp/t2ap-{before,after}-{sections,nm}.txt` とkernel.map、検証ログは `/tmp/t2ap-*.log`。

**PMへの未実施受入**: 独立実装レビュー、NP21/W8/17MBの新画像boot/selftest/GUI・CUI操作/gshell/16本/V86往復/fault/STOP (T2aのpark→resumeと故障ゲストpanicの未実施を含む)、Ra26664MBのONLINE・32MB超恒等写像・PCM STOP→再open。452KiB heapのピーク/ENOMEMとPT整合を観測し、固定10枚を配らないことを確認する。64MB実機未確認なのでT2a′受入完了とはしない。コミット/push/配備/NP21/W/NHD/ini/Windows側の直接操作は未実施。

今回のELFで見る記号: `kselftest_fail=0x160E40` (0)、`kselftest_pass=0x160E44`、`paging_memmap_bad_count=0x183AE0` (0)、`ledger_check_fail=0x184730` (0)、`ledger_region_count=0x184734`、`irq_ctx_violations=0x161784` (T2aと同じ起動時1を基準、操作差分0)、`ledger_exc_ops=0x183B00` / `ledger_irq_ops=0x184334` / `exec_as_leftover_pages=0x188C40` (0)。`page_directory` pointerは0x159840 (内容0x3F1000)、`page_tables` pointer表は0x158840 (先頭8要素0x3F2000〜0x3F9000、PDE1016用0x3FA000)。master CR3=0x3F1000、PDのPDE0〜7 frame=0x3F2000〜0x3F9000、PDE1016 frame=0x3FA000、固定10枚のaliasはP/RW・USER/PCD/PWTなし、[0x3FB000,0x400000)はNP。`workspace_first=0x159F08` / `workspace_end=0x159F04` はPFN、64MBのPDE8〜15 frameはその内側、固定域とは別。A/D bitはCPUの更新を許す。PMが再ビルドしたら必ずそのELF/map/nmで番地を引き直す。

**T2a′ 着地と PM の NP21/W 受入 (2026-10-01)**: 独立実装レビュー Codex `gpt-6-astra` は P1・P2 なしで Approve。P3 (CLAUDE.md の「0x380000–0x3FFFFF を present に保つ」が NP 予約と矛盾) は PM が CLAUDE.md と POLICY_DEBUG §4-15 を直した。`279272d` を NHD へ配備 (停止 → nhd-pull → deploy-kernel → deploy → 起動)。

| 構成 | 見たもの | 結果 |
|---|---|---|
| 17MB (`ver` Commit `279272d`) | 起動・番地 | `kselftest_fail`=0 (pass 258)、`paging_memmap_bad_count`=0、`ledger_check_fail`=0 (`ledger_region_count`=19)、**CR3=0x3F1000**。master PD の PDE0 → 0x3F2000 (0x27)、PDE1 → 0x3F3000、PDE2 → 0x3F4000 (0x03)。PT0 の 0x3F1000〜0x3FA000 の 10 PTE はすべて `…063` (P・RW・A・D、**U/S=0**)、0x3FB000〜0x3FF000 の 5 PTE は `…002` (**NP**) |
| 17MB | faulttest gp/de/ud/pf、loop・kloop + CTRL+STOP、`v86 -t` | T2a と同じ (kill 6・回収 7、`ledger_*_ops`=0、深さ 0、取り残し 0、`irq_ctx_violations`=1 のまま、V86 OK) |
| 17MB GUI (PEGC 480) | `os32gui` → Run... gui_demo → ESC → Start → CUI mode | GUI に入り窓が描かれ、CUI へ戻る (回収 8、取り残し 0) |
| 8MB (`ram-8mb`、終了後 `restore`) | 上の CUI 一式と GUI | 17MB と同じ (Physical 8192KB) |

**未実施**: 452KiB heap のピーク / ENOMEM の観測 (常駐シェルを枯渇まで使う手段を用意していない — ホストの `test_memmap_boot.py` が実 exec_heap / kheap で末端までの割当・枯渇・親の保存 / 復元を見ている)、gshell の 16 本、Ra266 64MB (ONLINE・32MB 超の恒等写像・動的 PT 併存・PCM)。T2a の未実施 (park → resume 後、R1 panic の故障ゲスト) も残る。

#### T2b-R. 私有 lease 基盤の実装結果 (2026-10-01、`wt/t2b`、GPT-6 / Codex)

基点は `c27640d` (T2a/T2a′とPMの8/17MB受入記録を含む)。`include/memmap.h` に物理帯とは独立した `[0xF0000000,0xFE000000)` / 56 PT / 8 lease の定数を追加。既存の公開callerは変更せず、合成AS用の内部 `paging_addrspace_create_lease` が旧配置ASと先頭lease PT1枚を一組で生成し、途中不足では全返却する。T2cが既存callerをこの生成経路へ接続する。通常の旧map/USER経路、形式、配置、KAPI/SDK ABIは切り替えていない。

`exec/lease.[ch]` は kernel 内で確定した AS・owner/backend/role 授権と `{sid,generation}` を照合する。whole-page first-fit、RO/RW、固定8本、boot単調u32 token (周回後拒否)、複数面のslot/PT準備→短いIRQ保存区間でPTE/表/refcount一括公開、成功後だけoutを更新。追加PT不足では準備PTを全部返し、既存PTE/表/会計/out/tokenは不変。低位アプリ帯がPT backingと重なる過渡期なので、active ASは入口でmasterへ切替え、通常文脈でwalk/準備し、出口で保存CR3を再ロードする (386のTLB反映)。IRQ保存はCR3切替と公開/解除に限り、準備中のcallback/AS schedulingはない。非active ASは次のCR3ロードで反映される。解除はPTE/PDE NP→TLB隔離→追加PT free→表失効/refcount減算。先頭PTはAS終了まで保持し、未revokeのAS破棄を拒否する。

SURFACEは16本、48B/本、gen=u32、lease_count=u16、plane offsetとclosingを追加。RAMの全PFN owner、FIXED_RAMの専用区間、native VRAMの恒久FIXED/UC owner区間、DEVICEの検証済みresourceのmap範囲、geometry/plane端/overflow、cache一致、SURFACE間のページ占有重複を検査する。RAM paddingを公開前にゼロ化し、未releaseのbackingを直接free/reclaimできないようにした。owner返却要求は新規貸与を止め、参照0後にRAMだけを返す。MMIO予約/kernel aliasは保持。slot再登録でgenを増やし、u32最大世代のslotは再利用しない。gfx型板は指定初期化子と実plane stride (`pitch×height`)へ追従しただけで、公開framebufferのcallerは切り替えていない。

**検査部品**: `lease_check` がlease窓の全56 PDE/全PTE、private PT owner、SURFACE世代/参照を比較。毎bootの `lease_selftest` はSをA、TをB、Uを両方へ貸し、masterと他ASの全present PT/PDEを写して照合 (CPUのA/Dだけ除外)、片側解除/別AS token/二重解除/全owner0を確認する。guestではAのU aliasへ書き、BのRO aliasから読む実CR3往復も実行する。これは部品の追加であり、旧USERが残るT2bを§5-2の最終地図検査合格とは数えない。

**大きさ**: 同一toolchainのreadelf/nm/mapによる実測。SQLite末尾0x2BC200は不変。

| 項目 | 前 (`c27640d`) | T2b後 | 差分 |
|---|---|---|---|
| `.text` 開始 / サイズ | 0x100000 / 318,398B | 0x100000 / 326,446B | +8,048B |
| `.data` 開始 / サイズ | 0x14DBC0 / 32,799B | 0x14FB40 / 32,975B | +176B |
| `.bss` 開始 / サイズ | 0x155C00 / 208,964B | 0x157C20 / 212,040B | +3,076B |
| `__bss_end` | 0x188C44 | 0x18B868 | +11,300B |
| ASSERT 0x195000まで | 50,108B | 38,808B | −11,300B |
| `.got.plt` 末尾→`.bss`余白 | 20B | 4B | −16B |
| 圧縮 `vmkernel.lz4` | 472,437B | 477,767B | +5,330B |

**実装時の訂正**: §6のT2b/T2e 3〜6KiBは見込みであり、T2b単独の正味実測は11,300B (自己診断と固定管理表を含む)。`struct addrspace` は24→440B、SURFACE表は192→768B。残り38,808Bの範囲でリンクし、予算/配置の設計自体は変更していない。前のdata/余白はT2a′-Rの記録と4B異なるため、本作業の前後実測を使用した。公開caller未切替という段の境界に合わせ、事前確保は新しい内部AS生成口で試し、従来の生成口はT2cまで維持する。既存の低位VRAM master PTEはWBで、台帳はUCであることも確認した。新leaseはcache不一致を拒否し、合成試験だけでUC alias一致を試す。実VRAM caller/aliasの切替はT2eへ渡し、T2bで旧master属性を変更していない。

**ホスト/変異**: 実paging/pgalloc/physmem/leaseをILP32で実行する `test_lease.py` を `check-memory-host` と変更時選択/TESTSに結線。S/T/U、master+他ASの全PDE/PTE比較、先頭PT枯渇、追加PT2枚目の枯渇/全巻戻し、8本満杯、4本の不連続plane一括と最後の参照拒否、RO/RW/role/旧世代、padding/plane端/cache、参照数overflow、token/世代周回、pending返却、active CR3保存/復帰、追加PT free時のPDE NPを検査する。leaseの変異は **8/8コンパイル成功後の実行時RED、コンパイル失敗0** (sharedPT、PCD、rollback、世代、plane端、権限、active TLB隔離、PDE失効前free)。ハーネス開発中のcompile errorと、二重検査の片方だけを壊したSURVIVEDはREDに数えず、両検査を対象に修正して最終実行を通した。既存のSURFACE fixtureにgeometryを明記し、gfx hostの固定padding backingと型板変異を追従させた。

**コマンドとrc**: 変更前 `CROSS_DIR=/home/hight/opt/cross NP21W_DIR=/tmp/t2b-images make all < /dev/null` はrc=0。最終同コマンドはrc=0 (`/tmp/t2b-final-native-all.log`)、NP21/W画像コピー2件の失敗警告だけを許容。`PATH=/home/hight/opt/cross/bin:$PATH PYTHONPATH=/tmp/t2ap-python python3 tools/tests/test_lease.py --mutate` はrc=0。ILP32実行は既存の `/tmp/t2ap-python/sitecustomize.py` によりqemu-i386へ送った (sandboxの32bit syscall制限を回避、実物の特権MMU/IRQ/HWはhost足場)。初回 `make check-memory-host MUT=--mutate < /dev/null` はrc=2 (旧SURFACE fixtureのgeometry不足)、追従後はrc=0。`python3 tools/gen_tests_inventory.py --write`、ビルド後の `python3 tools/gen_memmap.py --write` / `--check` / `--headroom`、`python3 tools/check_select.py --lint`、`python3 tools/check_p2v.py`、`git diff --check` はrc=0。初回check-changedはrc=2 (`/tmp/t2b-check-changed1.log`): `test_owner_reclaim.py` のmemmap足場に新しいlease定数が無く、paging.hの配列宣言でコンパイル失敗した。足場へ実memmapのlease定義を読み込ませ、数値の写しを増やさずに修正した。コンパイル失敗はREDに数えない。二回目もrc=2 (`/tmp/t2b-check-changed-final.log`): 全108検査は成功したが、私自身が検査中にnative VRAMのFIXED/UC検証と票/生成地図を更新したため、最後のソース不変検査が3件を検出した (変異による実物の破損ではない)。`make check-multiapp-model-host < /dev/null` と文書/地図/パッケージの最終個別検査はrc=0。実装/測定を確定し、編集を止めた三回目の全検査結果を末尾に記録する。

**PMの未実施受入**: 独立実装レビュー、NP21/W 8/17MBで新画像boot (`lease_selftest_result=0`, `kselftest_fail=0`)、既存GUI/CUI/V86、fault/STOP、park→resume後、pool/owner baselineと取り残し0。Ra26664MBで同じ新boot自己診断、ONLINE/32MB超恒等写像と動的PT併存、PEGC、PCM STOP→tick進行→IRQ解除→再open/再生。T2a/T2a′の未実施項目を消していない。hostのCR3模型では実TLB/権限fault/描画/HWを検証済みとしない。配備・NP21/W・NHD・ini・Windows側・実機・commit/pushは本作業で触っていない。

今回のELFの観測番地: `lease_selftest_result=0x18B864`、`kselftest_fail=0x162E60`、`kselftest_pass=0x162E64`、`paging_memmap_bad_count=0x1864C0`、`ledger_check_fail=0x187350`、`ledger_surfaces=0x186D20` (48B×16)、`ledger_irq_ops=0x186D14` / `ledger_exc_ops=0x1864E0`、`exec_as_leftover_pages=0x18B860`。selftestの後は試験SURFACEのnpages/refcountと試験owner pagesが0 (世代は残す)、master lease PDE960〜1015は空、固定CR3/10枚はT2a′の0x3F1000〜0x3FA000、上端5枚NPを維持。PMが再ビルドしたらそのELF/map/nmから引き直す。

**最終全検査**: `PYTHONPATH=/tmp/t2ap-python CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 NP21W_DIR=/tmp/t2b-images make check-changed < /dev/null` は **rc=0** (`/tmp/t2b-check-changed-final3.log`)。build入力の変更により全108検査を変異込みで選択。lease 8/8、gfx 14/14、memory_boot実行時8/8ほか全検査と最後のソース不変検査が成功。既存の配置境界2本のコンパイル拒否はNOT COUNTED。実装/測定を固定してから実行し、本結果の文書追記だけをその後に行った。状態行は変更していない。

**レビュー 1 回目の対応 (T2b-R、2026-10-01、基点 `17634b4`)**: 独立レビューのP2 2件/P3 1件を対応。SURFACE登録はIRQ/例外条件と同じ入口でmaster CR3以外を拒否し、検証/表公開/padding/out更新の前に戻る。非恒等ASのbacking番地を別の所有ページへ写像したILP32試験で、両ページ全バイト・sid・owner/region/resource/SURFACE表・allocator bitmap/owner map/使用数・CR3が不変を確認。paddingストアの足場はactive PTEを解決するため、恒等ホストメモリだけで反例を隠さない。拒否を外す変異はコンパイル成功後の実行時RED。

TLB試験はactive AS + closing + 最後の1 lease + 追加PTのケースへ変更。CR3同期、全1,025 PTEの消去、追加PT解放、SURFACE backing解放、caller CR3復帰を同じイベント列へ記録し、その順序と参照0/実返却を確認。386ではinvlpgを使わずCR3再ロードが同期原語。古いtranslationを模型に残したまま解除をmaster切替より前へ移し、unmap中の同期も後ろへ遅らせる変異はPT解放時の順序違反でRED。backing返却をPTE/PT解除より先へ移す別変異もbacking解放時の順序違反でRED。変異内では過渡期のmaster文脈ガードを緩め、context拒否で落ちる結果を排除し、診断文字列とrc=2を要求する。正常系はGREEN、全10変異はコンパイル成功後の実行時RED (コンパイル失敗0)。実TLB/HW/ゲストの検証は未実施。

P3は§6のとおり静的保持を継続し、KHEAP化をT2cの生成/破棄切替にまとめた。target管理合計 `sizeof` 検査7,304B/16KiBを追加。状態行は変更していない。

| 項目 | レビュー対応前 (`17634b4`) | 対応後 | 差分 |
|---|---|---|---|
| `.text` 開始 / サイズ | 0x100000 / 326,446B | 0x100000 / 326,462B | +16B |
| `.data` 開始 / サイズ | 0x14FB40 / 32,975B | 0x14FB40 / 32,975B | 0B |
| `.bss` 開始 / サイズ | 0x157C20 / 212,040B | 0x157C20 / 212,040B | 0B |
| `__bss_end` / ASSERT余白 | 0x18B868 / 38,808B | 0x18B868 / 38,808B | 0B |
| `addrspace` / `AppSlot` / 全6slot | 440B / 620B / 3,720B | 440B / 620B / 3,720B | 0B |
| 管理表合計 | 7,304B | 7,304B | 0B |
| 圧縮 `vmkernel.lz4` | 477,767B | 477,788B | +21B |

同一cross toolchainでELF section/nm/圧縮成果物を前後測定。textの+16Bは既存のalignment余白へ収まり、BSS末尾とSQLite末尾0x2BC200は不変。

**レビュー対応の検証記録**: `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` はrc=0 (`/tmp/t2br-all.log`)。既定NP21W_DIRは存在せず、FD画像のコピー失敗警告2件のみ (NP21/W/配備先を変更していない)。`PYTHONPATH=/tmp/t2ap-python python3 tools/tests/test_lease.py --mutate` の10変異を実行。初回check-changedはrc=2 (`/tmp/t2br-check1.log`)、pgalloc単体足場にCR3 stubがなく2件リンク失敗。pgalloc/ledger単体足場にmaster文脈stubを追加し、pgalloc12試験はrc=0。2回目はrc=2 (`/tmp/t2br-check-final.log`)、新しいincludeの変更検査マップに6件の依存漏れを検出し、`tools/check_map.yaml`へ追加 (lint漏れ0件)。並行検査中のSerialFS恒等対照にも1件の失敗が出たが、同じ20秒制限の単独対照は全ケースrc=0 (`/tmp/t2br-serialfs-control.log`)。関連 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-memory-host < /dev/null` はrc=0 (`/tmp/t2br-memory.log`)。検査の並行重複を解消して全検査を再実行する。32bit Linux実行には既存qemu-i386足場を使用、MMU/IRQはホスト模型。commit/push/NP21/W/NHD/配備/iniは未操作。

**レビュー対応の最終結果**: `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` はrc=0 (`/tmp/t2br-all-final.log`)。`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は **rc=0** (`/tmp/t2br-check3.log`)。実行環境ではcrossのbinをPATHに、既存のqemu-i386足場を `PYTHONPATH=/tmp/t2ap-python` に設定した。全108検査を変異込みで選択し、SerialFS恒等対照を含む86変異もERROR/見逃し0、lease10/10実行時RED、変更検査マップ漏れ0、最後のソース不変検査も成功。ソースを固定して単独実行し、この結果の文書追記だけをその後に行った。状態行は不変。NP21/W/実機/実TLB受入は未実施。

### 5-2. 検査3段と lease 回帰 (d)

| 検査 | 具体的な期待値 |
|---|---|
| (a) master | 台帳からRAM / FIXED / DEVICEを引き、低位〜池上端と高位窓を走査。**present PTE の USER**はSHM/trampolineだけ (PDE単体のUSERは違反に数えない)。登録MMIOはsup+PCD、15〜16MBの未登録部分はNP、APP/LEASE PDEは空 |
| (b) AS生成/変更後 | PDE0〜511はmasterと同一。512〜959は当該AS ownerのPTEだけ (shlib textはshlib owner ROのみ例外)、未使用帯はNP。960〜1015は当該ASの有効leaseと全page一致。1016以後の共有窓はmasterの権限/物理を維持 |
| (c) 毎起動 | gfx予約/写像、exec trampoline初期化が済んだ後のpost-exec地点で(a)+合成ASの(b)+S/T/U隔離を実行し全部返す。probe前の候補検査とprobe後の選択面検査を区別し、GUI移譲後にもledger_selfcheck。V86中は止め、終了時復元後に(a) |
| (d) lease | 物理対応・全aliasのcache一致、masterと第三ASのPDE/PTE(権限も)不変、3backendで実描画/present、PT/lease表枯渇と途中失敗全rollback、片側revoke、終了/起動失敗時回収、再initのgenerationと旧token拒否 |

master の PDE 0 は SHM/trampoline を通すため USER が立ち得る (`kernel/paging.c:477`)。V86 復元の `paging_pde_clear_user` (`kernel/v86_mem.c:163`) 後は、通常状態に必要な SHM/trampoline の PDE 権限も復元してから (a) を行う。個々の低位 PTE に USER が漏れないことと、許可された PTE への実効権限 (PDE/PTE の積) の両方を検査する。

比較対象はPTEの物理番号だけでなく P/U/RW/PCD/PWT と owner/参照数。失敗時のbyte比較から診断カウンタの増分だけを除き、会計変化を診断として免除しない。変異で境界を1ページずらすケースはリンクASSERT・地図検査・実動作試験のどれが止めたか記録する。

### 5-3. 8MB / 17MB / 64MB と観測方法

T1 §4-0 と同じ道具を使う。実施は後日PM/テスター、今回の設計作業で環境を変えない。

| 構成 | 手段 | T2 の受入 |
|---|---|---|
| NP21/W 8MB | `np21w_ini_live.py` の既存 `ram-8mb` (ExMemory=7)、planar/PEGC | kselftest/地図fail0、GUI + 私有総量2MBの3配分 (下)、起動描画終了×20、pool戻り、FEP1語、fault/STOP、枯渇から閉じる/再起動、trim |
| NP21/W 17MB | `ram-15mb` (名前は15だが ExMemory=16、検出17408KiB) | 同じ項目 + 15〜16MB非配布、2枚以上のアプリPDE、planar/PEGC/Cirrus、全画面/GUI/lease拒否、V86復元 |
| Ra266 64MB | 実機、`rshell_serial.py` とbootlog/mem/kselftest | 同じ基礎 + 検証済みPCI窓sup+PCD、PEGC、CS4231再生中STOP→再open/再生、pool/owner戻り。RAW BARを検証済みとして数えない |
| ホスト | T1の実ソースILP32足場 (test_memory_boot / pgalloc_model / paging_bounds / app_band_pde / app_bb_overlap / gfx_boot / ledger) を拡張 | 8/17/64MB合成地図、12MB付近FIXED/ARENA_TOP境界、疎PT/枯渇/overflow/変異。実機の代替とはしない |

2MBの3配分は上位票 §7 と同じ: (i) code1500 + shlib data40 + stack256 + heap252、(ii) code/bss64 + shlib data40 + stack512 + heap1432、(iii) code200 + shlib data40 + stack256 + exec_heap1552 (すべてKiB、計2048)。再ビルド後の shlib data が40KiBと異なる場合は **実測量を内数にして heap を調整**し、総量2048KiBを維持する。heapにはsbrk初期ページも含め、PD/PT・共有text・lease backingは別勘定。これは最終P1の容量条件でもある。T2で現固定帯のため不成立なら実測不足量を記録してT3のSLACK解放へ渡し、T2で「2MB合格」とは書かない。

各回で新しい kernel.elf / kernel.map のnmから番地を引き、NP21/Wは `/api/mem?space=phys` で `used_pages` / owner表 / kctx深さ / ledger診断 / leftover / lease数を前後比較、`gui_gate.py` と screenshot で見た目、`tools/guest_tests.py` で既存回帰。実機は `/api/mem` がないので `mem` のowner表示とselftest/trace出力を使う。既存の `mem` 表示に足りない lease/世代/owner 0 の読み口はT2hまでに追加する。

最低記録: commit/build ID・Image CRCとサイズ・構成・実コマンド・rc・before/after free/owner/lease・表示結果・失敗/skip。`fault_kill_count` はSTOPでも増える現挙動を前提に終了種別も読む。NP21/WのPCM無しをPASSにせず実機で音を確認。32/128MB presetは補助で64MBの代わりにしない。64MBちょうどのNP21/Wが必要なら別タスク+[D2]。配備手順は [08_build](../../08_build.md) §8-4 と [POLICY_DEBUG](../../POLICY_DEBUG.md) §5、停止/起動は `tools/np21w_ctl.py`。ここで操作承認を取得したことにはしない。

最終P1のうちT4以降にしか存在しない条件 (必須モジュール/MINIMAL、512KB DB poolとFEP優先予約、FD同梱、OpenType) は後続へ明示して渡す。T2では現SQLite/FEPを有効にした通常構成で検証する。

## 6. サイズ予算・リスク・未確認

### 6-1. カーネル本体の予算と X15

基準はT1f実測: `.text`316,942B、`.data`32,759B、`.bss`254,948B、`__bss_start=0x156000`、`__bss_end=0x1943E4`、ASSERT上限0x195000まで **3,100B**。`.got.plt`末尾から`.bss`前まで **2,524B**。両者は加算できない。text/dataが2,524Bを超えて増えるだけでもbss開始が4KB進んで失敗する。

以下は**設計時の概算、未ビルド** (正味の削除効果を楽観して予算に入れない)。

| 項目 | text/data 増分の見込み | 固定データ / その他 |
|---|---|---|
| T2a R1 | 0.5〜1.5KiB | pending等 <128B |
| T2b/T2e lease・属性・検査 | 3〜6KiB | SURFACE拡張・表、各AS8lease |
| T2c 配置 / shlib / D35 | 2〜5KiB、旧claim/先取りの削除は別に実測 | PD/PTは池。全64枚先取りなし |
| T2d B1 | 1〜2KiB | 大きなcopy bufferの新設なし |
| T2f/T2g map / trim / 回収 | 3〜6KiB | 各AS32extent、要求bit。SDK allocatorの大部分はuserland |
| kselftest/診断の追加 | 1〜3KiB | 本番同梱、削って帳尻を合わせない |

合計 **約10.5〜23.5KiB**、削除前の概算。現予算2.5KiBを大きく超えるため、T2a′を必須とする。`AppSlot`に大きな固定表を直に足すとBSSも超えるため、AS制御ブロックは起動/AS生成時に固定KHEAPから確保し、実行中は固定容量とする。失敗すれば起動を拒否、終了で返す。暫定目標: PT控え480B + lease8×32B + extent32×16B +制御128B = **1,376B/AS以下** (最大4通常ASで5,504B)。SURFACEはplane offsetを含む **64B以下×16=1,024B**、T1のowner/region/resource表2,816B、AppSlot等を含む所有権/写像管理の合計 **16KiB以下**をtarget sizeofで検査する (U1/U9)。extentは `{base,end,kind,flags}`、PFNはPTEを正とし二重の巨大配列を置かない。lease32Bはref/token/base/end/perm等を持ち、plane値はSURFACEから計算する。PD/PTページの実占有はこの16KiBとは別に計上。KHEAP192KBの他顧客と同居するピークも測る。

**T2b の静的保持と T2c への移行 (レビュー1回目対応)**: 現時点は既存exec/park/resume/自己診断が `AppSlot.as` を値で扱うため、T2bでポインタ化して全生命周期を変更せず、**T2cのAS生成/破棄・公開caller切替と同時に固定KHEAP確保へ移す**。それまでの予算差分は `addrspace` 24→440B (+416B) ×全6slot = **BSS +2,496B** (未使用ID0とshell枠も含む)、`AppSlot` 204→620B、表全体1,224→3,720B。これはKHEAPを使わない暫定の常時占有で、将来のPT控え/extentを同じ埋込みへ増設しない。`exec/appslot.c` のtarget `STATIC_ASSERT` は埋込みASを二重計上せず全6 AppSlot + owner/region/resource + SURFACEを合計し、現在 **3,720 + 2,816 + 768 = 7,304B ≤ 16,384B**を検査する。T2cはAppSlot本体 + 全AS制御ブロックの固定容量 + 台帳の合計へ検査式を更新し、KHEAPピークと確保失敗/終了時返却も検証する。PFNメタデータとPD/PT実ページは従来どおり別の物理予算。

**X15 の前倒し (ユーザー決定 2026-10-01)**: T3 の「固定 PT をカーネルイメージの外へ」を **T2a → T2a′ → T2b** の必須ゲートへ移す。TASK_MEMMAP_V3 §6 の T3 行および D 番号の本文は変更しない。前倒しで完了する範囲と T3 の残りは §1-3。T2a + T2b の見込みだけで現 text/data 余白 2,524B を超えるため、容量不足後の相談にはしない。T2a 自体が予算を超えたら同段を未受入とし、診断削除・ASSERT緩和・帯拡張で通さない。

#### T2a′ の配置 (半開区間、実装時に memmap.h を正典にする)

現 `kernel/paging.c:155`・`:156`・`:174` の `pd_raw` 1枚 + `pt_raw` 8枚 + `aperture_pt_raw` 1枚、計 **40,960B (40KiB)** を対象とする。`page_tables[1024]` のポインタ表や動的 PT は移設対象外。**起点はT3と同じ0x3F1000** (ユーザー決定2026-10-01「実測して0x3F1000を優先」)。旧案 `[0x2DD000,0x2E7000)` は撤回する。

**配置判断の証拠 (PM提供、2026-10-01)**: NP21/W・17MBのCUIで `ls -l /usr/bin`・`/sys`、`man hsync`、`cat boot.log`、`grep`、`hsync -n`、`mem`、`cfg get` とゲスト試験16本の後、物理 `[0x380000,0x400000)` を読んだ。書込みが観測された上端は起動直後 `0x38200F` → 作業後 `0x390017`、`[0x3F1000,0x400000)` は全バイト0 (PM報告では未書込み)。本セッションで再測定した結果ではない。CUIの観測は全負荷でのheap最大量やゼロ書込みの不存在を証明しないため、非重複は以下の定数・allocator境界・台帳・写像で保証し、GUIを含む容量回帰を受入に残す。

| 用途 | 新定数案 / 半開区間 | 属性・境界 |
|---|---|---|
| shell exec_heap | MEM_SHELL_HEAP_BASE = 0x380000、MEM_SHELL_HEAP_END = MEM_FIXED_PAGING_BASE、[0x380000,0x3F1000) | **452KiB = 512KiB − 60KiB**、恒等sup/RW/WB |
| master PD | MEM_FIXED_PD_BASE = MEM_FIXED_PAGING_BASE = 0x3F1000、[0x3F1000,0x3F2000) | 恒等present / supervisor / RW / WB |
| bootstrap PT 8枚 | MEM_FIXED_BOOT_PT_BASE = MEM_FIXED_PD_BASE + PAGE_SIZE、[0x3F2000,0x3FA000) | 同上、PDE0〜7用 (32MBの初期窓) |
| device aperture PT 1枚 | MEM_FIXED_APERTURE_PT_BASE = MEM_FIXED_BOOT_PT_BASE + MEM_FIXED_BOOT_PT_COUNT * PAGE_SIZE、[0x3FA000,0x3FB000) | backing自体はWB。写すMMIOのPTEは従来どおりPCD/PWT |
| 固定PD/PT全体 | MEM_FIXED_PAGING_END = MEM_FIXED_APERTURE_PT_BASE + PAGE_SIZE、[0x3F1000,0x3FB000) | 10枚40KiB、owner=kernel、恒久FIXED、全RAM構成共通 |
| 上端残余予約 | [MEM_FIXED_PAGING_END,MEM_SHELL_BAND_END + 1) = [0x3FB000,0x400000) | 20KiB、恒久FIXED・NP。T3のguard+kstack用、T2では未使用 |

**定数 [C4]**: `include/memmap.h` に起点 `MEM_FIXED_PAGING_BASE` と枚数 `MEM_FIXED_BOOT_PT_COUNT=8` を置き、各BASE/ENDは上表の式で導出する。`MEM_SHELL_HEAP_SIZE = MEM_SHELL_HEAP_END - MEM_SHELL_HEAP_BASE` (=0x71000) とし、60KiBの減算を呼出側へ直書きしない。`PAGING_BOOT_PT_COUNT` はこの枚数を参照し、1/8/1枚=10枚をASSERTで固定する。`MEM_SHELL_BAND_END=0x3FFFFF` は4MB未満の固定帯終端として維持するが、全域をheapと呼ばない。`exec/exec.c:2166`・`:2167` の常駐shell初期化と親shellのheap退避/復元は縮小サイズを使い、上端を旧512KiBから再計算する経路を残さない。newlib sbrk・shell guard・shell stackは変更しない。heapとPDの間に新guardは置かず、境界外割当をallocatorの失敗で止める。

カーネル帯 `[0x100000,0x200000)`、SQLite本体/代替stack、DMA `[0x2E8000,0x2F8000)` と上下guard、8MBのFIXED metadata/workspace `[0x2F9000,0x2FB000)`、kstack guard `[0x2FB000,0x2FC000)` とstack `[0x2FC000,0x300000)`、shell画像/newlib sbrk/guard/stack `[0x300000,0x380000)` および縮小exec_heapのいずれとも重ならない。池の開始0x400000も変えない。

**SQLiteの伸び代**: [02_memory §2-1](../../02_memory.md) の現SQLite末尾 `0x2BC200`、代替stack `[0x2BD000,0x2DD000)`、カーネル予約 (下) `[0x2DD000,0x2E8000)` **44KiBをPTのために消費しない**。U9 (`646e39e`) の既存 `ASSERT(__sqlite_end + MEM_SQLITE_STACK_SIZE + 0x1000 <= MEM_DMA_POOL_BASE)` を保持する。44KiBは地図上の予約幅で、丸め/guardを含むため本体が44KiB丸々伸びる保証ではない。旧案で残り約3.5KiBとなるP3-2の追加制約・SQLite対PD上限ASSERTは不要。**T4までSQLiteの伸び代を残す**責務を引き継ぎ、T3でSQLiteを本体の直後へ連結するときもPT分の40KiBをここから差し引かず、鎖全体の予算で再検証する (旧番地そのものをT4まで固定する意味ではない)。

**予約・写像・初期化**:

1. L0は4MB未満RESERVEDを維持。`memory_boot_fixed` の現shell全域 `[0x300000,0x400000)` のFIXED行を **[0x300000,0x3F1000) / [0x3F1000,0x3FB000) / [0x3FB000,0x400000)** の3行に分割する。全てowner=kernel、WB、PERMANENT (既存の登録処理で付与)、中央をPD/PT専用として識別。SQLiteからDMA直前のFIXED行は変えない。重複登録・pool確保・workspace流用をせず、L1/L2配布対象外を検査する。2行増える台帳容量も8MB FIXED型/ARENA_TOP型で確認。
2. `kentry` は画像とSQLiteのBSSだけをclearし、従来のkstackへ切替えて `kernel_main` へ進む。固定PD/PTはBSS外なのでこのclearに頼らない。`kernel.c:262`・`:292` は既に `_enable()` を行い、`:506` の `paging_init` **入口IF=0は前提にできない**。`paging_init` 内で `irq_save()` → 一度だけ初期化するガードの確認 → PG=0で固定10枚を明示ゼロ化/構築 → 下記写像検証 → CR3/PG有効化 → `pg_enabled=1` → `irq_restore(saved)` とする。再呼出しの早期return・PG有効化前の失敗出口でも入口IFを復元し、live AS/動的PTを再初期化しない。構築中は割込み依存のI/Oやcallbackを呼ばない。PG前の物理アクセスは `P2V_BOOT`、PG後の恒等pointerは `P2V`、PDE/CR3へは `V2P` [C5]。固定領域は絶対定数/リンカシンボルとし、大きなBSS/NOLOADを作らない。
3. 現 `paging.c:309` は `[0x380000,0x400000)` 全域をshell exec_heapとしてpresentに保つ。これを縮小heap・固定PD/PT・残余NPへ分割する。既存カーネル予約域NP化 (`:291`) に加え、切り離した末尾 `[MEM_SHELL_HEAP_END,MEM_SHELL_BAND_END+1)` を一旦NP化し、**全NP化の後に固定10枚を恒等sup/RW/WBで明示再写像**する。残余20KiBはNPのまま。範囲APIの終端規約 (NP化はinclusive、mapはexclusive) に合わせる。DMA/kstack/SQLite/縮小shell heapとguardの属性を確認してから CR3=V2P(master PD) → CR0.PG、固定10枚のaliasがpresentになった後で入口IFを復元。写像失敗はPGを立てずboot失敗。T1 FIXED metadataは別途後段の `paging_map_ledger_backing` で有効化する。
4. 32MB超のmaster動的PTはT1 workspaceから供給し、固定10枚と会計を分離。ASは低位backingをsupervisorで共有し、private PD/PTの解放で固定領域を返さない。T3は同じ10枚を使い続け、周辺配置変更後もNP→再写像→CR3→PGの順を検証する。

**リンカ・地図・実動作を一組にする**:

- `build/os32.ld` の絶対定数の写しを `tools/gen_memmap.py` の **MIRRORS写し一致表**に登録 [C4]。対象は固定pagingのBASE/PD_BASE/BOOT_PT_BASE/BOOT_PT_COUNT/APERTURE_PT_BASE/END、shellのHEAP_BASE/HEAP_END/HEAP_SIZE/BAND_ENDと、ASSERTで参照する固定境界。C側は上記の導出式、ld側の数値の写しは各マクロの評価値と照合する。既存SQLite/カーネル/stack/DMAの写し検査は維持。
- ASSERTは全境界4KB整列、PD1/boot PT8/aperture PT1の連続10枚、`MEM_FIXED_PAGING_BASE == MEM_SHELL_HEAP_END`、`MEM_SHELL_HEAP_BASE + MEM_SHELL_HEAP_SIZE == MEM_SHELL_HEAP_END`、`MEM_FIXED_PAGING_END <= MEM_SHELL_BAND_END + 1 == MEM_POOL_BASE` (比較は別々に書く)、残余5枚、shell stack末端≤heap先頭、kernel/SQLite/DMA/metadata/kstackとの非重複を検査する。既存の画像予算・SQLite対DMA ASSERTは保持し、SQLite対PDという新しい成長制限は加えない。bootstrap PT枚数変更はサイズ一致で止める。
- `gen_memmap.py` はshell exec_heapの短縮、PD/boot PT/aperture PTの3行、上端残余NPの行を生成し、旧「カーネル予約 (下)」44KiBは保持。kernel.mapの実値と定数で重複/逆転/予算/写しを検査し、`--write` で **02_memory §2-1生成ブロック**を更新、`--check` / `--headroom` で確認する。今回は未実装なので生成地図を未来の値で手書きしない。
- `paging_memmap_selftest` はshell帯全域RWという期待を分割し、heapと固定10枚はRW・USERなし、残余20KiBはNPと判定。CR3/PDEのframe一致、通常ASで固定PTへのUSERアクセス拒否 (V86セッションは既存の例外、復帰後も照合)を検査する。host変異は旧512KiB heap復活・非整列・1ページ重複・MIRRORSの写しずれ・NP後再写像欠落・誤USER・再初期化・irq_save欠落/無条件stiを対象とし、リンク/地図拒否と動作失敗を区別。入口IF=0/1双方の復元と構築中IF=0、SQLiteが旧案上限を越えても既存DMA限界内なら通り限界超過なら拒否することも検査。
- §5-1の **8MB / 17MB / 64MB** で固定10枚の内容・sup/RW/WB・台帳予約を確認し、pool全配布/返却でも対象PFNを配らない。8MBはFIXED metadata+workspaceの2枚、17/64MBはARENA_TOPと動的PTの別勘定を照合。縮小heapの境界割当/枯渇/親復元を検査し、PT破壊・NP予約への書込みがないことを確認する。PMのCUI操作/16本を再実施し、GUI/gshell・CUI・V86復帰・fault/STOP後のheapピーク/割当失敗とPT整合も記録する。**今回の17MB観測だけでは受入完了としない**。64MB実機未確認ならT2a′の受入完了とはしない。

**空く量は見積もりと実測を分ける**: 生配列の削除は40,960B。T1fの `__bss_end=0x1943E4` から単純に引けば `0x18A3E4`、旧上限0x195000まで44,060Bとなるが、これは配置・コード増分・整列不変の仮算。BSS開始の4KB整列、T2a/初期化コード、固定台帳の追加行、selftestを含む正味の増減は **T2a前 / T2a後 / T2a′後**の同一toolchainビルドで `readelf -SW` / `nm -S` / kernel.map / size を保存して求める。`.text/.data/.bss`、BSS前余白、`__bss_end`、ASSERT残り、固定10枚と動的PTの実占有、圧縮画像サイズを表で報告。T2bの3〜6KiB見込みと管理データを差し引いてもリンク可能なことをゲートにする。

全T2の削除前見込み10.5〜23.5KiBに対し、移設40KiBからの粗い差額は16.5〜29.5KiB (T2a′固有増分・整列・固定データ前)。これはtextが40KiB減る意味でも、圧縮画像が40KiB減る意味でもない。40KiBは非配布予約域へ移り、空くのは主に**カーネル帯のリンク容量**。KHEAP/SHMの浮動配置が下がるだけで、T2a′単独ではpoolのfreeが40KiB増えるとは数えない。削除効果は各段の実測でのみ差し引き、508KiB制約も維持する。

### 6-2. リスクと未確認の台帳

| ID | リスク / 未確認 | 閉じる段・証拠 |
|---|---|---|
| R1 | IRQ移譲時のEOI・スタック・全longjmp経路、再入cleanup | T2a、asm実行試験+guest全fault/STOP。PCMは実機証拠が要る |
| R2 | 0x80000000以上をsigned intで扱う隠れた比較 (U8)、HostDrv の I/O 回帰 | T2c/T2h、signed 比較は SDK・lib・apps/game も検索。**非恒等 VA のハイパーコール渡しの懸念はコード監査で解消**: fs/hostdrvfs.c:146・:147・:381 は kernel の g_databuf、:398 / :418 の kmemcpy だけが read/write の caller buf を触る。loader は §4-2 の backing、実行中 I/O は caller AS 上の有効 VA を使う。高位 buffer の read/write と24件の実機確認は別の未実施回帰として残す |
| R3 | metadata / BB探索の物理条件へ新APP定数が混ざり8MB全停止 | T2c、T1の8/12境界/17/64の足場で物理配置が維持されること |
| R4 | Cirrus CLIENTとDISPLAY混同、planar geometry変更、ページ端露出、fallback後古いfb | T2e、実backendからSDK描画まで通す。T1eの模擬選択の証拠だけでは足りない |
| R5 | 8bit世代の周回・slot/token再利用・参照数overflow | T2b、u32世代/周回拒否、別AS tokenと二重free。生ポインタの使用期限は§3-4 |
| R6 | static表/KHEAP/コード予算不足 (U1/U9/U15) | §6-1の各段実測。gshellはT2a′で452KiBに縮めるexec_heapを含む現shell帯のimage+二heap+stackのピーク/枯渇を測り、BB実ページを二重加算しない |
| R7 | nano malloc と _sbrk の接続、exec_heapのtrimで生存データ破壊 | nano は要求ごとの chunk と隣接検査を持つため、非連続 arena を上位で扱う接続確認は小さく切り出せる。ただし sbrk_aligned の追加整列要求と末尾 free chunk 拡張は連続性を要するので、§4-4 の EXACT _sbrk を維持。確認した newlib 4.4.0.20231231 の nano-mallocr.c の sbrk_aligned / nano_malloc を基に、T2fで実allocatorの穴・整列・trimを試す。模型だけで済ませずTLSF採否は未確定 |
| R8 | trim時にWMを同期で回してcurrent ownerを取り違える | T2g、要求記録と配送のtrace、再入・未応答・STOP、無確保性 |
| R9 | source互換bridgeがlease失敗を隠す、Unicode低位直読が残る、TVRAM直書きU20 | T2e全再ビルド/日本語/再attach。tvdump・nop・ring3 3本は§2/§3-4で移行を確定。残るprivate apps/gameは未監査 |
| R10 | D35のstampがCRTだけ / 欠落objectを見逃す、最終ELFに古いslotが残る | T2c/P7、旧.o/旧archive/asm/Rust/LTOを含む混在試験。private submodule不在は未完 |
| R11 | master空の高位shlibをshell/WMが呼ぶ、子終了のparent heapが別CR3 | T2cの静的参照禁止と親子/WM回帰。将来のshell shlib利用は本契約外 |
| R12 | PCI BARが高位app/lease仮想と衝突 (上位U10) | 恒等に置ける2GB未満のみ直写し。高位BARはdevice窓へ別名、実装未対応なら予約しても公開拒否、重ねて写さない。sizing/汎用配置はP4 |
| R13 | T2受入とP1/P6受入の混同 | §1-3/§5-3。x87・長いKAPI期限・モジュール・MINIMAL・最終8MB余量は未確認として担当へ |

## 7. 独立レビューに突き合わせる論点

2回目は `705a227` から `8a7bf4c` までの本票差分と §2 の参照コードを対象に、同じ Fable 5.1 が **Approve (P2なし)**。以下は確認観点を保持したもの。今回の配置変更はその後のユーザー決定・PM実測とP3対応であり、その変更自体の独立レビュー済みを意味しない。要旨は「高位ASとSURFACE leaseの契約を具体化し、T1のR1/所有権を接続する」。判定基準は [ROLES](../agents/ROLES.md) §5: **到達可能な反例を伴う現契約違反**をブロックとして示す。設計者による本票の整合確認は独立レビューの代用ではない。

1. **X3 / ページ占有** — 連続SURFACE・不連続planar4面の一括操作・Cirrus CLIENTの例外・cache一致で、未貸与物理や表示面を露出する反例がないか。通常map拒否だけでなくmaster変更口も閉じるか。
2. **lease寿命** — prepare/commit/rollback、active/非active TLB、片側revoke、全終了経路、owner返却待ち、再init世代とtoken周回、SDK再attachの契約が整合するか。
3. **R1 / T1 B1** — EOI後移譲→IF=1回収の全復帰点、全例外、broker深さ統一とpanic化の順、PCM停止/IRQ解除/再openが成立するか。
4. **R2 / R3-e** — heap希望VAと非連続arena、partial unmapと固定extent表、疎PT/先頭leasePT、全rollback、起動予約なしが矛盾しないか。
5. **trim / P6 X5** — 非同期要求→安全なpark配送→一回再試行でKAPI/allocator再入を避けられるか。`ring3_wm_depth`に排他の意味を持たせていないか。
6. **B1 / D31** — caller出自、RO入力/RW出力、overflow、page末NUL、IF復元、MMIO拒否、T4 B3/B4・T5a B2との保証範囲の境界。
7. **D35 / X6** — 4世代の独立性、shellも入口前拒否、各コンパイル単位の欠落検出、旧新混在試験、T2途中段/P7の機能版と世代の使分け。
8. **T1引継ぎと段の切替** — 8MB BB漏れ修正を外す時点、物理metadata定数の残留、shellは静的リンク、低位Unicodeの橋、T2cの過渡共有USER→T2e全面撤去に回帰不能な中間状態がないか。
9. **容量 / X15** — 16KiB管理予算・KHEAPピーク・2.5KiBのリンク予算、必須T2a′の0x3F1000配置・shell heap452KiB・SQLite伸び代維持・irq_save→PG=0構築→NP後再写像→PG→IF復元の順・FIXED台帳との非重複・T3再移設なし。概算を測定済みの数字として扱っていないか。
10. **受入の証拠** — 3 backend描画、S/T/Uの意味、8/17/64MB・実機PCM・P7混在の試験が実装の穴を検出するか。最終P1の容量条件をT2だけで達成済みと誤認しないか。

既知の未確認は §6-2。コード変更・ビルド・ホスト挙動試験・変異・NP21/W・実機は今回未実施。2回目の結果と今回の対応は下表に区別して記録する (本セッションは設計役)。

### 7-1. 独立レビューの所見と対応

Fable 5.1、1回目 (2026-10-01)、**Request changes (P2×2)**。PM転記の `fable_t2_r1.md` 全文を読み、`705a227` のコードと照合した。本表の「反映」は設計票への反映であり、実装完了を意味しない。2回目はユーザー提供の判定・所見に基づき末尾へ追記する。

| 所見 | 確認結果と対応 | 反映先 / 受入 |
|---|---|---|
| P2-1 物理APP定数の漏れ | PEGC BB探索下限とpool selftestを追加。追加走査でphysmemのL0供給境界にも依存を確認。高位化で物理PFNへ混入させない。pagingの帯selftestも書換え対象 | §2・§4-1・T2c、8MB PEGC候補維持/起動fail0/物理台帳維持 |
| P2-2 T2bのリンク予算 | X15を必須T2a′へ。ユーザー決定2026-10-01、初回案は予約域0x2DD000に10枚とSQLite上限ASSERT。2回目後のユーザー決定で0x3F1000へ変更、追加のSQLite上限は撤回。地図・8/17/64MB受入・正味実測は維持。上位D本文/T3行は変更なし | §1-3・§5-1・§6-1。T3は画像外化・最終配置済み、周辺変更と再検証を引継ぐ |
| P3-1 stack_size | offset 0x20の既存欄を有効化、包装の予約0と既定256KBを維持。「追加」を訂正 | §4-1・§4-6、0/512KB/不正指定 |
| P3-2 U20/受入道具 | sh.binのtvdumpをKAPI、nopとring3 3本のマーカーをSHMへ。レビューの細部は訂正: nopはTVRAM直書きで現deploy未登録、ring3 3本は0xA8000で登録済み | §2・§3-4・T2e、TVDM互換・SHMマーカー・狙ったfault確認 |
| P3-3 gfxの2実体 | アプリ静的リンクとshlib内でCLIENT lease2本 (8本の内数)、両方の再attachを明示 | §3-4・T2e、両描画経路/片側だけ更新する変異 |
| P3-4 R1着地点 | launch :2094 と resume :2648 の別setjmpを共通回収へ接続、park理由は回収しない | §4-3・T2a、初回とresume後それぞれfault/STOP/二重回収なし |
| P3-5 行番号 | execの実行文/関数定義を照合。kill :1621/:1629、teardown :1483、abort判定 :668 (定義:665)、dispatcher user_esp :1540 (定義:1537) と意味も区別 | §2、レビューの行番号を機械的にずらさない |
| P3-6 帯依存小漏れ | 引数窓→AppSlot.stack_top、shlib診断/config注記/mem表示/appslot・exec_heap注記/dbgserialを列挙 | §2・T2c、仮想と物理の残存検索 |
| P3-7 master USER基準 | (a)はpresent PTEのUSERで検査、PDE0のUSER単独を違反に数えない。V86後の通常PDE権限復元も対象 | §5-2・T2e、SHM/trampolineの実効権限と低位漏れの両方 |
| P3-8 HostDrv | g_databuf経由を確認し非恒等VAをハイパーコールへ渡す懸念を閉じた。caller bufへのコピー時のAS契約と実機回帰は残す | §1-2・§6 R2、read/writeの高位buffer試験 |
| P3-9 CUI前景ENOMEM | 端末配下CUIは裏GUIの返却をその場で待てずENOMEM、同期pumpなし。復帰後の配送を確認 | §4-5・T2g、D24の帰結として受入 |
| 補足 master往復 | T2c以後は低位backingを直接walkできる。不要なCR3往復の整理をT2dへ、残す過渡形も許可 | §4-7、caller PD/IF/CR3保持 |
| 補足 nano malloc | nanoのchunk/隣接確認から接続試験は小さく切出せる。整列追加/末尾拡張の連続性は必要で_sbrk EXACTを維持 | §6 R7・T2f、実nanoの穴/整列/trim試験 |
| 補足 manifest | 常駐shellと別にsh.bin・userland/tests/*.binを明記 | §4-6・T2c/P7、4世代/hash/旧新混在 |
| 2回目 判定 (2026-10-01) | **Fable 5.1 Approve、P2なし** (ユーザー提供)。下のP3と、その後の固定PT配置のユーザー決定を今回反映 | 実装受入は§5、今回の変更を独立再レビュー済みとはしない |
| 2回目 P3-1 入口IF | kernel.c:262/:292で_enable済み。入口IF=0前提を撤回し、構築〜PG有効化をirq_save/irq_restoreで囲む | §6-1、IF両値・早期return/失敗出口・再初期化禁止 |
| 2回目 P3-2 SQLite伸び代 | 0x2DD000案は44KiBを消費し約3.5KiBしか残らない。ユーザー決定で0x3F1000へ移し追加制限不要、U9の既存ASSERTと伸び代を保持 | §1-3・§6-1、T3予算へ継承、T4まで維持 |
| 2回目 P3-4 小さな帯依存 | PEGCのASSERTをMEM_SYSTEM_SPACE_BASEへ、未使用EXEC_LOAD_ADDR撤去、db_v50のVRAM_END許可帯期待値を更新 | §2・T2c/T2e、物理定数残存走査・未貸与VRAM拒否/許可RAM末端試験 |
| 2回目 P3-5 libos32gui行番号 | client.rs:185がattach_gfx本体、shlib.rs:422はshlib init入口、attach呼出しは:437と実物確認 | §3-4、2実体の再attach契約を維持 |
| 2回目後 ユーザー決定・PM実測 | 固定PD/PTをT3最終起点0x3F1000へ。shell heapを452KiBへ、後方60KiBを40KiBのPD/PTと20KiBのNP予約に分離 | §1-3・§5-1・§6-1、地図/初期化/容量受入、T3再移設なし |

既知の未確認は§6-2と未実施の実装受入 (§5)。

## 8. ユーザー判断と今回の文書検査

既決D番号の変更を求める点はない。**X15のT2aとT2bの間への前倒し、およびPM実測を根拠とする0x3F1000配置はユーザー決定 (2026-10-01)** として記録し、再承認を待つ未決事項から外す。追加のユーザー判断が必要になるのは、NP21/Wの64MB preset追加が必要な場合、各受入時の[D2]操作/実機物理操作。lease/API/trim/段の案は本票の独立レビューに出し、争点が3ラリーで決着しなければユーザーへ上げる。今はこれらの追加操作を承認済みとは扱わない。

今回の完了条件は文書検査のみ:

```text
make check-docs-links check-docs-orphans check-docs-status check-tests-inventory check-constraints < /dev/null
```

**2026-10-01 2回目反映後の実行結果: rc=0**。文書リンク (0 Errors)、orphan (0件)、状態行、試験一覧の最新性、制約17件の整合がすべて通った。`git diff --check` も rc=0。make開始時に既存 `/tmp/GMfifo3` に対する jobserver の `File exists` 警告が出たが、全5ターゲットが完走し終了値は0。コード/生成地図の更新・ビルド・ゲスト検証は今回未実施。将来の実装受入は §5 と分けて記録する。
