# TASK_T2D_T2H — T2d〜T2h 詳細設計

> 状態: **実装中 (2026-10-07)** — T2d・T2e (e1〜e11) は受入済み。T2e は e12 の整理、T2f は f1a〜f4 (f2〜f4 は未結線)、T2h は h2・h3 の準備まで着地。残りは e12、f5 以降、g、h の統合受入 (現在地は [HANDOVER_2026-10-06](../agents/HANDOVER_2026-10-06.md))。
> それまでの状態: 設計中 (2026-10-01) — 独立レビュー Opus 5.5 は 1 回目 Request changes (P1 2件 / P2 11件 / P3 8件) → 反映 → 2 回目 Approve (P3 5件は §11 の実装時の注記)。
> 作成: GPT-6 / Codex。調査基点: main / docs/t2d-h-design 共通 **9ae6073406c2027fd50938e3870a3fb3888cd7f6**。
> **実行記録は 2026-10-06 に [archive/v3/TASK_T2D_T2H_RECORDS.md](../../archive/v3/TASK_T2D_T2H_RECORDS.md) へ移した** (この票は契約・分割・受入条件・未実施の手順だけ)。延ばした試験と未実施は [DEFERRED_TESTS.md](../DEFERRED_TESTS.md) が正。段の記録は 1 段 10 行以内で書く ([ROLES §0](../agents/ROLES.md))。
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

**今渡されたポインタ**のUSER walkは保存PD=現在CR3、現slotのAS/owner/generation一致を要求。**登録済みポインタ**は別契約で、登録時に `{app_id, AS, pd_phys, owner, generation, origin}` をkernelのredir記録へ値保存し、呼出フレームへのpointerは保持しない。generationはAS寿命の単調識別子 (slot/owner/PD再利用と区別、周回時は再利用拒否)。redirのnest/park保存・復元にも付随させる。使用時に生存台帳から登録者ASを引き直し、失効したAS pointerをdereferenceする前に同一性を検査する。登録者が死んだ/世代不一致なら失敗し、書き手の子を登録者と取り違えてkillしない。
書込みは登録者PDをwalkしたPAへ `P2V(pa)` でpageごとに行う。現在CR3への一致条件は課さず、元のVAへ直接書かない。読取り側も登録者PDからcopyし、今渡された出力bufferとは別に検査する。容量/位置のoverflowとlen<=capacityを確認し、検査失敗ではデータ・位置不変。生存確認→全範囲walk→copyの間は短いIRQ保存区間でAS回収/切替を防ぐ。

両契約とも、PDはAS ownerの生きたPD、APP/lease PTは控えのframeと台帳owner、共有PTはmaster登録済みframeとの一致を先に確認してから読む。presentな任意RAMをPTとみなさない。PDEのPSを拒否、PDE/PTE両方PRESENT|USER、出力は両方RW。返ったPFNも私有RAM・SHM・shlib RO・RAM leaseの管理情報と突き合わせる。一般copyではMMIO/VRAMを拒否し、RAM RO lease/shlib rodataは入力だけ可。trampolineのRO+USERページ内の `ring3_user_str` scratchも、登録済みtrampoline backing/PDE/PTEを照合して**入力に許可**する (早期分類にも追加)。出力は拒否。
次の文字列返却で上書きされる既存寿命を越えて保存しない。低位VRAMが暫定USERでもB1の例外にしない。

walkはT2cの低位恒等backingをP2Vで参照し、**dではmaster往復を除去**する。短いirq_save区間で範囲全体を検証してからcopyoutし、通常の検査失敗は出力全byte不変。どの出口も入口IF/CR3不変。複数出力を持つ入口は全出力範囲を先に検査してから書く。allocation/VFS/SQLite/callback/GUI pumpは区間外。boundedなDB/lease構造体用であり、無制限サイズを割込み禁止でコピーする入口を新設しない。

早期 `ring3_ptr_ok` はNULL・高位image/heap/stack/SHMと**現在ASの有効lease**の分類に拡張。穴の最終判定はwalk。dでは旧consumerのため既存低位VRAM分類を暫定維持し、eで消す。既存出力ガードはRW検査を弱めず新walkへ集約する。`_always` の保存済みapp pointer経路をtrusted扱いへ変えない。

DB接続は `kapi_db.c:512` / `:974` / `:1116` の3呼出箇所 (db_open / db_open_existing / db_prepare_only)とその補助だけ。copy失敗では既存rcを返し、SQLite入口カウンタ差分0、旧stmt/FDに副作用なし。B3/B4やFEP全体を安全化したとは報告しない。 **例外: `db_prepare_only` の旧 stmt はコピー拒否でも入口で finalize する** (公開仕様 KAPI_SPEC.md:1304-1306 を優先、§10-13)。

> 記録は archive へ移した (2026-10-07): [d0a の試験と判定](../../archive/v3/TASK_T2D_T2H_RECORDS.md#land-r94)。

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

dのcopy契約とlease基盤がGREEN。変更は `gfx/gfx_core.c` / `gfx/backend_pc98.c` / `backend_pegc.c` / `backend_cirrus.c`、`exec/lease.[ch]` / exec / appslot、paging / v86_mem / `kernel/shm.c` / kselftest、`drivers/kcg.c` とboot順序/公開font入口、KAPI正典と生成、SDK gfx、`lib/utf8.c`、CRT、Rust guiのclient/shlib、gshell handler/multiapp、rshell tvdumpとguestマーカー/観測側。台帳のSURFACE登録補助は必要範囲だけ。
新ドライバ・BAR sizing・planar BBのpool移動・Unicode組表化・page0 NP化はしない。

### 2-2. 公開APIと授権

親票§3のquery/lease/unleaseを追記する。公開descはu32のref、role/backend、format、width/height/pitch、planes数、plane offset[4]、bytes、最大access。phys/kernel pointer/cache指定欄は無い。kernel内の48B ledger_surfaceと公開descを同型にしない。queryは値の写しだけ、lease数を増やさない。

query/leaseの両方で次の授権表を適用する。Uは保存したUSER callerと**現在実行中**のslot/AS/owner/generationの一致、Gはkernelが読んだ `hdr_flags & OS32X_FLAG_GFX`、FはGUIの `appslot_gfx_owner()==caller.app_id`。親/park中ASに代理貸与しない。UnicodeはROだけ、他roleもdescの最大access以内。user引数のownerや単なるsidを権限にしない。

| モード | CLIENT | DISPLAY | TVRAM | Unicode |
|---|---|---|---|---|
| GUI (`gui_mode=1`) | U、選択backend。通常GUIにはG不要、全画面GFXならGかつF | UかつGかつF | 拒否 | U、backend非依存RO |
| CUI (GUI→CUIを含む、`gui_mode=0`) | U、選択backendの初期化済み面。G不要 (`libos32gfx_init→BB→gfx_present`を許可) | UかつG、CUI前景実行者のみ | U、CUI前景実行者のみ。G不要 | U、表ready時RO |
| gshell不在のboot (`gui_mode=0`) | 通常AS開始前のUSER要求は不可。開始後はUかつboot所有の初期化済み面、CUIと同条件 | 通常AS開始後UかつG | 通常AS開始後U、G不要 | 通常AS開始後Uかつ表ready、RO |

TRUSTEDのboot/kernel/WMはlease授権を迂回してUSERへ貸すのではなく、内部backing口を使う。caller由来は§1-2で固定し、CPL/現在CR3で推測しない。CUIで `g_gfx_owner` がWMのままでもUを直接照合する。GUI→CUIは旧leaseをrevoke/TLB同期してモード世代を更新してから前景CUIへ貸す。CLIENT backingの台帳ownerがgshellのままでも、物理所有と利用授権を分け、この表で貸与を許可する。gshell不在のboot ownerも同様。CUI→GUIでも貸与を切り替え、park中GUIの古いviewは再attachまで使用不可。

不連続planar DISPLAYは4 SURFACEをまとめて取得する内部lease_acquire(count=4)を使用する。公開側にはroleを指定する束取得口 `gfx_surface_lease(role, refs, count, access, out)` (queryが返す最大4本のref配列を入力、outは4 viewとcount)を追記し、任意物理やASを受け取らない。単一surface_leaseはCLIENT/Unicode/TVRAM等の単面用とし、planar DISPLAYの単面要求は拒否する。queryは束の全desc/refを一つのsnapshotで返す。leaseでcount・sid順・role/backendと**各generation**が現在の束に一致することを照合し、旧世代はSTALE、
面欠落/重複/別束はINVALでout不変。4 slot/全PT/全出力を事前検査し全成功か全巻戻し。releaseは各tokenでよく、再init/終了revokeは全関連tokenを処理する。

refはB1でkernelへコピー、出力をB1で事前検査→授権/世代→slot/VA/PT準備→短いIRQ保存区間で全公開→copyout。準備からcopyoutまでAS scheduling/callbackなし。予期しないcopyout拒否も取得したleaseだけを解除してout不変に戻す。失敗で既存lease、master、他AS、token会計を変えない。公開rcは§0-2。token/generation周回拒否と8本/AS・16 SURFACEを増やして逃げない。

### 2-3. 面・alias・再attach

低位VRAMのmaster aliasがWB、SURFACEはUCというT2b-Rの差をここで閉じる。paging_initの初回表構築でnative VRAM aliasをUCにし (liveなWB aliasを後から変更する方式にはしない)、device窓既存aliasとlease aliasのcacheを台帳と一致させる。途中だけ異属性aliasを公開しない。RAMはWB。V86でこの初期設定を壊さないため、`v86_ident_map`のVRAM `[0xA0000,0xC0000)` とEプレーン行はsetup/teardownの**両方**に台帳cache由来のPCDを含める (§2-4、e10b)。ROM行に一律PCDを付けない。planarはpitch×heightでoffsetを再計算、
200/400行をページ丸めのplane strideへ変えない。paddingは占有全ページ内でゼロ、他用途を同居させない。Cirrus CLIENTは非表示MMIO面、DISPLAYとページ境界で分離した既存backingを使う。

SURFACE容量は既存4本 + planar DISPLAY4 + PEGC DISPLAY1 + TVRAM1 + Unicode1 = 最大11本を設計上の勘定とする (無効候補のslotも数える)。planar CLIENTは4planeでも1本。通常GUIの2実体はCLIENT2 + Unicode2 = 最大4 lease、全画面planarのDISPLAY4を加えて8。さらにTVRAMも同時要求すればFULLが正しく、上限増設せず使い終わったものを解放する。boot自己診断の一時SURFACEは公開前に返す。

kernel描画は内部 `gfx_kernel_framebuffer` を使用。bb_b/r/g/iとbb[]、backend bb_baseはSURFACE backingから設定し直す。旧公開void gfx_get_framebufferはUSERにはleaseを返す橋 (ASあたり専用の互換CLIENT tokenを再利用、8本の内数)、失敗はsyscall安全点で明示終了。SDKの新checked attachは自分のtokenを保持し、繰返しattachではqueryの世代と比較して再利用する。新SDKは旧橋を呼ばないので二重取得しない。TRUSTED直呼びは内部fb、USERへkernel aliasを返すfallbackは禁止。

再init/所有者剥奪は貸与停止→関連全AS revoke→TLB同期→参照0→backend/geometry更新→新generation公開。失敗時に旧tokenを復活させず、予約済みfallbackを新世代で公開する。CLIENT backingのboot→gshell移譲は1回、GUI→CUI→GUIで物理を取り直さない。

SDK gfxのC静的側とshlib側の両方で、待ち/yield/resumeから戻るたび、最初の描画前に**query KAPI**でdesc/refのgenerationを照合し (SHMの可変公開値は正典にしない)、不一致なら旧pointerを消してchecked attachを行う。一致ならtoken/viewを再利用する。GUIイベントループだけでなく、全画面のsys_yield/入力待ち復帰と明示再initの帰路も対象。shlibには内部再attach入口を設けるならprotocolを更新する。attachが部分失敗したら今回得たtokenを全部返し、両実体とも描画不可としてエラーにする。描画途中でのcallback/再initを許さない。生pointerを保存して再initを跨ぐアプリは契約違反、VA再利用後まで検出できるとはしない。

Unicodeはkernel所有FIXED_RAM/RO SURFACE。ユーザー版utf8だけsetterでlease pointerを受け、CRTとshlib初期化の双方が取得/終了解除する。kernel版は恒等のまま。既知JIS対照は維持、取得失敗時はpointerをNULL、`jis_table_ready=0`にしてCRT/shlib初期化を続け、未ready時の既存代替表示を使う。未初期化/古い表は読まず、低位直読へ戻さない。T3のKAPI化でこの橋を撤去する (§7)。フォントはkcg_read系を使いfont帯全USERを消す。

`kcg_load_font`のLZ4 scratch (`drivers/kcg.c:215`、Unicode起点から最大344KiB)はUnicode表とplanar BBを重ねる。公開口を残したままeを受け入れると正規KAPIからSURFACEを破壊できるため、[T3 §3](TASK_T3_LAYOUT.md)の**boot後NOSYS化をe8bへ前倒し**する。D7は変更しない。boot内部読込だけを許し、scratch失効→Unicode表構築/検証→BB・mailbox初期化→SURFACE公開→通常ASの順を固定する。bootフェーズ終了後はTRUSTEDを含む全経路でNOSYS・VFS進入/書込みなし。公開KAPI slotは残し説明と版をeの一式で更新する。`font_load_test`はNOSYS期待の成功試験へ変更し、
guest台本/host成功経路期待も同時更新。表/BB/mailbox不変はkselftest/host観測で確認し、受入から除外しない。scratch定数の整理とUnicode KAPI化はT3に残す。

### 2-4. USER切替の一括境界

まず新consumerを旧USERのある状態で試す。この途中結果は隔離合格ではない。全consumerが揃ったe最終差分で次を同時に行う。

1. `shm_init`がSHM状態/guardを初期化した**後**、最初のAS生成前に専用master初期化口でSHMをUSER|RW/WB、trampolineをUSER|RO/WBにする。boot専用初期化完了時のSHM契約はUSER|RWで、後続boot処理にsupervisorへ戻させない。既存の起動ごとの共有mapを撤去。
2. execのVRAM/font/Unicode/BB直接USER化とring3_ptr_okの旧VRAM例外を撤去。addrspace通常map/unmapは私有PTだけ許可、共有PTへの操作は無変更拒否。
3. masterの汎用map/set_page/map_physはlive AS後のUSER昇格を禁止。V86専用session口に加え、SHM専用権限口を**既存USERページのRW切替**として例外にする。`kernel/shm.c`のlockはUSER|RO、free/free_owned/cleanup_allはUSER|RW、初期化は1の順でUSER|RW、全てPCDなし。SHM範囲/PFN/共有PTを固定検査し、USER欠落は一般昇格で救済せず不変条件違反として検出する。PDE USERを維持、変更後active TLBを同期し、他ASは次回CR3 loadで同期。exec_exitのfree_owned後にも次アプリのSHM書込みが通ること。通常のDEVICE aliasはsupervisorのまま。
4. V86全出口で旧低位内容/属性を復元し、VRAM/Eのsetup/teardown両行のPCDを台帳と一致させる。SHM/trampoline用のPDE USERも復元→CR3 reload→通常地図検査。V86中の通常AS実行/生成/検査は禁止。T7の低位専有化やpage0 NPは前倒ししない。
5. tvdumpはCUI授権済みchecked copy KAPIへ移しTVDM wire形式を維持。nop/ring3_hello/fault/guardの観測マーカーは割当済みSHMへ (CRT非依存入口も同じ取得/初期化)。保護違反そのもののVRAM書込みは残す。db_v50_testのVRAM許可期待をRAM境界成功と未貸与VRAM拒否へ変更。
6. memory世代更新と現行ビルド対象の全再ビルド。apps/gameはユーザー決定により対象外。caller追随要件と再開時ゲートをhへ渡し、決定による持越しと記録する (§5/§8)。

**e11 結線表** ([持越し台帳 §2](../DEFERRED_TESTS.md#2-関門-e11-公開-kapi-の一括版の更新は-1-回) の ID。§2-4 項の「—」は内部準備・契約補足)

| ID | §2-4 項 | 担当パック (a/b/c、複数可) | 何を | 接続先 (port・KAPI) | 撤去する旧経路 | 期待が変わる試験 | 受入 |
|---|---|---|---|---|---|---|---|
| E11-1 | 2 | a/c | 面・束・Unicode・両 gfx 実体・帰路の結線 | 面query/lease/bundle、attach/Unicode port、publisher・互換橋 | 本番 NULL port、USER への kernel alias、Unicode 低位直読 | lease/gfx_boot・SDK連結、帰路失敗 | 世代不一致で再attach、部分失敗で全返却・描画不可、RO日本語 |
| E11-2 | — | a/b/c | 結線後の予算実測 (§6) | kernel・SDK・shlib の最終リンク | 撤去前の見積り流用 | サイズ計測 | 同一toolchain前後差、e枠・圧縮・8MB私有量 |
| E11-3 | 2 | a/b | 低位 USER・共有PT操作の閉鎖、Cirrus DISPLAY公開 | lease CLIENT/DISPLAY、通常map/unmap | VRAM/font/Unicode/BB直map、`ring3_ptr_ok` 例外、`exec_map_shared_bb` | app_bb_overlapを退役/lease化、paging_bounds拒否へ | 共有PT無変更拒否、DISPLAY NONE→RWは授権leaseだけ |
| E11-4 | 3 | b/c | 台帳KAPI-CALLBACK/OWNER/DISK-AUTH受入後の公開変更 | 値返し列挙KAPI・caller、OS32X授権flag | 旧callback列挙caller・旧授権方式 | man列挙、SHM他owner拒否、ディスク拒否/正規利用 | 修正の証拠＋caller追随、未解消のまま公開しない |
| E11-5 | — | a/b/c | Run全画面の入力配送 | 全画面owner 1または専用KAPI・WAIT_POLL | 端末だけへの注入 | Run全画面4点 | キー終了、端末子二重注入なし、窓漏れなし、WAIT_POLL到達 |
| E11-6 | 1/3/6 | a/b/c | boot順・P3残件と公開一式 | SHM master・cdecl/終了門、CRT/shlib・公開一式 | 起動毎共有map、古いtoken/utf8初期値・二重取得 | map_user_keep内部改修、font NOSYS、wait帰路4件 | SHM初期化後RW→初AS、漢字維持、全現行対象再ビルド・世代整合 |
| E11-7 | — | c | 公開契約の注記 | KAPI文書・SDK契約 | STALE/INVALを秘匿保証とする解釈 | 契約と生成の照合 | 推測可能性明記、callback/scheduling禁止 |
| E11-8 | — | c | 台帳KAPI-AUDIT-FIX/OWNERの受入・分類を接続 | `pipe_get_buf` kernel番地返却の意味変更・未監査区分 | 未監査を安全扱いする区分 | 区分別正常/拒否対照、範囲検査P3変異 | 到達可能な穴は先行修正、意味変更だけ一括公開 |
| E11-9 | 5 | b/c | tvdumpのCUI授権 | `tvram_readchar_at` checked copy | 無授権TVRAM読出し | tvdump正常/非所有者拒否 | CUI前景のみ、TVDM wire不変 |
| E11-10 | 2 | b/c | 旧Eの拒否と成功対照 | 正規CLIENT lease | BB低位直書き生存 | `ring3_guard bb` E→拒否 | E kill、正規CLIENTは生存 |
| E11-11 | 2/5 | b | 未貸与VRAM拒否 | DB出力のB1 walk | VRAM出力許可 | `db_v50_test` 拒否追加 | RAM最終byte成功・guard越境拒否も維持 |
| E11-12 | 5 | a/c | h3本人識別・writer初期化 | 値返し本人識別、owned SHM marker | 旧PM(A)識別、TVRAM観測 | h3・nop/hello/fault/guard | CRT非依存も初期化、fault目的地一致、違反用VRAM書込みは保持 |
| E11-13 | 3 | b | SHM全ページROの実効性 | `paging_shm_set_rw`、lock/free/owner回収 | 呼出し成功だけの判定 | SHM lock書込み拒否、shm_reuse | CPL3拒否、free/exit後の次AS全ページ書込み成功 |
| E11-14 | — | c | SHM・ページ長の公開定数 | SDK共有ヘッダ・caller | 私有PAGE_BYTES等の重複 | SDK/markerビルド | 公開値とkernel一致、生成・版は一括 |
| 補4 | 4 | a/b | V86全出口と通常地図の結線 | session end → CR3 reload → 3段検査 | 旧出口の復元漏れ | V86正常/失敗/STOP/kill/K1・VM INT80h | 内容/属性・PCD・PDE USER復元、通常AS禁止、DISPLAY/TVRAM再lease |

**e11 統合受入 (PM、2026-10-07、main `1d222ac`、17MB・今の ini・PEGC)**:
受入合格: kselftest 283/0、guest_tests PASS 15、低位 USER 撤去・E kill/F 生存と revoke kill・SHM RO/再利用・PT0/V86 監査・世代拒否。
値返し列挙、CUI 描画、GUI/日本語、Run 全画面のキー終了を受入。今回の変更による退行なし。
既存問題は X-12 (PEGC の blit_test/bench_scale2x)・X-13 (font_test のフォント未配備) に登録。
持越しの所在: E9-1、E10-1/4/5/6、E11-5/8/9/12/A2/A3/BUD/PERF、PRIO-1/3、H-4/5/7/9、S-5/6。
予算 E11-2 は b2_sizes.json の実測で確定。閉じた行と証拠は [閉鎖台帳](../../archive/v3/DEFERRED_CLOSED.md)。
証拠: `~/os32-tmp/evidence/2026-10-07/accept_e11/RESULT.md` と同ディレクトリのログ・画面・PT0/marker。
> 記録は archive へ移した (2026-10-07): [優先段・KAPI-AUDIT-FIX・e11c1/c3](../../archive/v3/TASK_T2D_T2H_RECORDS.md#land-r217)、[e11b1/c2](../../archive/v3/TASK_T2D_T2H_RECORDS.md#land-r678)。

### e11 の分担と切替順

- a=内部結線 (旧USER下は準備確認)、b=低位USER/旧例外撤去・SHM/V86契約・拒否、c=正規lease対照・公開KAPI/caller/SDK/生成/版/manifest。**順序は b1 → c → b2** (aの後。b1は直接consumerの無いTVRAM/font/VRAM例外、b2はUnicode/BB/共有PT)。a/b/cは独立公開・配備せず、**版の更新は統合でだけ行う** (b1のTVRAM wrap本体は宣言・slot・版不変で再生成可)。統合で§2-4全6項を同時成立、現行対象再ビルド・配備を一括。受入は§2-5末尾のNP21/W一式とe12の変異一覧による。apps/gameは§5/§8へ。E11-5は2026-10-03決定の「全画面中のowner 1または専用のKAPI」、追加ならcで版一括へ含める。
> 記録は archive へ移した (2026-10-07): [e11b2・e11a2](../../archive/v3/TASK_T2D_T2H_RECORDS.md#land-r231)。

- **KAPI-CALLBACK**: 未完了は[台帳の優先段](../DEFERRED_TESTS.md#関門-新機能より先-優先段)。公開KAPIの形と版を変えずCPL0実行を塞ぎ、**e11より先に配備・受入する**。hsync・install・filerを壊す単純拒否は不可。候補はkernel生成trampolineのslot 12 stubをCPL3 shimへ替え、kernel内部の列挙口で項目を写しcallbackをCPL3で呼ぶ。CPL0不実行・既存callerの列挙正常・`man -l` crash解消を受入。値返し列挙KAPI追加とcaller移行はe11c。
- **KAPI-OWNER**: 台帳の優先段でSHM lock/free・pipe free/clear/get_buf・DB slotをwrapで所有者照合し、既存の-1で他owner操作を無変更拒否。本人成功・trusted回収も先行受入。`pipe_get_buf`がkernel番地を返す件の意味変更はE11-8のe11c。
- **KAPI-DISK-AUTH**: 台帳の優先段でkernelが知るexec経路・`/sys`由来・CUI前景の識別を授権に使い、拒否＋許可リストを先行配備。無授権I/Oゼロと隔離媒体での正規caller `inst_hdd` (CPL3)・常駐シェル`cmd_hdprep`を受入。OS32Xヘッダへflagを足す方式はE11-4のe11c。
- **KAPI-AUDIT-FIX**: 台帳の優先段で`console_set_cursor`の後の`cursor_x++`の符号付きあふれをclampで防ぎ、FM/SSGのch無検査、`fm_play_mml`/`serial_getchar`のCTRL+STOP不能DoS、`wrap_rshell_set_active`の無授権を先行修正・配備・受入。残りの未監査区分 (pipe/redirect/host_*、exec_*/launch_*/appslot、ime_*、gui_call/register、con_sink) はe11a着手前に分類し、到達可能な穴は新機能より先に修正。公開意味変更・範囲検査P3変異はE11-8へ。

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
| e9 | tvdumpとSHMマーカー・観測側。wire不変、fault目的地一致、lock→free/exit→2本目アプリのSHM書込み<br>申し送り (2026-10-02): `nop`・`ring3_hello/fault/guard` のマーカーを SHM へ移し、`db_v50_test` は実 RAM 境界の成功へ移す。未貸与 VRAM 拒否は e11 の USER 撤去後 (PM 決定)。 |
| e10a | shm_init後のboot口・SHM権限口・起動時map撤去・汎用昇格禁止。lock/free/回収のUSER維持<br>申し送り (2026-10-02、e11 で統合): kselftest `test_map_user_keep` と paging.c `paging_map_user_keep_selftest` を共有PT昇格禁止に合わせて改廃し、`paging_bounds_host.c` の `paging_addrspace_map_user_keep(...) == 0` の呼出しは拒否期待へ反転する。`paging_map_user_keep_selftest() == 0` の呼出しは成功期待を維持し、selftest の中身を共有PT昇格禁止に合わせて書き直す。 |
| e10b | V86 sessionと全出口復元。VRAM/Eのsetup/teardown PCDはe3へ前倒し済み (PM判断、初回起動/V86でUCを失わないため)。PDE USER、失敗/STOP出口を確認 |
| e10c | master/AS/post-exec毎bootの3段検査。kselftest (c)とV86帰路でalias_cache一致、DISPLAY/TVRAM再取得 |
| e11 | 全切替・世代/生成/manifest確認 (前小段を統合、単独で部分配備しない)<br>申し送り (2026-10-02): `test_app_bb_overlap.py` は旧直接BB公開の撤去時に退役するか lease CLIENT 試験へ作り直す。e10a の map_user_keep 試験更新も統合する。 |
| e12 | 変異対応表 [T2E_MUTANTS](T2E_MUTANTS.md)・§6-1 実測・構成別 guest_acceptance を確定。X-12/X-13/X-10 と X-2 前半を閉鎖、ピークを kernel 専用化。検査証拠は `~/os32-tmp/run/e12/report.md`、構成 / PF 詳細 / ピーク再測定は H-4/H-8/H-9/E11-BUD 等で PM 受入 |

実ソース `test_lease.py` / `test_gfx_boot.py` とSDK呼出しを連結し、backendを選ぶだけの模型で終えない。変異は旧bb pointer、plane stride丸め、片実体だけ再attach、GUI DISPLAY許可、UC落ち、共有PT書込み許可、revoke前free、V86後PDE USER復元欠落、teardownのPCD欠落、SHM lock/free/回収後USER欠落、束generation照合削除、fontのboot終了ガード除去。既存10 lease変異の意図も保持。

NP21/Wは8MB planar/PEGC、17MB planar/PEGC/Cirrus。日本語/描画/present、GUI→CUI→GUI、全画面DISPLAY、通常GUIの低位VRAMとdevice直書きkill、S/T/Uと片側revoke、cirrus-off強制指定fallback、V86復元後のalias_cache一致とDISPLAY/TVRAM再lease、SHMの2本目書込み、boot後font_load_testのNOSYS/表・BB不変を確認。ring3_guard旧Eの「低位BB生存」はここから**拒否へ更新**し、正規CLIENT leaseで生存する対照を追加。Bはshlib実ロード後にPTE P/U/ROかつPF error=7、Aは実stack直下NPかつerror=6を確認する。Ra266のPEGC/日本語/全画面とUCはhへ。予算は§6のe枠。

> 記録は archive へ移した (2026-10-07): [e11a1・e10c・e10b・e9](../../archive/v3/TASK_T2D_T2H_RECORDS.md#land-r274)。
未確認: B は shlib を読み込まずに走らせた (RO の error 7 は未証明、台帳 H-9)、sh.bin 経由の tvdump (台帳 E9-1)。証拠は `~/os32-tmp/evidence/2026-10-06/accept_e9/`。

> **記録は archive へ移した (2026-10-06)**: T2e — e1〜e10a、KAPI の範囲検査・NULL 検査 (kapinull) の実装結果と受入 — [TASK_T2D_T2H_RECORDS.md の「元の行 219–1889」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r219)。

## 3. T2f — map/unmapとallocator、暫定heap終了

### 3-1. 着手条件・範囲

eの隔離・両gfx実体・B1が合格。新 `exec/appmem.[ch]`、paging、exec/appslot/exec_heap、SDK CRT/allocator、Rust allocator、KAPI生成、mem表示と試験を対象にする。汎用KHEAPを伸長しない。resident shellの452KiB exec_heapと固定sbrkを維持し、T3の256KiB分割を先取りしない。trim通知配送はg、fはtrimの安全な返却部品まで。

### 3-2. extentとmap transaction

ASに32本の `{u32 base,end,kind,flags}` (16B)を固定追加。空slotはbase=end=0、管理はkernelのみ。kindはLIBC_INITIAL / EXEC_INITIAL / ANON / EXEC_ARENA / EXEC_LARGE等の内部識別、公開引数にしない。公開mapはANON。隣接同kind/flagsは併合するが、EXEC_LARGEの割当識別は保持する。exec_heap が空でも予約起点 MEM_EXEC_HEAP_BASE をまたいで併合しない (F-2)。PFNの正典はPTE。公開unmap対象kindは**ANONとLIBC_INITIALだけ**。EXEC_INITIAL / EXEC_ARENA / EXEC_LARGEは全て拒否し、返却はkernelのowner検証付き内部口だけ。image/stack/shlibも公開unmap対象に含めない。
libc初期量のうちBSSと同居する端pageはimage所属のまま、独立したheap pageだけをLIBC_INITIALとして返却可能にする。

map(bytes,hint,flags)は0、EXACT、TOPDOWN、EXACT|TOPDOWNを許可 (最後はEXACT優先)。NULL hintは希望なし、EXACT+NULLは拒否。非NULL hintはpage整列かつアプリ私有利用帯内、bytesは加算前overflow検査して切上げ。未知flag/size0/帯外は拒否。flags=0は `[page_align(image+BSS end), 0x88000000)` (exec_heap予約起点未満)の**上端側から下向き**に穴を探し、primary _sbrk直上を先に塞がない。TOPDOWNはstack guard直下からexec_heapの現在端より上の穴を下へ探す。hintが有効で空なら先に採用、EXACTの衝突は別穴へ逃がさない。hint が image・shlib・lease・stack/guard・現在の exec_heap にかかる場合は INVAL で拒否し、別穴へ逃がさない (F-2)。image/BSS端page、
初期heap、stack/guard、shlib、leaseを除外し、低位/未使用256MB超へ広げない。exec_heap伸長も現在端のEXACTが第一候補、衝突時は別arena。予約はVAの排他境界で物理先取りを意味しない。

順序は引数/呼出元→穴→併合後slot数→必要PT→全data page確保/ゼロ→公開。pendingのPTは最大64本の控え (256B)を**非再入map transactionのkernel stack上**に置く。ASにもAppSlotにも常駐させず、呼出stack high-waterの予算/実測へ含める。dataは予約した空PTEにPFNを**PRESENTなし**で記録し、失敗時の返却リストとする。live PTへ置く場合もPRESENTなし・AS更新中で、callback/AS切替なし。既存PTE/未使用slotの初期値を前提にして撤去範囲を確定、既存の有効entryを上書きしない。巨大PFN配列・追加rollbackページを確保しない。失敗はpending PFN全返却→今回PT全返却→予約slot解放で元通り。
公開時だけPRESENT/USER/RWとextent/heap端を一括確定、active CR3再ロード。IRQ禁止は公開の短い区間、確保/ゼロ化では解除する。IRQ/例外からallocatorを呼ばないR1を維持。

unmapはbytes page倍数、base整列、overflow/全範囲のextent種別 (公開口はANON/LIBC_INITIALのみ)/連続被覆/PTE ownerを先に検査。内部exec_heap返却口は別でEXEC_*と所有者を検証する。一つの穴・image・stack・shlib・leaseを含めば全拒否。中抜きは最大2残片を計算してslotを事前確保、足りなければFULLで全不変。複数extent跨ぎは全て同じ検査を通す。全対象NP→active CR3 reload (非activeは次回load保証)→PFN owner free→空APP PTをPDE NP/TLB同期後返却→extent残片確定。先頭lease PTは触らない。freeに必要なPFNはNP PTEのframeに保持し返却後ゼロにする。
owner不整合は検査段で拒否/診断し、途中まで返して成功しない。検査後の PFN/PT free が失敗した AS は毒状態とし、USER 復帰前に kill。teardown はその owner を再返却/reclaim/retire せず、残りページ数を exec_as_leftover_pages に数えシリアルへ 1 行出す。kstop にはしない (f4 P3-3)。

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

Rust (`sdk/rust/os32api/src/lib.rs:149`)は**newlibをリンクせずmem_allocに整列adapterを被せる**。CPL=3 Rustアプリ・libos32gui.shlib・CPL=0 resident gshellで同じGlobalAllocを使う。Layoutのsize/2冪alignを検査し、実効align=max(Layout.align, prefixの整列)として `size + 実効align - 1 + prefix` のoverflowを検査してraw blockを確保し、範囲内で整列したpointerと元baseをprefixに保持、deallocは元baseをmem_freeへ渡す。reallocは新確保→copy→旧free、失敗は旧内容保持。prefixも整列させる。
65536のkernel側分類はmem_allocに渡る実byte数で、Rustの余白込みなら閾値を跨ぐことを試験で明示する。C nanoは要求サイズで分類する。size0はRustの既存契約を守る。

resident/USERの振り分けは**kernelが保存したcaller由来**で行い、ビルドfeatureや現在CR3/g_cur_appで推測しない。USER由来のapp/shlibは当該ASのexec_heap、WM/TRUSTED由来のgshell/shlib呼出しはresident heap。USER syscall内にWMが入っていてもresident側に割り当てる。freeも由来/所有者を検証し、両heapを跨いで渡さない。Layout.alignの既存不具合候補はこの実接続上で4/8/16/64/4096整列・overflow・失敗・解放と3種類の利用者を試す。Rust trimはnanoを呼ばずTRIM_DONE帰路のexec_heap trimを使い、任意cache hookとbusyは同じ通知規約に従う。

nanoのtail trimは実free list上で末尾のfree chunkを確認し、header/次参照に必要なpageを保持する。リンク/size/breakの更新案を控え→末尾の完全なpageだけunmap→成功確定、失敗で旧状態。空の副arenaは全返却。trimはallocator busy中に呼ばず、別arenaの生存chunkを読まない/動かさない。接続試験は実nanoをリンクし、malloc→穴→整列追加→EXACT失敗→別arena→realloc→trim→再割当を通す。

既存mem_alloc/exec_heapはkernel側のAS別arena管理で同じappmem実体を使い、64KiB未満はKHeapのarena、以上はTOPDOWN。resident用の単一KHeapを維持し、選択は上記caller由来で固定する。USER arenaのbase/sizeはextentから導出し、usedは検証済み走査で算出する一時KHeap viewを使う (32本分のKHeapをASへ重複保持しない)。集計値と現在arena索引だけをAS制御へ置く。アプリのBlkHdrは改竄可能なので、kernelが辿る前に所属extent、alignment、size加算、次ポインタ/ブロック終端の単調性とPTE/ownerを検査。破損時はそのappの明示失敗/通常回収へ、他ownerへfreeしない。負例は二つに分ける。
公開unmapのEXEC_*指定は全拒否/metadata不変。USERが書けるBlkHdrの直接改竄は次のalloc/free/trimで検出し、他ownerを変えない。汎用kheapの信頼済みkernel顧客へこの検査を一律に広げず、USER arena用の入口で行う。exec_heap trimは同じ安全検査後の末尾freeページのみ、kernelがユーザー申告の「free量」を信用して返却しない。

### 3-5. 分割・試験・受入

| 小段 (各45〜75分) | 成果 / 閉じる試験 |
|---|---|
| f1a | toolchainの実nano入力/hash・リンク対象と由来/ライセンス/パッチのビルド台帳。実装・対象host検証済み (2026-10-02)、全体検査は下記記録 |
| f1b | adapter最小接続。整列追加/末尾拡張の実ソース対照、resident/Rust接続の分離 |
| f2 | extent/穴探索/flags。両端/overflow/EXACT/hint衝突、固定32本 |
| f3 | map準備/公開。data各枚・PT各枚不足の全rollback |
| f4 | unmap/部分分割/併合。FULL不変、NP→TLB→free、別owner拒否 |
| f5a | 内部結線 (KAPI 70 不変): AS extent/layout、初期 heap 登録、ANON teardown、毒 AS 隔離、owner 往復 +5。AS 1,224B (表 512 + layout 16 + poison 4)、1,376B/16KiB ASSERT 内。F-2/F-3 の負例はホスト試験へ。fix1: 毒化時に対象の中断要求、resume 前に検査、live 数から隔離。4 TU は -Os。証拠 `/home/hight/os32-tmp/run/f5/fix1_report.md` と `f5a_fix1_sizes.json` (初回は `report.md` / `f5a_sizes.json`)。実 kill・high-water は台帳 F-6。 |
| f5b | USER 専用 mem_map/mem_unmap (slot 246/247)、KAPI 71・memory 2。内部の検査とエラー翻訳に接続、公開 flags を静的照合。F-1 閉鎖。map/write/unmap の対照と同じ VA の PF 試験を登録、ゲスト受入は F-6。P3 の poison 経路検査と master 文脈注記を追加。証拠 `/home/hight/os32-tmp/run/f5/f5b_report.md`・`f5b_sizes.json`。 |
| f6 | USER/resident CRT、_sbrk EXACT/負増分とmapped_end |
| f7 | nano副arena/通常malloc・reentrant入口。非連続成功と連続性保持 |
| f8 | 大塊/calloc/realloc/整列とRust結線。65535/65536/65537の3値 |
| f9 | exec_heap小arenaと親保存/復元。公開EXEC_* unmap拒否と改竄header検出で他owner不変 |
| f10 | exec_heap大塊と安全なtrim。空末尾/空arena/生存データ保持 |
| f11 | nano trim。失敗rollback、再割当の実ソース試験 |
| f12 | 起動予算・旧helper撤去・最小初期量/世代の一括切替<br>申し送り (2026-10-02): `test_sbrk_tier.py` を丸ごと削除し、`app_band_pde_host.c` の legacy byte budget、`memory_boot_host.c` の `MEM_EXEC_SBRK_MIN` 式、kselftest `test_pool_model` の `pool:exec range`、`heap_test` を新予算/heap契約へ更新する。 |
| f13 | mem表示・変異結線・size/manifestとPM台本を確定 |

> **記録は archive へ移した (2026-10-06)**: T2f — f1a〜f4 の実装記録 — [TASK_T2D_T2H_RECORDS.md の「元の行 1951–2370」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r1951)。

## 4. T2g — 池不足時の非同期trim

### 4-1. 着手条件・範囲

fのmap失敗理由・allocator busy/trim・STOP全返却がGREEN。appslot/appmem、exec安全点、GUI共有protoとRust写し、SDK C/Rustイベントループ、gshell handler/multiappを対象にする。新しいretry KAPI、timerで常時trim、CUIの裏実行、同期WM pump、P6の一般scheduler/x87は追加しない。

### 4-2. 状態と配送契約

各AppSlotに固定 `{pending_epoch, delivered_epoch}`、SDKにbusy/in_trimとlast_epoch、カーネルにu32 pressure_epochを置く (0=無し)。32bit満了はsaturateさせ、epoch一致だけで新要求を無視せずpending bitで管理する。動的queueは作らない。slot回収/再利用でpending/deliveredをゼロ、古いイベントを次のownerへ渡さない。

appmemは**data/PTの物理池不足**で準備を全巻戻しした後だけ、要求者以外の生きたback GUI slotにbitをORしてNULLを返す。VAの穴不足・extent満杯・KHEAP制御不足・引数不正は要求しない。front/backはWMが既存focus遷移の安全点でkernelへ伝える固定状態で判定し、USER申告を信用しない。その状態が未確定なら配送を遅らせる。kernelのepochは通知の合成であり再試行保証ではない。

WM top-levelの既存park/resume配送でpendingを見て、固定イベント枠にtrim通知を出す。通常queue満杯ならbitを残して後送、既存イベントを捨てない。gshell multiappはこれをready理由にし、要求者Aが安全なyieldへ入った時点のtrim pending slot集合を固定し、**要求者のyield復帰より先に各slotを1回ずつresume**する。GUI_OP_WAITのtimeoutでAが即readyでもこの一巡を先に処理する。1 slotの試行は既存の安全なresume→park/終了までで、応答待ちを追加しない。busy/queue満杯/未応答ならbitを残して1巡で打切り、TRIM_DONEを待ち続けない。USER loopから戻らないslotの停止は既存STOP/P6境界で、
今回新たな強制preempt保証は作らない。更新途中のwrapperやallocatorから起こさない。CUI/終了ASへは配送しない。SDK受信時はbusyなら保留し、allocatorを抜けた安全点でtrim→任意cache hookを1回→完了記録。GUI protoに内部のTRIM_DONE op (epochを値で渡す)を設け、saved callerの保留要求と一致する場合だけ受理する。この帰路でkernel側exec_heapの安全な末尾trimを同じcaller ASのまま実行し、他ASへ切り替えない。新しいメモリKAPIは増やさない。hook中はin_trim、同一要求の再配送やhookのmalloc失敗で再帰trimしない。SHMのUSER可変完了値はスケジューリング上のhintだけで、pool freeは台帳から測る。

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

manifestはkernel/loader/SDK/CRT/libs/shell/gshell/shlib/sh.bin/testsのhash/4世代/ビルドIDを固定。**apps/gameはv3でビルド対象外** ([ユーザー決定2026-09-30](../../archive/agents/HANDOVER_2026-09-30.md) §1/§3)。hの成果は外部callerの追随一覧 (lease/query/世代、低位直読撤去、heap/整列、再attach/trim、manifest)と、再開時の再構築・監査・guest受入ゲート。状態はPASSやh未完ではなく**決定による持越し**。外部集合の完全移行を実証したとは記さない。旧shellを起動して更新する手順にはしない [D1][D2][V1][V2]。
媒体異常のあるNHDを無断修復せず、PMが健全な試験媒体を用意する (別件は§8)。

> **記録は archive へ移した (2026-10-06)**: T2h — h2 の準備記録 — [TASK_T2D_T2H_RECORDS.md の「元の行 2416–2584」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r2416)。

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

park→resume台本は**h3のコーダーがe9と同じSHM観測形式で実装**し、PM/テスターが実行する。試験GUI 2本を立上げ→各々が自分のSHM blockに `{magic, owner, generation, phase, mode}` を初期化→OP_WAITでpark確認→新kernel.map/診断から当該blockの物理番地を引く→MCP `emu_write_mem`で固定mode (pf/gp/de/ud/USER-loop/KAPI-loop)とarmだけを書込む→resume印とswitch増分を待つ→当該mode発火を観測→owner回収/次起動を確認、を1ケースとする。armはresume後に1回だけ消費。timerはarmed以後の猶予にだけ使い、park前に発火させない。
別owner/古いgenerationなら台本は中止し、キー注入を代用にしない。試験専用のSHM制御であり任意書込口は製品に追加しない。

**h3準備・PM決定(A)と実装記録 (2026-10-02、Codex gpt-6-astra、基点 `6a8aba9`)**:
PM決定(A)を採用。初期化時だけ、ホストが SHM 所有アプリ ID → appslot の AS →
台帳 AS owner / AS generation を照合して owner、generation の順に書く。
GUI slot を owner、tick を generation とする代用はない。以後は mode / arm のみ。
e9 で本人識別を渡す正式な経路ができたら、h3 の初期化もそちらへ切り替える。
> **記録は archive へ移した (2026-10-06)**: T2h — h3 の実装記録 — [TASK_T2D_T2H_RECORDS.md の「元の行 2606–2622」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r2606)。

#### e9 と共有する観測形式

正典は試験専用
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

> **記録は archive へ移した (2026-10-06)**: T2h — h3 台本の修正 (h3fix〜h3fix4) と確認の記録 — [TASK_T2D_T2H_RECORDS.md の「元の行 2642–3122」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r2642)。

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

> **記録は archive へ移した (2026-10-06)**: §5-4 GUI KAPI-loop の STOP — 実装・ホスト検証の記録 — [TASK_T2D_T2H_RECORDS.md の「元の行 3216–3346」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r3216)。

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
> **記録は archive へ移した (2026-10-06)**: §5-4 GUI KAPI-loop の STOP — 着地とゲスト受入の記録 — [TASK_T2D_T2H_RECORDS.md の「元の行 3361–3369」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r3361)。

## 6. 予算・ホスト試験の重さ

### 6-1. 予算ゲート (e12 実測、2026-10-07)

同一 cross toolchain で `make kernel`、`build/out/kernel.map` の `__bss_end` と
`ls -l build/out/vmkernel.lz4` から取り直した。ログ・JSON は `~/os32-tmp/run/e12/`。
d6 の `__bss_end=0x18C270` が e の増分の基準。KHEAP は E11-BUD の一時措置で
192KiB → 176KiB、画像上限は **0x199000** (旧 0x195000 から +16,384B)。

| 段 / 基点 | 実測 / 計画枠 (text/data/BSS/整列込み) | ASSERT 残り |
| --- | ---: | ---: |
| d6 確定 | `__bss_end=0x18C270`、d 正味 4,260B | 旧上限で 36,240B |
| e11 / e12 修正前 (`83f2db7`) | `__bss_end=0x1936E0`、e 正味 **29,808B** | **22,816B** |
| e12 ピーク修正後 | `__bss_end=0x193740`、e 正味 **29,904B** (+96B) | **22,720B** |
| f (**ユーザー決定 2026-10-08: 予備から +4KB → 12,288B**) | 12,288B (f5a 実測 6,400B、-Os 込み、残り 5,888B) | 10,432B |
| g | 3,072B | 7,360B |
| h (selftest/診断追加) | 3,072B | **4,288B** (予備) |

e の 16,384B 枠は修正前で **13,424B 超過**、修正後で **13,520B 超過**。
一時 +16KiB で吸収しているため、枠内の合格とはしない。修正前の f/g/h 14,336B
控除後は **8,480B**、修正後は **8,384B**。KHEAP を 192KiB に戻すと、
修正前 **6,432B** / 修正後 **6,336B** しか残らず f/g/h の枠に足りない。
E11-BUD の kernel 専用ピークを統合ゲストで測り直してから復元と再配分を決める。
**f5a の後 (2026-10-08)**: f5a だけで f 枠 8,192B のうち 6,400B (appmem 4 TU は -Os) を使ったので、ユーザー決定で予備から f に +4KB (12,288B)。予備は約 4.3KB、KHEAP の一時増枠は維持。f の各段で実測し、枠を超えたら止める。
ASSERT・診断削除・T3前倒しで超過を隠さず、T3 着手前の再見積もりゲートを維持する。

圧縮 `vmkernel.lz4` は修正前 **494,395B** / 修正後 **494,417B**、
520,192B 上限まで **25,797B / 25,775B**。build_id/CRC により既存記録の値と
異なるので今回の成果物を採用する。圧縮の余白と未圧縮の枠は交換できない。
SDK/shlib はこの kernel 表の外で、8MB/私有量/画像へ別計上する。
BSS 前余白を二重加算せず、撤去の減少は先取りしない。

pending PT控え256Bは§3-2のkernel stackに置く。ASへ足すと1,456Bで1,376B上限を越すため禁止。AS688B + extent512B = 1,200B、1,376B上限まで176Bで制御/arena索引を賄う。arena毎の巨大PFN控えは置かない。AppSlot追加はtrim/caller寿命等をtarget sizeofで計上し、全6slot+最大4通常AS+SURFACE/台帳の**16KiB**を維持。KHEAP192KiBの他顧客込みピーク/起動拒否/返却も測る。KHEAPの制御を増やした分はBSS節だけでは見えない。PD/PT、SURFACE backing、PFN metadataは別物理勘定。32extent制限によるENOMEM/FULLを隠すために表を動的増設しない。

d の計画枠はユーザー決定 (2026-10-02) で 3,072B → 5,888B。
d6 は正味 4,260B、未消費 1,628B。旧「全枠消費後の余白 5,520B」は e 未実測の
履歴値なので現行予算には使わない。d6 の詳細は §10-15、現行は上表を参照する。

**T3 前の文書の再整備計画 (ユーザー指示 2026-10-03)**: T2h の受入の後、T3 の実装に入る前に文書の再整備計画 (完了した T2 の節のアーカイブ・INDEX.md 冒頭の正典表の見直し・重複と古い記述の洗い出し・担当) を立ててユーザーに提示する。提示までは T3 に着手しない。

### 6-2. 試験実装の規約

各小段に正常対照と1つの目的別負例を先に用意し、実ソースの失敗→修正→成功を記録。MMU/IRQ/I/Oだけをホスト足場にし、kernelの判定やallocatorを模型に複製しない。実CR3/TLB・デバイス・描画の合格はguestで補う。新試験は `tools/check_map.yaml`、該当build検査、生成 [TESTS](../../TESTS.md)へ実装時に登録する。今回は実在しない試験へのリンクや生成一覧を作らない。

変異は写しの対象ソース/fixtureだけを1回読み込んだcacheから作る。1 mutantごとにリポジトリ全木コピー、rg全走査、make check、全manifest再生成をしない。依存closureを固定し、正常objectは再利用、変異TUだけ再コンパイル/再リンク。入力hashは一組の開始/終了で確認する。`mutpar.py`の固定並列度を使い、別の全体検査と重ねない。

1変異の目安はcompile+runで30秒以内、timeoutは誤り検出用でGREEN/REDに数えない。初回に最重と中央値を計測し、60秒を越す変異は対象fixture/依存を絞ってから全本数へ展開する (実機待ちを縮める指示ではない)。生き残り、compile/link error、timeout、意図したruntime REDを別集計し、理由assertを必須にする。リンクASSERT/生成拒否は別分類の正当な拒否で、runtime RED本数へ混ぜない。

e12 の変異名・検出条件・分類別集計の正典は
[T2e 変異対応表](T2E_MUTANTS.md)。既存 12 種類と lease 10 種類を保持する。

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

> **記録は archive へ移した (2026-10-06)**: §9 文書検査・§10 独立レビュー所見の対応 — [TASK_T2D_T2H_RECORDS.md の「元の行 3437–3481」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r3437)。

## 10-2〜10-16. T2d (d0a〜d6) の実装結果と受入 — アーカイブへ移した

2026-10-02 に T2d の完了記録 (d0a〜d6 の実装結果・レビュー対応・ゲスト受入、§10-2〜§10-16) を [archive/v3/TASK_T2D_RESULTS.md](../../archive/v3/TASK_T2D_RESULTS.md) へそのまま移した。この票の中の「§10-2」〜「§10-16」はそのファイルの同じ節番号を指す。

## 11. 独立レビュー 2 回目 (Opus 5.5、Approve) の P3 — 実装時の注記

2026-10-01、`3180a51` の差分に対して Approve (P1 2 件・P2 11 件はすべて閉)。以下の 5 件は設計の変更ではなく、実装時に従う注記 (PM 記入)。

1. **d0b の IRQ 保存区間**: redir のバッファは最大 `PIPE_BUF_SIZE` = 64KB (`fs/pipe_buffer.h:16`)。登録者 AS の回収は通常文脈でしか起きない (R1) ので、**ページ単位**で「生存確認 → walk → copy」を IRQ 保存し、ページの間で IF を戻す (buf_len はページ単位で進める)。§1-2 の「無制限サイズを割込み禁止でコピーしない」と同じ意味。
2. **§2-3 の PCD 範囲**: `v86_ident_map` の `[0xA0000,0xC0000)` には CG 窓など SURFACE 外も入る。PCD を付けるのは**台帳の native VRAM 区間と一致する範囲**だけ (paging_init で UC にする範囲と同じ)。区間外は現状どおり。
3. **ext2 調査票 x2/x3**: 保全コピーは `nhd-pull` で取らない (作業イメージ `build/nhd/os32.nhd` を上書きする)。停止中に `/home/hight/os32-tmp/` へ複製し原本を上書きしない。x3 で別の像から起動するための ini の HDD 指定の変更は、原本を保持する経路 (`np21w_ini_live.py` の live-apply / restore、または停止中にバイト単位で控えて戻す) で行う。
4. **§4-2 の front/back 通知**: gshell は CPL=0 なので、フォーカス遷移の安全点からの固定状態の通知は**カーネル内部の呼出し**を第一候補とし、KAPI を足す場合は [ABI1]〜[ABI3] (版の更新・clean → all) に従う。g1/g2 で決めて票に書く。
5. **予算の実測ゲート**: §6-1 の e12 実測で f/g/h の全枠を使うと残りは 8,384B (KHEAP 176KiB の一時増枠込み)。T3 にもカーネル側の増分 (Unicode の組表など) があるので、**T3 の着手前に実測で残りを再計算する**ことを §6-1 の実測ゲートに含める。

## 12. 受入の構成の方針 (ユーザー指示 2026-10-02)

「ハードウェア構成の変更でのテストは行わない。切りの良いところで一括」「(RAM 量・音源の) 二点は最後に確認すれば良い」。

- **d〜g の各段の NP21/W 受入は、今の構成 (17MB、今の `np21x64w.ini`) だけで行う。** ini を切り替えない。
- 本票の各段にある **8MB、planar / PEGC / Cirrus の切替、音源 (PC-9801-118 の PCM) など構成を変える確認は、受入記録に「構成依存は一括確認へ持越し」と書いて溜め、T2h の統合受入でまとめて行う** (§5 の h1〜h4 に加える)。Ra266 64MB も T2h。
- ゲスト側の設定 (`gfxmode` など) で済む確認は、構成の変更に当たらない — 段の受入で行ってよい。
  - V86 の出口は 6Ah の標準/拡張 (20h/21h) も戻さない — 9821 でゲストが拡張モードのまま戻ると A6h が効かず E0000h が MMIO のまま ([U] 00A6h)。未対処・未観測。
> **記録は archive へ移した (2026-10-06)**: §12 の P3 対応の記録、§13 検査の仕組みの整理 (ci-stab / ci-select / ci-stab2) — [TASK_T2D_T2H_RECORDS.md の「元の行 3504–4013」](../../archive/v3/TASK_T2D_T2H_RECORDS.md#r3504)。
