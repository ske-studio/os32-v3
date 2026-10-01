# TASK_T2D_T2H — T2d〜T2h 詳細設計

> 状態: **設計中 (2026-10-01)** — 実装前。Opus 5.5のRequest changes (P1 2件 / P2 11件 / P3 8件)を反映、差分の独立再レビュー待ち。
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

cstrはcapがNUL込み。cap0/NULLを拒否し、整数番地で加算前にoverflowを確認して、**その1 byteの権限確認→読取→NUL判定**。page末NULの次pageは調べも読まない。失敗時dstは未完成stagingであり使用禁止 (dst不変までは保証しない)。trustedでも容量・NUL検査を行う。copyoutはlen0で書込みなし、NULL+非0拒否。srcはkernelで確定済みの非重複staging、ユーザー同士のmemmoveには使わない。

**今渡されたポインタ**のUSER walkは保存PD=現在CR3、現slotのAS/owner/generation一致を要求。**登録済みポインタ**は別契約で、登録時に `{app_id, AS, pd_phys, owner, generation, origin}` をkernelのredir記録へ値保存し、呼出フレームへのpointerは保持しない。generationはAS寿命の単調識別子 (slot/owner/PD再利用と区別、周回時は再利用拒否)。redirのnest/park保存・復元にも付随させる。使用時に生存台帳から登録者ASを引き直し、失効したAS pointerをdereferenceする前に同一性を検査する。登録者が死んだ/世代不一致なら失敗し、書き手の子を登録者と取り違えてkillしない。書込みは登録者PDをwalkしたPAへ `P2V(pa)` でpageごとに行う。現在CR3への一致条件は課さず、元のVAへ直接書かない。読取り側も登録者PDからcopyし、今渡された出力bufferとは別に検査する。容量/位置のoverflowとlen<=capacityを確認し、検査失敗ではデータ・位置不変。生存確認→全範囲walk→copyの間は短いIRQ保存区間でAS回収/切替を防ぐ。

両契約とも、PDはAS ownerの生きたPD、APP/lease PTは控えのframeと台帳owner、共有PTはmaster登録済みframeとの一致を先に確認してから読む。presentな任意RAMをPTとみなさない。PDEのPSを拒否、PDE/PTE両方PRESENT|USER、出力は両方RW。返ったPFNも私有RAM・SHM・shlib RO・RAM leaseの管理情報と突き合わせる。一般copyではMMIO/VRAMを拒否し、RAM RO lease/shlib rodataは入力だけ可。trampolineのRO+USERページ内の `ring3_user_str` scratchも、登録済みtrampoline backing/PDE/PTEを照合して**入力に許可**する (早期分類にも追加)。出力は拒否。次の文字列返却で上書きされる既存寿命を越えて保存しない。低位VRAMが暫定USERでもB1の例外にしない。

walkはT2cの低位恒等backingをP2Vで参照し、**dではmaster往復を除去**する。短いirq_save区間で範囲全体を検証してからcopyoutし、通常の検査失敗は出力全byte不変。どの出口も入口IF/CR3不変。複数出力を持つ入口は全出力範囲を先に検査してから書く。allocation/VFS/SQLite/callback/GUI pumpは区間外。boundedなDB/lease構造体用であり、無制限サイズを割込み禁止でコピーする入口を新設しない。

早期 `ring3_ptr_ok` はNULL・高位image/heap/stack/SHMと**現在ASの有効lease**の分類に拡張。穴の最終判定はwalk。dでは旧consumerのため既存低位VRAM分類を暫定維持し、eで消す。既存出力ガードはRW検査を弱めず新walkへ集約する。`_always` の保存済みapp pointer経路をtrusted扱いへ変えない。

DB接続は `kapi_db.c:512` / `:974` / `:1116` の3呼出箇所 (db_open / db_open_existing / db_prepare_only)とその補助だけ。copy失敗では既存rcを返し、SQLite入口カウンタ差分0、旧stmt/FDに副作用なし。B3/B4やFEP全体を安全化したとは報告しない。

### 1-3. 分割・試験・受入

| 小段 (各45〜75分目安) | 成果 / その場で閉じる試験 |
|---|---|
| d0a | 既存fd_redirect候補のゲスト再現確認。`ls \| cat`、親子同VA/別PFN、親buffer/子heapの比較 |
| d0b | 登録者記述子とPA copyを最小実装。死んだ登録者/slot再利用/RO化/入れ子・park保存復元、子の内容不変。修正後guest回帰 |
| d1 | caller記述子と入口/正常出口。USER/trusted/入れ子とCR3不一致拒否 |
| d2 | park/longjmp/WMの寿命配線。実exec R1足場で古い記述子不使用 |
| d3 | read/write walkと管理frame検証。RO入力成功・RW出力・PS/偽PT拒否 |
| d4 | cstr/copyout。page末NUL、次NP、未終端、overflow、IF両値、out不変 |
| d5 | 上記DB3入口と既存出力ガード接続。実wrapper→実copy、SQLiteは入口だけ記録 |
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
| e1 | 公開desc/エラー/授権とquery。偽owner/旧ref/GUI DISPLAY拒否 |
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
