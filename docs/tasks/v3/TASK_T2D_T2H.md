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
| ext2調査 | [調査票](TASK_EXT2_ERRORS_INVESTIGATION.md)をdと並行。原因判定・必要な修正/再検証はh受入の前提。既知不具合化したら上記の優先順へ |
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
| e2 | 単面公開lease + copyout rollback。`lease.c:158 walk_enter/leave`のmaster往復を除去、保存caller/管理PD/PTをP2VでwalkしIF/CR3不変。FULL/PT不足/out不変 |
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
| f1a | toolchainの実nano入力/hash・リンク対象と由来/ライセンス/パッチのビルド台帳 |
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

**h3準備・PM決定(A)と実装記録 (2026-10-02、Codex gpt-6-astra、基点 `6a8aba9`)**:
PM決定(A)を採用。初期化時だけ、ホストが SHM 所有アプリ ID → appslot の AS →
台帳 AS owner / AS generation を照合して owner、generation の順に書く。
GUI slot を owner、tick を generation とする代用はない。以後は mode / arm のみ。
e9 で本人識別を渡す正式な経路ができたら、h3 の初期化もそちらへ切り替える。
公開 KAPI / JSON / 版・kernel/exec/gfx/lease/SHM 権限の実装は変更していない。

成果は [h3a.c](../../../userland/tests/h3a.c) / [h3b.c](../../../userland/tests/h3b.c)、
共通 [fixture.inc](../../../userland/tests/h3/fixture.inc) と
[state.inc](../../../userland/tests/h3/state.inc)、
[PM台本](../../../tools/h3_park_resume.py)、
[診断配置のコンパイル入力](../../../tools/h3_layout.c)。両バイナリを
`userland/deploy.yaml` に `/usr/bin/h3a.bin` / `h3b.bin` として登録した [V2]。
各窓は左 (30,60,250,130) / 右 (340,60,250,130)、独立の `sys_shm_alloc(1)`。
起動引数は使わない。初期化受領待ちは `sys_yield` (WAIT_POLL) で他アプリへ譲る。
**この初期化 yield は試験対象の OP_WAIT park の証拠に数えない**。
受領後、イベントを捨て OP_WAIT(0) へ進む。戻るたびに resumes を増やすが、
それだけでは実 park を主張せず、台本が appslot PARKED / parked_from_wait と
前後の `ring3_switch_count` を必ず照合する。mode は一度消費したローカル値に固定。
arm 消費後だけ get_tick の 500 tick 猶予を設け、その後に発火する。
USER loop は KAPI なし、KAPI loop は get_tick。park 前の timer はない。

**e9 と共有する観測形式**: 正典は試験専用
[protocol.h](../../../userland/tests/h3/protocol.h)。48 B、全欄 little-endian u32。
SHM ブロック先頭に置き、公開 SDK ABI にはしない。

| byte offset | 欄 / 意味 |
|---|---|
| 0 / 4 / 8 | magic=`0x48335031` / owner=台帳 AS owner / generation=AS 寿命世代 |
| 12 / 16 / 20 | phase / mode / arm (host は mode を先、arm=1 を後に書く) |
| 24 / 28 | version=1 / fixture=1(h3a),2(h3b) |
| 32 / 36 | resumes=WAIT 返却印の回数 / consumed=arm 消費回数 (1 回限定) |
| 40 / 44 | window=GUI window handle / gui_slot=GUI slot (識別 owner と別物) |

phase=1 INIT → 2 IDENTIFIED → 3 WAIT → 4 RESUMED → 5 ARMED → 6 FIRING。
異常は7 ERROR。2/4は短いので、台本は後続phaseとresumes/consumedでも受領を確認する。
owner/generation は最初0、ホストが generation を最後に publish し、fixture が phase を進める。
mode=1 pf (#PF14)、2 gp (HLT/#GP13)、3 de (DIV0/#DE0)、4 ud (UD2/#UD6)、
5 USER-loop、6 KAPI-loop。arm は RESUMED 後に0へ戻し consumed を1へ。
不正 mode / 二度目の arm は発火させない。任意アドレス書込み口を製品に追加していない。

**PM手順 (コーダーは live 台本を未実行、レビュー指摘対応 2026-10-02)**:

1. 新しい一式の反映を確認 [V1]。`python3 tools/h3_park_resume.py layout --layout
   /home/hight/os32-tmp/h3-layout.json` はオフライン。
   cross GCC の `sizeof/offsetof`、新 `kernel.elf` の nm、`kernel.map` のアンカー一致を使う。
   `__bss_end` 切上げとコンパイルした SHM 差分からブロック PA を求める。
   外部アプリの SHM / 複数 block の span=0 の後続は読み飛ばし、magic/version/fixture
   が一致する候補が一つだけであることを要求。live コマンドは ELF/map SHA-256 を照合する。
   **ホスト成果物の一致検査は実行中ゲストへの反映確認の代用ではない**。
2. GUI で h3a / h3b を引数なしで起動。各々 `init --fixture 1` / `--fixture 2`、
   共通の `--layout` と別々の `--case /home/hight/os32-tmp/h3-a.json` / `h3-b.json` を渡す。
   INIT だけに owner/generation を publish、受領を待つ。caseファイルの再使用は拒否。
3. PM は相手側の窓へフォーカスを移し、両窓の位置を動かさず target の OP_WAIT park を待つ。
   `arm --mode pf --capture /home/hight/os32-tmp/h3-capture.json
   --out /home/hight/os32-tmp/h3-observe.json` (gp/de/ud/USER-loop/KAPI-loop も同様)、
   同じ `--layout` / `--case` を渡す。loop は `--trace /home/hight/os32-tmp/h3-front.json`
   も渡し、PM/e9 が FIRING 後の新しい前景証拠をそのファイルへ渡す。
   台本は一時 pause 中に対象と相手の PA/所有者/世代、実 park を照合して mode / arm を書く。
   **breakpoint を click より先に設置**し、target のタイトルをクリックして起こす
   (480行なら `--height 480`)。SHM 書込み単独には WM を起こす効果がない。
   同じプロセスの採取ループで resume 印・switch増分・consumed=1 を case に記録する。
   fault の5秒猶予内に別コマンドを起動する必要はない。失敗は中止、arm 再送なし。
4. loop は同じプロセスが捕捉・前景照合・STOP送信・前後のobserveを順に実行する。
   再開済みloop用には `loop-watch --capture RAW.json --out OBS.json --trace FRONT.json`。
   USER runaway の200 tick猶予を待ち、case/identity/map/phase と5秒以内の前景app/windowを
   照合して `/api/key` に `seq=CTRL%2BSTOP&hold=300` をPOSTする。STOP再送は拒否。
   `trace-watch --trace RAW.json` は受動採取専用。別端末の stop / observe と併用しない。
   live台本全体を共通の非blockingロックで排他し、競合コマンドはHTTP操作前に拒否する。
   既存breakpoint / user pauseは先にPMが解除し、台本外のdebugger操作も同時に行わない。
   fault の vector / FIRING と前景の実観測は引き続き e9/PM の外部証拠を必須とする。
5. 回収後、次起動**前**に `reclaim`。対象 appslot FREE、SHM owner0/free、
   台帳ownerのkind/pages=0、reclaim +1/対象ID、ledger IRQ/例外操作0、pending ID0を要求。
   IRQ 中または下記の静止点でない標本は最大20回、再開を挟んで0.1秒おきに取り直す。
   int80入口直後の `(1,0)` も取り直す。例外深さは0。
   syscall/WM は通常 `(0,0)`、**記録した相手 fixture が同じAS世代で生存し、g_cur が
   その app、appslot RUNNING かつ in_op_wait=1 の静止点では `(1,1)` も許す**。
   2本構成の残り1本は should_park でparkせず X3 内で WM が回るためであり、任意の深さを
   許すものではない。この方法は製品の破棄点に breakpoint を増やすより単純で、相手の
   巻き添え終了も拒否できる。mode別の期待値は次表。

| mode | 設計上の経路 / landing | fault_kill差分 | abort差分 | launch/resume pending | pending消費 |
|---|---|---:|---:|---|---:|
| pf/gp/de/ud | FAULT_PENDING → resume着地 | 1 | 0 | 0 / 1 | 1 |
| USER-loop | runaway IRQ → ABORT_PENDING → resume着地 | 1 | 1 | 0 / 1 | 1 |
| KAPI-loop A | WMが宛先を解決 → 通常文脈exec_kill (`wm-kill`) | 0 | 0 | 0 / 0 | 0 |
| KAPI-loop B | syscall境界ring3_abort_check → 直行回収 (`syscall-abort`) | 1 | 1 | 0 / 0 | 0 |

KAPI-loop は共通して pending 0 (着地なし)、回収1回、静止点の深さ、相手の同じAS世代での生存を要求。
A は対象ID一致の wm_kill 1 / syscall-abort 0、B は wm_kill 0 / syscall-abort 1 とし、
どちらか一方だけを認める。回収を伴わない入口到達や混在は合格にしない。次に同じfixtureを新規起動し `verify --trace /home/hight/os32-tmp/h3-trace.json`。
新世代の INIT、mode別の着地/pending、breakpoint採取との一致を確認して初めてPASS。
各modeを両fixtureで行い、e〜gを含む最終一式でも再実行する。

**e/g への申し送り (未観測、コード読み)**:
[TASK_T2_APPBAND §4-3 R1 / §4-4 R2](TASK_T2_APPBAND.md) は KAPI 連打の強制停止受入と
通常文脈回収を要求し、[TASK_MEMMAP_V3 §3-5-1](TASK_MEMMAP_V3.md) は KAPI 内の更新を
途中で破棄せず安全点へ渡す。PM決定により、通常文脈での WM kill と syscall 境界 abort の
両方を認める。穴の直し方は e/g が決める。現実装の `appslot_abort_admit` は RUNNING /
in_op_wait=0 かつ int80 ごとに last_kernel_tick が更新される get_tick loop を拒否する。
X4 (`pump.rs`) は入力から abort_seen を立てるだけで、消費点 X3 / top-level へこの loop
から戻らない。**要求と消費点の接続不足が疑われる製品側の穴**であり、h3では修正しない。
USER-loop は raw CTRL+STOP が後の WM top-level に残り相手まで畳む可能性もある。
起きたら製品側の所見として扱い、追加reclaimや相手消失を台本の期待値へ吸収しない。

KAPI-loop のSTOP直前・送信後2秒/10秒の観測は統合採取プロセスが `--out` へ保存する。
単独の `observe --out 別ファイル.json` もPASS判定をしない観測用で、caseを上書きしない。
統合出力は `observations.before_stop / after_2s / after_10s / final`、faultは `final` のみ。
各標本に新nm番地のカウンタ、g_cur/tick、対象slotの state / in_op_wait / abort_req /
last_kernel_tick、`slot.as`、`address_space.address / owner / generation`、観測時刻を記録する。
STOP後も同一対象が RUNNING、in_op_wait=0、last_kernel_tickが進む、reclaim差分0なら未回収。
wm_kill / ring3_abort_check の採取点 / 両pending / consumed の実PC・標本も保存し、
回収経路A/Bのどちらかと一致するか確認する。製品側の未回収はh3の期待値に吸収しない。

**park中WM killの対照 (手作業を選択)**:
自動STOP/armケースに混ぜず、別の新しい2本とcaseを初期化し mode=arm=consumed=0 のまま、
新layoutの対象 appslot PARKED / parked_from_wait=1、SHM phase=WAITをfreeze中に読む。
PMが対象窓の閉じる操作で WM の request_kill → top-level exec_kill を起こす。
対象IDの wm_kill入口は下記の手動breakで採取する。
対象FREE / SHM owner0/free / 台帳kind/pages0 / reclaim+1 / last_reclaim_id対象、
fault_kill/abort/pending消費の差分0、相手生存、IRQ外で上記深さ、次起動の新世代を確認する。
armを作らない対照では trace-watch の `resume_verified` ガードを解除しない。
代わりにPMがlayoutの `wm_kill` PCへ手でbreakし、ESPと `/api/mem` の恒等写像 `[ESP+4]` にあるcdeclの対象IDを読み、
breakを解除する。
この対照はresume印やresume後faultの証拠には数えない。窓を閉じるUIは手作業にすることで、
位置変更・他窓・未公開WM状態のオフセットを台本へ固定せずに済む。

**e9 / PM trace の受渡し**:
正典の共通 protocol.h を e9 も読む。host試験が欄順・MAGIC/version・全phase/mode定数を照合。
layout の `trace_sites` は nm で関数を同定して objdump から launch/resume の setjmp返却PC、
それぞれの pending helper 呼出PC、helper入口、g_pending_id=0代入直後PC、exec_kill入口、
ring3_abort_check内の実際にabortする分岐の採取点を得る (毎syscallで通る入口は数えない)。
呼出/代入が一意でないビルドは拒否する。trace-watch は launch/resume pending呼出PCで
対象pending IDを持つ到達だけ数え、consumed PCで pending=0・IRQ/例外深さ0を読む。
通常のpark帰りは pending=0 なので pending回数に含めない。exec_killはcdeclで、
入口のESPを読み、恒等写像 `/api/mem` の `[ESP+4]` を対象app IDとして採取する。
実ELFの先頭命令 `push %ebp; mov %esp,%ebp` をlayout生成で検証し、違えば拒否する。
採取JSONには実PC・ESP・スタック引数・pending・深さ・時刻を残す。
最終JSONの `capture` 欄へ、この採取JSON全体を入れる。verifyは生サンプルから回数を再計算し、
ELF SHA / case_id / 全観測PCも照合する。採取ファイルのSHA-256と started_at / ended_atを
caseの `capture_record` へ記録し、埋め込みcaptureのハッシュ一致と `armed_at` 後の時刻を要求する。採取器を通さず
最終JSONへ期待値を書いただけのcaptureや採取後の改変を拒否する。

外部証拠は `case_id`, `identity` (index/address/app/owner/generation/slot), `mode`,
`map_sha256`、`resume_observed:true`, `fired:mode`, `vector:14/13/0/6` (loopはnull)。
最終 `landing`, `launch_pending`, `resume_pending`, `pending_consumed` は上表と採取値に一致させる。
STOP前景用は `phase:6`, `observed_at:Unix秒`, `foreground_app`, `foreground_window`、
最終STOP証拠にも同じ前景欄を含める。
**前回 P2-1 の判断を維持: 自動着地/pending採取を実装し、前景自動物理読取は見送る**。
gshellのGuiState/WinはRust私有配置で、新ELFに対応したoffsetofを保証する診断経路が無い。
製品を変えず型の配置を推測して読むより、PM/e9の前景観測を必須に残す。
従ってSTOP送信と同一freezeでの前景確定は未実施であり、5秒以内の外部観測と
照合の間の前景変更は排除できない。PMはこの間に入力/フォーカス変更を行わず、
前景の実読取番地と値・ゲスト同一性の証拠を添える。自動採取の追加はe9接続時に再検討する。

**今回の再レビュー対応・ホスト検証 (2026-10-02、Codex gpt-6-astra)**:
P1-A/B、P2-a/b/c、P3-a/b をh3準備の範囲で修正。製品コード・公開KAPI・版は変更しない。
[test_h3_park_resume.py](../../../tools/tests/test_h3_park_resume.py) は偽emulator/CLI/
breakpoint client/実ELF layout/プロトコル照合 **32ケース**、実state.incのILP32 **42検査**。
変異は C 8 + Python 45 = **53/53 runtime RED**、compile失敗0。置換当たり数を固定し、
Cはcompile成功後の実行失敗、Pythonは試験assertion failureのみを数える。
拡張途中にSTOP再送ガードの置換件数増加を固定件数検査が検出して中断した。
正しい2箇所に更新し、最終の変異一式を再実行して上記結果を得た (全体検査の失敗ではない)。
前回の22ケースに、cdeclスタック/EAX不一致、captureハッシュ/時刻、KAPI二経路と混在拒否、
IRQ abort採取点の除外、静止点再採取/上限、observe別出力とAS識別、並行コマンド拒否、
freeze/trap競合、click前break設置〜STOP〜時間別観測、外部user pause拒否の10ケースを追加。
`check-h3-park-resume-host` の登録を維持し、ELF採取点の入力も `tools/check_map.yaml` へ追加。
`docs/TESTS.md` は生成器で更新。ILP32は `tools/tests/host32.py` / `HOST32_RUNNERS=qemu`。

今回実行済み:
- `CROSS_DIR=/home/hight/opt/cross make all < /dev/null` **rc=0**。
- `python3 tools/tests/test_h3_park_resume.py --mutate` **rc=0**。
- `python3 tools/gen_memmap.py --write` / `python3 tools/gen_tests_inventory.py --write` **rc=0**。
- `python3 tools/check_select.py --lint` **rc=0** (116検査、漏れ0)。

共通環境は `TMPDIR=/home/hight/os32-tmp`, `PYTHONPATH=`, `HOST32_RUNNERS=qemu`。
make all の外部FDコピー先は存在しない `NP21W_DIR=/home/hight/os32-tmp/h3-no-deploy-destination`
へ向け、NP21/W側へコピーしない。既存Rust警告とFDコピー失敗警告あり。
ログは同所 `h3-rereview-all.log` / `h3-rereview-all.rc` / `h3-rereview-host.log`。
既存の製品ビルド依存を含む差分は選択器の許可型外なので、check-changedは全変異へ
安全側fallbackする。選択器の許可型は拡張しない。

この票を検査前に固定し、最後に
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp HOST32_RUNNERS=qemu
make check-changed < /dev/null` を実行する。最終結果は
`/home/hight/os32-tmp/h3-rereview-check-changed.log` と同所 `.rc`、最終報告に記録する。
検査前時点では未実施であり、PASSを先取りしない。検査中は票/ソースを変更しない。
NP21/W・NHD・配備・ini・実機・commit/pushは未実施。nativeはsandbox SIGSYSのため未実施。
e9識別/前景接続、KAPI-loop穴の観測とe/g判断、実trace/park中WM kill対照、
Opus 5.5による差分再レビュー、e〜gを含む最終一式でのゲストmatrixを次段/PMへ申し送る。

**h3準備の独立レビュー (Opus 5.5) — 3往復で Approve (2026-10-02)**: 1回目 P1 2件 (深さ0の読み方、KAPI-loopの期待経路)、
2回目 P1 2件 (wm_killの採取をcdeclのスタック引数に、捕捉とSTOP/observeの直列化) と PM の決定 (KAPI-loop は経路 A/B のどちらか1本)、
3回目は着地を止める指摘なし。残る P3 は実ゲストの初回採取で確かめる (どれも失敗すれば台本が中止する側に倒れ、偽PASSは出ない):
(1) freeze が `/api/instance` を読んだ直後・`/api/pause` の前にトラップが入るとユーザー pause が残り `'user pause during capture'` で中止する —
起きたら PM が手で再開し新しい case でやり直す (直すなら `/api/pause` の後に instance を読み直し、トラップなら CaptureTrap 扱い)。
(2) `exec_kill` の `[ESP+4]` を物理番地で読む前提 — 初回採取で argument が 2〜5 に入ることを確かめ、範囲外なら CR3 を見て線形番地で読む。
(3) KAPI-loop 経路 A で製品の修正の形によっては exec_kill の入口に2回届き得る — e/g の修正の形が決まった時点で「回収を伴う到達1回」に数え直すか決める。
(4) 前景の照合から STOP 送信までの隙間は前回合意の見送りのまま。
**h3準備の PM 検査 (2026-10-02)**: PM のホスト (PYTHONPATH なし、既定 `HOST32_RUNNERS=native qemu`) で `make all`・`check_select --lint`・`check-changed` すべて rc=0 (`/home/hight/os32-tmp/pmf-h3-{all,cc}.log`)。ゲストでの採取・受入は最終一式で行う (準備の段)。

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
| 原因未確定の観測 | PM記録にT2b起動からext2のerrors印。原因未確定のため現時点では既知の不具合と断定しない | [調査票](TASK_EXT2_ERRORS_INVESTIGATION.md)をdと並行、h受入前ゲート。FSはカーネル層で、既知不具合化したらPOLICY_DEV §1により修正優先。修復は[D2]承認対象 |
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
