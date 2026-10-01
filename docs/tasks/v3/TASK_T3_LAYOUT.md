# TASK_T3_LAYOUT — T3 カーネル帯・常駐シェルの詳細設計

> 状態: **設計中 (2026-10-01)** — 実装用の設計。独立レビュー Opus 5.5 (`claude-opus-5-5`) 往復3でApprove。実装・受入は未実施。
> 設計: Codex gpt-6-astra。調査基点 `59c4285bbacf36e829e7480d741bbabac95be191`。
> 決定の正典は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) D1〜D36、配置の定義は `include/memmap.h`、実装済み地図は [02_memory](../../02_memory.md)。本票は実装順・追加契約・試験だけを持つ。

## 1. 着手条件と成果物

T3 は [T2](TASK_T2_APPBAND.md) の **T2c〜h が統合・受入済み**になってから実装する。調査基点で着地しているのは T2a・T2a′・T2b までであり、高位アプリ、lease、世代検査、可変 map はまだ前提を満たさない。T2 のゲスト未確認項目を T3 の成功で消さない。

T2a′ の固定 PD/PT 10 ページは既に画像外・最終位置にある。再移設せず、周囲の旧 shell heap を解放し DMA・ガード・kstack に替える。旧 T3 行の「PT を画像の外へ」は残作業ではない。後続は [T4〜T6b](TASK_T4_T6_MODULES.md)、[T7 と後半](TASK_T7_AND_FOLLOWUPS.md)。

| 成果物 | 責務 |
|---|---|
| 一組の新配置成果物 | kernel、resident shell/gshell、SDK、shlib、同梱ユーザーランドを同じ配置世代で再ビルド。旧 shell は入口前に拒否 |
| 配置 manifest | ELF シンボルから code/data/bss・暫定 SQLite・浮動鎖・SLACK・shell の半開区間を生成。手書きの実測値を増やさない |
| 起動診断 | 固定占有、SLACK、一般池、メタデータ、BB のページ数を別々に記録。推測予算を合格根拠にしない |

## 2. 配置の計算と起動契約

### 2-1. SQLite を含む暫定の鎖

`build/os32.ld` は現在 SQLite を 0x200000 へ飛ばし、`__bss_end` と `__sqlite_end` を別々に扱う。T3 ではコアの BSS の後をページ整列して SQLite の text/rodata/data/bss を連結し、**SQLite の全 BSS と暫定代替スタック 128KiB を含む占有終端**から KHEAP → KAPI → guard → SHM → guard を計算する。

設計シンボル `__kernel_resident_end` をこの占有終端に設ける。`__bss_end` はゼロ化対象の終端であり、鎖の起点へ無条件に流用しない。SQLite の BSS は独立範囲として `kentry.asm` でゼロ化し、NOLOAD の隙間をファイル長と混同しない。T4 で SQLite が外れたら `__kernel_resident_end` はコア BSS の整列終端になる。KHEAP 192KiB・KAPI・SHM のサイズは既決値を維持する。

暫定代替スタックは下端に1ページのNP guardを置き、guardも占有終端と予算へ含める。guard用ページは全BSSゼロ化対象から除外し、PG有効化後にNPへする。暫定代替スタックは linker symbol で表し、現行 `sqlite_stack.asm` の引数へ渡す。T4 の可変ポインタ化までの移行橋であり、固定 SQLite 帯を別の番地に再作成しない。`build/kernel.mk` の別圧縮 SQLite エントリは T3 中は維持してよいが、宛先は ELF の `__sqlite_start` から導出し `--sqlite-addr 0x200000` を撤去する。全エントリのロード先・BSS・スタックを含む非重複検査を追加する。T4 でこの暫定エントリを廃止する。

SLACK は `[align_up(chain_end, PAGE_SIZE), MEM_DMA_POOL_BASE)`。終端を切り上げたページを重複登録しない。空区間は合法、逆転はリンク時エラー。KERNEL_SLACK は通常 RAM と同じ owner 割当を行い、固定 kernel owner からの無条件 reclaim で生み出さない。

### 2-2. 固定領域とページング

最終番地の表は上位票 §2-1 を参照し、値は `memmap.h` だけで更新する。`MEM_POOL_BASE` は shell 新帯の直後へ移り、台帳の L0/固定用途、メタデータ置場、workspace、DMA、BB 探索下限、起動最小 RAM 判定も同じ差分で追随させる。旧 SQLite 予約と旧 shell heap 予約の削除漏れ・二重供給を検査する。

順序は PG=0 で固定 PD/PT をゼロ化・構築 → 予約/NP 範囲を適用 → 固定 PD/PT 自身と kstack を supervisor/RW で確認・必要なら再写像 → CR3 → PG=1。固定 PT を旧 shell 帯の未使用領域として NP にする処理を残さない。PDE 1 の shell の PTE は全 AS 共通 supervisor。APP_BAND の私有 PT と同じ番号だという旧仮定は T2 で消えていることを再監査する。

**FIXED 型の台帳 backing は core 常駐BSSの末尾へ移す。** 2ページ (metadata1 + workspace1) をページ整列した専用NOLOAD sectionとして `__bss_end` の内側に置き、`__ledger_fixed_start/end` をexportする。旧DMA末尾/stack guard隣接の導出式は廃止し、`MEM_LEDGER_META_BASE/END` はそのlinker symbol参照へ替える。従来の番地STATIC_ASSERTは、サイズ/枚数のSTATIC_ASSERTと、整列・core内包含・他section非重複のlink ASSERTへ分ける。`memory_boot.c` の固定区間表はcore占有をこの2ページの前後に分割し、metadata用途との二重登録を避ける。旧 `[0x2F9000,0x2FB000)` は新地図の他の占有が無ければ通常のSLACKとして扱う。

この2ページは **paging_init時点で全機種ともcore BSSとして恒等sup/RW/WBでpresent** にする。旧 `boot_range_valid(..., 0)` のNP期待は廃止し、present・USERなし・物理一致の期待へ替える。FIXED機の `paging_map_ledger_backing()` はその既存写像を検査する専用入口として残し、NPからpresentへ切替える責務は持たせない。ARENA_TOP機では動的backingを使い、core内の予備2ページは未使用のままkernel owner/sup RWで保持し、途中でSLACKへ返さない。`memory_boot_fixed[]` のsymbol由来区間 (core分割/metadata/SLACK) は実行時に配列を構築し、linker symbolを割った値をstatic const PFN初期化子へ入れない。`tools/gen_memmap.py` は固定数値の正規表現からELF symbolを読む経路へ替え、`test_memory_boot.py` の定数変異と `memmap_boot_host.c` / `memory_boot_host.c` の旧NP期待も同じT3aで変更する。

全機種でcore常駐が8KiB (別途ELF整列差) 増えてSLACKが同量減ることを予算へ記録する。新しい固定番地の穴をDMA直前へ作らず、Dの帯を変えない。

SHM後方guardとDMA手前のSLACKは区別し、旧「後方予約をまとめてNP」にする処理を撤去する。SLACK全域はPG開始時から恒等sup/RW/WB、guardだけNP。

リンク ASSERT は、鎖終端≤DMA、DMA の64KiB整列とサイズ、DMA/guard/PT/guard/stack の非重複、固定 PT の 1/8/1 枚、shell 帯と池の隣接、画像に固定 PT を含まないことを検査する。SLACK と shell の恒等写像を T2 の master/AS 検査へ追加する。ISR の stack 切替、TSS.esp0、初期 ESP は同じ kstack シンボルを使う。

### 2-3. シェル 1 箱・2 ヒープ

`app_sys.ld` は shell 画像を新帯起点へ置き、末尾 `_end` を出力する。帯上端から 40KiB stack とその下の guard を引いた地点を heap ceiling とする。画像/BSS の上に sbrk と exec_heap の共通供給領域を置く。

**T3では固定分割を採用する。** D8の「API2・供給元1」は、同じshell専用帯の残りを起動時の一つの境界計算で分配する意味とし、実行中の両heap間の貸し借りは行わない。`shell_heap_claim/release_tail` 案は採用しない。heap ceiling直下の **256KiBをexec_heap**、その下の画像終端からexec_heap開始までをsbrkへ固定する。`MEM_SHELL_EXEC_HEAP_SIZE` を `memmap.h` に定義し、共通初期化が `{image_end, sbrk_limit=exec_base, exec_base, exec_end=heap_ceiling}` を一度だけ作る。sbrkの最低余裕128KiBをリンク/起動検査し、足りなければそのshell画像を拒否する。実行中にexec_baseを下げないのでCRTの現在breakをkernelが知る必要はない。

T2fのCPL=3用 `_sbrk` (EXACT mem_map) と **常駐shell/gshell用CRTの `_sbrk` をビルドで分ける**。`build/programs.mk` でapp_sysリンク対象にだけresident CRTを渡し、resident版は固定 `sbrk_heap_limit` を使い、範囲/符号/overflowを検査して自身のbreakを進める。ユーザー入力のフラグでresident経路を選べない。exec_heapは上の固定256KiB内で現行allocatorを初期化する。shellヘッダのheap_szは0なら256KiB、0超256KiB以下なら固定exec_baseから下詰めでその長さを使い、余りをsbrkへ渡さない。256KiB超はヘッダ不適合として起動前に拒否する。公開後のブロック移動・再分割はしない。子終了は `exec_heap_restore_state()`、shell heapの再初期化は禁止。

この256KiBはT3用の暫定設計値としてT3cでshell/gshellと子ネストのpeakを実測し、T7bでfont込みの再ゲートを通す。全体のSLACK再計測にはT7のpage0 snapshot増分 (+2,560Bの設計値とELFページ丸め、[T7詳細](TASK_T7_AND_FOLLOWUPS.md)) も含め、現時点の実測値とはしない。T7bの未来の実測をT3完了の前提にはしない。足りなければ同じ1MiB内の固定分割定数を変更・全再ビルドして再受入し、実行中の境界移動で救済しない。

## 3. Unicode の前倒しと ABI

T3 で Unicode 組表を kernel `.rodata` と二分探索へ移す。既存 `lib/unicode_jis_table.h` の並び/重複/端点をホスト検査し、Unicode→JIS の失敗値は現行 `utf8` 契約を維持する。ユーザー側の変換は **KAPI `unicode_to_jis` を選ぶ設計案**とし、`lib/utf8.c` の kernel/user ビルドを分ける。アプリが kernel の表ポインタを取得する API は設けない。

T2 の暫定 Unicode RO lease は全同梱 caller が KAPI へ移行してから撤去する。apps/game は現行ビルド対象外のため変更要件を引き渡し、再ビルド再開時の受入ゲートとする。`unicode.bin` 読込・ready/4点照合・暫定RO leaseは同一差分で撤去し、leaseだけ残して未初期化領域を読ませない。

`kcg.c` のLZ4 scratchはT7aまで旧低位FIXED領域に保持し、専用定数 `MEM_KCG_LZ4_TEMP_BASE/END` (従来の0x4A000〜MEM_CONV_END、344KiB) でUnicodeから名前を分離する。poolの追加確保・常駐予算追加はしない。ただしこの範囲はplanar BBとmailboxを重ねるため、**旧kcg_load_fontの実行はboot時のBB公開/host操作/V86開始より前に限定**する。bootフェーズを閉じた後の旧KAPI呼出しはNOSYSで副作用なし (D7の意味変更)。実行中の再ロードを温存してGUIやmailboxを上書きしない。scratchが有効なboot期間は通常AS/USER leaseがゼロ。boot期間終了でscratchを失効させ、重なるplanar BBは全消去・初期化してからT2のSURFACE/leaseとして公開する。公開後の低位BB leaseはscratchの漏出とは数えず、kcgからの上書きを禁止する。mailboxもhost操作開始前に初期化する。T7aまではV86の現行636KiB backingによる扱いを維持し、低位を直接渡さない。T7aで旧scratch/BB用途を撤去してV86へ専有を移す。低位の占有を解いて V86 へ専有を引き渡すのは T7a (一般池へは返さない)。旧 `kcg_load_font` の意味変更と同じ差分で `userland/tests/font_load_test.c` を **期待NOSYSのPASS試験 (BB/mailbox不変はhost読取またはkselftestで別に照合し、未貸与mailboxをCPL=3試験が直読しない)** へ変更し、`tools/tests/guest_tests.txt` と `tools/tests/test_result_conv_host.c` の成功経路期待も更新する。SKIPで回帰を隠さない。`sdk/kapi.json` の説明はboot専用内部読込/公開口NOSYSを明記し、T3のUnicode追加と一組でKAPI版を上げる (追加とは別に二重加算しない)。生成・clean再ビルドは [KAPI_SPEC §3-1](../../KAPI_SPEC.md) と [ABI1]〜[ABI3]。

KAPI 追加は [ABI1]〜[ABI3]、番号予約は [KAPI_SPEC §3-2](../../KAPI_SPEC.md) に従い、D35 の世代を勝手に別定義しない。

## 4. 実装分割と受入

| 段 | 主な変更ファイル | 閉じる試験 |
|---|---|---|
| T3a 配置式 | `include/memmap.h`, `build/os32.ld`, `tools/gen_memmap.py`, `kernel/kentry.asm`, `boot/boot_defs.h`, `build/kernel.mk` | ELF の BSS込み区間、鎖終端とDMA境界の±1ページ変異、画像外PT、暫定SQLiteエントリの実宛先 |
| T3b 起動/台帳 | `kernel/paging.c`, `kernel/memory_boot.c`, `kernel/physmem.c`, `kernel/pgalloc.c`, `kernel/dma_pool.c`, `kernel/kselftest.c` | SLACK全ページを一度だけ配り返せる、固定/guardを配らない、PG直後とAS生成後の地図 |
| T3c shell | `sdk/link/app_sys.ld`, `exec/exec.c`, `exec/exec_heap.c`, `sdk/crt/syscalls.c`, shell/gshell build | 2 heap交差失敗の原状維持、子実行前後の親データ、stack guard、旧shell拒否 |
| T3d Unicode/統合 | `lib/utf8.c`, `kernel/kernel.c`, `drivers/kcg.c`, `sdk/kapi.json` と生成器、ユーザー側caller | JIS既知対・未収録・境界、全再ビルド、GUI/CUI日本語、低位直読残存ゼロ |

T3aでは `boot/loader_fat_new.asm` のVK32_LOAD_END写しと `tools/mkvmkernel.py` も対象とし、core展開上端をMEM_DMA_POOL_BASEまで広げる。`tools/gen_memmap.py` の写し照合とC/ASMの共通fixtureで確認する。T3cの直書き監査には `userland/lib/rt/dbgserial.c`、`tools/memmap_audit_live.py`、gen_memmapの旧帯表示、gshellの旧番地コメントも含める。T3a/T3dは圧縮 `vmkernel.lz4 <= 508KiB` を必須ゲートとし、超過はT6bを黙って前倒しせず段取りを再レビューする。Unicode KAPI追加で1字2トラップになる経路はcold/warm日本語描画の時間を測り、D15未達なら文字列変換口の別設計へ送る。

**T3aとT3bは一体の差分で着地**し、symbol化・runtime区間・pagingの新期待を分割してビルド不能な段を作らない。T3a〜d はレビュー可能な差分単位で、**配置の半分だけを配備しない**。同一世代の統合成果物を 8MB planar/PEGC、17MB、Ra266 64MB で検証する。kselftest・地図検査ゼロ、CPL=3 fault/STOP→次起動、DMA境界、shell子ネスト、2MB試験3種、GUI終了後CUIを確認する。T3 時点は未分離 SQLite のため最終余裕値とは区別し、実ページ・SLACK・shell peak・kstack high-water を記録する。

失敗時は旧新を混ぜず直前の一式へ戻す。配備・故障注入はテスター、NHD/実機操作は [D1]〜[D3] と既存手順に従う。文書段階ではビルド・実行結果を作らない。

## 5. 独立レビュー対応 (往復1)

レビュアー `claude-opus-5-5` (Opus 5.5)、2026-10-01、判定Request changes。実装検証ではなく文書/コード照合。修正後の確認は未了。

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
| N1 caller/NOSYS | §3にfont_load_test・guest台本・host期待値・kapi.json説明の同時変更、T3のKAPI版更新を明記 |
| N2 backing写像順 | §2-2で全機種ともPG開始時present、FIXED専用入口は既存写像検査へ。旧NP期待をhost試験でも変更 |
| N3 実装注記 | symbol由来区間のruntime構築、gen_memmap/変異のELF対応、heap_szの下詰めと上限を明記 |
| 自己点検 scratch寿命 | 低位BBと重なるscratchはboot中のみ有効、失効→BB全初期化→lease公開の順。公開後のBB leaseを禁止しない |

往復3: 同じ `claude-opus-5-5` が **Approve**。N1/N2/N3とscratch寿命はclosed。最終非blocker R1〜R3はT3a/b一体着地、T7aまでの現行V86 backing維持、BB/mailbox観測をhost/kselftestへ分離する注記で対応。PM方針に従いこの文言追記の追加レビューは行わない。実装・各測定ゲートは未実施。
