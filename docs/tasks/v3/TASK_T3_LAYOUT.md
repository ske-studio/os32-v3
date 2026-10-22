# TASK_T3_LAYOUT — T3 カーネル帯・常駐シェルの詳細設計

> 後続: [TASK_T4_T6_MODULES](TASK_T4_T6_MODULES.md)・[TASK_T7_AND_FOLLOWUPS](TASK_T7_AND_FOLLOWUPS.md)

> 状態: **受入完了 (2026-10-18)** — T3a0〜T3d2 の全段が着地し NP21/W 64MB で受け入れた (T3d2 の総点検 `~/os32-tmp/evidence/2026-10-18/t3d2_np21w/RESULT.md`)。後続 (T4) への引渡しは §7。構成別 (8MB・17MB・Ra266) と D15 の判定は台帳 (H-4・T3D2-D15・T4T7-RH)。
>
> それまでの状態: **実装中 (2026-10-17)** — T3a0・T3a1・T3b0・**T3ab** (6b4e37e、7 段の票はアーカイブ済み — `docs/archive/v3/t3ab/`) が着地、T3ab は NP21/W 64MB の受入済み (7 項、台帳 T3AB-4・LGY98-L12)。実機の回帰 T3bR は 2026-10-17 に全項目 PASS。**T3b1** (cbdf9b8) も着地し NP21/W 64MB の受入 4 項に合格 (2026-10-17、`~/os32-tmp/evidence/2026-10-17/t3b1_np21w/RESULT.md`)。T3c1・T3d0・T3c2・T3d1 も着地 (2026-10-17)。T3c2 と T3d1 は NP21/W 64MB で受入済み (`~/os32-tmp/evidence/2026-10-17/t3c2_t3d1_np21w/RESULT.md`、GUI の端末と H-5 の実機比較は台帳 T3C2-GUI)。T3c2 の受入で v2.1 からの不具合 EXECNEST-1 (esp0) を見つけて直した。T3d0 の実測で Unicode の表は像に入れない決定 (§6)。T3b2・T3c3 も着地し NP21/W 64MB で確認 (2026-10-18、`~/os32-tmp/evidence/2026-10-18/t3b2_t3c3_np21w/RESULT.md`: `[h4] high result=PASS entered=1`、F-9 の基準値 — 小割当 N=256 で割当 1.57 秒・EXACT 失敗 N(N-1)/2)。T3b3・T3c4 も着地し NP21/W 64MB で受入 (2026-10-18、`~/os32-tmp/evidence/2026-10-18/t3b3_t3c4_np21w/RESULT.md`: h4 の I/O の往復 4 通りと DMA pool が PASS で **H-4(a) を閉じる** (PCM の (b)(c) と構成別は台帳 H-4 に残る)、F-9 は小割当 N=256 で 1.57 秒 → 0.10 秒、残りは台帳 F-9)。次は **T3d2** (統合と引渡し)。 本文は r3 と案5決定を反映。未完了は T3b1 以後の段・測定ゲートであり、T3ab の受入とは区別する。
> **レビューの経過 (PM、2026-10-16)**: astra が起草 (r2・r3) → Opus 5.5 が独立レビュー 2 往復 (r1「直してから」、r2「確定してよい (未決の争点はユーザーへ)」)。往復の記録は `~/os32-tmp/run/t3design/T3_LAYOUT/` (`opus_r1.md`・`astra_r2_response.md`・`opus_r2.md`)。0x3FB000 の衝突はユーザー承認の**案 5** (DMA の池を 0x7F0000 へ移し、0x3E0000〜0x3FFFFF を見張り・PT の枠 26・見張り・スタック 16KiB に) で決着 (2026-10-16、astra 改訂 2 回 → Opus 2 往復で「確定してよい」、記録 `~/os32-tmp/run/t3design/opt5/`)。
> 設計: Codex。再照合基点 main `c81b27bed7c1b0d05e5e7d9de9763c4eded844e5` (2026-10-15)。旧基点 `3c4171a784cc47720392ab55b3c4a24aee17e6f3`、元の設計レビュー基点 `59c4285`。根拠の file:line は今回の木。実測は既存成果物の読取で、新規ビルド・ゲスト実行はしていない。
> 決定の正典は [MEMMAP_V3](../../decisions/MEMMAP_V3.md) D1〜D36、定義は `include/memmap.h`、実装済み地図は [02_memory](../../02_memory.md)。D・契約・順序は変えない。リンクは `docs/tasks/v3/` 配置時の相対パス。本票は本文を全部持つ。
> OS32 は PC-98・MIT・単一利用者・単一タスク・協調切替。攻撃者を想定せず、バグ/装置誤動作から戻せることと像の予算を守る。認証・暗号・権限分離強化は足さず、性能は P100/32MB の D15 まで。[CONSTRAINTS](../../CONSTRAINTS.md):16-205 ([C1]〜[D3])、装置の扱いを追加するなら [HW3]:92-98 のメーカー資料と BSD/MIT 実装を先に示し、GPL から写さない。

## 0. 再照合

以下は 2026-10-15 基点の調査記録。配置の未実装という記述は当時の状態で、T3ab 受入後の状態は冒頭と §2 の案5を参照する。

| 元票の前提・着手条件 | 分類 | 今回の照合と扱い (根拠) |
|---|---|---|
| T2c〜h の統合・受入後に着手 | 維持 | T2 の本体は受入済み、残項目は台帳で管理。受入済みと残関門の閉鎖を混同しない (`HANDOVER_2026-10-09.md:11-16`、`TASK_T2D_T2H.md:3`、§1) |
| e5 未統合、公開 query/lease・可変 map・trim 待ち | 吸収済み | e11 公開、f1〜f13、g1〜g5 の引渡し済み (`TASK_T2D_T2H.md:190-196,290-306,334-339`)。実装済みを再開発しない |
| resident CRT を USER CRT から分離 | 吸収済み | `build/programs.mk:39-49,83-84,144-147`、`sdk/allocator/nano_adapter.c:127-136`。T3 は新しい固定限界へ接続 |
| boot 後 kcg_load_font を NOSYS 化 | 吸収済み | `drivers/kcg.c:247-263`。BB/mailbox 初期化と公開順は維持し、Unicode 再検証だけ撤去 (§3) |
| SQLite 固定帯・旧 shell heap・FIXED backing を移す | 維持 | `build/os32.ld:71-108`、`include/memmap.h:317-344`、`kernel/memory_boot.c:211-246`。連結配置/SLACK は未実装 |
| 固定 PD/PT は画像外・最終位置の10枚 | 要改訂 | R7FIX2 の最上位 MMIO PT (`memmap.h:345-352`、`paging.c:226-229`) を含む 11 枚とも、案 5 (決定済み、§6) で `0x3E1000` からの PT の枠 (26 枚) へ移す |
| e3のnative VRAM UC・CLIENT/USER接続 | 吸収済み | `kernel/v86_mem.c:36-42,170-180` のsetup/teardown UC、`gfx/gfx_core.c:192,213,489` のCLIENT/publisherを継承。旧WB/未結線として扱わずT3のPTE回帰対象にする |
| Unicode は低位表と暫定 RO lease | 維持 | `lib/utf8.c:28-105`、`exec/system_surface.c:7-35`、`userland/lib/gfx/libos32gfx_core.c:78-106`。kernel/静的SDK/shlib/常駐の全経路を同時移行 |
| KAPI の版と自己試験の置き場 | 要改訂 | KAPI 75 (`sdk/kapi.json:3`)、自己試験は `kdebug/kselftest.c`。DBGREF S0〜S2 後も木は進み `build/kdebug.mk:6-9` は S4/nic_trace 構成。票 `TASK_DBGREF.md:3` の進捗と実物を分ける |
| LAN 前の予算・配備前提 | 要改訂 | LB-2c 送信まで、LB-3 は未受入 (`realhw/TASK_LAN_82557.md:3,8`)。LAN /host 段1は `fs/sfs_lan.c:12-32` と `tools/tests/test_lanfs.py:18-21`、実機 hsync 完了とはしない |
| KHEAP 192KiB・SHM224KiB・508KiB圧縮上限 | 維持 | 192KiB は上位票:82 の設計値。現行176KiBは暫定 (`memmap.h:42-44`) なので T3 で戻す。SHM は削らない、圧縮上限は `boot/boot_defs.h:93` |
| 文書計画提示・台帳引渡し | 維持 | `DOCS_REORG_T3.md:3,23,89` の承認済み計画と F-9/H-4(a) を §1 で受ける。設計を現状へ弱めない |
| SLACK、2heap peak、kstack、Unicode 時間 | 未測定 (T3配置) | 現配置の基準値 (H-5、実機 Ra266): 10/14 d14b は kstack=4,348 B・shell_stack=20,840 B・kheap_peak=111,960 B・resident_peak=131,656 B、**10/16 (86ba40c、LAN の hsync と GUI 往復の後) は kstack=3,544 B・shell_stack=20,440 B・kheap_peak=45,176 B・resident_peak=131,656 B** (`~/os32-tmp/evidence/2026-10-16/h5_mem/`)。kstack 16KiB に対し最大 4,348 B。resident_peakを2heap各々のpeakと同一視せず、T3c2で同じ指標と新しい個別peakを採る。SLACK算術は下記、T7将来分は未測定 |
| 構成別のゲスト受入 | 要改訂 | `V3_PLAN.md:41-46`、`DEFERRED_TESTS.md:19` の10/14決定に従い当面64MB一構成。8/17MB等の最低条件は最終段に残す |

**現行予算 [既存成果物の実測]**: `build/out/kernel.map:4452` の `__bss_end=0x1940C0` → 本体606,400 B。`memmap.h:259-261` の上限は `0x99000=626,688 B` (`build/os32.ld:23` と一致)、残り **20,288 B**。DBGREF 後30,176 B (`TASK_T2D_T2H.md:547`) から差引9,888 B増であり「LAN約+6.5KB」だけでは説明しない。KHEAPを今の帯のまま192KiBへ戻すと残り3,904 Bに減るため、Unicode表の先行投入は不可。
`build/out/vmkernel.lz4` は **500,430 / 520,192 B、残り19,762 B** (ファイル長、`generations-manifest.json:62-64`)、manifest:3-9 は基点/KAPI75/形式4・ABI1・配置3・shlib2を示す。kernel.bin と圧縮像の SHA256 は manifest と一致。これは T3 の圧縮後サイズではない。後続 LAN 統合で基点が動けば実装パックで再計算する。

## 1. 着手条件と成果物

T3 は T2c〜h の統合・受入後に実装する。旧「e5は別worktree」「T2完了後に確定」は吸収済み (§0)。T2 の未確認は T3 の成功で消さず、`DEFERRED_TESTS.md:15-21,82` の次回ゲスト8行・R7-1目視等と LAN L-B の関門を PM が閉鎖根拠で照合する。順序は R7FIX2 → ゲスト/実機 → LAN L-B → T3 (`DOCS_REORG_T3.md:123-126`) のまま、文書の再照合は並行可。§6 の案5の配置・予約検証を配置切替前の関門とする。
固定 PD/PT は R7FIX2 の11枚目を含めて §2-2 の画像外枠へ移設する。DMA を低位の池へ移し、旧 shell heap 周囲には両guard・余裕つきPT枠・16KiB kstackを置く (案5)。後続は [T4〜T6b](TASK_T4_T6_MODULES.md)、[T7 と後半](TASK_T7_AND_FOLLOWUPS.md)。

| 引渡し ID | T3 の責務と閉じる観測 |
|---|---|
| **F-9** | **着手前の扱いは、既知限界を台帳に残し、c3で現行USER allocatorを再現・測定、c4で正しさと性能を別判定し、必要時だけ副arena最小64KiBを比較する方針**とする (本票提示時にPM照合、`DOCS_REORG_T3.md:89,126`)。EFULL時の一覧保持/exit回収とrealloc成功時の旧領域残存/errnoは性能良否で閉じず、常駐2heap変更でも閉じない。f12の最大4/32・副arena1は対照のみ (`DEFERRED_TESTS.md:58`) |
| **H-4(a)** | 64MBで実bufferの物理ページを32MiB超へ置く専用fixtureとI/O往復を T3b2/b3 で実施 (`DEFERRED_TESTS.md:59`)。(b)(c)はPCM票、構成別の残りはv3最終段のまま |
| **X-4** | 選択漏れ3種は最初の依頼パック前、残りは遅くとも最初の取り込みの全体検査まで (`DEFERRED_TESTS.md:20-21,63`)。本票の文書完成では閉じない |

| 成果物 | 責務 |
|---|---|
| 一組の新配置成果物 | kernel・loader・resident shell/gshell・SDK/CRT・shlib・同梱userland・対のkdebugを同じ配置世代で再ビルド。旧shellは入口前に拒否。apps/gameはH-7再開時ゲートを維持 |
| 配置 manifest | ELFからcode/data/bss・暫定SQLite/stack・浮動鎖・SLACK・shellの半開区間、owner/用途/ページ数と世代・hashを生成。手書き実測の写しを増やさない |
| 起動診断 | 固定占有・SLACK・一般池・metadata・BB・kdebugを別勘定。KDEBUG=1は対の部品がRUNNINGで実試験が走ったこと、KDEBUG=0は監査継続を記録 |

### 1-1. 基点の実物と引渡し待ち

2026-10-15 時点の記録 (T3ab 実装前)。未完了の段は冒頭の状態行を参照。

| 実物 (今回の file:line / シンボル) | 確認した境界と残作業 |
|---|---|
| `build/os32.ld:76,82-108` / `__bss_end,__sqlite_start/end` | SQLiteは固定0x200000、BSS込み770,560 B (`kernel.map:4456,8010`)。T3の連結配置は未実装 |
| `build/kernel.mk:210-212,334`、`boot/boot_defs.h:58`、`boot/loader_fat_new.asm:405` | 通常像/試験像ともSQLite宛先が固定。C/ASMの展開上端と生成器を一緒に追随 |
| `kernel/memory_boot.c:22-23,211-246`、`kernel/paging.c:351-357,399` | 固定backingは旧番地、NP期待。runtime区間・core BSS・SLACKへの変更は未実装 |
| `sdk/allocator/nano_adapter.c:127-190`、`exec/exec.c:1885` | residentの固定break検査は存在。新しいexec_baseをsbrk限界へ渡す変更と2heapの共通計算が残る |
| `lib/utf8.c:288-308`、`drivers/kcg.c:220-221,247-263` | かなの算術変換・BMP外0と低位表参照、scratchのUnicode名依存が残る。NOSYSは既に実装済み |

T3前の予算再計算と文書計画提示は `TASK_T2D_T2H.md:532-553`、`DOCS_REORG_T3.md:89` の関門。旧版の未結線リストは再利用せず、公開KAPI75とT2のquery/lease・revoke/fallback・可変map・resident分離・trimを引渡し基準にする。

## 2. 配置の計算と起動契約

### 2-1. SQLite を含む暫定の鎖

`build/os32.ld` は現在 SQLite を 0x200000 へ飛ばし、`__bss_end` と `__sqlite_end` を別々に扱う。T3 ではコアの BSS の後をページ整列して SQLite の text/rodata/data/bss を連結し、**SQLite の全 BSS と暫定代替スタック 128KiB を含む占有終端**から KHEAP → KAPI → guard → SHM → guard を計算する。

設計シンボル `__kernel_resident_end` をこの占有終端に設ける。`__bss_end` はゼロ化対象の終端であり、鎖の起点へ無条件に流用しない。SQLite の BSS は独立範囲として `kentry.asm` でゼロ化し、NOLOAD の隙間をファイル長と混同しない。T4 で SQLite が外れたら `__kernel_resident_end` はコア BSS の整列終端になる。KHEAP 192KiB・KAPI 4KiB・SHM 224KiB の既決値を維持し、E11-BUDの暫定176KiBを戻す (§0)。coreの `__bss_start/end` とSQLiteの `__sqlite_data_end/__sqlite_end` は各開始を4byte整列、各終端を全BSS格納後に4byteへ切り上げ、両端の整列・順序・guard非交差をlink ASSERTで保証する。paddingも占有に含め、現行DWORD単位のゼロ化で全域を覆う (`kernel/kentry.asm:32-46`、現行未保証箇所は `build/os32.ld:71-76,97-108`)。

暫定代替スタックは下端に1ページのNP guardを置き、guardも占有終端と予算へ含める。guard用ページは全BSSゼロ化対象から除外し、PG有効化後にNPへする。暫定代替スタックは linker symbol で表し、現行 `sqlite_stack.asm` の引数へ渡す。T4 の可変ポインタ化までの移行橋であり、固定 SQLite 帯を別の番地に再作成しない。`build/kernel.mk` の別圧縮 SQLite エントリは T3 中は維持してよいが、宛先は ELF の `__sqlite_start` から導出し `--sqlite-addr 0x200000` を撤去する。全エントリのロード先・BSS・スタックを含む非重複検査を追加する。T4 でこの暫定エントリを廃止する。

SLACK は `[align_up(chain_end, PAGE_SIZE), MEM_FIXED_PAGING_GUARD_BASE)`、新しい終端定義は `MEM_FIXED_PAGING_GUARD_BASE = 0x3E0000` (DMA番地から独立)。終端を切り上げたページを重複登録しない。空区間は合法、逆転はリンク時エラー。KERNEL_SLACK は通常 RAM と同じ owner 割当を行い、固定 kernel owner からの無条件 reclaim で生み出さない。

### 2-2. 固定領域とページング

最終番地の表は上位票 §2-1 を参照し、値の正典は `memmap.h` とし、linker/boot ASMの必要な写しも同じ差分で更新して生成検査で一致させる。`MEM_POOL_BASE` は shell 新帯の直後へ移り、台帳の L0/固定用途、メタデータ置場、workspace、DMA、BB 探索下限、起動最小 RAM 判定も同じ差分で追随させる。旧 SQLite 予約と旧 shell heap 予約の削除漏れ・二重供給を検査する。

**案5のページ配置 (半開区間)**: guard `[0x3E0000,0x3E1000)`、固定PD/PT枠 `[0x3E1000,0x3FB000)` (26枚)、guard `[0x3FB000,0x3FC000)`、kstack `[0x3FC000,0x400000)` (4枚)。PD は `0x3E1000`、boot PT 8枚は `0x3E2000 + i×0x1000` (i=0〜7)、device PT は `0x3EA000`、最上位MMIO PT は `0x3EB000`。予備15枚 `[0x3EC000,0x3FB000)` も固定kernel ownerで保持し、枠全体を恒等sup/RW/WB、両guardだけNPとする。使用終端と枠終端を別定義にし、余りをSLACKへ返さない (`include/memmap.h:345-352`、`kernel/paging.c:226-229,320-323` の11枚を維持)。
**DMAの番地は固定 `MEM_DMA_POOL_BASE = 0x7F0000`、64KiBを採る**。起動時に低位の池から64KiB整列で探索予約する案も可能だが、8MB時の確保保証には一般割当より先の専用予約が要り、番地の受渡し・失敗処理も増える。固定なら `[0x7F0000,0x800000)` が8MB末尾のRAMかつ集積域/同梱域 `[0x500000,0x700000)` の外で全機種同じになる。0x700000では8MB機の低位の池が3区間に割れるため、同梱域の後ろに約1.9MBを連続で残せる末尾を採る。検出RAMが終端0x800000に届かなければDMAを使う装置 (PCM・82557) だけを無効にし、診断を1行出して起動を続ける (池の初期化・自己診断・当該装置の起動を行わない)。予約重複は診断停止する。一般池・metadata・BB等を配る前に `LEDGER_R_DMA / LEDGER_OWNER_KERNEL / WB` として恒久予約し、通常RAMとの二重供給を禁止する。物理<16MB・64KiB整列/境界非跨越 [HW2]・恒等supervisor/RWは上位票 §2-1 の資料根拠に従う。
`kernel/dma_pool.c:20-26` は固定定数の変更で追随し、内部割当/返却は現契約を維持する。PCM は返却された `.va/.pa` と16MB上限を使い (`drivers/pcm_cs4231.c:526-531`)、82557 はPCIバスマスタへ `.pa` を渡す (`drivers/e82557.c:694,729`)、ともに番地の直書きを増やさない。FDC は独立バッファで池を使わない (`drivers/fdc.h:243`)。予約・恒等写像確認 → `dma_pool_init` → 池を再初期化する自己診断 → `pci_bind_all`/PCM利用の順を維持する (`drivers/pci_bind.h:160`、`kernel/kernel.c:588-617`)。新番地でPCM転送と82557送受信・停止/回収を実機T3bRで確認する。

順序は PG=0 で固定 PD/PT をゼロ化・構築 → 予約/NP 範囲を適用 → 固定 PD/PT 自身と kstack を supervisor/RW で確認・必要なら再写像 → CR3 → PG=1。固定 PT を旧 shell 帯の未使用領域として NP にする処理を残さない。PDE 1 の shell の PTE は全 AS 共通 supervisor。APP_BAND の私有 PT と同じ番号だという旧仮定は T2 で消えていることを再監査する。

**FIXED 型の台帳 backing は core 常駐BSSの末尾へ移す。** 2ページ (metadata1 + workspace1) をページ整列した専用NOLOAD sectionとして `__bss_end` の内側に置き、`__ledger_fixed_start/end` をexportする。旧DMA末尾/stack guard隣接の導出式は廃止し、`MEM_LEDGER_META_BASE/END` はそのlinker symbol参照へ替える。従来の番地STATIC_ASSERTは、サイズ/枚数のSTATIC_ASSERTと、整列・core内包含・他section非重複のlink ASSERTへ分ける。`memory_boot.c` の固定区間表はcore占有をこの2ページの前後に分割し、metadata用途との二重登録を避ける。旧 `[0x2F9000,0x2FB000)` は新地図の他の占有が無ければ通常のSLACKとして扱う。区間はbyte半開→整列済PFN半開の順で変換し、空区間は登録を省く。

この2ページは **paging_init時点で全機種ともcore BSSとして恒等sup/RW/WBでpresent** にする。旧 `boot_range_valid(..., 0)` のNP期待は廃止し、present・USERなし・物理一致の期待へ替える。FIXED機の `paging_map_ledger_backing()` はその既存写像を検査する専用入口として残し、NPからpresentへ切替える責務は持たせない。ARENA_TOP機では動的backingを使い、core内の予備2ページは未使用のままkernel owner/sup RWで保持し、途中でSLACKへ返さない。`memory_boot_fixed[]` のsymbol由来区間 (core分割/metadata/SLACK) は実行時に配列を構築し、linker symbolを割った値をstatic const PFN初期化子へ入れない。`tools/gen_memmap.py` は固定数値の正規表現からELF symbolを読む経路へ替え、`test_memory_boot.py` の定数変異と `memmap_boot_host.c` / `memory_boot_host.c` の旧NP期待も同じT3abの切替差分で変更する。

全機種でcore常駐が8KiB (別途ELF整列差) 増えてSLACKが同量減ることを予算へ記録する。新しい固定番地の穴を固定 PT 手前の見張りの直前へ作らず、Dの帯を変えない。

SHM後方guardと固定PT手前guardまでのSLACKは区別し、旧「後方予約をまとめてNP」にする処理を撤去する。SLACK全域はPG開始時から恒等sup/RW/WB、guardだけNP。

リンク ASSERT は、鎖終端≤`MEM_FIXED_PAGING_GUARD_BASE`、guard/PT枠/guard/stack の隣接・非重複 (1/26/1/4頁)、使用PD/PT枚数11≤枠26、画像がPT枠へ入らないこと、DMAの64KiB整列・サイズと `0x7F0000=DMA開始<DMA終端=0x800000`・起動用域非交差・15〜16MB除外、shell帯と池の隣接を検査する。SLACK と shell の恒等写像を T2 の master/AS 検査へ追加する。ISR の stack 切替、TSS.esp0、初期 ESP は同じ kstack シンボルを使う。

### 2-3. シェル 1 箱・2 ヒープ

`app_sys.ld` は shell 画像を新帯起点へ置き、既存の `_end` (`sdk/link/app_sys.ld:29`) を使う。帯上端から 40KiB stack とその下の guard を引いた地点を heap ceiling とする。上位票:89の1MiB帯なら、stack `[0x4F6000,0x500000)`、guard `[0x4F5000,0x4F6000)`、exec_heap `[0x4B5000,0x4F5000)`、最低sbrk余裕を引いたimage_end上限は `0x495000` (帯起点から596KiB) となる [式から算出]。値は共通定数から生成し直書きしない。画像/BSS の上に sbrk と exec_heap の共通供給領域を置く。

**T3では固定分割を採用する。** D8の「API2・供給元1」は、同じshell専用帯の残りを起動時の一つの境界計算で分配する意味とし、実行中の両heap間の貸し借りは行わない。`shell_heap_claim/release_tail` 案は採用しない。heap ceiling直下の **256KiBをexec_heap**、その下の画像終端からexec_heap開始までをsbrkへ固定する。`MEM_SHELL_EXEC_HEAP_SIZE` を `memmap.h` に定義し、共通初期化が `{image_end, sbrk_limit=exec_base, exec_base, exec_end=heap_ceiling}` を一度だけ作る。`image_end=align_up(_end, allocator_alignment)` とし、`image_end≤exec_base≤heap_ceiling<stack_top` とsbrkの最低余裕128KiBをリンク/起動検査し、足りなければそのshell画像を拒否する。実行中にexec_baseを下げないのでCRTの現在breakをkernelが知る必要はない。

T2fのCPL=3用 `_sbrk` (EXACT mem_map) と **常駐shell/gshell用CRTのビルド分離は既にある** (`build/programs.mk:39-49`)。resident版は `nano_adapter.c:127-190` の固定 `sbrk_heap_limit` と範囲/符号/overflow検査を継承し、exec側が共通計算した `exec_base` を限界に渡す。ユーザー入力フラグでresident経路を選ばせない。exec_heapは固定256KiB内で現行allocatorを初期化する。shellヘッダheap_szは0なら256KiB、0超256KiB以下ならexec_baseから下詰めでその長さを使い、余りをsbrkへ渡さない。256KiB超は入口前に拒否。公開後の移動・再分割はせず、子終了は `exec_heap_restore_state()`、shell heapの再初期化は禁止。

この256KiBはT3用の暫定設計値としてT3cでshell/gshellと子ネストのpeakを実測し、T7bでfont込みの再ゲートを通す。全体の SLACK 再計測では、T7 の page0 snapshot は既存4096Bの転用として追加0Bとする ([T7 §1-1](TASK_T7_AND_FOLLOWUPS.md#1-1-v86-状態遷移))。既存占有を二重計上しない。T7bの未来の実測をT3完了の前提にはしない。足りなければ同じ1MiB内の固定分割定数を変更・全再ビルドして再受入し、実行中の境界移動で救済しない。

**T3 の予算式 [推、未リンク]**: `C=align_up(現行__bss_end,4096)+8192`、`R=align_up(C+770560,4096)+4096+131072`、`chain_end=R+196608+4096+2×4096+229376` とするとT3abは `0x2E0000`、SLACKは1,048,576 B (256頁)。FIXEDの2頁は全機種常駐で、ARENA_TOPの動的metadataは別勘定。SQLiteの128KiBは既存占有の移動、guard4KiBとKHEAP復元16KiBは増分として数える。
Unicodeの7,063組×4 B=28,252 B (`lib/unicode_jis_table.h:2-8`)、FIXED8,192 B、実装code/data/BSS増分の仮枠16,384 B [推] を現行像に足し、各境界を丸めると `chain_end≈0x2EA000`、SLACK≈1,007,616 B (246頁)。仮枠はT3ab8KiB・c2KiB・d2KiB・予備4KiB、SDK/試験部品は別測定。案5でもSLACK終端は0x3E0000で不変、低位の一般池はDMA予約で64KiB減り、固定PT枠は予備込みで保持する。圧縮増分はT3d0の試作像とd1の完成像の実長で確認し、shellは1MiB内、kdebugは池の実ページで別ゲートにする。H-4(a)の試験本体はkdebugへ置くが、範囲指定割当のimport追加はexport表は少なくとも1枠4 B＋整列差を常駐仮枠に計上する [推] (`kernel/pgalloc.h:215-218`、`build/kdebug.mk:10-13`、`kdebug/generated/kdebug_kexports.c:58`)。製品像増分0とは断定しない。

## 3. Unicode の前倒しと ABI

T3でUnicode組表をkernel `.rodata` と二分探索へ移す。`lib/unicode_jis_table.h:2-8` の7,063組/28,252 Bは今回の読取でも厳密昇順・重複なしを確認した。単一翻訳単位だけに置き、kernel内は直接呼出し、ユーザー側は **KAPI `unicode_to_jis` を選ぶ設計案** (`u32 cp` → `u16 JIS`、未収録/BMP外は0)。`lib/utf8.c:288-308` のかな範囲の算術変換も含め、全BMPと範囲外の結果一致を条件とし、表ポインタを公開するAPIは作らない。
T2の暫定Unicode RO leaseは全同梱callerのKAPI移行と同一切替で撤去する。対象はkernel読込/register、`exec/system_surface.c:7-35`、静的SDKとshlibの `userland/lib/gfx/libos32gfx_core.c:78-110,198`、CPL0常駐の低位直読、関連初期化/解除、生成/配備のunicode.bin。`lib/utf8.c:60-105` のready/4点照合とsetter、`kcg_boot_phase_close()` のUnicode再検証も同時撤去し、未初期化領域を読む経路を残さない。公開queryのUnicode roleは未登録と同じ `OS32_ERR_INVAL` (`exec/system_surface.c:29-32`) とし、TVRAM等のroleと既存slot番号は維持する。apps/gameはH-7再開時にcaller追随・再ビルド・日本語描画を受入ゲートにする。
boot後の旧 `kcg_load_font` NOSYS化とfont_load_testの期待変更はT2e8bで吸収済み (`drivers/kcg.c:263`、`TASK_T2D_T2H.md:154-157,221`)。T3では継承を確認し、同じ意味変更を版更新へ二重計上しない。Unicode KAPI追加の版は現在75を起点に実装時に予約し、`sdk/kapi.json`と`KAPI_VERSION`を揃え、生成物は手編集しない ([ABI1]〜[ABI3]、`KAPI_SPEC.md:49-83`)。
`kcg.c:220-221` のLZ4 scratchはT7aまで旧低位FIXEDに保持し、専用定数 `MEM_KCG_LZ4_TEMP_BASE/END` (従来0x4A000〜MEM_CONV_END、344KiB)へ名前を分離する。poolの連続確保や常駐増はしない。Unicode用途を外しても低位128KiBを一般池へ返さず、kernelのFIXED用途としてT7aまで保持する。scratchがplanar BB/mailboxと重なるため、boot読込→scratch失効→BB全消去/初期化・mailbox初期化→host操作/V86/通常AS・USER leaseの開始を守る。現在のclose順は `kernel/kernel.c:773-776`、消去は `drivers/kcg.c:247-253`。公開後のBB leaseは禁止せず、kcgからの上書きを禁止する。
T7aまではV86の636KiB backingを維持し、低位を直接渡さない。T7aで旧scratch/BB用途を撤去してV86へ専有を引き渡す (一般池へ返さない)。font_load_testは期待NOSYSのPASS試験を維持し、BB/mailbox不変はhost読取かkdebug自己試験で照合する。未貸与mailboxをCPL3試験が直読せず、SKIPで回帰を隠さない。`tools/tests/guest_tests.txt`・`test_result_conv_host.c`・`test_unicode_surface.py:15-35,60-65` の旧期待も新しい不成立/変換口へ置換する。
D35の4世代を再定義しない。配置切替時は `sdk/kapi.json` の `generations.memory_layout` を更新し、Unicodeは機能版の末尾追加とする。旧shell・旧shlib・新SDK+旧.o・NHD/HostDrv混在は入口前拒否。kernelのImage CRCが変わるたびに対の `/boot/kdebug.bin` を再生成し、古い部品による「自己試験0件」を合格にしない (`TASK_DBGREF.md:6-9`)。

## 4. 実装分割と受入

各行は実装・対象試験・レビュー直しを含む半日〜1日の独立受入単位 [推]。番号付き受入は各段10項以下とし、パックで増えたら分割する。新しいfixture/診断の結線先を示し、単に未使用コードを置いて完了にしない。T3a/T3bは下記の準備段と**一体切替T3ab**へ分け、symbol化・runtime区間・paging期待を別々に配備しない。

| 段 (依存) | 変更範囲・独立して閉じるもの | 観測可能な受入条件 / 代表変異 |
|---|---|---|
| T3a0 (関門照合後、0.5日) | ELF配置manifest・予算/写し検査の準備。`tools/gen_memmap.py`、`test_memmap_gen.py`。現配置も検査可能にする | ①現ELFと既存manifestの値一致 ②code/data/BSS/NOLOADを半開区間で出力 ③SLACK鎖終端は `MEM_FIXED_PAGING_GUARD_BASE` (0x3E0000) −1頁/一致で通り+1頁は失敗。DMA境界はb3で別検査 ④BSS省略・区間重複・C/ASM写し不一致の変異を検出。製品像増分0 |
| T3a1 (a0、0.5〜1日) | `mkvmkernel.py`・`build/kernel.mk` の通常/R1像のSQLite宛先をELFから導出、C/ASM loader fixture | ①現配置の展開先/CRC不変 ②連結配置fixtureも同じ生成器で通る ③ロード先+長さのoverflow/重複/BSS食込みを拒否 ④BSSゼロ化区間の両端4byte整列・全BSS包含・guard非交差をELF/link ASSERTで確認 ⑤圧縮像≤508KiB。旧0x200000強制・NOLOADをファイル長扱いの変異を検出 |
| T3b0 (a0、0.5〜1日) | 台帳runtime区間とpagingのhost fixtureを先行。`test_memory_boot.py`、`memory_boot_host.c`、`memmap_boot_host.c` | ①新旧地図を同じ入力で検証 ②FIXED/ARENA_TOP両方でmetadata二重登録0 ③SLACK空/逆転/境界を区別 ④PT/stack/guardの重なりを拒否。旧NP期待・SLACK二重供給の変異で失敗 |
| **T3ab (a1,b0、1日)** | `memmap.h`・linker/kentry・paging/memory_boot/physmem/pgalloc/dma・shellリンク/exec限界を一体切替。既検証の式/fixtureを結線し世代を更新。0x2E8000・0x3F1000・0x3FB000を直書きする `tools/tests/dma_pool_host.c`・`tools/tests/pcm_cs4231_host.c`・`tools/tests/dma8237_host.c`・`tools/tests/test_mem_stat.py`・`tools/tests/test_memmap_gen.py`・`tools/tests/paging_bounds_host.c` (0x3FB000 の CHECK、新しい MMIO PT は 0x3EB000)・`tools/tests/test_dma_pool.py` (0x2F0000 をまたぐ前提、新しい池は 64KiB 整列)・`tools/tests/vk32_host.c` と `tools/tests/test_vk32_crc.py` (展開の上限 0x2E8000) と `drivers/fdc.h` の注記も追随。`kernel/pgalloc.c:1044-1048` の `ledger_arena_freeze` は 0x500000 から上へ最初の PERSIST で止まるので、DMA の予約 (0x7F0000) でも止まる — `sys_usable_mem_end` の意味 (低位の連続の上端) をこの段で書き直す | ①同世代一式と対kdebugの配備予行が通り、ONでRUNNINGと実試験実行を確認 ②link ASSERT/manifest/予算一致、固定PTは承認配置 ③PG直後とAS生成後のSLACK/shell/metadataは恒等sup/RW/WB、guardはNP ④SLACK全頁を一度だけ割当/返却、固定頁混入0 ⑤SQLite正常DB/FEP・BSS全域/stack guard成立 ⑥NP21/W 64MBの起動/地図監査0fail、PEGC通常経路とLGY-98 L0〜L3回帰が通る (FFF00000hと82557はbRへ) ⑦圧縮≤508KiB・KHEAP192KiB。NP化/旧pool下限/metadata重複の変異を検出 |
| T3b1 (ab直後、0.5日) | kdebug/監査、`tools/accept/` の地図採取、DBGREF配備結線の受入 | ①KDEBUG=1でCRC一致/RUNNING、追加自己試験の実行と0fail ②KDEBUG=0で部品読込/割当0・常時監査0fail ③古い部品は診断付き不採用、ONの受入は未成立と表示 ④SLACK/一般池/metadata/BB/部品の頁数保存と総和一致。RUNNING判定省略変異を検出 |
| **T3bR (b1、実機セッション、0.5日)** | Ra266の配置変更回帰をシリアルで判定、立会い不要。PT処置後にも必須 (`TASK_LAN_82557.md:3,24,41`、`DEFERRED_TESTS.md:82`) | ①同世代一式と対kdebugを照合 ②sys16m=1で `[pegc] win=fff00000`・`fff=ok`・GUI入場時 `alias=ok` ③LB-R5相当のMAC/EEPROM値が既受入値と一致し、T3着手前に受入済みの送受信経路も再実行 ④監査/自己試験0fail・fault 0。実機待ちは日数見積から分け、未実施ならT3全体は未受入。R7-1の目視関門 (§1) を代替しない |
| T3b2 (b1、0.5〜1日) | H-4(a)用の高位backing fixture/入口は `kdebug/kselftest.c` に置く。既存 `pgalloc_alloc_n_owner` をimportしてowner付きで物理32MiB超を選ぶ (§2-3の微増)。製品allocatorの方針と公開KAPIは変えない | ①USER VA→PFNを全頁採取し全bufferが物理32MiB超 ②通常割当の低位対照も通る ③ページ跨ぎbufferに異なる既知パターン ④確保失敗は未実施と明示し入口へ進めず、終了/失敗でowner頁0。高位をVAだけで判定する変異を検出 |
| T3b3 (b2、0.5日) | H-4(a)の実I/O受入、DMA pool境界の再確認 | ①64MBで同bufferを使うファイルwrite/readの内容完全一致 ②高位bufferを通るHostDrv経路も同じ確認 ③DMA pool割当は新帯内/64KiB非跨ぎ、FDCは既存BSS窓のまま16MiB未満/非跨ぎを確認 (`drivers/fdc.c:50-79`)、HostDrv経路とISA DMAの証拠を混同しない。PFN証拠と結果を揃えてH-4(a)を閉じる、PCM(b)(c)は閉じない |
| T3c1 (b1、0.5〜1日) | shell2heap共通境界・resident adapterの接続、heap_sz検査。`exec.c`/`exec_heap.c`/`app_sys.ld` | ①image_end〜exec_base余裕128KiB以上 ②heap_sz=0/1/256KiBが規定どおり、+1は入口前拒否 ③sbrkの上限+1/負方向下限超え/overflowでbreakと両heapデータ不変 ④exec枯渇も親データ不変 ⑤USER mem_map経路は従来どおり。上限混同・失敗後break更新・余り再供給の変異を検出 |
| T3c2 (c1、0.5日) | shell/gshell・子ネスト・CUI/GUI復帰、番地直書き監査 | ①親sentinelを子正常終了/fault/STOP後も保存 ②同じ操作20回でpool/owner/heap消費が累積しない ③stack guardのhost検査と既存fault回収試験 ④sbrk/exec_heap個別peakに加えH-5と同じ4指標を採取し、§0の現配置基準と同条件で比較、各上限未満 (異なる構成の値は参考扱い) ⑤旧shell入口未到達、新shell起動。`dbgserial.c`/`memmap_audit_live.py`/gen_memmap/gshellコメントの旧帯残存も検出 |
| T3c3 (c2、0.5日) | F-9の測定fixture、現行USER allocatorを通る基準採取 | ①小割当N=16/64/256でarena数・EXACT失敗回数・map回数・free/trim時間を採取 ②64KiB閾値±1と大塊の虫食いでextent32本到達/EFULLを観測 ③realloc旧unmap失敗の残ブロック・errno・exit回収を確認 ④同じゲスト/入力で変更前後を測れる。模擬tickのcfg_benchやLinux SQLite時間で代用しない |
| T3c4 (c3、0.5〜1日) | §1で決めたF-9方針の結果を確定。副arena最小64KiBは必要時の比較候補 [推] (`nano_adapter.c:245-262`) | ①正しさ: EFULL後の一覧/owner保持とexit回収、realloc成功時の新データ・旧領域残存・errno=ENOMEMを個別判定 (`nano_adapter.c:402-416,520-524`)。性能良好でもこの既知限界を閉じず、差異/破損/回収漏れは修正対象としてPMへ返す ②性能: 同条件の時間・EXACT/map回数を記録、D15未測定と分けて不要な最適化は採らない ③改善が必要なら最小64KiB案を同入力で比較し、確保不能時は従来の要求丸め量で再試行 ④正味map失敗/余剰常駐/時間と失敗原状維持・trim/exit回収で採否を記録。最小量変更でO(N)/EFULLまで解消扱いにせず、残項目はF-9の内訳としてPMへ返す |
| T3d0 (b1、0.5〜1日) | Unicode二分探索とkernel/user分離のhost fixture、配備しない試作像で圧縮予算を事前実測 | ①全7,063組と全BMPを旧変換結果と照合 ②先頭/末尾/未収録/BMP外/かな端点で一致 ③kernelは直接・SDKはKAPI経路を確認 ④順序/重複/二分探索の端点/失敗値変異を検出、表1実体28,252 B ⑤LAN L-B統合後のT3配置に表・変換コード・KAPI接続の試作を入れ、通常/R1それぞれを本番と同じ `mkvmkernel.py:123-176` で圧縮し長さ/余裕/基点を記録。削除予定分は実際に削った分だけ差し引き、≤508KiBでd1へ。超過ならd1を止め§6へ実測値と両案を追記 |
| T3d1 (c2,d0、1日) | Unicode追加・全caller/lease/読込撤去・scratch名整理・KAPI生成を一体切替 | ①版/生成物/C・Rust stamp一致とclean全再ビルド ②静的SDK/shlib/常駐/kernelの日本語一致 ③低位表直読・unicode.bin読込・Unicode lease取得が0 ④boot close後NOSYS、BB/mailbox不変、次描画成功 ⑤旧query不成立と他role維持 ⑥圧縮≤508KiB/表1実体/全予算更新。leaseだけ残す・CPL0直読残存・scratch再開の変異を検出 |
| T3d2 (b3,bR,c4,d1、0.5〜1日) | 統合と引渡し。予算・世代・日本語時間の最終計測 | ①同世代manifestと対kdebugを照合 ②64MBで地図監査/自己試験0fail、CPL3 fault/STOP→次起動 ③2MB試験3種・GUI終了後CUI・DB/FEPが通る ④日本語の起動/入力→描画時間を同じ文面・環境で記録 (D15判定は未測定として台帳へ) ⑤全占有/SLACK/shell/KHEAP/kstackの実測と未測定を区別し後続へ渡す |

T3b1 の記録 (2026-10-17): 決定 — 起動時に `[mem] pages` で固定・SLACK・一般池・metadata・BB・kdebug・その他の頁数と総和を照合 (`kernel/kaudit_boot.c`、不一致は MemStat の audit_fail)、SLACK の一巡は post_exec の `slack_cycle` (台帳 T3AB-4 を閉じた)、kdebug の import に pgalloc_alloc_n_owner ほか 5 本 (T3b2 も使う)。未確認 — SLACK に owner の無い永久予約を置くと slack_cycle が誤 fail する (今の配置では到達しない、T3c で SLACK に予約を置くときに見直す)。「地図採取」は既存の `tools/accept/` の道具で足り、`tools/memmap_audit_live.py` の直書きの直しは T3c2。手戻り 4 回 (段の外の試験の器具の追従 3・静的検査 1、製品の誤りは 0)。証拠 `~/os32-tmp/evidence/2026-10-17/t3b1_np21w/`。

T3abに必要なshellの最小境界結線はc1待ちにしない (新帯を旧sbrk限界で起動しない)。a0/a1/b0は現配置を保ったhost受入、abで実配置を切替え、d0はその受入済み配置の試作像をhostで測る。危険度の高いheap/exec/paging変更は64MBゲスト受入を終えてから次を積み、実機bRは統合d2の必須依存とする。H-4(a)はkdebugから範囲指定割当→試験ASのUSER VAへ写像→全頁PFNを記録→既存caller試験経路でI/O往復→AS/ownerを回収する (`kselftest.c:750-838`、`kernel/kdebug_imports.def:183-199,815`)。低位対照と頁を跨ぐ高位bufferのbyte一致を取り、専用の途中I/O失敗注入・境界長の総当たりは追加しない。各段が1日または10項を越えるなら準備fixture・台本をさらに分け、不可分な切替自体は分断しない (`ROLES.md:19`)。
変異は一度に一欠陥を入れ、正常対照PASSと対応するassert/観測FAILを対に残す。コンパイル失敗を検出成功と数えない。実装者と PM の検査分担は [ROLES §0](../agents/ROLES.md#0-現行の体制) に従う。ABI変更後は [ABI3] のclean→all、386命令/生成物/世代/配備manifest検査も通す。試験は `check_slot.sh` を通し同時2本まで (`ROLES.md:18-20`)。文書段階で合格ログを作らない。
`vmkernel.lz4≤508KiB` はa1/ab/d0/d1で必須。現余裕19,762 Bに表が入るかは未測定で、d0の試作測定をd1開始条件とし、以後基点/コードが変われば再測定する。超過時は§6へ「表の一括移行を維持して像を削る側」と「508KiBを守るため段取り再検討を求める側」の根拠・不足量を残し、T6b/SQLite分離の前倒しや上限変更を本票で決めない。F-9/Unicodeの時間は同条件の記録だけとし、D15のP100/32MBで起動2秒・入力→描画100msという目標を維持し、達成判定は未測定として台帳へ渡す (c4でF-9の正しさを別判定し、性能を合格扱いにしない)。
> 2026-10-14ユーザー決定: 今のゲスト受入はNP21/W 64MB一構成。8MB planar/PEGC、17MB (Cirrus/fallback含む)、Ra266 64MBの構成一覧と最低条件はv3最終段へ残す (`DEFERRED_TESTS.md:19,59`)。構成別の全通しとは別に、今回の配置変更に対するRa266のPEGC最上位窓/82557回帰はT3bRで省略しない。
失敗時は旧新を混ぜず直前の一式へ戻す。NHD/HostDrv/実機の配備予行とhash確認、停止→配備→起動は [V1]・[D1]〜[D3] と既存手順に従いテスターが行う。既存データを新masterで上書きしない。T3のSLACKは未分離SQLiteを含み最終余裕とは区別する。T7 の page0 snapshot は既存4096Bを転用し追加0B。T7b font込み shell peak は将来の再ゲートで、T3の完了条件に先取りしない。

## 5. 独立レビュー対応 (往復1)

以下の往復1〜3は旧版の履歴。再照合r2は Opus 5.5 が2026-10-16に独立レビューし「直してから」(P1なし、P2×3/P3×5、`opus_r1.md`)。本r3でA1〜A5/A8を採用、A6/A7を一部採用し、各根拠は `astra_r2_response.md` に記録。その後の独立レビュー r2 と案5のユーザー決定は冒頭の記録を参照。

レビュアー `claude-opus-5-5` (Opus 5.5)、2026-10-01、判定Request changes。実装検証ではなく文書/コード照合。この行は当時の記録で、修正後の確認結果は下の往復2/3。

| 指摘 | 到達性の確認と対応 |
|---|---|
| P2-1 metadata | `memory_boot.c` のDMA guard隣接式は新PDと衝突。§2-2でcore BSS2ページへ、ARENA_TOP時も固定保持、予算/ASSERT/登録を具体化 |
| P2-2 shell break | CRT static breakはkernelから観測不能。§2-3で固定分割256KiBを選択、resident CRTをT2f map経路から分離 |
| P2-3 kcg scratch | 344KiB連続pool確保は不要。§3で低位FIXEDをboot限定維持、BB/mailboxとの重なりも監査 |
| 非blocker1〜4 | SLACKの旧NP撤去、VK32 ASM/生成器、508KiB検査、Unicode読込とlease同時撤去を明記 |
| 非blocker5〜7 | 追加trapの測定、SQLite stack下guard、番地直書き監査を追加 |

### 往復2の確認と最終追記

同じレビュアー `claude-opus-5-5`、判定 **Approve (文書設計、N1/N2追記条件)**。往復1のP2-1〜P2-3と非blocker1〜7はすべてclosed。実装/未測定ゲートの合格ではない。

| 指摘 | 最終追記 |
|---|---|
| N1 caller/NOSYS | §3にfont_load_test・guest台本・host期待値・kapi.json説明の同時変更を明記。後続レビューでNOSYS化はT2eへ前倒し、T3はUnicode追加分を更新 |
| N2 backing写像順 | §2-2で全機種ともPG開始時present、FIXED専用入口は既存写像検査へ。旧NP期待をhost試験でも変更 |
| N3 実装注記 | symbol由来区間のruntime構築、gen_memmap/変異のELF対応、heap_szの下詰めと上限を明記 |
| 自己点検 scratch寿命 | 低位BBと重なるscratchはboot中のみ有効、失効→BB全初期化→lease公開の順。公開後のBB leaseを禁止しない |

往復3: 同じ `claude-opus-5-5` が **Approve**。N1/N2/N3とscratch寿命はclosed。最終非blocker R1〜R3はT3a/b一体着地、T7aまでの現行V86 backing維持、BB/mailbox観測をhost/kselftestへ分離する注記で対応。PM方針に従いこの文言追記の追加レビューは行わない。実装・各測定ゲートは未実施。

## 6. 未決の争点

| 論点 | 両方の言い分・根拠 | 決着までの扱い |
|---|---|---|
| **決定 (案 5、ユーザー承認 2026-10-16): 0x3FB000 の衝突** | DMAを低位の池へ移し、画像外の固定PT枠を26枚へ拡張、現11枚を収容する (§2-2)。0x3FB000はguard、kstackは16KiBを維持 | 番地は0x7F0000固定を採る。manifest・予算・PG/AS検査を新配置へ更新し、実機T3bRでFFF窓・alias・PCM・82557とstack高水位/guardを確認。常駐/圧縮増分は未測定 |
| 旧案1/2 (不採用の記録) | core BSSへ11枚目を置く案1は画像外契約とSLACKに影響、guard/stack一式移設の案2は移設先とshell隣接の再設計が必要 | 案5は画像外・両guard・16KiBを維持 |
| 旧案3/4 (不採用の記録) | 0x3F0000へ11枚目を置く案3は手前guardを失い、stackを12KiBにする案4は未観測負荷への余裕を減らす | 案5は両方の縮小を避ける |
| **決定 (ユーザー 2026-10-17): Unicode の組表を像に入れない** | T3d0 の試作の実測: 表 (28,252 B) は LZ4 でほとんど縮まず、通常像 501,998→530,350 B (余裕 −10,158 B)、R1 像 503,320→531,719 B (−11,527 B) で 508KiB を超えた (本番の mkvmkernel.py が拒否。撤去予定の読込・lease は未撤去で控除 0 B、`~/os32-tmp/run/t3d0/report.md`)。案は (a) 表をファイルのままカーネルが持つ、(b) 表を詰めて像に入れる (11KB 以上縮むかは未測定)、(b') 508KiB の上限を上げる (ブート層の変更) | **(a) を採る**: 表は `/sys/unicode.bin` のまま、起動時にカーネル所有のページ (低位ではない) へ読み、カーネル内の二分探索で引く。§3 の他の目的 (KAPI `unicode_to_jis`、表ポインタを公開しない、Unicode lease・低位の直読・ready/4 点照合・setter の撤去、全 BMP の一致) はそのまま。像の増分はほぼ 0。RAM は新しい 7 頁 (28KiB) を取り、旧低位 128KiB は T7a まで FIXED のまま残るので、T7a まで +28KiB (2MB 構成で足りるかは T3d2 で見る)。T3d1 はこの形で行い (契約の補遺は `T3D1_UNICODE_FILE`)、§3 の「kernel `.rodata`」はこの行が上書きする。T3d0 の器具 (`tools/tests/unicode_lookup_*`) は探索の照合に使う |

## 7. T4 への引渡し (T3d2、2026-10-18、NP21/W 64MB)

実測は `~/os32-tmp/evidence/2026-10-18/t3d2_np21w/RESULT.md`。peak は今回の操作の範囲での最大で最悪値ではない。

| 項目 | 実測 (KDEBUG=1 / 0) | 上限 | 余裕・備考 |
|---|---|---|---|
| vmkernel.lz4 (圧縮) | 502,027 B | 520,192 B (`boot/boot_defs.h` の MAX_IMAGE_SIZE) | 18,165 B。T4 で SQLite が部品として外へ出れば縮む見込み [推] |
| `__bss_end` / `__kernel_resident_end` | 0x198000 / 0x276000 | カーネル帯 0x100000〜0x3E0000 | — |
| `[mem] pages` (64MB = 16,384 頁) | fixed 991・slack 243 (KDEBUG=0 は 255)・pool 14,997・meta 18・bb 107・kdebug 12 (0)・other 16、diff 0 | — | Unicode の表 7 頁は slack/pool の内数 |
| SLACK の空き | slack_cycle free=236 busy=19 (KDEBUG=1) | 255 頁 | KDEBUG=0 は未測定 (slack_cycle は kdebug の中) |
| KHEAP | peak 3,784 B | 196,608 B | 192,824 B |
| kstack | 4,348 / 3,648 B | 16,384 B | 12,036 B |
| shell stack | 最大 20,840 / 17,672 B | 40,960 B | 20,120 B |
| 常駐 shell の sbrk / exec_heap | 1,032 B / peak 131,656 B | 294,428 B / 262,144 B | 293,396 B / 130,488 B |
| KAPI / 配置の世代 | v76 / memory_layout 4 | — | T3d1 で v76 (`unicode_to_jis`) |

未測定: KDEBUG=0 の SLACK の空き、GUI アプリを多数起動したときの最大、8MB・17MB・Ra266 の構成別 (台帳 H-4・T4T7-RH)、D15 の判定 (台帳 T3D2-D15)。時間の参考値 (エミュレータ): 起動からシェルの待受けまで tick 591〜595 (KDEBUG=1) / 544〜558 (0)、`time cat` で 42.8KB の日本語文書 0.22〜0.23 秒、CUI の FEP の入力→表示は測定の細かさ (約 100ms) 未満。
T3 の途中で見つけて直したカーネル層の不具合: EXECNEST-1 (CPL3 の親から子を繰り返すと esp0 が戻らない、v2.1 から潜在)、FDWP-1 (書込み禁止の FD から起動できない)。
