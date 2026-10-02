# T2d (B1 checked copy) の実装結果と受入 — d0a〜d6

> 状態: **完了記録 (2026-10-02)** — T2d (d0a〜d6) は main `3fafa7c` / `531baf4` で完了。 本文は [TASK_T2D_T2H](../../tasks/v3/TASK_T2D_T2H.md) の §10-2〜§10-16 を 2026-10-02 にそのまま移したもの (節番号は元の票のまま。票の中の「§10-x」はこのファイルを指す)。設計と T2e 以降は元の票が正典。

## 10-2. d0a の結果 (PM、2026-10-01、NP21/W 17MB、main `e641e9c`)

`/usr/bin/d0a_test.bin` (CPL=3、`hsync` で配備、10,560 バイト):

```
d0a: cpl=3 buffer_va=0x80102940
d0a: parent_buffer=MISSING
d0a: child_value=CHANGED
d0a: child_status kind=1 code=10 rc=10 result_rc=0 bytes=17
```

**指摘の再現を確認した**: 子の出力 17 バイトは親の登録バッファに届かず、子自身の同じ VA (0x80102940) を書き換えた (子は自己点検で code=10)。ホストの実ソース試験 (`tools/tests/test_fd_redirect_d0a.py`) も同じく RED (XFAIL として登録)。カーネル層の既知の不具合として、**d0b の修正を T2d の新機能より先に行う** (POLICY_DEV §1)。

## 10-3. d0b の実装結果 (コーダー、2026-10-01)

`fs/fd_redirect` に登録時の `{origin, app_id, AS, pd_phys, owner, generation}` を値保存する
`RedirAccess` を追加。`exec/redir_access.c` は live AppSlot を引き直してから AS の同一性を
照合し、登録 PD の read/write walk が返す PA を `P2V` でコピーする。現在 CR3 との一致は
登録時だけ要求する。登録者の終了・世代/owner/PD/AS 不一致・RO 出力は -1 で拒否し、
子を kill しない。len/capacity/位置/番地の桁あふれも拒否する。

全範囲を先に検査して通常の拒否ではデータ・位置を不変とし、コピー時にもページごとに
「生存確認→walk→copy→位置更新」を IRQ 保存内で行う。ページの間で入口 IF を戻す。
この間に allocation/callback/yield はなく、AS 回収は R1 の通常文脈だけ。ページ途中で
防御的な再検査が失敗した場合は、既にコピーしたバイト数を返す。AS generation は生成時に
単調採番し、上限到達後は生成拒否して周回させない。trusted の kernel/WM buffer は従来の
恒等写像を使う。syscall caller 保存の一般化や管理 frame walk の強化 (d1〜d3) は今回の対象外。

保存構造体に記述子を含めるので、exec_run の継承・子 owner だけの回収、および appslot の
park/resume の値保存で登録者を保持する。ホストは実物 fd_redirect + redir_access、同VA/別backing
で親出力・子全byte不変・登録者PDからの読取りを確認。死んだ登録者、同slot/AS/owner/PDの
世代再利用、AS/owner/PD不一致、RO出力拒否/RO入力許可、入れ子とparkの保存復元、
次ページ拒否時の全byte不変、非整列のページ跨ぎ、IF=0/1、容量とoverflowも確認した。
実物 appslot の既存試験には記述子全フィールドの park/resume 保持検査を追加した。
`test_fd_redirect_d0a.py` の XFAIL 受理と make の `--expect-known-bug` 登録は撤去した。

対象試験: `test_fd_redirect_d0a.py --mutate` は GREEN + **16/16 実行時RED**、
`test_paging_bounds.py --mutate` は実物 AS の再利用・周回拒否が GREEN + **2/2 実行時RED**。
どちらも compile 失敗は RED に数えない。既存 ring3_guard / fstat_redir / owner_reclaim /
multiapp_impl の対象試験も成功。kselftest に AS 世代の再利用拒否と登録者なしの早期拒否を反映。
この環境では ILP32 native 実行が SIGSYS になるため、ディスク上の一時 `sitecustomize.py` で
ホスト ELF32 のみ `qemu-i386` を経由した (NP21/W ではない)。

| ILP32 実測 | 修正前 | 修正後 |
|---|---:|---:|
| AS | 688 B | 692 B |
| AppSlot | 192 B | 192 B |
| FdRedirect / State (3本) | 32 / 96 B | 52 / 156 B |
| redirect現在表 + 6退避枠 | 672 B | 1,092 B |
| 管理合計 (親票のAS/slot/台帳会計、redirect別) | 7,488 B | 7,504 B |
| kernel.bin | 359,424 B | 360,596 B |
| 本体 `.text/.data/.bss` (`__bss_end - 0x100000`) | 569,772 B | 571,404 B |
| `__bss_end` | `0x18B1AC` | `0x18B80C` |
| リンカ ASSERT 残り (596 KiB枠) | 40,532 B | 38,900 B |

修正後は未コミットの `-dirty` build_id を含む。生成地図は
`python3 tools/gen_memmap.py --write` で同期した。
`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp make all < /dev/null` は rc=0。
make all の FD image 出力先は `NP21W_DIR=/home/hight/os32-tmp/d0b-image-output` に設定し、
NP21/W・NHD・ini・実環境への配備・commit/push は実施していない。
最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は **rc=2**。原因は `docs/TESTS.md` の生成忘れ
(`check-tests-inventory` だけが失敗)。`python3 tools/gen_tests_inventory.py --write` で
修正し、同鮮度検査は rc=0。そこで未実行になった `check-constraints` 以降の選択済み
ターゲットと `check-tests-inventory` を `make -k ... MUTATE=1 < /dev/null` で続行し、
**rc=0**。両回のソース不変検査も rc=0。本体の `check-changed` は依頼の1回指定に従い
再実行していないため、**同コマンド rc=0 の完了条件は未確認としてPMへ申し送る**。
環境は上記 TMPDIR、cross/bin の PATH、ELF32 用一時ランナーの PYTHONPATH を使用。
ログは `/home/hight/os32-tmp/d0b-check-changed.log` と `d0b-remaining-checks.log`。
既存 Windows opt-in fixture は単独試験4件・集約試験5件が skip、ゲスト受入は未実施。
初回 `make all` の既定 FD コピーは Warning で失敗し、以降は上記の一時出力先に隔離した。

PM の NP21/W 受入は未実施。新しい `d0a_test.bin` の期待結果は
`d0a: parent_buffer=OK`、`d0a: child_value=OK`、
`d0a: child_status kind=1 code=0 rc=0 result_rc=0 bytes=17`。
CPL=3 の sh 内で `ls | cat` を単独 `ls` と比較し、8MB/17MBの回帰とkill差分を確認する。

### レビュー (Opus) の P3 の対応 (2026-10-01)

基点 `26bf6da`、worktree `wt/t2d0b-p3`。独立レビュー Opus 5.5 Approve の P3 6件を対応した。

1. ホストに ABORT_PENDING、登録者 cpl3=0、登録時CR3不一致、TRUSTED (user_call=0、pa=va) を追加。
   最初の3条件を消す変異を登録し、既存PAコピー変異を kmemcpy の形へ更新した。
2. kselftest の名称・注記を「登録者なしの早期拒否」「CPL0 slot の USER capture 拒否」に合わせ、
   origin は REDIR_USER / REDIR_TRUSTED にした。CPL=3 の d0a_test は RO+USER の KAPI
   トランポリン表への登録を専用子 `--ro-child` で試す。KAPI の出力guardが先にwalkして
   fault終了するため、これは **登録者PDの redir_access walk のゲスト試験ではない**。
   そのwalkのRO拒否は実物 redir_access のホスト試験で確認する。
3. TRUSTED は桁あふれを避けた減算比較で `va + len <= MEM_APP_BAND_BASE` を要求。
   PAは `V2P((const void *)(uptr)va)`。上限ちょうどの成功、帯外・跨ぎ・overflowの拒否と変異を追加。
4. IRQ保存内のページ単位コピーを lib/kstring.h の kmemcpy に変更。ページ間のIF復元は維持。
5. ring3_str.h / exec.h の旧 user_origin・現在CR3 walk の注記を RedirAccess / redir_access へ更新。
6. カーネルシンボル `redir_refuse_count` (volatile u32、KAPIなし) を追加。
   fd_redirect_write のバッファ書込みが位置/容量整合性やアクセス検査で断られるたびに1加算し、
   ページ再検査で途中終了した場合も1加算する。容量による通常の短い書込み、読取り、登録拒否は数えない。
   ホストは拒否1回ごとの増分を確認し、カウンタ加算を撤去した変異も実行時RED。

対象ホストは GREEN、変異は **21/21 実行時RED** (compile失敗はREDに数えない)。
ring3_guard、fstat_redir、owner_reclaim、multiapp_impl の対象回帰も rc=0。
ILP32ホスト試験は既存の一時 sitecustomize.py で ELF32 のみ qemu-i386 経由。
ゲスト未実施 (依頼でNP21/W・NHD・配備・iniは禁止)。新 d0a_test の期待出力は
`d0a: ro_registration=OK`、`d0a: parent_buffer=OK`、`d0a: child_value=OK`、
`d0a: child_status kind=1 code=0 rc=0 result_rc=0 bytes=17`、プログラムrc=0。
RO子だけは kind=2 / code=-2 / rc=-2 が期待値で、fault_kill_count の増分 **+1** は意図した拒否。
PMは新kernel.mapの redir_refuse_count と kselftest を読み、通常の親子出力では拒否増分0を確認する。
従来の `ls | cat` 比較と8MB/17MB回帰も受入時に行う。

同一toolchainで基点を `make kernel` (rc=0) してから変更後を測定した。
基点はclean build_id、変更後は `-dirty` を含む (従来票のd0b計測もdirtyなので基点kernel.binは8B小さい)。

| 大きさ | 基点 26bf6da | P3対応後 | 差分 |
|---|---:|---:|---:|
| kernel.bin | 360,588 B | 360,696 B | +108 B |
| vmkernel.lz4 (SQLite含むVK32) | 478,777 B | 478,899 B | +122 B |
| 本体占有 (`__bss_end - 0x100000`) | 571,404 B | 571,504 B | +100 B |
| `__bss_end` | `0x18B80C` | `0x18B870` | +100 B |
| リンカ ASSERT 残り (596 KiB枠) | 38,900 B | 38,800 B | -100 B |
| AS / AppSlot / FdRedirect / State | 692 / 192 / 52 / 156 B | 同左 | 0 B |
| 診断カウンタ | 0 B | 4 B | +4 B |

新kernel.mapの redir_refuse_count は `0x18B860` (4B)。アドレスは今回の成果物の値で、
受入時は配備した成果物と対応するmapを使う。圧縮上限520,192Bまで41,293B。
初回の対象変異実行は、kmemcpy化で同じ宣言が2か所になった変異アンカー検査で停止 (rc=1)。
コピー側のwhileを含む一意なアンカーに直して再実行し21/21実行時RED (rc=0)。
生成地図は `python3 tools/gen_memmap.py --write` (rc=0) でdirty成果物に同期した。
`python3 tools/gen_tests_inventory.py --write` もrc=0 (内容変更なし)。

`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp
NP21W_DIR=/home/hight/os32-tmp/d0bp3-image-output make all < /dev/null` は **rc=0**。
FDコピー先を一時ディレクトリに限定し、NP21/W・NHD・配備・ini・commit/push は未実施。
既存の GNU-stack / RWX リンク警告は出たがビルドは成功。d0a_test.bin は10,848B。
ログ: `/home/hight/os32-tmp/d0bp3-all.log`。
最終ゲートは `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` (PATH=cross/bin、PYTHONPATH=一時ELF32ランナー) を1回だけ実行する。
既定の基点選択がHEAD~1に戻り build/kernel.mk / build/sdk.mk を含むため、今回はfullを選択する。
最終 `make check-changed` は **rc=2**。kmemcpy用の新しい入力 `lib/kstring.h` を
`tools/check_map.yaml` の check-fd-redirect-d0a-host 欄へ足し忘れたため、check-map と
check-check-select-host の case_lint_real が失敗した。これはコーダーの登録漏れ。
終了後に同欄へ1行追加し、
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-map check-check-select-host MUTATE=0 < /dev/null` は **rc=0** (25/25 PASS)。
修正前のゲート実行中にソースを変えていないことは `check_tree_unchanged.py --verify chg1` で確認した。
ログ: `/home/hight/os32-tmp/d0bp3-check-changed.log`、`d0bp3-map-fix.log`。
依頼の「最後に1回」に従い check-changed 本体は再実行していない。
したがって **同コマンドrc=0の完了条件は未達**。PM側で修正後の最終ゲートを確認する必要がある。
check-mapの後続にある kapi_layout / edit_doc / fstat_redir / kstring_c / kstr_bench /
sh_status / hsync_h3 / hsync_h2 / h4_manifest / vfs_excl / fs_kind_callers / cat_linenum /
result_conv / guest / fd_redirect_d0a / cirrus_win / pegc_mode のゲート内実行は未実施。
fstat_redir と fd_redirect_d0a は前述の対象単独試験では成功している。
Windows opt-in fixture は単独試験4件・集約試験5件がskip。
C方言は変異27/27 RED・対照5/5 GREEN、P2Vは12/12実行時RED (compile失敗0) まで成功。
修正後の生成票更新と `git diff --check` もrc=0。

## 10-4. d0b のゲスト受入 (PM、2026-10-01、NP21/W 17MB、main `26bf6da`)

独立レビュー Opus 5.5 は P1・P2 なしで Approve (P3 6 件は wt/t2d0b-p3 で対応中)。NHD へ配備 (停止 → nhd-pull → deploy-kernel → deploy → 起動)。

```
d0a: parent_buffer=OK
d0a: child_value=OK
d0a: child_status kind=1 code=0 rc=0 result_rc=0 bytes=17
```

**修正を確認した** (d0a の MISSING / CHANGED が OK に)。kselftest 0 fail、faulttest 一式・V86・GUI (gui_demo → CUI) の回帰も従来どおり (取り残し 0、深さ 0)。未実施: CPL=3 の sh での `ls | cat` (rshell から入れ子の sh を操作できない既知の制約)、8MB、Ra266。

## 10-5. d1 の実装結果 (コーダー、2026-10-02)

基点 `babca79` (d0b P3 対応着地)、worktree `wt/t2d1`。状態行・親票 §4-7・
TASK_MEMMAP_V3 の決定は変更していない。

`include/redir_access.h` の既存 24B 記述子を `struct caller_access` として共用し、
`RedirAccess` は同じ型の別名とした。origin は USER/TRUSTED の内部列挙。
`exec/redir_access.c` の capture と live AppSlot→AS 同一性検査を共用し、
登録済み buffer 用の別実装・別 generation 台帳は作っていない。
現在 caller は値の `CallerAccessFrame` (記述子+valid、28B) を保持し、
入口で前の値をローカルへ退避、正常出口で戻す。stack へのリンクは保持しない。
`caller_access_get` は入口の記述子を取り直さず、USER の slot/owner/AS/generation/PD
生存と現在 CR3 一致を再確認して値を返す。拒否時 out は不変で、trusted への fallback はない。
TRUSTED は kernel 内部の明示した `caller_access_enter(..., CALLER_TRUSTED)` のみ。

実 `ring3_syscall_dispatch` は callback より前に USER を capture し、不一致なら
既存の `ring3_fault_kill` へ渡して wrapper 進入を拒否する。正常出口は caller と
従来の frame を復元し、`ring3_in_syscall` も入口値へ戻して入れ子を保つ。
IRQ 保存・復元以外に CR3 を操作しない。KAPI/SDK の公開 ABI・形式は変更なし。

**d2 へ渡す穴**: park/kill/exit の longjmp と launch/resume 着地点での無効化、
WM enter/leave の TRUSTED scope と保存 USER の明示利用、実 exec R1 足場での
古い記述子不使用は未実装。値保持なので捨てられた stack の dangling pointer は
作らないが、非局所出口後の古い値の失効を保証したものではない。
既存 redirect は d0b の `ring3_call_from_user` による登録時 capture を継続する。
copy/出力 guard/DB はまだ保存 caller を消費しない。これらを d2 の寿命保証前に
新 caller へ切り替えない。WM 深さの配線は d2、walk 強化は d3、copy は d4〜d5、
小さな boot 自己診断の確定は d6 に残す。これは §0 の分割に従い、未配線の間は
既存 consumer の挙動を保つ解釈である。

ホスト `test_caller_access.py` は実 dispatcher 本文を抽出して、実 redir_access と
同じ TU で実行する (AppSlot/CR3/IRQ/KAPI invoke は足場)。USER→USER、USER→TRUSTED、
TRUSTED→USER の正常復帰、入口 CR3/owner 不一致、使用時 CR3/slot/owner 不一致、
同フィールドの別 AS、generation/PD/owner 変更、dead/pending/CPL0、origin 不正、
拒否時 out 不変、IF=0/1 と CR3 不変を検査。master CR3 でも USER は拒否する。
19/19 変異が **コンパイル成功後の実行時 RED**。初回は fixture の KAPI 型・定数で
compile 失敗 (RED に算入せず)、AS 同一性変異は最初 SURVIVED だったため
同フィールド別 AS のケースを足し、再実行で RED にした。
d0b の実 redirect 回帰は GREEN + **21/21 実行時 RED**。
両試験とも変異対象の C と fixture だけを写し、ヘッダは元の木から参照する。
変異ごとのツリー複製・全木走査は行わない。
ring3_guard (既存変異14/14を含む)、実 exec R1、実 AppSlot の回帰も成功。
ILP32 fixture のみ既存一時 sitecustomize.py で qemu-i386 を経由した。

| ILP32 実測 | 基点 (clean build_id) | d1 後 (-dirty) | 差分 |
|---|---:|---:|---:|
| AS / AppSlot | 692 / 192 B | 同左 | 0 B |
| RedirAccess / FdRedirect / State | 24 / 52 / 156 B | 同左 | 0 B |
| caller 現在値 / 各入口の退避値 | 0 / 0 B | 28 / 28 B | BSS +28 B、各 dispatch stack +28 B |
| kernel.bin | 360,688 B | 361,048 B | +360 B |
| vmkernel.lz4 (SQLite 含む) | 478,893 B | 479,125 B | +232 B |
| 本体占有 (`__bss_end - 0x100000`) | 571,504 B | 571,888 B | +384 B |
| `__bss_end` | `0x18B870` | `0x18B9F0` | +384 B |
| リンカ ASSERT 残り (596 KiB 枠) | 38,800 B | 38,416 B | -384 B |

圧縮上限まで 41,067B。§6-1 の T2c-R 基準 40,500B からの消費は 2,084B、
d の計画枠 3,072B の残りは 988B (d2〜d6 が収まると保証しない。超過時は再見積り)。
入口にはこのほか guard 退避の int 1 個を追加。stack の数値は C の保存値サイズであり、
compiler の spill/整列込み最大 stack 使用量の測定ではない。

実行環境: `CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、
PATH に cross/bin、ILP32 用 `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner`。
make はすべて `< /dev/null`。`make kernel` (変更前) は rc=0。
`NP21W_DIR=/home/hight/os32-tmp/d1-image-output make all` は **rc=0**。
FD のコピー先は一時出力先に限定。既存 GNU-stack/RWX 警告あり。
`OS32_MUT_JOBS=4 python3 tools/tests/test_caller_access.py --mutate` と
`test_fd_redirect_d0a.py --mutate` はともに rc=0。
対象 make の初回は存在しない `check-exec-r1-host` を指定し rc=2。
`python3 tools/tests/test_exec_r1.py` と `test_multiapp_impl.py` を直接実行して rc=0、
ring3_guard は同 make 内で成功。`gen_memmap.py --write`、`gen_tests_inventory.py --write`
で生成物を同期し rc=0。
最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は **1回実行して rc=0** (上記 PATH/PYTHONPATH)。
新 Make 規則の `.PHONY` 行が選択実行の許可形式に合わず、安全側の full
(全検査・全変異) が選ばれた。C 方言27/27 RED・対照5/5 GREEN、d1 19/19 と
redir 21/21 実行時 RED を含め成功し、最後のソース不変検査も成功。
ログは `/home/hight/os32-tmp/d1-check-changed.log`。
既存 Windows opt-in fixture は単独4件・集約5件が skip。
ゲート終了後の変更はこの結果の票への追記だけで、コード・試験は変更していない。
ログは `/home/hight/os32-tmp/d1-all.log`、`d1-targets.log`、`d1-r1.log`、`d1-multiapp.log`。
NP21/W・NHD・配備・ini・commit/push は未実施。guest/実機は未検証。

## 10-6. d1 の着地とゲスト受入 (PM、2026-10-02、NP21/W 17MB、main `be303d4`)

独立レビュー Opus 5.5 は P1・P2 なしで Approve (網羅性の要求つき、経路の一覧あり)。入口の kill の新設で正当な CPL=3 の syscall が断られる到達可能な筋書きは無し、d2 へ回した穴は今の段で誤動作しない。**P3 5 件は d2 へ申し送る**: P3-1 `exec.c:1551` の入れ子保存は正常復帰のみで子の CPL=3 実行中に `ring3_in_syscall=1` が残る (意図を票に明記、d2 で sys_exit・kill・着地点に入れ子の復元)、P3-2 入れ子試験 (`caller_access_host.c:51-68`) の形が実物と違う (d2 の R1 足場で実物の形を固定)、P3-3 入口の拒否の専用カウンタ (例 `ring3_caller_reject_count`) を足してゲスト回帰で 0 を確認、P3-4 `redir_access.c:21,23` の `generation != 0` / `pd_phys != 0` を外す変異が生き残る (d0b から持越し — 0 の登録者を拒否するケースを足す)、P3-5 毎回の syscall のコスト増 (ゲスト回帰で体感・`gfx_counters` を見る)。

ゲスト (17MB、今の ini — §12): kselftest 0 fail、`klibc_test` 49/49、`alloc_demo` 16/16、`ring3_fault` kill、`ls / | wc -l` = 54、`echo abc | wc -c` = 4、`d0a_test` 全行 OK、faulttest 一式・V86・GUI (gui_demo → CUI) 従来どおり。kill 8 件はすべて意図したもの (d0a の RO 子 1・ring3_fault 1・faulttest 6) で、入口の誤拒否の形跡なし。構成依存の確認は §12 のとおり T2h へ。

## 10-7. d2 の実装結果 (コーダー、2026-10-02)

モデル: GPT-6。基点 `1a75fc3`、worktree `wt/t2d2`。状態行・親票・
TASK_MEMMAP_V3 は変更しない。公開 ABI / 形式 / エラー / park 条件は従来どおり。

- launch / resume の setjmp 前に caller 値・`g_cur_frame`・`ring3_in_syscall`・
  WM 深さを `Ring3CallContext` へ値保存する。戻り先 stack は生存するので、
  longjmp の両着地点で子の文脈を無効化→pending 回収→親の文脈を復元する。
  snapshot は setjmp 後に書き換えず、独自 setjmp が returns_twice 宣言を持たない
  ため volatile にして compiler の stack slot 再利用も防ぐ。
- sys_exit / 通常 kill は共通 `exec_exit`、IRQ/例外 kill と fault recover は
  `exec_pending_transfer` で回収・移譲より先に無効化する。OP_WAIT / kbd / poll /
  sys_yield の4 park もフレーム保存後、master 切替より先に無効化する。
  捨てた dispatch stack の pointer は保持しない。
- WM の明示 enter/leave の深さが正の間だけ `caller_access_get` は TRUSTED を返す。
  下にある USER 値は変更しない。保存 app pointer 用の `caller_access_get_user` は
  WM 中も USER の同一性・現在 slot/owner/CR3 を要求し、TRUSTED へ fallback しない。
  USER redirect 登録をこの保存値へ接続し、途中の generation 変化を取り直して
  許可しない。登録済み buffer は従来の登録者 PD / 値保存を維持する。

**P3 対応と挙動維持の判断**:
P3-1/2 は、子の通常 syscall が返ると親の記述子と `in_syscall=1` に戻ったまま
子が CPL=3 で走る既存の形を維持する。この瞬間は現 slot/CR3 が子なので
親 USER の取得は拒否する。次の子 syscall は子を新規 capture する。
子の exit/kill の longjmp は子を失効させ、着地点で親 USER と guard/frame/WM
深さを復元する。正常 dispatch 出口の WM 深さは従来どおり0、親 WM 深さの復元は
launch/resume 着地点で行う。実 R1 足場でこの形を固定した。
P3-3 は `ring3_caller_reject_count` (BSS 4B) を入口 identity 拒否だけで増やす。
ホストで wrapper 進入なし・差分+1を確認。ゲストで正常操作の差分0はPM未確認。
P3-4 は登録値と生存 AS の **両方**を generation=0 / pd_phys=0 に揃え、
データ・位置不変で拒否するケースを追加。非0検査除去の2変異は実行時 RED。
P3-5 の guest 時間 / gfx_counters は測定していない (NP21/W 操作禁止)。
成功 syscall の入口 capture/正常復元は d1 と同じで、追加カウンタは拒否時のみ。
ホスト足場時間を PC-98 の syscall コストとは扱わず、最適化は加えていない。

**d3 以降へ渡す穴**: read/write walk・管理 frame/PFN 検査は未変更。
WM が保持する app pointer には `caller_access_get_user`、WM 自身の pointer には
`caller_access_get` を使えるが、既存 `_always` / copy / DB 入口の切替は d4/d5。
保存 caller API のホスト検証を、それら consumer の安全化済みとは扱わない。
ゲスト/実機・構成依存回帰は未実施、PM受入と §12 の一括確認へ持越し。

ホスト試験は実 `redir_access.c` / dispatcher / WM enter/leave と、実 exec の
exit/kill/4 park/両着地・実 `setjmp.asm` を使用する。R1 は AppSlot/CR3/IF と
資源境界だけを足場にし、loader 全体・実 CPL 遷移は実行しない。子の正常 syscall と
次の終了 syscall を実 dispatcher で実行し、longjmp 前の無効化、回収前の失効、
両着地の親復元を確認する。着地へ stale 値を注入するケースもあり、転送側と
着地側の無効化を独立に検証する。IF両値・拒否時out不変、WM入れ子と USER 不変、
親子同VA別backing・WMから登録済み親bufferへの書込みも検査する。

対象変異は caller **24/24**、redirect **24/24**、R1 **20/20** が
コンパイル成功後の **実行時 RED**。ツリー複製なし、対象 TU と fixture の写しのみ。
R1 は正常 compile/run 約0.3〜0.5秒、20変異一式は約10秒。
R1拡張の初回は `ring3_ptr_ok` の足場の static 宣言が実headerと衝突し compile失敗
(rc=1)、修正後GREEN。RED本数には含めない。
ring3_guard は既存 **14/14 RED** (静的検査を含む、上記実行時68本とは別勘定)、
実 AppSlot 回帰は host/target とも成功。
対応表の初回 lint は caller 試験から R1 helper を import した依存7件の不足で rc=1。
不要な import を除き、実際の R1→redir_access 依存だけを追加して rc=0。

| ILP32 実測 | 作業前 (clean build_id) | d2 後 (-dirty) | 差分 |
|---|---:|---:|---:|
| AS / AppSlot | 692 / 192 B | 同左 | 0 B |
| RedirAccess / FdRedirect / State | 24 / 52 / 156 B | 同左 | 0 B |
| caller 現在値 / dispatch 退避値 | 28 / 28 B | 同左 | 0 B |
| launch/resume の寿命 snapshot | 0 B | 各40 B | 各stack +40 B |
| kernel.bin | 361,040 B | 361,592 B | +552 B |
| vmkernel.lz4 (SQLite含む) | 479,116 B | 479,367 B | +251 B |
| 本体占有 (`__bss_end - 0x100000`) | 571,888 B | 572,432 B | +544 B |
| `__bss_end` | `0x18B9F0` | `0x18BC10` | +544 B |
| リンカ ASSERT 残り (596 KiB枠) | 38,416 B | 37,872 B | -544 B |

型サイズは ILP32 R1 fixture の sizeof assert、画像は同一 cross toolchain の
nm / ファイルサイズで確認。stack はCの保存値のサイズで spill/整列込みの最大値ではない。
圧縮上限残り40,825B。§6-1のT2c-R基準から2,628B消費、d計画枠3,072Bの残りは
**444B**。d3〜d6のwalk/copy/自己診断まで収まる根拠はなく、後続で実装前に再見積りする。
ASSERTは緩和していない。

実行環境は `CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、
PATHにcross/bin、ILP32は既存の `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner`
で qemu-i386 を使用 (native int80 を拒否するホスト環境への適合)。
make は全て `< /dev/null`。
`make kernel` (作業前) rc=0、`NP21W_DIR=/home/hight/os32-tmp/d2-image-output make all`
rc=0。FDコピー先を一時パスに限定し、その未作成パスへのコピー警告が出たが
build 自体は成功。既存GNU-stack/RWX警告あり。実NP21/W・NHD・配備・iniは未操作。
`python3 tools/tests/test_caller_access.py --mutate`、
`test_fd_redirect_d0a.py --mutate`、`test_exec_r1.py --mutate`、
`test_ring3_guard.py --mutate`、`test_multiapp_impl.py` は各 rc=0。
`python3 tools/check_select.py --lint`、`gen_memmap.py --write`、
`gen_tests_inventory.py --write` は rc=0。
ログは `/home/hight/os32-tmp/d2-{before,all,caller,redir,r1,guard,multiapp}.log`。
最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は **1回だけ実行して rc=0** (上記PATH/PYTHONPATH)。
変更関連40ターゲットは変異込み、残り71は変異なし。caller 24/24・redirect 24/24・
R1 20/20の実行時RED、C方言27/27 RED・対照5/5 GREENを含め成功。
ログは `/home/hight/os32-tmp/d2-check-changed.log`。
既存 Windows opt-in fixture は単独4件・集約5件が skip。
検査完了後はこの結果の票への追記だけで、コード・試験は変更していない。
commit/pushは未実施。


### d2 独立レビュー P3-1 / P3-2 / P3-4 の対応 (2026-10-02)

モデル: GPT-6。基点 `dffd625`、worktree `wt/t2d2`。P3-1 は save / clear /
restore の前方宣言に `__attribute__((noinline))` を付け、R1 が抽出する定義の形は
維持した。4 park の代入3本 + invalidate を `ring3_context_clear()` へ集約し、
kapi_sys_exit / ring3_kill_kind の直後の exit / pending transfer と重複するゼロ書きを
削除した。WM fault の計数は clear より前のまま。P3-3 の WM TRUSTED 分岐は維持した。

P3-2 は R1 の着地抽出を `volatile Ring3CallContext caller_context;` から始め、
実ソースの save を含めた。save 欠落 / setjmp 後の着地側へ save を移す2種類を
launch / resume 両方に追加 (4変異)。park 変異も共通 clear の除去へ変更した。
既存 ring3_guard の WM fault 計数の静的検査・変異アンカーは、共通 clear を行う
exec_exit / exec_pending_transfer より前で数える形へ追従させた。

入れ子の exec_run から戻った後、親の wrapper の残りは着地点で
`ring3_in_syscall=1` に戻る (d2 の前は 0 のまま CPL0 扱いで走っていた)。
その区間の #PF は親アプリの kill になり、3つの門 (`ring3_user_range_ok` /
`ring3_user_ranges_writable` / `tramp_copy`) が働く。正しい方向の変化で、
WM の深さも正しく復元されるようになった。ゲスト回帰では
**sh → exec_run → 戻った後の親 wrapper の経路**を確認する。
今回は NP21/W 操作禁止のためゲスト確認は未実施、PM受入へ持ち越す。

| 同一 cross toolchain 実測 | P3修正前 (clean build_id) | P3修正後 (-dirty) | 差分 |
|---|---:|---:|---:|
| exec.o text | 20,213 B | 19,889 B | -324 B |
| kernel.bin | 361,584 B | 361,272 B | -312 B |
| 本体占有 (`__bss_end - 0x100000`) | 572,432 B | 572,112 B | -320 B |
| `__bss_end` | `0x18BC10` | `0x18BAD0` | -320 B |
| リンカ ASSERT 残り (596 KiB枠) | 37,872 B | 38,192 B | +320 B |
| §6-1 d枠の残り (3,072 B) | 444 B | 764 B | +320 B |

作業前 `make kernel` で基点の clean build_id に揃えたため、上のd2作業時の
kernel.bin 361,592 B (-dirty) と8 B異なる。exec.o のd1からのtext増分は
+400 B → +76 B。kernel本体は整列込みで320 B減少し、kernel.binの差分は
build_idのdirty化も含む。d枠の消費はT2c-R基準から2,308 Bとなった。
ASSERTは緩和していない。d3以降の再見積りゲートは維持する。

検証環境: `CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、
PATHにcross/bin、ILP32 fixtureのみ既存の
`PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner` で qemu-i386 を経由。
R1 は GREEN + **24/24 コンパイル成功後の実行時RED** (新規4本を含む)。
編集直後のR1実行はPythonの連結記号漏れでSyntaxError (rc=1) となり、修正後rc=0。
共通clearへの追従前のring3_guardは旧静的アンカーでrc=1、追従後はrc=0、
既存14/14 RED (静的検査を含む)。
失敗はRED本数へ算入しない。
`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp
NP21W_DIR=/home/hight/os32-tmp/d2-p3-image-output make all < /dev/null` は **rc=0**。
FDコピー先は一時パスへ限定し、未作成パスへのコピー警告と既存GNU-stack / RWX等の
警告が出たがbuildは成功。NP21/W・NHD・配備・ini・commit/pushは未操作。
ログ: `/home/hight/os32-tmp/d2-p3-{before,all,r1,guard}.log`。
生成地図は `python3 tools/gen_memmap.py --write` rc=0、対応表lintと試験一覧生成もrc=0。
最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は上記PATH/PYTHONPATHで **1回だけ実行し rc=0**。
R1 24/24実行時RED、ring3_guard 14/14 RED、C方言27/27 RED・対照5/5 GREENを含め成功。
既存Windows opt-in fixtureは単独4件・集約5件がskip。
ログ: `/home/hight/os32-tmp/d2-p3-check-changed.log`。
検査中はソースを変更せず、終了後は本結果の追記だけ。ゲスト未実施・commit/push未実施。

## 10-8. d2 の着地とゲスト受入 (PM、2026-10-02、NP21/W 17MB、main `d975016`)

独立レビュー Opus 5.5 は P1・P2 なしで Approve (網羅性の要求つき)。P3-1 (inline の重複、-324B)・P3-2 (R1 の save 欠落・移動の変異)・P3-4 (§10-7 の注記) は着地前にコーダー (sol) が対応、P3-3 (caller_access_get の WM TRUSTED 分岐の使う側が無い 34B) は d4/d5 まで据え置き。予算は §6-1 の決定のとおり d の枠を拡大。

ゲスト (17MB、今の ini — §12): kselftest 0 fail、`klibc_test` 49/49、`alloc_demo` 16/16、`ring3_fault` kill、`ls / | wc -l` = 54、`echo abc | wc -c` = 4、`d0a_test` 全行 OK (CPL=3 の親 → exec_run の子 → 親へ戻る経路を含む)、faulttest 一式・V86・GUI (gui_demo → CUI) 従来どおり。**`ring3_caller_reject_count` = 0** (入口の誤拒否なし)、kill 8 件はすべて意図したもの、取り残し 0、深さ 0。P3-5 (毎回の syscall のコスト) は体感で差なし (計測は未実施)。

## 10-9. d3 実装結果 (2026-10-02)

モデル: GPT-6。基点 `191f3d3`、worktree `wt/t2d3`。状態行・親票・
TASK_MEMMAP_V3 は変更していない。d4〜d6 の実装は含めない。

`exec/access_walk.c` の `as_access_page` は、生存確認済みASとIRQ保存区間を
前提に、PDの整列/owner、APP/lease PT控えとowner、共有PTのmaster PDEと
`page_tables`登録frameを**表の読取り前**に確認する。既存の
`as_va_to_pa` / `as_va_to_pa_read` を再利用してPDE/PTEのP/Uと出力RWを検査し、
PSを拒否する。失敗時のPA出力は不変。masterへのCR3往復は追加していない。

PFNは私有RAM (同ownerのPD/PTをpayloadとして許可しない)、恒等SHM、
shlibの登録済みtext/rodata原本と台帳owner、live RAM/FIXED_RAM leaseの
surface世代・参照数・ページ数・権限・台帳・offsetと照合する。MMIO/VRAMは
一般copy対象外。trampolineは登録済み恒等backingかつRO PTEの場合だけ入力可。
`ring3_ptr_ok` にscratchの先頭〜末尾の早期分類も追加した。
`redir_page` をこのwalkへ接続し、`caller_access_page` は同じ処理に加えて
現在slot/owner/CR3の一致を要求する (明示USERはWM中もUSERのまま)。

判断の補足: closingなsurfaceでも既存leaseは従来どおりreleaseまで有効なので、
世代と参照が生きているRAM leaseのread/writeを許可する。SHMは現行の共有帯全体を
許可し、block所有者による新しい制限は追加しない。trampolineはROページ単位で
許可し、早期分類で追加するのは返却scratch内だけ。これらは現行挙動の維持。

**d4/d5へ渡す穴**: `caller_access_page` はページ1枚の内部primitiveで、呼び手が
IRQ保存から利用までを囲む。cstrのNUL/overflow/cap、bytesの全範囲preflight、
copyoutの全byte不変はd4。DB3入口、既存のread/outputガードのmaster往復撤去、
有効leaseの早期分類はd5の入口接続で行う。古い `as_va_to_pa*` 単体の利用側を
B1完成済みとは扱わない。kernelは `-ffunction-sections` なしでリンクするため、
未使用の `caller_access_page` もGCされず、d3のkernelに152 B含まれている
(`kernel.map` / nm: `0x146e5c`、size `0x98`)。d4のsize見積もりではこの既計上分を
接続時の増分として再加算せず、撤去による節約としても二重に差し引かない。
boot自己診断/最終size確定はd6。

ホストは `test_access_walk.py` / `access_walk_host.c` を追加し、実paging・
allocator・shlib登録・redir/callerを1つのILP32 fixtureで実行。高位VAと低位PAを
分離し、RO入力/RW出力、NP、PDE/PTEのP/U/RW、PS、present RAMの偽PT、
別ownerのPD/PT/PFN、PD/PTのpayload化、偽共有PT/master PDE、SHMの非恒等PFN、
trampolineのRO/出力拒否、shlibの原本取り違え、RAM/FIXED_RAM lease、世代/失効/
RO権限/MMIO拒否、currentとregistrantのCR3契約差、IF両値/CR3/拒否時PA不変を確認。
新規20変異はすべて**コンパイル成功後の実行時RED**。単独実測0.39〜0.45秒/本
(OS32_MUT_JOBS=4)、全木コピーや変異ごとのmakeは使わない。
既存redirは24/24、callerは24/24、exec R1は24/24実行時RED。
app-band試験にはscratch両端と直前/直後の早期分類を追加しGREEN。
検査列・対応表・生成TESTS一覧へ登録した。

失敗履歴: 初回native ILP32実行は成功せず、既存のqemu-i386 runnerへ切替。
新fixtureのsurface登録時CR3設定漏れを直した後GREEN。
shared-PFN変異が一度生存したため、偽masterを戻す足場でPDE USERが落ちていた点を
修正し正常対照を追加、その後20/20 RED。既存redirのRO変異は引数が未使用になる
コンパイルエラーを修正してから実行時REDを確認。MMIO fixture追加時の定数名誤記も
コンパイル時に修正。これらをRED本数へ算入しない。

| 同一cross toolchain実測 | 作業前 (clean build_id) | d3 (-dirty) | 差分 |
|---|---:|---:|---:|
| access_walk.o text | 0 B | 964 B | +964 B |
| redir_access.o text | 1,428 B | 1,461 B | +33 B |
| exec.o text | 19,889 B | 19,765 B | -124 B |
| paging.o text | 9,740 B | 9,760 B | +20 B |
| shlib.o text | 1,200 B | 1,292 B | +92 B |
| kernel.bin | 361,264 B | 362,232 B | +968 B |
| vmkernel.lz4 | 479,369 B | 480,070 B | +701 B |
| 本体占有 (`__bss_end - 0x100000`) | 572,112 B | 573,072 B | +960 B |
| `__bss_end` | `0x18BAD0` | `0x18BE90` | +960 B |
| リンカASSERT残り (596 KiB枠) | 38,192 B | 37,232 B | -960 B |
| d枠残り (拡大後5,888 B) | 3,580 B | 2,620 B | -960 B |

AS/AppSlot/BSS追加なし。ASSERTは緩和していない。d枠消費は基準から3,268 B。
圧縮上限520,192 Bまで40,122 B。kernel.bin差分はbuild_idのdirty化も含む。

実行環境: `CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、
PATHにcross/bin、ILP32は `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner`。
作業前 `make kernel < /dev/null` rc=0。対象の `python3 tools/tests/test_access_walk.py
--mutate`、`test_fd_redirect_d0a.py --mutate`、`test_caller_access.py --mutate`、
`test_exec_r1.py --mutate`、`test_app_bb_overlap.py` は各rc=0。
`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp
NP21W_DIR=/home/hight/os32-tmp/d3-image-output make all < /dev/null` rc=0。
FDコピー先は存在しない一時パスへ限定し、コピー警告あり。既存GNU-stack/RWX警告あり。
`python3 tools/gen_memmap.py --write`、`python3 tools/check_select.py --lint`、
`python3 tools/gen_tests_inventory.py --write` は各rc=0。
ログは `/home/hight/os32-tmp/d3-{before,all,walk,redir,caller,r1,app}.log`。
NP21/W・NHD・配備・ini・commit/pushは未操作。ゲスト受入と独立レビューはPMへ。
最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は上記PATH/PYTHONPATHで**1回だけ実行しrc=0**。
`build/kernel.mk` の翻訳単位追加により選択器が全変異 (full) に拡張した。
d3は20/20実行時RED、redir/callerは各24/24実行時RED、C方言は27/27 REDと
正常対照5/5 GREENを含め成功。既存Windows opt-inは単独4件・集約5件がskip。
ログ: `/home/hight/os32-tmp/d3-check-changed.log`。
最終buildログ: `/home/hight/os32-tmp/d3-all-final.log`。
検査中はソースを変更せず、終了後はこの結果の追記だけ。

### d3 独立レビュー (Opus 5.5、Approve) の P2-1 / P3-1 / P3-3 対応 (2026-10-02)

モデル: GPT-6。基点 `6efb0bf`、worktree `wt/t2d3`。
P2-1: master CR3下で登録者の `MEM_EXEC_LOAD_ADDR` の1 Bをread/writeとも許可する
正常対照をIF=0/1で追加。現在callerの同じアクセスは拒否し、CR3/IFは不変。
実物 `exec/access_walk.c` のwrite/readそれぞれの `as->pd_phys` を
`paging_current_cr3()` に置き換える2変異を追加した。d0a側の2変異は
代用 `as_access_page` を書き換える変異であるとコードと表示名に明記した。

P3-1: SHM_ENDより上の `MEM_DEVICE_APERTURE_BASE` (Cirrus窓) に恒等USER/RW
ページを作り、実 `as_va_to_pa` では翻訳成功する正常対照を確認したうえで、
caller walkのread/write両方を拒否するprobeを追加。共有master/登録PTの照合は通る
足場であり、SHM上限式を `1` にする実物への変異を検出する。
walkは正常GREEN、新規3本を含む **23/23コンパイル成功後の実行時RED**。
P3-3: 上記caller primitiveのGC説明を訂正した。実装コードの変更はない。

**d4 / d5 / d6 / e への申し送り (記録のみ、今回のコード変更なし)**:

- **P3-2 → d6**: 生き残る多重防御の変異は、trampolineの `!write`、leaseの
  `!sf->lease_count` / `sf->npages != l->npages` / `l->sid`上限 /
  `perm_max == NONE`、`shlib_read_page` のSHLIB owner / `page < g_text_pages`、
  master PDEのP / PS、PDの整列、`caller_access_page` の `appslot_cur()` 照合。
  d6で残すものと不要なものを仕分け、残すものは正常対照と目的別負例を用意する。
- **P3-4 → d5**: trampoline scratchで `ring3_ptr_ok` と
  `ring3_user_range_ok` (`exec.c:700/915`、d3時点) が食い違い、getcwdの結果を
  db_openなどの入力に渡すとINVALになる。d5の入口接続でwalkへ揃える。
- **P3-5 → d4**: `caller_access_page` (`redir_access.c:140`、d3時点) の
  TRUSTEDには帯・長さ・NULLの制限がない。d4の受入にredirと同じ帯の制限
  (`< MEM_APP_BAND_BASE`) とcap/NUL検査を含める。
- **P3-6 → d5前**: leaseではページごとの `ledger_surface_validate` をIRQ停止中に
  呼ぶため `O(npages × regions)`。入口接続前に計測するか、世代と参照数の一致だけで
  済む形を検討する。
- **P3-7 → e**: 共有帯の `result != va` / `frame == exec_tramp_page_addr()` は
  恒等写像に依存する。低位USER撤去時に見直す。

| 同一cross toolchain実測 | レビュー修正前 (clean build_id) | 修正後 (-dirty) | 差分 |
|---|---:|---:|---:|
| kernel.bin | 362,224 B | 362,232 B | +8 B |
| vmkernel.lz4 | 480,065 B | 480,070 B | +5 B |
| 本体占有 (`__bss_end - 0x100000`) | 573,072 B | 573,072 B | 0 B |
| `__bss_end` | `0x18BE90` | `0x18BE90` | 0 B |
| リンカASSERT残り (596 KiB枠) | 37,232 B | 37,232 B | 0 B |
| d枠残り (5,888 B) | 2,620 B | 2,620 B | 0 B |

kernel実装は変更しておらず、kernel.binの8 B増分はbuild_idのdirty化による。
caller primitiveの152 Bはこの前後両方に含まれる。ASSERTは緩和していない。

実行環境: `PATH=/home/hight/opt/cross/bin:$PATH`、
`CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`。
ILP32試験は既存 `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner` のqemu-i386経由、
`OS32_MUT_JOBS=4`。作業前 `make kernel < /dev/null` rc=0。
`python3 tools/tests/test_access_walk.py --mutate` rc=0 (23/23 runtime RED)、
`python3 tools/tests/test_fd_redirect_d0a.py --mutate` rc=0 (24/24 runtime RED)。
`NP21W_DIR=/home/hight/os32-tmp/d3-review-image-output make all < /dev/null` rc=0。
FDコピー先は未作成の一時パスに限定し、コピー警告あり。既存Rust / GNU-stack / RWX
警告あり。ログ: `/home/hight/os32-tmp/d3-review-{before,walk,redir,all}.log`。
NP21/W・NHD・配備・ini・commit/pushは未操作。ゲスト確認は未実施、PM受入へ渡す。

`python3 tools/gen_memmap.py --write` rc=0 (生成地図の差分なし)。最終
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は上記PATH/PYTHONPATHで **1回だけ実行しrc=0**。
選択器が基点 `191f3d33c77d` (mainとのmerge-base) からのbuild規則の変更を検出し、
full (全変異込み) へ拡張した。C方言27/27 RED・正常対照5/5 GREENも成功。
既存Windows opt-in試験は単独4件・集約5件がskip。
ログ: `/home/hight/os32-tmp/d3-review-check-changed.log`。
検査中はソースを変更せず、終了後は本結果の追記だけ。

## 10-10. d3 の着地とゲスト受入 (PM、2026-10-02、NP21/W 17MB、main `1c00078`)

独立レビュー Opus 5.5 は P1 なしで Approve、P2-1 (実物の walk への「登録者 PD → 現在 CR3」の変異が生き残る — 試験の強さの低下) と P3-1・P3-3 をコーダー (sol) が直し (`ca14075`)、同じレビュアーが差分で Approve (walk の変異 23/23 実行時 RED)。残りの P3 は §10-9 の申し送り (P3-2 → d6、P3-4 → d5、P3-5 → d4、P3-6 → d5 の前、P3-7 → e)。予算: d の枠の残り 2,620B (d4〜d6 の見込み 1.0〜1.8KB)。

ゲスト (17MB、今の ini — §12): kselftest 0 fail、`klibc_test` 49/49、`alloc_demo` 16/16、`ring3_fault` kill、`ls / | wc -l` = 54、`echo abc | wc -c` = 4、`d0a_test` 全行 OK、faulttest 一式・V86・GUI (gui_demo → CUI) 従来どおり。`ring3_caller_reject_count` = 0、`redir_refuse_count` = 0、kill 8 件はすべて意図したもの、取り残し 0、深さ 0。

## 10-11. d4 の実装結果 (2026-10-02)

モデル: GPT-6。基点 `17756a9`、worktree `wt/t2d4`。d4のみ。
`exec/redir_access.c` / `include/redir_access.h` に §1-2 の4 helperを追加した。
d3の生存照合/walkと同じ翻訳単位に置き、既存の返却scratchの寿命は変えない。
cstrはcap=NUL込み、cap0/NULL拒否、整数加算前のoverflow検査、1 byteずつ
walk→PA読取→NUL判定。NULの先のpageを検査しない。失敗したstagingは使用禁止。
bytes/copyoutは範囲全体をpreflightしてからPA経由でpage単位にcopyし、その全体を
1つのIRQ保存区間で囲む。通常の拒否では出力全byte不変、IF/CR3も不変。
allocation/callback等は呼ばず、kernelで確定した非重複stagingを要求する。

P3-5を反映し、`caller_access_page` はNULL/0番地とTRUSTEDの
`va >= MEM_APP_BAND_BASE` を拒否する。固定長では帯末をまたぐ長さもpreflightで
拒否し、cstrはNULまでの各byteに同じ帯制限を適用する (cap全体の帯内性は要求せず、
帯末NULを許す)。固定長のlen0はNULLを含め無アクセスで成功する。
既存redir同様の帯制限であり、TRUSTEDにUSER権限walkを新設しない。
単独checkは予約ではない。複数出力は全検査から全書戻しまで呼出側のIRQ保存が必要。

ホストは `test_caller_copy.py` / `caller_copy_host.c` を追加し、d3の実paging/
pgalloc/shlib/walk/caller足場を共用する。物理恒等backingと高位VAを分離し、
次のVA pageを別のPA位置へ張って、page末NUL/次NP、cap末NUL/未終端、NULL、
len0、整数overflow、RO入力/出力拒否、2pageのコピーと拒否時不変、古い世代/
CR3不一致、WM中の明示USER、TRUSTED帯末/cap/NULをIF両値で確認した。
実primitiveの入口を観測し、IRQ停止とNULまでの検査回数、overflow/帯外rangeの
事前拒否もassertする。MMU/IRQはホスト代用で、実CR3/TLBの合格ではない。

変異16/16は全てコンパイル成功後の実行時RED (1.43〜2.06秒/本、4並列)。
IRQ区間除去、NUL後probe/read、cap0/overread/未終端成功、NUL前のcap先読み、
overflow検査除去、preflight除去/RW無視、copy長/offset、無条件STI、TRUSTEDの
上端/全長検査を対象にした。写しの固定source closureだけを使用し全木copyなし。
実walkの23/23変異も実行時RED。初回fixtureとcap0変異のmisleading-indentation
コンパイル失敗は修正後に再実行し、REDに算入していない。
検査列・check_map・生成TESTSに新規試験を登録した。

| 同一cross toolchain実測 | 作業前 (clean build_id) | d4 (-dirty) | 差分 |
|---|---:|---:|---:|
| kernel.bin | 362,224 B | 362,840 B | +616 B |
| vmkernel.lz4 | 480,065 B | 480,477 B | +412 B |
| 本体占有 (`__bss_end - 0x100000`) | 573,072 B | 573,680 B | +608 B |
| `__bss_end` | `0x18BE90` | `0x18C0F0` | +608 B |
| リンカASSERT残り (596 KiB枠) | 37,232 B | 36,624 B | -608 B |
| d枠残り (5,888 B) | 2,620 B | 2,012 B | -608 B |

既リンクのcaller primitive約152 Bは再計上していない。新helperもkernelに
リンク済み (nmで確認)、ASSERTは変更なし。kernel.bin差分のうち8 Bはdirty化。

**d5へ渡す穴**: DB3入口と既存read/outputガードへの接続は未実施。
trampoline scratchの早期分類とrange判定の食い違い (P3-4)、有効lease分類、
master往復撤去はd5へ。P3-6のlease検査コストは入口接続前に測定/検討が必要。
新helperのlen/capはwrapperが有限のDB/構造体サイズへ制限する。複数出力の
全検査を先に行う配線もd5の責任。d6の自己診断・変異残件・最終size確定は未実施。
ゲスト/独立レビューはPMへ渡す。NP21/W・NHD・配備・ini・commit/pushは未操作。

実行環境は `PATH=/home/hight/opt/cross/bin:$PATH`、
`CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、
ILP32は既存 `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner` のqemu-i386経由。
`make kernel < /dev/null` (前後)、`python3 tools/tests/test_caller_copy.py --mutate`、
`python3 tools/tests/test_access_walk.py --mutate`、`python3 tools/check_select.py --lint`、
`python3 tools/gen_tests_inventory.py --write`、`python3 tools/gen_memmap.py --write` はrc=0。
対象ログ: `/home/hight/os32-tmp/d4-{before,kernel,copy,walk}.log`。
`python3 tools/tests/test_caller_access.py` / `test_fd_redirect_d0a.py` はrc=0。
`NP21W_DIR=/home/hight/os32-tmp/d4-unused-image-destination
CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp make all < /dev/null` はrc=0。
FDコピー先は存在しない一時パスへ限定し、copy警告を確認。NP21/Wへは書いていない。
既存Rust / GNU-stack / RWX警告あり。ログ: `/home/hight/os32-tmp/d4-all.log`。
最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は上記PATH/PYTHONPATHで**1回だけ実行しrc=0**。
`build/sdk.mk` の検査追加で選択器がfullへ拡張した。d4の16/16、walkの23/23は
実行時RED、C方言27/27 RED・正常対照5/5 GREEN。P2V違反0件。
既存Windows opt-inは単独4件・集約5件skip。ゲスト確認は未実施。
ログ: `/home/hight/os32-tmp/d4-check-changed.log`。
検査中はソース変更なし。終了後は本結果の追記だけ。

### d4 独立レビュー (Opus 5.5、Approve) の P3 対応 (2026-10-02)

基点 `7fac1ed`、`wt/t2d4`。今回の P3 番号は上記の d3 からの申し送りと別。
カーネル実装は変更せず、試験と記録を補強した。

- **P3-1**: RO の次ページを含む範囲について
  `CHECK(!check_caller_write_range(&caller, (void *)va, 8))` を IF 両値で追加。
  `caller_range(c, (u32)(uptr)dst, len, 1)` の末尾を `0` にする変異を
  `test_caller_copy.py` に追加した。補強前は変異が生存して試験器 rc=1、
  補強後はコンパイル成功後の実行時 RED。正常対照 PASS、全変異 **17/17 runtime RED**。
- **P3-2**: copyout の次ページ NP 拒否前に、写し先の先頭 4 byte を input と
  異なる `0x55` で埋め、拒否後も全 4 byte が `0x55` のままと確認する。
  部分書込みを input と同じ初期値で見逃す穴を閉じた。

**d5 接続前の申し送り (P3-3〜5、コード変更なし)**:

- **P3-3**: cstr は 1 byte ごとに walk 全体を走らせ、範囲全体を 1 つの
  IRQ 保存区間で処理する (`exec/redir_access.c:259-268`、
  `exec/access_walk.c:34-53`)。lease 上では各 byte の
  `ledger_surface_validate` が npages 回検査するため、割込み禁止時間は
  cap × npages に比例する (例: SQL の cap 1024 × 75 ページの面で約 77k 回)。
  **d5 の接続前に lease 上の終端なし 1024 byte の時間を測定するか、
  「ページ先頭で 1 回 walk → ページ内は PA+off」へ変更する**。
  後者を採る場合は §1-2 の「その1 byteの権限確認」に、同一 IRQ 保存区間内で
  確認済みページの権限を再利用する旨を注記し、NUL 後を検査しない契約を保つ。
- **P3-4**: helper 自身は len / cap の上限を持たない
  (`exec/redir_access.c:197 / :221 / :255`)。
  **d5 の各 wrapper で上限を明示し、レビューで確認する**。
- **P3-5**: `kmemcpy` は重なりを保証しない。
  **d5 の staging は SHM に置かず**、caller の写し元・写し先と非重複にする。

| 同一cross toolchain実測 | P3対応前 (clean build_id) | P3対応後 (-dirty) | 差分 |
|---|---:|---:|---:|
| kernel.bin | 362,832 B | 362,840 B | +8 B |
| vmkernel.lz4 | 480,466 B | 480,477 B | +11 B |
| 本体占有 (`__bss_end - 0x100000`) | 573,680 B | 573,680 B | 0 B |
| `__bss_end` | `0x18C0F0` | `0x18C0F0` | 0 B |
| リンカASSERT残り (596 KiB枠) | 36,624 B | 36,624 B | 0 B |
| d枠残り (5,888 B) | 2,012 B | 2,012 B | 0 B |

本体増分はなく、ファイル増分は build_id の dirty 化による。
`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp make kernel < /dev/null`
(対応前)、`python3 tools/tests/test_caller_copy.py --mutate` (補強後) は rc=0。
`NP21W_DIR=/home/hight/os32-tmp/d4-p3-unused-image-destination
CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp make all < /dev/null` は rc=0。
FDコピー先は存在しない一時パスへ限定し、copy 警告を確認した。
PATH / PYTHONPATH は上記と同じ。ログは
`/home/hight/os32-tmp/d4-p3-{before,red,copy,all}.log`。
NP21/W・NHD・配備・ini・commit/push は未操作。ゲスト確認と性能測定は未実施。
`python3 tools/gen_memmap.py --write` は rc=0 (生成差分なし)。最終
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は上記 PATH / PYTHONPATH で **1 回だけ実行し rc=0**。
基点以降の `build/sdk.mk` の変更で選択器が full へ拡張した。
caller-copy は 17/17 runtime RED、C 方言は 27/27 RED・正常対照 5/5 GREEN。
既存 Windows opt-in は単独 4 件・集約 5 件 skip。
ログ: `/home/hight/os32-tmp/d4-p3-check-changed.log`。
検査中はソース変更なし。終了後は本結果の記録だけ。



## 10-12. d4 の着地とゲスト受入 (PM、2026-10-02、NP21/W 17MB、main `f38afad`)

独立レビュー Opus 5.5 は P1・P2 なしで Approve (d5 で接続したときに到達する反例もなし)。P3-1 (RO の write-range 拒否の試験の穴) と P3-2 (NP 拒否の写し先の不変の確認) はコーダー (sol) が対応 (変異 17/17 実行時 RED)、P3-3〜5 は §10-11 の d5 への申し送り (cstr の IRQ 区間の長さの計測、len / cap の上限をラッパーごとに明示、staging を SHM に置かない)。予算: d の枠の残り 2,012B、ASSERT 残り 36,624B。新しい 4 関数は d5 まで呼び出し元が無い。

ゲスト (17MB、今の ini — §12): kselftest 0 fail、`klibc_test` 49/49、`alloc_demo` 16/16、`ring3_fault` kill、`ls / | wc -l` = 54、`echo abc | wc -c` = 4、`d0a_test` 全行 OK、faulttest 一式・V86・GUI (gui_demo → CUI) 従来どおり。`ring3_caller_reject_count` = 0、`redir_refuse_count` = 0、kill 8 件はすべて意図したもの、取り残し 0、深さ 0。

## 10-13. d5 の実装結果 (2026-10-02)

モデル: GPT-6。基点 `47397cb`、worktree `wt/t2d5`。d5のみ。
`db_open` / `db_open_existing` / `db_prepare_only` の文字列を実
`copy_caller_cstr` へ接続。USERは保存caller、CPL0直呼び/WMは既存の明示trusted
経路を使う。前2入口のcapは `PATH_COPY_BUF_SIZE = OS32_MAX_PATH = 256`
(NUL込み)、SQLは `SQL_COPY_BUF_SIZE = DB_SQL_MAX_BYTES = 1024` (NUL込み)。
既存kernel BSS stagingを再利用し、SHMにstagingを新設していない。

`db_user_str_copy` / `db_user_range_ok` を撤去。後者の既存consumerである
bind_text/blobの「検査→直接kmemcpy」も、同じ入力補助の固定長分岐から
`copy_caller_bytes` へ置換した (text最大255 B、blob最大4096 B、NULLはlen0も
従来どおり拒否、非NULLのlen0は空値)。bindのindex検査順序・TRANSIENT・rcは維持。
既存 `db_v50_selftest` のNULL/overflow 2呼出だけ撤去補助から新補助へ付替えた。
d6のboot自己診断追加はしていない。

**初回実装の記録 (下記レビュー対応で撤回)**: §1-2で指定されたcopy失敗時のSQLite進入0・旧stmt不変を満たすため、
prepare_onlyはcopyを旧stmtのfinalizeより前へ移した。NULL/未終端/NP等の
**copy拒否では旧stmt/bindableを残す**。空SQL・複数statementなどコピー成功後の
意味検査は従来どおり旧stmtを捨てる。旧ホスト試験の「NULL/未終端でも破棄」は
この設計契約へ更新し、コピー拒否後は明示finalizeして旧SQLを実行しない対照にした。
公開slot・引数・エラー値 (open=-1/CANTOPEN、existing/prepare=-1/MISUSE) は不変。
SQLite engine、旧db_exec/db_prepare、FEP facadeには入っていない。

既存 `ring3_user_range_ok` / `ring3_user_ranges_writable[_always]` は保存callerの
管理walkへ統一。PDE/PTEのPRESENT/USER、出力は両方RW、管理frame/backingを検査。
`ring3_pd_range_writable` / trivial補助 / master CR3往復を撤去した。
初回は2出力を1つのIRQ保存区間で全検査した (下記P3-1でページ単位へ変更)。
`_always` はWM内も保存USERを使う。
単独checkは予約ではなく、既存出力wrapperが検査後yieldしない契約を保つ。
汎用ガードは従来のlenを検査するだけで新たな無制限copyを追加しない。
DB結果は既存固定SHM形式であり、3入口にcaller出力引数はないため
`copy_to_caller` の架空の呼出箇所を作らない。d4の実copyout試験を回帰する。
内部拒否診断は新walkの失敗をread=BAND / write=WR_TABLEへ集約 (pageは0)。

**申し送り対応**:

- d3 P3-4: trampoline RO scratchの入力をread walkへ揃えた。
  `ring3_ptr_ok` のscratch分類を維持し、有効RAM leaseの分類を追加。
  scratch/RO leaseへの出力は拒否し、世代違いleaseは早期分類でも拒否する。
- d3 P3-6 / d4 P3-3: cstrを「ページ初回walk→ページ内PA+off」へ変更。
  同じIRQ区間内でAS/mapが変わらない条件を利用し、NUL後のページを触らない。
  世代/参照数だけへの弱化は採らず、`ledger_surface_validate` の管理backing照合を
  維持した。75ページRAM面の未終端1024 Bで実walk **1024回→1回**
  (ページ整列時。非整列で跨げば最大2回) をIF=0/1でassert。
  面全体の検査は75ページ分が残るが、cap倍の反復はなくなる。
  PC-98のIRQ停止時間は未計測であり、ホスト時間を実機時間としない。
- d4 P3-4 / P3-5: 上記cap/len上限をwrapperで保持、stagingは既存kernel BSS。
  app/lease/SHM入力と非重複。TRUSTEDには内部callerの非重複staging契約を適用。

ホスト `test_db_caller.py` は実 `kapi_db.c` 全文、exec.cの実ガード関数群
(定義/本文を無改変で抽出)、実caller/copy/paging/pgalloc/shlib/walkをリンク。
MMU/IRQ/VFS/SQLite境界のみ足場、SQLiteは入口呼出を数えてstaging内容を照合する。
高位VAに異なるdecoy、低位PAに本物の入力を置き、3入口のpage末NUL対照/
次NP拒否→次操作成功、SQLite/VFS進入0、prepare_only は拒否でも旧 stmt を finalize (他の 2 入口は旧状態不変)、RO入力/出力拒否、
2本目拒否、PDE RW、WMの通常/always、CPL0直呼び、失効caller、scratch、lease、
IF/CR3不変を確認。カウンタはホスト境界のもの、製品KAPIを追加していない。
既存SQLite-engine試験5本は共通caller境界shimへ更新し、実walk試験と区別した。

変異は固定source closureの写しだけで、全木copy/SQLite engine再ビルドをせず実施。
d5の9本 (PA→VA、lease早期拒否、path/SQLをbytesに置換、拒否時finalize、
WM trusted漏れ、先/後出力検査除去、RO入力拒否) とd4の17本が対象。
初回fixtureのコンパイル失敗、75ページ面作成時のmaster条件不足、padding初期化で
文字列が消えた正常対照失敗、変異のunused変数コンパイル失敗/NULL期待値による
signal終了は試験器側を修正し、実行時RED本数には算入していない。

| 同一cross toolchain実測 | 作業前 (clean build_id) | d5 (-dirty) | 差分 |
|---|---:|---:|---:|
| kernel.bin | 362,832 B | 362,136 B | -696 B |
| vmkernel.lz4 | 480,466 B | 480,064 B | -402 B |
| 本体占有 (`__bss_end - 0x100000`) | 573,680 B | 572,976 B | -704 B |
| `__bss_end` | `0x18C0F0` | `0x18BE30` | -704 B |
| リンカASSERT残り (596 KiB枠) | 36,624 B | 37,328 B | +704 B |
| d枠残り (5,888 B) | 2,012 B | 2,716 B | +704 B |

旧補助撤去で704 Bを戻した。ASSERT変更なし。kernel.bin差分にはdirty化の8 Bを含む。
AS/AppSlot/台帳のサイズは不変。copy4口はnmでリンク済み。d6の最終size確定とは別の
今回の実測であり、d枠は2,716 Bを残す。

実行環境は `PATH=/home/hight/opt/cross/bin:$PATH`、
`CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`。
ILP32は既存 `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner` のqemu-i386経由、
`OS32_MUT_JOBS=4`。`make kernel < /dev/null` (前後)、
`python3 tools/tests/test_db_caller.py --mutate` (9/9 runtime RED、0.31〜0.38秒/本)、
`python3 tools/tests/test_caller_copy.py --mutate` (17/17 runtime RED、0.41〜0.49秒/本)、
`python3 tools/tests/test_kapi_db_v50.py` (24/24)、
`python3 tools/tests/test_kapi_db_owned.py`、`python3 tools/tests/test_caller_access.py`、
`python3 tools/check_select.py --lint`、
`python3 tools/gen_tests_inventory.py --write` はrc=0。
`NP21W_DIR=/home/hight/os32-tmp/d5-unused-image-destination
CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp make all < /dev/null` はrc=0。
FDコピー先は存在しない一時パスへ限定しcopy警告を確認。既存のRust/GNU-stack/RWX/
未使用変数等の警告あり。ログは `/home/hight/os32-tmp/d5-{before,kernel,db,copy,v50,owned,caller,all,all-final}.log`。
初回check-mapのYAML字下げ/19依存漏れは修正してlint=0。
P2V初回は既存名paを物理と判定する1件を検出したため、実態どおりcaller VAの
引数名va/vbへ改めた (例外追加なし)。

**PMのゲスト手順 (未実施)**: 現構成17MB/現iniのまま、新kernel.elf/mapと画像の
hash/size、kselftestを確認 [V1]。構成依存は一括確認へ持越し (§12)。

1. `/usr/bin/db_test.bin` で正常DBを確認する。`db_v50_test /tmp/d5.db` は
   旧 `BAND_TOP=0x800000` の帯外pointerを含むため、そのまま全件PASSを要求しない。
   PMの一時fixtureでは当該旧帯負例を除いた既存の正常列
   (open_existing→prepare_only→bind→step→close) を実行し、次操作も成功を記録。
   元guest試験の高位帯追随は未実施で、d6/PMのfixture整備へ明示して残す。
   FEPはSHIFT+SPACE→既知の読み1語→SPACE変換→ENTER確定、GUI→CUIも回帰。
2. 負例は既存db_v50_testの各3入口で1回ずつデバッガ停止する。現在ASの未使用
   heap末page (RW/USER、次pageはNP) を選び末尾4 Bへ非NULを書き、
   当該syscallのユーザー引数領域の文字列pointerだけを末尾4 BのVAに差替える
   (open/existingの第1引数、prepare_onlyの第2引数。元の値/4 Bを控える)。
   dispatcherの早期分類前で差替え、帯内判定を通過することも確認する。
   新nmのsqlite3_open/open_v2/prepare_v2/finalize等の入口breakpointで当該wrapper
   の呼出区間を数え、open/prepare入口は0、rc=-1、kill差分0を確認。prepare_onlyは有効handleに
   旧stmtを用意し、finalizeのみ1回、stmt=NULL/bindable=0、FD数不変を観測する。
   診断はopen=CANTOPEN、existing/prepare=MISUSE。引数/4 Bを復元し、
   新しい正常prepare/DB操作が成功することまで記録する。page末をNULへ変えた
   対照では次NPを読まずSQLite入口へ進む (SQL/path内容の意味エラーは別)。
3. CPL3の短い呼出列 `p = api->sys_getcwd(); h = api->db_open(p);` を確認する。
   次の返却文字列KAPIを間に呼ばずscratchの寿命を守る。入口でpが新nmのtrampoline
   scratch内、P/U/ROであることを確認し、sqlite3_open入口が1回となることを記録。
   getcwdはディレクトリ名なのでSQLite側CANTOPENはあり得る。**open成功を条件にせず**、
   入力copy拒否文が出ないこととSQLiteに同じcwd文字列を渡したことを受入とする。
   RO scratchへの出力拒否は別の出力ガード回帰で確認する。

**d6へ渡す穴**: §10-9 P3-2の多重防御変異仕分け、小さなboot自己診断、最終size
確定は未実施。ゲストの実CR3/TLB/画面/IRQ時間、独立レビューはPMへ。
T4へは旧db_exec/db_prepare/FEP全体の未移行を残す。d3 P3-7の恒等依存はe。
親票/TASK_MEMMAP_V3/本票状態行は変更なし。commit/push/NP21/W/NHD/配備/iniは未操作。

**最終全体検査と補修**:
`PATH=/home/hight/opt/cross/bin:$PATH
PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner
CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は **1回だけ実行しrc=2**。
`build/sdk.mk` の検査追加によりfullへ拡張した。唯一の失敗targetは
`check-memory-host` の `test_app_bb_overlap.py --mutate`。
実 `ring3_ptr_ok` を切り出す旧fixtureが新しいcaller型/2関数を持たず、15変異とも
compile失敗 (REDに算入しない)。通常実行でも同じコンパイル失敗を再現した。

`app_bb_overlap_host.c` にヘッダと未使用lease経路の足場を追加した。
その経路に入ると `CHECK(0)` で失敗させ、判定を偽って素通しにしない。
実lease/callerの結合試験はd5側が担当。補修後
`python3 tools/tests/test_app_bb_overlap.py --mutate` は **rc=0、正常対照PASS、
15/15コンパイル成功後の実行時RED**。停止したrecipeの後続
`python3 tools/tests/test_gfx_boot.py --mutate` も個別実行して **rc=0、18試験PASS、
14/14 RED**。`python3 tools/check_select.py --lint` / `git diff --check` はrc=0。
補修は試験足場だけでkernel画像・上表のサイズは不変。

全体検査内のd5 9/9・copy 17/17・walk 23/23はruntime RED、
C方言27/27 RED・正常対照5/5 GREEN、P2V違反0件。
既存Windows opt-inは単独4件・集約5件skip。
ログ: `/home/hight/os32-tmp/d5-check-changed.log`、
`d5-app-bb-before.log`、`d5-app-bb.log`、`d5-gfx.log` (同ディレクトリ)。
**ユーザーの「最後に1回」に従いcheck-changedは再実行していない。
対象失敗は修正済みだが、完了条件のcheck-changed rc=0は未達で、PMの再確認に残る。**

### d5 独立レビュー (Opus 5.5、Request changes) 対応 (2026-10-02)

基点 `747a1e1`、`wt/t2d5`。モデル: GPT-6。上記初回記録の契約/IRQ区間を更新する。

- **P2-1**: 許容された小さい修正を採用し、kselftestは拒否理由非0・計数+1・addr一致を要求する。
  管理backing/leaseを含む全walk層への理由伝播は変更範囲が広く、現段階ではwrite=WR_TABLE /
  read=BANDの集約を維持する。起動中のcaller無効はwrite=WR_TABLE (8)、read=BAND (4)。
  kselftest注記とring3_guard_tddの試験案内を修正。ホストでdispatch中/caller無効の拒否、
  理由、計数、addr、page=0を確認し、理由誤置換/計数削除をruntime REDにする。
  2本目の出力拒否addrはvbへ修正。P3-3の詳細なPDE/PTE理由と拒否pageは未実装 (page=0)。
- **P2-2**: **公開仕様 (KAPI_SPEC.md:1304-1306) を優先 — prepare_only の旧 stmt は拒否でも finalize**。
  §1-2の「旧stmt/FDに副作用なし」のうちprepare_onlyの旧stmtは例外とする。
  コピー前に旧stmtをfinalizeし、active_stmt=NULL/bindable=0にする。
  copy拒否でも旧stmtのfinalizeはSQLiteへ入るが、新SQLのprepare/openへは入らない。
  実SQLiteのprepare_replaces全mode (空/未終端/NULL) は拒否後step=DONE・bind拒否・旧SQL未実行。
  実callerのNP/NULL拒否でも旧stmt破棄を確認し、finalizeしない変異をREDへ反転した。
- **P3-1**: 単独checkおよびread/writeガードはページごとにirq_save/restoreする。
  ガード全体を囲むIRQ区間も撤去。copyの全preflight/書戻し区間は維持する。
  2ページのcheckでページ間restoreとIF/CR3不変を確認し、restoreを最終ページだけにする変異をREDにした。
- **P3-4**: db_v50_selftestの512 B要求の写し先を1024 Bのsql_copy_bufへ変更。
- **P3-5**: WM文脈でreadガードにUSER無効番地を渡す対照を追加し、WM素通し撤去の変異をREDにした。

**申し送り (コード変更なし)**:
P3-2: ring3_ptr_okはRO lease番地を早期分類で許す。kapi_host_status等の出力ガードを持たない
KAPIはCR0.WP=0でそこへ書ける。lease取得の公開APIはT2eまで無く、現時点では到達しない。
**T2eの前に出力ガードを持たないKAPIを棚卸しする**。
P3-6: 低位USER帯 (VRAM/バックバッファ/font) への出力とVRAMからの入力は拒否へ変わった。
設計票どおりであり、**ゲストでGUI回帰を必ず見る**。今回はゲスト未実施。

| 同一cross toolchain実測 | 対応前 (clean) | 対応後 (-dirty) | 差分 |
|---|---:|---:|---:|
| kernel.bin | 362,128 B | 362,232 B | +104 B |
| 本体占有 | 572,976 B | 573,072 B | +96 B |
| __bss_end | 0x18BE30 | 0x18BE90 | +96 B |
| ASSERT残り (596 KiB枠) | 37,328 B | 37,232 B | -96 B |
| d枠残り (5,888 B) | 2,716 B | 2,620 B | -96 B |

kernel.bin増分にはdirty化8 Bを含む。ASSERTは変更なし。
対象試験: `python3 tools/tests/test_db_caller.py --mutate` (12/12 runtime RED)、
`python3 tools/tests/test_caller_copy.py --mutate` (18/18 runtime RED)、
`python3 tools/tests/test_kapi_db_v50.py prepare_replaces v50_selftest` (2/2 PASS)、全てrc=0。
初回のdb fixture追加stepは未提供SQLite足場へのリンク失敗となり、その確認は実SQLite試験へ集約した。
IRQ観測fixtureの初回は前試験のprobe数を残して正常対照が失敗、probe初期化後に全変異が合格。
これらの失敗はruntime REDに数えていない。

環境: `PATH=/home/hight/opt/cross/bin:$PATH`、`PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner`、
`CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、`OS32_MUT_JOBS=4`。
`NP21W_DIR=/home/hight/os32-tmp/d5-review-unused-image-destination make all < /dev/null` はrc=0。
存在しないFDコピー先に限定し、コピー警告を確認。実NP21/W・NHD・配備・iniは未操作。
commit/pushなし。
`python3 tools/tests/test_ring3_guard.py --mutate` は14/14 RED、rc=0。
`python3 tools/gen_memmap.py --write`、`python3 tools/check_select.py --lint` はrc=0。
最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は上記PATH/PYTHONPATHで**1回だけ実行しrc=0**。
基点以降のbuild/sdk.mk変更によりfullへ拡張した。対象12/12・18/18 runtime RED、
C方言27/27 RED・正常対照5/5 GREEN。既存Windows opt-inは単独4件・集約5件skip。
ログ: `/home/hight/os32-tmp/d5-review-all-final.log`、
`/home/hight/os32-tmp/d5-review-check-changed.log`。
検査開始後はコード変更なし。終了後は本結果の追記のみ。

### d5 受入で見つかった退行と修正 (2026-10-02、基点 `9b1e917`)

モデル: GPT-6、worktree `wt/t2d5-fix`。**原因は d5 の内部出力ガードではなく、
T2c (`396ed1f`) の高位配置へのゲスト試験の追従漏れ**。
`userland/tests/db_v50_test.c` は v2 の `BAND_TOP=0x800000` を残し、
`db_bind_text(h, 1, 0x7fffff, 2)` を渡していた。`exec/exec.c` の
`ring3_syscall_dispatch` → `ring3_ptr_ok` が先頭を帯外と判定し、
wrapper へ入る前に `ring3_fault_kill` する。この経路は例外ハンドラを通らず、
`ring3_range_reject_count` も更新しない。RO/RW open の成功時は従来何も表示せず、
fixture の行が最後に見えるため、直後の open が落ちたように見えた。
T2c 前の許可帯は `0x400000` からで、現在は `0x80000000` から。
d4 基点 `47397cb` にも同じゲスト定数と低位を断る早期検査があり、d5 起因ではない。

**提示された `WR_TABLE / addr=0x2ffd84 / count=4` は起動時自己診断の記録**。
`kernel/kselftest.c:test_ring3_wm_guard` は `&local` を write/read/write/write の
4 回拒否させ、最後を負の WM 深さで write=WR_TABLE にする。
基点を同じ cross toolchain でビルドした逆アセンブルでは、
`kentry` が ESP=`0x2ffffc` から3語を積んで `kernel_main` へ jump、
同関数の EBP=`0x2fffec`、固定フレーム 0x158 B、
`kselftest_run` の call と saved EBP 各4 B から EBP=`0x2ffe8c`。
インライン化された `local` は `[ebp-0x108]`、すなわち **`0x2ffd84` と完全一致**。
`db_open_existing` のローカル `st` を拒否した証拠ではない。

修正はゲスト試験の `guard_crossing_text` が公開 `sbrk_heap_limit - 1` を使う形。
先頭は早期検査を通り、2 byte 目の guard_a を d5 の copy が拒否して -1 で戻る。
RO/RW open の handle も表示し、次の受入で停止位置を区別できるようにした。
KAPI・kernel の製品コードと公開契約は変更していない。

**内部経路の棚卸し**: DB3入口と bind、旧 exec/prepare、SQLite VFS の
read/stat/size/resolve は内部 VFS/SQLite 関数へ直接渡し、生成 `wrap_*` を呼ばない。
`db_open_existing` の本体/journal の `&st`、resolver の BSS/stack 出力は
既に内部口を通る。生成出力ガード43 wrapper と手書きの
`kapi_sys_time_now` / `kapi_pci_bind_info` を調査し、カーネル内部バッファを
公開出力ラッパーへ渡す新たな到達経路は見つからなかった。
時計内部は `sys_time_now(&snap_lo, &snap_hi)`、WM の公開表経由は既存
`ring3_wm_enter/leave`、登録済み redirect は登録者の検証という境界を維持する。

**回帰**: `test_db_caller.py` に実 dispatcher・生成引数表・生成 sys_stat wrapper・
実 `fs/vfs.c` を追加。MMU/IRQ/KAPI呼出命令・SQLite・FSドライバだけ足場。
旧値が wrapper 進入0・kill・range counter不変、新値が bind の -1へ到達することを確認。
実 DB → 実 resolve/stat → FS の kernel local 出力では拒否計数不変、
公開 sys_stat はRW出力成功/RO出力kill・書込み前の値不変を確認する (IF=0/1)。
既存12変異に、旧低位定数へ戻す/内部statを公開wrapperへ誤配線/
公開statの出力検査を外す3変異を追加し、**15/15コンパイル後の実行時RED**。
`test_kapi_db_v50.py` は実SQLiteで **24/24 PASS**。
初回の足場コンパイル失敗 (宣言/定数名) と native ILP32 のSIGSYSはREDに算入せず、
既存qemu-i386 runnerへ切り替えて上記結果を確認した。

| 同一cross toolchain実測 | 基点 clean | 修正後 dirty | 差分 |
|---|---:|---:|---:|
| kernel.bin | 362,224 B | 362,232 B | +8 B |
| vmkernel.lz4 | 480,088 B | 480,091 B | +3 B |
| 本体占有 | 573,072 B | 573,072 B | 0 B |
| __bss_end | 0x18BE90 | 0x18BE90 | 0 B |
| ASSERT残り (596 KiB枠) | 37,232 B | 37,232 B | 0 B |
| d枠残り (5,888 B) | 2,620 B | 2,620 B | 0 B |

画像差は build_id の dirty 化のみ。環境は
`PATH=/home/hight/opt/cross/bin:$PATH`、
`PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner`、`TMPDIR=/home/hight/os32-tmp`。
`CROSS_DIR=/home/hight/opt/cross make kernel < /dev/null` (修正前)、
`python3 tools/tests/test_db_caller.py --mutate`、
`python3 tools/tests/test_kapi_db_v50.py`、
`CROSS_DIR=/home/hight/opt/cross make all NP21W_DIR=/home/hight/os32-tmp/d5fix-unused-image-destination < /dev/null`
は全てrc=0。allのFDコピー先は存在しないパスを指定し、コピー警告を確認。
`python3 tools/gen_memmap.py --write` と `python3 tools/check_select.py --lint` はrc=0。
ログは `/home/hight/os32-tmp/d5fix-{before,db-caller,v50,all}.log`。

**PM の受入手順 (未実施)**: 現構成17MBのまま、既定の停止→配備→起動手順で
修正後の `db_v50_test.bin` をNHD側も更新し、実行するバイナリのサイズ/ハッシュを照合。
起動後・試験前に `fault_kill_count` と range reject 一式を記録し、
`db_v50_test` の RO/RW handle >=0、最後の `PASS n/n` と `$?=0`、
fault kill の増分0を確認する。range reject は絶対値4ではなく前後差で見る。
`db_test`・`d0a_test`・`klibc_test`・`alloc_demo`・パイプを回帰する。
ホストでは停止原因を除去できているが、ゲストの完走はPM確認待ち。
NP21/W・NHD・配備・ini・commit/pushは未操作。状態行は変更していない。

最終 `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` は上記PATH/PYTHONPATHで **最後に1回だけ実行しrc=0**。
追加回帰15/15 runtime RED、C方言27/27 RED・正常対照5/5 GREEN。
既存Windows opt-inは単独4件・集約5件skip。
ログ: `/home/hight/os32-tmp/d5fix-check-changed.log`。
検査中のソース変更なし。終了後は本結果の記録とwrapper数の表記訂正のみ。


## 10-14. d5 の着地とゲスト受入 (PM、2026-10-02、NP21/W 17MB、main `9b1e917` → 試験の追従 `d5fix`)

独立レビュー Opus 5.5 は 1 回目 Request changes (P2-1 kselftest の拒否理由、P2-2 prepare_only の公開契約) → コーダー (sol) が対応 (`555fba9`、P2-2 は PM の決定で公開契約を優先) → 同じレビュアーが差分で Approve。

ゲスト (17MB、今の ini — §12): kselftest 0 fail、`db_test` 9/9、`klibc_test` 49/49、`alloc_demo` 16/16、`ring3_fault` kill、`ls / | wc -l` = 54、`echo abc | wc -c` = 4、`d0a_test` 全行 OK、faulttest 一式・V86・GUI (gui_demo → CUI) 従来どおり。

**受入で `db_v50_test` が kill された** (`[Process crashed]`、$?=139)。PM は当初、カーネルスタックの番地 (0x2ffd84、WR_TABLE) の拒否記録から d5 の退行と見立てたが、コーダー (astra) の調査で**誤り**と分かった: 拒否記録は起動時の kselftest (kselftest.c:1691) の残り値で、kill の本当の原因は **T2c の高位配置への試験の追従漏れ** — 試験が「帯の端」として旧い番地 0x7fffff を `db_bind_text` に渡し、T2c 以降は帯の外なので入口の早期検査 (exec.c:1451) が設計どおり kill した。カーネル・公開 KAPI は変更不要で、試験の番地を `sbrk_heap_limit - 1` に直した (`d5fix`、ホストの回帰試験も追加)。直した後のゲストで **`db_v50_test` PASS 41/41、$?=0**。T2c の受入で `db_test` / `db_v50_test` を流していなかったのが見逃しの原因 — 以後の段の受入に加える。

## 10-15. d6 実装結果 (コーダー、2026-10-02)

モデル: GPT-6 (Codex)。基点 `bbabb6a`、worktree `wt/t2d6`。
§10-9 P3-2は以下の11本を個別に仕分けた。製品の防御撤去 (c) は0本。
(a)は管理情報の不整合を注入した実ソース試験でruntime RED、(b)は多重防御を維持。
正常な登録APIが不整合を作ると主張するものではなく、walkが受け取る管理状態の
拒否を検証する。ABI・公開形式・ユーザーから見える挙動は変更していない。

| 対象 | 仕分け | 正常対照・目的別負例 / 維持理由 |
|---|---|---|
| trampoline `!write` | (b) 多重防御、維持 | RO入力成功、出力拒否。write walkのPTE_RW要求と末尾のRO PTE要求は両立しないため、単独削除は生存。入力専用契約の明示として残す |
| lease `!sf->lease_count` | (a) | live参照1で成功→0でread拒否。他の権限/台帳は正常。削除をruntime RED |
| lease `sf->npages != l->npages` | (a) | 1 page一致で成功→lease側だけ2 page、先頭page readも拒否。削除をruntime RED |
| lease `l->sid` 上限 | (b) 多重防御、維持 | 正常sidで成功、上限sidを拒否。`paging_lease_map`はsid上限を確認後にl->sidを保存し、USERからAS metadataは変更不可。単独削除は足場の範囲外位置がgen不一致となり生存したが、これは配列外読取りの安全性の証拠ではない。**sfを読む前の境界検査は撤去不可** |
| lease `perm_max == NONE` | (a) | RW surfaceのread成功→NONEだけに変更。`ledger_surface_validate`自体はNONEを許す正常対照を明示し、read拒否。削除をruntime RED |
| shlib SHLIB owner | (a) | 登録PFN一致のread成功→原本の台帳ownerだけ別ownerへ移譲、拒否→復元。削除をruntime RED |
| shlib `page < g_text_pages` | (a) | text原本のread成功、登録済みdata原本をRO/USERにmapしても入力拒否。境界をg_pages容量まで広げる変異をruntime RED (配列外読取りを発生させる無制限削除は使わない) |
| master PDE P | (a) | AS PDE/登録PT/PTEは正常のままmaster Pだけclearし拒否。削除をruntime RED |
| master PDE PS | (a) | AS PDE/登録PT/PTEは正常のままmaster PSだけsetし拒否。削除をruntime RED |
| PD整列 | (a) | owned/present PDを1 byteずらし、ずれた位置に正常PDEを用意。下位translatorの成功対照と上位walk拒否を確認。削除をruntime RED |
| `caller_access_page` の `appslot_cur()` | (a) | CR3/live AS/resource ownerを保存callerに合わせ、current slotだけ別にする。fixtureのres_owner_getをcurrent slotから独立させ、削除をruntime RED |

(a)9本を既存walk23本へ追加し、**32/32コンパイル成功後の実行時RED**。
一組内の中央値1.145秒・最重2.17秒 (all/DB試験と並行した最終対象実行)、
全木コピー・変異ごとのmakeは無し。
(b)2本の写しへの単独削除はcompile成功後rc=0で生存 (`d6-retained.log`)。
生存をREDに数えず、複合削除で見かけの本数を増やしていない。
初回のshlib境界変異は未定義の仮定マクロ名でcompile失敗し、実定数
`MEM_SHLIB_SIZE / PAGE_SIZE` へ修正した。この失敗もREDに数えない。

**結線**: `build/sdk.mk`は既存recipe/検査列に6系統すべてあり、変更不要。
caller/walk/copy/DB/redirectは各専用target、`test_exec_r1.py`は
`check-memory-host`のrecipeで変異込みに結線済み。`check_map.yaml`にboot実ソースと
静的include closureを追加。実行スクリプトのTARGET_SRCを明示して生成TESTSの
walk/copy/DB欄の対象ソース空欄を解消した。
`make check-map`はrc=0、114検査・漏れ0件。
`check_select.py --select --files exec/access_walk.c exec/redir_access.c exec/exec.c
kernel/kselftest.c fs/fd_redirect.c tools/tests/test_access_walk.py` で6系統を含む
21検査が変異込みに選択されることを確認。実未コミット差分で選ぶ場合も
walk/copy/DB/boot(memory)を選択する。未変更のcaller/redirect変異は対象試験で別途実施。

**小さなboot診断**: `test_ledger`の既存AS/2 data pageを再利用して
`test_caller_boot`をpost-exec毎bootへ結線。TRUSTED descriptor enter/get/leave、
2 byte cstr成功・cap1未終端拒否、2 byte copyout・NULL拒否、1枚のRO APP mapの
read PA一致・write拒否時PA不変、IF/CR3不変を10 checkで見る。
追加確保は疎APP PT **1 page**だけ、直後のAS destroyで回収し、既存owner0/
retire/selfcheckで取り残しを確認する。常駐BSS追加なし、USER ASのCR3ロードなし。
コピーは最大2 byte、walkも1 pageで、VFS/SQLite/callbackなし。
boot専用の時間/byte上限数値は票にないため、既存試験再利用・d枠内に収める解釈で実装。
同じ実helperをhost fixtureへ抽出してIF=0/1・frame復元を検証した。
**ゲストのkselftest 0 fail・実IRQ時間は未実施、PMが新kernel.mapで確認する**。
構成依存の確認は§12どおりT2hへ持越し。

| 同一cross toolchain実測 | d0b前ソース・ID長を同一化 | d5着地 | d6 (-dirty) |
|---|---:|---:|---:|
| kernel.bin | 359,432 B | 362,224 B (clean) | 363,220 B |
| vmkernel.lz4 | 477,999 B | 480,088 B (旧計測) | 480,683 B |
| 本体占有 | 569,804 B | 573,072 B | 574,064 B |
| __bss_end | 0x18B1CC | 0x18BE90 | 0x18C270 |
| ASSERT残り (596 KiB) | 40,500 B | 37,232 B | 36,240 B |
| d枠消費 / 残り | 0 / 5,888 B | 3,268 / 2,620 B | **4,260 / 1,628 B** |

d6本体増分は **992 B**。d0b〜d6増分はkernel.bin **3,788 B**、本体 **4,260 B**。
e〜h枠30,720 B消費後 **5,520 B**持越し (§6-1)。ASSERTの緩和なし。
サイズ比較の旧ソースは `git archive a64dd4e` を `/home/hight/os32-tmp/d6-before-d0b`
へ展開してkernelだけ再ビルド。最初のarchive既定ID `unknown` (7文字、clean相当)は
359,424 B / 0x18B1ACで、票の基準より整列込み32 B少なかった。
生成器の `--repo` を現worktreeへ指定して比較用IDを `bbabb6a-dirty` に揃え、
build_id.oを再生成して上表を実測した。**旧ソース像はサイズ比較専用で、基点の実行像ではない**。
基点実測と同一化実測を混同せず、現在像との差からdirtyの整列影響を除いた。

環境: PATHに `/home/hight/opt/cross/bin`、`CROSS_DIR=/home/hight/opt/cross`、
`TMPDIR=/home/hight/os32-tmp`、`OS32_MUT_JOBS=4`。
ILP32だけ既存 `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner` でqemu-i386を経由。
`python3 tools/tests/test_{access_walk,caller_copy,db_caller,caller_access,fd_redirect_d0a,exec_r1}.py
--mutate` は各rc=0 (32/18/15/24/24/24本runtime RED)。
`CROSS_DIR=/home/hight/opt/cross make all < /dev/null` はrc=0。
allの `NP21W_DIR=/home/hight/os32-tmp/d6-unused-image-destination` は存在しない
一時パスへ限定し、FDコピー警告を確認した。既存GNU-stack/RWX/Rust警告あり。
実NP21/W・NHD・配備・ini・commit/pushは未操作。親票/TASK_MEMMAP_V3/状態行は変更なし。
ログ: `/home/hight/os32-tmp/d6-{all,baseline,baseline-normalized,walk-final,retained,copy,db,caller,redir,r1}.log`。

**最終全体検査と補修**: 上記PATH/PYTHONPATHで
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` を**最後に1回だけ実行しrc=2**。
boot helperを追加したことに対する既存fixtureの追従を見落とした。
`check-memory-host` の `test_app_band_pde.py` がkselftestのledgerブロックを
広く抽出し、caller型/関数を持たない足場へboot helperまで取り込んでcompile失敗。
この失敗は変異REDに数えていない。残りの実行中ジョブの終了までソースを変更せず、
C方言27/27 RED・正常対照5/5 GREEN、P2V違反0件を確認した。
既存Windows opt-inは単独4件・集約5件skip。

終了後にAPP帯fixtureの抽出をledgerの確保/回収部分へ限定し、caller bootの
実helper/呼出だけを抽出対象から外した (判定の模型や素通しstubは作らない)。
実boot helperは上記walk試験で実行済み。製品コード/画像/確定sizeは変更なし。
補修後 `make check-memory-host MUT=--mutate < /dev/null` は上記環境で**rc=0**。
APP帯の正常対照+5/5 runtime RED (compile failures 0)、後続のledger/lease/R1/
backbuffer/gfxを含む停止したtargetを全て確認した。
`make check-map`、`python3 tools/gen_tests_inventory.py --check`、`git diff --check` はrc=0。
ログ: `/home/hight/os32-tmp/d6-check-changed.log`、`d6-memory-recovery.log`。
**ユーザーの「最後に1回」に従いcheck-changedは再実行していない。
対象失敗は修正済みだが、完了条件のcheck-changed rc=0は未達。PMの全体再確認へ残す。**
ゲストkselftest 0 failもPM確認待ち。状態行は変更していない。


**レビューの P3 と作業ツリーでの試験の失敗の対応 (2026-10-02、基点 `4d2294b`)**:

- 作業ツリーの `FAIL: boot caller` はホスト足場のスタック番地に依存した。
  `test_caller_boot` のローカル `src` / `dst` が Linux native i386 の高位スタックに
  置かれると、TRUSTED の `va >= MEM_APP_BAND_BASE` (`0x80000000`) 拒否に掛かる。
  以前の試験は `PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner` の qemu 補助を使い、
  qemu の低位スタックで通っていた。実ソース閉包は毎回一時ディレクトリへコピーして
  host GCC でリンクするため、OS の `.o` / 生成像 / `kernel.map` は入力でない。
  同じ閉包を qemu 上で高位スタック (`0xB0000000`) に置くと
  `caller:boot bounded cstr` で rc=1、通常の qemu スタックでは rc=0。
  ELF 内の低位 64KiB スタックへ entry で切替える修正を入れ、高位 entry stack の
  正常対照も常設した。切替命令だけを写しから除くと同診断で rc=1、修正版は rc=0。
  製品の TRUSTED 境界検査は維持した。失敗名も長さ0でなく全文を出力する。
  この sandbox では native `int 0x80` が SIGSYS (-31) なので、walk runner は
  **SIGSYS のときだけ** `qemu-i386` を明示的に再実行する。通常の失敗は再試行しない。
  補助 PYTHONPATH 無しでも本試験が通り、native の高位スタック位置にも依存しない。
- **P3-1**: missing NUL の部分コピーが残した `dst` を、copyout の直前に
  `{'x', 'y'}` へ置き直す。caller copyout の `kmemcpy` 削除変異を追加した。
  旧 boot helper では同変異が rc=0、新 helper では `caller:boot copyout` で rc=1。
  既存32本と合わせ **33/33 runtime RED**。compile失敗をREDに含めない。
- **P3-2**: boot helper の間は kernel PD、current slot/resource owner 0 とし、
  終了後に保存値へ戻す。IF=0/1 の保存とcaller frame復元を継続検査する。
  `registrant PD write` 変異は本来の
  `redir_access_check(&caller, MEM_EXEC_LOAD_ADDR, 1, 1)` で rc=1 となることを明示的に検査。
  CR3 の修正とスタック問題は別で、kernel PD だけでは高位TRUSTED bufferを救えない。
- **P3-3**: 上表のsid保存元を実在する `paging_lease_map` へ訂正した
  (`kernel/paging.c` の関数1353行・sid上限検査1364行・保存1412行)。

根拠ログ: `/home/hight/os32-tmp/d6-fix-evidence.log` (旧copyout変異生存、新copyout RED、
低位stack切替削除RED、高位entry正常対照、登録者write変異の失敗箇所)。
修正後 `make all` はrc=0 (`d6-fix-all.log`)。kernel.bin 363,220 B、
`__bss_end=0x18C270`、本体574,064 B、ASSERT残り36,240 Bは前回と同じ。
vmkernel.lz4は480,697 B。NP21/WへのFDコピー先は存在しない一時パス
`NP21W_DIR=/home/hight/os32-tmp/d6-fix-unused-destination` に限定した。
ゲストkselftest・NP21/W・NHD・配備・ini・commit/pushは未実施。

`make all` の終了後に、補助PYTHONPATH無しで
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
python3 -B tools/tests/test_access_walk.py --mutate` はrc=0 (33/33 runtime RED、
`d6-fix-walk-after-all.log`)。共通足場を使うcaller copyとDBも同じ環境で
`python3 -B tools/tests/test_caller_copy.py --mutate` / `test_db_caller.py --mutate`
が各rc=0 (18/18、15/15 runtime RED、`d6-fix-copy.log` / `d6-fix-db.log`)。
`python3 tools/gen_memmap.py --write`、試験一覧の鮮度確認、`git diff --check` もrc=0。


**今回の最終検査**: `PATH=/home/hight/opt/cross/bin:$PATH`、
`PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner`、上記の存在しないNP21W_DIRで
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` を**今回最後に1回だけ実行しrc=0**
(`d6-fix-check-changed.log`)。全体でもwalk 33/33、copy 18/18、DB 15/15 runtime RED。
他の既存i386足場にもsandboxのSIGSYS制限があるため、この全体検査だけ既存qemu補助を付けた。
Windows opt-inの既存skipは単独4件・集約5件。ゲスト/配備は依頼どおり未実施。
以前のd6で残したcheck-changed rc=0の未達は、今回の実行で解消した。

## 10-16. d6 の着地と T2d の完了 (PM、2026-10-02、NP21/W 17MB、main `3fafa7c`)

独立レビュー Opus 5.5 は P1・P2 なしで Approve (網羅性の要求つき — 仕分けた 11 本と boot 診断 10 項目を 1 本ずつ確認)。P3 3 件 (copyout の移動の確認、helper 中は kernel PD、票の関数名) と、**PM の取り込み前の検査で見つかった `test_access_walk` の失敗** — 足場が boot helper を Linux の高位スタック (0xFFxxxxxx) で走らせ、TRUSTED の境界 (< 0x80000000) で落ちていた。コーダーの sandbox とレビュアーは qemu-i386 (低位スタック) で動かしていたので通っていた。製品のコードは正しく、実機の起動時はカーネルスタック (0x2FC000〜) なので通る — はコーダー (sol) が足場を低位の固定スタックにして直し (`d1d5fa8`)、高位スタックで始まる場合も試験に入れた。PM のネイティブ実行でも PASS。

ゲスト (17MB、今の ini — §12): **kselftest pass 270 / fail 0** (d6 の boot 診断を含む)、`db_test` 9/9、`db_v50_test` 41/41、`klibc_test` 49/49、`alloc_demo` 16/16、`d0a_test` 全行 OK、faulttest 一式・V86・GUI (gui_demo → CUI) 従来どおり、取り残し 0、深さ 0。

**T2d (d0a〜d6) はこれで完了**。T2d 全体の増分 +4,260B、ASSERT 残り 36,240B、d の枠の残り 1,628B (e〜h を使い切った後の余白 5,520B の見込み)。構成依存の確認 (8MB・GFX 切替・音源) と Ra266 は §12 のとおり T2h で一括。次は T2e (gfx / 低位 USER の切替)。
