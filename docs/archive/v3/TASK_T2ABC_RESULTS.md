# T2a〜T2c — 実装・受入の記録

> 状態: **完了記録 (2026-10-01)** — 着地した段の当時の記録 (未実施・残件の記述を含む)。2026-10-02 に [TASK_T2_APPBAND §5-1](../../tasks/v3/TASK_T2_APPBAND.md#5-1-単独着地の単位) から切り出した。節番号は元票のもの。T2 全体の設計・受入条件・未確認台帳は元票に残る。

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

**T2b 着地と PM の NP21/W 受入 (2026-10-01)**: 独立実装レビュー Codex `gpt-6-astra` は P1 なし・P2 2 件・P3 1 件で Request changes → コーダーが対応 (`b7fe58e`) → 確認レビューは体制の変更 (Fable 枯渇、ROLES §0) により Opus 5.5 サブエージェントが差分だけを見て Approve (3 件とも閉じた、変異 10/10 実行時 RED、TLB の 2 変異は順序違反そのもので rc=2)。`c06df8d` で main に入れ、NHD へ配備 (停止 → nhd-pull → deploy-kernel → deploy → 起動)。

| 構成 | 見たもの | 結果 |
|---|---|---|
| 17MB (`ver` Commit `c06df8d`) | 起動 | `kselftest_fail`=0、**`lease_selftest_result`=0** |
| 17MB | faulttest gp/de/ud/pf、loop・kloop + CTRL+STOP、`v86 -t` | T2a / T2a′ と同じ (kill 6・回収 7、`ledger_*_ops`=0、深さ 0、取り残し 0、`irq_ctx_violations`=1 のまま、V86 OK) |
| 17MB GUI (PEGC 480) | `os32gui` → Run... gui_demo → ESC → Start → CUI mode | 窓 2 枚が描かれ (画面で確認)、CUI へ戻る (回収 8、取り残し 0) |
| 8MB (`ram-8mb`、終了後 `restore`) | 上の CUI 一式と GUI | 17MB と同じ |

公開 caller は未切替なので、新しい lease 窓の実使用は T2c 以降。未実施は T2a / T2a′ と同じ (park → resume 後、R1 panic の故障ゲスト、Ra266 64MB)。

#### T2c-R. 調査・停止履歴と実装記録 (2026-10-01、`wt/t2c`、GPT-6 / Codex)

**PM の決定 (2026-10-01、再開指示)**: §4-6の共通正典は未作成でP7票も未発行。T2cで `sdk/kapi.json` にOS32X形式版・KAPI ABI世代・メモリ配置世代・shlibプロトコルを独立した4欄として新設し、既存KAPI生成器を拡張してC/SDK/Python/Rustへ生成する。番号を各道具へ手書きしない。P7も同じ正典を使用し、T2cではスロット整理をしない。[ABI1]〜[ABI3]に従い版上げとclean→allを行う。前回の正典不在による停止理由はこの決定で解消。

**PM の決定 2 (2026-10-01、再開指示)**: T2c〜T2e の暫定 heap は旧計算の byte 数と上限をそのまま保持する。既定1/最大2 PDE・上端0x00C00000、`ring3_band_set` の旧物理上端切詰め、`exec_sbrk_pick_tier` の二段選択を物理専用計算に残す。64MBで旧上限を超える明示heapを断るのは既存挙動。上限撤去・map allocator・最小初期量はT2fで一括。以後、暫定段の曖昧さは **PM判断待ちでなく、挙動維持の解釈で実装** し、高位VAを物理helperへ渡さない。

以下の「未実装」「停止」は再開前の履歴。今回の実装・検証は本節末尾の結果記録を参照。

**未実装**。基点 `59c4285` で §4・§5-1・§6・§7-1、T2a/T2a′/T2b-R と PM 受入、上位 D35 を確認した。形式・配置・shlib・CRT は同時更新、旧低位共有 USER は T2d まで維持、heap は T2f で一括、AS 制御ブロックの固定 KHEAP 化は T2c という境界を維持する。コード・ABI・生成物を変更せず、コミット/配備も行っていない。

**実装時の訂正 (参照先の確認)**: §4-6 は「具体的な世代値は P7 と共通の正典から生成」と指定しているが、この基点の追跡対象と `.claude/skills/` を調査した範囲では、KAPI ABI 世代・メモリ配置世代・shlib プロトコルの値/生成元の定義を確認できなかった。`sdk/kapi.json` は機能版 `version=68`、`sdk/os32x_hdr.py` と公開ヘッダは OS32X v3 と `kapi_data_off` の定義を持ち、D35 の3世代欄はまだ無い。`V3_PLAN` P7 と `FORK_PLAN` は D35 を参照するが、共通生成元の所在/値を定めていない。これは設計の決定を変更する訂正ではなく、現在の実物に参照先が見つからないという調査事実である。

共通正典を T2c で新設する意図なのか、別の P7 成果物を参照する意図なのかを確定できていない。ユーザーの「設計の解釈に迷ったら止めて報告」に従い、番号と生成元を推測して ABI を切り替える前に停止した。再開に必要なのは共通正典の場所と3世代値、または T2c でそれらを新設する指示。状態行・D35・T2c の受入条件は変更していない。

**変更前測定** (`CROSS_DIR=/home/hight/opt/cross make all < /dev/null`、rc=0、ログ `/tmp/t2c-before-all.log`; `readelf -SW` / `nm -n` / `gen_memmap.py --headroom`):

| 観測 | 今回の変更前 | 停止時 (コード変更なし) |
|---|---:|---:|
| `.text` 開始 / サイズ | 0x100000 / 326,462B | 同左 |
| `.data` 開始 / サイズ | 0x14FB40 / 32,971B | 同左 |
| `.bss` 開始 / サイズ | 0x157C20 / 212,040B | 同左 |
| `__bss_end` | 0x18B868 | 同左 |
| ASSERT 0x195000 まで | 38,808B | 同左 |
| `.got.plt` 末尾 → `.bss` の余白 | 8B | 同左 |
| 圧縮 `vmkernel.lz4` | 477,781B | 同左 |

T2b-R 最終記録と比べ、今回の `.data` は4B小さく、BSS前余白は4B大きく、圧縮画像は7B小さい。原因は断定しない。text/BSS/ASSERT残りは一致し、今回の測定を高位配置の実装後測定とは扱わない。既定の NP21/W 向け FD コピー2件は失敗警告のみ。外部 apps/game は空であり、外部アプリの再ビルド・新世代への移行・監査は未実施。

**PM 向け**: 今回の成果物は T2b までの旧配置で、T2c のゲスト受入には使わない。T2c 実装後の一式と新しい map/nm で、8/17MB と Ra26664MB の §5-1 T2c (CUI/GUI/入れ子/park/fault、256/512KB stack、旧CPL0/shell/shlib拒否) を確認する。高位 entry は0x80100000〜、shlib は0x80000000〜、stack 上端0x90000000、master高位APP PDEは空を確認する。今回の旧配置 ELF の観測番地は `kselftest_fail=0x162E60` / `kselftest_pass=0x162E64` / `lease_selftest_result=0x18B864` / `exec_as_leftover_pages=0x18B860` / `ledger_irq_ops=0x186D14` / `ledger_exc_ops=0x1864E0`。T2c 実装後には必ず引き直す。NP21/W/NHD/配備/ini/Windows/実機には触っていない。

**検証と rc (現基点の確認であり、T2c 実装の合格ではない)**: 初回 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は rc=2 (`/tmp/t2c-doc-check-changed.log`)。sandbox の32bit Linux syscall制限により既存 vmkernel-lz4/vk32-crc/HDD-stage1/stage2 のホスト実行が SIGSYS (exit -31) になった。T2b で使った既存の実行足場を適用し、`PYTHONPATH=/tmp/t2ap-python CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は **rc=0** (`/tmp/t2c-check-changed-qemu.log`)。既定基点が HEAD~1 になり、前のコミットの build 入力変更を含めて全検査を変異込みで選択した。ソース不変検査も成功。既存 T2b lease は10/10実行時RED・コンパイル失敗0。既存配置境界2件のコンパイル拒否および構文破壊2件はNOT COUNTED、実行時REDへ数えない。T2c固有のホスト/変異試験は未実装・未実施。ビルド後の `python3 tools/gen_memmap.py --check` / `--headroom` と `git diff --check` はrc=0。地図とTESTS一覧は最新で、生成ブロックの変更は不要だった。最終結果はソース不変検査の完了後に本票だけへ追記した。

**再開調査・解釈確認のため停止 (2026-10-01)**: PMの正典新設指示は上記に記録し、前回の世代正典不在は解消済み。今回の停止はそれとは別で、§5-1末尾の暫定heap予算の解釈確認である。同節は「T1の起動時予算計算を物理専用の暫定helperとして残し、旧配置のcode/stack/guardを引いた容量から得たbyte数だけを新しい仮想予約へ写す」と指定する。実物の `paging_app_band_pdes` は `MEM_APP_BAND_MAX_PDES=2` で枚数を打ち切り、`ring3_band_set` はその上端と `sys_usable_mem_end()` の小さい方を使う。旧容量上限も暫定helper内に保持するのか、現在の池の空きで容量を算出し直すのかで、64MB機の大きな明示heap要求の起動可否が変わる。前者は旧上端0x00C00000による拒否を継続し、後者は起動時の容量方針も変更する。新しい仮想配置64 PDEと疎PTそのものの制限とは区別する。**PMへこの2択を確認中**。指定の「解釈に迷ったら止めて報告」に従い、どちらも実装せず停止した。これは設計の変更・矛盾の断定ではない。

**実装時の訂正 (実物の調査事実)**: 暫定heap helperは単なるRAM上端の減算ではなく、旧アプリ帯の既定1 PDE/最大2 PDEによる容量制限と、物理空きによるsbrk二段選択を含む (`paging_app_band_pdes` / `ring3_band_set` / `exec_sbrk_pick_tier`)。高位VAを物理計算へ入れてはいけない点は確定している。今回コード・KAPI・SDK・CRT・生成物は未変更、T2cは未実装であり、成果物はT2bまでの旧配置。状態行・D番号・受入条件は不変。

**今回の測定と検証**: `CROSS_DIR=/home/hight/opt/cross NP21W_DIR=/tmp/t2c-images make all < /dev/null` はrc=0 (`/tmp/t2c-baseline-all.log`、画像コピー失敗2件は警告のみ)。測定は `/tmp/t2c-baseline-sections.txt` / `/tmp/t2c-baseline-nm.txt`。前後は同値で、text開始0x100000/326,462B、data開始0x14FB40/32,971B、bss開始0x157C20/212,040B、`__bss_end=0x18B868`、ASSERT 0x195000まで38,808B、got.plt末尾→bss余白8B、圧縮477,781B。最初にルートの `kernel.elf` を指定したreadelf/nmはrc=1、正しい `build/out/kernel.elf` で測定し直してrc=0。`python3 tools/gen_memmap.py --check` / `--headroom` と `git diff --check` はrc=0。地図生成ブロックとTESTSは最新なので書換え不要。

`PYTHONPATH=/tmp/t2ap-python CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` はrc=0 (`/tmp/t2c-restart-check.log`)。文書のみの選択10検査、docs statusの13/13試験と14/14実行時RED (Python変異でコンパイル失敗をREDへ数えない)、package検査が通った。32bit実行には既存qemu-i386足場を使用。これは既存基点/文書の確認で、T2c固有の変異・起動失敗・高位AS・旧.o混入試験は未実装/未実施。ABI未変更のためclean→allは未実施。apps/gameは空で外部アプリ移行/再ビルド/監査は未実施。

**PMのゲスト確認は実装後へ**: 今回の旧配置をT2c受入に使わない。実装後の一式と新mapで8/17MBおよびRa26664MBのCUI/GUI/入れ子/park→resume/fault/STOP、256/512KB stack、旧CPL0/shell/shlib/未知版/旧.o混在の入口前拒否を確認する。予定番地はshlib0x80000000、exec0x80100000、stack上端0x90000000、master高位APP PDEは空。今回の既存ELFの診断番地は `kselftest_fail=0x162E60`、`kselftest_pass=0x162E64`、`lease_selftest_result=0x18B864`、`exec_as_leftover_pages=0x18B860`、`ledger_irq_ops=0x186D14`、`ledger_exc_ops=0x1864E0`。実装/PM再ビルド後に必ず引き直す。コミット/push/配備/NP21/W/NHD/ini/Windows/実機は未操作。

**実装結果 (2026-10-01、同じworktree、未コミット)**:

- `sdk/kapi.json` をKAPI **69**へ。`generations` に OS32X形式 **4**・KAPI ABI **1**・メモリ配置 **1**・shlibプロトコル **1** の独立欄を新設。slot追加/並替えなし。既存生成器からC/Python/Rust/NASM・リンカ世代参照を生成する。OS32X v4は60B、完全な読込・形式/サイズ/世代/KAPI配置の完全一致・既知flag・image種別ごとのload/entryを入口前に検査する。常駐shellも検査し、不一致の停止案内を有効にする。`--cpl0` / FORCE_CPL0 / `cpl0_probe` の実行入口を廃止。
- app/shlibのVAを **0x80100000 / 0x80000000**、heap予約を **0x88000000**、stack上端を **0x90000000**へ。masterの高位APP PDEは空。ASはPD+lease先頭PTのみから始め、触るAPP PDEのPTを疎に確保し、準備失敗を巻き戻す。既存の低位共有USER・VRAM/SHM/BB/フォント/トランポリンはT2eまでの暫定契約を保つ。物理計算は `MEM_POOL_BASE` / `MEM_PHYS_EXEC_FLOOR` / `MEM_PHYS_WORKSPACE_FLOOR` と旧予算定数に分離。
- `stack_size=0` は256KiB。明示値はページ切上げ・最低16KiB、signed/丸めoverflow・予約超過・argvフレーム不足を拒否。実際のstack_base/sizeをAppSlotに控え、budget/map/guard/argv/終了・fault・kill・park/resumeで使う。image/BSS/argvはmaster下で実ページを翻訳して書き込む。親のcmdlineも保存した親PDのbackingから読む。新しいコマンド長上限は設けない。
- shlib原本はSHLIB ownerの非連続の実ページ。公開前に外/内ヘッダ、プロトコル、text/data境界と全entryを検査。ASのtextはRO、dataは原本から私有複製し、途中失敗で返す。masterに高位shlib aliasを置かない。Rust stubにはLTO後にも残る依存symbolを持たせ、包装器が依存なし0と依存ありを区別する。常駐shellのshlib依存は拒否し、静的リンクを維持。
- AS制御をAppSlot埋込みから固定KHEAP確保へ。target **addrspace=688B (≤1376B)**、**AppSlot=192B**、全6slot+通常4AS+台帳/SURFACEの計 **7,488B (≤16,384B)**。4AS制御のallocator header込み実占有 **2,784B**をホストで測定し、終了時0へ戻る。これは他のKHEAP顧客とのゲスト総ピークの代用ではない。
- C全単位 (kernel/SQLite/SDK/userland)、asm CRT/kernel、Rust出力へnote/refまたはcrate/member hashを強制付与。リンクの実選択 `.o` / archive memberを検査し、未使用memberを区別する。RustはcrateをLTO前に照合。新SDK+旧.o/旧値混在を拒否し、包装時に最終ELF・raw・検証済みリンク入力hashを再照合する。newlib/libgccの正確なvendorパスを台帳へ記録。`make all` が **build/out/generations-manifest.json** にkernel/loader/SDK/CRT/libs/shell/shlib/全in-tree (sh.binとtests/*.binを含む) の一組のID・4世代・hashを列挙する。apps/gameは空として明記し、完全な外部成果物セットとはしない。外部は新しいSDKのforced include/link guard/Rust wrapperへ追従し、clean-externalを明示して再ビルドする必要がある。

**実装時の訂正 (事実のみ)**: 旧heap helperには1/2 PDE上限と物理空きによる二段選択が実在するため、PM決定2どおり旧byte予算として残した。生成地図は従来VA/物理を恒等とみなして一律に重なりを比較していたので、高位APP VAと物理RAM/デバイスの数値一致を衝突と数えないよう分離した。kernel.mapは世代検査wrapperの一時mapで置換してはならず、呼出元指定のmap出力を保存する。旧 `make clean` はgshell Rust targetを消していなかったためclean-programsに含めた。D番号・状態行・T2d以降の設計を変更していない。

**カーネル実測** (同じ構成、kernel ELFのreadelf/nm、生成器のheadroom。SQLite/非ロード世代noteは本体3節に含めない):

| 観測 | 変更前 (T2b) | T2c |
|---|---:|---:|
| `.text` 開始 / サイズ | 0x100000 / 326,462B | 0x100000 / 325,998B |
| `.data` 開始 / サイズ | 0x14FB40 / 32,971B | 0x14F980 / 31,943B |
| `.bss` 開始 / サイズ | 0x157C20 / 212,040B | 0x157660 / 210,348B |
| `__bss_end` | 0x18B868 | 0x18AC0C |
| ASSERT 0x195000までの残り | 38,808B | 41,972B |
| `.got.plt`末尾→`.bss`手前 | 8B | 12B |
| VK32圧縮一式 | 477,781B | 477,105B |

余白12BとASSERT残り41,972Bは加算しない。固定PD/PT起点0x3F1000、SQLite/DMA境界とASSERTは変更していない。

**検証記録 (最終全体検査は下へ追記)**: `CROSS_DIR=/home/hight/opt/cross make clean < /dev/null` → `make all < /dev/null` はrc=0 (最終cleanのログ `/tmp/t2c-clean13.log`、all `/tmp/t2c-all13.log`、最新all `/tmp/t2c-all16.log`)。画像コピー失敗警告のみ許容し、NP21W_DIRは変更していない。`make external` は対象外、外部アプリ移行の実証なし。既存qemu-i386足場 `PYTHONPATH=/tmp/t2ap-python` とtoolchain PATHでホスト32bitを実行する。関連 `make check-memory-host` rc=0 (`/tmp/t2c-memory3.log`)、高位ASの4/4、BB/可変stack/argv/返却の11/11、shlib断片化・失敗巻戻しの4/4が実行時RED、これらのコンパイル失敗0。既存lease10/10、R1移譲・回収も再実行済み。全体check-changed初回はrc=2 (`/tmp/t2c-changed1.log`)、旧claim/shlib予約試験とC89検査変異の当て先が旧形だったため修正。メモリ地図/TESTSはビルド後に生成器で更新した。

**PM の未実施ゲスト受入**: 本セッションは配備/NP21/W/NHD/ini/Windows/実機/commit/pushを操作していない。PMが停止中にmanifestの一組を揃え、NP21/W 8/17MB とRa266 64MBで CUI/GUI・入れ子exec・park→resume・通常終了/#PF/#GP/STOP/WM kill、256/512KiB stack、pool不足と固定KHEAP不足からの起動拒否・次アプリ再起動を確認する。`owner pages=0`、`exec_as_leftover_pages` / `ledger_bad_free` / IRQ・例外中allocator操作の差分0、KHEAP総ピークと返却を確認。旧app/shell/shlib/未知形式/世代不一致はentry前拒否し、`exec_entry_calls`の差分0 (旧shellは最初の入口0) を見る。64MBの旧上限超え明示heapは拒否継続が正しい。HostDrv/NHDのhash食違い、v2 SDK製apps/gameもPMの外部再構築後に確認する。

現在のELFの診断番地: `kselftest_fail=0x162C60` / `kselftest_pass=0x162C64` / `lease_selftest_result=0x18AC08` / `exec_as_leftover_pages=0x18AC00` / `exec_entry_calls=0x18AC04` / `ledger_bad_free=0x1866F8` / `ledger_irq_ops=0x1860B4` / `ledger_exc_ops=0x185880` / `kmalloc_peak_bytes=0x15C3C8` / `exec_sbrk_tier_last=0x17CFD0`。master PD実体 **0x3F1000** のPDE512〜575 (`0x3F1800`〜`0x3F18FC`) は0、高位entry **0x80100000**、shlib **0x80000000**、stack上端 **0x90000000**、stack guardは要求サイズ直下。`g_pages=0x15BE00` は実PFN原本の控え、masterの高位aliasではない。PMの再ビルド後はnmで引き直す。


**最終全体検査 (T2c、2026-10-01)**: 最終 `PATH=/home/hight/opt/cross/bin:$PATH PYTHONPATH=/tmp/t2ap-python CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は **rc=0** (`/tmp/t2c-changed3.log`)。全109検査を変異込みで選択し、最後のソース不変検査も成功。高位AS4/4、BB/可変stack/argv/回収11/11、shlib4/4、形式/世代/旧単位混入14/14はコンパイル成功後の実行時RED、コンパイル失敗0。既存lease10/10も成功。既存配置境界・構文破壊のコンパイル拒否はNOT COUNTEDで、実行時REDへ数えていない。二回目 (`/tmp/t2c-changed2.log`) のrc=2は、配布manifest試験の正常fixtureが旧48B/v3のまま、およびkstr_benchの要求API版68と現行69の不一致。正常fixtureを生成定数による現行60Bへ追従し、kstr_benchを69へ更新した。最新 `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` はrc=0 (`/tmp/t2c-all17.log`)、上のELF測定値は不変。ビルド後に `python3 tools/gen_memmap.py --write`、試験一覧を `python3 tools/gen_tests_inventory.py --write` で更新済み。実装・生成物を固定して全検査を完了し、その後は本結果の文書追記だけを行った。状態行は変更していない。ゲスト/実機/外部アプリの未実施項目は上記のとおり。

**レビュー 1 回目の対応 (T2c-R、2026-10-01、GPT-6 / Codex、基点 `efb40d6`、未コミット)**:

- P2-1: 段選びと `appslot_start_admit` の `need_pages` を `exec_ring3_pages` へ統一。実際の stack_size、本体/heap/stack の疎 PT、PD と lease先頭PT、shlib dataを数える。PM決定2の暫定heap byte予算 (旧1/2 PDE上限・既定stackを引いた容量・折半) は維持。8MB・512KiB stackは段1が835枚 (旧768枚)、67枚の不足範囲とちょうど835枚の境界を回帰試験し、不足範囲で段2の実admitが通ることを確認。固定256KiB stackとPTの3枚過小計上を戻す変異はコンパイル成功後の実行時RED。
- P2-2: `ring3_guard` A/B の番地を memmap の `MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE` / `MEM_SHLIB_BASE` に統一し、fault addrと `ring3_hello` 注記を更新。PMは **A: addr=0x8FFBF000**、**B: addr=0x80000000** を照合する。Aは当該ASのPTE非present (CPL3 writeのPF errorは6)、Bはshlibロード済みの当該ASでPTE present/USER/RO (PF errorは7) を確認する。シリアルのBは `[shlib band, WRITE]`。それぞれ `fault_kill_count` +1、`GRD?` / `SLB?`、`SURV`なし、カーネル生存。未ロードでのBのkillはRO保護の合格へ数えない。512KiB要求のアプリのstack guardは **0x8FF7F000** (この既定stack試験binary自体は256KiB)。ゲストの確認はPMへ、今回は未実施。
- P2-3: link_guard のホームパス既定を撤去。ldに渡した `-L` と `-l` の順序から解決したnewlib/libgccの正確なarchive pathだけをvendorとして記録し、ld mapの実選択入力と照合する。config/SDK例はCROSS_DIRをexport。実際のlibc.a/libgcc.aを別の一時ディレクトリへコピーし、CROSS_DIR環境変数なしで実memberを選択したリンクとvendor証跡の完全一致をホスト試験 (KAPI layout計58件) で確認。
- P3: deploy.yamlの重複キーを撤去。load/種別/entry・range/未ロードshlib/常駐shellのshlib依存拒否に理由別1行の案内を追加し、形式・KAPI配置・ABI世代・memory世代・shlib protocol・要求API版の案内を分けた。読み取りwalkはPDE/PTEのPRESENT|USERを要求しROも許可、argvの翻訳失敗を終端から区別して起動拒否。早期pointer検証は実stack_baseからguardを除外。死んだCPL0判定2関数とFORCE_CPL0定義/別名、旧成功を期待したホストケースを撤去 (旧flag 0x0004の拒否試験は維持)。指定された配置/60Bヘッダ/v69注記を更新。RAM上端とheap/stackのPDEがAPP帯内というSTATIC_ASSERTを追加。常駐shellはローダでもshlib_protocol=0を要求。

**T2c-R の大きさ** (基点T2c → レビュー対応後、同じ構成):

| 観測 | 対応前 | 対応後 |
|---|---:|---:|
| `.text` 開始 / サイズ | 0x100000 / 325,998B | 0x100000 / 327,070B |
| `.data` 開始 / サイズ | 0x14F980 / 31,943B | 0x14FDA0 / 32,347B |
| `.bss` 開始 / サイズ | 0x157660 / 210,348B | 0x157C20 / 210,348B |
| `__bss_end` | 0x18AC0C | 0x18B1CC |
| ASSERT 0x195000までの残り | 41,972B | 40,500B |
| `.got.plt`末尾→`.bss`手前 | 12B | 24B |
| VK32圧縮一式 | 477,105B | 477,998B |

AppSlot / addrspace の構造体は変更なし (192B / 688B)。診断の最新nm: `kselftest_fail=0x163220` / `kselftest_pass=0x163224` / `lease_selftest_result=0x18B1C8` / `exec_as_leftover_pages=0x18B1C0` / `exec_entry_calls=0x18B1C4` / `ledger_bad_free=0x186CB8` / `ledger_irq_ops=0x186674` / `ledger_exc_ops=0x185E40` / `kmalloc_peak_bytes=0x15C988` / `exec_sbrk_tier_last=0x17D590`。PMの再ビルド後に引き直す。

**T2c-R 再検証**: `CROSS_DIR=/home/hight/opt/cross make clean < /dev/null` → 同環境で `make all < /dev/null` は最終いずれもrc=0 (`/tmp/t2cr-clean4.log` / `/tmp/t2cr-all4.log`)。初回の足場なし `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` はrc=2 (`/tmp/t2cr-changed1.log`)、既存ILP32ホスト試験のSIGSYS (exit -31)。前回と同じ `PYTHONPATH=/tmp/t2ap-python` のqemu-i386足場で再検証する。二回目は既存ring3ホスト変異試験の全木コピーがRustの `target/` まで変異ごとに累積保持して/tmpを枯渇させ、ring3/HDD2/VK32試験が停止 (`/tmp/t2cr-changed2.log`)。同試験を変異ごとに一時木を破棄・target除外へ修正し、単独rc=0、17/17 RED (`/tmp/t2cr-ring3-host.log`) を確認。既存の失敗を合格へ数えず、全体を再実行する。clang AST版のp2vは違反0、例外63で不変 (例外追加なし)。追加のホスト回帰を含むapp/BB/argv/guard/shell試験は15/15実行時RED・コンパイル失敗0 (`/tmp/t2cr-bb4.log`)、sbrk勘定の逆戻し変異もコンパイル成功後の実行時RED (`/tmp/t2cr-sbrk-wired.log`、`--mutate`)。状態行・D番号・暫定heap方針は変更なし。commit/push/NP21/W/NHD/配備/ini/実機/外部アプリは未操作。

sbrkの逆戻し変異は `build/sdk.mk` の `$(MUT)` / `--mutate` へ接続し、check-fastでは走らないことも確認 (`/tmp/t2cr-sbrk-fast.log`、rc=0)。予備試験では新署名・旧段1期待値・RO fixtureの写像方法・定数名の追随で失敗を修正し、コンパイル落ちはREDへ数えなかった。二回目の失敗確定後、残る長時間の検査器試験を中断 (rc=130)。三回目は製品/回帰/構文木検査に失敗なし (`/tmp/t2cr-changed3.log`)、C方言の通常82件も通ったが、sbrkのcheck-fast変異配線を修正するため中断 (rc=130)。最終の配線で全体を再実行する。

**T2c-R 最終全体検査**: `PYTHONPATH=/tmp/t2ap-python CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は **rc=0** (`/tmp/t2cr-changed4.log`)。mainとのmerge-base `8612b06ebc87` を基点に全109検査を変異込みで実行し、対応表の漏れ0。clang AST版のp2v/C方言/LE access/arch asmは違反0、例外一覧の追加なし。C方言検査器は通常82件失敗0、変異27/27 RED・対照5/5 GREEN。新しいsbrk境界/RO argv/可変stack guard/shell拒否の回帰と実行時RED変異、CROSS_DIRなしの別配置vendor実リンクも成功。最終のclean→allも上記clean4/all4でrc=0、測定サイズは表のまま。実装・試験を固定して全体検査を完了し、その後は本結果の文書追記のみ。足場なしのrc=2を成功扱いせず、qemu-i386はILP32ホスト試験の実行にだけ使用した。状態行は不変、NP21/W・実機の受入は未実施。


**T2c-R 追補 — PM の受入で見つかった kselftest の失敗と対応 (2026-10-01、基点 `11e1c9d`)**:

PM の NP21/W 17MB 起動 (API v69) は post-exec の `ledger:AS alloc` だけ失敗し、pass=258 / fail=1、ledger の irq/exc/check_fail/bad_free は全て0。既存 `test_app_band_pde.py` の ILP32 足場へ実 `kselftest.c` の `ledger_persist_total` / `test_ledger` 本文を取り込み、実 paging/pgalloc/physmem と合成8/17MBで再現した。`paging_addrspace_create` は **rc=0**、owner pagesは **3** (PD=1、lease先頭PT=0、高位PT=0、データ=2)。修正前は同じ `ledger:AS alloc` が実行時RED。CR3/owner/KHEAP/admitの拒否ではない: この自己診断はmaster下の通常文脈・有効AS ownerでPDだけを生成し、AS制御はstack上に置き、KHEAP確保とexec admitを呼ばない。

§4・§5-1 T2c/T2c-R・§6とT2b-Rの契約を照合し、**旧期待値を修正、ページング実装は変更しない**。T2cの高位PTは写像時の疎確保、lease先頭PTは起動用 `paging_addrspace_create_lease` の事前確保で、plain `create` には無い。期待枚数は `PDE_COUNT * sizeof(u32) / PAGE_SIZE + data_pages` とし、PD/lease/高位PTの内訳をコメントへ記録。データ枚数は確保・回収の検査でも共通の試験定数を使う。

kselftest.cのAS/paging/exec検査と呼出先を見直し、他の実行時期待値の追随漏れは見つからなかった。`test_app_band_pde` と呼出先 `paging_app_band_selftest` の旧identity/eager PT説明、`paging.h` のAS生成契約の説明は高位/疎PTへ更新。PD共有/USER・cache隔離/高位stackの写像回収/memmap/trampoline/exec状態検査は現契約を維持する。試験はplain ASと起動用lease ASを区別し、後者のPD+lease先頭PT=2枚も確認、destroy/reclaim後owner0・永続owner不変・pool復元を検査する。変更時選択へkselftest.cとその入力headerを登録。

回帰試験の旧式 `(PDE_COUNT + PTE_COUNT) * sizeof(u32) / PAGE_SIZE + data_pages` (=4) への変異はコンパイル成功後の **実行時RED**。既存4変異と合わせ **5/5 runtime RED、コンパイル失敗0**。最初の直接ILP32実行は環境のSIGSYSで失敗し、既存と同じqemu-i386補助を `/home/hight/os32-tmp/t2cks/python/sitecustomize.py` に置いて実行。初期変異案の未使用変数によるコンパイル拒否はREDへ数えず、旧計算式を戻す変異に訂正した。一時診断を製品ソースへ追加していない。ログ/一時ファイルは `/home/hight/os32-tmp/t2cks/`。NP21/W/NHD/配備/ini/実機/commit/pushは未操作、ゲスト再受入はPMへ。

**T2c-R kselftest 追補の最終検証**: `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` は **rc=0** (`/home/hight/os32-tmp/t2cks/all-final.log`)、`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 make check-changed < /dev/null` は **rc=0** (`/home/hight/os32-tmp/t2cks/check-changed.log`)。共通環境は `TMPDIR=/home/hight/os32-tmp`、ILP32実行補助の `PYTHONPATH=/home/hight/os32-tmp/t2cks/python`、FD自動コピーを防ぐ存在しない `NP21W_DIR=/home/hight/os32-tmp/t2cks/no-deploy`。FDコピー2件は警告/失敗で、配備していない。check-changedは既定基点HEAD~1 (`31870465ce18`)から全109検査を変異込みで選択。追加回帰5/5 runtime RED、compile failures 0、C方言検査器27/27 RED・対照5/5 GREEN。`python3 tools/gen_memmap.py --check` / `--headroom`、`python3 tools/check_select.py --lint`、`git diff --check` もrc=0。TESTS生成結果は既存と同じ。検査中はソースを変更せず、終了後はこの結果の文書追記のみ。ゲスト再受入は未実施。

**T2c 着地と PM の NP21/W 受入 (2026-10-01)**: 独立実装レビュー Opus 5.5 は P1 なし・P2 3 件で Request changes → 対応 (`1e1712e`) → 同じレビュアーが差分で Approve。main を取り込み (`efb40d6`)、`11e1c9d` で着地、[ABI3] どおり `make clean` → `make all` → NHD へ一式配備 (deploy-kernel は全成果物を同期 — 新しい常駐シェルごと入れ替わる)。**受入で kselftest 1 件の失敗** (`ledger:AS alloc`、期待値 4 が T2b までの式のまま — 素の `paging_addrspace_create` は T2c で PD 1 枚だけ、lease 先頭 PT はアプリ起動の `create_lease()` だけが持つ) を発見 → コーダーが期待値を設計の定数から導く形に直し (`6aacf43`)、再配備。

| 構成 | 見たもの | 結果 |
|---|---|---|
| 17MB (`ver` API v69、Commit `6aacf43`) | 起動 | `kselftest_fail`=0、`lease_selftest_result`=0、取り残し 0 |
| 17MB | faulttest gp/de/ud/pf、loop・kloop + CTRL+STOP、`v86 -t` | 例外の EIP は **0x801xxxxx (高位帯)**、kill 6・回収 7、`ledger_*_ops`=0、深さ 0、取り残し 0、V86 OK |
| 17MB | `ring3_guard` A〜E | A: `#PF addr=0x8FFBF000` (既定 256KB スタックのガード) で kill、B: `addr=0x80000000 [shlib band, WRITE]` で kill (CUI では shlib 未ロードなので RO = err 7 の確認にはなっていない)、C: 0x01000000・D: 0x00F00000 (表示面) で kill、E: 生き残り `BB??SURV` (正解) |
| 17MB GUI (PEGC 480) | Run... gui_demo (libos32gui を 0x80000000 へ) → ESC → CUI mode | 窓 2 枚が描かれ (画面で確認)、CUI へ戻る |
| 8MB (`ram-8mb`、終了後 `restore`) | 上の CUI 一式、ring3_guard A・B・E、GUI | 17MB と同じ。`exec_sbrk_tier_last`=1 |

**未実施**: 512KB スタックの実アプリ (ヘッダの stack_size を指定したバイナリが無い — ホストの境界試験だけ)、旧形式 (旧 shell / 旧 shlib / 未知版 / 新 SDK + 旧 .o) の入口前拒否のゲスト確認 (`exec_entry_calls` を使う) — T2h の統合受入でまとめて、Ra266 64MB、T2a からの未実施 (park → resume 後、R1 panic の故障ゲスト)。NHD の ext2 にエラーの印 (`[EXT2] warning: mounting fs with errors`) が T2b の起動から出ている — T2 の変更とは別件として調べる。

