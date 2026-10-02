# TASK_T2D_T2H — T2d〜T2h 詳細設計

> 状態: **設計中 (2026-10-01)** — 実装前。独立レビュー Opus 5.5 は 1 回目 Request changes (P1 2件 / P2 11件 / P3 8件) → 反映 → **2 回目 Approve** (P3 5件は §11 の実装時の注記)。次は d0a (fd_redirect のゲスト再現) と ext2 調査票の x1 (並行)。
> 作成: GPT-6 / Codex。調査基点: main / docs/t2d-h-design 共通 **9ae6073406c2027fd50938e3870a3fb3888cd7f6**。
> 計画文書。下記のAPI名・内部構造・分割は実装契約案であり、未実装のものを現行仕様とはしない。
> 決定の正典は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) D1〜D36 と [TASK_T2_APPBAND](TASK_T2_APPBAND.md) §1〜§4・§6。本票は決定を変更しない詳細化。食い違いは§8へ。番地の定義は `include/memmap.h`、地図は [02_memory](../../02_memory.md) §2-1、ABIは `sdk/kapi.json`、規則は [CONSTRAINTS](../../CONSTRAINTS.md)。

## 0. 入口と作業の単位

T2a / T2a′ / T2b / T2c の実装結果、レビュー対応、PM受入は親票§5-1の各 **-R** 節を読む。T2cの「未実装」「停止」は履歴で、末尾のPM受入が現在地。NP21/W 8/17MBの受入済みを、Ra266や未実施ケースの合格に読み替えない。

本票の承認後に d0 (既存不具合候補の再現確認→修正) → d → e → f → g → h の順で進む。ext2調査をdと並行に先行着手し、hの受入前に調査ゲートを閉じる。既知のカーネル層不具合と確定した場合は [POLICY_DEV §1](../../POLICY_DEV.md) に従い新機能より修正を優先する。各小段は**実装・対象ホスト試験・報告を含め45〜75分を目安、最大120分**。75分で残作業を見積もり、90分で実装を止め、差分・失敗ログ・次の1ケースを記録する。120分に検査を押し込まない。表の小段でも超えるなら「失敗注入の半分」等へ分ける。着地単位と一回のコーダー依頼は別で、未使用helperの準備は個別レビュー可能、公開切替は依存小段を揃えて一組で受け入れる。

各段のチェック結線・全再ビルド・独立レビュー・PMゲスト受入も別の依頼単位にする。将来の実装完了検査は親票/ROLESどおり `make check-changed`、PM着地時 `make all` と `make check`。**今回の文書作業では§9の5検査だけ**。一時物とTMPDIRは `/home/hight/os32-tmp/` 以下。コード、配備、NP21/W、NHD、ini、commit/pushは今回の対象外。

| 作業 | 依存 / 並行できる準備 |
|---|---|
| d0a→d0b→d1〜d6 | fd_redirectのゲスト再現確認を先頭に置く。修正に必要な最小caller/walkはd0bへ前倒しし、dの新機能より先に閉じる |
| ext2調査 | [調査票](../../archive/v3/TASK_EXT2_ERRORS_INVESTIGATION.md)をdと並行。原因判定・必要な修正/再検証はh受入の前提。既知不具合化したら上記の優先順へ |
| e1〜e12 | d完了後。e10a (boot/SHM/閉鎖)、e10b (V86)、e10c (3段検査)を分離、全切替はe11 |
| f1a / f1b、h2 / h3準備 | eと並行可。toolchain台帳・adapter単体接続・fixture準備だけ。fの公開切替はe受入後、hの証拠は最終一式で再実行 |
| f2〜f13→g→h | f1bの成立とe受入がf公開切替の前提。h受入はd〜g・ext2ゲート・実機ゲートを照合 |

### 0-1. 調査で確認した実物 (上記SHAの file:line)

| 実物 | 確認したこと / 後続への意味 |
|---|---|
| `sdk/kapi.json:2`・`:3` | generations=形式4 / ABI1 / memory1 / shlib1、機能版69。T2c-RのPM決定1は着地済み。新しい正典を探したり別定義しない |
| `kernel/paging.h:187`、`kernel/paging.c:755`・`:775` | ASにAPP PT64 / lease PT56の控えとlease8本。素のcreateはPD1枚だけ。通常アプリは別のcreate_leaseを使う |
| `exec/exec.c:1899`・`:1900` | AS制御はkmalloc、通常起動はcreate_lease。先頭lease PTを二重に追加しない。PD/PTはKHEAP制御とは別会計 |
| `exec/exec.c:925`・`:937`、`:1030`のexec_stack_bytes | 実stack量と疎PT/lease PTを含む段選び。T2c-R修正を戻さない。暫定の旧byte予算と実起動必要ページ数は別 |
| `exec/exec.c:658`・`:860`、`kapi/kapi_db.c:210` | 早期pointer/読取範囲は主に帯判定、db_user_str_copyは1 byteずつそれを使用。B1の完全なPTE検査は未実装 |
| `exec/exec.c:817`、`kernel/paging.c:674`・`:689`・`:694` | 書込み検査はmaster往復。read/write別walkは既にあるが、PD/PTの管理下確認の強化とcaller保存はdに残る |
| `exec/exec.c:1473` | syscall入口でframe/WM深さを扱うが、B1用の保存caller記述子は無い。既存frame保存だけを新契約の実装済み証拠にしない |
| `kernel/paging.c:814`・`:840`、`exec/exec.c:1076`・`:1931` | private帯外で共有PTを書ける旧経路、BB/VRAM/font/Unicode/SHM/trampolineの起動時USER化が残る。eでまとめて閉じる |
| `gfx/gfx_core.c:120`・`:194`、`gfx/backend_cirrus.c:386`、`gfx/backend_pegc.c:694` | fbはkernel aliasを返す。SURFACE型板は4本、Cirrus DISPLAYはNONE。既存CLIENT backingは再利用する |
| `userland/lib/gfx/libos32gfx_core.c:25`、`lib/utf8.c:32` | SDKは旧fb取得と低位Unicode直読。C静的側とshlib側の保存状態は別 |
| `sdk/crt/syscalls.c:162`、`exec/exec_heap.c:20`・`:28`・`:65` | _sbrkは固定上限内、exec_heapは単一KHeap、親復元は再初期化しない。fでアプリ側だけ切替 |
| `sdk/rust/os32api/src/lib.rs:149` | Rustはmem_allocにsizeのみを渡し、Layout.alignを満たす処理が無い。fのalignment回帰対象 |
| `kernel/v86_mem.c:130` | teardownが低位を戻す。SHM/trampolineの実効PDE権限もeで復元。page0をNPへ変える段ではない |
| `exec/exec.c:1107`・`:2152`・`:2156` | exec_entry_callsは入口直前で増える。拒否試験は前後差分を測る (常駐shell初回拒否は0) |

行番号は調査時の目印。実装開始時に関数名と本文を再照合し、旧票の番地・行を機械的に流用しない。T2c-R末尾の実測はAS688B、AppSlot192B、管理合計7,488B、ASSERT残り40,500B。今回ビルドした値ではない。

### 0-2. 全段で固定する判断

- D35の4世代は上記JSONから全言語へ生成。追記APIの機能版は着地時の正典に対して上げ、番号の未来予約を本票で作らない。スロット整理はP7、構造体返却は使わずint/u32とchecked out。公開構造体はC89 [ABI1]〜[ABI3]。
- **ユーザー決定 (2026-10-02)**: T2e の query 等の公開 KAPI は **e11 でまとめて公開**する。e1〜e10 は既存経路につながないカーネル内部の準備として単独着地可。上の「追記APIの機能版は着地時」は T2e では **e11 の着地時**と読む。`sdk/kapi.json` の登録・機能版更新は e11 の全切替で1回、memory 世代更新も従来どおり e11。e11 の「単独で部分配備しない」は公開切替を指し、未接続の準備の着地を禁じない。
- T2d〜T2eのheapは**PM決定2の挙動維持**。旧1/2 PDE、旧物理上端、sbrk二段選択を残す。fの公開切替だけで撤去する。高位VAを旧物理予算へ渡さない。
- T2e前の共有USERは移行期間、最終隔離の合格ではない。低位の物理配置はeで動かさない。T2f前に初期heapを縮めない。
- 公開エラーは既存 `OS32_ERR_INVAL` (不正/権限)、`OS32_ERR_STALE` (-11、既存番号。識別可能な旧generation/失効token)、`OS32_ERR_FULL` (固定表)、`OS32_ERR_NOSPC` (物理/PT不足)へ対応させる。**OS32_ERR_NOMEMは現正典に無い**。内部LEASE_NOMEMと公開番号を同一視しない。mapはNULL、SDKはENOMEM。内部には物理不足/VA不足/表不足/不正の理由を残し、gの通知判定に使う。
- 公開契約を変えるeのUSER撤去、fのheap契約切替ではmemory世代をそれぞれ現値+1。gでGUI配送形式を変える際はshlib protocolを現値+1。形式幅/slot配置を変えない限り形式/ABI世代は据置。全消費者を生成・再構築し、manifestを一式にする。P7はこの正典を継承する。

## 1. T2d — B1 checked copy

### 1-1. 着手条件・範囲

T2cの修正版とPM記録を基点にする。変更は `exec/ring3_str.[ch]`、`exec/exec.[ch]`、`kernel/paging.[ch]`、`kapi/kapi_db.c` の既存checked入口、`fs/fd_redirect.[ch]` と保存/復元するexec/appslot、関連ホスト/小さなkselftest。SQLite engine、旧db_exec/db_prepareの全面移行、FEP facade、低位USER撤去、heap、lease公開APIは触らない。既存返却文字列scratch (`ring3_user_str`) の寿命は変更しない。

`fd_redirect`はT2c以前からの既存カーネル層の不具合候補 (ゲスト未再現)。登録は `fs/fd_redirect.c:106/119`、現在CR3の検査は`:236`、VA直接書込みは`:270`。`sh.bin`は`.bss`を登録 (`userland/shell/main.c:696/708`)して子をexec_runし、appslotのredir切替はpark時だけ。d0aで健全な試験媒体上の `ls | cat` と同一VA/別PFNの親子fixtureを観測し、親buffer/子heapの前後内容・CR3・killを記録する。再現しなくても候補をPASSとして消さず条件差を記録し、d0bで登録ASを固定する修正と回帰を先に閉じる。ゲスト操作は将来のPM/テスター担当で、今回は実施しない。

### 1-2. 実装契約

内部の `caller_access` は `{origin, app_id, owner, pd_phys, as, generation}`。originはUSER/TRUSTEDの列挙で、kernelが確定する。syscall入口で実CR3とslot/AS/ownerを照合して保存し、wrapper完了まで固定する。保存は呼出フレーム単位で、入れ子は前の文脈を保存/復元する。park/killのlongjmpで捨てられたstackへのpointerを残さず、launch/resume着地で無効化、次の入口で作り直す。WM enter/leaveはtrusted呼出区間を明示し、保存USER記述子を上書きしない。保存済みapp pointerをWMが扱う場合はそのUSER記述子を明示的に使う。現在CPL、pointerの低さ、master CR3だけでtrusted化しない。

内部API案 (戻り値1=成功、0=拒否。外部のrcは各wrapperが現在の規約へ変換):

```c
int copy_caller_cstr(const struct caller_access *c,
                     const char *src, char *dst, u32 cap);
int copy_caller_bytes(const struct caller_access *c, const void *src,
                       void *dst, u32 len);
int check_caller_write_range(const struct caller_access *c, void *dst, u32 len);
int copy_to_caller(const struct caller_access *c, void *dst,
                    const void *src, u32 len);
```

固定長ref/desc用のcopy_caller_bytesも同じread walkで全範囲確認してからkernel stagingへ写す。入力stagingをそのまま公開結果として返さない。

cstrはcapがNUL込み。cap0/NULLを拒否し、整数番地で加算前にoverflowを確認して、**その1 byteの権限確認→読取→NUL判定**。page末NULの次pageは調べも読まない。d5では同一IRQ保存区間内の確認済みページの権限・PAを再利用し、次ページはNUL未検出時にだけwalkする。失敗時dstは未完成stagingであり使用禁止 (dst不変までは保証しない)。trustedでも容量・NUL検査を行う。copyoutはlen0で書込みなし、NULL+非0拒否。srcはkernelで確定済みの非重複staging、ユーザー同士のmemmoveには使わない。

**今渡されたポインタ**のUSER walkは保存PD=現在CR3、現slotのAS/owner/generation一致を要求。**登録済みポインタ**は別契約で、登録時に `{app_id, AS, pd_phys, owner, generation, origin}` をkernelのredir記録へ値保存し、呼出フレームへのpointerは保持しない。generationはAS寿命の単調識別子 (slot/owner/PD再利用と区別、周回時は再利用拒否)。redirのnest/park保存・復元にも付随させる。使用時に生存台帳から登録者ASを引き直し、失効したAS pointerをdereferenceする前に同一性を検査する。登録者が死んだ/世代不一致なら失敗し、書き手の子を登録者と取り違えてkillしない。書込みは登録者PDをwalkしたPAへ `P2V(pa)` でpageごとに行う。現在CR3への一致条件は課さず、元のVAへ直接書かない。読取り側も登録者PDからcopyし、今渡された出力bufferとは別に検査する。容量/位置のoverflowとlen<=capacityを確認し、検査失敗ではデータ・位置不変。生存確認→全範囲walk→copyの間は短いIRQ保存区間でAS回収/切替を防ぐ。

両契約とも、PDはAS ownerの生きたPD、APP/lease PTは控えのframeと台帳owner、共有PTはmaster登録済みframeとの一致を先に確認してから読む。presentな任意RAMをPTとみなさない。PDEのPSを拒否、PDE/PTE両方PRESENT|USER、出力は両方RW。返ったPFNも私有RAM・SHM・shlib RO・RAM leaseの管理情報と突き合わせる。一般copyではMMIO/VRAMを拒否し、RAM RO lease/shlib rodataは入力だけ可。trampolineのRO+USERページ内の `ring3_user_str` scratchも、登録済みtrampoline backing/PDE/PTEを照合して**入力に許可**する (早期分類にも追加)。出力は拒否。次の文字列返却で上書きされる既存寿命を越えて保存しない。低位VRAMが暫定USERでもB1の例外にしない。

walkはT2cの低位恒等backingをP2Vで参照し、**dではmaster往復を除去**する。短いirq_save区間で範囲全体を検証してからcopyoutし、通常の検査失敗は出力全byte不変。どの出口も入口IF/CR3不変。複数出力を持つ入口は全出力範囲を先に検査してから書く。allocation/VFS/SQLite/callback/GUI pumpは区間外。boundedなDB/lease構造体用であり、無制限サイズを割込み禁止でコピーする入口を新設しない。

早期 `ring3_ptr_ok` はNULL・高位image/heap/stack/SHMと**現在ASの有効lease**の分類に拡張。穴の最終判定はwalk。dでは旧consumerのため既存低位VRAM分類を暫定維持し、eで消す。既存出力ガードはRW検査を弱めず新walkへ集約する。`_always` の保存済みapp pointer経路をtrusted扱いへ変えない。

DB接続は `kapi_db.c:512` / `:974` / `:1116` の3呼出箇所 (db_open / db_open_existing / db_prepare_only)とその補助だけ。copy失敗では既存rcを返し、SQLite入口カウンタ差分0、旧stmt/FDに副作用なし。B3/B4やFEP全体を安全化したとは報告しない。 **例外: `db_prepare_only` の旧 stmt はコピー拒否でも入口で finalize する** (公開仕様 KAPI_SPEC.md:1304-1306 を優先、§10-13)。

**d0a の試験と判定 (2026-10-01、ゲスト未実施)**:
`userland/tests/d0a_test.c` を既定の CPL=3 でビルドし、`userland/deploy.yaml` に
`/usr/bin/d0a_test.bin` として登録した [V2]。同一バイナリを親/子に使い、親の
volatile BSS 64B を 0xA5 で埋めて stdout に登録 → `exec_run` で自分を `--child`
付きで起動 → 子は引数の親VAと自身のBSS VAの一致と CPL=3 を確認し、同VAを
0x5Aで埋める → `sys_write(1, "d0a child stdout\n", 17)` → 子自身の64Bを
自己点検して終了 → 親は長さ17・payload全byte・未使用47Bの0xA5を確認する。
子の終了codeは0=値不変、10=値変更、11=write不完全、12=VA不一致、13=CPL不一致。
親はリダイレクト解除後に結果を出し、全正常なら0、それ以外は1で終了する。

PMは健全な試験媒体に今回のバイナリを反映したことを確認 [V1] し、8MB/17MBで
`/usr/bin/d0a_test.bin` を実行する (外側にパイプ/リダイレクトを付けない)。
正しい実装なら `d0a: parent_buffer=OK`、`d0a: child_value=OK`、
`d0a: child_status kind=1 code=0 rc=0 result_rc=0 bytes=17` が各1行。
**`parent_buffer=MISSING` と `child_value=CHANGED`、かつ
`child_status kind=1 code=10 rc=10 result_rc=0 bytes=17` の組なら、同VAの子を
書換えたという指摘が当たり、d0bの修正対象と判断する。** `MISSING`単独は
登録/継承/起動失敗などでも起きるので確定根拠にしない。子の異常終了/起動失敗/
自己点検未完は `child_value=UNVERIFIED` と終了状態を出し、CHANGEDと偽らない。
この場合は条件差として記録し候補を消さない。追加で CPL=3 の `sh` 内から
`ls | cat` を実行し、単独 `ls` と比較する。画面・buffer VA・新kernel.mapから
引いた親/子CR3・同VAの別PFN・kill差分もPMが記録する (本fixtureはCR3/PAを読む
公開口を追加しない)。ゲストの別PFN/CR3と画面結果は未確認であり、ホスト結果を
ゲスト再現として扱わない。

ホストは `tools/tests/test_fd_redirect_d0a.py` と `fd_redirect_d0a_host.c`。
実物 `fs/fd_redirect.c` をincludeし、Linuxの別memfd backingを同VAに順にmapして
上の登録→切替→write→自己点検→子owner回収→親復帰を模擬する。MMU/権限検査/VFS
境界は足場で、実exec/KAPI/CR3は検証しない。同AS正常対照はOK、親子ケースは
`MISSING/CHANGED` (親64B不変・子payload一致・残り不変) を観測した。
既定実行は契約に対してRED (rc=1)。全体checkには
`check-fd-redirect-d0a-host` の `--expect-known-bug` でこの厳密な失敗のみをXFAIL
(rc=0)として登録し、compile error/kill/別失敗/XPASSは失敗にする。
d0bでrecipeの同flagを外し、登録者ASのwalk/copy境界の足場を追加してGREENにする。

### 1-3. 分割・試験・受入

| 小段 (各45〜75分目安) | 成果 / その場で閉じる試験 |
|---|---|
| d0a | ゲスト試験 `d0a_test`・同VA別backingの実fd_redirectホスト試験を作成。§1-2の判定でPMが `ls \| cat`・同VA/別PFNを受入 (ゲスト未実施) |
| d0b | 登録者記述子とPA copyを最小実装。死んだ登録者/slot再利用/RO化/入れ子・park保存復元、子の内容不変。実装・ホスト結果は §10-3、修正後guest回帰はPM待ち |
| d1 | caller記述子と入口/正常出口。USER/trusted/入れ子とCR3不一致拒否。実装・ホスト・予算結果は §10-5、d2 の寿命配線は未実装 |
| d2 | park/longjmp/WMの寿命配線。実exec R1足場で古い記述子不使用。実装・P3対応・ホスト・予算は §10-7 |
| d3 | read/write walkと管理frame検証。RO入力成功・RW出力・PS/偽PT拒否。実装・ホスト・予算結果は §10-9 |
| d4 | cstr/copyout。page末NUL、次NP、未終端、overflow、IF両値、out不変。実装・ホスト・予算は §10-11 |
| d5 | 上記DB3入口と既存出力ガード接続。実wrapper→実copy、SQLiteは入口だけ記録。実装・試験・PM手順は §10-13 |
| d6 | 対象変異・結線・小さなboot自己診断、size記録を別依頼で確定 |

ホストは `test_ring3_str.py` / `test_ring3_guard.py` / `test_kapi_db_v50.py` と実paging/execの組合せ。読み側を真似た模型だけで済ませず、物理恒等と高位VAが異なるfixtureを使う。変異: master PDで検査、PDE RW無視、管理frame確認除去、NUL後先読み、事前strlen、無条件sti、WM復帰後trusted漏れ、redir登録PDを現在CR3へ置換、PA copyをVA直書きへ戻す、登録generation照合削除。各々狙ったデータ/順序assertでREDにする。

NP21/W 8/17MBで正常DB/FEP1語とGUI回帰、帯内だが次page NPの文字列を3入口へ渡し「明示失敗・SQLite進入0・次操作成功」。帯外pointerのdispatcher killは別ケース。Ra266はhの同試験へ束ねる。予算は§6のd枠。T4へcaller APIと未移行入口一覧、T5aへstaging/copyout契約を渡す。

## 2. T2e — gfx consumer移行と低位USER撤去

### 2-1. 着手条件・範囲

dのcopy契約とlease基盤がGREEN。変更は `gfx/gfx_core.c` / `gfx/backend_pc98.c` / `backend_pegc.c` / `backend_cirrus.c`、`exec/lease.[ch]` / exec / appslot、paging / v86_mem / `kernel/shm.c` / kselftest、`drivers/kcg.c` とboot順序/公開font入口、KAPI正典と生成、SDK gfx、`lib/utf8.c`、CRT、Rust guiのclient/shlib、gshell handler/multiapp、rshell tvdumpとguestマーカー/観測側。台帳のSURFACE登録補助は必要範囲だけ。新ドライバ・BAR sizing・planar BBのpool移動・Unicode組表化・page0 NP化はしない。

### 2-2. 公開APIと授権

親票§3のquery/lease/unleaseを追記する。公開descはu32のref、role/backend、format、width/height/pitch、planes数、plane offset[4]、bytes、最大access。phys/kernel pointer/cache指定欄は無い。kernel内の48B ledger_surfaceと公開descを同型にしない。queryは値の写しだけ、lease数を増やさない。

query/leaseの両方で次の授権表を適用する。Uは保存したUSER callerと**現在実行中**のslot/AS/owner/generationの一致、Gはkernelが読んだ `hdr_flags & OS32X_FLAG_GFX`、FはGUIの `appslot_gfx_owner()==caller.app_id`。親/park中ASに代理貸与しない。UnicodeはROだけ、他roleもdescの最大access以内。user引数のownerや単なるsidを権限にしない。

| モード | CLIENT | DISPLAY | TVRAM | Unicode |
|---|---|---|---|---|
| GUI (`gui_mode=1`) | U、選択backend。通常GUIにはG不要、全画面GFXならGかつF | UかつGかつF | 拒否 | U、backend非依存RO |
| CUI (GUI→CUIを含む、`gui_mode=0`) | U、選択backendの初期化済み面。G不要 (`libos32gfx_init→BB→gfx_present`を許可) | UかつG、CUI前景実行者のみ | U、CUI前景実行者のみ。G不要 | U、表ready時RO |
| gshell不在のboot (`gui_mode=0`) | 通常AS開始前のUSER要求は不可。開始後はUかつboot所有の初期化済み面、CUIと同条件 | 通常AS開始後UかつG | 通常AS開始後U、G不要 | 通常AS開始後Uかつ表ready、RO |

TRUSTEDのboot/kernel/WMはlease授権を迂回してUSERへ貸すのではなく、内部backing口を使う。caller由来は§1-2で固定し、CPL/現在CR3で推測しない。CUIで `g_gfx_owner` がWMのままでもUを直接照合する。GUI→CUIは旧leaseをrevoke/TLB同期してモード世代を更新してから前景CUIへ貸す。CLIENT backingの台帳ownerがgshellのままでも、物理所有と利用授権を分け、この表で貸与を許可する。gshell不在のboot ownerも同様。CUI→GUIでも貸与を切り替え、park中GUIの古いviewは再attachまで使用不可。

不連続planar DISPLAYは4 SURFACEをまとめて取得する内部lease_acquire(count=4)を使用する。公開側にはroleを指定する束取得口 `gfx_surface_lease(role, refs, count, access, out)` (queryが返す最大4本のref配列を入力、outは4 viewとcount)を追記し、任意物理やASを受け取らない。単一surface_leaseはCLIENT/Unicode/TVRAM等の単面用とし、planar DISPLAYの単面要求は拒否する。queryは束の全desc/refを一つのsnapshotで返す。leaseでcount・sid順・role/backendと**各generation**が現在の束に一致することを照合し、旧世代はSTALE、面欠落/重複/別束はINVALでout不変。4 slot/全PT/全出力を事前検査し全成功か全巻戻し。releaseは各tokenでよく、再init/終了revokeは全関連tokenを処理する。

refはB1でkernelへコピー、出力をB1で事前検査→授権/世代→slot/VA/PT準備→短いIRQ保存区間で全公開→copyout。準備からcopyoutまでAS scheduling/callbackなし。予期しないcopyout拒否も取得したleaseだけを解除してout不変に戻す。失敗で既存lease、master、他AS、token会計を変えない。公開rcは§0-2。token/generation周回拒否と8本/AS・16 SURFACEを増やして逃げない。

### 2-3. 面・alias・再attach

低位VRAMのmaster aliasがWB、SURFACEはUCというT2b-Rの差をここで閉じる。paging_initの初回表構築でnative VRAM aliasをUCにし (liveなWB aliasを後から変更する方式にはしない)、device窓既存aliasとlease aliasのcacheを台帳と一致させる。途中だけ異属性aliasを公開しない。RAMはWB。V86でこの初期設定を壊さないため、`v86_ident_map`のVRAM `[0xA0000,0xC0000)` とEプレーン行はsetup/teardownの**両方**に台帳cache由来のPCDを含める (§2-4、e10b)。ROM行に一律PCDを付けない。planarはpitch×heightでoffsetを再計算、200/400行をページ丸めのplane strideへ変えない。paddingは占有全ページ内でゼロ、他用途を同居させない。Cirrus CLIENTは非表示MMIO面、DISPLAYとページ境界で分離した既存backingを使う。

SURFACE容量は既存4本 + planar DISPLAY4 + PEGC DISPLAY1 + TVRAM1 + Unicode1 = 最大11本を設計上の勘定とする (無効候補のslotも数える)。planar CLIENTは4planeでも1本。通常GUIの2実体はCLIENT2 + Unicode2 = 最大4 lease、全画面planarのDISPLAY4を加えて8。さらにTVRAMも同時要求すればFULLが正しく、上限増設せず使い終わったものを解放する。boot自己診断の一時SURFACEは公開前に返す。

kernel描画は内部 `gfx_kernel_framebuffer` を使用。bb_b/r/g/iとbb[]、backend bb_baseはSURFACE backingから設定し直す。旧公開void gfx_get_framebufferはUSERにはleaseを返す橋 (ASあたり専用の互換CLIENT tokenを再利用、8本の内数)、失敗はsyscall安全点で明示終了。SDKの新checked attachは自分のtokenを保持し、繰返しattachではqueryの世代と比較して再利用する。新SDKは旧橋を呼ばないので二重取得しない。TRUSTED直呼びは内部fb、USERへkernel aliasを返すfallbackは禁止。

再init/所有者剥奪は貸与停止→関連全AS revoke→TLB同期→参照0→backend/geometry更新→新generation公開。失敗時に旧tokenを復活させず、予約済みfallbackを新世代で公開する。CLIENT backingのboot→gshell移譲は1回、GUI→CUI→GUIで物理を取り直さない。

SDK gfxのC静的側とshlib側の両方で、待ち/yield/resumeから戻るたび、最初の描画前に**query KAPI**でdesc/refのgenerationを照合し (SHMの可変公開値は正典にしない)、不一致なら旧pointerを消してchecked attachを行う。一致ならtoken/viewを再利用する。GUIイベントループだけでなく、全画面のsys_yield/入力待ち復帰と明示再initの帰路も対象。shlibには内部再attach入口を設けるならprotocolを更新する。attachが部分失敗したら今回得たtokenを全部返し、両実体とも描画不可としてエラーにする。描画途中でのcallback/再initを許さない。生pointerを保存して再initを跨ぐアプリは契約違反、VA再利用後まで検出できるとはしない。

Unicodeはkernel所有FIXED_RAM/RO SURFACE。ユーザー版utf8だけsetterでlease pointerを受け、CRTとshlib初期化の双方が取得/終了解除する。kernel版は恒等のまま。既知JIS対照は維持、取得失敗時はpointerをNULL、`jis_table_ready=0`にしてCRT/shlib初期化を続け、未ready時の既存代替表示を使う。未初期化/古い表は読まず、低位直読へ戻さない。T3のKAPI化でこの橋を撤去する (§7)。フォントはkcg_read系を使いfont帯全USERを消す。

`kcg_load_font`のLZ4 scratch (`drivers/kcg.c:215`、Unicode起点から最大344KiB)はUnicode表とplanar BBを重ねる。公開口を残したままeを受け入れると正規KAPIからSURFACEを破壊できるため、[T3 §3](TASK_T3_LAYOUT.md)の**boot後NOSYS化をe8bへ前倒し**する。D7は変更しない。boot内部読込だけを許し、scratch失効→Unicode表構築/検証→BB・mailbox初期化→SURFACE公開→通常ASの順を固定する。bootフェーズ終了後はTRUSTEDを含む全経路でNOSYS・VFS進入/書込みなし。公開KAPI slotは残し説明と版をeの一式で更新する。`font_load_test`はNOSYS期待の成功試験へ変更し、guest台本/host成功経路期待も同時更新。表/BB/mailbox不変はkselftest/host観測で確認し、受入から除外しない。scratch定数の整理とUnicode KAPI化はT3に残す。

### 2-4. USER切替の一括境界

まず新consumerを旧USERのある状態で試す。この途中結果は隔離合格ではない。全consumerが揃ったe最終差分で次を同時に行う。

1. `shm_init`がSHM状態/guardを初期化した**後**、最初のAS生成前に専用master初期化口でSHMをUSER|RW/WB、trampolineをUSER|RO/WBにする。boot専用初期化完了時のSHM契約はUSER|RWで、後続boot処理にsupervisorへ戻させない。既存の起動ごとの共有mapを撤去。
2. execのVRAM/font/Unicode/BB直接USER化とring3_ptr_okの旧VRAM例外を撤去。addrspace通常map/unmapは私有PTだけ許可、共有PTへの操作は無変更拒否。
3. masterの汎用map/set_page/map_physはlive AS後のUSER昇格を禁止。V86専用session口に加え、SHM専用権限口を**既存USERページのRW切替**として例外にする。`kernel/shm.c`のlockはUSER|RO、free/free_owned/cleanup_allはUSER|RW、初期化は1の順でUSER|RW、全てPCDなし。SHM範囲/PFN/共有PTを固定検査し、USER欠落は一般昇格で救済せず不変条件違反として検出する。PDE USERを維持、変更後active TLBを同期し、他ASは次回CR3 loadで同期。exec_exitのfree_owned後にも次アプリのSHM書込みが通ること。通常のDEVICE aliasはsupervisorのまま。
4. V86全出口で旧低位内容/属性を復元し、VRAM/Eのsetup/teardown両行のPCDを台帳と一致させる。SHM/trampoline用のPDE USERも復元→CR3 reload→通常地図検査。V86中の通常AS実行/生成/検査は禁止。T7の低位専有化やpage0 NPは前倒ししない。
5. tvdumpはCUI授権済みchecked copy KAPIへ移しTVDM wire形式を維持。nop/ring3_hello/fault/guardの観測マーカーは割当済みSHMへ (CRT非依存入口も同じ取得/初期化)。保護違反そのもののVRAM書込みは残す。db_v50_testのVRAM許可期待をRAM境界成功と未貸与VRAM拒否へ変更。
6. memory世代更新と現行ビルド対象の全再ビルド。apps/gameはユーザー決定により対象外。caller追随要件と再開時ゲートをhへ渡し、決定による持越しと記録する (§5/§8)。

### 2-5. 分割・試験・受入

| 小段 (各45〜75分) | 成果 / 閉じる試験 |
|---|---|
| e1 | 公開予定desc/エラー/授権とqueryの内部準備。偽owner/旧ref/GUI DISPLAY拒否。実装・試験・サイズ・単独着地の扱いは下記「e1 実装結果」 |
| e2 | 単面公開lease + copyout rollback。`walk_enter/leave`のmaster往復を除去、保存caller/管理PD/PTをP2VでwalkしIF/CR3不変。FULL/PT不足/out不変。実装・ホスト・予算は下記「e2 実装結果」 |
| e3 | DISPLAY4面束 + native UC。最後の面失敗で全巻戻し、cache全alias一致 |
| e4 | backend→kernel fb/geometry。実3backendの選択と200/400行 |
| e5 | 再init/revoke/fallback。旧token拒否、第三AS不変 |
| e6 | C SDK checked attach/互換void橋。反復取得/失敗時描画なし |
| e7 | shlib/gshell復帰配線。gfx両実体で再描画 |
| e8a | Unicode CRT/shlib/user utf8。日本語、RO出力拒否、取得失敗時ready=0 |
| e8b | kcg boot専用化とNOSYS。font_load_test/台本/host期待値、表/BB/mailbox不変 |
| e9 | tvdumpとSHMマーカー・観測側。wire不変、fault目的地一致、lock→free/exit→2本目アプリのSHM書込み |
| e10a | shm_init後のboot口・SHM権限口・起動時map撤去・汎用昇格禁止。lock/free/回収のUSER維持 |
| e10b | V86 sessionと全出口復元。VRAM/E両行PCD、PDE USER、失敗/STOP出口を確認 |
| e10c | master/AS/post-exec毎bootの3段検査。kselftest (c)とV86帰路でalias_cache一致、DISPLAY/TVRAM再取得 |
| e11 | 全切替・世代/生成/manifest確認 (前小段を統合、単独で部分配備しない) |
| e12 | 変異/サイズ結果と受入台本を確定、PM受入は構成ごと別依頼 |

実ソース `test_lease.py` / `test_gfx_boot.py` とSDK呼出しを連結し、backendを選ぶだけの模型で終えない。変異は旧bb pointer、plane stride丸め、片実体だけ再attach、GUI DISPLAY許可、UC落ち、共有PT書込み許可、revoke前free、V86後PDE USER復元欠落、teardownのPCD欠落、SHM lock/free/回収後USER欠落、束generation照合削除、fontのboot終了ガード除去。既存10 lease変異の意図も保持。

NP21/Wは8MB planar/PEGC、17MB planar/PEGC/Cirrus。日本語/描画/present、GUI→CUI→GUI、全画面DISPLAY、通常GUIの低位VRAMとdevice直書きkill、S/T/Uと片側revoke、cirrus-off強制指定fallback、V86復元後のalias_cache一致とDISPLAY/TVRAM再lease、SHMの2本目書込み、boot後font_load_testのNOSYS/表・BB不変を確認。ring3_guard旧Eの「低位BB生存」はここから**拒否へ更新**し、正規CLIENT leaseで生存する対照を追加。Bはshlib実ロード後にPTE P/U/ROかつPF error=7、Aは実stack直下NPかつerror=6を確認する。Ra266のPEGC/日本語/全画面とUCはhへ。予算は§6のe枠。

### e1 実装結果 (コーダー、2026-10-02)

モデル: GPT-6 (Codex)。基点 `531baf4`、worktree `wt/t2e1`。
`exec/surface_query.[ch]` に公開予定の値の記述子 (60B、4面の結果244B)、
既存エラーへの変換、共通授権、ref照合、B1 queryを追加した。
公開型はまだSDKに出さず、ポインタ/物理/owner/cacheを含めないu32欄だけ。
queryは参照数もASのlease表も変えない。

**今の挙動を保つ具体化**: e4/e5/e8のpublisherがまだ無いため、
内部だけの `surface_query_source` に選択backend・ready・role・順序付きrefを渡す。
ユーザー入力ではなく、将来のkernel publisherが呼出し直前に作る値である。
e1はpublisher、初期化/再init経路、KAPI呼出しを追加しない。
ready=0/未登録/NONE面は拒否。TVRAM/Unicodeはbackend=0、UnicodeはROのみ。
CLIENT/DISPLAYは選択backendと台帳のrole/backendを一致させる。
通常CLIENTは1記録 (planarでも4planeを内包)、planar DISPLAYのquery結果は4記録。
このsnapshot検査はe1の値の準備で、4面のlease取得・登録はe3へ残す。
CUI前景は保存callerとcurrent slot/resource owner/CR3が一致するRUNNING USERと解釈し、
WMの `g_gfx_owner` をCUIの授権条件にしない。物理ownerと利用者を分離する。
GUIではGFX宣言付きCLIENTとDISPLAYにFを要求し、通常GUI CLIENTにはG不要。
bootで通常AS開始前の要求、TRUSTED、WM代理、park中、失効callerを拒否する。
旧generation/closing/登録解除はSTALE、範囲外sid/面欠落/重複/現行の別束/権限違反はINVAL。
範囲内の未使用slotはSTALE (generation=0の入力はINVAL)。
入力の範囲/gen≠0/重複を先に検査する。sidが変わった旧refも台帳の
世代不一致/closing/npages=0ならSTALE、現行の別面が混ざればINVAL。
再initでsid不変とは約束しない。

**単独着地・配備**: 既存の呼出し経路に接続していないため、既存GUI/CUI/アプリの
挙動は変更しない。buildに追加した新TUはコンパイルされるが、未参照なので
`--gc-sections` が本体から除去する (map/nmで確認)。KAPI slot、機能版69、
形式/ABI/memory/shlib世代、SDKと生成物は無変更。ABI変更によるcleanや外部再ビルドは
e1では不要。e11で[ABI1]〜[ABI3]の生成・版更新・clean→allを行う。
これは未配備のコード経路上の判断で、ゲスト動作確認の合格ではない。

**e2以降への穴**: e2はUSER refsをB1でstagingへコピーしてから
`surface_query_refs` を使い、同じ保存caller/sourceで取得・copyout・失敗時rollbackを
行う。今のhelperはkernel staging専用で、任意owner/ASの公開引数を作らない。
既存leaseのmaster往復、枯渇理由の内訳、token失効は未変更。
e3のDISPLAY登録/束取得、e4以降の実backend/ready publisher、e5のモード切替と世代、
e8のUnicode/TVRAM登録、e11のKAPI wrapper/SDK公開は未実装。
sourceの生成からsnapshot/取得完了までcallback/schedulingを挟まない契約を引き継ぐ。

**試験**: `tools/tests/test_surface_query.py` は実query + T2d caller/copy/walk +
paging/pgallocを実行。MMU/IRQだけを既存host足場に置換し、低位固定64KiBスタックへ
切替える (高位entry stackの正常対照)。正常な台帳登録から作る4面、GUI/CUI×role×G/F、
3backend、ready/boot/gshell owner、偽caller owner/旧AS generation/別current/park/WM、
旧ref・最後の面・重複・RO、B1のNULL/overflow/RO/次NPとout不変、会計/IF/CR3不変を確認。
初稿版 (最初の独立レビュー前) はqemu-i386正常対照rc=0、
**22/22がコンパイル成功後のruntime RED**。今回のRequest changes対応版は
正常対照rc=0・**33/33 runtime RED** (明示qemu、PYTHONPATHなし)。
通常の実ソース閉包を一組で1回だけコンパイルし、変異は新TUだけを再コンパイル/リンク。
初稿の中央値0.23秒、最大0.28秒、全木コピー・変異ごとのmakeなし。
最初の足場は台帳createをUSER CR3のまま呼んでrc=1、登録時だけmasterへ戻して修正。
初回のTRUSTED拒否削除はB1の別防御で生存したのでREDに数えず、
共通ref授権への直接負例を追加して22本すべてruntime REDを確認した。
**Linux nativeはSIGSYS (signal 31、sandboxのint 0x80制限)で実行不可**。
qemuへの自動fallbackでnative合格に見せず、`--runner native` は明示失敗する。
PMが制限のないLinuxで **PYTHONPATHのqemu補助を外して** native正常対照PASS・
変異23/23 runtime REDを確認した (PM報告、最初の独立レビュー対応版)。
これは今回の33本の版とは異なる。旧版のnative/qemu両方PASSは達成済み。
今回の追加分のnative実行は未確認。ゲスト/構成依存は§12の方針を継承。

| 同一cross toolchain実測 | 基点clean | e1未コミット (-dirty) |
|---|---:|---:|
| kernel.bin | 363,216 B | 363,220 B |
| vmkernel.lz4 | 480,677 B | 480,697 B |
| __bss_end | 0x18C270 | 0x18C270 |
| 本体占有 | 574,064 B | 574,064 B |
| ASSERT残り | 36,240 B | 36,240 B |
| e枠の実消費 / 残り | 0 / 16,384 B | 0 / 16,384 B |

新objectはtext 1,255B・data/BSS 0B。未参照除去による実消費0を将来の無料実装と
扱わず、e11の接続時に少なくともこのtextと整列を再計上する
(1,255Bを見込んだe残枠は15,129B、接続時の実測は未実施)。
AS/AppSlot/SURFACEの常駐サイズは無変更。ASSERT緩和なし。

`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp
NP21W_DIR=/home/hight/os32-tmp/e1-unused-destination make all < /dev/null` は
基点・e1ともrc=0。FDコピー先は存在しない一時パスに限定し、コピー警告を確認。
通常のGNU-stack/RWX等の既存警告あり。実NP21/W・NHD・配備・ini・commit/pushは未操作。
検査を `build/sdk.mk` / `tools/check_map.yaml` / 生成 `docs/TESTS.md` に結線し、
`make check-map` は115検査・漏れ0件、rc=0。`gen_memmap.py --write` はrc=0で差分なし。
親票、TASK_MEMMAP_V3、状態行は変更していない。
ログは `/home/hight/os32-tmp/e1-{baseline,all,query,mutations,native}.log`。

**初稿版の最終検査 (過去の記録)**: PATHにcross/bin、既存のELF32用qemu補助
`PYTHONPATH=/home/hight/os32-tmp/d0b-host-runner`、上記NP21W_DIRを設定し、
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
make check-changed < /dev/null` を**最後に1回だけ実行しrc=0**。
`build/kernel.mk`のソース一覧変更で選択器はfullとなり、全検査・全変異を実行した。
初稿版のe1は22/22 runtime RED (全体並行時の中央値1.12秒・最大1.30秒)、
C方言27/27 RED・正常対照5/5 GREEN。既存Windows opt-inは単独4件・集約5件skip。
ログ: `/home/hight/os32-tmp/e1-check-changed.log`。
検査中はソース無変更、終了後はこの結果の記録だけを追記した。
**旧版のall/check-changedとnative/qemu両方PASSは達成済み**
(nativeは上記PMの実行結果。コーダーのsandboxでのSIGSYSとは分けて記録する)。

**e2への申し送り (e1レビューP3-3)**: `lease_acquire` はgeneration不一致を
LEASE_INVALで返し、`surface_query_error` はSTALEを作れない。
`surface_query_refs` は出口でIRQを戻すため、e2はrefs照合からacquireまで
同じIRQ保存区間に入れるか、acquire側でSTALEを区別すること。
`surface_query_authorize` の返すcallerも呼び手のIRQ保存区間内でだけ有効。
**e2への申し送り (e1の3回目のレビューN-1、P3)**: 'IF restore' の変異は`surface_query()`の出口だけに
絞られ、`surface_query_refs()`の出口 (`done:` の `irq_restore(flags)`) のIF復元を壊す変異が無い。
e2でrefsの出口を結線し直すときに、その出口に一意に当たる変異を足し、refsの後のIF確認でREDになることを見る。

**e1レビュー修正 (2026-10-02、基点6080946)**: P2-1は下位slotが空いた実台帳で
release→createしてsid変更を作り、旧束STALE・旧/現行別面の混在INVALを確認。
入力重複・範囲外sid・gen=0はINVAL (範囲内の未使用slotはSTALE)。P3-1のsystem role定数は`kernel/pgalloc.h`の
LEDGER_ROLE_*へ移動。P3-4のCLIENT/DISPLAY backend上下限、system backend≠0、
PEGC DISPLAY count=4の負例と変異を追加した。前回レビュー対応版は
qemu正常対照rc=0、28/28 runtime RED。今回版は5変異追加で33/33 runtime RED。
途中の変異1本は未使用変数のコンパイルエラーでREDに数えず、変異を直して再実行した。
P2-2は`HOST32_RUNNERS ?= native qemu`でT2d/T2eの4試験だけ全runnerを正常対照、先頭だけ変異とし、
access_walkも同じ方式へ変更 (qemu正常対照rc=0、33/33 runtime RED)。自動fallbackなし。
CIのaptへqemu-userを追加。CodexではHOST32_RUNNERS=qemuを明示する。

**main CIの赤**: d6ログはcase_mk_new_checkの`shutil.copytree`中に
`.git/objects/maintenance.lock`が消える競合で、case_mk_real_treeはPASS。
履歴を使う試験は既に自前fixtureなのでfetch-depth=1を維持する。
fixtureのcommit前にmaintenance.auto=false/gc.auto=0を設定し、global設定からの
隔離も回帰試験へ追加。depth 1 clone (`--is-shallow-repository=true`)は修正前25/25・rc=0、
修正後26/26・rc=0。コピー時のlock消失を決定的に注入すると修正前rc=1
(提供CIと同じENOENT)、修正後rc=0。
ログは `/home/hight/os32-tmp/e1r-{shallow-before,shallow-after,race-before,race-after}.log`。
取得したActionsログでもd3はcase_mk_real_tree、d5はcase_mk_negativeの同じlock消失、
d4は25/25 PASSが2回だった。失敗箇所が変わる競合で、d4では発生しなかった。
履歴不足による失敗ではない (run 36939542806 / 36953762147 / 36945743667)。

**3回目レビュー対応 (PMの範囲限定)**: 全runnerの正常対照は
check-access-walk-host / check-caller-copy-host / check-db-caller-host /
check-surface-query-host の4本だけ。既存試験はexportされた先頭runnerで1回実行する。
check.ymlは4本を呼ばないためqemu-user不要、build.ymlはqemu-userを導入する。
recipe末尾の`;`を入力抽出時に除去し、回帰試験とhost32.pyの入力globを追加。
kstring_cのILP32呼出しもhost32化。nativeは読めるbinfmt_miscに有効なi386 ELF登録があれば拒否する。
SIGSYSだけrunner不備、他signalは `signal N (runner=…)` と負の終了値で試験失敗を伝える。
closingのsnapshot側と別sid側は独立した変異にし、4試験の置換当たり数を固定した。
複数出口を意図的に壊すcaller_copy/db_callerの変異には期待数を明記する。
qemu正常対照の壁時計 (コンパイル込み、秒): access_walk 0.62、caller_copy 0.35、
db_caller 0.42、surface_query 0.41。変異は順に33/18/15/33本、すべて実行時RED。

**Request changes対応 (P2-A、P3-a〜h)**: caller_copy/db_callerにもrunner引数を
渡し、4試験とも正常対照はHOST32_RUNNERSの全runner、変異は先頭で実行する。
共通host32.py (walk.run_host32からも呼ぶ) はnative時にsubprocess.run/Popenの実装ファイルを標準ライブラリと
照合し、sitecustomize等による差し替えを拒否する (差し替え拒否の補助確認rc=0)。
PYTHONPATH除去で既存ILP32試験にもSIGSYSが見つかったため、既存ILP32試験の
実行呼び出し (b8のストリームPopenを含む) にも同じrunnerを適用した。
既存試験のrecipeは単純なpython3呼出しに戻し、正常対照・変異ともexportしたHOST32_RUNNERSの先頭だけで実行する。全runnerの正常対照は上記4試験に限定する。subprocess差し替えなし。
別sidのclosing=1/npages>0/世代一致と世代不一致を負例へ追加し、sid上限・gen=0・
世代一致・closing・npagesの単独変異を追加。sid変更のstale変異は分岐の1か所だけ。
旧sidの現行別面への再利用をCHECK(fourth == old.sid)で固定した。
maintenance回帰はGIT_TRACE2_EVENTでfixture commitの実行とmaintenance/gc子プロセス
不在を確認し、設定をcommit後へ動かす変異1本がRED。build_idの一時repoと
サブモジュールにもmaintenance.auto=false/gc.auto=0をcommit前に設定する。
HOST32_RUNNERSの運用はdocs/08_build.md §8-4に明記。生成器がMUTATE=1の条件分岐を
展開してdocs/TESTS.mdを再生成した (生成物の手編集なし)。

**e11への申し送り (e1レビューP3-g)**: STALEとINVALの違いから任意の(sid, gen)が
現行かを推測できる。lease取得にはsource一致も必要なので権限昇格にはならない。
e11の公開KAPI文書へこの情報の違いを注記すること。

**今回の最終検証と未達条件**: 全コマンドでPYTHONPATHを除去、
CROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、PATHにcross/bin、
NP21W_DIRは存在しない一時コピー先 `/home/hight/os32-tmp/e1rr-unused-destination`。
`CROSS_DIR=/home/hight/opt/cross make all < /dev/null` はrc=0 (最終allもrc=0)。
最初のenv起動はPATH空白の引用漏れでrc=127・make未起動、引用を直して実行した。
`python3 tools/gen_memmap.py --write` はrc=0・追跡差分なし。
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
HOST32_RUNNERS=qemu make check-changed < /dev/null` は2回ともrc=2。
初回は既存vmkernel_lz4/vk32_crc/shlib_high/hdd_stage2の直接native実行によるSIGSYS。
2回目は追加したhost32.py/test_host32.pyのcheck_map登録漏れ20件が唯一の失敗。
漏れを修正し、事後の `python3 tools/check_select.py --lint` は115検査・漏れ0件・rc=0。
全体検査は指定上限2回に達したので、修正後の3回目は未実行・追加承認待ち。
したがって**今回版の全体check-changed rc=0条件は未達**。
2回目の実行済みC方言検査は27/27 RED・正常対照5/5 GREEN。

事後の4試験一括検証 (`make check-access-walk-host check-caller-copy-host
check-db-caller-host check-surface-query-host < /dev/null`、明示qemu、補助PYTHONPATHなし)
はrc=0。正常対照PASS、walk 33/33・caller_copy 18/18・db_caller 15/15・
surface_query 33/33が全てコンパイル成功後のruntime RED (計99本)。
host32のrunner/差し替え拒否/signal回帰は5/5 PASS。
選択器の事後補助検査 `python3 -B tools/tests/test_check_select.py --mutate` は
rc=0、正常対照26/26 PASS・既存44変異＋maintenance順序変異1本がRED。
runner化により古いledger recipeの文字列を参照した実物fixtureが25/26 PASSで失敗したため、
同じ型の単純recipeが残るtime_mathへ試験対象を更新した (選択器本体の判定は無変更)。
補助check-fastはrc=2 (highramの直接実行とb8のstream Popenを補修)。
共通runner導入中のgfx変異でsignal判定の差を検出し、補助check-memory-hostはrc=2。
当時は今回4試験の全signalを拒否していた。3回目レビュー対応ではSIGSYSだけをrunner不備とし、他のsignalは負のreturncodeを返す試験失敗へ統一した (4試験の変異は引き続きFAIL出力が必要)。
b8補助再検査rc=0、gfx正常対照18/18 PASS・14/14変異RED・rc=0。
今回版のnativeはsandboxのSIGSYS制限のため未実行でPMへ委ねる。
commit/push・実NP21/W・NHD・配備・iniは未操作。
ログ: `/home/hight/os32-tmp/e1rr-{all-final,check-changed-final,four-final,
select-final-fixed,fast,legacy-fixes,b8,gfx}.log`。
(上の「未達」は2回目 (sol) の版のこと。3回目 (astra) の版はsandboxで明示qemuのcheck-changed rc=0、下のPMのnative/qemu検査で確定。)

**e1 の着地とゲスト受入 (PM、2026-10-02)**: レビュー (Opus 5.5) は 3 往復で Approve
(3回目の新しい指摘は N-1 の P3 だけ → e2への申し送り)。worktree の最終版 `f6228a2` で PM がホスト
(PYTHONPATH なし、既定 `HOST32_RUNNERS=native qemu`) で `make all` rc=0、`check_select.py --lint` rc=0、
`check-changed` (full) rc=0 — 4 試験は native と qemu の両方で正常対照が流れた。main へ取り込み (`e203f31`)、
コミット済みの木で `make all` rc=0・`make check` rc=0。NP21/W を停止 → 停止確認 → `nhd-pull` → `deploy-kernel`
→ `deploy` → 起動 (17MB、今の ini — §12)。`ver` の Commit `e203f31`、`/boot/vmkernel.lz4` 480,677 B が手元と一致。
**kselftest pass 270 / fail 0** (d6 と同数)、`db_test` 9/9、`db_v50_test` 41/41、`klibc_test` 49/49、`alloc_demo` 16/16、
`d0a_test` 全行 OK、faulttest gp/de/ud/pf は 4 件とも `-> kill app`、loop・kloop + CTRL+STOP、`v86 -t` OK、
GUI (gui_demo の窓 → ESC → CUI に戻る) OK。終わりのカウンタ: `fault_kill_count`=7 (faulttest 4 + STOP 2 + d0a の RO 子の
意図した拒否 1)、`ring3_caller_reject_count`=0、`redir_refuse_count`=0、深さ 0、`exec_as_leftover_pages`=0、
`irq_ctx_violations`=1 (起動時の基準値のまま)。e1 は未結線なので、ゲストでは回帰が無いことだけを見た。


### e2 実装結果 (コーダー、2026-10-02)

記名: Codex gpt-6-astra (利用上限で完了報告前に停止)、レビュー対応は gpt-6.1-sol。基点 `6a8aba9`、worktree `wt/t2e2`。commit/pushなし。
範囲は単面USER入口・B1・copyout rollbackと既存leaseのmaster往復撤去。
`surface_lease(source, user_ref, access, user_out)` をkernel内部に追加した。
sourceはe1と同じkernel publisher用の値で、USERからowner/AS/物理は受け取らない。
CLIENT/Unicode/TVRAMと非planar DISPLAYの単面を扱い、planar DISPLAY単面は拒否する。
4面束、publisher、公開KAPI/版/生成物/SDKは変更していない。

**入口・IRQ・rollback**: 保存USER caller取得→B1でrefをkernelへコピー→B1で
全outを事前検査→短いIRQ保存区間で共通授権/ref照合→slot/VA/PT準備→
既存pagingの短いIRQ保存区間で公開→B1 copyout。
準備からcopyoutまで通常文脈でAS scheduling/callbackを挟まない契約を維持する。
refs照合後の世代不一致/closing/失効はacquire側でも新しい内部 `LEASE_STALE` で区別し、
公開予定 `OS32_ERR_STALE` へ変換する (e1の申し送り1)。
copyout拒否はB1の全範囲preflightによってoutを一切書かず、今回のtokenだけをreleaseし、
追加PTを返却、未使用slotの旧内容と未返却tokenの採番を復元する。
既存lease、SURFACE参照数、master/他AS、固定上限8本/AS・16 SURFACEは維持する。
FULLはFULL、PT不足はNOSPC、現行の別束はINVAL。

`walk_enter/walk_leave` とmaster専用wrapperを撤去した。現在rootはmasterまたは対象AS
に限定し、保存ASの所有する整列済み管理PD/PTをP2Vで辿る。
pagingの書換え前にはlease PDEのPFN/権限/PSを管理表と一致させる。
読み側も対象PTを検査し、全56 PDEを各PTEごとに再検査する方式は避けた。
CR3の値は途中も不変。active ASのTLB同期には**同じCR3のreload**を残し、
PTE/PDE無効化→同期→PT/backing返却の順を保つ。入口IFは両値とも保存する。

**結線・予算**: 既存leaseの修正はboot/内部経路に入るが、新USER入口は未結線。
kernel全体はfunction-sectionsではないため、新入口を `.text.surface_lease` に分けた。
mapのdiscarded欄とnmで入口500Bとquery一式1,463Bの未参照除去を確認した。
分離前の試作ではqueryまで残ることを実測し、最終版では除去済み。
e1票の未参照1,255Bを無料扱いせず、今回の同一toolchainではquery 1,463Bと入口500B、
計1,963B以上 (整列別) をe11接続時に再計上する。
AS/AppSlot/SURFACEの常駐サイズは不変、ASSERT緩和なし。

| 同一cross toolchain実測 | 基点 | e2未コミット |
|---|---:|---:|
| kernel.bin | 363,216 B | 363,124 B (-92) |
| vmkernel.lz4 | 480,677 B | 480,619 B (-58) |
| __bss_end | 0x18C270 | 0x18C210 |
| 本体占有 | 574,064 B | 573,968 B (-96) |
| ASSERT残り | 36,240 B | 36,336 B |
| e枠の正味増分 / 残り | 0 / 16,384 B | -96 / 16,480 B |

未参照1,963Bを見込んだe残枠は14,517B (接続時の実測はe11)。

**試験**: 新 `test_surface_lease.py` は実lease/queryとT2d caller/copy/walk、
paging/pgallocを連結したILP32試験。MMU/IRQ、allocator枯渇とcopyout直前の
末尾PTE RO化だけを足場で注入し、B1自身の拒否と全out不変を確認する。
USER高位VAと物理backingを分け、低位fixture stackを使用する。
IF両値、RO入力/RW出力、CLIENT RW/Unicode RW拒否、planar単面拒否、GUI/CUI授権、
NULL/overflow/次NP、FULL/PT不足、旧世代/別束、既存leaseを持つ他AS、
管理PTのPS/所有者/USER/整列、追加PT付きrollbackと次tokenの連続性を確認した。
PT非整列の目的別負例は実 `lease_check` に対して修正前runtime RED (rc=1)、
整列検査追加後GREEN。kselftest追加はなく、既存lease boot自己診断を維持する。

qemu正常対照PASS。e2は**12/12 runtime RED** (直近の中央値1.64秒、最大3.57秒)、
e1 queryはrefs出口のIF復元変異を一意に追加して**34/34 runtime RED** (申し送り2)、
既存leaseは**10/10 runtime RED**。計56本。compile/link error・signal・timeoutは
これらのREDへ数えず、置換当たり数を固定し、理由を含むFAIL/順序違反出力を要求する。
新試験は正常objectを再利用し、変異TUだけを再コンパイル/リンクする。
旧10本のPT/backing早期返却変異は新しい同期順序に合わせて更新した。
途中の旧イベント数期待は正常対照でREDとなり、master往復を除いた順序へ更新した。
planar単面ガードだけを外す試作変異はquery側防御で生存したので、本数に含めない。
新検査をHOST32_RUNNERS全runnerの正常対照・先頭runnerの変異というe1と同じ形で
build/sdk.mk / check_mapへ登録し、docs/TESTS.mdを生成器で更新した。

**コマンド・ログ**: 全てPYTHONPATH空、TMPDIR=/home/hight/os32-tmp。
makeはstdin=/dev/null、CROSS_DIR=/home/hight/opt/cross、HOST32_RUNNERS=qemu、
NP21W_DIR=/home/hight/os32-tmp/e2-unused-destination (存在しないFDコピー先) を使用。
基点/最終版 `make all` はrc=0、FDコピー警告を確認。最初の基点allはフォント未取得で
rc=2だったため、既存mainの取得済みTTFをコピーして再実行した (新規同意なし)。
`python3 tools/gen_memmap.py --write` rc=0、`python3 tools/check_select.py --lint` rc=0
(116検査・漏れ0件)、`git diff --check` rc=0。
`make check-memory-host check-access-walk-host check-caller-copy-host MUT=--mutate` rc=0。
補助 `make check-fast` rc=0 (PT整列の最終修正前)。起動準備の1回はPATHの空白引用漏れで
rc=127・make未起動、環境指定を直して実行した。Windows opt-inは単独4件/集約5件skip。
ログは `/home/hight/os32-tmp/e2-{baseline,all,public,query,lease,related,alignment-red,check-fast}.log`。

**独立レビューとP3対応**: Opus 5.5 は Approve (P3だけ)。PMのホスト検査は
`make all` / `check_select.py --lint` / `make check-changed` (full、native/qemu) が全てrc=0。
実在ログは `/home/hight/os32-tmp/pm-e2-{all,lint,cc}.log`。
e2 12/12・surface_query 34/34・lease 10/10、計56変異がruntime RED。

今回の対応はP3-1〜P3-8だけ。rollback release失敗を
`lease_rollback_fail_count` (カーネル診断用のvolatile u32)で数え、通常は到達せず
残ったleaseを終了時revokeへ委ねる旨を記した。通常文脈・safe pointのみのAS回収、
保存caller.asの区間外使用、caller取得/B1 helperもIRQ保存すること、live再検査と
ABORT_PENDINGによるcopyout拒否→rollbackをコメントで明示した。
入口はB1読み込み前にIRQ/例外深さを拒否する。二重root述語は相互参照コメントで保守する。
全runner正常対照の正典一覧をsurface_lease込みの5本へ修正した。

**追加負例と変異**: lease_pt_phys[0]をずらしlease_acquire=INVAL、
paging_lease_pte=0を確認する。権限bitでずらしてPDE比較は一致させ、読み側は
ずれた読取先を非zeroにする。map側は使用PTの独立PFN検査でも拒否されるため、
未使用PDEのPTも同じ形でずらしlease_context全走査の整列検査を分離した。
初回map整列変異は生存 (runtime rc=0) し、未使用PT負例追加後にruntime RED。
rollback master hop、診断欠落、入口文脈検査欠落も追加し、新規5本、e2計17/17 runtime RED。
qemu正常対照PASS、中央値2.73秒/最大7.20秒。IRQ/例外入口のB1未読、
ABORT_PENDINGの実B1 copyout拒否と正常rollback、PDE破損によるrollback失敗の
カウンタ+1/lease保持/修復後revokeも確認した。置換は各1か所、compile/link失敗は数えない。
既存check-surface-lease-hostのcheck_map登録内で負例を拡張し、docs/TESTS.mdを生成器で更新。

**修正後のコマンド・結果**: PYTHONPATH空、TMPDIR=/home/hight/os32-tmp、
HOST32_RUNNERS=qemu、makeはCROSS_DIR=/home/hight/opt/crossとstdin=/dev/null。
NP21W_DIRは前回同様存在しないe2-unused-destinationへ向け、FDコピーは警告のみ。
`python3 tools/tests/test_surface_lease.py --runner qemu --mutate` rc=0 (17/17)、
`make all` rc=0、`python3 tools/gen_memmap.py --write` rc=0、
`python3 tools/gen_tests_inventory.py --write` rc=0、`python3 tools/check_select.py --lint` rc=0
(116検査・漏れ0件)。ログは `/home/hight/os32-tmp/e2-p3-{target,all,memmap,lint}.log`。
ビルド実測はkernel.bin 363,124 B、vmkernel.lz4 480,618 B、__bss_end=0x18C214、
本体573,972 B、ASSERT残り36,332 B (今回追加の常駐カウンタ4 B)。
未参照入口540 B+query 1,463 B=2,003 Bはe11接続時に再計上 (e残枠16,476 B、見込み14,473 B)。
修正後の全体検査初回はfull、rc=2。e2の読み側整列変異がqemu実行の10秒上限で
TimeoutExpiredとなった (REDには不算入)。実在ログ:
`/home/hight/os32-tmp/e2-p3-check-changed.log` (末尾FINAL_RC=2)。
残りの検査が終了してから、e2の実行物だけhost32.runで60秒上限へ変更した。
単体再確認はrc=0、正常対照PASS・17/17 runtime RED (中央値0.52秒/最大1.05秒)。
修正後の `make all` / memmap生成 / TESTS生成 / lint は再度rc=0。
ログは `/home/hight/os32-tmp/e2-p3-{target,all,memmap,lint}-final.log`。
最終の全体再検査コマンドは `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4
TMPDIR=/home/hight/os32-tmp HOST32_RUNNERS=qemu make check-changed < /dev/null`。
MAKEFLAGS='-j4 --jobserver-style=pipe'で全体並列数を4に抑え、初回のjobserver
FIFO衝突警告も回避する。再検査の結果正典は
`/home/hight/os32-tmp/e2-p3-check-changed-final.log` の末尾FINAL_RC
(この票を先に固定し、検査中・終了後は変更しない)。
補助check-fastがPT整列の最終修正前だった注記は上記のとおり維持する。
**最終の値 (PM、2026-10-02)**: コーダーの sandbox (qemu) の全体再検査は FINAL_RC=0、変異は e2 17/17・query 34/34・lease 10/10 が実行時 RED。
PM のホスト (PYTHONPATH なし、既定 `HOST32_RUNNERS=native qemu`) で `make all`・`check_select --lint`・`check-changed` (full) すべて rc=0、
全 runner の正常対照 5 試験は native と qemu の両方で PASS (`/home/hight/os32-tmp/pm2-e2-{all,cc}.log`)。
独立レビュー (Opus 5.5) は 1 回目 Approve (P3 8 件) → P3 対応の差分確認で Approve。
**e2 の着地とゲスト受入 (PM、2026-10-02)**: main へ取り込み (`b0d4d7e`、f1a と `build/sdk.mk`・`tools/check_map.yaml` が競合 → 両方を残して解消、TESTS.md は生成器で再生成)。コミット済みの木で `make all` rc=0・`make check` rc=0。NP21/W を停止 → 停止確認 → `nhd-pull` → `deploy-kernel` → `deploy` → 起動 (17MB、今の ini — §12)。`ver` の Commit `b0d4d7e`、`/boot/vmkernel.lz4` 480,603 B が手元と一致。**kselftest pass 270 / fail 0**、`db_test` 9/9、`db_v50_test` 41/41、`klibc_test` 49/49、`alloc_demo` 16/16、`d0a_test` 全行 OK、faulttest gp/de/ud/pf は 4 件とも `-> kill app`、loop・kloop + CTRL+STOP、`v86 -t` OK、GUI (gui_demo の窓 → ESC → CUI) OK。終わりのカウンタ: `fault_kill_count`=7 (e1 と同じ内訳)、`ring3_caller_reject_count`=0、`redir_refuse_count`=0、深さ 0、`ledger_*_ops`=0、`exec_as_leftover_pages`=0、`irq_ctx_violations`=1 (起動時の基準値)。`surface_lease` は未結線 (gc で除去) なので、ゲストでは `kernel/paging.c` の変更を含む回帰が無いことだけを見た。

**e3/e5/e10cへの申し送り (P3-7、記録のみ)**:
(a) lease_acquireは先頭の旧世代refでSTALEとなり後続INVALを見ない。
surface_query_refsは束全体を検査してからSTALEなので、e3の4面束で順序を揃える。
(b) rootはmasterか対象ASのみ。第三ASがactiveな間は他ASをrevokeできない。
e5の「関連全ASのrevoke」はこの制約を前提にする。
(c) exec.cのlease_revoke_all戻り値無視は未変更。厳しいlease_contextではPDE1本の
破損で全releaseが拒否されlease_countが残るため、e5かe10cで失敗を数える。
今回nativeはsandboxのSIGSYS制限で未実施 (PMに委ねる)。commit/push、NP21/W、
NHD、配備、ini、実機は未操作。公開KAPI・sdk/kapi.json・版は不変。

**未実施・申し送り**: nativeはsandboxのSIGSYS制限により未実施、PMのホストで確認。
NP21/W・実機・NHD・配備・iniは未操作。独立レビューとゲスト回帰はPMへ。
e3は4面束/全出力の巻戻しとnative UC、e4以降は実publisher、e5は再init/revoke、
e8はUnicode/TVRAM登録を担当する。e11はKAPI/SDK/版の一括公開と接続時サイズ測定、
STALE/INVALが示す情報の違いの公開文書化を引き継ぐ。


## 3. T2f — map/unmapとallocator、暫定heap終了

### 3-1. 着手条件・範囲

eの隔離・両gfx実体・B1が合格。新 `exec/appmem.[ch]`、paging、exec/appslot/exec_heap、SDK CRT/allocator、Rust allocator、KAPI生成、mem表示と試験を対象にする。汎用KHEAPを伸長しない。resident shellの452KiB exec_heapと固定sbrkを維持し、T3の256KiB分割を先取りしない。trim通知配送はg、fはtrimの安全な返却部品まで。

### 3-2. extentとmap transaction

ASに32本の `{u32 base,end,kind,flags}` (16B)を固定追加。空slotはbase=end=0、管理はkernelのみ。kindはLIBC_INITIAL / EXEC_INITIAL / ANON / EXEC_ARENA / EXEC_LARGE等の内部識別、公開引数にしない。公開mapはANON。隣接同kind/flagsは併合するが、EXEC_LARGEの割当識別は保持する。PFNの正典はPTE。公開unmap対象kindは**ANONとLIBC_INITIALだけ**。EXEC_INITIAL / EXEC_ARENA / EXEC_LARGEは全て拒否し、返却はkernelのowner検証付き内部口だけ。image/stack/shlibも公開unmap対象に含めない。libc初期量のうちBSSと同居する端pageはimage所属のまま、独立したheap pageだけをLIBC_INITIALとして返却可能にする。

map(bytes,hint,flags)は0、EXACT、TOPDOWN、EXACT|TOPDOWNを許可 (最後はEXACT優先)。NULL hintは希望なし、EXACT+NULLは拒否。非NULL hintはpage整列かつアプリ私有利用帯内、bytesは加算前overflow検査して切上げ。未知flag/size0/帯外は拒否。flags=0は `[page_align(image+BSS end), 0x88000000)` (exec_heap予約起点未満)の**上端側から下向き**に穴を探し、primary _sbrk直上を先に塞がない。TOPDOWNはstack guard直下からexec_heapの現在端より上の穴を下へ探す。hintが有効で空なら先に採用、EXACTの衝突は別穴へ逃がさない。image/BSS端page、初期heap、stack/guard、shlib、leaseを除外し、低位/未使用256MB超へ広げない。exec_heap伸長も現在端のEXACTが第一候補、衝突時は別arena。予約はVAの排他境界で物理先取りを意味しない。

順序は引数/呼出元→穴→併合後slot数→必要PT→全data page確保/ゼロ→公開。pendingのPTは最大64本の控え (256B)を**非再入map transactionのkernel stack上**に置く。ASにもAppSlotにも常駐させず、呼出stack high-waterの予算/実測へ含める。dataは予約した空PTEにPFNを**PRESENTなし**で記録し、失敗時の返却リストとする。live PTへ置く場合もPRESENTなし・AS更新中で、callback/AS切替なし。既存PTE/未使用slotの初期値を前提にして撤去範囲を確定、既存の有効entryを上書きしない。巨大PFN配列・追加rollbackページを確保しない。失敗はpending PFN全返却→今回PT全返却→予約slot解放で元通り。公開時だけPRESENT/USER/RWとextent/heap端を一括確定、active CR3再ロード。IRQ禁止は公開の短い区間、確保/ゼロ化では解除する。IRQ/例外からallocatorを呼ばないR1を維持。

unmapはbytes page倍数、base整列、overflow/全範囲のextent種別 (公開口はANON/LIBC_INITIALのみ)/連続被覆/PTE ownerを先に検査。内部exec_heap返却口は別でEXEC_*と所有者を検証する。一つの穴・image・stack・shlib・leaseを含めば全拒否。中抜きは最大2残片を計算してslotを事前確保、足りなければFULLで全不変。複数extent跨ぎは全て同じ検査を通す。全対象NP→active CR3 reload (非activeは次回load保証)→PFN owner free→空APP PTをPDE NP/TLB同期後返却→extent残片確定。先頭lease PTは触らない。freeに必要なPFNはNP PTEのframeに保持し返却後ゼロにする。owner不整合は検査段で拒否/診断し、途中まで返して成功しない。

### 3-3. 最小起動と_sbrk

fの公開切替でのみ、未指定exec_heap=64KiB、明示値は丸めて最低64KiB、libcはBSS端の端数+初期追加1 page。stackは既存可変量を全map。`exec_ring3_pages`は使用PDEの集合 (重複を一度だけ) + PD + lease先頭PT + shlib data + 実data pagesで必要量を求める。固定KHEAP不足も起動失敗として全返却する。旧物理上端0x00C00000制限・折半・sbrk tierをこの時に撤去し、64MBの旧上限超heapはVA/実freeが足りれば通す。物理不足を別の起動予約で救済しない。

CRTをUSER/residentでビルド時に分ける。USER _sbrkはu32番地でsigned incr/INT_MIN/上端を加算前に検査し、breakとmapped_endを別に保持。増加は必要なpageだけ末尾へEXACT map、全成功時だけbreakを更新し旧breakを返す。減少は初期break未満拒否、breakだけ下げる。mapped_endはtrim成功時だけ下げる。非連続値を成功として返さない。resident版は既存固定上限方式 (境界/overflow検査は同じ)で、mem_mapへ落ちない。`sbrk_heap_limit`はUSERでは初期mapped_endの引渡し値であり、実行中の予約天井と解釈しない。SDKが自分のmapped_endを持つ。親CR3復元後に親heapの状態を復元し、初期化し直さない。

### 3-4. allocatorの選択と安全な返却

**選択案はnewlib nano維持 + SDKの複数arena接続**。TLSFを同時導入しない。調査した実ソースはnewlib 4.4.0.20231231のnano-mallocr.c (`/home/hight/opt/src/`、リポジトリ外)。free_list、sbrk_start、sbrk_alignedの追加整列要求、末尾free chunkとの隣接検査を持つ。f1aで実toolchainのリンク対象/hashを確認し、違う版ならその差を記録して接続試験を直す。システムのlibc.aを手編集せず、SDK用のnanoビルド/adapterを用意し、由来・ライセンス・パッチをビルド入力として追跡する。

小要求はprimary nano (_sbrk EXACT)へ。連続伸長が失敗したらSDKがflags0 mapで別arenaを作り、**arenaごとのnano状態** (free_list、break、mapped_end、sbrk_start、統計)へ切り替えて再度割り当てる。arena内MORECOREは必ず連続で、追加整列要求も同じarena内から返す。単一のfree_listに別arenaを偽装連結しない。arena記録は各arenaの先頭管理pageに置き、SDKのlistはユーザー領域。完全に空ならlistから外す準備→unmap→成功確定、失敗なら元のlistへ戻す。初期BSS共有pageは返さない。

Cのmalloc/free/calloc/reallocと `_malloc_r` 等reentrant入口の全てを同じadapterへ結線する。alloc_sizeでなく**要求サイズ>=65536**はTOPDOWN直接map。prefixに種別/base/map bytes/requested/alignmentを保持、freeは全mapを即unmap。小→大/大→小reallocは新確保→必要量copy→旧free、失敗は旧内容保持。callocの積とprefix+alignment+page丸めを別々にoverflow検査。通常mallocのABI整列と強い整列の余白をmap内に収める。Rustの接続先は次段落のmem_alloc整列adapterに固定する。size0の既存C/Rust契約をそれぞれ保ち、公開mem_map(0)拒否とは分ける。

Rust (`sdk/rust/os32api/src/lib.rs:149`)は**newlibをリンクせずmem_allocに整列adapterを被せる**。CPL=3 Rustアプリ・libos32gui.shlib・CPL=0 resident gshellで同じGlobalAllocを使う。Layoutのsize/2冪alignを検査し、実効align=max(Layout.align, prefixの整列)として `size + 実効align - 1 + prefix` のoverflowを検査してraw blockを確保し、範囲内で整列したpointerと元baseをprefixに保持、deallocは元baseをmem_freeへ渡す。reallocは新確保→copy→旧free、失敗は旧内容保持。prefixも整列させる。65536のkernel側分類はmem_allocに渡る実byte数で、Rustの余白込みなら閾値を跨ぐことを試験で明示する。C nanoは要求サイズで分類する。size0はRustの既存契約を守る。

resident/USERの振り分けは**kernelが保存したcaller由来**で行い、ビルドfeatureや現在CR3/g_cur_appで推測しない。USER由来のapp/shlibは当該ASのexec_heap、WM/TRUSTED由来のgshell/shlib呼出しはresident heap。USER syscall内にWMが入っていてもresident側に割り当てる。freeも由来/所有者を検証し、両heapを跨いで渡さない。Layout.alignの既存不具合候補はこの実接続上で4/8/16/64/4096整列・overflow・失敗・解放と3種類の利用者を試す。Rust trimはnanoを呼ばずTRIM_DONE帰路のexec_heap trimを使い、任意cache hookとbusyは同じ通知規約に従う。

nanoのtail trimは実free list上で末尾のfree chunkを確認し、header/次参照に必要なpageを保持する。リンク/size/breakの更新案を控え→末尾の完全なpageだけunmap→成功確定、失敗で旧状態。空の副arenaは全返却。trimはallocator busy中に呼ばず、別arenaの生存chunkを読まない/動かさない。接続試験は実nanoをリンクし、malloc→穴→整列追加→EXACT失敗→別arena→realloc→trim→再割当を通す。

既存mem_alloc/exec_heapはkernel側のAS別arena管理で同じappmem実体を使い、64KiB未満はKHeapのarena、以上はTOPDOWN。resident用の単一KHeapを維持し、選択は上記caller由来で固定する。USER arenaのbase/sizeはextentから導出し、usedは検証済み走査で算出する一時KHeap viewを使う (32本分のKHeapをASへ重複保持しない)。集計値と現在arena索引だけをAS制御へ置く。アプリのBlkHdrは改竄可能なので、kernelが辿る前に所属extent、alignment、size加算、次ポインタ/ブロック終端の単調性とPTE/ownerを検査。破損時はそのappの明示失敗/通常回収へ、他ownerへfreeしない。負例は二つに分ける。公開unmapのEXEC_*指定は全拒否/metadata不変。USERが書けるBlkHdrの直接改竄は次のalloc/free/trimで検出し、他ownerを変えない。汎用kheapの信頼済みkernel顧客へこの検査を一律に広げず、USER arena用の入口で行う。exec_heap trimは同じ安全検査後の末尾freeページのみ、kernelがユーザー申告の「free量」を信用して返却しない。

### 3-5. 分割・試験・受入

| 小段 (各45〜75分) | 成果 / 閉じる試験 |
|---|---|
| f1a | toolchainの実nano入力/hash・リンク対象と由来/ライセンス/パッチのビルド台帳。実装・対象host検証済み (2026-10-02)、全体検査は下記記録 |
| f1b | adapter最小接続。整列追加/末尾拡張の実ソース対照、resident/Rust接続の分離 |
| f2 | extent/穴探索/flags。両端/overflow/EXACT/hint衝突、固定32本 |
| f3 | map準備/公開。data各枚・PT各枚不足の全rollback |
| f4 | unmap/部分分割/併合。FULL不変、NP→TLB→free、別owner拒否 |
| f5 | public KAPIとcaller接続、kselftest owner往復。機能は準備のみ |
| f6 | USER/resident CRT、_sbrk EXACT/負増分とmapped_end |
| f7 | nano副arena/通常malloc・reentrant入口。非連続成功と連続性保持 |
| f8 | 大塊/calloc/realloc/整列とRust結線。65535/65536/65537の3値 |
| f9 | exec_heap小arenaと親保存/復元。公開EXEC_* unmap拒否と改竄header検出で他owner不変 |
| f10 | exec_heap大塊と安全なtrim。空末尾/空arena/生存データ保持 |
| f11 | nano trim。失敗rollback、再割当の実ソース試験 |
| f12 | 起動予算・旧helper撤去・最小初期量/世代の一括切替 |
| f13 | mem表示・変異結線・size/manifestとPM台本を確定 |

**f1a 実装記録 (2026-10-02、Codex gpt-6.1-sol、wt/t2f1、基点6a8aba9)**:
範囲は実toolchain台帳と検査・CI結線だけ。選択は§3-4のnewlib nano維持。
eのlease/gfx/SHM権限/KAPI・公開SDK・`sdk/kapi.json`・機能版/4世代は変更しない。
入力の正本は [`sdk/allocator/nano_inputs.json`](../../../sdk/allocator/nano_inputs.json)、
配置理由・実リンク・CIの扱いは [08_build §8-5](../../08_build.md)、
試験記録は [`nano_inputs_tdd.md`](../../../tools/tests/nano_inputs_tdd.md)。

- 実版newlib 4.4.0.20231231 / GCC13.2.0、`-g -O2`。
  `build-newlib-nano/i386-elf/newlib/config.log` の4フラグは§8-5と一致。
  関連ソース25ファイルは公式tarballとhash一致、追加patchは無し。
  `libc_nano.a` / `libg_nano.a` は無く、nano構成の通常名 `libc.a` / `libg.a` は同一hash
  `d7a35f3fe99d533d16dca01694a4effd7a149dd7367e610de03e8ae5441e83d8`。
  実 `-lc` はcrossの `libc.a`、提供object/hashは台帳。計22 memberを記録 (当初9 memberは実build objectとbyte一致)。
- f1bへ: ソースは `/home/hight/opt/src/newlib-4.4.0.20231231/newlib/libc/stdlib/nano-mallocr.c`。
  行107–109は `free_list`→`__malloc_free_list`、`sbrk_start`→`__malloc_sbrk_start`、
  `current_mallinfo`→`__malloc_current_mallinfo`。定義は198/201、統計はDEFINE_MALLINFO区間。
  行117–119はMALLOC_ALIGN=8、CHUNK_ALIGN=sizeof(void*) (ILP32では4)、padding=4。
  `sbrk_aligned` 行209–231はsbrk_startを_sbrk_r(0)で保存→要求量取得→
  CHUNK_ALIGNへ切上げ→不整列時は行226で追加 `_SBRK_R(RCALL align_p - p)`。
  後半要求の失敗時に先の取得量を自動巻戻ししない点も接続試験に含める。
  末尾隣接検査は行326の `(char *)p + p->size == (char *)_SBRK_R(RCALL 0)`、
  行330で不足分だけ要求、行332成功後に334でsizeを増す。
  別arenaを同一free_listへ連結せず、状態/整列の追加要求もarenaごとに扱う。
  `#ifdef _LIBC` は行54、入口renameは69–80の全12入口、`#else` は82。
  各DEFINE_* wrapper→`_mallocr.c`→nanoという入力。
  ARM Ltdの表示は `sdk/allocator/nano.LICENSE` に保持。adapter実装・patchはf1bで追記する。
- 再レビュー対応 (N1〜N5、2026-10-02): cache_keyにlocal.members/local.archivesの
  ソート済み名前だけを追加。hash値・SDK入力・symbolsはキーから除外する。
  CIはchecker出力を独立した代入で受け、rc=1ならbash -eで停止してkeyを出さない。
  receipt builder不一致には再記録/再構築の復旧路を付記。builderはreceipt/cache_keyとも
  ファイル全体SHA256で比較 (正規化なし)、手作りtoolchainでは名目上の記録であることを§8-5に明記。
  receipt absentの案内はcompareのhash差にだけ付け、inventoryの提供元/dlmalloc/map/GCC失敗には付けない。
  nanoのlicenseは現行toolchainの責務なので台帳のtoolchain節へ戻した。
- 対象host試験: 名前追加時のキー変化/hash値変更時の不変性、CIのrc=1停止、
  builder復旧案内、inventory失敗の診断を追加。50ケースGREEN、25変異は実行時RED、
  生き残り0 / ERROR0、対象hostコマンドrc=0。
  `CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp PYTHONPATH= HOST32_RUNNERS=qemu
  python3 -B tools/tests/test_nano_inputs.py --mutate`。
  i386リンクprobeはld -rのmap検査のみ、ILP32実行無し。置換当たり各1、実行時REDだけを数える。
  ログ: `/home/hight/os32-tmp/t2f1-rereview-nano.log`。
- 今回の全体ビルド: `CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp PYTHONPATH=
  HOST32_RUNNERS=qemu make all < /dev/null` rc=0。
  既存image.mkによるNP21/W宛の自動cpが失敗 (警告) した。配備先の変更成功は無し。
  今後の再ビルドはコピー先を隔離する必要がある。ログ: `/home/hight/os32-tmp/t2f1-rereview-all.log`。
  `python3 tools/gen_memmap.py --write` rc=0 (追跡差分無し)、
  `python3 tools/gen_tests_inventory.py --write` rc=0、`python3 tools/check_select.py --lint` rc=0。
  試験はcheck_mapと生成TESTSへ登録済み。
- 最終全体検査は本欄を固定後、
  `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
  HOST32_RUNNERS=qemu PYTHONPATH= make check-changed < /dev/null` を実行する。
  実行中・終了後は票/ソースを変更せず、終了rcは完了報告と
  `/home/hight/os32-tmp/t2f1-rereview-check-changed.log` に残す。
- 未実施: GitHub Actions新規toolchain構築/receipt生成、native ILP32 (PM担当)、
  修正後の独立再レビュー、adapter接続/arena/trim (f1b以降)、NP21/W/NHD/ini/実機操作。
  意図した配備・commit/pushは無し。実config/sourceによる隔離prefixへのreceipt生成は前回rc=0。
  receiptは署名/手元とのbyte再現性の証明ではない。初回build.yml runでCIを受入し、f公開切替はe受入後。
- f1bへ追加申し送り: `sdk_build.sources` にはtoolchainのupstreamと同じ照合を足すこと。
- f1bへ追加申し送り (f1a の再レビュー P3、2026-10-02): (a) receipt を持つ手元の toolchain は、台帳 `local.members` に名前を足すと `members SHA256 differs` で案内なしに落ちる (CI はキーが変わり作り直される) — 名前の集合が違うときの案内か §8-5 への 1 行を足す。(b) `build_inputs()` が receipt に必須キーを足すとキャッシュ済み receipt が KeyError になりキーも変わらない — receipt の形を変えるときに `RECEIPT_SCHEMA` 定数を receipt と `cache_key` の両方に入れ、check で一致を require する。
- **f1a の着地 (PM、2026-10-02)**: 独立レビュー (Opus 5.5) は 3 往復で Approve (1 回目 P2 6 件、2 回目 N1 の P2 1 件、3 回目 P3 2 件 → 上の申し送り)。PM がホスト (PYTHONPATH なし、既定 `HOST32_RUNNERS=native qemu`) で `make all`・`check_select --lint`・`check-changed` rc=0。カーネル・userland の実行物は変えていないのでゲスト回帰は不要と判断。受入の条件の初回 build.yml run (toolchain の新規構築と receipt) は push 後に確かめる。
  `_memalign_r` / memalign / aligned_alloc、`_valloc_r` / `_pvalloc_r` /
  valloc / pvalloc、`_malloc_usable_size_r`、`_mallopt_r` / mallopt / malloc_stats、`_cfree_r`、
  `_mallinfo_r` / `_malloc_stats_r` / mallinfo / malloc_usable_size も同じfree_list系の入力として台帳に含める。
  valloc系reentrantの提供元はvallocr.o / pvallocr.o、wrapperはvalloc.o。
  未結線入口の選択肢は (A) リンクされたら失敗、(B) adapterへ落とす。
  **推奨はA**: 一部だけ元のlibc状態へ落ちるとarenaが混在するため、f1bで結線しない入口は
  リンクされたら失敗にする。整列・統計・usable-sizeを含め全arena状態への動作が定義/試験できた入口から
  Bに替える。これはf1bの受入方針の申し送りで、f1aでは公開リンク挙動を変更しない。
  `_SBRK_R` 行52→`_sbrk_r` (libc_a-sbrkr.o)→U sbrk→SDKの
  `sdk/crt/syscalls.c:162` `_sbrk` / `:181` `sbrk` ALIAS が実経路。
  MALLOC_LOCK行64=`__malloc_lock`、UNLOCK行65。libc_a-mlock.oの実lock/unlockはretのみ。
  再入/複数arenaの排他を保証しないので、adapterはbusy/arena選択の保護を明示し、
  再帰呼出しを含む全入口で整合させる (mlock.cの再帰lock契約)。

f1bの接続が成立しなければ最小初期量切替へ進まない。TLSF採用へ黙って切り替えず、失敗した実ソースケースとサイズを添えて本節の設計差分をレビューする (D23の再決裁ではなくallocator実装選択の再設計)。各小段の不確実性を全fの一回依頼へまとめない。

ホストは実appmem/paging/pgalloc/execと実nano/KHeap/SDKを使用。変異: zero化省略、公開前free、EXACTを別VA成功にする、slot不足後部分unmap、hint重複上書き、flags0の下端探索によるbreak妨害、EXEC_*公開unmap許可、失敗時break更新、65536判定をheader込みに変更、calloc積overflow、realloc先に旧free、別owner PFN返却、固定stack計数、起動予約追加。assertの目的を分け、コンパイルエラーをREDにしない。

NP21/W 8/17MBで伸長/unmap/再起動、物理不足/VA断片化/32extent FULLを別の理由で観測。pool0でも閉じる/STOP→owner0→次起動、A free後Bの内容保持、512KiB stack実アプリを§5で受入。Ra26664MBは旧上限を越す明示heapと32MB超PFNのread/writeをhで確認。`exec_as_leftover_pages==0`を強制reclaim前に要求する。予算は§6のf枠。

## 4. T2g — 池不足時の非同期trim

### 4-1. 着手条件・範囲

fのmap失敗理由・allocator busy/trim・STOP全返却がGREEN。appslot/appmem、exec安全点、GUI共有protoとRust写し、SDK C/Rustイベントループ、gshell handler/multiappを対象にする。新しいretry KAPI、timerで常時trim、CUIの裏実行、同期WM pump、P6の一般scheduler/x87は追加しない。

### 4-2. 状態と配送契約

各AppSlotに固定 `{pending_epoch, delivered_epoch}`、SDKにbusy/in_trimとlast_epoch、カーネルにu32 pressure_epochを置く (0=無し)。32bit満了はsaturateさせ、epoch一致だけで新要求を無視せずpending bitで管理する。動的queueは作らない。slot回収/再利用でpending/deliveredをゼロ、古いイベントを次のownerへ渡さない。

appmemは**data/PTの物理池不足**で準備を全巻戻しした後だけ、要求者以外の生きたback GUI slotにbitをORしてNULLを返す。VAの穴不足・extent満杯・KHEAP制御不足・引数不正は要求しない。front/backはWMが既存focus遷移の安全点でkernelへ伝える固定状態で判定し、USER申告を信用しない。その状態が未確定なら配送を遅らせる。kernelのepochは通知の合成であり再試行保証ではない。

WM top-levelの既存park/resume配送でpendingを見て、固定イベント枠にtrim通知を出す。通常queue満杯ならbitを残して後送、既存イベントを捨てない。gshell multiappはこれをready理由にし、要求者Aが安全なyieldへ入った時点のtrim pending slot集合を固定し、**要求者のyield復帰より先に各slotを1回ずつresume**する。GUI_OP_WAITのtimeoutでAが即readyでもこの一巡を先に処理する。1 slotの試行は既存の安全なresume→park/終了までで、応答待ちを追加しない。busy/queue満杯/未応答ならbitを残して1巡で打切り、TRIM_DONEを待ち続けない。USER loopから戻らないslotの停止は既存STOP/P6境界で、今回新たな強制preempt保証は作らない。更新途中のwrapperやallocatorから起こさない。CUI/終了ASへは配送しない。SDK受信時はbusyなら保留し、allocatorを抜けた安全点でtrim→任意cache hookを1回→完了記録。GUI protoに内部のTRIM_DONE op (epochを値で渡す)を設け、saved callerの保留要求と一致する場合だけ受理する。この帰路でkernel側exec_heapの安全な末尾trimを同じcaller ASのまま実行し、他ASへ切り替えない。新しいメモリKAPIは増やさない。hook中はin_trim、同一要求の再配送やhookのmalloc失敗で再帰trimしない。SHMのUSER可変完了値はスケジューリング上のhintだけで、pool freeは台帳から測る。

front SDKのmalloc/map失敗時はallocator lock/busyを解き、GUIかつ安全なイベントループに戻れる文脈だけ一巡yield→元要求を1回再試行する。raw mem_mapは自動retryしない。再試行も失敗ならENOMEM、未応答backを待たない。要求/配送/完了/retryは別カウンタで、成功の判定は実freeと再試行rc。pool0でも固定slot/eventで通知・閉じる・STOPが動く。

`ring3_wm_depth`を更新排他とみなさない。appmem/leaseのtransaction busyとSDK allocator busyは別に持つ。syscall入口の既存 `ring3_gui_pump` は入力処理の役割を維持するが、ここからtrim hookや別AS実行を追加しない。park可能な操作をSDKが明示した帰路に限定する。

端末配下CUI前景では失敗時bit記録→ENOMEM。CUIが親へ戻りWM top-levelを通った後でbackへ配送し、その後の**別要求**が成功し得る。CUIの失敗を同期pumpで成功へ偽装しない。

### 4-3. 分割・試験・受入

| 小段 (各45〜75分) | 成果 / 閉じる試験 |
|---|---|
| g1 | 固定bit/epoch/理由分類。slot再利用、飽和、pool0無確保 |
| g2 | gshell multiappのpending集合/1回ずつresumeを要求者復帰より先に配置。timeout即ready、queue満杯、CUI/終了AS除外 |
| g3 | SDK busy/hook/trim接続。保留、再入、未応答 |
| g4 | front一巡/一再試行とg2順序の統合。未応答で1巡打切り、raw map/CUIと区別、失敗後旧内容保持 |
| g5 | GUI proto両言語/世代・生成・STOPとの統合trace |
| g6 | 変異/小さなkselftest/予算とPM台本 |

実appmem/execのtraceとSDKイベント処理を組み、A map開始→巻戻し→KAPI復帰→allocator解錠→park→B trim→A retryの順をassertする。変異はmap途中pump、VA不足通知、CUI配送、busy無視、slot再利用で古いbit維持、無制限retry、queue満杯でbit消失、GUI_OP_WAIT timeoutでAをB trimより先に復帰。epochの単なる値だけで試験を成立させない。

NP21/W8MBでback末尾返却によるfront成功、返せないback/未応答backでENOMEM、trim中STOP→owner0/WM生存、pool0から閉じる。17MBでも同順序、Ra266はhで繰り返す。CUI前景ケースは取得できた端末、またはin-treeのCUI子をexec_runする最小GUI試験で作り、別アプリの導入待ちにしない。予算は§6のg枠。

## 5. T2h — 統合受入と先行段の未実施を閉じる

### 5-1. 条件・成果物

d〜gの対象host/変異・独立レビューとNP21/W受入が揃って統合実行へ進む。ext2調査票の原因判定・必要な修正/再検証をh受入前のゲートとする。新機能をまとめて実装する段ではなく、guest試験/build・deploy登録/観測、host結線の不足、正典の実装説明と結果を更新する段。機能の欠陥は該当d/e/f/g小段へ戻す。T3の帯変更・SLACK回収、T4のmodule/MEMSYS5を足さない。

**h1** manifest/世代/全consumer台帳、**h2** stack/旧形式fixture、**h3** resume/fault/STOP fixture、**h4** panic専用fixture/診断読み口、**h5** 8MB planar、**h6** 8MB PEGC、**h7** 17MB各backend、**h8** Ra266、**h9** 結果/残件の照合に分ける。準備は各45〜75分、PM実行は各構成/試験群を120分以内に切る。h2/h3準備はf以前へ前倒ししてよく、受入の証拠は最終一式でも取り直す。

manifestはkernel/loader/SDK/CRT/libs/shell/gshell/shlib/sh.bin/testsのhash/4世代/ビルドIDを固定。**apps/gameはv3でビルド対象外** ([ユーザー決定2026-09-30](../agents/HANDOVER_2026-09-30.md) §1/§3)。hの成果は外部callerの追随一覧 (lease/query/世代、低位直読撤去、heap/整列、再attach/trim、manifest)と、再開時の再構築・監査・guest受入ゲート。状態はPASSやh未完ではなく**決定による持越し**。外部集合の完全移行を実証したとは記さない。旧shellを起動して更新する手順にはしない [D1][D2][V1][V2]。媒体異常のあるNHDを無断修復せず、PMが健全な試験媒体を用意する (別件は§8)。

#### h2 準備記録 (コーダー、2026-10-02)

モデル: Codex gpt-6.1-sol (利用上限で完了報告前に停止)、レビュー対応は gpt-6-astra、`wt/t2h2`。**h2準備のみ完了、guest受入の証拠は未取得**。
e (lease/gfx/SHM権限/KAPI) の実装・公開SDK/KAPI版は変更しない。

- `userland/tests/h2_stack.c` を既定256KiB (`h2_stack.bin`、header要求0) と
  明示512KiB (`h2_stack512.bin`、`mkos32x --stack 524288`) でビルド。
  両方 `userland/deploy.yaml` の `/usr/bin/`、`[test]` に登録 [V2]。
  512版は288KiBのvolatile自動配列 + 16KiB×8段の生きた再帰フレーム、
  256版は96KiB + 16KiB×4段。各配列内の全pageと末尾へpatternを書き、
  戻り時に照合する。任意のstack帯を掃引せずreturn frameを壊さない。
  深い段でargc/argvの `h2-arg` を検査し、待機後にも再検査する。
- モードは `run|park|guard h2-arg`。`park` は深いフレームを保持して
  GUI窓を作り、生成時のringをPOLL/消費して `GUI_OP_WAIT(100 ticks)`、
  復帰後全フレーム/配列/argvを検査。
  `h2_stack_probe.h/.inc` のwait callbackをh3が再利用できる。
  この窓はh3のSHM `{magic,owner,generation,phase,mode}`/arm台本を代用しない。
  WAITの戻りや `H2 resume` の文字だけではpark成功に数えない。
  `guard` はpattern/argv照合後、各版の
  `MEM_APP_STACK_TOP - 実stack量 - MEM_GUARD_SIZE` へUSER byte write、PF error=6を期待。
  `H2 entry`、pages/span/low/high、guard番地、`H2 exit OK`を出す。
  exit行は回収前の観測印で、owner0の実測成功を自己申告しない。
- `make h2-fixtures < /dev/null` → `build/out/h2-isolated/`。
  現在の正規ビルドを壊す専用生成器 `tools/gen_h2_fixtures.py` を明示targetだけで起動。
  **通常deploy・package・NHDの入力に登録しない**。成果物は `.fixture` と
  source/size/SHA-256/期待理由の `manifest.json`。
  正常controlはapp/shell/shlib/依存GUI appの4本。
  拒否用は `old-app` / `old-shell` / `old-shlib` (**合成した旧形式ヘッダ**: 現在の
  フィールドからversion/header_sizeを戻した48B v3 header + 元body。実際の旧版成果物ではない)、
  `unknown-format` / `bad-abi-generation` / `bad-memory-generation` /
  `bad-shlib-generation` / `bad-shell-generation` / `old-cpl0-flag` の9本。
  不正世代を強制したguest用fixtureと、新SDK+旧.oを拒否するhost証拠を分ける。
  古い有効印を捏造する包装口は追加しない。
- `check-h2-fixtures-host` を `CHECK_PAR_TARGETS`、`tools/check_map.yaml` と
  生成 `docs/TESTS.md` に登録。ホストはビルド済header/deploy 2版、
  一時dirだけに生成したfixtureのhash、試験側の固定13行表 (名前→期待rc) と
  recordsのキー集合一致、実 `os32x_layout_check` のILP32 admission (4 control/9拒否)、
  新SDK+刻印なし旧.oのlink_guard拒否・ELF削除とmkos32x拒否 (2判定) を検査。
  `build/out/h2-isolated` の有無・鮮度には依存しない (PM決定b)。
  共通 `h2_plan` を256/512KiBで呼ぶ2ケースで配列量/段数/guard番地を検査、
  共通の起動argc判定3ケース、両サイズのlive-frame各6ケース (正常、argc/token、
  復帰時frame/argv/外側array破損) を実行。計34ケース。
  ILP32実行はすべて `tools/tests/host32.py`、`HOST32_RUNNERS=qemu`。
  13変異は置換数をすべて1箇所に固定し、**13/13 runtime RED**、compile失敗0。
  大小arrayを独立に壊し、512版の配列選択/大小depth/guard減算/起動argcも検出。
  両ELFにmemmap/GUI共有ヘッダの依存を追加 (userlandの.dは読まれない)。

**PM台本 (最終d〜g一式の同一hashで再生成・再実行する)**:

1. 正常一式の `make all < /dev/null` → `make h2-fixtures < /dev/null` の直後に、
   `build/out/h2-isolated/manifest.json` の全項目について生成物のbytes/SHA-256を照合する。
   例: `python3 -c 'import pathlib,json,hashlib; p=pathlib.Path("build/out/h2-isolated"); m=json.loads((p/"manifest.json").read_text()); assert all(len((p/n).read_bytes())==r["bytes"] and hashlib.sha256((p/n).read_bytes()).hexdigest()==r["sha256"] for n,r in m.items())'`。
   rc=0を記録してmanifestを保管。入力を再buildしたら生成・照合もやり直す。
   old-app/old-shell/old-shlibは「合成した旧形式ヘッダ」と記録する。
   健全な隔離試験媒体のコピーにPMが1ケースずつ載せる [D1][D2][V1]。
   `.fixture` は運用名へPMがコピーするだけで再包装しない。
   初回control後に異常を載せ、各ケース後に正常一式へ戻す。
   hash/build IDと新kernel.map/nm・対応control ELFを使い、入口 `_start`
   (headerのload+entry、shlib関数は対象jump entry) のbreakpoint/trace hitを数える。
   カーネルに未実装のentryカウンタがあるとは仮定しない。
2. Stack: CUIで `h2_stack run h2-arg` / `h2_stack512 run h2-arg`。
   pattern/argv OK、pages=40/104、512版span>256KiB、終了rc=0。
   起動中に `g_slot` の当該AS ownerを控え、exit後 `ledger_owners[owner].pages=0`
   (owner退役後は同番号の空き)、leftover/depth=0、次の起動成功を確認。
   `H2 exit OK`のみでowner0としない。GUIでは **GUI端末 (t5a_display)** のコマンド欄から
   `/usr/bin/h2_stack.bin park h2-arg` / `/usr/bin/h2_stack512.bin park h2-arg` を実行する。
   ソース確認: `t5a_display/src/guest.rs` の `launch_req(cmdline)` → gshell
   `drain_launch_requests` → `run_program` → `exec_start` → `exec_launch(cmdline,1)`。
   CUIビルドでもこの経路はAppSlot.gui=1。同期CUIのexec_runではgui=0で
   `appslot_park_check` が拒否する。Start→Runは引数列が対象外なので使わない。
   別のreadyな窓/端末を残し、WMのshould_park条件を満たすことを確認する。
   起動経路はソース上で確認済み、実PARKEDは未実測。
   当該slotのPARKED状態と `ring3_switch_count` の復帰増分を確認してから
   resume出力・pattern/argv・回収を確認。
   `api->shm_base` からGUI slotのrequest/ringを直接読み書きする箇所は、
   eのSHM権限変更・低位直読撤去後に再確認し、hの最終一式で再検証する。h3の2窓/SHM固定mode台本へhelperを接続する。
   両版の `guard h2-arg` は表示番地のCR2・PF error=6、fault_kill_count+1、
   SURVIVED行なし、同owner回収/次起動成功。正常終了とfaultを別に記録。
3. App: `/test/h2/control-app.bin` と各app用破損fixtureを `/test/h2/*.bin` に置く。
   controlを `run h2-arg` で1回起動、entry差分+1/正常終了。
   old-app/unknown-format/bad-abi-generation/bad-memory-generation/old-cpl0-flagは
   1回ずつ起動、entry差分0・拒否理由とrc、AS/ownerの取り残し0。
   H2出力が無いだけで入口前拒否としない。拒否理由はformat/flags又はgeneration。
4. Shell: 起動前からcontrol-shellの入口traceを準備。隔離媒体の `/sys/shell.bin`
   だけをold-shell / bad-shell-generationへ1ケースずつ置換して初回boot。初回entry=0、各format/generation拒否の停止理由と画面、
   停止継続を記録。旧shellを起動して更新しない。正常shell controlは初回entry+1。
5. Shlib: 毎回cold bootの隔離媒体で `/sys/lib/libos32gui.shlib` だけを
   old-shlib / bad-shlib-generationに置換。既ロード状態からの差替えでは試さない。
   `g_loaded=0`、`g_reject=SHLIB_REJECT_LAYOUT`、共有text/関数未公開、
   control-dependent (gui_demo) を明示起動してentry差分0。
   正常shlib controlはg_loaded=1、依存app entry+1と正常描画。
   単なる起動失敗/未ロードでのPFをshlib拒否証拠にしない。
6. 新SDK+旧.oはhostのlink/包装拒否・guest binary未生成を合格証拠とする。
   `bad-*-generation.fixture` のguest拒否とは別行に記録する。
   h2のguest証拠は準備時点には無く、f/h受入とh3接続はPMが最終一式で取り直す。

**実行記録**: PATHに `/home/hight/opt/cross/bin`、`CROSS_DIR=/home/hight/opt/cross`、
`TMPDIR=/home/hight/os32-tmp`、`PYTHONPATH=`、`HOST32_RUNNERS=qemu`。
`NP21W_DIR=/home/hight/os32-tmp/h2-unused-np21w-destination` は存在しない出力先へ限定し、
FD copy警告を確認 (実NP21/W・NHD・配備・ini・実機、commit/pushは未操作)。
初回fixture単独buildと最初のallは既存未生成libos32saveでrc=2。
`make libs < /dev/null` rc=0。次のallは未取得フォントの非対話同意待ちでrc=2。
[既存手順](../agents/HANDOVER_2026-09-30.md)に従ってmain worktreeの正規TTFをコピーし、
取得器の固定hash確認後、`CROSS_DIR=/home/hight/opt/cross make all < /dev/null` **rc=0**
(`h2-all-ready.log`)。`make h2-fixtures < /dev/null`、
`python3 -B tools/tests/test_h2_fixtures.py --mutate` は各rc=0 (`h2-fixtures-{build,test}.log`)。
`python3 tools/gen_memmap.py --write` rc=0 (最初はkernel.map未生成で失敗、all後に再実行)。
`python3 tools/check_select.py --lint` rc=0。
ログは `/home/hight/os32-tmp/`。既存GNU-stack/RWX/Rust警告あり。
前回PM検査: ホストで `make all` / lint / `make check-changed` (native qemu) はrc=0。
ただし `make h2-fixtures` を先に流した木であり、clean出力の保証にはならなかった。

**レビュー対応の検査記録 (gpt-6-astra)**:
- 同じ環境変数と未存在NP21W_DIRを使用。`make all < /dev/null` rc=0
  (`h2-review-all.log`)、FD copyは失敗警告で実環境には触れていない。
- `python3 -B tools/tests/test_h2_fixtures.py --mutate` rc=0、13/13 runtime RED。
- clean clone全体buildは、初回のlibs/フォント準備を含む再buildを避けるため省略。
  許可された代替として `/home/hight/os32-tmp/h2-clean-wwinx0ct/repo` にgit cloneし、
  今回の変更とallで得た必要な5入力binaryだけをコピー、**build/outにファイルが無い**状態で
  `CROSS_DIR=/home/hight/opt/cross HOST32_RUNNERS=qemu make check-h2-fixtures-host < /dev/null`
  rc=0 (変異13本込み、`h2-review-clean.log`)。h2-fixturesは未実行。
  最初の補助スクリプトはmakeが空のbuild/out/libを作るため「directoryも無い」という
  事後assertだけ失敗 (対象試験自体は成功)。ファイル不在判定に直して再実行rc=0。
- `python3 tools/gen_memmap.py --write` rc=0、
  `python3 tools/gen_tests_inventory.py --write` rc=0。
  `python3 tools/check_select.py --lint` はinclude/types.h登録漏れで初回rc=1、追加後rc=0。
- レビュー修正後の `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4
  TMPDIR=/home/hight/os32-tmp HOST32_RUNNERS=qemu make check-changed < /dev/null` は
  **rc=0** (`h2-review-check-changed-pre.log`)。build規則変更により全検査・全変異へ拡大。
  h2は34ケース/13変異、13/13 runtime RED。既存Windows opt-in試験は計9件skip
  (PowerShell temporary fixtures 4件が単独/まとめ検査で2回、Windows parser 1件)。
  検査中の票/ソース編集なし。
  結果記録後の最終検査はskip内訳の誤記に気付きCtrl-Cで中断 (rc=130)。
  停止完了後に内訳だけを訂正し、票を固定して同じコマンドで最終検査を再実行する。
  最終 **rc=0 (full、skip 9件)**。内訳はPowerShell temporary fixtures 4件が
  単独/まとめ検査で各1回 (計8件)、Windows parser 1件。
  `h2-review-check-changed-final.rc` と同名の `.log` で確認済み
  (ともに `/home/hight/os32-tmp/`、票は最終検査前に記入完了)。
- guest/native実行、e後のSHMアクセス、最終d〜g一式での受入とh3接続は未実施。
  NP21/W・NHD・配備・ini・実機、commit/pushは未操作。

**再レビュー Approve / P3 3件の対応 (Codex gpt-6.1-sol、2026-10-02)**:
- h2準備の範囲のみ。P3-1は前回最終rc=0とfull/skip内訳を実ログで確定。
  P3-2は `H2_STACK512_BYTES := 524288` をコンパイルと包装で共有。
  `test_built` は両版のguard絶対書込み即値を、headerのstack_size
  (要求0ならMEM_EXEC_STACK_SIZE) とmemmapから求める番地へ照合する。
  既定版の中間 `.raw` はmakeが削除するため、包装済みheader後の同一bytesを読む。
  P3-3は生成側と同じhashを再計算する照合を削除し、書き出したmanifestの
  expected理由3種→rc (0/2/3) が固定13項目の表と一致することを検査。
- 環境は上記と同じ。`CROSS_DIR=/home/hight/opt/cross make all < /dev/null`
  rc=0 (`h2-p3-all.log`)。h2単独試験は最初にmakeによる中間raw削除でrc=1、
  包装済みbytesを読む形へ修正後、`python3 -B tools/tests/test_h2_fixtures.py --mutate`
  rc=0 (`h2-p3-fixtures.log`)。**36ケース/13変異、13/13 runtime RED、
  置換各1箇所、compile失敗0**。ILP32実行は全てhost32.py/qemu経由。
- 既存 `check-h2-fixtures-host` の `tools/check_map.yaml` 登録を使用。
  `python3 tools/gen_memmap.py --write`、
  `python3 tools/gen_tests_inventory.py --write`、
  `python3 tools/check_select.py --lint` は各rc=0 (116本、対応表漏れ0件)。
- `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
  HOST32_RUNNERS=qemu make check-changed < /dev/null` **rc=0 (full、skip 9件)**
  (`h2-p3-check-changed-pre.log/.rc`)。内訳は上記と同じ8+1。
  検査中の票/ソース編集なし。この結果記入後も票/ソースを固定し、同条件の最終検査を行う。
  最終実行 (コーダーの sandbox、qemu) は rc=0 (full、skip 9 件、`h2-p3-check-changed-final.log/.rc`)。
  独立レビュー (Opus 5.5) は P3 対応の差分確認でも Approve。PM のホストでの native を含む結果は着地の記録に書く。
**h2準備の PM 検査 (2026-10-02)**: PM のホスト (PYTHONPATH なし、既定 `HOST32_RUNNERS=native qemu`) で `make all`・`check_select --lint`・`check-changed` すべて rc=0 (`/home/hight/os32-tmp/pmf-h2-{all,cc}.log`)。ゲストでの採取・受入は最終一式で行う (準備の段)。
- 公開KAPI・sdk/kapi.json・KAPI版・eのlease/gfx/SHM権限実装は変更なし。
  guest/native・e後のSHMアクセス・最終一式の受入とh3接続はPMへ申し送り。
  NP21/W・NHD・配備・ini・実機、commit/pushは未操作。

### 5-2. 未実施項目の担当と合格証拠

| 未実施 / 初回に閉じる段 | 具体的な準備・合格条件 |
|---|---|
| 512KiB stack実アプリ / f、h再実行 | mkos32xのstack指定をbuild/deployへ結線した専用guest。256KiBを超えるvolatile自動配列/制御した深い呼出しの各pageへpatternを書き、使用中のreturn frameを壊さずargv・park/resume・終了後owner0を確認。別faultモードで実stack直下guardを触りPF error=6。既定256KiBと明示512KiBを両方記録。単なるheader読取試験ではない |
| park→resume後fault/STOP / e準備、遅くともh | in-treeの窓を持つGUI試験2本を起動し、OP_WAITで実park→ring3_switch_count増分とSHMの復帰印を確認後、timer/SHMの固定試験modeでpf/gp/de/udへ進む。Run...引数や注入リングのキー配送に依存しない。STOP用は復帰後USER loop/KAPI loopへ移り、前景窓を確定してCTRL+STOP。park中WM killは対照だけ。両setjmp着地を区別しpending消費1回/深さ0/ledger IRQ・例外操作0/owner0/次起動を確認 |
| 旧形式入口前拒否 / h | 旧app/旧shell/旧shlib/未知形式/世代違い/旧CPL0 flagの専用破損fixtureを隔離媒体へ。正常controlのentry差分+1に対し拒否差分0。shellは初回0と停止理由、shlibは未公開・依存app entry0。新SDK+旧.oはリンク/包装で拒否してguest binaryを作らないこと自体が合格、強制した不正世代fixtureのguest拒否とは別記。古い有効印で包装を偽造する試験を通常ビルドの証拠にしない |
| R1 panic故障guest / h | 普通のCPL3から発火不能なテスト専用kernel構成でIRQ allocと例外freeを各1回。正常画像と別hash/manifest、pool会計変更前にR1 context/op/owner/EIPを残して停止、以後進行しない。hostの停止捕捉とguest停止を別証拠にする。通常配布物から故障口を除外、各回PMが正常一式へ復旧確認 |
| Ra26664MB / h | 実機boot/ONLINE、32MB超の恒等写像とworkspace PT、固定10枚sup/RW/WB・非配布、PEGC/日本語/HostDrv高位buffer、map/trim/owner戻り、PCM再生STOP→tick→IRQ解除→再open/実際の音。NP21/Wで代用不可 |
| T2a′ shell heap / h | 452KiB heapのピーク/失敗数、子ネスト/GUI/CUI/既存16本、pool0の閉じる。固定PD/PTと残余NPの不変。host境界試験とguest実ピークを別記 |
| ring3_guard BのRO保護 / e、h | shlibをロードしたASでP/U/ROとerror7を確認。未ロードNPでのkillは代用不可 |
| 外部apps/game / hで引渡し | ビルド対象外、決定による持越し。h1/h9がcaller追随一覧を作成しPMが保管。再開時に再構築/低位直読/符号付き高位比較/4世代/lease・heapを監査してguest受入。外部未ビルドだけで現行hを未完にしない |
| T1残件 / h | T1f実機回帰24件、cirrus-off強制fallback、PCM実機結果を元票に対応付ける。欠けたものは理由・担当・次の段を明記しPASSにしない |

park→resume台本は**h3のコーダーがe9と同じSHM観測形式で実装**し、PM/テスターが実行する。試験GUI 2本を立上げ→各々が自分のSHM blockに `{magic, owner, generation, phase, mode}` を初期化→OP_WAITでpark確認→新kernel.map/診断から当該blockの物理番地を引く→MCP `emu_write_mem`で固定mode (pf/gp/de/ud/USER-loop/KAPI-loop)とarmだけを書込む→resume印とswitch増分を待つ→当該mode発火を観測→owner回収/次起動を確認、を1ケースとする。armはresume後に1回だけ消費。timerはarmed以後の猶予にだけ使い、park前に発火させない。別owner/古いgenerationなら台本は中止し、キー注入を代用にしない。試験専用のSHM制御であり任意書込口は製品に追加しない。

故障画像・旧shell試験は作業用の媒体と明示した台本をPMが扱う。本設計作業から環境へ触れない。Ra266物理操作/配備の承認手続は既存規則の担当へ渡す。

### 5-3. 統合matrixと観測

親票§5-2の(a)master/(b)各AS/(c)post-exec毎boot/(d)leaseを適用する。低位present USERはSHM/trampolineだけ、APP/LEASEのmaster PDEは空、AS私有PFNはowner一致、共有shlib textはRO、lease全page/cache/refcount一致。PDE USER単独を漏れと数えない。guardと15〜16MB未登録部NP、DEVICE永久予約を確認する。

8MB planar/PEGC、17MB planar/PEGC/Cirrus、Ra26664MB PEGCの行ごとに、boot fail0→通常CUI/GUI/FEP→高位HostDrv read/write→親子→lease切替→map枯渇→trim→fault/STOP→次起動→V86往復を記録。8MBの私有総量2048KiBは親票§5-3の3配分で、shlib dataは実測分を内数、PD/PT/共有text/BBは別勘定。起動/描画/終了を20回。T2時点の固定帯で不足するなら必要/不足ページを記録してT3へ渡すことは既決だが、「2MB達成」とは記さない。

診断の番地は**受入対象の新kernel.elf / kernel.map / nmから毎回引き直す**。static symbolがmapに無ければnm/専用診断、`used_pages`は関数なので値の番地として読まない。NP21/Wは物理読取と画面、Ra266はmem/bootlog/シリアル診断を使う。読み口不足はh4で固定量の診断へ追加し、公開の任意物理read APIを作らない。

記録欄: SHA/build ID/hash/画像size/機種RAM/backend、実コマンド/rc、kselftest/地図、owner別used/free、lease数、leftover、bad_free、IRQ/例外深さ、ledger_irq_ops/exc_ops、fault終了種別、KHEAP/stack high-water、画面結果。brokerのboot自己診断+1は基準値、操作差分は0。失敗後の次起動と親データの一致までが1ケース。失敗/skipには担当と次の試験を記す。

## 6. 予算・ホスト試験の重さ

### 6-1. 予算ゲート (設計上の配分、未測定)

T2c-Rのレビュー修正後は `__bss_end=0x18B1CC`、上限0x195000まで**40,500B (約39.6KiB)**、圧縮477,998B。初回T2cの41,972Bを使わない。下表は親票§6の見積もりを更新する実装管理用の警戒枠で、ASSERT緩和の承認ではない。

| 段 | 正味kernel増分の計画枠 (text/data/BSS/整列込み) | 全枠消費時のASSERT残り |
|---|---:|---:|
| d | 3,072B | 37,428B |
| e | 16,384B | 21,044B |
| f | 8,192B | 12,852B |
| g | 3,072B | 9,780B |
| h (selftest/診断追加) | 3,072B | 6,708B |

eの旧10KiB枠は撤回する。T2b基盤だけで実測11,300Bだったため、公開/授権/束/再attach kernel側に6KiB、boot/SHM/V86/font閉鎖に4KiB、自己診断/失敗処理に4KiB、整列/統合余裕に2KiBの計16KiBを仮配分する。既存T2b分は基準値に含まれ二重計上しない。残り6,708Bは未配分余裕で、d0や新規検査も各段の枠へ含める。撤去による減少は実測まで先取りしない。kernel自己診断を含む。SDK/shlibの増分はこの表の外でも8MB/私有量/画像へ計上する。BSS前余白を二重加算しない。圧縮508KiB上限の残りは42,194Bで、未圧縮の枠と交換できない。各小段統合後に同一toolchainのreadelf/nm/map/sizeと圧縮sizeを前後記録。枠超過は理由と再見積りをPM/レビューへ、ASSERT・診断削除・T3前倒しで隠さない。

pending PT控え256Bは§3-2のkernel stackに置く。ASへ足すと1,456Bで1,376B上限を越すため禁止。AS688B + extent512B = 1,200B、1,376B上限まで176Bで制御/arena索引を賄う。arena毎の巨大PFN控えは置かない。AppSlot追加はtrim/caller寿命等をtarget sizeofで計上し、全6slot+最大4通常AS+SURFACE/台帳の**16KiB**を維持。KHEAP192KiBの他顧客込みピーク/起動拒否/返却も測る。KHEAPの制御を増やした分はBSS節だけでは見えない。PD/PT、SURFACE backing、PFN metadataは別物理勘定。32extent制限によるENOMEM/FULLを隠すために表を動的増設しない。

**d の枠の拡大 (ユーザー決定 2026-10-02「全体の余白から回す」)**: d0b・d1・d2 (P3 後) の実測で d の枠 3,072B の残りが **764B** になり (`kernel.bin` 361,272B、ASSERT 残り 38,192B)、d2 のレビュー (Opus) の見積もりでは d3〜d6 に 1.7〜3.2KB 要る。そこで **d の枠を +2,816B して 5,888B にする** (d3〜d6 に約 3.5KB)。h の後の全枠消費時の ASSERT 残りは 6,708B → **約 3,892B** に減る。d3 では d0b の `as_va_to_pa` / `redir_page` を使い回して見積もりの下限側に寄せる。**T3 の着手前に実測で残りを再計算する** (§11 の注記 5) — e〜h の枠と T3 のカーネル増分がこの余白に収まらなければ、その時点で再見積もりのゲートに戻す。

**d6確定値 (2026-10-02、同一cross toolchain、詳細§10-15)**: d0b直前 `a64dd4e` の
ソースを一時ディレクトリで再ビルドし、比較用build_idを現worktreeと同じ
`bbabb6a-dirty` に揃えると、基準 `kernel.bin=359,432 B` / `__bss_end=0x18B1CC`。
d6は `kernel.bin=363,220 B` (+3,788 B)、`__bss_end=0x18C270`、本体574,064 B。
d0b〜d6の正味増分は **4,260 B**、ASSERT残り **36,240 B**、d枠5,888 Bの残り
**1,628 B**。e/f/g/h枠16,384/8,192/3,072/3,072 Bは変更せず、全部消費後の余白は
**5,520 B** (=拡大後3,892 B + d未消費1,628 B) を後続へ持ち越す。T3前の再計算ゲートを維持。
圧縮像480,683 B、520,192 B上限まで39,509 B。ASSERT・状態行・親票は変更していない。

### 6-2. 試験実装の規約

各小段に正常対照と1つの目的別負例を先に用意し、実ソースの失敗→修正→成功を記録。MMU/IRQ/I/Oだけをホスト足場にし、kernelの判定やallocatorを模型に複製しない。実CR3/TLB・デバイス・描画の合格はguestで補う。新試験は `tools/check_map.yaml`、該当build検査、生成 [TESTS](../../TESTS.md)へ実装時に登録する。今回は実在しない試験へのリンクや生成一覧を作らない。

変異は写しの対象ソース/fixtureだけを1回読み込んだcacheから作る。1 mutantごとにリポジトリ全木コピー、rg全走査、make check、全manifest再生成をしない。依存closureを固定し、正常objectは再利用、変異TUだけ再コンパイル/再リンク。入力hashは一組の開始/終了で確認する。`mutpar.py`の固定並列度を使い、別の全体検査と重ねない。

1変異の目安はcompile+runで30秒以内、timeoutは誤り検出用でGREEN/REDに数えない。初回に最重と中央値を計測し、60秒を越す変異は対象fixture/依存を絞ってから全本数へ展開する (実機待ちを縮める指示ではない)。生き残り、compile/link error、timeout、意図したruntime REDを別集計し、理由assertを必須にする。リンクASSERT/生成拒否は別分類の正当な拒否で、runtime RED本数へ混ぜない。

## 7. T3以降へ渡す境界

| 次の票 | T2から渡すもの / T2でしないこと |
|---|---|
| [T3配置](TASK_T3_LAYOUT.md) | 高位AS、画像外固定10枚、private map/owner診断、resident CRT分離。shell再配置/SLACK/metadata移動はT3。固定PTを再移設しない。Unicode RO leaseはT3の全caller KAPI化と同時に廃止。kcg boot後NOSYS/試験変更はe8bへ前倒し、T3は継承検証とscratch定数整理 |
| [T4〜T6](TASK_T4_T6_MODULES.md) | B1 caller/copyと失敗試験。旧db_exec/db_prepareの全結線/stmt finalize前copy、B4 engine異常、F2/F3、512KiB MEMSYS5/FEP優先予約、必須module/MINIMALはT4/T5a。T2のSQLite entry0はB1接続済み3入口だけ |
| [T7/後半](TASK_T7_AND_FOLLOWUPS.md) | lease経由のplanarポインタ、再init/再attach、V86通常復元検査。低位640KiB専有化、BB pool移動、.kcgfont廃止/OpenTypeはT7。page0 NPのためのboot snapshot移行も後続 |
| P4/P6/P7/P8/P9 | 検証済みDEVICE resourceの利用、停止/trim安全点、4世代manifestを渡す。BAR汎用配置、x87切替/長いKAPI期限、fork slot整理、音/Video HAL刷新、私有surface合成は本票外 |

T2で2MBが成立しても最終P1/P6合格ではない。SQLite/FEP/モジュール、最終shell/font予算が揃った後の再測定が要る。T2で未確認の実機/外部成果物は、将来票の存在だけで閉じない。

## 8. 決定と実物の食い違い・判断が要る点

| 区分 | 決定/記録と実物の差 | 本票での扱い / 判断者 |
|---|---|---|
| 予定された過渡状態 | D1/D19の最終隔離に対し共有PT書込み/低位USER/旧fb直読が残存。T2b-R記録どおりnative VRAMのcacheも旧状態 | eで解消。c〜dの継続は承認済みで、D変更の判断待ちではない |
| 予定された過渡状態 | D23最小初期量に対し旧heap上限・二段先取りが残る | PM決定2を維持しfで終了。T2d/eで64MBの起動挙動を変えない |
| 設計API名と実物 | 親票のNOMEM説明に対し公開OS32_ERR_NOMEMは無い (内部LEASE_NOMEMのみ) | §0-2のNOSPC対応案をレビュー。新番号を勝手に予約せず、別コードが必要ならPMがKAPI正典で調停 |
| 後続設計による具体化 | 親票§3-4はUnicode橋をT7の新表/廃止までと記す一方、T3詳細§3はKAPI化と橋撤去を前倒しする | 本票はT3詳細へ引渡し、親票の決定本文は変更しない。PMレビューで撤去担当=T3を確認し、T2で先に廃止しない |
| 受入条件と実績 | 親票§6-1は64MB実機未確認ならT2a′受入完了にしない。main着地/NP21/W受入は済みだがRa266は未実施 | hへ未閉鎖ゲートとして継承。PMが実機日程/試験手段を決める。延期なら部分受入の明示だけ、免除はユーザー判断 |
| ユーザー決定の適用 | D35の外部caller追随要件に対し、2026-09-30にapps/gameをv3で組まないと決定済み | §5の一覧と再開時ゲートへ引渡す。決定による持越し、追加の対象外承認は不要。再開はユーザー判断 |
| 既存の不具合候補 | Rust allocatorがLayout.alignを満たす処理を持たない | fで実要求を試験し整列adapterへ接続。現時点の全Rustアプリが誤動作したと断定しない |
| 原因未確定の観測 | PM記録にT2b起動からext2のerrors印。原因未確定のため現時点では既知の不具合と断定しない | [調査票](../../archive/v3/TASK_EXT2_ERRORS_INVESTIGATION.md)をdと並行、h受入前ゲート。FSはカーネル層で、既知不具合化したらPOLICY_DEV §1により修正優先。修復は[D2]承認対象 |
| 既存の不具合候補 | fd_redirectが登録者ASを保存せず別CR3でVAを解決、ゲスト未再現 | d0aで再現確認→d0bで修正/回帰を新機能より先に実施。§1-2の登録済みpointer契約 |
| 公開エラー対応の具体化 | 親票§3-1は旧世代INVALとするが、実際のOS32_ERR対応を生成時に固定する契約。既存STALE=-11が利用可能 | §0-2は旧世代をSTALEへ対応する案。親票本文/Dは変えず、PM/独立レビューで対応を確認 |

新しいD決裁を必要とする仕様変更は本票から提案していない。allocator接続の成立、公開束API/エラー対応、世代更新は実装前レビューの対象。成立しない場合は到達可能な反例と差分案をPMへ返し、争点3ラリーならユーザーへ。ゲートを外す判断をコーダーへ委ねない。

## 9. 今回の文書検査

実行対象は `make check-docs-links check-docs-orphans check-docs-status check-tests-inventory check-constraints < /dev/null` のみ。**実行結果: rc=0** (2026-10-01、TMPDIR=/home/hight/os32-tmp)。リンク0 Errors、orphanなし、状態語彙適合、試験一覧は最新、制約17件適合。初回は新調査票の状態語「未着手」が語彙外でrc=2、「計画」へ修正後に5検査を再実行した。コード/ABI生成/ビルド/host実行・変異/guest/実機の新しい合格証拠は今回作っていない。

## 10. レビューで見てほしい点

- 保存callerの寿命がWM/入れ子/park/longjmpを跨いでtrusted漏れを起こさないか。
- B1がRO RAM入力を通しMMIOを拒否し、copyoutの全範囲失敗を無副作用にできるか。
- DISPLAY4面束、8 lease上限、2つのgfx実体とUnicodeの同時使用が矛盾しないか。
- 低位USER撤去とV86復元がSHM/trampolineの実効権限を保持するか。
- pending NP PTEによるmap rollbackと部分unmapが既存写像/別ownerを壊さないか。
- 実nanoの複数arena・整列・tail trimとUSER exec_heap metadata検査が予算内で実装できるか。
- trimがallocator/KAPI更新中に別ASを動かさず、一巡一再試行で終わるか。
- 40,500Bからの予算、未実施guest/実機と外部集合のゲートが過大な合格宣言を防ぐか。
- 各小段が120分以内の依頼になっているか、公開一括切替と準備差分を分離できるか。


### 10-1. 独立レビュー所見の対応 (2026-10-01改訂)

P3は依頼文の列挙順に番号を付す。反映済みは文書の修正を意味し、実装・guest合格ではない。

| 所見 | 反映箇所 / 契約 |
|---|---|
| P1-1 | §2-1・§2-4・§2-5。SHM専用権限口、shm_init後のUSER化、lock/free/回収後のUSERと2本目書込み |
| P1-2 | §2-3〜§2-5。V86 setup/teardownのPCD、kselftest (c)/帰路のcache一致と再lease、欠落変異 |
| P2-1 | §0・§1-1〜§1-3・§8。d0再現→修正優先、登録者AS/owner/generationとPA copy |
| P2-2 | §2-4・§5-1/§5-2・§8。apps/gameは決定による持越し、caller一覧と再開時ゲート |
| P2-3 | §2-2。GUI/CUI/boot × 4 roleの授権、gshell所有CLIENTのCUI貸与 |
| P2-4 | §2-2。束ref配列/count入力と全generation照合 |
| P2-5 | §3-2・§3-5。flags=0は上端から下向き、primary break伸長を確認 |
| P2-6 | §3-2・§3-4/§3-5。公開unmapはANON/LIBC_INITIALのみ、EXEC_*拒否とheader改竄検出を分離 |
| P2-7 | §3-4/§3-5。Rust mem_alloc整列adapter、caller由来でresident/USERを選択 |
| P2-8 | §4-2/§4-3。g2/g4がtimeout即readyよりpending各slotの1回resumeを優先 |
| P2-9 | §2-3/§2-5・§7、T3 §3。boot後NOSYS化をe8bへ前倒し、font試験の期待値更新 |
| P2-10 | §0・§5-1・§8、新しいext2調査票。dと並行、h受入前ゲート、原因未確定 |
| P2-11 | §0・§2-5・§3-5。e10を3分割、f1を2分割、eと並行できる準備を明示 |
| P3-1 | §0-2・§2-2・§8。旧世代は既存STALE=-11への対応案 |
| P3-2 | §3-2・§6-1。pending PT控え256Bはkernel stack、ASへ加算しない |
| P3-3 | §1-2。trampoline scratchは管理backingを照合したRO入力のみ許可 |
| P3-4 | §2-5 e2。lease walkのmaster往復を除去、IF/CR3不変 |
| P3-5 | §2-3。待ち復帰後query KAPIで世代照合、Unicode取得失敗はready=0で続行 |
| P3-6 | §5-2。h3担当とSHM物理mode書込み→実resume→fault/STOPの台本 |
| P3-7 | §9。check-constraintsを追加 |
| P3-8 | §6-1。eを16KiBへ再見積り、全枠消費後6,708B、実測ゲート維持 |

## 10-2〜10-16. T2d (d0a〜d6) の実装結果と受入 — アーカイブへ移した

2026-10-02 に T2d の完了記録 (d0a〜d6 の実装結果・レビュー対応・ゲスト受入、§10-2〜§10-16) を [archive/v3/TASK_T2D_RESULTS.md](../../archive/v3/TASK_T2D_RESULTS.md) へそのまま移した。この票の中の「§10-2」〜「§10-16」はそのファイルの同じ節番号を指す。

## 11. 独立レビュー 2 回目 (Opus 5.5、Approve) の P3 — 実装時の注記

2026-10-01、`3180a51` の差分に対して Approve (P1 2 件・P2 11 件はすべて閉)。以下の 5 件は設計の変更ではなく、実装時に従う注記 (PM 記入)。

1. **d0b の IRQ 保存区間**: redir のバッファは最大 `PIPE_BUF_SIZE` = 64KB (`fs/pipe_buffer.h:16`)。登録者 AS の回収は通常文脈でしか起きない (R1) ので、**ページ単位**で「生存確認 → walk → copy」を IRQ 保存し、ページの間で IF を戻す (buf_len はページ単位で進める)。§1-2 の「無制限サイズを割込み禁止でコピーしない」と同じ意味。
2. **§2-3 の PCD 範囲**: `v86_ident_map` の `[0xA0000,0xC0000)` には CG 窓など SURFACE 外も入る。PCD を付けるのは**台帳の native VRAM 区間と一致する範囲**だけ (paging_init で UC にする範囲と同じ)。区間外は現状どおり。
3. **ext2 調査票 x2/x3**: 保全コピーは `nhd-pull` で取らない (作業イメージ `build/nhd/os32.nhd` を上書きする)。停止中に `/home/hight/os32-tmp/` へ複製し原本を上書きしない。x3 で別の像から起動するための ini の HDD 指定の変更は、原本を保持する経路 (`np21w_ini_live.py` の live-apply / restore、または停止中にバイト単位で控えて戻す) で行う。
4. **§4-2 の front/back 通知**: gshell は CPL=0 なので、フォーカス遷移の安全点からの固定状態の通知は**カーネル内部の呼出し**を第一候補とし、KAPI を足す場合は [ABI1]〜[ABI3] (版の更新・clean → all) に従う。g1/g2 で決めて票に書く。
5. **予算の実測ゲート**: §6-1 の全枠を使うと残りは 6,708B。T3 にもカーネル側の増分 (Unicode の組表など) があるので、**T3 の着手前に実測で残りを再計算する**ことを §6-1 の実測ゲートに含める。

## 12. 受入の構成の方針 (ユーザー指示 2026-10-02)

「ハードウェア構成の変更でのテストは行わない。切りの良いところで一括」「(RAM 量・音源の) 二点は最後に確認すれば良い」。

- **d〜g の各段の NP21/W 受入は、今の構成 (17MB、今の `np21x64w.ini`) だけで行う。** ini を切り替えない。
- 本票の各段にある **8MB、planar / PEGC / Cirrus の切替、音源 (PC-9801-118 の PCM) など構成を変える確認は、受入記録に「構成依存は一括確認へ持越し」と書いて溜め、T2h の統合受入でまとめて行う** (§5 の h1〜h4 に加える)。Ra266 64MB も T2h。
- ゲスト側の設定 (`gfxmode` など) で済む確認は、構成の変更に当たらない — 段の受入で行ってよい。
