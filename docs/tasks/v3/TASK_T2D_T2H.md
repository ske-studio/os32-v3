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
| e3 | DISPLAY4面束 + native UC。最後の面失敗で全巻戻し、cache全alias一致。実装・試験・予算は下記「e3 実装結果」。exec/V86のPCD保持を前倒し |
| e4 | backend→kernel fb/geometry。実3backendの選択と200/400行。下記「e4 実装結果」 |
| e5 | 再init/revoke/fallback。旧token拒否、第三AS不変 |
| e6 | C SDK checked attach/互換void橋。反復取得/失敗時描画なし |
| e7 | shlib/gshell復帰配線。gfx両実体で再描画 |
| e8a | Unicode CRT/shlib/user utf8。日本語、RO出力拒否、取得失敗時ready=0 |
| e8b | kcg boot専用化とNOSYS。font_load_test/台本/host期待値、表/BB/mailbox不変<br>申し送り (2026-10-02): `font_load_test` と `test_result_conv` の期待を boot 後 NOSYS/表・BB 不変へ更新する。 |
| e9 | tvdumpとSHMマーカー・観測側。wire不変、fault目的地一致、lock→free/exit→2本目アプリのSHM書込み<br>申し送り (2026-10-02): `nop`・`ring3_hello/fault/guard` のマーカーを SHM へ移し、`db_v50_test` の VRAM 許可期待を未貸与拒否へ変える。 |
| e10a | shm_init後のboot口・SHM権限口・起動時map撤去・汎用昇格禁止。lock/free/回収のUSER維持<br>申し送り (2026-10-02、e11 で統合): kselftest `test_map_user_keep` と paging.c `paging_map_user_keep_selftest` を共有PT昇格禁止に合わせて改廃し、`paging_bounds_host.c` の `paging_addrspace_map_user_keep(...) == 0` の呼出しは拒否期待へ反転する。`paging_map_user_keep_selftest() == 0` の呼出しは成功期待を維持し、selftest の中身を共有PT昇格禁止に合わせて書き直す。 |
| e10b | V86 sessionと全出口復元。VRAM/Eのsetup/teardown PCDはe3へ前倒し済み (PM判断、初回起動/V86でUCを失わないため)。PDE USER、失敗/STOP出口を確認 |
| e10c | master/AS/post-exec毎bootの3段検査。kselftest (c)とV86帰路でalias_cache一致、DISPLAY/TVRAM再取得 |
| e11 | 全切替・世代/生成/manifest確認 (前小段を統合、単独で部分配備しない)<br>申し送り (2026-10-02): `test_app_bb_overlap.py` は旧直接BB公開の撤去時に退役するか lease CLIENT 試験へ作り直す。e10a の map_user_keep 試験更新も統合する。 |
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


#### e3 実装結果 (2026-10-03、GPT-6)

**状態: 実装・個別ホスト確認済み、独立レビュー・ゲスト受入待ち。**
作業木 `wt/t2e3`、基点 `d4d5a2b` (STOP修正着地後)。
PM判断は作業依頼の `/home/hight/os32-tmp/ref_e3.md` §7。
全体検査前に本欄・生成文書を固定し、最終ゲートの結果は下記ログ/rcと最終報告へ残す。

**範囲・設計**:
- §2-2の束契約を `surface_lease_bundle(source, user_refs, count, access, user_out)` として
  内部実装。PC98 DISPLAY/count=4専用、出力はcount+4 view (116B)。単面口は従来どおり。
  B1でrefを一括コピー・出力全域を事前検査し、授権→surface_query_refs→acquireを
  同じIRQ保存区間で実行。束全体INVAL優先→STALEの順序を入口で確定し、
  低層lease_acquireの重複sid許可/再検査/既存10変異の当て先は維持。
- 4slot/VA/PTを準備後に一括公開。copyout拒否では今回4tokenだけをrelease。
  失敗したreleaseの本数だけ診断を加算し、**全release成功時だけ**旧slot全バイトと採番を復元。
  sourceはkernel snapshot。束入口と単面CLIENT/DISPLAYは `gfx_sf_backend()` と
  source.backendを同じIRQ保存区間で照合し、不一致はINVAL・out不変。
  PEGC/Cirrus選択中にPC98 source/4refを渡して拒否、PC98選択時に成功を確認。
- `gfx_boot_reserve`にB/R/G/Iの4 SURFACEを追加。owner=KERNEL、VRAM/UC/RW、
  planes=1/offset=0、640×400/pitch80/8page。
  formatは既存PLANAR4 (画素配置)、planesはその記録に入る面数。
  PC98は `g_backend_list` の最終候補で明示PEGC/Cirrus失敗時もfallbackするため常時候補。
  登録があっても選択中backendと一致しなければ貸さない。PEGC DISPLAYはe4。
  Cirrus DISPLAYのNONEは既存の授権接続段まで維持。VRAM末尾768Bは装置の同じ面であり
  他用途と同居させず、PM判断どおりゼロにしない。RAM planar CLIENTのstrideは32000Bのまま。
- nativeの番地正典を `include/pc98.h` に集約しgfx/tvram/V86は参照。
  paging_initの最初のPTEからTVRAM `[A0000,A4000)`、BRG `[A8000,C0000)`、
  I `[E0000,E8000)` にPCD。CG/ROM/RAMはWB。
  FIXED/UC台帳のnative backing方針をboot前の定数で表現する (ROM/CGまで一律UCにはしない)。
  execの低位USER化は既存 `_keep`、V86の表はCGを分離しsetup/teardownともPCD。
  これはe10b分の前倒し。共有USER化の撤去はe11、全ASのPD照合はe10cのまま。
- kselftestに4登録とnative alias全ページの2 check追加。期待pass **270→272**、
  ゲスト未実施なので272成功とは主張しない。KAPI/SDK生成・版・memory世代は変更なし。

**変更前に洗い出した既存期待・足場**:
1. `lease_host.c`: TVRAMのboot aliasがWBなのでINVAL → UCなので成功。
   WB不一致の拒否という旧意図は明示的なPCD破壊で残した。
2. `gfx_boot_host.c`: S trace+4。実bootにあるFIXED/UC記録を足場へ追加し、
   identifyが確認する初期region数3→4。4面の全属性とCLIENTの32000B strideも照合。
3. `test_surface_lease.py`: 新入口追加で同文言が増えるため、既存単面変異を単面側の
   ソース区間に限定し置換数1を維持。ビルド足場は新束試験と共有。
4. `con_sink_host.c`: tvram.hがpc98.hを含むようになったため、旧include順専用の
   `#undef TVRAM_BPR`を除去。console/con_sink実物の期待値は変更しない。
5. query/leaseの重複sid契約、paging_bounds/memmap/access_walkの既存成功・拒否期待は維持。
   `rg`でpaging_init/gfx_boot_reserve/v86_ident_map/surface_lease/map_user/gfx_sfの
   includeと関数切出しを走査。app_band_pde/app_bb_overlap/highram/memory_boot/device_reservation/
   paging_rebuild/shlib_high等のpaging全文取り込み、gfx_bootのsliceも確認。

**試験**: [surface_bundle_tdd](../../../tools/tests/surface_bundle_tdd.md)。
新規1ホストsuite (実lease/query/B1/paging/pgalloc、execの実VRAM map文、V86 setup/teardown全文)。
正常対照はqemu成功、束21/21 runtime RED (初回18+レビュー対応3)。gfxは17構成+1静的検査、
既存14+新規3=17/17 runtime RED。既存lease10/10、単面18/18 runtime RED (既存17+backend照合1) を確認。
この4集合は**66変異 (e3新規25+既存41)**。query既存34/34も個別で確認し、
5集合では**100変異**。con_sinkの既存正常対照とtargetコンパイルもrc=0。
全変異の置換数固定、コンパイル失敗/signal/timeoutをREDに数えない。
4面目だけPT不足、最終outページRO、既存lease/master/別AS/slot/台帳/会計/IF/CR3不変、
RO/RW、FULL、混在INVAL/STALE、通常GUI拒否、PEGC選択拒否、release全4失敗/途中1失敗を確認。
boot→exec map→V86 setup→teardown→再execで低位1MB全PCD/PWTを照合。
V86 BIOS/I/Oはstub、CPU仮想RAM書込みだけhost backingへ向ける。実exec全体や実V86 CPUは未検証。
最初のslot復元変異SURVIVEDは、空slotの残存内容が偶然一致していたため。異なる残存バイトを
注入して閉じた。初期の関数名/kmemset/boot限定登録の足場修正はコンパイルREDに算入しない。

**サイズ** (同じ `/home/hight/opt/cross`、size/nm/readelf/kernel.map、build ID差を含む):

| 項目 | STOP着地後の基準 | e3 | 増分 |
|---|---:|---:|---:|
| ELF text (SQLite込み) | 702,788 B | 703,316 B | +528 B |
| ELF data | 36,555 B | 36,731 B | +176 B |
| ELF bss | 606,484 B | 606,484 B | 0 |
| kernel.bin | 364,404 B | 365,124 B | +720 B |
| vmkernel.lz4 | 481,352 B | 481,821 B | +469 B |
| __bss_end | 0x18C754 | 0x18CA34 | +736 B |
| ASSERT余白 | 34,988 B | 34,252 B | −736 B |

レビュー対応後のmake all実測を採用 (同一toolchain、CONFIG_LGY98_FLAGS=0)。
今回の開始時の実物はkernel.bin 365,076B / vmkernel.lz4 481,824B、text703,284B /
data36,715B / bss606,484B / __bss_end0x18C9F4。
今回の修正だけではbin+48B、圧縮−3B、text+32B、data+16B、bss不変、__bss_end+64B。
未結線 `.text.surface_lease_bundle` は578B、単面584B、query1463Bでgc。
未参照見込みは2,625B。d6起点0x18C270からのe残枠は**14,396B**、
未参照見込み控除後**11,771B**。接続時には再計測する。
STOP最終増分は予告1152Bではなく1344B。ASSERT/診断は削らず、定数上限も増やしていない。

**実行と残件**:
- 環境はTMPDIR=/home/hight/os32-tmp、PYTHONPATH空、HOST32_RUNNERS=qemu。
  makeはCROSS_DIR=/home/hight/opt/cross、NP21W_DIR=/dev/null、stdin=/dev/null。
- 初回基準make allは未追跡IPAフォント不足でrc=2。既存mainの取得済み2ファイルを
  worktreeにコピー後、基準再実行rc=0、e3のmake allもrc=0。
  `/dev/null`へのD88コピー警告は指定による配備抑止。配備先へのコピーはなし。
- 個別コマンド `python3 -B tools/tests/test_{lease,gfx_boot}.py --mutate`、
  `test_{surface_lease,surface_bundle}.py --runner qemu --mutate` はrc=0。
  queryの`--runner qemu --mutate`とcon_sinkもrc=0。
  ログ: `/home/hight/os32-tmp/e3-{lease,gfx,single,bundle,query,con-sink}.log`。
- `gen_memmap.py --write` / `gen_tests_inventory.py --write` / `check_select.py --lint`
  はrc=0 (120検査、対応表漏れ0)。ヘッダ参照増加の6対応表も追従。
- 最後に指定の `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`
  を実行。ログ `/home/hight/os32-tmp/e3-check-changed.log`、終了値 `.rc`。
  新規make recipeの型が絞込み対象外なので安全側の全変異となる。実行中は票・ソースを固定。
  **初回rc=2**: (1) con_sink足場の旧`#undef TVRAM_BPR`でコンパイル失敗、
  (2) gfxの既存bb-whole-pool変異がSIGSEGVで終わり、厳格なruntime RED判定では不合格。
  最初のgfx個別17/17はsignalも拾っていた旧判定によるため、その時点の同変異はREDに算入しない。
  (1)は不要undef除去、(2)は実allocatorの戻り範囲をクリア前にassertして修正。
  修正後はcon_sink rc=0、gfx全17変異がrc=1/FAILによるRED、正常18試験も成功。
  headerのundef/代替定義を追加grepし、同型の足場は他に無いことを確認。
  記録・生成物を更新し、許可された**再実行1回**を
  `/home/hight/os32-tmp/e3-check-changed-retry.log` / `.rc` に保存した。
  **再実行rc=0** (初回実装の束18/18・gfx17/17・単面17/17・query34/34)。
  レビュー対応後の個別結果は上記の束21/21・単面18/18へ更新。
- nativeはPMのホストへ。初回独立レビューはOpus 5.5のApprove (条件付き)。
  今回はそのP2/P3対応。差分の再レビュー、NP21/W/実機の受入はPMへ。
  NHD/配備/ini/実機/commit/pushは未操作。e4はpublisher/geometryとPEGC DISPLAY、
  e5は再init/revoke、e10bはsession全出口/PDE USER、e10cは全AS alias照合、
  e11は共有USER化撤去・KAPI/世代公開・未参照2,625Bの接続時再計測を引き継ぐ。


**独立レビュー対応 (2026-10-03、P2-1/P3-1〜10)**:
- 選択中backendの台帳名を返す `gfx_sf_backend()` を内部公開し、束と単面の
  CLIENT/DISPLAYの照合を追加。実backend probeは模擬、source/refsはPC98のまま
  選択をPEGC/Cirrusへ変える拒否試験。削除変異は束/単面で各1本。
- 内部名/sectionは `surface_lease_bundle`。e11公開予定の
  `gfx_surface_lease(role, refs, count, access, out)` が内部sourceを構築して呼ぶ対応。
- `test_surface_lease.py:run()` は同一unitの重複とpaging/V86/execを2種類以上
  同時に差し替える変異をassertで拒否 (同じfixture.oを上書きするため)。
  query+lease+pagingの世代3防壁変異は保つ。束の出力事前検査とIRQ/例外拒否を各1変異追加。
- 全runner正常対照は6試験へ。生成器のmemory表とpaging見出しにnative UC/CG・ROM WBを記載。
  V86表の注記はPC98_NATIVE_VRAMとの同一区間とhost照合を明記。
  DISPLAY登録失敗数は起動ログ `display_fail` へ。
- 既存関数取り込みの足場を `rg` で再走査。lease単面/束の共通ビルド、lease_hostの
  全文取り込み、gfx_bootのsliceとgfx_sf_backendを使う2変異の当て先を確認。
  走査ログ: `/home/hight/os32-tmp/e3-review-scaffolds.txt`。
- e4申し送り: publisherはsourceをg_backend (gfx_sf_backend)から作る。
- e11申し送り: `exec/exec.c:ring3_ptr_ok` のVRAM範囲直書き (現行727行付近) も
  共有USER化撤去の対象。今回は変更しない。
- UC化のpresent性能: NP21/Wはキャッシュを模擬しないので差は出ない見込み。
  実機はMTRRがA0000〜BFFFFをUCにしていれば変化なし、WBだったならpresentは遅くなるが
  正しくなる方向。h (Ra266) でgfx_countersを計測する。
  §11-2に従いCG窓はWBのまま。I/Oでコードを切り替えた後に古いグリフが読める危険もhで確認。
- 今回の `make all` はrc=0。個別ログは `/home/hight/os32-tmp/e3-review-{bundle,single,gfx}.log`。
  単面の初回正常対照は足場編集時のTVRAM期待の重複でrc=1、当該重複だけ除去してrc=0。
  レビュー対応後の全体ゲート `e3-review-check-changed.log` / `.rc` はrc=0。
  束21/21・gfx17/17・単面18/18・query34/34・lease10/10、関連100変異。
  検査終了後に§2-2の公開予定名の誤置換1箇所をgfx_surface_leaseへ戻した。
  文書訂正後の `e3-review-check-changed-final.log` / `.rc` はrc=2。
  今回の関連100変異は通過したが、H3 FakeEmulator試験が共有TMPDIRの
  `os32-h3-live.lock` 競合により、期待するSTOP拒否より先にロック拒否を受けた。
  `test_h3_park_resume.py:setUp` のロック先だけ試験ごとの一時ディレクトリへ隔離。
  実flock/競合CLI拒否と全既存期待は維持し、運用側ロック/H3本体/実機は変更しない。
  個別H3は正常39試験＋ILP32状態42チェック、61/61 runtime RED、rc=0。
  ログ `e3-review-h3.log`。失敗後の再実行1回 (`e3-review-check-changed-retry.log`) は rc=0。
  検査中は票・ソースを固定。コミット/push/配備/NP21W/NHD/ini/実機は未操作。

**e3 の PM 検査 (2026-10-03)**: 独立レビュー (Opus 5.5) は 1 回目 Approve (条件付き、P2 1 件・P3 10 件) → 対応の差分確認で Approve
(残る P3: 予定形の記述 → 上で結果に訂正、exec/lease.c:6 の gfx.h の相対 include は害が無いので据え置き)。PM のホスト
(既定 `HOST32_RUNNERS=native qemu`) で `make all`・lint・`check-changed` rc=0 (修正前・修正後の版とも、`/home/hight/os32-tmp/pm{,2}-e3-{all,cc}.log`)。

**e3 の着地とゲスト受入 (PM、2026-10-03、main `5a47990`、NP21/W 17MB・今の ini)**: f1b と同時期の取り込みで build/sdk.mk・check_map.yaml が競合 → 両方を残して解消、TESTS.md と 02_memory.md を再生成。コミット済みの木で `make all`・`make check` rc=0。停止 → `nhd-pull` → `deploy-kernel` → `deploy` → 起動。`ver` の Commit `5a47990`、`/boot/vmkernel.lz4` 481,837 B 一致。**kselftest pass 272 / fail 0** (e3 の 2 項目を含む)、db・klibc・alloc・d0a・faulttest 4 種・loop/kloop + STOP・`v86 -t` OK、GUI (gui_demo → CUI) OK。**アプリ起動と `v86 -t` を通った後に master の低位 PT (物理 0x3F2000) を直接読み、PCD を照合**: TVRAM A0000-A3FFF・B/R/G A8000-BFFFF・E E0000-E7FFF は全ページ PCD=1、CG 窓 A4000-A7FFF・ROM C0000-DFFFF / E8000-FFFFF は PCD=0、全ページ present・恒等の frame — 塞いだ 2 経路 (exec の共有 USER 化、V86 teardown) を実物で確認。
- **既存の表示の不具合 (e3 とは無関係、記録)**: `v86 -t` の後でグラフィック表示がオンのまま残り (`grph_disp` 0→1)、VRAM に残っていた起動スプラッシュの「OS32」ロゴが CUI の背後に見える。e3 の直前のカーネル (`85ffa38`、f1b の worktree のビルド) でも同じ — 前からの挙動。kernel/boot_splash.c は「VRAM クリア → テキストモード復帰」と書くが VRAM のロゴが残っており、V86 の自己試験の後始末もグラフィック表示を止めていない。CUI の見た目だけの不具合で回収・資源には影響しない。直す段は PM が後で決める (候補: V86 の自己試験の出口でグラフィック GDC を停止 / スプラッシュの終わりで VRAM を実際に消す)。GUI を往復すると grph_disp=0 に戻る。

  **表示不具合の修正 (2026-10-03、基点 `e4fc688`、wt/dispfix、コーダー Codex gpt-6-astra (P3 対応は Codex gpt-6.1-sol))**:

  - 原因A: `gfx_state_for_os32` が CUI への出口でグラフィック GDC を START し、表示ページ0を見せていた。
    原因B: スプラッシュの終了時の `gfx_present` は flip の描画ページ1だけを黒にし、ページ0にロゴを残していた。
  - ユーザー決定 (§5、PM の参照資料): `v86 -t/-d/-b` の出口は常に CUI。
    `v86_cui_display_restore` に STOP → 68h表示可 → テキストSTARTを共通化し、通常出口と
    `gcap_cui_rebuild` から呼ぶ。通常出口の A4h/A6h はともに0。16色・400ライン・CSRFORM・
    SCROLL・パレット・カーソル復帰は保持、SYNCは追加しない。旧設計 `07_gfx_state.md` の
    「残す」は VRAM の内容保持と解釈し、V86終了時に消去せず表示だけ止める。
  - `gfx_clear_planar_pages` を `gfx_init` / `gfx_init_200` / スプラッシュ終了で共有。
    A6hで0/1を選び、4プレーンの表示領域をCPUで消去 ([HW1])。400ラインは各32000 bytes、
    200ライン初期化は従来どおり各16000 bytes。スプラッシュはPC98標準planar経路に限定済みで、
    shutdown直前に両ページを消す。ページ番号を定数化し、`pc98.h` の4Bh注記をCSRFORMへ訂正。
  - ハードウェア根拠 (本文の転載なし): `/mnt/c/WATCOM/docs/undocumented/io_disp.md`
    「グラフィックGDC」I/O 00A2h、「モードフリップフロップ1」DISP ENABLE、
    「VRAMプレーン切り換え」I/O 00A4h/00A6h。
    `PC9800Bible/2-7_グラフィック.md` §2-7-2・§2-7-3・§2-7-4、
    `PC9800Bible/3-2_グラフィック256色表示.md` §3-2-3・表3-4。
    A6hはCPUの書込先、68h表示可は両画面共通、グラフィック停止にはGDC STOPが必要。
    PEGC拡張時のE0000hはMMIOなのでplanar消去を拡張経路に流さない。
    Bibleとの不一致はUNDOCUMENTEDを優先。worktreeに `docs/hw/` が無いため指定の別置き資料で照合した。
  - **変更前の既存試験棚卸し**: `rg` で実物の取り込み・関数切り出しを調査。
    `test_boot_splash_native.py` / `boot_splash_native_host.c` の片ページ模型・転送スタブを
    両ページ模型と実物 `gfx_vram.c` に変更。設定4種×正常/異常後再試行の8条件について、
    既存のSTART/STOP回数、設定保持、optional機器非初期化、反復・失敗復帰の期待は維持し、
    両ページへの描画実績と消去を追加。既存の合格期待を弱める変更はない。
    `test_gfx_boot.py` は予約関数の切り出しで今回の初期化本体を含まない。
    `test_v86_gcap.py` は数理部分、`test_gui_gate.py` はGUI判定のため期待変更なし。
    `gui_gate.py` の判定は変更しない。
  - **回帰/変異**: 新設 `test_display_cleanup.py` はV86実物関数のOUT列・採取迂回・gcap復帰の3条件と
    スプラッシュ8条件、計11条件成功。14変異を写しの木で実施し、全14件が実行時RED
    (START復活、アクセス/表示ページ1、表示可/テキストSTART欠落、gcap共通復帰欠落、
    旧スプラッシュ終了処理、同ページ二度消去2種、半面消去、各プレーン欠落4種)。
    置換一致数は各1、プレーン欠落のみ各2に固定。コンパイル失敗・signalは検出数に含めない。
    基点のスプラッシュは8条件ともページ0残留でRED、基点のV86通常出口はOUT列でRED
    (採取迂回とgcapの2条件は成功)。修正後は既存2テスト/8条件も成功。
    足場作成中の未使用変数警告・未定義スタブは修正済み。32bit libcヘッダが無いため新設の
    libc依存足場は既存スプラッシュと同じLP64で実行し、ILP32の代用結果とは扱わない。
    実行口は `host32.run`、最終検査のILP32は `HOST32_RUNNERS=qemu` に固定する。
    `check-display-cleanup-host` を `tools/check_map.yaml` と生成 `docs/TESTS.md` に登録。
  - **サイズ** (同じcross、build ID差を含む): ELF text 703316→703108 (-208)、data 36731→36731、
    bss 606484→606484、kernel.bin 365124→364900 (-224)、vmkernel.lz4 481821→481696 (-125) bytes。
    ABI・KAPI版・リンカASSERTの変更なし。
  - **検証コマンド**: 共通環境 `CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp PYTHONPATH=`、
    `HOST32_RUNNERS=qemu`。`make all NP21W_DIR=/dev/null < /dev/null` は基点初回のみ
    未配置フォントでrc=2。本線の取得済みTTF2本をworktreeへコピー後の基点と修正後はrc=0。
    `/dev/null` 宛の任意D88コピー警告あり (NP21/Wへの配備はしていない)。
    `python3 tools/tests/test_display_cleanup.py --mutate`、`test_boot_splash_native.py` はrc=0。
    `python3 tools/gen_memmap.py --write`、`python3 tools/gen_tests_inventory.py --write`、
    `python3 tools/check_select.py --lint` はrc=0 (対応表漏れ0)。
    最終 `OS32_MUT_JOBS=4 HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`
    は本記録を書き終えてから実行し、実行中はソース/票を固定する。終了rcはコーダー最終報告と
    `/home/hight/os32-tmp/disp-check-changed.log` で確認する。
  - **未確認/実機受入**: 配備・NP21/W・NHD・ini・実機・commit/pushは未実施。
    PMが起動直後と `v86 -t/-d/-b` 後のグラフィック停止、text_on、A4h/A6h=0、ロゴ残留なし、
    GUI往復・`v86 -g -t`・9801でのgui_gate誤判定なしを確認する。
    Ra266ではCUIを撮影して確認し、31kHz機の200/400ライン往復、STOP1/STOP2の差、
    PEGC/planar VRAMの関係はh最終一式へ。GRCG/EGCをV86終了時に無効化しない件は
    未観測のまま記録のみ (今回変更しない)。A6h=1/flip無効の不一致は通常出口のA6h=0で解消。
  - **着地とゲスト受入 (PM、2026-10-03)**: Opus 5.5 の独立レビューは Approve (P1/P2 なし、P3 9 件は Codex gpt-6.1-sol が対応 — 正常 13・変異 16/16 が期待文言で RED、PM が差分を読んで取り込み)。main へ取り込み (`f7174c4`)。コミット済みの木で `make all` rc=0・`make check` rc=0。NP21/W を停止 → 停止確認 → `nhd-pull` → `deploy-kernel` → `deploy` → 起動 (17MB、今の ini — §12)。`ver` の Commit `d5fca8f`、`/boot/vmkernel.lz4` 481,688 B が手元と一致、**kselftest pass 272 / fail 0**。`/api/gdc`: 起動直後 `grph_on`=0・`access_page`=0 (修正前は 1)、`v86 -t` の後も `grph_on`=0・A4h/A6h=0・`text_on`=1、画面にロゴの残留なし。`v86 -g` と `v86 -g -t` の後も `grph_on`=0・A4h/A6h=0。GUI (gui_demo の窓 → ESC → CUI) OK、戻った CUI にロゴなし。カウンタ: `fault_kill_count`=0、深さ 0、`ledger_*_ops`=0、`exec_as_leftover_pages`=0、`irq_ctx_violations`=1 (起動時の基準値)。**観測 (記録だけ)**: `v86 -g` の後は 68h の状態 (`mode1` 0x89→0x99)、`crtc`、グラフィック GDC の SYNC/CSRFORM が ROM の設定のまま残る (表示は止まっていて CUI に影響なし、GUI の起動で設定し直される)。修正の前からあるかは未確認、GRCG/EGC・6Ah の件と同じく h の最終一式で見る。



### e4 実装結果 (2026-10-03、Codex gpt-6-astra)

基点 `f7174c4` (dispfix取り込み済み)、worktree `wt/e4`。
PM判断は作業依頼の `ref_e4.md` §8。独立レビュー (Opus 5.5) P2-1で
公開queryの起動前PC98固定によるpegcchk/hal_testの誤判定を指摘、PMが到達可能性を確認してQ3を訂正。
以下は訂正後のQ3に準拠する。
独立レビュー・ゲスト受入はPMへ。最終全体検査中は本欄/ソースを固定する。

**実装と判断の対応**:
- Q1: 公開型とは別の `struct gfx_kernel_fb` と `gfx_kernel_framebuffer`。
  CLIENTのbackingをP2V/P2V_IOで解決し、登録済みplane_offsetを足す。
  `bb_b/r/g/i` と `bb[]` はNULL開始、`gfx_bind_client` でreserveのPC98登録直後・
  init/init_200・prepareの選択/fallback時に設定。非constのPC98 bb_baseも同じ口。
  CLIENT不在時は旧pointerを消し、present/dirty/raster/scrollは何もしない。
  boot_splashは内部fbを取得。互換KAPIは両CPLともkernel aliasを返す現契約を維持。
- Q2: PC98登録は640×400/pitch80、stride32000、200行では各面先頭16000Bを使用。
  台帳の再登録・generation更新はしない。
- Q3: 起動済み状態をinit成功で設定、shutdown/prepare/init開始で解除。
  起動済みfbは選択backend queryのwidth/height/format、pitchはqueryに欄が無いので
  CLIENTから設定したbackend bb_pitchを使う。未起動の**内部fbだけ**PC98 CLIENTの640×400。
  公開gfx_screen_infoは基点どおり常に選択backend query、互換fbは選択CLIENTのbackingと
  同backend queryのgeometryを返す。prepare済みPEGCは640×480/pitch640でUSER写像とも一致。
  GFX_FMT_*とGFX_BB_*の一致はSTATIC_ASSERTで固定。
  PEGC/Cirrusのinit_200もnative 640×480。prepare後のpacked BB/400行の混在を解消。
- Q4/Q5: USERへの橋は未接続。`gfx_surface_source` は選択backendからCLIENT/DISPLAYを作り、
  planar DISPLAYは表のPFNを照合してB/R/G/I順に並べる。起動済みかつ面の存在がready条件。
  `.text.gfx_surface_source` は未参照でgc。生成からsnapshot/取得完了までcallback/schedulingなし。
- Q6/Q7: PEGC DISPLAYは窓の予約/写像成功後にVRAM/UC/RW、KERNEL、640×480/pitch640、
  75pageで登録。CLIENT確保失敗でもDISPLAYは残る。登録失敗はdisplay_failへ。
  Cirrus DISPLAYはNONEを維持、publisherはnot-ready/INVAL。
- Q8/Q9: 実3backend連結のILP32試験を追加、全runner正常/先頭runner変異へ登録。
  gfx_bootのS順/本数とPEGC DISPLAY属性、splashのCLIENTスタブ、display_cleanupの
  変異用ヘッダ閉包を更新。kselftestにPEGC DISPLAY属性の条件付き検査1項目を追加。
  **pass見込み272→273** (未配備なので273成功とは主張しない)。

**予算** (同一 `/home/hight/opt/cross`、make all、size/nm/map。build ID差を含む):

| 項目 | 着手時 | レビュー前e4 | P2/P3修正後 | 修正差分 |
|---|---:|---:|---:|---:|
| ELF text (SQLite込み) | 703,108 B | 704,276 B | 704,260 B | −16 B |
| ELF data | 36,723 B | 36,779 B | 36,779 B | 0 B |
| ELF bss | 606,484 B | 606,516 B | 606,516 B | 0 B |
| kernel.bin | 364,892 B | 366,132 B | 366,100 B | −32 B |
| vmkernel.lz4 | 481,688 B | 482,534 B | 482,538 B | +4 B |
| __bss_end | 0x18C934 | 0x18CE34 | 0x18CE14 | −32 B |
| ASSERT余白 (上限0x195000) | 34,508 B | 33,228 B | 33,260 B | +32 B |
| 圧縮余白 (上限520,192 B) | 38,504 B | 37,658 B | 37,654 B | −4 B |
| e枠残り (d6起点0x18C270) | 14,652 B | 13,372 B | 13,404 B | +32 B |
| 未結線分控除後のe枠残り | 12,027 B | 10,415 B | 10,447 B | +32 B |

未結線の既存2,625Bにpublisher332Bを追加控除。e4分は接続見込み込み1,580Bで
約11.8KB以内。ASSERT・上限・診断は緩和なし。

**P3対応**: (1) check_mapにhost fixture自身を登録、(2) 実Cirrus initのI/Oスタブで
probeを落としfallback直後のbb全4面/内部fb/互換fbを照合、bind除去変異を追加、
(3) splashはGDC START後にCLIENTのplanesを2へ減らし部分欠落で畳む枝を復元、
(4) formatのSTATIC_ASSERT、(5) prepareなしPC98→Cirrus切替とpacked bind除去変異、
(7) TDD冒頭を「設計票」にしてTESTS生成器がTASK_T2D_T2Hリンクを抽出できるよう修正。
P3-6はP2-1で解消、P3-8は従前の記録を維持。

**試験とコマンド**: [gfx_kernel_fb_tdd](../../../tools/tests/gfx_kernel_fb_tdd.md)。
共通環境 `TMPDIR=/home/hight/os32-tmp PYTHONPATH= HOST32_RUNNERS=qemu`、
makeは `CROSS_DIR=/home/hight/opt/cross`、`NP21W_DIR=/dev/null < /dev/null`。
- 新規fb: `python3 tools/tests/test_gfx_kernel_fb.py --runner qemu --mutate` rc=0、
  **1 suite/正常496チェック、10/10 runtime RED**。実gfx_core/3backend/pgalloc/paging/B1/query/lease。
  probe/識別とI/O・IRQ/MMUを足場化。3backend lifecycle、fallback、欠落CLIENT、PEGC DISPLAY lease。
- 既存gfx_boot: `python3 tools/tests/test_gfx_boot.py --mutate` rc=0、18試験/17変異。
  display_cleanup: 同名scriptの`--mutate` rc=0、13条件/16変異。
  splash_native: 同名script rc=0、2 unittest/8構成。
  上記の独立した変異集合は合計**43本** (新規10+既存33)。
- 初回の新規試験足場はinclude/宣言/caller保存/IRQ変数の不整合で失敗→修正。
  display_cleanup初回変異はコピー先のsurface_query.h欠落でコンパイル失敗→閉包を補って再実行rc=0。
  コンパイル失敗は変異REDに算入しない。lint初回の依存ヘッダ漏れと追記の字下げ不一致も修正。
- 開始時/実装後の `make all NP21W_DIR=/dev/null` はともにrc=0。
  `/dev/null`へのD88コピー警告は配備抑止の指定によるもの。実配備なし。
- `python3 tools/gen_memmap.py --write` / `python3 tools/gen_tests_inventory.py --write` /
  `python3 tools/check_select.py --lint` はrc=0、123検査/対応表漏れ0。
- レビュー前の最終ゲートは初回rc=0 (ログ `e4-check-changed.log`)。
  P2/P3修正の個別fb試験は初回void関数のCHECK誤用でコンパイル失敗、修正後496チェック/10変異rc=0。
  splashは2 unittest/8構成rc=0。修正後make all rc=0 (`e4-p2-all.log`)。
- 修正後の最終ゲートは本欄を固定して
  `OS32_MUT_JOBS=4 HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`。
  ログ `/home/hight/os32-tmp/e4-p2-check-changed.log`。**初回rc=0、再実行なし**。
  新規fb正常496チェック/10変異RED、C方言27/27変異RED・5/5対照GREEN。
  make定義差分によりfull選択。実行中は票/ソースを固定し、終了後に結果のみ追記。
  修正後のgen_memmap/gen_tests_inventory --writeとcheck_select --lintもrc=0 (123検査、漏れ0)。

**e5以降への申し送り・持ち越し**:
- Q2: 200行でpitch×200へSURFACEを再登録しgeneration更新するかはe5の論点。
  現在の公開予定queryは登録geometry (400行) を返し、内部描画fbは200行の有効範囲。
- Q7: Cirrus DISPLAYのNONE→RWは全画面授権の接続段 (e6以降)。
- Q10: dispfixの `v86_cui_display_restore` はGDC状態のみ。
  gfx_current_height/flip状態との整合はe5へ持越し、e4ではV86を変更しない。
- e5はrevoke/TLB/参照0→再init/generation/fallbackとpublisherの接続、
  e6/e11は互換USER lease橋、e8はTVRAM/Unicode。`exec_map_shared_bb` はe11まで維持。
  sdk/kapi.json・機能版・memory世代・SDK生成物は無変更、公開はe11。
- NP21/W・NHD・配備・ini・実機・commit/pushは未操作。
  native ILP32とゲスト受入は未実施。§12どおり構成依存の一括確認はT2hへ。


**e4 の着地とゲスト受入 (PM、2026-10-03)**: 独立レビュー Opus 5.5 は 1 往復目 Request changes (P2-1: 起動前の公開 `gfx_screen_info` が PC98 固定になり pegcchk / hal_test が誤判定 → PM が Q3 を訂正し、Codex gpt-6-astra が直した、P3 6 件も同時) → 2 往復目 Approve。PM のホスト検査 `HOST32_RUNNERS="native qemu"` の check-changed rc=0 (新試験の正常対照は native と qemu で PASS 496)。main へ取り込み (`ac0a728`、`docs/02_memory.md` の競合は生成物なので再生成)。コミット済みの木で `make all` rc=0・`make check` rc=0。NP21/W を停止 → 停止確認 → `nhd-pull` → `deploy-kernel` → `deploy` → 起動 (17MB、今の ini — §12)。`ver` の Commit `ac0a728`、`/boot/vmkernel.lz4` 482,526 B が手元と一致、**kselftest pass 273 / fail 0** (PEGC DISPLAY の 1 項目)。`db_test` 9/9、`db_v50_test` 41/41、`klibc_test` 49/49、`alloc_demo` 16/16、`d0a_test` 全行 OK、faulttest gp/de/ud/pf は 4 件とも `-> kill app`、loop・kloop + CTRL+STOP、`v86 -t` OK。**gfx**: 今の ini は PEGC が選ばれる構成で、`hal_test` は `backend pegc (packed 8bpp)`、`pegcchk` は SKIP せず 640x480 に入り CUI に戻った (起動前の公開 query が選択 backend を返す — P2-1 の直しをゲストで確認)。GUI (gui_demo の窓 → ESC → CUI) OK。終わりのカウンタ: `fault_kill_count`=7 (faulttest の内訳)、深さ 0、`ledger_*_ops`=0、`exec_as_leftover_pages`=0、`irq_ctx_violations`=1 (起動時の基準値)。**未実施**: `gfx200_test` (キー待ちの画面デモで rshell から操作できない。PEGC 構成では init_200 が 480 行で起動し PC98 の 200 行経路は通らない) — PC98 の 200 行と planar 構成は e5 の 200 行再登録と合わせて T2h の構成変更の確認へ。

### e5 実装結果 (2026-10-03、コーダー GPT-6-astra)

**対象**: 再 init / revoke / fallback、旧 token 拒否と第三 AS の不変。
PM 参照資料 ref_e5 §8 Q1〜Q12・§9・§10 の訂正を適用。公開 KAPI、機能版、memory 世代、
SDK 生成物、AS の大きさ、8 lease/AS・16 SURFACE、ASSERT は変更しない。

**実装と PM 判断の対応**:
- Q1/Q2/§9: `gfx_reinit_surfaces` に revoke と regen の master 区間を集約。
  旧 backend、選択 backend、probe 失敗で加わる PC98 を対象にし、
  同一呼出し中に一度処理した sid は重複 revoke/regen しない。
  backend の選択に応じた revoke 区間と最後の regen 区間の各出口で
  caller CR3 を再ロードし、保存 IF を戻す。長い init/shutdown/bind は caller 文脈。
  walk/acquire の root 述語は不変。publisher の呼び手は未結線 (e11)。
- Q3/Q4/Q5: `ledger_surface_regen` は master・通常文脈・参照0・非closing・
  gen未枯渇を要求し、geometry を validate して gen+1。
  sid/backing/owner と RAM 内容・padding は保存。PC98 CLIENT は200行でも
  640×400・offset 32000刻み、DISPLAY 4面もheight=400のまま、最後にkernel bbを再bind。
  prepare/init/init_200/shutdown、切替時の旧面、fallback の面を更新。
  **「e4 Q2の200行持越しは閉じる」を取消**。レビューP2-2を受けPMがQ5を(a)へ戻した。
  SDKの固定stride/heightと整合させ、各面先頭16000 Bだけを200行で使用する。
  P2-1は400→200→400の全表示範囲消去と200行後半の保持、消去範囲変異で固定。
- Q6/Q7: 区間の間は gfx_started=0。regen でも参照0を再確認。
  sid の not-ready bitmap により、revoke未完了/世代枯渇の role は INVAL、
  kernel backing は維持。gfx_reinit_fail_count はregen失敗時に一度だけ数える。
  bitmap幅はSTATIC_ASSERT、gen番兵はpgallocと共有するLEDGER_SURFACE_GEN_MAX。
  旧 token を保存/復活する経路は作らない。CLIENT欠落は従来どおりnot-ready。
- Q8/Q9: `lease_revoke_sid` と appslot 列挙の `lease_revoke_surface` を追加。
  `lease_revoke_all` は最後まで処理し、失敗slot数を返す。
  lease_revoke_fail_count で数え、exec teardown は戻り値を診断出力へ使う。
  解除済みtokenのreleaseはINVAL、旧refのacquire/queryはSTALE。
- Q11: boot→gshell CLIENT は先にrevoke、参照0・gen余裕を確認して移譲/gen+1。
  同一ownerへの繰返しは無変更。所有者交代時の再initも同じrevoke経路。
  revoke失敗・gen枯渇では移譲せずnot-ready。IF/CR3復元とともに試験で確認。
- Q12: 新規ILP32試験 `check-gfx-reinit-host` を全runner正常対照の8本目に登録。
  実3backend・lease/query/paging/pgallocを連結し、3 ASのappslot足場で確認。
  詳細は [gfx_reinit_tdd.md](../../../tools/tests/gfx_reinit_tdd.md)。
  kselftest は regen/backing保持を1項目追加し、**pass 274 / fail 0 の見込み**
  (e4の273から+1、ゲスト未測定)。

**予算実測** (同一CROSS_DIR、kernel.map/stat。3,000 B上限):

| 項目 | 着手時 | 実装後 | 増分 |
|---|---:|---:|---:|
| __bss_end | 0x18CE14 | 0x18D358 | +1,348 B |
| kernel.bin | 366,092 B | 367,444 B | +1,352 B |
| vmkernel.lz4 | 482,526 B | 483,344 B | +818 B |
| ASSERT残り | 33,260 B | 31,912 B | −1,348 B |

e5予算残り1,652 B。e残枠は13,404→12,056 B。
レビュー修正前比: __bss_end −160 B、kernel.bin −160 B、vmkernel.lz4 −134 B。
未結線入口の接続時の再計測はe11へ (publisherのnot-ready判定も含む)。
圧縮520,192 B上限まで36,848 B。backingの追加確保は無し。

**試験・実行記録 (レビュー修正前 — 最終値は下の「レビュー修正の実行記録」)**:
- 環境: CROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、
  PYTHONPATH空、HOST32_RUNNERS=qemu。makeはNP21W_DIR=/dev/null・stdin=/dev/null。
- 新規正常対照はAのCR3/master・IF=1/0、BのCLIENT/DISPLAY、
  master/Cの全PD/PT・lease表/参照数不変、旧ref/token、200⇄400、fallback、
  not-ready区間の取得拒否、revoke部分失敗/継続、gen上限、移譲を確認。
  レビュー修正後は**11/11変異runtime RED**。compile/timeoutは不算入。
  直接acquireの区間間負例とregenの再確認、packed両系のcaller ASも通す。
- 既存個別: fb496チェック/10変異、lease10変異、gfx boot17変異、
  display_cleanup16変異を維持。exec_r1とapp_bb_overlap(15変異)もrc=0。
  置換が変わった既存変異は当て先/期待FAILを更新し、検出目的を維持。
- 部分make kernel初回はkmemcpy宣言不足でrc=2、配列コピーへ修正後rc=0。
  新規試験初回はcaller frame未設置でquery期待に失敗、足場修正後rc=0。
  個別検査起動1回は存在しないcheck-gfx-boot-host指定でrc=2、実際のPython入口で再実行。
  既存boot変異1回は置換先不一致、fb変異1回はFAIL文言不一致、修正後すべてruntime RED。
  補助check-fastは試験一覧生成前でrc=2 (check-tests-inventory)。
- `make all NP21W_DIR=/dev/null` は初回/最終ともrc=0。
  gen_memmap --write、gen_tests_inventory --write、check_select --lint はrc=0
  (124検査・漏れ0件)。/dev/nullへのD88コピー警告は指定による配備抑止。
- 最終ゲートは票/ソースを固定して
  `OS32_MUT_JOBS=4 HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`。
  ログ `/home/hight/os32-tmp/e5-check-changed.log`、終了コードは同名 `.rc`。
  **初回rc=0、再実行なし (make定義差分でfull選択)**。
  新規e5は10/10、fbは496チェック/10変異、単面18/18、束21/21、
  query34/34がruntime RED。C方言27/27 RED・正常対照5/5 GREEN。
  既存Windows opt-inは計5件skip。検査中は票/ソースを変更せず、
  終了後にこの結果だけを追記した。

**レビュー修正の実行記録**:
- 個別検査でSTATIC_ASSERT引数不足、packedのホストBIOS未写像、変異期待文言、
  既存fb試験の実表示高/登録高の混同を検出し修正 (各rc=1)。正常対照と変異は修正後rc=0。
- make all rc=0。gen_memmap/gen_tests_inventory --write、check_select --lintと
  最終check-changedの結果は修正後ログ e5-revision-* と完了報告で記録する。
  初回check-changedはrc=2: gfx_boot足場の未使用gfx_current_heightが-Werror。
  全ジョブ終了後にその変数を削除。gfx_bootは18試験 (17構成+静的検査)・17変異でrc=0。
  再実行check-changedは**rc=0** (full選択、2回目で完了)。
  ログ `/home/hight/os32-tmp/e5-revision-check-changed-2.log`、同名 `.rc` に0。
  新規e5の11/11変異、fb502チェック/10変異、boot17変異、C方言27/27 RED・5/5 GREEN。
  gen_memmap/gen_tests_inventory --write、check_select --lintもrc=0 (124検査・漏れ0)。
  この結果だけを全ジョブ終了後に追記。
  最終検査中は票/ソースを固定する。

**e6以降への申し送り**:
- Q5訂正: planarのpitch×height再計算はe6/e7のchecked attachへ持越し。
  SDK監査はgfx_fill.cのy切り、gfx_dump.cの32000固定、asm_draw.asmのGFX_PLANE_SZ、
  libos32mgxのMGX_MAX_PLANEの4箇所を併せて行う。e5でSDKは変更しない。
- P3-2: Cirrus prepareでinit後probe偽ならPC98面が一度not-readyになる。
- P3-3: gfx_shutdownのKAPIには門が無い。e11のlease結線時にDoSを検討する。
- P3-4: slot再利用時にnot-ready bitが残る件はe8で扱う。
- P3-6: teardownのrevoke失敗でlease_countが残り、以後regenできない。
- P3-8: 区間2のirq_restoreからbindまで窓がある。IRQ文脈でsfを読む描画が無い前提。
- e6/e7はgen変更後のC静的/shlib両方の再attach、旧pointer/tokenの破棄。
  e11でpublisherを結線するときも、source生成から取得完了までcallback/scheduling禁止、
  not-readyを無視して古いsource値から取得しない契約を保つ。
- Cirrus DISPLAYのNONE→RWは授権接続段へ。TVRAM/Unicodeはe8。
- **Q10**: V86出口のgfx_current_height/flip整合はe10bへ持越す。
  V86本体とdisplay_cleanupの変異定義は変更しない。
- native、NP21/W、gfx200_test/gfx_demo200、実機の受入は未実施。
  構成依存は§12どおりT2hへ。独立レビューとゲスト受入はPMへ。
  commit/push・NP21/W・NHD・配備・ini・実機は未操作。
- `ledger_surface_regen` の geometry を変える口は本番の経路で使わない (試験の validate 拒否だけ)。e6/e7 で 200 行の pitch×height 再登録に使うときは、padding のゼロ化 (APPBAND:140) を一緒に決める (独立レビュー 2 往復目 P3-4、記録)。


**e5 の着地とゲスト受入 (PM、2026-10-03)**: 独立レビュー Opus 5.5 は 1 往復目 Request changes (P2-1: 200⇄400 で旧 offset のまま BB を消す、P2-2: 200 行で 16000 刻みにすると SDK の 32000/400 前提で隣の面を壊す → PM が Q5 を (a) に戻した、ref_e5 §10) → Codex gpt-6-astra が修正 → 2 往復目 Approve (残る P3 は PM が 3 件直し 1 件記録)。PM のホスト検査 `HOST32_RUNNERS="native qemu"` の check-changed rc=0 (新試験の正常対照は両 runner で PASS)。main へ取り込み (`e28f7de`、票は e4 の受入記録と e5 の節の両方を残して競合を解消)。コミット済みの木で `make all` rc=0・`make check` rc=0、push。NP21/W を停止 → 停止確認 → `nhd-pull` → `deploy-kernel` → `deploy` → 起動 (17MB、今の ini — §12)。`ver` の Commit `953656d` (文書の取り込み後の HEAD、カーネルのソースは e5 と同じ)、**kselftest pass 274 / fail 0** (regen の 1 項目)。`db_test` 9/9、`db_v50_test` 41/41、`klibc_test` 49/49、`alloc_demo` 16/16、`d0a_test` 全行 OK、faulttest gp/de/ud/pf は 4 件とも `-> kill app`、loop・kloop + CTRL+STOP、`v86 -t` OK。カウンタ: 深さ 0、`ledger_*_ops`=0、`exec_as_leftover_pages`=0、`irq_ctx_violations`=1 (起動時の基準値)。USER の lease は 0 本なので、ゲストでは gen が進むだけ (再 init の経路は gfx_init/shutdown の通常の回帰で通る)。**照合の注記**: `/boot/vmkernel.lz4` は 483,335 B、手元は 483,339 B。ゲストの Build は 11:02:16 (`deploy-kernel` が組んだもの)、手元の `build/out` はその後の `make deploy` が 11:02:41 に組み直したもの (build_id の時刻だけが違い、圧縮後の大きさが 4 B 変わる)。サイズの照合は「NHD に書いたビルド」と比べる必要がある — 手順側の穴として記録 (e4 までは偶然一致)。

### e6 実装結果 (2026-10-03、Codex GPT-6)

基点 `953656d`、worktree `wt/e6`。PM 判断は作業依頼 `ref_e6.md` §8 の
Q1〜Q11 を優先し、逸脱なし。独立レビュー・ゲスト受入は PM へ。

**実装と PM 判断の対応**:
- Q1/Q5/Q6/Q7: `libgfx_attach_internal.h` に CLIENT query/lease/unlease の内部 port。
  SDK 配布から除外する `_internal.h` 命名。公開 KAPI 型を先取りしない独立の値型で、
  `libos32gfx_attach_checked(void)` と `libos32gfx_check(void)` は 0/負の rc を返す。
  `gfx_api` は既存の api 引数付き void attach/init で設定する (checked を直接呼ぶときは
  呼出側が先に設定)。port は e6 本番では NULL、CPL=0 も旧経路の内部 backing。
  port のある USER は旧 gfx_get_framebuffer に fallback しない。
  同じ sid/gen は query だけで token/view を再利用。変更時は旧 state を消し、release 後に取得。
  失敗では取得済み token を返し、gfx_fb 全体をゼロ・gfx_ready=0 にする。
  C の framebuffer 描画入口・asm 呼出しの包みに ready の門を付ける。
  present/raster 入口は check を呼び、同世代なら query 1 回だけ。
  check は surface/sprite pool を初期化し直さない。
- Q2/Q3/Q4: `gfx_framebuffer_bridge` を `.text.gfx_fb_bridge` に未結線で実装。
  保存 caller を使い USER にだけ CLIENT lease VA、TRUSTED には内部 framebuffer。
  AS の既存 flags に `AS_LEASE_GFX_COMPAT` を付け、互換 token を 1 本だけ再利用する。
  flags の印は lease_check の PTE 照合から除外し、AS のサイズは増やさない。
  失敗は checked copy で framebuffer をゼロにしてから abort_req を立て、
  `gfx_bridge_fail_count` に数える。copy 自体が拒否された不正出力先には書かない。
  取得から copyout まで callback/scheduling なし。既存 KAPI の binding は無変更。
- Q8/Q9: 200 行でも登録 height=400、stride=32000 を維持。query は登録 geometry、
  再取得時だけ既存 screen_info で有効 height (200/400、packed は480) を照合する。
  同世代の check には screen_info を追加しない。Cirrus DISPLAY は NONE のまま。
- Q10: 実 SDK core/描画入口と実 query/lease/gfx_core/3 backend を連結する
  `test_gfx_attach.py` を追加。正常 **230 条件**、必須8種を含む **12/12 runtime RED**。
  e4 **502 条件・10/10**、e5 **11/11** 変異も成功。
  build/sdk.mk・check_map に登録、08_build の全 runner 列は nano_adapter も含む10本へ。
  f2 側の追加は未混入。詳細は [gfx_attach_tdd.md](../../../tools/tests/gfx_attach_tdd.md)。

**Q11 予算 (同じ /home/hight/opt/cross toolchain、byte)**:

| 項目 | 前 | 後 | 増分 |
|---|---:|---:|---:|
| kernel.bin | 367,436 | 367,440 | +4 |
| vmkernel.lz4 | 483,339 | 483,335 | −4 |
| __bss_end | 0x18D358 | 0x18D358 | 0 |
| kernel 本体 (BSS/整列込み) | 578,392 | 578,392 | 0 |
| ASSERT 残り | 31,912 | 31,912 | 0 |
| e 枠残り | 12,056 | 12,056 | 0 |
| libos32gfx.a | 66,708 | 69,514 | +2,806 |
| libos32gui.shlib | 129,568 | 129,568 | 0 |
| shlib BSS | 17,012 | 17,052 | +40 |
| gshell.bin | 225,064 | 226,312 | +1,248 |
| hello_gfx.bin | 21,004 | 22,252 | +1,248 |

ELF 全体 (SQLite なども含む) の size は text/data/bss が
705524/36835/606520 → 705540/36839/606520。before は clean 基点、after は dirty
build ID を含むのでバイナリ/圧縮の差をコード増分だけとは扱わない。
bridge の text 915 B と counter 4 B は map の discarded、nm に未出現。
本体正味増分 0 B は e6 上限2,000 B以内。shlib はページ整列内に収まり
text_pages=26/data_pages=10 を維持。SDK/shlib の勘定は kernel 枠の外。

**Q8 の 32000/400 前提の監査一覧 (定数/asm の変更なし)**:

| 場所 | 前提 |
|---|---|
| userland/lib/gfx/geom/gfx_fill.c | y を400、x を640で切る (max_y、gmin/gmax、x_end) |
| userland/lib/gfx/draw/gfx_dump.c | VDP1 の保存/読込が各面32000 B固定 |
| userland/lib/gfx/asm/gfx_const.inc | width640/height400/BPL80/PLANE_SZ32000 |
| userland/lib/gfx/asm/asm_draw.asm | clear の4面32000 B、pixel/line の400行クリップ (142–176、844–848付近) |
| userland/lib/gfx/asm/asm_sprite.asm | 101–133付近の画面端クリップが固定 geometry |
| userland/lib/gfx/draw/gfx_raster.c | present_with_raster の dirty rect が640×400 |
| userland/lib/mgx/libos32mgx.h | MGX_MAX_PLANE=32000 はファイル形式上限、BB strideとは別 |
| sdk/rust/os32api/src/lib.rs | present() の dirty rect が640×400 |
| userland/tests/blit_test.c | snapshot_bb が80×400 B/面をコピー |

**検証と申し送り**:
- `make all NP21W_DIR=/dev/null < /dev/null` rc=0。memmap/test inventory は生成器で更新。
  check_select --lint は125検査・漏れ0。本番で bridge/query/lease が未結線であることを map/nm で確認。
  全体検査は票/ソース固定後に check_slot.sh e6-coder 経由で check-changed を実行し、
  3回目で rc=0。HOST32_RUNNERS=qemu、保守的な full 選択で全変異を実行。
  slot 0 を取得でき、直接実行への fallback は不要だった。
  ログは `/home/hight/os32-tmp/e6-check-changed.log`。この結果記録は検査終了後に追記。
  初回は rc=2: boot_splash の非gcリンクが未結線bridgeの依存を要求し、P2V検査も
  lease VA の cast を検出。終了後、試験リンクへ --gc-sections を付け、[C5] の
  例外表に関数と「lease VA、物理変換不要」の理由を記録して再実行する。
  初回ログは `/home/hight/os32-tmp/e6-check-changed-first.log`。
  2回目も rc=2: 同じ足場を独立にリンクする display_cleanup 実行器にも
  --gc-sections と変異用コピーの appslot.h が必要だった。実行器だけを修正し、
  既存16変異の定義/期待理由は維持。正常13条件・16/16 runtime REDを単独再検査してrc=0。
  ログは `e6-check-changed-second.log`。
- e7: shlib/gshell の復帰、待ち/yield後の check、2実体の一括 attach/失敗巻戻しを結線。
  `libos32gfx_detach()` は内部 port に対応する実体自身の token を返すので、片側失敗時は
  両側で呼んでからエラーにする。port の変更は attach 前または detach 後だけ。
- e11: 内部 port を生成 query/lease/unlease に束ね、compat bridge の KAPI を結線。
  screen_info を再取得時に使う geometry 契約、保存 caller/B1 と失敗出口を維持する。
  sdk/kapi.json・機能版69・memory世代・KAPI生成物・exec_map_shared_bb は無変更。
  Cirrus DISPLAY NONE→RW、旧USER写像撤去、生成/世代一式は同段。
- 16000 B stride 化は SDK/asm 全面監査が必要。Q8 に従い PM のユーザー報告と
  DOCS_REORG_T3 §4 の論点へ渡す。e5 の再登録申し送りを e6 で実行しない。
- e5 P3-3 (shutdown の門)、P3-6 (revoke 失敗で参照が残る) は既存の持越し。
  native runner、NP21/W、NHD、配備、ini、実機は未実施。構成依存は §12 の一括確認へ。
  exec/appmem.*、apps/game、commit/push は未操作。


**独立レビュー修正 (2026-10-03、Codex gpt-6-astra、Opus 5.5 Request changes 対応)**:
- P2-1: checked attach は refresh の失敗時も gfx_api があれば surface/sprite を初期化。
  両 init は既存スロットをリセットするため、SDK 実体ごとの初回だけ呼ぶ。
  初回 FULL → slot 返却 → present 回復 → surface/sprite 作成・描画と、
  再 attach 後も既存オブジェクトを保持する正常対照を追加。
- P3-1: bridge 失敗の abort は保存 caller が USER、current >= APP_ID_MIN、
  current slot が RUNNING の場合だけ。TRUSTED 失敗でもカウンタは増やすが abort しない。
- P3-5: tilemap の compose 4入口と present、md の page/statusbar の C 入口で
  gfx_ready=0 なら何もしない。Rust Painter は e7。
- P3-7: gfx_attach_tdd の票リンクを追加し、TESTS.md は生成器で再生成。
- 追加変異3本 (失敗時 init を飛ばす、再 attach でプールをリセット、TRUSTED abort)。
  qemu ILP32 は **268 条件 / 15/15 runtime RED**、単独実行 rc=0。
  P3-5 は make all でビルド確認 (専用 runtime 試験・ゲスト検証は未実施)。

今回の修正前 → 修正後 (B、基点との差分表は上記):

| 項目 | 修正前 | 修正後 | 増分 |
|---|---:|---:|---:|
| kernel.bin | 367,440 | 367,444 | +4 |
| vmkernel.lz4 | 483,335 | 483,347 | +12 |
| __bss_end | 0x18D358 | 0x18D358 | 0 |
| libos32gfx.a | 69,514 | 69,698 | +184 |
| libos32gui.shlib | 129,568 | 129,568 | 0 |
| shlib BSS | 17,052 | 17,052 | 0 |
| gshell.bin | 226,312 | 226,376 | +64 |
| hello_gfx.bin | 22,252 | 22,316 | +64 |

kernel 本体正味増分は引き続き0 B (基点比、上限2,000 B)、bridge は gc で未結線。
ELF text/data/bss は 705540/36843/606520。kernel data は build ID を含む。
`CROSS_DIR=/home/hight/opt/cross make all NP21W_DIR=/dev/null` rc=0。
初回の CROSS_DIR 未指定は libc/libgcc の探索失敗で rc=2、設定して再実行した。
ログ: `/home/hight/os32-tmp/e6-review-build-retry.log`、`e6-review-attach.log`。
全体検査はソース・票固定後、
`CROSS_DIR=/home/hight/opt/cross /home/hight/os32-tmp/bin/check_slot.sh e6-coder env HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`
で **rc=0** (slot1、full 選択、直接実行への fallback なし)。
ログ: `/home/hight/os32-tmp/e6-review-check-changed.log`。この結果だけ検査終了後に追記。
`gen_memmap.py --write`、`gen_tests_inventory.py --write`、`check_select.py --lint` も rc=0
(125検査、対応表漏れ0)。NP21/W・配備・native runner は未実施、コミットはPMへ。

レビュー指摘の申し送り (この段では記録のみ):
- P3-2 → e11 互換表: pre-init USER は bridge で abort するが現 KAPI は boot PC98
  CLIENT を返す。TRUSTED 経路の選択も bridge は gfx_started、現 KAPI は selected=1。
- P3-3 → e11: bridge 本体は __cdecl でない。公開時の kapi wrapper で呼出規約を合わせる。
- P3-4 → e11: port 経路の gfx_screen_info に版の門はない。結線時は版70以上を前提にする。
- P3-6 → e7: SDK の再利用判定は sid/gen だけで token の生存を見ない。
- (2 往復目のレビュー、記録) 再 init でプールを空きに戻さなくなった — 旧コードは attach / init のたびに surface・sprite の枠を全部空きに戻していた。今は実体ごとの初回だけ (生きている surface を壊さないため、こちらが正しい)。init → 作る → shutdown → init を free せずに繰り返すアプリは 16 枠を使い切る。in-tree で 2 回 init する呼び手 (bench_scale2x、bench、game、libos32gui) はその間に作らないので実害なし。公開ヘッダの注記に「再 init はプールを戻さない」を e7 で足す。
- (2 往復目、→ e7・e11) tilemap と md は `libos32gfx_check()` を呼ばないので、e11 で port を結線した後に `gfx_ready=0` になると回復の経路が無い。e7 で帰路の check を配線するときに扱う。
- (2 往復目、→ e11) P3-1 の直しで、`caller_access_get` が失敗した (`!valid`) USER では abort しなくなった。e11 の KAPI 経路では dispatch が常に frame を張るので届かない — 結線のときの確認項目。

**e6 の着地 (PM、2026-10-03)**: 独立レビュー Opus 5.5 は 1 往復目 Request changes (P2-1: attach に失敗した後 check で回復すると surface/sprite のプールが未初期化のまま ready=1 → gfx_create_surface が NULL を読む。e6 の本番 (port が NULL) には届かない、e7・e11 の結線で届く) → Codex gpt-6-astra が直した (P3-1・P3-5・P3-7 も同時) → 2 往復目 Approve (レビュアーの再現が通り、再 attach を越えて既存の surface が残ることも確認)。PM のホスト検査は native の単体と `check_slot.sh` 経由の `HOST32_RUNNERS="native qemu"` check-changed で rc=0。

**e6 のゲスト受入 (PM、2026-10-03)**: main へ取り込み (`328c379`、f2/f3 と sdk.mk・08_build・TESTS・票が競合 → 両方を残し、全 runner の列挙を実物の 12 本に、TESTS.md は生成器で再生成)。コミット済みの木で `make all` rc=0・`make check` rc=0 (h3fix2 の取り込みとまとめて 1 回)、push。NP21/W を停止 → 停止確認 → `nhd-pull` → `deploy-kernel` (直後の `vmkernel.lz4` 483,339 B を控えた) → `deploy` → 起動 (17MB、今の ini — §12)。`ver` の Commit `328c379`・Image 483,339 B が控えと一致、**kselftest pass 274 / fail 0**。回帰の一式 (db_test 9/9、db_v50_test 41/41、klibc_test 49/49、alloc_demo 16/16、d0a_test、faulttest gp/de/ud/pf は 4 件とも `-> kill app`、loop・kloop + CTRL+STOP、`v86 -t`) OK。gfx: `hal_test` は `backend pegc (packed 8bpp)`、`pegcchk` は 640x480 に入って CUI に戻った、GUI (gui_demo の窓 → ESC → CUI) OK (新しい SDK を同梱した shlib で描画)、`hello_gfx` (Rust static、新しい SDK) を GUI から起動して全画面の描画を確認。カウンタ: 深さ 0、`ledger_*_ops`=0、`exec_as_leftover_pages`=0、`irq_ctx_violations`=1 (起動時の基準値)。**既存の観測 (e6 とは無関係、記録)**: GUI から起動した全画面アプリ (hello_gfx) が KAPI `kbd_getchar` で待つと、`/api/key` で注入したキー (`SPACE`、`a`) で終わらない — e6 より前の SDK で組んだ hello_gfx (wt/f4 のビルドを `/host/test/hello_old.bin` に置いて同じ手順) でも同じだったので前からの挙動。CTRL+STOP で畳める。キーの経路 (gshell の全画面所有者への配送、または注入の経路) の調査は別件で、h の最終一式までに見る。

**全画面アプリへの注入キーが届かない件 — 原因とユーザー決定 (2026-10-03)**: 原因は T8 (2026-09-12) の設計の穴。GUI 中の `kbd_getchar` は注入リング (`kbd_inject`) だけを見るが、注入できるのは con_sink の読み手 (端末) だけ (kernel/kbd_inject.c:106-118)。gshell は raw キーをフォーカス窓のリングへ配るだけで、全画面の所有者へ注ぐ経路を持たず、宛先が無ければ捨てる (userland/gshell/src/input.rs:479-481・:504-520)。起床条件 `key_ready` は注入リングの未読だけ (multiapp.rs:582-590)。Run から直接起動した全画面アプリには担い手の端末が居ないので WAIT_KEY のまま。CTRL+STOP だけは全画面の所有者宛ての別経路 (T8 D4d) で効く。**ゲストで確認**: hello_gfx の起動で `ring3_kbd_park_count` 2→3・`g_gfx_owner` 1→2、`a` の注入で raw リングの head 16→18 (make/break)・未読 0 (gshell が読み捨て)、`g_inj_count` 0 のまま、`ring3_switch_count` 不変。付随: 端末はプロンプト中は注入しない (票 E2) ので「端末を開いて Run」でも届かない見込み (未確認)。**ユーザー決定 (2026-10-03)**: e11 の公開 KAPI 一括に入れる — gshell が全画面の所有者 (端末の子でないもの) へ raw キーを変換して注入する経路と、カーネルの注入の権限 (全画面中の owner 1、または専用の KAPI) を e11 で足し、版の更新を 1 回にまとめる。それまでは既知の制限 (Run から直接起動した全画面アプリはキーで抜けられない、CTRL+STOP で畳む)。e11 の受入に「Run から全画面アプリ → キーで終わる」「端末の子は二重に注がない」「窓へ漏れない」「WAIT_POLL 型 (gfx200_test) も届く」を足す。
  revoke 経路を増やす際の前提として再検討する。

### e8b 実装結果 (2026-10-03、Codex gpt-6.1-sol)

基点 `5159cd5` (main)、worktree `wt/e8b`。PM `ref_e8b.md` §8 Q1〜Q5 / §9 に沿い、逸脱なし。
公開slot/target・sdk/kapi.json・版・KAPI生成物・D7・scratch定数名は無変更。
userland/lib・userland/rust・userland/gshell・sdk/rust と apps/game は無編集。

- Q1: `kcg_boot_phase_close()` を shlib_init / bootlog_save 後、シェルループの
  最初のexec前に1回結線。kcg.cのboot状態を永久に閉じ、入口で全callerへNOSYS。
  logging / VFS / cacheへのアクセスより前に返す。kcg_initも再開せず、二重閉鎖は無変更。
- Q2: 閉鎖時はscratch失効→Unicode4点再検証 (不一致ready=0)→planar BB全128KiB消去
  →mailbox868B消去。Unicode完全読込直後にもlib/utf8.cの既存4点probeを使う。
  失敗したreadのready=0を閉鎖時に復活させない。通常AS/USER lease開始前に完了。
  予約済みSURFACEとgfx_started後のpublisherを区別し、既存boot順序は維持。
- §9: MEM_AUTOPLAY_MAILBOX_BASE/SIZEをmemmap.hに追加 (0x90000/868)。
  game commit `6d5be1e` のapp/view_export.c/hとdriver.pyを読取りで照合。
  対と同じコミットでの変更規則を注記。docs/02_memory.mdの旧「定義は無い」は
  生成ブロック内のためgen_memmapの説明を直して再生成。
- Q3: font_load_testはNOSYS=PASS、0と他のrc=FAIL。stat段のSKIPを維持。
  guest台本・host P/F/S・変異5の新文言を同時更新 (予約値127の意図は維持)。
- Q4: 実kcg.c/utf8.c全文とC LZ4を使うILP32ホスト試験を追加。
  正常12シナリオとboot結線順序、必須5/5変異を期待FAIL文言/rc=1で検出。
  cache/Unicode/BB/mailboxを含む全640KiBとVFSカウンタ不変、初期化範囲外不変を照合。
  全HOST32_RUNNERS列へ追加し08_buildも13試験へ。既存結果集計は227条件/0失敗、
  11/11変異RED。詳細: [kcg_boot_tdd.md](../../../tools/tests/kcg_boot_tdd.md)。
  kselftestは閉鎖前の位置であり、起動ログ確定後の新しい自己試験位置を増やさず
  hostとゲストfont_load_testで代替。ゲスト実行はPM受入へ (未実施)。

**Q5予算 (同一 /home/hight/opt/cross、byte、build ID差を含む)**:

| 項目 | 前 | 後 | 増分 |
|---|---:|---:|---:|
| kernel.bin | 367,436 | 367,636 | +200 |
| vmkernel.lz4 | 483,339 | 483,473 | +134 |
| __bss_end | 0x18D358 | 0x18D418 | +192 |
| kernel本体 (BSS/整列込み) | 578,392 | 578,584 | +192 |
| ASSERT残り | 31,912 | 31,720 | −192 |
| e枠残り | 12,056 | 11,864 | −192 |

上限1,500Bに対して192B、残り1,308B。ELF text/data/bssは
705540/36835/606520→705716/36843/606520。KHEAPページ境界は維持。

**検証・申し送り**:
make all NP21W_DIR=/dev/nullは前後ともrc=0。初回足場のcompile失敗と
I/O stub不足signal11を修正し、正常12シナリオ/5変異はrc=0。
途中gen_memmap --checkは古いbuild値でrc=1、最終build後に--writeで同期する。
全体検査は票/ソース固定後、check_slot.sh e8b-coder経由で実行し、結果のみ終了後追記。
環境はCROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、PYTHONPATH空、
HOST32_RUNNERS=qemu、makeのstdinは/dev/null。ログはos32-tmp/e8b-*.log。
NP21/W・NHD・配備・ini・実機・native runnerは未実施。commit/pushなし。
e11へ: 公開説明/版/生成と通常ASの一括切替。e8a/T3のUnicode移行は別段。

最終結果 (全体検査終了後の追記):
`/home/hight/os32-tmp/bin/check_slot.sh e8b-coder env HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`
は **rc=0、1回、slot0、full選択**。途中の票/ソース変更なし、直接実行へのfallbackなし。
128検査の対応表漏れ0、新規正常12シナリオ/必須5/5変異、既存result_convの
227条件/0失敗・11/11変異を全体検査内でも確認。C方言は27/27変異・5/5対照。
ログ: `/home/hight/os32-tmp/e8b-check-changed.log`。
最終make allはrc=0 (`e8b-final-build.log`)。
最終gen_memmap/gen_tests_inventoryの--writeと--check、check_select --lintはrc=0。


**e8b の着地 (PM、2026-10-03)**: 独立レビュー Opus 5.5 は Approve (P1/P2 なし。レビュアーは qemu で 12 シナリオと変異 5/5 を再実行、kcg_load_font の実際の呼び手は boot_font.c と font_load_test だけ、閉じた後に BB・mailbox を読む者は無い、mailbox は game の view_export.c の sizeof==868 と driver.py の MAILBOX_SIZE=868 と一致)。PM のホスト検査は native の単体と `check_slot.sh` 経由の `HOST32_RUNNERS="native qemu"` check-changed で rc=0。P3 の扱い: P3-2 (kernel.c の古い [DEBUG] 注記) と P3-6 (コーダー表記) は PM が直した。**e11 へ**: P3-1 公開 KAPI の `kcg_init` は `kanji_fetched`/`ank_fetched` をゼロにするので、呼んだプログラム (apps/edit、blit_test2、rotate_test、gfx_demo200、bench) の後は再起動まで漢字が ROM の字形になる — e8b より前からあり、e8b は読み直しの道 (kcg_load_font) を NOSYS で塞いだだけ。e11 で閉じた後の `kcg_init` はフラグを消さない (または NOSYS) を決める。**e8a へ**: P3-3 `lib/utf8.h` は SDK に写される公開ヘッダで、カーネル専用の `utf8_validate_jis_table` が `#ifdef __KERNEL_BUILD__` で載っている — Unicode の面の整理で外へ出す。記録: P3-4 (slot の path ガードが無効なポインタを先に弾いたときの戻り値は未確認、副作用なし)、P3-5 (kernel.c の結線は試験では文字列の順序照合だけ、実行時はゲストの font_load_test で見る)、P3-7 (font_load_test の SKIP 文言の変更は不要だったが、変異 5 の当て先も合わせてあり期待は弱まっていない)。
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

注記 (f1bレビュー対応、2026-10-03): 上の「CRTがbreak/mapped_endを保持」は
接続口の説明。実際の状態の所有者はprimary arenaのadapterとし、f6のCRT `_sbrk` は
そのmorecoreへ委ねる。公開時にCRT自身の独立したbreakを残さない (§3-5 f1b記録)。

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
| f12 | 起動予算・旧helper撤去・最小初期量/世代の一括切替<br>申し送り (2026-10-02): `test_sbrk_tier.py` を丸ごと削除し、`app_band_pde_host.c` の legacy byte budget、`memory_boot_host.c` の `MEM_EXEC_SBRK_MIN` 式、kselftest `test_pool_model` の `pool:exec range`、`heap_test` を新予算/heap契約へ更新する。 |
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

**f1b 実装記録 (2026-10-03、Codex gpt-6-astra、wt/t2f1b、基点 d4d5a2b = 着手時 main)**:

範囲はSDKの単体接続だけ。実アプリ/shell/gshellのリンク、CRT、kernel、KAPI/4世代、
Rustを変更しない。`sdk/allocator/build_nano.py` は検証済みlibc.aから6 member
(mallocr/freer/callocr/reallocr/msizer/mallinfor) を抽出し、台帳のrename表で私有化する。
実nanoの命令本体・ソースを再コンパイル/patchせず、system libc.aも変更しない。
理由は既存toolchainで検証済みの実コードをそのまま対照にし、同じ再帰呼出しを保ちながら
外部のmalloc/free/calloc/reallocと4つのreentrant入口をadapterへ集約できるため。

`nano_adapter.c` は入口全体でbusyを保持し、arenaごとのfree_list/sbrk_start/mallinfoを
実memberの私有globalへload/saveする。別arenaのchunkを一つのlistへ繋がない。
nano内部のcalloc/reallocからの私有malloc/freeは同じbusy/arenaを使い、map callbackから
公開入口への再入はENOMEM、freeは状態不変、arena選択は拒否する。
f1bのarena選択・初期化はfixtureの責務。free/reallocはenter後に
pointerが選択arenaの `[sbrk_start (未設定ならinitial), brk)` に属するか検査し、
外れならEINVALを立て、freeは状態不変、reallocはNULLで旧内容を保持する。f7で自動routingと
先頭管理page/副arena生成・回収へ接続するまで、これを公開allocatorとして使わない。

`os32_nano_morecore` はu32の加算前overflow/INT_MIN/下限/上限/page丸めを検査し、
旧breakだけを返す。USER接続点は「指定末尾の全pageを原子的にEXACT mapする」callback、
失敗時はbreak/mapped_end不変。resident接続点はcallback無しの固定mapped_end。
負増分はbreakだけを下げる。f6の実CRT二分とmem_mapへの接続は未実施。
primary arenaのbreak/mapped_endはadapterが持ち、f6のCRT `_sbrk` はそのmorecoreに
委ねる。公開の段ではCRTが独自のbreakを持たない。f6への申し送りとして、CRTの
`_sbrk`/`sbrk` とlibc_a-sbrkr.oを残して所有者を二重化せず、adapter側の定義へ集約する。
初期initialはCHUNK_ALIGN (4) に切り上げてCRTから渡す契約とする (f6)。
fixtureの不整列break試験はupstream追加要求の対照として残す。
実 `sbrk_aligned` の不整列時追加要求 (:226相当) と `nano_malloc` の末尾不足分要求
(:332相当) を実行した。追加要求の後半失敗時は先の取得分が残るupstream挙動を記録し、
巻戻しや別arenaへの非連続成功へ改変しない。nanoのpatchは不要だった。

未結線入口は `sdk/allocator/check_link.py` のopt-in検査で拒否 (失敗出力を削除)。
ldが選んだobject/archive memberの全定義を調べるため、gshell同様の
`--allow-multiple-definition` で後勝ち/先勝ちが隠れても検出する。
`sbrk`/`_sbrk`/`_sbrk_r` の定義がnano_adapter以外から選ばれた場合も、
同じopt-in検査で拒否する (未定義は許可)。公開 `sdk/link_guard.py` への適用は公開切替時。Rustの `Os32Alloc::alloc/dealloc`
(`sdk/rust/os32api/src/lib.rs`) はmem_alloc/mem_free直結でnanoを呼ばない。
実libos32guiのinputsにもnano member無し。gshellのC CRTは別にnanoを使うため、
「gshell全体がnewlibをリンクしない」とは扱わない。Rustの整列adapterはf8。

台帳の `sdk_build.sources` にSDK実装hash、`upstream` にnanoの原本hash、member/全rename、
空patch列を記録。SDK入力は原本台帳と照合し、toolchainのreceipt/cache_keyとは分離。
SDK upstream照合は台帳内の2つのhashの比較のみで、f1aの「toolchainと同じ照合」
より弱い。再コンパイルは行わず、実member/archiveのhashはinputs.checkで固定する。
THIRD_PARTYに変換を追記、nano.LICENSEは原文のまま (追加vendor source無し)。
receipt P3(a)のmember名追加時の復旧案内は08_build §8-5へ追記。
receipt形式を変えないためRECEIPT_SCHEMA追加/CI cache更新は行わない。

**既存試験の事前一覧と追従**: 期待を変えた既存試験は **0件**。
`rg` で実関数を抽出/コピーする足場も検索し、sbrk_tier、memory_boot、memmap_boot
(実exec_heap)、app_band_pde、exec_r1、app_bb_overlap、db_caller、shlib_high、
guest heap_test/alloc_demo/klibc_test/dbgserialは対象ソース不変と確認。
`test_nano_inputs.py` は既存50ケースの期待を維持してSDK照合5ケースと4変異を追加
(55 GREEN / 29 runtime RED)。SDK patch/cache分離の既存負例も残した。

**試験**: [nano_adapter_tdd.md](../../../tools/tests/nano_adapter_tdd.md)。
実nano ILP32の88 CHECK GREEN、未結線/重複/独立break所有者の46リンク拒否、C21+リンク検査3 =
24 runtime RED / 0 survived / 0 ERROR。新検査はcheck_mapと生成TESTSへ登録。
変異置換当たり数は固定、期待CHECKラベルが一致した実行失敗だけRED。
compile/link失敗・timeout・別CHECKの失敗はREDに数えない。
途中のfixture compile警告と丸め後上限変異の生存は同ログへ記録し、修正後に再実行。

**サイズ (基点build→変更後build)**:

| 対象 | 前→後 (byte) | 増分 |
|---|---|---|
| kernel.bin | 364404→364404 | 0 |
| vmkernel.lz4 | 481352→481352 | 0 |
| shell.bin | 99968→99968 | 0、SHA256も不変 |
| gshell.bin | 225064→225064 | 0、SHA256も不変 |
| libos32gui.shlib | 129568→129568 | 0、SHA256も不変 |

108成果物を比較し、全size不変、kernel/vmkernel以外の106本はSHA256も不変。
kernelは毎回のkapi_sys日時埋込みでhashが変わるためSHA不変とはしない。
`__bss_end=0x18C754`、ASSERT残34988 B、f枠8192 Bの消費0。
レビュー対応前の未配布fixture archiveはtext2649/data0/bss56 B、その内adapterはtext1425/bss8 B。
レビュー対応後 (owns_pointer 追加後) の adapter は同じフラグ (`i386-elf-gcc -std=gnu11 -O2 -ffreestanding -fno-builtin`) で text 1682 / data 0 / bss 8 B (+257 B、PM 実測 2026-10-03)。公開成果物への影響は 0。
**公開の段への申し送り (f1b 再レビュー P3)**: sdk/allocator/check_link.py:45,52 は提供元を `endswith('(nano_adapter.o)')` (ファイル名) だけで見分ける — adapter を .o のままリンクすると正しい構成でも拒否され、同じ名前の別物は通る。公開切替 (link_guard.py への適用、f6 で CRT の `_sbrk` を adapter に集める段) のときに、台帳の sdk_build.sources の hash などで提供元を照合する形に改める。
**f1b の着地 (PM、2026-10-03)**: 独立レビュー (Opus 5.5) は 1 回目 Request changes (P2 3 件・P3 8 件) → P2/P3 対応の差分確認で Approve (P3 2 件 → 上の 2 行)。PM のホスト (既定 `HOST32_RUNNERS=native qemu`) で `make all`・lint・`check-changed` rc=0 (修正前の版・修正後の版とも、`/home/hight/os32-tmp/pm{,2}-f1b-{all,cc}.log`) — 実 nano の ILP32 試験は native でも通過。公開リンク・CRT・kernel・Rust は不変なのでゲスト回帰は不要と判断。
arena記録は72 B/本 (fixture側、f7の管理page実装ではない)。画像へのadapter増分0。

**独立レビュー対応 (2026-10-03、Codex gpt-6.1-sol)**:
P2-1: pointer所属検査、境界で隣接するchunkの非併合/別arena free・realloc拒否、検査迂回変異。
P2-2: primary break所有者をadapterと明記、§3-3注記とf6申し送り、独立sbrk定義のopt-in拒否/変異。
P2-3: link負例を実数集計、malloc_trim/_malloc_trim_rも未結線として拒否 (46件)。
P3-1: initialの4整列契約と不整列fixture維持。P3-2: 変異ごとの期待CHECKラベル照合。
P3-3: page丸めoverflow/resident分岐/busy外morecoreの3変異追加。
P3-4: 実libc_a-reallocf.oをfixtureにリンクしてarena free_list変化を確認。
P3-5: upstream照合の限界を記録。P3-6: baselineのinputs.checkは1回、変異は私有memberを再利用。
P3-7: HOST32 runnerの列挙をMake recipeへ合わせ、前実装者表記をCodex gpt-6-astraへ訂正。
公開リンク、6 memberの私有化、arena状態load/save、receipt/cache_key、Rust経路、e受入ゲートを維持。

**前実装者の検査の実行記録**:
環境はCROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、PYTHONPATH空、
HOST32_RUNNERS=qemu。`make all NP21W_DIR=/dev/null < /dev/null` は初回rc=2
(worktreeにIPAフォント無し)。既存mainの取得済みTTFをコピーしhash検査後、再実行rc=0。
変更後の同コマンドもrc=0。配備コピーは/dev/null指定のため警告のみで実行されない。
`test_nano_adapter.py --runner qemu --mutate`、`test_nano_inputs.py --mutate` は各rc=0。
`gen_memmap.py --write`、`gen_tests_inventory.py --write`、`check_select.py --lint` は各rc=0。
この欄を凍結後、指定の `OS32_MUT_JOBS=4 make check-changed NP21W_DIR=/dev/null < /dev/null`
を実行し、そのrcは最終報告と `/home/hight/os32-tmp/f1b-check-changed.log` に残す。
全体検査中は票/ソースを書き換えない。

**レビュー対応後の検査記録 (全体検査開始前に凍結)**:
環境は上記と同じ。`make all NP21W_DIR=/dev/null < /dev/null` はrc=0。
`python3 -B tools/tests/test_nano_adapter.py --runner qemu --mutate` はrc=0
(88 CHECK / 46 link拒否 / C21+gate3=24 runtime RED)。
`python3 -B tools/tests/test_nano_inputs.py --mutate` はrc=0 (55 GREEN / 29 runtime RED)。
`python3 -B tools/gen_memmap.py --write`、`python3 -B tools/gen_tests_inventory.py --write`、
`python3 -B tools/check_select.py --lint` は各rc=0 (対応表120検査、漏れ0)。
以下の全体検査中は票/ソースを変更せず、結果は最終報告と
`/home/hight/os32-tmp/f1b-review-check-changed.log` に残す:
`OS32_MUT_JOBS=4 HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`。

**未実施と申し送り**: nativeはPMホスト、NP21/W/NHD/配備/ini/実機は依頼により未実施。
外部apps/gameの再結線も無し (公開SDK不変)。独立レビューはPMへ引渡し。
f5/f9で保存caller由来のkernel接続、f6で実CRT、f7で副arena/全入口routing、f8で大塊とRust、
f11で実free listのtrim/rollbackを続ける。f1bはtrimや最小初期量切替の成立を主張しない。
f2以降および公開切替のe受入ゲートは維持する。commit/push無し。

**f2 実装記録 (2026-10-03、Codex gpt-6.1-sol、wt/f2、基点 main e28f7de)**:

PM の先行方針: f2〜f4 は e と並行する未結線の先行準備とし、公開切替・AS 埋込み・呼び手への接続は e 受入後の f5 以降に行う。

PM 判断 `/home/hight/os32-tmp/ref_f2_pm.md` を最優先に実装、判断からの逸脱なし。
新 [appmem.h](../../../exec/appmem.h) / [appmem.c](../../../exec/appmem.c) は
ホスト試験だけにリンクする。`build/kernel.mk`・paging・gfx・lease・KAPI・SDK生成物は不変。

- Q1/Q2/Q6: 独立の `appmem_table` は `APPMEM_EXTENT_MAX=32`、
  extent16 B/table512 BをSTATIC_ASSERT。base昇順、空slot全欄ゼロで末尾。
  `appmem_prepare` は表を変更せず、穴と併合後slot数を先に検査する。
  `appmem_plan` は36 B、失敗時はproposalも不変。成功proposal専用の
  `appmem_publish` はf3のPTE確保/ゼロ化成功後の確定に使う。
  prepare〜publish は同一表を直列化し、callback/AS切替を入れない契約。
- Q3/Q4: 配置記述子は img_end / primary_mapped_end / exec_heap_cur_end / guard_b。
  image/BSS端page、初期heap、stack/guard/shlib/leaseはextentに数えない。
  primary_mapped_endまでを除外し、flags0は下側窓の上端から、TOPDOWNはguard直下から探索。
  非NULL hintは整列/私有帯/加算overflowを検査し、両窓の空き希望を先に採用する。
  固定領域やextentへの衝突はflags0/TOPDOWNで別穴、EXACTはENOVAで全不変。
  EXACT|TOPDOWNはEXACT優先。exec_heap自体の内部伸長専用口はf9。
- Q5: map_flagsとextent.flagsを分離。kind/内部flagsが一致した隣接だけ併合し、
  EXEC_LARGEは同識別子でも併合しない (識別子の供給は接続側)。
- Q7/Q8: kernelへの増分0 B、f枠8192 Bの消費0。
  kernel.mapにappmem object/symbol無し、`__bss_end=0x18D358`、ASSERT残31912 B。
  INVAL=-1 / ENOVA=-2 / EFULL=-3は私有値で、公開エラー対応はf5。
  ASへの512 B追加はf5に延期し、§6-1の1376 B上限と全管理16 KiB検査をその段で更新する。
- Q10: `check-appmem-host` をsdk.mk末尾/検査列/check_map/生成TESTSへ登録。
  08_buildの全runner列挙を実recipeの10本 (nano_adapterとappmemを含む) に合わせた。
  既存試験の期待変更0件。事前にrgでsbrk_tier/app_band_pde/memory_boot/
  exec_r1/app_bb_overlapの足場を一覧し、対象ソースを変更していない。

**試験**: [appmem_tdd.md](../../../tools/tests/appmem_tdd.md)。
実appmem ILP32/qemuで136 CHECK GREEN、13/13 runtime RED、生存0 / compile-link ERROR0 /
signal0 / timeout0 / その他ERROR0。必須3変異に併合kind/flags/LARGE、FULL不変、
丸め/加算overflow、TOPDOWN優先違反、併合前FULL、探索下限、公開時移動の変異を追加。
全変異は写しで置換当たり数1、期待FAIL文言を照合し、原本入力hash不変を確認。
最終対象実行は中央値0.14秒/最大0.18秒 (compile+run、OS32_MUT_JOBS=4)。

**検査の実行記録 (全体検査開始前に凍結)**:
共通環境はCROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、PYTHONPATH空、
HOST32_RUNNERS=qemu、makeはstdin=/dev/nullかつNP21W_DIR=/dev/null。
`make all NP21W_DIR=/dev/null < /dev/null` rc=0 (`/home/hight/os32-tmp/f2-all.log`)。
既存imageレシピの自動コピーは/dev/null宛のため失敗警告、配備成功なし。
`python3 -B tools/tests/test_appmem.py --runner qemu --mutate` rc=0
(`/home/hight/os32-tmp/f2-appmem.log`)。
`python3 tools/gen_memmap.py --write`、`python3 tools/gen_tests_inventory.py --write`、
`python3 tools/check_select.py --lint` は各rc=0 (対応表125検査、漏れ0)。
この欄を固定後、協調枠の
`/home/hight/os32-tmp/bin/check_slot.sh f2-coder env HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`
を実行する。OS32_MUT_JOBSは枠に任せる。sandboxで枠が使えない場合だけ指定の
OS32_MUT_JOBS=4直接実行へ移る。実行中に票/ソースは変更せず、枠の可否/終了rcは
最終報告と `/home/hight/os32-tmp/f2-check-changed.log` に残す。

**f3〜f5への申し送り**: f3はprepare後のPT/data全確保/ゼロ/rollbackと成功時publishを接続し、
ENOSPCを私有理由に追加する。slot/物理をprepareで先取りしない。
f4は同じ昇順/空slot/併合規約でunmap被覆・最大2残片・FULL不変・owner検査を実装する。
f5は型をpaging.hかincludeへ移しASへ固定埋込み、保存code_end/配置記述子、
caller由来、公開エラー翻訳、KAPI/kselftestとkernelリンクをe受入後に行う。
公開mapはANON、kind/extent.flags/配置記述子をcallerから受けない。
未実施: native (PM担当)、独立レビュー (PMへ引渡し)、f3以降の接続/guest受入。
NP21/W・NHD・配備・ini・実機操作、commit/push無し。
**f2 の着地 (PM、2026-10-03)**: 独立レビュー Opus 5.5 は Approve (P1/P2 なし、native で 136 CHECK・変異 13/13 を自ら再実行)。PM のホスト検査は native の単体と `check_slot.sh` 経由の `HOST32_RUNNERS="native qemu"` check-changed で rc=0。P3 の扱い: P3-5 (tdd に票の行) は PM が直した。P3-3 (publish の入口で plan と表の照合) と P3-6 (未知 flag・非整列 hint・hint 下限の変異 3 本) は f3 で直す。**持ち越し**: P3-1 exec_heap が空の配置 (`exec_heap_cur_end == MEM_EXEC_HEAP_BASE`) で 0x88000000 をまたぐ併合が起きる — f5/f9 で「cur_end > BASE を不変条件にする」か「境界をまたぐ併合を禁じる」かを決める。P3-2 shlib 帯・lease 窓の hint は flags=0 でも INVAL (私有利用帯の外は拒否、ref_f2_pm Q4 の文言との差) — 公開での挙動を f5 で票に確定させる。P3-4 初期 heap を extent に数えないので、LIBC_INITIAL を unmap で返した後の穴が flags=0 の窓に入らず再利用されない — f4/f5 で LIBC_INITIAL を extent に持つか `primary_mapped_end` を下げるかを決める。


**f3 実装記録 (2026-10-03、Codex gpt-6.1-sol、wt/f3、基点 main 37d1aef — f2取込み済み)**:

PM判断 `/home/hight/os32-tmp/ref_f3.md` §8を最優先、Q1〜Q10からの逸脱なし。
f2〜f4はeと並行する未結線の先行準備、公開切替・AS埋込み・caller接続はf5以降。

- 新 [paging_app.h](../../../kernel/paging_app.h) / [paging_app.c](../../../kernel/paging_app.c)
  にstage/commit/abortを配置。appmem側の1回完結入口は
  [appmem_map.c](../../../exec/appmem_map.c) の `appmem_map`、宣言はappmem.h。
  prepare→stage→IRQ保存→commit→appmem_publish→active CR3→IF復元を直列化。
  callerは同一表・非再入・callback/AS切替なしを保証する。確保とゼロ化はIF=1。
- pending PT控えはcallerのframeに64本/256 B、tx全体268 B、plan36 B。
  AS/AppSlotは変更なし。PTを各1枚確保直後に全1024 entryゼロ化、
  dataを各1枚確保直後にゼロ化して `frame|RW|USER` のNP PTEへ記録。
  既存PTの範囲も全0を先に検査し、非0ならEINVALで全不変。
- stage入口はmaster/対象AS CR3、IRQ/例外深さ0、IF=1、PDと既存app PTのowner/PDE整合を検査。
  PT/data不足はいずれも私有 `APPMEM_ENOSPC=-4`、診断カウンタは理由別。
  abortは既存/pending PTのNP PFNをowner付き返却→entryゼロ→pending PT返却。
  owner拒否は `paging_app_bad_free_count` に記録し、正常対照では0。
  公開順序はPTE PRESENT→PDE (既存にはUSER伝播)→app_pt_phys→extent→active CR3。
  invlpg/追加rollback page/巨大data PFN配列は無し。
- f2持越しP3-3はpublish入口のcount/range_freeとproposal境界検査、
  古いcount/同数の占有済み範囲の拒否対照で対応。
  P3-6の未知flag/非整列hint/hint下限の3変異を追加。f2は139 CHECK/16変異。
  P3-1/P3-2/P3-4の公開配置・初期heap返却の決定はf4/f5/f9への持越しを維持。
- 新 `check-appmem-map-host` をsdk.mk末尾/検査列/check_map/生成TESTSへ登録、
  08_buildの全runner列を11本へ更新。paging.c/paging.h/kernel.mk/gfx/lease/SDKは変更なし。
  カーネルリンク増分0 B、f枠8192 Bの消費0。
  `__bss_end=0x18D358`、ASSERT残31912 B、kernel.mapにf3のobject/symbol無し。

**試験**: [appmem_map_tdd.md](../../../tools/tests/appmem_map_tdd.md)。
実appmem/paging_app/paging/pgalloc/physmemをILP32/qemuで連結。
境界跨ぎ新PT2枚と既存/新PT混在、master/activeの4成功・全18確保失敗を確認。
最終2,254,940 CHECK GREEN (各byte/entryの照合を含む)、11/11 runtime RED、
生存/compile-link ERROR/signal/timeout/その他ERROR各0。中央値0.47秒/最大0.60秒。
f2は139 CHECK GREEN/16 RED。変異は写しのTUだけ再コンパイル、共通object再利用、
期待FAILの完全な行を照合し、開始/終了入力hash不変。
PD/PTE/app_pt_phys/extent/owner pages/池の完全rollback、master/他ASの全PD/PT不変、
成功/失敗後の全app PT内の非0 NP PTE=0、bad_free=0を確認。

**frame実測**: cross GCC13.2.0、`-std=gnu11 -march=i386 -O2 -fstack-usage
-ffreestanding -fno-builtin -Wall -Wextra -Werror -Werror=implicit-function-declaration
-Werror=implicit-int -Werror=vla -Iinclude -Ikernel -Iexec -Ilib -Iarch/x86 -Iplatform/pc98`。
`i386-elf-gcc ... -c exec/appmem_map.c` / `kernel/paging_app.c` は各rc=0。
appmem_map=384 B、paging_app_context=64 B、stage=64 B、abort=48 B (dynamic,bounded)、
commit=28 B (static)。控え `/home/hight/os32-tmp/f3-stack/*.su`。
**16 KiB kernel stack全体のhigh-waterは未測定**、hで測る。

**実行記録 (全体検査開始前に凍結)**:
共通環境はCROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、PYTHONPATH空。
makeはNP21W_DIR=/dev/null・stdin=/dev/null、HOST32_RUNNERS=qemu。
`make all NP21W_DIR=/dev/null < /dev/null` rc=0 (`f3-all.log` / `.rc`)。
既存imageレシピは/dev/null宛cp失敗警告、配備成功なし。
`python3 -B tools/tests/test_appmem_map.py --runner qemu --mutate` rc=0
(`f3-appmem-map.log` / `.rc`) と `test_appmem.py --runner qemu --mutate` rc=0
(`f3-appmem.log` / `.rc`) — ログは全て `/home/hight/os32-tmp/`。
`python3 -B tools/gen_memmap.py --write`、`gen_tests_inventory.py --write`、
`check_select.py --lint` は各rc=0 (126検査、漏れ0)。`check_p2v.py` rc=0、違反0。
初回足場/変異のエラーはtddに記録し修正済み、初回lintは依存7件漏れでrc=1→補完後rc=0。
この欄とソースを固定後、
`/home/hight/os32-tmp/bin/check_slot.sh f3-coder env HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`
を1回実行する。OS32_MUT_JOBSは枠の設定に任せる。
検査中は票/ソースを変更せず、終了rcは最終報告と `f3-check-changed.log` / `.rc` に残す。
**全体ゲート結果**: slot1取得後、上記コマンドを**1回実行してrc=0**、再実行なし。
Make登録差分によりfull選択。f3は2,254,940 CHECK/11 RED (中央値0.82秒、最大1.01秒)、
f2は139 CHECK/16 RED (中央値0.87秒、最大1.17秒)、C方言27 RED/対照5 GREEN。
既存Windows opt-inは計5件skip。検査開始前の16変更ファイルのhashが全て不変と
終了後に照合し、**終了後にこの結果だけを追記**した。
未実施: native/独立レビュー (PM)、f4 unmap、f5 caller/KAPI/heap端/AS埋込み、guest受入。
NP21/W・NHD・配備・ini・実機・git commit/push操作なし。
**f3 の着地 (PM、2026-10-03)**: 独立レビュー Opus 5.5 は Approve (P1/P2 なし。レビュアーは native/qemu で 2,254,940 CHECK と変異 11/11・f2 の 16/16 を再実行し、追加の変異 12 本と既存 PT だけ・新 PT 1 枚・隣接 PRESENT の形の探りでも実装の欠陥なし)。PM のホスト検査は native の単体 2 本と `check_slot.sh` 経由の `HOST32_RUNNERS="native qemu"` check-changed で rc=0。P3 は f4 で直す (ref_f4.md §9): P3-1/P3-2 publish の照合が件数の同じ古い plan を通す → `appmem_plan_valid()` を commit の前に、P3-4 試験の穴 (既存の非 0 PTE の検査を先頭だけにする変異・abort で既存 PT を全ゼロにする変異が生き残る、need_pt=0 と新 PT 1 枚の形)、P3-5 commit の後の abort を無害に、P3-7 登録の体裁。**PM 判断 (P3-3)**: pending の新しい PT の中の PTE は PDE を入れるまで見えないので、PRESENT は irq_save の前に立ててよい (ref_f3 Q7 の補足、IRQ 禁止の区間を O(PDE 数 + 既存 PT のページ数) に)。P3-6 (CHECK 数が確保数の 2 乗で増える、大きな形を足すときは照合を間引く) は記録。

**f4 実装記録 (2026-10-03、Codex gpt-6.1-sol、wt/f4、基点 main f281b7a — f2/f3取込み済み)**:

PM判断 `/home/hight/os32-tmp/ref_f4.md` §8 Q1〜Q9 / §9を最優先、逸脱なし。
f2〜f4はeと並行する未結線の先行準備。kernel.mk/caller/KAPI/AS埋込みはf5以降。

- [appmem.c](../../../exec/appmem.c) にunmap prepare/plan_valid/publish、
  [appmem_unmap.c](../../../exec/appmem_unmap.c) に公開方針の1回完結入口を追加。
  公開対象はANON/LIBC_INITIALのみ。隣接するallowed-kindを跨げるが穴/EXEC_*は全拒否。
  最大2残片は元kind/flagsを保持し、32本で中抜きFULLなら表/PTE/PDE/owner/池/output全不変。
  31本での成功対照も確認。私有EINVAL/EFULL、公開値への対応はf5。
- **Q1訂正 (f2 Q3 / P3-4)**: 初期heapだけ固定領域のextent除外から外す。
  LIBC_INITIALを `[page_align(img_end), primary_mapped_end)` のextentに持つ。
  BSS共有端pageはimage。f5で登録してlayout.primary_mapped_endにpage_align(img_end)を渡す。
  f4はhost内で登録し、返した穴へのflags0再mapを確認。ANONとは併合しない。
  f2 P3-1 (空exec_heap境界の併合) とP3-2 (公開hintの範囲) はf5/f9へ引継ぐ。
- [paging_app.c](../../../kernel/paging_app.c) に全PTEの形式/owner/live SURFACE検査、
  空PT判定/withdraw/freeを追加。A/D許可、PS/PCD/PWT/権限不足/NP/0/別ownerは事前拒否・診断。
  空PT判定は範囲外entry全0、触れたPTだけ、k=0保持。8 Bの空PT membershipだけ控え、
  data PFNはNP PTE、返すPTはapp_pt_phys/PDEに保持 (pending配列追加なし)。
  IRQ保存中は残るPTの対象PTEをNP、返すPTはPDE=0、active CR3を1回同期 (master=0回)。
  空PTの中身は同期前不変、IF=1復元後に全対象NP→data owner free→entry0→PT free→
  app_pt_phys0→extent残片確定。完了後PDE/app_pt_phys整合とcontext成功を確認。
  不変条件違反のfree失敗はbad_free++/負値/失敗NP frame保持、成功へ偽装しない。
- **§9 P3-1/P3-2**: map_mergeをprepare/plan_validで共有し、first/remove_count/mergedを再計算。
  同数古いproposalの挿入位置/隣接消失/併合向きの反例を拒否。
  mapはstage前とIRQ保存前にvalid、後者失敗はabort。unmapも件数/位置/残片を再計算。
- **P3-3**: 全確保成功後にpending新PTのPRESENTをIF=1で立て、IRQ区間は
  O(PDE数+既存PTの対象ページ数)。unmapも空PTをPDE撤去し、残るPTだけNP化。
  hookでIRQ内PTE操作数の上限と、空PTの同期前不変を検査。
- **P3-4**: 2ページ目/2枚目PTの非0、範囲外PRESENTのabort後保持、既存PTだけ/
  新PT1枚の形を追加。検査を先頭だけにする変異・abortで既存PT全消去の変異もruntime RED。
- **P3-5**: commitの最後にtx.end=tx.base、続くabortで公開済PTE/owner不変。
  省略変異をruntime REDにした。
- **P3-6**: data byte/他AS PTの全内容照合をpage単位CHECKに集約。
  大きい形のO(確保数²×4096)増幅は避け、runner timeout10秒を維持。
- **P3-7**: sdk.mkのrecipe字下げ/echo/MUT/末尾を周辺と統一。
  TARGET_SRCSをliteral一覧へ直し、生成TESTSの対象ソース欄を埋めた。
  既存test_appmem_mapの拡張なので全runner列は11本のまま。
- 禁止対象 (paging.c/paging.h/kernel.mk/gfx/lease/userland/lib/SDK/KAPI生成物) は変更なし。
  kernelリンク増分0 B・f枠8192 B消費0、AS/AppSlot増分0。
  kernel.mapにappmem/paging_app object無し、__bss_end=0x18D358、ASSERT残31912 B。

**試験**: [appmem_map_tdd.md](../../../tools/tests/appmem_map_tdd.md) / [appmem_tdd.md](../../../tools/tests/appmem_tdd.md)。
実appmem/paging_app/paging/pgalloc/physmemをILP32/qemuで連結。
map6成功/21確保失敗、unmap16基本成功と31本split成功、16穴再map、目的別負例、
検査後free失敗注入 (負値/NP frame/診断) を確認。
統合111,088 CHECK GREEN/32 runtime RED、表194 CHECK GREEN/19 runtime RED。
必須unmap14種類を全て含む。生存/compile-link ERROR/signal/timeout/その他ERROR各0。
対象実行の中央値/最大は統合0.66/0.96秒、表0.56/1.12秒。
原本はhash不変、変異TUだけ差替え、期待FAIL完全行を照合。
拡張途中の置換重複/ラベル相違はERRORで記録して修正、REDへ算入しない。

**frame実測**: cross GCC13.2.0、`-std=gnu11 -march=i386 -O2 -fstack-usage
-ffreestanding -fno-builtin -Wall -Wextra -Werror -Werror=implicit-function-declaration
-Werror=implicit-int -Werror=vla -Iinclude -Ikernel -Iexec -Ilib -Iarch/x86 -Iplatform/pc98`。
`i386-elf-gcc ... -c` でappmem.c/map.c/unmap.c/paging_app.c、各rc=0。
appmem_map384 B、appmem_unmap160 B、表unmap prepare/valid/publish=52/96/40 B、
paging unmap prepare/withdraw/free=80/64/64 B。map context/stage/abort/commit=64/64/48/52 B、
map plan_valid72 B。控え `/home/hight/os32-tmp/f4-stack/*.su`。
**16 KiB kernel stack全体のhigh-waterは未測定**、hで測る。

**実行記録 (全体検査開始前に凍結)**:
環境CROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、PYTHONPATH空。
makeはNP21W_DIR=/dev/null・stdin=/dev/null。
`make all NP21W_DIR=/dev/null < /dev/null` rc=0 (`f4-all.log` / `.rc`)。
既存imageの/dev/null宛コピー失敗警告はあり、NP21/Wへの配備成功なし。
`python3 -B tools/tests/test_appmem_map.py --runner qemu --mutate` rc=0 (`f4-appmem-map.log` / `.rc`)、
`python3 -B tools/tests/test_appmem.py --runner qemu --mutate` rc=0 (`f4-appmem.log`)。
`gen_memmap.py --write` rc=0 (`f4-gen-memmap.log` / `.rc`; 初回はbuild中でkernel.map未生成のためrc=1)、
`gen_tests_inventory.py --write` rc=0 (`f4-gen-tests.log` / `.rc`)、
`check_select.py --lint` rc=0 (126検査/漏れ0、`f4-lint.log` / `.rc`)。
`check_p2v.py` rc=0 (違反0/例外63、`f4-p2v.log` / `.rc`)。
全ログは `/home/hight/os32-tmp/`。
票/ソースを固定して次を1回実行し、終了rcは最終報告と `f4-check-changed.log` / `.rc` に残す:
`/home/hight/os32-tmp/bin/check_slot.sh f4-coder env HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null`。
全体検査中は票/ソースを書き換えず、OS32_MUT_JOBSは枠に任せる。
未実施: native/独立レビューはPMへ、kernel結線・初期heap実登録・caller/KAPI/AS埋込みはf5、
EXEC_*内部返却口はf9/f10、実TLBを含むguest受入は結線後。
NP21/W・NHD・配備・ini・実機・git commit/push操作なし。PM判断から外れた点なし。
**全体ゲート結果**: slot0取得後、指定のf4-coderコマンドを**1回実行しrc=0**。
Make登録差分を含みfull選択。統合111,088 CHECK/32 RED (中央値1.17秒、最大1.70秒)、
表194 CHECK/19 RED (中央値0.80秒、最大1.15秒)、C方言27 RED/対照5 GREEN。
既存検査は計5件skip。検査開始前の16ファイルhashが全て不変と終了後に照合し、
**終了後にこの結果だけを追記**した。ログ `f4-check-changed.log` / `.rc` は上記tmp配下。
**f4 の着地 (PM、2026-10-03)**: 独立レビュー Opus 5.5 は Approve (P1/P2 なし。レビュアーは native/qemu で 111,088 CHECK・変異 32/32、表の 194 CHECK・19/19 を再実行し、unmap の表操作をページ単位のモデルと約 600 万件の fuzz で突き合わせて不一致 0、f3 の反例 2 件が新しい plan 照合で拒否されることも確認)。PM のホスト検査は native の単体 2 本と `check_slot.sh` 経由の `HOST32_RUNNERS="native qemu"` check-changed で rc=0。P3 の扱い: P3-5 (コーダー表記) は PM が直した (f2〜f4 の記録を Codex gpt-6.1-sol に)。**f5 で直す**: P3-1 map の plan 照合の `remove_count` 比較を外す変異が生き残る (同じ件数・first・merged で remove だけ違う古い plan の負例を足す)、P3-2 空 PT の `app_releasable` 検査を外す変異が生き残る (PT の frame に SURFACE を置く負例か記録)、P3-4 生きた SURFACE と closing+lease の拒否でカウンタの増分を確かめていない・hook の「unmap PDE before PT free」を単独で落とす変異が無い。**記録 (f5 で決める)**: P3-3 free の段の失敗 (不変条件の違反、範囲内の 2 つの PTE が同じ PFN を指す場合を含む) で PDE=0 なのに app_pt_phys≠0 が残り、その AS は以後 `paging_app_context` が偽になり NP の frame も destroy で返らない — kstop にするか AS を毒として扱うか。

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

**h3 台本 HTTP 応答・後始末修正 (2026-10-02、Codex gpt-6.1-sol、wt/h3fix、基点 e1c213b)**:
実装対象は `tools/h3_park_resume.py` と `tools/tests/test_h3_park_resume.py`。
PM の KAPI-loop 捕捉が個別 breakpoint 削除の `{"ok":1,"removed":1}` を
`is True` で拒否した不具合を修正。e の lease/gfx/SHM 権限・公開 KAPI/JSON/版は変更なし。
以下は `/home/hight/np21w-src/src/win9x/aidebug/` の
`aidebug_api.cpp` と `aidebug_app.cpp` を読み取りだけで照合した成功応答。

| 台本が使う HTTP endpoint | 実フォークの ok 型と応答欄 |
|---|---|
| GET `/api/instance` | bool true。`trap_pause` / `user_pause` も bool。他に instance_id、pid、exe、ini、media 等 |
| POST `/api/pause` / `/api/resume` | bool true のみ。resume は trap pause があれば trap、なければ user pause を解除 |
| GET `/api/mem` | bool true。`addr` は hex文字列、`len` は整数、`space` / `hex` は文字列 |
| POST `/api/mem` | bool true。`addr` は hex文字列、`written` は整数 |
| GET `/api/break` | bool true。`breakpoints` 配列、要素は slot/eip/use_cs/cs/hits (eip/cs は hex文字列、他は整数) |
| POST `/api/break/add` | bool true。`slot` は整数、`eip` は hex文字列 |
| POST `/api/break/del?addr=...` | **整数** `ok=1, removed=1`。不在は両欄0。全削除 (`addr=*` / `all=1`、台本では不使用) だけ bool true |
| POST `/api/step` | bool true。`trap_pause` は整数、`cs` / `eip` は hex文字列 |
| GET `/api/regs` | bool true。eax/eip/esp 等は hex文字列、その他 segment/control register 欄 |
| POST `/api/key` | bool true のみ |
| POST `/api/mouse` | bool true。dx/dy/btn/ax/ay/pending_x/pending_y は整数、left/right/abs_override は bool、hw_btn は hex文字列 |

個別 del は **int 型の ok=1 と removed=1 の両方**を要求し、他 endpoint は bool true を要求。
欠落・0・文字列・float・bool の個別削除 status を成功扱いしない。
GET 捕捉/mem と mouse にも成功判定を追加。偽 client は個別削除の整数 status と
捕捉用 endpoint の実欄を返す。成功した削除は所有リストから外し、再設置時に戻す。
後始末は削除失敗を集約して残りを試み、所有する breakpoint/step trap を再開する。
user pause/無関係の trap は保持。元の例外と後始末の失敗を両方報告し、capture_active を必ず解除。

- Python **36 ケース**、ILP32 **42 state checks**。追加4ケースは整数 status の成功/拒否、
  後始末の削除失敗継続、step 失敗後の PC 変化、無関係の trap の保持。
  **57/57 変異 runtime RED (追加4本)、compile/import失敗0、置換数は各固定**。
  元の `is True` 判定へ戻す変異も実行時 RED。ILP32 は host32.py/qemu 経由。
- `PYTHONPATH=`、`TMPDIR=/home/hight/os32-tmp`、`HOST32_RUNNERS=qemu`、
  `CROSS_DIR=/home/hight/opt/cross`。`make` は全て `< /dev/null`。
  `NP21W_DIR=/home/hight/os32-tmp/h3fix-unused-np21w-destination` (存在しない専用先) を指定。
  初回 `make all` はフォント未取得で **rc=2** (`h3fix-all.log`)。
  既存main worktreeの正規TTFをコピーして `fetch_fonts.py --check` rc=0、
  再実行 `make all` **rc=0** (`h3fix-all-ready.log`)。FD copy失敗警告は専用先による。
- `python3 -u -B tools/tests/test_h3_park_resume.py --mutate` **rc=0**
  (`h3fix-tests-final.log`)。その開始時はELF/map未生成のlayout 1件skip。
  all後の `python3 -B tools/tests/test_h3_park_resume.py` **rc=0、36件/skip0**
  (`h3fix-tests-built.log`) でlayoutも確認。
- 既存 `check-h3-park-resume-host` 登録に追加ケースを含め、`tools/check_map.yaml` に内容を明記。
  `python3 tools/gen_tests_inventory.py --write` rc=0 (生成 `docs/TESTS.md` は差分なし)。
  `python3 tools/gen_memmap.py --write` と `python3 tools/check_select.py --lint` は各 **rc=0**
  (119本、対応表漏れ0件)、`git diff --check` rc=0。
- この記録を検査前に完了し、票/ソースを固定して最終
  `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
  HOST32_RUNNERS=qemu make check-changed < /dev/null` を実行する。
  結果は `/home/hight/os32-tmp/h3fix-check-changed.log` と `.rc` に保存し、完了報告で確定する。
- guest/native・最終一式でのh受入は未実施。PMが今回の KAPI-loop 捕捉を再実行し、
  native と独立レビューも担当する。台本修正は別途記録済みのKAPI-loop/STOPカーネル不具合を直すものではない。
  NP21/W操作・NHD・配備・ini・実機、commit/push は未操作。

**h3 台本 P3 対応 (2026-10-02、Codex gpt-6.1-sol、独立レビュー Opus 5.5 Approve / P3 4件)**:
対象は上記台本・偽 client 試験・本 h3 欄。試験登録の説明だけ `tools/check_map.yaml` を更新。
e の lease/gfx/SHM 権限・公開 KAPI/JSON/版は変更なし。前回の未コミット差分を維持。

- P3-1: 期限切れ/advance完了後、削除との間に来た監視点トラップは、
  `failure is None` なら **incomplete capture** 例外とする。採取処理を後始末に複製せず、
  未採取の二度目を成功扱いしない単純な方式を選択。安全に解除できる所有trapは再開する。
  wm_kill/syscall_abort の二度目を、期限切れとadvance完了の両方で偽clientに注入。
- P3-2: 全削除を試みた後、GET `/api/break` の実一覧と監視番地を突き合わせる。
  残留番地を列挙し、再開せず **PM must delete them manually before resume** と案内。
  2番地の削除失敗でも5番地全部を試み、再開しないことを試験。
- P3-3: 後始末で pause が残る/エラーがあるとき、読んだ `trap_pause` / `user_pause` /
  `eip` を例外へ加える。trap + user pause の併存を保持し、元の例外と状態を両方報告。
- P3-4: 上記実装記録のモデル名を **Codex gpt-6.1-sol** に訂正。
- RED→GREEN: 修正前の追加試験は **39件中6失敗** (遅延trap 4条件 + 残留 + pause)。
  修正後は **Python 39件 / skip0、ILP32 42 state checks PASS**。
  **61/61変異 runtime RED** (追加4本)、compile/import失敗0、置換の当たり数は各固定。
  ILP32は `tools/tests/host32.py` / qemu。ログ `h3p3-red.log` / `h3p3-tests.log`。
- 共通環境は `PYTHONPATH=`、`TMPDIR=/home/hight/os32-tmp`、`HOST32_RUNNERS=qemu`、
  `CROSS_DIR=/home/hight/opt/cross`。makeは全て `< /dev/null`。
  `NP21W_DIR=/home/hight/os32-tmp/h3fix-unused-np21w-destination` は存在しない専用先。
  `make all` **rc=0** (`h3p3-all.log` / `.rc`、FD copy失敗警告は専用先による)。
  `python3 -u -B tools/tests/test_h3_park_resume.py --mutate` **rc=0** (`h3p3-tests.log` / `.rc`)。
- 既存 `check-h3-park-resume-host` に追加試験を登録済み。
  `python3 tools/gen_tests_inventory.py --write` **rc=0** (生成 `docs/TESTS.md` 差分なし)。
  `python3 tools/gen_memmap.py --write` **rc=0** (生成地図差分なし)、
  `python3 tools/check_select.py --lint` **rc=0** (119本、対応表漏れ0件)、`git diff --check` **rc=0**。
- 本欄の記録をここで完了し、票とソースを固定して最後に
  `CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp
  HOST32_RUNNERS=qemu make check-changed < /dev/null` を実行する。
  結果は `/home/hight/os32-tmp/h3p3-check-changed.log` / `.rc` と完了報告で確定する。
- native/guest・最終一式でのh受入は未実施。PMはnativeと新しい最終一式での捕捉を再実行。
  残留breakpointの案内時は手で削除し、pause状態を確認してから再開。
  NP21/W・NHD・配備・ini・実機、commit/pushは未操作。

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
3. PM は相手側の窓へフォーカスを移し、両窓の位置を動かさず target の OP_WAIT を待つ (PARKED/待ち由来印1、または g_cur=target・RUNNING/in_op_wait=1)。
   `arm --mode pf --capture /home/hight/os32-tmp/h3-capture.json
   --out /home/hight/os32-tmp/h3-observe.json` (gp/de/ud/USER-loop/KAPI-loop も同様)、
   同じ `--layout` / `--case` を渡す。loop は `--trace /home/hight/os32-tmp/h3-front.json`
   も渡し、PM/e9 が FIRING 後の新しい前景証拠をそのファイルへ渡す。
   前景の実観測ごとに `case_id`、`identity`、`map_sha256`、`phase=6`、
   `foreground_window`、`foreground_app`、`observed_at=time.time()` (ホストUNIX秒) を書く。
   phase=6 は resume_verified だけから推定せず、SHM phase欄をpauseなしの単独読取りで
   確認し、撮影した対象の前景と合わせる (配置は上のbyte offset表)。
   **1.5〜3ホスト秒ごと**に新しい実観測で更新し、**stop_sent または捕捉終了まで**続ける。
   固定90秒でwriterを止めない。撮影/読取り時間も含めた更新間隔を3秒以内にする。
   同じディレクトリの一時JSONを `replace` して公開し、書込み途中のJSONを読ませない。
   STOP時の鮮度は **0〜5ホスト秒**、arm以後の観測だけが有効。古い画像の時刻だけ更新しない。
   PM/e9のwriterは撮影とファイル更新を行い、pause/breakpoint/キー操作は台本に任せる。
   台本は一時 pause 中に対象と相手の PA/所有者/世代、上記 OP_WAIT 状態を照合して mode / arm を書く。
   **breakpoint を click より先に設置**し、target のタイトルをクリックして起こす
   (480行なら `--height 480`)。SHM 書込み単独には WM を起こす効果がない。
   同じプロセスの採取ループで resume 印・switch値・consumed=1 を case に記録する。
   PARKEDからはswitch増分必須、arm時に実行中OP_WAITだった場合だけ同値も認める。
   実行中OP_WAITでarmしたcaseはcase内でpark→resumeを通らないため、
   **最終matrixにはPARKEDでarmするcaseを少なくとも1本含める**。
   fault の5秒猶予内に別コマンドを起動する必要はない。失敗は中止、arm 再送なし。
4. loop は同じプロセスが捕捉・前景照合・STOP送信・前後のobserveを順に実行する。
   再開済みloop用には `loop-watch --capture RAW.json --out OBS.json --trace FRONT.json`。
   FIRING観測後にゲスト210 tickを待ち (USER runawayの200 tick猶予を越える)、case/identity/map/phase と5秒以内の前景app/windowを
   照合する。210 tickの間に前景証拠がホスト5秒より古くなった場合は中止せず、
   STOP前の3,000 tick上限内でファイルを読み直し、新しい証拠を待つ。
   単独`stop`も210ゲストtickと同じ鮮度再試行を使う。STOP後はtick上限で打ち切らず、
   ホスト2秒/10秒の観測を完了する。送信は `/api/key` に `seq=CTRL%2BSTOP&hold=300` をPOSTする。STOP再送は拒否。
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

**h3 台本の修正の着地 (PM、2026-10-02)**: 独立レビュー (Opus 5.5) は 1 回目 Approve (P3 4 件) → P3 対応の差分確認で Approve。PM のホスト (既定 `HOST32_RUNNERS=native qemu`) で `make all`・`check_select --lint`・`check-changed` rc=0 (`/home/hight/os32-tmp/pm-h3fix-{all,cc}.log`)。

**h3準備の独立レビュー (Opus 5.5) — 3往復で Approve (2026-10-02)**: 1回目 P1 2件 (深さ0の読み方、KAPI-loopの期待経路)、
2回目 P1 2件 (wm_killの採取をcdeclのスタック引数に、捕捉とSTOP/observeの直列化) と PM の決定 (KAPI-loop は経路 A/B のどちらか1本)、
3回目は着地を止める指摘なし。残る P3 は実ゲストの初回採取で確かめる (どれも失敗すれば台本が中止する側に倒れ、偽PASSは出ない):
(1) freeze が `/api/instance` を読んだ直後・`/api/pause` の前にトラップが入るとユーザー pause が残り `'user pause during capture'` で中止する —
起きたら PM が手で再開し新しい case でやり直す (直すなら `/api/pause` の後に instance を読み直し、トラップなら CaptureTrap 扱い)。
(2) `exec_kill` の `[ESP+4]` を物理番地で読む前提 — 初回採取で argument が 2〜5 に入ることを確かめ、範囲外なら CR3 を見て線形番地で読む。
(3) KAPI-loop 経路 A で製品の修正の形によっては exec_kill の入口に2回届き得る — e/g の修正の形が決まった時点で「回収を伴う到達1回」に数え直すか決める。
(4) 前景の照合から STOP 送信までの隙間は前回合意の見送りのまま。
**h3準備の PM 検査 (2026-10-02)**: PM のホスト (PYTHONPATH なし、既定 `HOST32_RUNNERS=native qemu`) で `make all`・`check_select --lint`・`check-changed` すべて rc=0 (`/home/hight/os32-tmp/pmf-h3-{all,cc}.log`)。ゲストでの採取・受入は最終一式で行う (準備の段)。

**h2・h3 準備の着地と PM のゲスト観測 (2026-10-02、main `4693a62`、NP21/W 17MB・今の ini)**: h2・h3 を main へ取り込み (e2・f1a と `build/sdk.mk`・`build/programs.mk`・`tools/check_map.yaml` が競合 → 全部を残して解消、TESTS.md は生成器で再生成)、コミット済みの木で `make all`・`make check` rc=0。配備後の回帰は従来どおり (kselftest 270/0、db・klibc・alloc・d0a、faulttest 4 種、STOP、V86)。
- h2: `h2_stack run h2-arg` は stack=262144・pages=40・span 163,968、`h2_stack512 run h2-arg` は stack=524288・pages=104・span 426,240 (256KiB 超) で patterns/argv OK・`$?`=0。`guard` モードは 512 版 `#PF addr=0x8FF7F000`、既定版 `#PF addr=0x8FFBF000` で kill (= 上端 − stack − 1 page)。旧形式 fixture のゲスト試験は最終一式 (隔離媒体)。
- h3: GUI で h3a/h3b を起動し init (h3a app 2・owner 8・世代 26、h3b app 3・owner 9・世代 27)。h3a に `arm --mode KAPI-loop --capture` を流すと、捕捉は `exec_resume` の pending 点で止まったが、台本が **`/api/break/del` の返り値 `"ok":1` (数値) を `ok is True` で失敗と判定**して中止した (偽 client の試験がこの形を再現していなかった — 台本の不具合、直す)。残った breakpoint 3 つを PM が削除し trap を再開。
- **KAPI-loop の CTRL+STOP が畳まれないことを観測 (カーネル層の不具合、確定)**: 再開後の h3a は app 2 が `g_cur`、slot RUNNING・`in_op_wait=0`・`last_kernel_tick` が現在 tick と一致 (KAPI を回し続けている)、画面で h3a が前景。`/api/key` POST `seq=CTRL%2BSTOP&hold=300` の 2 秒後・10 秒後・約 70 秒後の `observe` で、slot は RUNNING のまま、`abort_req`=0、`appslot_reclaim_count` 18 のまま、`fault_kill_count`・`ring3_abort_count` も不変。画面の時計は 21:41 のまま止まり、WM が一度も回らない (GUI 全体が固まり STOP でも戻れない)。NP21/W の再起動で復旧。観測は `observe` (合否判定なし) と手で送った STOP によるもので、h3 の受入ではない。上の「e/g への申し送り」の疑い (`appslot_abort_admit` が int80 ごとの `last_kernel_tick` 更新で暴走と判定しない、X4 の pump は abort_seen を立てるだけ) と一致する。POLICY_DEV §1 により新しい段より先に直す。ログ: `/home/hight/os32-tmp/h3-obs-{before,2s,10s,70s}.json`。
- **一度の CTRL+STOP が二回 kill になる (二重 kill) をゲストで再現 (2026-10-02、main `8bea831`、17MB)**: h3a/h3b を GUI で起動・init (app 2 / app 3)、h3a に直した台本で `arm --mode USER-loop --capture` を流した。台本は「捕捉の 30 秒の間に STOP が送られなかった」で止まった — fixture の 5 秒 (500 tick) の猶予の待ちが台本の頻繁な一時停止でゲスト時間として遅れ、FIRING がホストの 30 秒の窓を超えたため (台本の時間の取り方の問題、h の最終一式までに直す)。breakpoint と trap は直した後始末が正しく片付けた。続けて SHM で phase=6 (FIRING、USER-loop) と h3a が前景であることを確かめ、`/api/key` POST `seq=CTRL%2BSTOP&hold=300` を **1 回**送った。5 秒後: h3a の slot は FREE、`fault_kill_count` 0→1・`ring3_abort_count` 0→1 (カーネルの R1 経路で畳んだ) に加え、**`appslot_reclaim_count` 0→2、`appslot_last_reclaim_id`=3 (h3b)**、画面から両方の窓が消えた。WM に残った raw STOP が次の前景 (無関係な h3b) を kill した。Opus の設計レビューの P1-2 (drivers/kbd.c が raw を先に積む → WM の top_level_abort) の見立てと一致。ユーザー決定により STOP の修正 (wt/kstop、案 A) で同時に閉じる。ログ: `/home/hight/os32-tmp/dk-{a,b,after-a}.json`、画面 `dk_before.png` / `dk_after.png` (scratchpad)。

故障画像・旧shell試験は作業用の媒体と明示した台本をPMが扱う。本設計作業から環境へ触れない。Ra266物理操作/配備の承認手続は既存規則の担当へ渡す。

**h3fix2 — 捕捉の時間窓・arm の OP_WAIT 判定 (2026-10-03、Codex gpt-6.1-sol、コーダー)**:
基点 `37d1aef`、ブランチ `wt/h3fix2`。変更は台本・h3ホスト試験・本記録だけ。
§5-4末尾の既知2件を修正し、T2h最終一式のゲスト受入はPMへ残す。

**h3fix2 のゲストでの実地確認 (PM、2026-10-03、kernel `328c379`、17MB)**: layout → GUI で h3a/h3b を起動 → init (fixture 1/2) は成功。h3a に `arm --mode USER-loop --capture ... --trace FRONT.json --height 480` を流すと、arm は PARKED 経由で通り resume の捕捉 (resume の印・switch 値・consumed=1) も成功したが、**rc=1・ホスト 384 秒・`STOP not sent during capture`** で終わった。終了後に SHM を直接読むと h3a は phase=6 (FIRING) でユーザー空間のループ中 — FIRING に入っていたのに台本が STOP を送らずに上限で終わった。捕捉の間ゲストはホスト 365 秒で数千 tick しか進まず (実時間の 1 割前後、一時停止の頻度が高い)。前景の証拠は PM のスクリプトが 1.5〜3 秒ごとに 34 回書いた。**PM が手で CTRL+STOP を 1 回送ると h3a だけが畳まれた** (appslot_reclaim_count +1、fault_kill_count +1、h3b は生存、二重 kill なし — カーネル側の STOP 修正は正常)。台本の不具合として sol に原因の調査と修正を出した (wt/h3fix3)。資料: /home/hight/os32-tmp/h3x-*.json・h3x-arm.log・h3x-front.log。

- 時間窓: `arm --capture` / `loop-watch` は新layoutの `tick_count` をfreeze中に読む。
  捕捉開始後の最初の標本から**STOP前は3,000ゲストtick (PIT_HZ=100、30秒)**を上限にし、
  停止によるホスト経過をfixtureの500 tick猶予から引かない。差分はu32周回を処理。
  FIRING観測後のSTOP待ちも**210ゲストtick**にする (ホスト2.1秒では暴走判定前になり得る)。
  通常pollを0.01秒から**0.25秒**へ広げ、pause/メモリ読取の頻度も下げる。
  tickが進まない場合は最後の進行から**ホスト120秒**で異常終了し、既存のbreakpoint後始末を通す。
  ゲスト進行中にはホスト30秒の打切りを適用しない。受動`trace-watch`は従来のホスト15秒。
  STOP後2秒/10秒観測と前景証拠の5秒鮮度は従来のホスト時間。
  STOP送信後はtick上限を適用せず、after_10sまで採取する。古い有効な前景証拠は
  例外にせずSTOP前の上限内で更新を待つ (case/identity/map等の不一致は引き続き拒否)。
  単独`stop`も同じ捕捉pumpで210 tickを待ち、鮮度再試行・上限・後始末を揃える。
- arm: SHM WAIT/unarmed/consumed0に加え、**PARKED + parked_from_wait=1**、または
  **RUNNING + g_cur=対象 + in_op_wait=1**を同じfreeze内で要求。
  `appslot_park_commit` / `appslot_resume_commit`、`exec_park` / `exec_resume`、
  `multiapp::should_park` / `op_wait`を照合。相手がreadyでなければWMが現在のOP_WAITを
  実行し続けるため、PARKEDだけでは正しいWAITを拒否する。
  caseへ`armed_in_running_wait`を保存。この経路のOP_WAITはクリックでAS切替なしに戻るので、
  resume判定のswitch同値をこの印がある場合だけ認める (減少は拒否、PARKEDは増分必須)。
  resume印増分・mode一致・arm0/consumed1、実PCの着地/pending/WM kill検証は維持。
  別appがcurrent・待ち印欠落/不正・WAIT_KEY・古いpark印だけのRUNNINGは書込み前に拒否する。
- ホスト対照: 通常速度/ホスト30秒超の遅いゲスト、FIRING後210 tick、u32周回、
  FIRINGしないまま3,000 tick (周回あり/なし)、tick停止、後始末をAPI模擬clientで確認。
  PARKEDの従来正常対照に実行中OP_WAITのarm/継続を追加し、上記不正状態・switch減少を拒否。
  修正前台本の追加試験は**rc=1、45件中4失敗/skip1** (`h3fix2-red.log`)。
  ホスト壁時計への差戻しとPARKED限定への差戻しも写しの台本の変異として検証する。
  最終個別試験は**Python 47件 (layout 1件skip)、ILP32 42検査 PASS、
  C 8 + Python 65 = 73/73変異 runtime RED、compile/import失敗0、rc=0**。
  追加8ケース/12変異に両不具合の正常対照と負例を含む。
  全体検査の最終結果は下記ログと完了報告で確定する。
- 試験開発中: 変異実行を長時間化として中断 **rc=130**、再調査で既存start抑止変異の
  壁時計待ちと確認。再実行はtick差分の符号なし処理を外す変異1本が生き残り **rc=1**。
  周回を含む予算超過の負例を追加。2対照を同一caseで行った試行はarm再使用拒否で **rc=1**、
  独立caseへ分けた。実行中OP_WAITの継続条件も確認して試験を固定するため、途中の変異実行を
  もう一度中断 **rc=130**。最終ソース/試験の再実行結果を記録する。compile/import失敗をREDに数えない。

**Approve後のP3対応 (同日、確認レビューは回さずPMが差分を読む)**:
STOP後の3,000 tick打切り、古い前景証拠での例外、単独stopのホスト2.1秒待ちを修正。
上限直前2,900 tickでのSTOPと10秒観測完了、証拠未到着/更新なしで上限終了、
遅いゲストでの鮮度更新待ち、単独stopのtick周回と更新待ちを追加した。
採取tick読取とtrapの競合、捕捉全体でPARKEDのswitch同値拒否、stallの上限側も確認。
修正前は追加試験で**Python 53件中4失敗、rc=1** (`h3fix2-p3-red.log`)。
指定4変異 (tick読取のCaptureTrapをraise、stall上限1000秒、基点を1000 tick早める、
run_captureで実行中OP_WAIT印を常に真にする) とP3修正差戻し3変異を追加。
Pythonは**54件 (追加7件、layout skip0)**、ILP32は**42検査**、
C 8 + Python 72 = **80/80変異 runtime RED、compile/import失敗0、rc=0**
(`h3fix2-p3-mutate-final.log`)。変異定義の置換対象数のずれで途中の実行はrc=1
(`h3fix2-p3-mutate.log`)となり、定義を合わせて再実行した。
単独stopは送信直後にcaseを保存し、後始末失敗時も送信済み印を残す。
既存start抑止変異の受動捕捉でホスト時間を長く待ったため、模擬clientのmonotonicを
仮想化して再試験する (個別の遅いゲスト・stall試験は専用時計を使う)。
最終ソースの個別試験は`HOST32_RUNNERS=qemu python3 -u -B
 tools/tests/test_h3_park_resume.py --mutate`、ログは
`/home/hight/os32-tmp/h3fix2-p3-tests-final.log` / `.rc`。
全体検査は前掲と同じ`check_slot.sh h3fix2-coder`経由の`make check-changed
 NP21W_DIR=/dev/null`、ログは`/home/hight/os32-tmp/h3fix2-p3-check-changed.log` / `.rc`。
最終個別試験は**rc=0、Python 54件/skip0、ILP32 42検査、80/80変異 runtime RED**。
全体検査も**rc=0** (基点側の`build/sdk.mk`による全変異fallbackを含め完走)。
検査前後の差分は`cmp`で一致 **rc=0**。検査終了後に本結果だけ追記した。
試験準備中の初回RED実行は2失敗/2エラー、rc=1 (空の前景JSONを試験側で修正)。
続くRED実行は前述の4失敗、rc=1。これらと置換対象数エラーはruntime RED本数に数えない。
ゲスト・native・配備・commitは未実施。

**h3fix3 — 低速ゲストでのSTOP未送信 (2026-10-03、GPT-6 / Codex、コーダー)**:
基点 `328c379`、ブランチ `wt/h3fix3`。変更は台本・そのホスト試験・本票だけ。
PMの `h3x-arm.log` はrc=1、ホスト384.25秒。caseはPARKED経由でresume検証済み、
captureは365.30秒でresume_pendingの1標本だけ。`h3x-front-writer.py` は起動から90秒で終了し、
34回書いた最後の `observed_at=1791007330.9045947` はarmの71.45秒後、捕捉終了の294.05秒前。
終了後のPMのSHM観測はFIRING/mode5/consumed1、手動STOPで対象だけ回収された。
**原因**は待機中もtick最大3回とphase/所有者全照合を別々にfreezeするためゲストを長く止め、
500 tick猶予＋FIRING観測後210 tick待ちがwriterの寿命を越え得ること。
最後の証拠が失効しても台本は残りの3,000 tick予算を待ち、旧例外には理由がなかった。
ゲストログにはFIRING観測時刻がないため、実行中の失効開始点そのものは断定しない。
HTTPごとホスト0.1秒、thaw中22 tick/秒 (旧台本のfreeze込みで平均約8.5 tick/秒、
通常100Hzの約1割)、前景更新1.5/3秒交互の模擬clientで再現した。writerは
捕捉の18秒前に起動して90秒で終了する設定で、最後の証拠がarm約71秒後という
PMの流れに合わせた。旧台本のUSER/KAPI-loopとも仮想353.75秒の捕捉後に同じSTOP未送信、
上限tickのSTOPとresume未観測の診断もRED、計4件/4失敗、rc=1
(`h3fix3-red-regressions-final.log` / `.rc`)。先行のthaw中10 tick/秒・writer90秒の
対照も2件/2失敗、rc=1 (`h3fix3-red-http.log`)。先行のREDは模擬時計上限が短すぎて2失敗、
全59件の旧台本対照は8失敗/1エラー/skip1 (欠落欄のKeyErrorを含む)、rc=1。
これらを変異runtime RED本数には数えない。

修正は待機のtick/phaseを単独のu32読取りにし、resume照合とSTOP前の多欄観測だけfreeze。
phaseのlive読取りは待機判断のヒントで、STOP前には所有者/世代/phaseをfreeze中に再照合する。
前景JSONはfreeze前と観測/thaw後に読み、重い観測で鮮度が落ちれば更新を待つ。
firing_atは最初のFIRING観測のゲストtickを維持、u32周回・210 tick・5秒鮮度も維持する。
3,000 tickは捕捉開始が基点で500 tick猶予も含むため、FIRINGが遅すぎる場合は
残り210 tickを満たせないまま終わり得る。この場合も上限を延長せず不足tickを報告する。
上限のtickでもadvanceを1回実行し、そのtickで条件を満たすSTOPは許可して後続観測を完了する。
STOP未送信の例外はFIRING未観測／210 tick未満 (経過tick付き)／前景なし・不正／
前景失効／照合不一致を区別する。更新方法・鮮度は§5-2手順3を正典とし、
h3fix2のゲスト手順3でもそのwriterを捕捉終了まで動かす。

ホスト追加7件: HTTP時間込みのUSER/KAPI-loop、予算末尾のFIRINGによる210 tick不足、
上限tickでのSTOP、resume未観測の診断、観測コストで失効した証拠の再取得、前景JSON必須欄の欠落。
既存のFIRING未観測・前景なし・失効試験も例外の原因文字列を照合する。
待機tickへのfreeze差戻し、phase全照合の差戻し、最後の鮮度照合削除、診断文削除の4変異を追加。
検証環境は `CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、`PYTHONPATH=`、
`HOST32_RUNNERS=qemu`。`HOST32_RUNNERS=qemu python3 -u -B tools/tests/test_h3_park_resume.py --mutate`
は **rc=0、Python61件 (layout skip1)、ILP32 42検査、C8＋Python76＝84/84変異 runtime RED、
compile/import失敗0** (`h3fix3-tests-final.log` / `.rc`)。
検査用成果物の初回生成は `build/out/unicode.bin` を直接目標にして **rc=2** (生成規則なし、
`h3fix3-build.log` / `.rc`)。`unicode_bin` へ訂正し、
`make -j4 boot kernel programs sdk assets-deployed unicode_bin build/out/settings.db build/out/settings.v2.fixture
 NP21W_DIR=/dev/null < /dev/null` は **rc=0** (`h3fix3-build-ready.log` / `.rc`)。
既存mainの正規TTFをコピーし `fetch_fonts.py --check` rc=0。NHD/配備は行っていない。
生成後の同試験 (`--mutate` なし) は **rc=0、61件/skip0、ILP32 42検査**
(`h3fix3-tests-built.log` / `.rc`)。実ELF/mapのlayoutも確認済み。
`gen_memmap.py --check` / `check_manifests.py` は各rc=0。
最終の時間モデル調整後に指定の `HOST32_RUNNERS=qemu python3 -u -B
 tools/tests/test_h3_park_resume.py --mutate` を再実行し、**rc=0、Python61件/skip0、
ILP32 42検査、84/84変異 runtime RED、compile/import失敗0**
(`h3fix3-tests-ready.log` / `.rc`)。最終ソースの個別合否はこの結果。
全体検査1回目は **rc=2** (`h3fix3-check-changed.log` / `.rc`)。
既定selectorが`HEAD~1`を選び、基点側の`build/sdk.mk`を含む33ファイルも対象にして全変異fallback。
起動FD未生成により `check-packages-host` / `check-vk32-crc-host` が失敗。他の実行済み検査は通過。
検査前後の差分は`cmp`で一致、rc=0。生成レシピのNP21/W向けcopyを使わず、
`mkpkg.py --fd-args` → `mkfat12.py` でホスト作業ツリー内の2HD/1.44MB FDを生成、
`mkpkg.py --output packages/` → `genisoimage` でISO、`gen_generation_manifest.py`で証明書を生成。
このホスト生成は **rc=0** (`h3fix3-media.log` / `.rc`)。既存NHD/配備先は未操作。
最終 `python3 -B tools/check_select.py --lint` / `python3 -B tools/gen_tests_inventory.py --write`
は各 **rc=0** (`h3fix3-lint-final.log` / `.rc`、`h3fix3-inventory-final.log` / `.rc`)。
生成 `docs/TESTS.md` に差分なし。指定コマンド
`/home/hight/os32-tmp/bin/check_slot.sh h3fix3-coder env HOST32_RUNNERS=qemu make check-changed
 NP21W_DIR=/dev/null < /dev/null` は最終 **rc=0** を1回達成
(`h3fix3-check-changed-final.log` / `.rc`)。基点側の変更による全変異fallbackも完走。
全体検査内でもh3の61件・84/84変異が通過。検査前後の差分は`cmp`で一致、rc=0。
この結果だけ検査終了後に追記した。ログはすべて `/home/hight/os32-tmp/`。
ゲスト再確認はPM担当、NP21/W/NHD/配備/ini/実機・commit/pushは未実施。

**PMのゲスト確認 (未実施)**:
1. PMが最終一式の反映を[V1]で確認し、新ELF/mapからlayoutを生成。
   新規h3a/h3bをinitし、別caseのowner/世代を保存。既存break/user pauseは先に解除する。
2. 両fixtureがWAITのとき、対象のPARKED/待ち由来印1、または
   g_cur=対象/RUNNING/in_op_wait1を新layoutで確認。
   §5-2の`arm --mode USER-loop --capture RAW.json --out OBS.json --trace FRONT.json`
   (共通`--layout`/`--case`)を実行。手作業のSHM arm書込みで迂回しない。
   AS切替なしで待ちが戻った場合はcaseの印とswitch同値を確認し、resume印増分/consumed1を確認する。
3. ホスト30秒を越えてもゲスト500 tickの猶予からFIRINGへ進むことを確認し、
   PM/e9がFIRING後の新しい前景証拠を§5-2手順3どおり1.5〜3ホスト秒ごとに更新し、
   stop_sentまたは捕捉終了まで続ける。鮮度は0〜5ホスト秒。台本外の入力/フォーカス/debug操作を重ねない。
   FIRING観測後210 tick以上でSTOPが**一度だけ**送られることと、RAW/OBSの採取を確認。
   捕捉終了後、監視breakpointが残らずtrap/user pauseもないことを確認する。
4. `reclaim`、同fixtureの新規起動、外部証拠を添えた`verify`まで§5-2どおりに実行。
   reclaim+1/対象ID、相手の同世代での生存、pending/深さ、mode別の経路を確認。
   対象をh3bへ交換して再実行し、KAPI-loop・pf/gp/de/ud・park中WM kill対照を最終matrixで採る。
   最終matrixでPARKEDのarmを少なくとも1本採る (実行中OP_WAITのarmだけではpark→resumeの証拠にならない)。
   不正OP_WAIT状態はarm拒否/書込みなしを確認する。tick停止のホスト120秒上限と
   後始末は**ホスト試験で確認済み**。実ゲストのuser pauseは先に
   `user pause during capture`で中止するため、120秒上限の再現手順には使わない。

**h3fix3 のゲストでの実地確認と着地 (PM、2026-10-03、kernel `5159cd5`、17MB)**: 新しい case で layout → GUI で h3a/h3b → init → h3a に `arm --mode USER-loop --capture ... --trace FRONT.json --height 480`、前景証拠は PM の書き手が 1.5〜3 秒ごとに捕捉終了まで `replace` で書いた。**arm rc=0・ホスト 73 秒** (h3fix2 では 384 秒で rc=1)、PARKED から arm → resume の捕捉 → FIRING → **STOP 1 回**、`appslot_reclaim_count` 0→1・`fault_kill_count` 0→1、h3b は phase=WAIT で生存、`reclaim` rc=0。verify (新しい起動) と KAPI-loop・PARKED 以外の arm は h の最終一式で採る。独立レビュー Opus 5.5 は Approve (P1/P2 なし。`/api/mem` は core_lock の中で読むので単独読取りは裂けない、firing_at は安全側、STOP 直前に freeze の中で身元と phase を照合、二重送信なし)。**P3 は sol へ (wt/h3fix4)**: P3-1 STOP 前の freeze 照合を守る試験が無い (freeze 内の phase==FIRING の require を外す・checked() を block() に替える・stop_loop の 2 回目の鮮度照合を外す・resume の門から ERROR を外す、の 4 変異が生き残る)、P3-2 FIRING を観測した後に phase が変わったときなどの診断文の取り違え、P3-3 resume 前に fixture が死んだときの検出が 3,000 tick の上限まで遅れる、P3-4 `guest_tick()` が CaptureTrap を出さなくなったので `except CaptureTrap: continue` とその試験・変異は死んだ経路。

**h3fix4 の P3 対応 (2026-10-03、Codex GPT-6、wt/h3fix4、基点 `a958439`)**:

- P3-1: run_capture/stop_loop の live 読取り後、freeze 入口で phase が ERROR または AS 世代が変わる負例を追加。両経路とも STOP 0 回で例外になることを検査。単独 stop の身元照合中に前景証拠が 6 秒古くなる負例と、resume 前の ERROR の負例も追加。レビューの m1 (両方の frozen phase require 削除)、m6 (checked→block)、m2 (単独 stop の最後の鮮度照合削除)、m3 (resume の門から ERROR 削除) を変異定義に登録。
- P3-2: FIRING 観測後の phase 変化は `loop not firing: phase changed after FIRING` で中止。単独 stop は最初の poll から FIRING を要求し `loop not firing`。run_capture の完了判定は resume を先に要求し、未捕捉時は `resume not captured`。
- P3-3: resume 前の待機 poll に、SHM の割当て/app owner、slot の生存、AS と SHM の owner/generation の安価な live 読取りを追加。死んだ fixture/世代変更は 3,000 tick を待たずに拒否し、待機中の freeze は追加しない。ERROR は既存の resumed() の照合で即時拒否。待機中の死/世代変更と freeze 0 回を負例で検査。
- P3-4: 死んだ guest_tick() の CaptureTrap catch と `test_capture_tick_sampling_trap_is_drained`、対応する変異・注入オプションを削除した。freeze 入口での trap 競合の既存試験は維持。
- 試験は 8 件追加・1 件削除で **68 件**、ILP32 状態チェック **42 件**。変異は指定 4 件＋待機生存確認/phase 診断の 2 件を追加、死んだ 1 件を削除し **89 件 (C 8 + Python 81)**。修正前の台本へ新試験を適用した RED は rc=1 (7 failure)。最初の変異実行は新試験の subTest 内 RuntimeError が error 扱いとなり rc=1、試験側を修正した。
- 初回の通常試験は 68 件中 layout 1 件 skip (実 ELF/map 未生成)、ILP32 42 件 PASS。ゲスト検証・NP21/W・NHD・配備・ini・実機・commit/push は未実施。独立確認レビューは回さず PM が差分を読む。
- 共通環境は `CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp PYTHONPATH= HOST32_RUNNERS=qemu`。`python3 -u -B tools/tests/test_h3_park_resume.py --mutate` は **rc=0、89/89 runtime RED、compile failure 0** (`/home/hight/os32-tmp/h3fix4-tests.log`)。全体検査の成果物を準備する `make all NP21W_DIR=/dev/null < /dev/null` は **rc=0** (`h3fix4-all.log`)、/dev/null への FD コピー失敗警告は想定どおりで実配備なし。生成後の通常 h3 試験は **68 件/skip0 + ILP32 42 件、rc=0** (`h3fix4-tests-built.log`)、実 ELF/map の layout を確認済み。
- `python3 tools/check_select.py --lint`、`python3 tools/gen_tests_inventory.py --write` は各 **rc=0** (127 本、対応表漏れ0件、生成 inventory の追跡差分なし)。`git diff --check` は rc=0。追跡変更は台本・ホスト試験・本票の3ファイルだけ。
- 本記録を固定後、最後に `/home/hight/os32-tmp/bin/check_slot.sh h3fix4-coder env HOST32_RUNNERS=qemu make check-changed NP21W_DIR=/dev/null < /dev/null` を1回実行する。検査中・終了後に票/ソースを変更せず、実際の終了rcは完了報告と `/home/hight/os32-tmp/h3fix4-check-changed-final.rc`、ログは `.log` に残す。



共通環境は`CROSS_DIR=/home/hight/opt/cross`、`TMPDIR=/home/hight/os32-tmp`、`PYTHONPATH=`、
`HOST32_RUNNERS=qemu`。inventory生成`python3 tools/gen_tests_inventory.py --write`はrc=0、
生成`docs/TESTS.md`の差分なし。個別試験は`python3 -u -B tools/tests/test_h3_park_resume.py --mutate`、
ログ`/home/hight/os32-tmp/h3fix2-tests-final.log` / `.rc`。実ELF/map未生成のlayout試験1件はskip、
native/guestは未実施。lintは`python3 tools/check_select.py --lint` **rc=0**
(125本、対応表漏れ0件)。`git diff --check` rc=0。
全体検査の1回目は**rc=2** (`h3fix2-check-changed.log` / `.rc`)。
基点側の`build/sdk.mk`が選択器の許可型外なので全変異へfallbackし、
`check-vmkernel-lz4-host` / `check-vk32-crc-host`が未生成のkernel.bin/sqlite.bin/vmkernel.lz4で停止した。
検査前後の差分は同一。ホスト成果物の前準備不足を解消するため、
`make kernel NP21W_DIR=/dev/null < /dev/null` **rc=0** (`h3fix2-kernel.log` / `.rc`)。
製品ソース差分なし。生成後のh3ホスト試験は**47件/skip0 + ILP32 42検査、rc=0**
(`h3fix2-tests-built.log`)、実ELF/mapのlayoutも確認済み。
2回目も**rc=2** (`h3fix2-check-changed-second.log` / `.rc`)。
`check-manifests`がuserland成果物なし、`check-vk32-crc-host`がos32_boot.imgなしで停止。
`make kernel`だけでは前準備が不足していたため、
`make all NP21W_DIR=/dev/null < /dev/null` **rc=0** (`h3fix2-all.log` / `.rc`)で生成。
NP21W_DIR=/dev/nullへのFDコピー失敗警告は想定どおりで、実配備なし。
生成後の`check_manifests.py`と`test_vk32_crc.py --real --target`は各rc=0。
追跡ファイルは指定3ファイルのみ。この追加記録後に票/ソースを固定し、
最後に以下を再実行して**rc=0を1回**得る。
検査実行中は票/ソースを変更しない。

```sh
CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp PYTHONPATH= \
/home/hight/os32-tmp/bin/check_slot.sh h3fix2-coder env HOST32_RUNNERS=qemu \
make check-changed NP21W_DIR=/dev/null < /dev/null
```

最終検査 (3回目) は**rc=0** (`/home/hight/os32-tmp/h3fix2-check-changed-final.log` / `.rc`)。
全変異へのfallbackを含め完走し、h3は47件/skip0 + ILP32 42検査、73/73変異runtime RED。
検査前後の差分は`cmp`で同一 (rc=0)。検査終了後に本結果だけ追記した。
NP21/W・NHD・配備・ini・実機・commit/pushは未操作。製品コード・kernel.mkは変更なし。

### 5-3. 統合matrixと観測

親票§5-2の(a)master/(b)各AS/(c)post-exec毎boot/(d)leaseを適用する。低位present USERはSHM/trampolineだけ、APP/LEASEのmaster PDEは空、AS私有PFNはowner一致、共有shlib textはRO、lease全page/cache/refcount一致。PDE USER単独を漏れと数えない。guardと15〜16MB未登録部NP、DEVICE永久予約を確認する。

8MB planar/PEGC、17MB planar/PEGC/Cirrus、Ra26664MB PEGCの行ごとに、boot fail0→通常CUI/GUI/FEP→高位HostDrv read/write→親子→lease切替→map枯渇→trim→fault/STOP→次起動→V86往復を記録。8MBの私有総量2048KiBは親票§5-3の3配分で、shlib dataは実測分を内数、PD/PT/共有text/BBは別勘定。起動/描画/終了を20回。T2時点の固定帯で不足するなら必要/不足ページを記録してT3へ渡すことは既決だが、「2MB達成」とは記さない。

診断の番地は**受入対象の新kernel.elf / kernel.map / nmから毎回引き直す**。static symbolがmapに無ければnm/専用診断、`used_pages`は関数なので値の番地として読まない。NP21/Wは物理読取と画面、Ra266はmem/bootlog/シリアル診断を使う。読み口不足はh4で固定量の診断へ追加し、公開の任意物理read APIを作らない。

記録欄: SHA/build ID/hash/画像size/機種RAM/backend、実コマンド/rc、kselftest/地図、owner別used/free、lease数、leftover、bad_free、IRQ/例外深さ、ledger_irq_ops/exc_ops、fault終了種別、KHEAP/stack high-water、画面結果。brokerのboot自己診断+1は基準値、操作差分は0。失敗後の次起動と親データの一致までが1ケース。失敗/skipには担当と次の試験を記す。

### 5-4. GUI KAPI-loop の STOP (ユーザー決定: A + 二重 kill の修正、2026-10-02、GPT-6)

**状態: 実装・独立レビュー指摘の修正済み、ゲスト受入待ち。** 全体ホスト検査の最新結果は下の追補ログを参照。 作業木 `wt/kstop`、開始 HEAD `e1c213b`。ユーザーは
Opus の第三案 C ではなく **A** を選択し、OWNER_EXIT の内部引数へ終了種別を
渡す変更も承認した。設計 A = h3 の **KAPI-loop A / wm-kill**。
公開 KAPI・sdk/kapi.json・KAPI 版・h3 台本は変更しない。

**確定設計**: IRQ の GUI 受付拒否時、RUNNING/gui/CPL3 の current に
`stop_wm_req` だけを立てる。WM の入力読取り直後〜resume/start の隙間に
IRQ が来たときは shell slot に未配送要求を保持する。次の正常 syscall、既存park、
WMのabort_clear、回収で消費し、currentへ誤killせず同じAへ渡す。正常 syscall 完了後 (戻り値書込・callback 復帰後)、
第5の由来 `parked_from_stop` で WAIT_POLL に退避する。状態の公開値は増やさず、
WM の既存 pick/should_park で起こせる。resume は EAX を保存し注入リングを読まない。
既存 park・abort_clear・reclaim で要求を消す。WM は既存宛先解決で exec_kill。
`resume_one` の前段でも keyboard capture (Standalone) → top_level_abort を行う。
exec_start が同じ周の X3 より後に戻った場合も、未読 raw / deferred / abort_seen を
処理してから再開する。マウス・時計はそこで取り込まず、既存の起床順位/tick制御を保つ。
ABORTED の OWNER_EXIT は raw の未読 CTRL+STOP make、WM の abort_seen と
pending_raw 内の CTRL+STOP make を消費し、次の前景への二重 kill を防ぐ。
STOP 以外の打鍵・break は保つ。複数の未処理 STOP は既存 bool と同じく合流する。
STOP と無関係な ABORTED (request_kill_all、resume 失敗の exec_kill_one) でも、
その時点の実打鍵 STOP はこの合流に含めて消費する。
IRQ/例外では ledger を触らず、通常回収の深さ0、owner別回収、R1 pending一回消費を維持。

**レビューの扱い**: P1-1 は既知の制約として受け入れる。張本人=current と
前景連鎖末尾の宛先が違う場合、宛先を畳んだ後に張本人は保存 EAX で再開し、
再び KAPI-loop で WM を塞ぎ得る。窓なし・連鎖外の張本人も A では畳めない。
試験はこの挙動も固定し、受入の中心は「前景自身が KAPI-loop」である。
P1-2 は今回修正、P2-1 は第5由来の印・EAX保存・S6正常対照で確認する。
P1-3 (B) は対象外。P3-1 は上の名称対応、P3-2 は **時計の進行確認は STOP 後だけ**。
協調型なので KAPI-loop 中の時計停止は残る。GUI アプリの exec_run の子 (gui=0) が
KAPI-loop に入る場合も救えない (修正前からの制約)。
- 健全だが遅いアプリが前景にあり、別アプリの OP_WAIT の X3 や top-level が走ると、宛先リングの空きが 4 未満のとき先打ちの打鍵が数えられて捨てられる (以前はカーネルの待ち行列に最大 32 件残った)。
- 読み捨てには WM のショートカット (GRPH+TAB など) と SHIFT+SPACE も含まれ、宛先リングが満杯の間はキーボードでフォーカスを移して逃げられない (以前も読まれず詰まっていたので悪化ではない)。


**原因 (実ソースで確認、ゲスト再実行なし)**:

1. `drivers/kbd.c` は CTRL+STOP を raw リングに積む経路と
   `ring3_abort_request()` の経路を持つ。後者は `exec/exec.c` で
   `appslot_abort_admit(con_sink_is_enabled(), tick_count)` が偽なら戻る。
2. `exec/appslot.c` の受付は GUI / RUNNING / `in_op_wait=0` の場合、
   `now_tick-last_kernel_tick >= APP_RUNAWAY_TICKS` だけを例外として通す。
   `ring3_syscall_dispatch()` は入口ごとに `last_kernel_tick` を更新する。
   get_tick loop ではこの受付を通らず、`abort_req` は立たない。
   **CUI は同じ受付の先頭で無条件に通る**ため、IRQ 中は要求だけを立て、
   被割込み CPL=3 なら既存 R1 移譲、KAPI 中なら後の syscall 安全点で畳める。
3. GUI の raw 側は `gshell_gui_pump()` → `input::capture(Ctx::Pump)` に届き、
   CTRL+STOP make が `abort_seen=true` になる。しかし X4 は capture のあと戻るだけ。
   消費する OP_WAIT / top-level は get_tick loop から呼ばれない。
   従って **IRQ 側は受付拒否、WM 側は要求の消費点に到達不能**という二重の穴である。
   IRQ や raw 入力が全く来ないことを原因とするものではない。
4. `multiapp::abort_target()` は全画面 owner または前景窓 owner から
   launch 連鎖の末尾を選ぶ。一方 IRQ が知るのは current だけ。
   GUI の受付を一律に通す修正は、別アプリが動いている瞬間の STOP で
   current を巻き添えにするため採らない。暴走の時計変更も USER-loop の
   既存判定と OP_WAIT 待機の扱いを変えるため、本修正の近道にしない。

**A を選ぶ理由と比較**: 宛先解決と回収を既存 WM top-level に集約し、X4 の
許可操作と公開 callback ABI を維持できる。既存 OP_WAIT の park を偽装せず、
完了済み syscall を第5由来として区別する (API_CONTRACTS T8 を更新)。
前回の B (X4 で宛先を決め、syscall境界へ私有要求を渡す) は採用しない。
第三案 C (WMへ2秒戻らないことを暴走へ追加) も採用しない。
R1/R2 と [TASK_MEMMAP_V3 §3-5-1/§3-5-3](TASK_MEMMAP_V3.md) の
安全点と通常文脈回収の範囲内で接続する。

**A の不変条件・実装の境界**:

- IRQ は要求だけを記録し、AS/owner/ledger を更新しない。KAPI 本体・callback が
  全て正常復帰した安全点でだけ退避する。CUI、USER-loop の admit/IRQ 移譲、fault、
  既存 park 中 WM kill の受付と回収経路を変えない。
- IRQ の返却要求と raw/abort_seen は同じ STOP の二つの表現として処理する。
  通常 OP_WAIT に到達した場合・対象なし・終了/fault・slot 再利用にも古い要求を残さない。
  一回の STOP が二回の kill にならず、IRQ USER-loop の残存 raw も相手へ転送しないことを
  対照試験で確認する。USER-loop の残存 STOP による二重 kill も承認済み範囲で閉じる。
- KAPI-loop は h3 の **A / wm-kill**: 対象回収1回、fault/abort差分0、
  launch/resume pending と pending消費は0。R1 の IRQ/fault 経路は pending消費1回を維持。
  回収点の IRQ/例外深さ0、ledger IRQ/例外操作の差分0、対象 owner の pages/kinds0、
  SHM owner0/free、pending ID0を要求する。観測時の syscall/WM 深さは §5-2 の静止点規則に従う。
- 非対象 app の AS 世代・資源・保存レジスタを保ち、注入キーを勝手に消費しない。
  syscall の戻り値を改変せず、再開時に副作用のある KAPI を二回呼ばない。
  通常の OP_WAIT park と STOP 由来退避を識別し、h3 の park 証拠を偽装しない。

**実装・ホスト検証**:

- `tools/tests/test_kstop.py`: 実 AppSlot 全体と exec の IRQ受付/dispatch/完了退避/
  resume値選択、kbd raw除去、gui OWNER_EXIT 配送を ILP32 で実行。
  STOP が syscall の前/本体中、完了値/全13語保存、副作用1回、相手生存、
  CUI、USER-loop/tick wrap、既存4由来park、clear、reclaim、shell/CPL0拒否、
  IRQ/例外/WM深さの拒否、raw循環境界を検査。11変異すべて runtime RED (再開直前のSTOP保持/参照欠落の2本を追加)。
- `test_exec_r1.py`: 実回収/両着地/setjmp の既存対照を維持し、終了種別と
  ABORTED の raw消費を追加。正常終了/fault/USER-loop、CUI/GUI、launch/resume、
  park中WM kill、通常syscall abort、親文脈復元、pending一回消費を検査。
  AppSlot 200Bをassert。26変異すべて runtime RED (今回追加2)。
- gshell 実Rust: 追加5試験。旧 arg=0 の OWNER_EXIT で相手への2本目killが
  起きる正常対照、修正後の捕捉済み/退避STOP消費、S6/STOP退避とも前景のみ1回、
  張本人≠宛先の再開、起動直後のraw/退避/捕捉済みSTOP、窓なし宛先なしを固定。
  既存の連鎖末尾/全画面/park中kill対照も維持 (153試験PASS)。追加5変異を含む49変異が runtime RED。
- 新規は ILP32 1試験プログラム + Rust 5試験、追加変異は **18本**
  (=11+2+5)。各置換当たり数を固定し、コンパイル失敗はREDに含めない。
  kselftestが既に呼ぶ `appslot_resume_mark_selftest` に第5由来の検査を追加。
  ゲストでのselftest実行は未実施。実ハード/ページ返却の観測は以下のPM手順で行う。
- `tools/check_map.yaml` 登録と `gen_tests_inventory.py --write` を実施。
  ILP32は host32.py / qemu。個別実行ログは `/home/hight/os32-tmp/kstop-{new,r1,rust}.log`。
  初期の試験足場の型/サイズ不整合は修正済み。再開前に入力全体を取り込む案では
  既存8試験が失敗したため keyboardだけへ限定し、起床順位/tick制御の回帰を解消。

**Approve 後の P3 追補 (2026-10-02、GPT-6)**:

- P3-a: raw 満杯時、CTRL 無し STOP make / CTRL+STOP break / CTRL make が末尾を置換せず、dropped に各1件を加えることを全32通りの head で固定。make 条件と CTRL 条件を外す2変異を追加 (各置換1か所、runtime RED)。kstop は34/34変異RED。
- P3-b: X3 の読み捨て中に CTRL+STOP make を拾ったら、その周の raw 読取りを止める。STOP 後の make/break は kernel raw と X4 退避列の双方で保持し、OWNER_EXIT 後の次の前景へ配送する。既存試験の dropped/退避件数を追従し、Rust 1試験・1変異を追加。修正前は2試験RED、修正後は155試験PASS・53/53変異runtime RED。raw 満杯/カーネル dropped 増加に限る案は採用せず、上の既知の限界に記載。
- P3-c: WM ショートカットと SHIFT+SPACE も読み捨てる制約を上に記載。
- 直前未コミット版比: kernel.bin / vmkernel.lz4 は **±0 B**、gshell .text **−48 B**、.data/.bss **±0 B**、gshell.bin **−64 B** (225,128 → 225,064 B)。kernel の ASSERT 余白34,988 Bとe枠の残り15,040 Bは変わらない。
- 指定環境で make all rc=0。NP21W_DIR=/dev/null によるD88コピー失敗警告のみ。個別試験と変異、生成2本、lint、最終check-changedのコマンド/rcは最終報告と /home/hight/os32-tmp/kstop-p3-*.log に記録する。全体検査中は票・ソースを固定する。ゲスト・実機検証は今回の依頼範囲外で未実施。

**独立レビュー追補 (2026-10-02、GPT-6、P1なし・Request changesへの対応)**:

- P2-1: X3 は配送不能な raw を dropped / OVERFLOW として捨てて STOP まで読む。
  X4 の保留は維持。カーネルは raw 満杯時の CTRL+STOP make で末尾を上書きする。
  1枠予約と違い既に満杯の状態でも確実に保持でき、head と先行順序を保つためこの方式を採用。
- P2-2: gui_call の入口で OWNER_EXIT を OS32_ERR_INVAL。内部 gui_owner_exit は
  gui_call を経由せず g_gui_handler を呼ぶ。拒否の試験と1変異を追加。
- P3-1〜4: exec_park_stop の6守り、他4park/clear/reclaim/start/resumeの要求掃除、
  非GUI拒否を関数単位で変異。CPL0拒否、CTRL無しSTOP保持、OP_WAIT中のabort_reqも固定。
  R1はSTOP退避を実関数へ置換し、親caller文脈が残る条件でinvalidate欠落をruntime REDにする。
- P3-5: ring3_stop_park_count は退避成立ごとに+1。今回mapは **0x180B4C**。
  PMの再ビルド後は必ず引き直す。
- P3-6〜10: T4/T6/U8/PROTO_LAYOUT/KAPI_SPECを追従。Rustの3定数は
  Cヘッダ対照のstatic assertで固定し、公開ABIを増やさない。kbd注釈の位置、
  STOP以外のABORTEDとの合流、gui=0のexec_run子の限界を明記した。
- 足場のgrep: dispatcher.incのcaller_access、dispatch_host_source.cのdb_callerに
  回数を数えるstubとassertを追加。test_exec_r1/test_kstopは実dispatch/STOP関数。
  test_kapi_db_v50/test_net_linkの関数抽出は現行署名を確認。
  multiapp_impl_host/net_link_hostは入口規則のみ、con_sink_host/owner_reclaim_hostは
  回収部分だけの模型で、新しいSTOP呼出しの追加漏れなし。
  検索全文は /home/hight/os32-tmp/kstop-review-scaffolds.log。
- 個別確認: kstop **32/32**、R1 **27/27**、ring3_guard **15/15**、
  caller_access **24/24**、db_caller **15/15** の変異がruntime RED。
  Rust **154試験PASS・52/52変異RED**。この6集合は変異合計165本。
  初回はkstopのカウンタ追加後の置換不一致、R1のSTOP-invalidate変異SURVIVEDを検出。
  置換と親callerを含む足場を直して再実行済み。コンパイル失敗はREDに数えない。
- 今回のレビュー修正増分 (直前未コミット版比): .text **+192 B**、
  .data/.bssのsectionサイズは不変 (カウンタは整列余白に収まる)、
  kernel.bin **+192 B**、vmkernel.lz4 **+159 B**、__bss_end **0x18C754**、
  ASSERT余白 **34,988 B**。最初の修正前比は本体 **+1,344 B**、
  e枠16,384 Bの残り **15,040 B** (他作業との合算はPM)。
- 共通環境: CROSS_DIR=/home/hight/opt/cross、TMPDIR=/home/hight/os32-tmp、
  PYTHONPATH空、HOST32_RUNNERS=qemu、makeはNP21W_DIR=/dev/null、stdin=/dev/null。
  make all rc=0 (D88コピー失敗警告は指定の/dev/nullによる配備抑止)。
  個別は python3 -B tools/tests/test_{kstop,exec_r1,ring3_guard,caller_access}.py --mutate、
  test_db_caller.py --runner qemu --mutate、userland/gshell/host/integration.py --mutate、
  すべてrc=0。ログは /home/hight/os32-tmp/kstop-review-{kstop,r1,guard,caller,db,rust,all}.log。
- 本追補と生成地図/試験一覧を固定後、gen_memmap.py --write、
  gen_tests_inventory.py --write、check_select.py --lintを実施し、
  最後にmake check-changed (OS32_MUT_JOBS=4、OS32_MUT_NICE=0)を実行する。
  結果は /home/hight/os32-tmp/kstop-review-check-changed.log / .rc と最終報告へ残す。
  実行中は票・ソースを書き換えない。過去のrc=130二回とPMのrc=2は下記の履歴として保持。
  配備・NP21/W・NHD・ini・実機・commit/pushは今回も未実施。

**§6-1 e枠への内数計上** (同じ cross、build ID `e1c213b-dirty`、selftest込み):

| 指標 | 修正前 | 修正後 | 差分 |
|---|---:|---:|---:|
| kernel .text | 330,446 B | 331,550 B | +1,104 B |
| kernel .data / .got.plt | 32,647 / 12 B | 同左 | 0 |
| kernel .bss | 210,836 B | 210,900 B | +64 B |
| kernel.bin | 363,124 B | 364,212 B | +1,088 B (整列 -16 B込み) |
| __bss_end | 0x18C214 | 0x18C694 | **+1,152 B** |
| ASSERT余白 (0x195000まで) | 36,332 B | 35,180 B | -1,152 B |
| vmkernel.lz4 | 480,618 B | 481,193 B | +575 B |

AppSlotは192→200B、6slot分+48B (BSS差分に内包、別加算しない)。AS/ledgerの
構造は不変、16KiB管理ASSERTも通過。今回分1,152Bをe枠16,384Bの内数とし、
今回分を引いた残りは15,232B (他のe作業との統合残高はPMが合算)。圧縮余白38,999B。
基準ELF/LZ4は `/home/hight/os32-tmp/kstop-before.{elf,lz4}`。

**固定前コマンドと結果** (PYTHONPATH空、TMPDIR=/home/hight/os32-tmp、
ILP32はHOST32_RUNNERS=qemu、makeはstdin=/dev/null):

- `CROSS_DIR=/home/hight/opt/cross make all < /dev/null`: rc=0。
  初回はIPAフォント不在の非対話確認でrc=2。既存mainの取得済みTTFをコピーして解消
  (新たなライセンス同意やダウンロードなし)。最終ログ `kstop-all-final.log`。
  make all内のD88コピーは初回既定 `/tmp/np21w` で失敗。以後は
  `NP21W_DIR=/home/hight/os32-tmp/kstop-image-output` に隔離してビルド出力だけを保存。
- `python3 tools/gen_memmap.py --write`: rc=0。
- `python3 tools/gen_tests_inventory.py --write`: rc=0。
- `python3 tools/check_select.py --lint`: rc=0 (119検査、漏れ0)。初回の依存ヘッダ2件漏れは補完済み。
- `python3 tools/tests/test_kstop.py --mutate` / `test_exec_r1.py --mutate` /
  `python3 userland/gshell/host/integration.py --mutate`: 各rc=0。

この欄を書き終えてから最後に
`CROSS_DIR=/home/hight/opt/cross OS32_MUT_JOBS=4 TMPDIR=/home/hight/os32-tmp HOST32_RUNNERS=qemu
make check-changed < /dev/null` を実施する (PYTHONPATH空、NP21W_DIRは上記隔離先)。
1回目は ring3-guard の変異パターンが旧 OWNER_EXIT 引数0を前提として失敗。
残りC方言変異で12分以上出力が止まり、停止して全体rc=130
(`kstop-check-changed-first.log` / `.rc`)。元の木の不変性は保ったまま停止した。
その後パターンを更新し、`test_ring3_guard.py --mutate` はrc=0 / 14本runtime RED。
再開直前STOPも写しのハーネスでruntime REDを再現後に上記の保持で修正した。
2回目は `OS32_MUT_NICE=0` も明示して実行した (対象/変異数は同じ、nice +10だけ解除)。
db-v50回収順検査が旧 `exec_reclaim_owned(int id)` の正規表現で失敗し、
net-linkにも旧 `exec_notify_owned(int id)` の同じ追従漏れがあることを確認。
2回目も停止して全体rc=130 (`kstop-check-changed.log` / `.rc`)。
**全体検査のrc=0という完了条件は未達**。1回目・2回目とも検査中の票/ソース変更はない。
停止後、この2検査を現行署名へ追従させた。製品の回収順・資源操作は変えていない。
以下の個別確認・lintをこの欄の固定後に実施し、結果は最終報告と
`kstop-signature-checks.log`へ残す。依頼の再実行枠は使い切ったので3回目は実施しない。
PMへの引継ぎは **最終差分のcheck-changedを新たに実行しrc=0を得ること**と、
Opus 5.5の差分再レビュー、下のゲスト確認。現時点で受入完了とはしない。

旧ソースの写しによる二重kill正常対照も追加実行した。HEAD の修正前 handler.rs を
一時木へ復元し、登録済み `kstop_old_owner_exit_contract_reproduces_second_kill` を実行。
捕捉済みSTOP・退避STOPとも app 3終了後に無関係なapp 2がkillされるassertがPASS (rc=0)。
ログ `kstop-old-source-control.log`。作業木のソースは変更せず、修正後153試験PASSと対照した。

**PM へのゲスト手順 (A 採用・実装・ホスト検査完了後、未実施)**:

1. PM が新一式の反映を [V1] で確認し、新 ELF/map から §5-2 の layout を生成する。
   GUI で h3a/h3b を起動し、別々の case で init。両 AS の owner/世代を記録する。
2. §5-2 に従い対象の実 park → arm KAPI-loop → resume/FIRING を採取する。
   対象が前景である新しい証拠を渡して、修正済み台本から STOP を一度だけ送る。
   台本の `ok:1` 対応は別担当の成果物を使い、この段では変更しない。
3. 新 kernel.map の ring3_stop_park_count を前後で読み、KAPI-loop の STOP で +1 を確認する。
   打鍵・クリックを重ねて宛先リング/raw を満杯にした場合も再実行し、同じ退避・回収を確認する。
   A の wm-kill 対象ID/回収1回、pending0、abort/fault差分0、対象FREE/owner0、
   相手の同じ AS 世代での生存、ledger/深さを既存判定で確認する。
   時計の再進行と相手窓への操作で WM 再開も観測する。次起動の新世代まで確認する。
4. 両fixtureで対象を交換して再実行し、USER-loop・4 fault・park中WM kill の対照を行う。
   e〜g を含む最終一式で全証拠を取り直す。native のホスト検査は PM が行う。

NP21/W・NHD・配備・ini・実機・commit/push は未実施。

**着地とゲスト受入 (PM、2026-10-03、main `9897cf4`、NP21/W 17MB・今の ini)**: 独立レビュー (Opus 5.5) は 1 回目 Request changes (P2 2 件・P3 10 件) → 2 回目 Approve (P3 3 件) → P3 対応の差分確認で Approve。PM のホスト (既定 `HOST32_RUNNERS=native qemu`) で `make all`・lint・`check-changed` rc=0 (1 回目は既存試験 test_caller_access の足場の追従漏れで rc=2 → 直して rc=0)。main へ取り込み (TESTS.md の競合は生成器で解消)、`make all`・`make check` rc=0。停止 → 停止確認 → `nhd-pull` → `deploy-kernel` → `deploy` → 起動。`ver` の Commit `9897cf4`、`/boot/vmkernel.lz4` 481,335 B 一致。`deploy-kernel` の prune で `/usr/bin/faultprobe_r3.bin` が刈られ、ゲストの `ls` で無いことを確認 (`faultprobe.bin` は残る)。kselftest pass 270 / fail 0、db・klibc・alloc・d0a・faulttest 4 種・STOP・V86 は従来どおり。
- **GUI KAPI-loop の STOP (受入の中心)**: h3a/h3b を起動・init、h3a に台本で `arm --mode KAPI-loop` (台本は 30 秒の窓で STOP を送れず止まった — 既知の時間窓の問題、後始末は正常)。SHM の phase=6 (FIRING) で h3a (app 2) が `g_cur`・RUNNING・`in_op_wait=0`。CTRL+STOP を 1 回 → 2 秒後に h3a の slot は FREE、`appslot_reclaim_count` 14→15 (+1、last=2)、**`ring3_stop_park_count` 0→1** (案 A の退避経路)、`fault_kill_count`・`ring3_abort_count` は不変 (h3 の経路 A / wm-kill と一致)。h3b は生存し前景になり、WM が回る (時計が進む)。修正前 (`4693a62`) は同じ操作で 70 秒たっても畳まれず GUI 全体が固まっていた。
- **通常の STOP (S6)**: h3a を起動し直して前景のまま (OP_WAIT 中) CTRL+STOP → `appslot_reclaim_count` +1 だけ、h3b は生存。
- **USER-loop の STOP と二重 kill**: h3a を起動し直し、h3b を前景にして、h3a の SHM block に mode=USER-loop・arm=1 だけを書き (台本の arm は「OP_WAIT park でない」で断ったので、台本と同じ書き方で PM が直接書いた観測 — 受入の PASS 判定ではない)、h3a のタイトルをクリックして FIRING。1 回目の CTRL+STOP は FIRING から約 1 秒で、暴走の判定 (200 tick) の前だったため何も起きない (修正前からの挙動)。2 回目の STOP で `fault_kill_count` 7→8 (R1 経路)、`appslot_reclaim_count` 16→17 (**+1 だけ**)、**h3b は生存**。修正前 (`8bea831`) は同じ形で `appslot_reclaim_count` +2 (h3b も kill) だった — 二重 kill は解消。
- 残り: 台本の捕捉の時間窓 (fixture の 5 秒の猶予がホストの 30 秒を超える) と arm の park 判定 (2 本とも OP_WAIT のとき PARKED にならない) は h の最終一式までに台本側で直す。打鍵を重ねた後の STOP、前景以外の張本人、park 中 WM kill の対照は h の最終一式で取る。

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

**T3 前の文書の再整備計画 (ユーザー指示 2026-10-03)**: T2h の受入の後、T3 の実装に入る前に文書の再整備計画 (完了した T2 の節のアーカイブ・INDEX.md 冒頭の正典表の見直し・重複と古い記述の洗い出し・担当) を立ててユーザーに提示する。提示までは T3 に着手しない。

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
  - V86 の出口は 6Ah の標準/拡張 (20h/21h) も戻さない — 9821 でゲストが拡張モードのまま戻ると A6h が効かず E0000h が MMIO のまま ([U] 00A6h)。未対処・未観測。
  - **P3 対応**: 変異ごとの期待FAIL文言をstderrで照合し、V86の失敗理由を分離。
    `gfx_init` / `gfx_init_200` の消去欠落2変異と両ページの初期化直後検査を追加し、
    計13正常条件成功・16/16変異runtime RED。定数統一で増えた置換候補は通常出口だけに限定。
    `TARGET_SRCS` を目録生成器へ公開し、規則はsdk.mk末尾、PHONY列へ追加。
    近い試験の語幹一致の慣例に合わせて
    [display_cleanup_tdd.md](../../../tools/tests/display_cleanup_tdd.md) を置き、V86/初期化の
    変異記録をまとめる。既存splashの足場拡張は
    [boot_splash_native_tdd.md](../../../tools/tests/boot_splash_native_tdd.md) に追記。
    shutdown/ゲスト入口のA4h/A6hもGDC_PAGE_0へ統一し、定数に資料注記、余分な空行を削除。
    最終検査は記録/ソースを固定して実行し、rcは最終報告と
    `/home/hight/os32-tmp/disp-p3-check-changed.log` に残す。
