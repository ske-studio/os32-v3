# HSYNC_MANIFEST_DESIGN — hsync の往復を減らす (ホストの木の一覧と CRC で比べる)

> 状態: **現行 (2026-11-16)** — 確定 (ユーザー決定 2026-11-16「往復を減らす」「今どおり飛ばす」、astra 起草 r1・r2、Opus 5.5 の独立レビュー 2 回)。段の状態は票 TASK_HSYNC_MANIFEST が持つ。
> 出所: 往復の版は `~/os32-tmp/run/hsync-manifest/` (DESIGN.r1・r2・final、REVIEW.r1)。計測は `~/os32-tmp/run/hsync-measure-x86/report.md`。

# hsync 木一覧による差分判定 詳細設計 r2
## r1→r2 の変更
- R1: HostFS を直接受け、fd helper と stat 共用変換で ns 変化検出・wire metadata を両立（§3/§7/§8）。
- R2: ユーザー決定「今どおり飛ばす」に従い same_mtime の無読出しを維持（§4/§5/§8）。
- R3: ジョブを1件にし、new で前を破棄、旧 token の取り違えを拒否（§2/§6/§8）。
- R4: 生成予算を500 msへ変更し、生成時間から追加照会回数を算出（§2/§5）。
- R5: 単一ファイルは旧経路、一覧 ERR_FULL は書込み前に一度だけ旧経路へ（§4/§6/§8）。
- R6: LIST と同じ無効名除外を使い、除外件数を一覧ヘッダに追加（§3/§8）。
- R7: magic による実体衝突判別と HostDrv のエラー fallback を明示（§6/§8）。
- R8: metadata 同値試験、変異の正常対照、エミュレータ試験の終了値を追加（§8）。
- R9: 設計基準を origin/main 44f2a83f0 に固定（冒頭）。
- PPC-1〜4: 分割/cursor は§3、1.0秒期限は§2/§5、ゲストCRC時間は§4、型/KAPIは§2に回答。
- 日付: 2026-11-16。対象: x86 SerialFS / raw LAN、PowerPC SFS on link。
- 設計基準: origin/main 44f2a83f0（REVIEW.r1 指定）。成果物は設計のみ。リポジトリの変更・ビルド・配備・preflight は実施しない。
- 決定済み: 木全体の情報を一度取得し、same_mtimeを先に判定、必要時にローカルCRCと比較する。LAN pipeliningは変更しない。
- 「一度」は木の生成が一度という意味。512 B の SFS 応答一つに木全体を詰める意味ではない。

## 1. 実物の確認
| 要約から補正すべき点（4 件） | 実コード・計測の根拠 | 設計への反映 |
|---|---|---|
| STAT 265 は差分なしの場合だけ。変更時は 790 | report.md:25、hsync.c、`compare_files()`・`copy_body()`・`replace_file()` | コピー元比較 STAT と open/コピー再確認を区別する |
| READ 32,316 は比較専用の回数ではない | report.md:24、hsync.c (compare_files())、`copy_body()` | 固定 READ・比較 READ・コピー READ に分ける |
| CRC は未導入ではなくコピー時に既に使う | hsync.c、`copy_body()`・`verify_readback()` | 共通 CRC 核とコピー後検証を再利用する |
| ホスト 3 系統は独立した FS 実装ではない | lanfs_host.py、rshell_serial.py、host_sfs_link.py | serialfs_host.HostFS への一度の結線で全経路に適用する |
- 上表の hsync.c は userland/system/hsync.c、Python は tools/ 配下。
- report.md は /home/hight/os32-tmp/run/hsync-measure-x86/report.md。実測値とコードの直接矛盾は認めない。
- big_changed は 260 件中 same_content=255。全件の本文が変わった試験ではない（report.md:8–11）。
- compare_files は 32 KiB 要求と末尾 1 B 確認。SFS はその内部で小分けする（hsync.c）。
- SerialFS READ は 508 B、LAN/link は 1024 B（fs/serialfs/serialfs.c）。
- open 用 get_file_size も STAT を呼ぶ（同:181）。明示的 sys_stat を消すだけでは STAT=0 にならない。
- LIST の各項目には日時がない（fs/serialfs/sfs_proto.h）。既存 LIST の再利用だけでは不足。
- lib/crc32.c (crc32_calc()) は共通核の公開窓口。hsync は既に同じ核を直接組み込む。
- docs/design/fs/serialfs/sfs_block.md「不変条件」は、/host のファイルをパス+offset で要求し遠隔 file handle を持たないと規定。
- docs/decisions/network/LAN_OPERATOR_DESIGN.md は共通 backend と CALL ごとの 1000 ms 期限を規定。

## 2. ゲストからの口
| 候補 | 採否 | 理由 |
|---|---|---|
| 新 SFS op + VFS/KAPI | 不採用 | 新しい利用口・ABI 版・カーネル変更が必要になる |
| 仮想ファイルを既存 STAT/READ で読む | 採用 | 既存 open/read/close と LE codec で両 CPU・全経路を通せる |
- 仮想名前空間は `<dir>/.os32-manifest/`。実ディスクに生成ファイルを書かない。
- `new` の STAT は常に通常ファイル・32 B・mtime=0。木の生成を開始しない。
- `new` の offset=0 READ がジョブを作り、32 B の記述子を返す。同一 SFS 要求の再送は既存応答 cache で同じ結果。
- 記述子は token を持つ。`status/<token>` は 32 B、`data/<token>` は完成後の不変一覧を返す。
- ゲストは new を一度 open/read/close。BUSY なら status を open/read/close し、READY まで待つ。
- READY 後 data を open して全量取得。STAT のサイズと記述子の bytes が一致することを確認する。
- 仮想パスが既存 SFS パス上限を越す深い開始点は、取得前に legacy を選ぶ（新しいパス上限を増やさない）。
- 一覧生成は各 new/status READ 内で500 msを目安に進める iterator。1回64 KiB以下の読み単位の前後で予算判定し、続きの走査位置とCRC状態を保持する。
- PPC-2: tools/host_agent.py の1.0秒期限の約半分を生成予算とし、残りを受付/転送に残す。全木を1 CALL中に計算する必要はない。BUSY は正常記述子、ゲストの生成待機上限は合計60秒。
- 各 READ は短時間で返す。低速ホストの単一 OS read が期限を越える場合は IO/TIMEOUT で終了し、成功扱いしない。
- READY 前は data の STAT/READ を ERR_IO。失敗ジョブは status=ERROR と OS32_ERR_* を記述子で返す。
- token はホスト起動 nonce 64 bit + 単調番号 64 bit、32 桁 hex。認証の用途は持たせない。
- ジョブは HostFS インスタンスに保持し、SFS sid 更新では消さない。期限切れ token は NOTFOUND、同じ token を再生成しない。
- ジョブは1件。new の新規READで前ジョブを破棄して新tokenを割り当てる（同一SFS要求の再送では作り直さない）。最終アクセスから120秒またはHostFS.closeでも回収する。
- 前tokenへのstatus/data STAT/READはNOTFOUNDで停止。同時利用でnewが来ても一覧を混ぜず、ジョブ枠の満杯による待ちを作らない。
- data 読了後も期限までは保持し、再送・offset 読み・STAT に同じ bytes を返す。
- 仮想名は LIST/生成一覧へ混ぜない。既存実体に `.os32-manifest` があればその実体を優先し、加速機能を提供しない。
- 予約名の実体衝突時は旧経路へ戻る。認証・許可リスト・番地制限は追加しない。
- PPC-2/4: host_agent.py は host_sfs_link.SfsLink を経由するだけ。3 adapter は同じHostFSへ同じSTAT/READを渡す。新SFS型・KAPI追加なし、カーネル製品変更なし。KBOOT_LINK=1 の像/coversに新しい口は増えず、既存ppc-link検査で退行を確認する。

## 3. 一覧の形式・生成費用
- バイナリ v1、全整数 LE。C の構造体 cast はせず get16/get32 で明示復号する。
- パスは既存 SFS と同じ bytes。Unicode への往復変換を避け、長さ付きとする。
```text
# descriptor: 32 bytes
0 magic[4]="HMD1"; 4 version:u16=1; 6 state:u16 (0 BUSY,1 READY,2 ERROR)
8 token[16]; 24 bytes:u32; 28 error:i32 (成功/BUSY は 0)
# data header: 24 bytes
0 magic[4]="HMT1"; 4 version:u16=1; 6 header_bytes:u16=24
8 total_bytes:u32; 12 entry_count:u32; 16 records_crc32:u32; 20 skipped_names:u32
# record: 16 + path_len bytes
0 kind:u8 (1=file,2=dir,3=other); 1 flags:u8=0; 2 path_len:u16
4 size:u32; 8 mtime:u32; 12 crc32:u32; 16 path[path_len]
# file: size<=0x7fffffff。dir/other: size=0,crc32=0。mtime=0 は不明。
```
- root 自体は暗黙の dir。相対パス、先頭/末尾 slash なし。空ディレクトリも 1 項目として収録する。
- 親を先に、各ディレクトリの子を byte 順で深さ優先に並べる。親欠落・重複はエラー。
- `other` は通常ファイルとして読まない。既存型エラー/除外判断へ渡す。
- 各dirの名前はHostFS.listと同じ `len(name)>PATH_MAX` またはbackslashを含む規則で飛ばす。飛ばした項目数をskipped_namesへ加算し、ゲストは件数を表示する（dirを飛ばしても内部を走査しない）。
- 残った項目のパス/深さ/名前はhsyncの既存上限に照合。表現不能サイズ等は従来の型/サイズエラーとし、LISTの除外条件を広げない。
- 一覧上限 1 MiB、項目上限 8192。超過は ERROR/ERR_FULL、部分成功・切詰めは禁止。
- BはHostFSインスタンスを直接受ける。metadataは `_stat_raw`/`_kind` と共用mtime関数で作る。mtime関数は `m=int(st.st_mtime); return m if 0<=m<=0xffffffff else 0`。nsの整数除算に置き換えない。
- HostFSに読み専用fd helperを追加し、secureは既存dir_fd/O_NOFOLLOW、非secureは既存resolveの開き方を使う。通常readと生成器で共用し、fdは生成器が完了/失敗/破棄時に閉じる。
- CRC は zlib.crc32 の最終値。ゲストの crc32_core_final と一致し、空ファイルは 0。
- 各ファイルをfd helperで一度開き、読み前後のfstatのsize/mtime_ns/ctime_nsと実読長を照合。列挙時の_stat_rawと開いたfdの同一性も照合し、差替えはSTALE。wireの種別/size/mtimeはstat()と共通変換で作り、nsは変化検出専用とする。
- 生成中の変更は ERROR/STALE。ディレクトリ列挙前後も metadata を照合し、変化検出時は成功一覧を出さない。
- 木全体の原子的 snapshot は保証しない。同期元を更新しない運用が前提。取得後のコピーは §4 の CRC でも確認する。
- ホストの CRC 永続 cache は初版では作らない。mtime/size 同一の書換えを見逃す cache にしない。
- 完成一覧 bytes の cache だけを token 単位で持つ。次の hsync は新しいジョブで全 CRC を計算する。
- この設計作業のホストで 16,237,823 B のメモリを zlib.crc32、20 回平均: 4696 μs（約 0.0047 秒）。
- これはゼロ埋めメモリの計算だけ。ディスク・260 回の open/stat・木の走査を含む実測ではない。
- 計画値: 温 cache 読出し 100 MB/s なら約 0.17 秒+ファイル処理、冷 cache 20 MB/s なら約 0.82 秒+ファイル処理。
- ホスト受入では生成総時間/CRC bytes/ファイル数/BUSY 回数を記録し、上の仮定を実測で置き換える。
- ゲストは取得前に 1 MiB 以下を検査して確保し、全量・record 数・CRC・構造を検証してから宛先を変更する。
- PPC-1: data READは64512 B（1024×63、既存64 KiBバッファ内）をVFSへ要求し、PPC/linkは内部で最大1024 BずつSFS CALLに分割する。輸送境界でrecordが割れても連結後に復号。
- 続きの印は `(data/<token>, offset:u32)`。offsetは既存READ要求のLE32で、成功応答の実bytesを加算。新cursor欄は不要。total_bytes到達で終了し、同じtoken/offsetの再読は同じbytesを返す。
- total_bytes だけ読み、余分な EOF READ を不要にする。短い READ・余剰 record・未知 flags は一覧エラー。
- 256 B程度のREADを項目ごとに発行しない。LAN/linkのdata CALL数はceil(total_bytes/1024)。シリアルは各VFS要求長b_i（最大64512）のΣceil(b_i/508)で、要求境界の端数も数える。

## 4. hsync の比較と既存判断
- 名札ロード/man_gate/man_kapi_gate の順序は維持。一覧の取得・完全検証は最初の mkdir/clean_temps/コピーより前。
- 開始点が単一ファイルなら一覧を作らず旧経路を使う。全体/部分木は1ジョブだけ取得。既定sys除外の内容もホスト一覧の枠には含むが、上限ERR_FULLなら書込み前に一度だけ旧経路へ戻る。
- sync_directory の列挙部分を供給元切替にする。新経路は一覧の直下項目を FileList へ供給し、os32_ls を呼ばない。
- MAX_FILES=128/dir、MAX_DEPTH=8 の既存エラーを維持。8192 件の総上限でこれらを置き換えない。
- 子 mount・sys 除外・settings.db・FEP・保護実体を既存と同順で判定し、その後だけ CRC/コピーへ進む。
- sync_file に「一覧項目あり/なし」を渡す。ありなら ss の型・size・mtime を項目から構築し、冒頭の source STAT を省く。
- 宛先 stat はローカルなので維持。型不明・型衝突・stat IO は従来どおり失敗。
| 条件 | 新経路の判断 | 日時・表示 |
|---|---|---|
| -f | 比較を省いてコピー | 保護・検証は省かない |
| 宛先なし/サイズ違い | コピー | new_file/size_changed |
| サイズ同じ、有効日時同じ、--verifyなし | ゲスト本文を読まず省略 | same_mtimeを従来どおり計上（CRCより先に判定） |
| サイズ同じ、日時違い/不明または--verify、CRC不一致 | コピー | content_changed |
| サイズ同じ、CRC同じ、日時違い/不明 | 本文を写さずapply_mtime | mtime_only、失敗/不明/NOSYSは既存集計 |
| --verify、サイズ同じ、CRC同じ、有効日時同じ | 本文を写さない | same_content（ローカルCRCは実施） |
- same_content は CRC 同一の意味へ更新し、ログに compare=crc/legacy を追加。旧経路の語義は従来どおり。
- 新規 local_crc(path,expected_size) は file_buf を使い、CRC と総長を計算し EOF まで確認。ローカル IO を同一扱いしない。
- g_content_compares は旧 byte compare の回数を維持。新たに local_crc_files/bytes を計上する。
- R2ユーザー決定「今どおり飛ばす」: 同size/有効mtimeなら同時刻の内容変更も既定では検出しない。日時違い/不明と--verifyだけCRC比較し、新経路では遠隔本文を読み比べない。CRC衝突まで同一を証明する仕様ではない。
- PPC-3: ゲストCRC cacheは作らない。必要な比較のたびローカルファイルを読む。既存crc32_coreの32bit整数演算を使い、SFSフレームCRCとファイルCRCの対象を混同しない。
- PPCのローカル読出し+CRC時間は未計測。T_local=対象bytes/実効bytes毎秒。16,237,823 Bなら5/20/100 MB毎秒の仮定で約3.25/0.81/0.16秒（保証値ではない）。
- same_mtimeだけのbig_nochangeはlocal_crc_files/bytes=0。QEMU両CPUでbig_changedと--verifyのCRC対象bytes・local_crc_msを採り、host CRCの4.7 msをゲストの時間に流用しない。
- コピー時は copy_body の最終 CRC/総長を一覧項目とも照合。不一致は source_changed、公開前に一時ファイルを捨てる。
- `.deploy/manifest.txt` の CRC 照合は併存。名札と一覧が違うコピー元は既存 manifest_crc_mismatch で止める。
- replace_file のコピー後 source STAT は変更ファイルだけに残す。途中の日時変更検出を維持する。
- source open 内部の STAT も残る。「全件比較に必要な 1 件 1 STAT」を廃止し、コピー時の少数 STAT は許容する。
- 宛先の再確認・保護再確認・sync・読戻し CRC・atomic replace は既存のまま。
- backup は実際にコピーする vmkernel だけ。CRC 同一/mtime 変更だけなら bootold 作成・再起動通知を行わない。
- --unsafe-overwrite は従来の直接上書き保証のまま。CRC 不一致を検出しても旧内容を復元できるとは表示しない。
- -n は CRC と計画表示のみ。mtime/backup/FEP close/掃除を新たに行わない。
- 名札の build/KAPI/世代情報は一覧には含めない。man_kapi_gate と --expect-build を一覧で代替しない。
- shell 見出しと名札の固定 READ 25 回は今回残す。既存の信頼性と変更範囲を保つ。

## 5. 往復の見積もり
- F=対象ファイル数、C=実コピー数、B=一覧 bytes、M=Σceil(b_i/508)（§3の分割）、P=追加 status READ 回数。
- V=仮想ファイル open に伴う STAT 総数。new と data で 2 open、BUSY ごとに 1 open 増える。
- 測定の STAT 差分は open あたり 2 回と整合するが、VFS 全文未調査なので V=2(2+P) は検証する仮定。
- K=既存のコピー本文 READ、Q=既存の比較本文 READ（EOF 確認含む）。生成待機の READ は 1+P。
| ケース | 現在 STAT/LIST/READ | 新 STAT/LIST/READ（SFS） |
|---|---|---|
| small_nochange F=4 C=0 | 9 / 1 / 25 | 5+V / 0 / 26+P+M |
| small_changed F=5 C=2 | 18 / 1 / 53 | 11+V / 0 / 26+P+M+K、K=28-Q |
| big_nochange F=260 C=0 | 265 / 69 / 25 | 5+V / 0 / 26+P+M |
| big_changed F=260 C=5 | 790 / 69 / 32316 | 20+V / 0 / 26+P+M+K、K=32291-Q |
- 変更時の定数 11/20 は現行から F と比較 open の STAT（2×1 / 2×255）を除いた推定。
- COPY 回数と比較 open の仮定が外れたら実トレースで補正。合算 report だけから K と Q を一意に分離しない。
- B=24+Σ(16+パス byte 数)。dir/other も含めるため、260 ファイルだけで B を確定しない。
- 例: 合計 300 項目、平均パス 40 B なら B=16824、M=34（LAN/link は 17）。
- PPC-2/R4: 生成処理時間をT秒とすればP≈max(0,ceil(T/0.5)-1)。0.17秒ならP=0、0.82秒ならP=1、2.0〜2.5秒なら境界によりP=3〜4。ファイル処理の時間もTに含める。
- この例でP=0,V=4のbig_nochangeはSTAT9+READ60=69往復、90 ms換算約6.2秒+生成T。R2によりゲスト本文CRCは0秒。
- 冷cacheの例T=0.82秒、P=1,V=6なら72往復、約6.5秒+生成0.82秒。P=4なら81往復/約7.3秒+生成T。現行359往復/33.7秒と比較する。
- big_changed は比較 Q を除いた K を加算。現行 cycle 約127 ms と CRC/媒体時間を使って別に予測する。
- small_nochange は既存 35 往復に対し 31+P+V+M。固定費で遅くなり得る点を受入記録に明示する。
- ホスト CRC とゲスト CRC は新しい待ち時間。HostDrv の 1.2/6.7 秒を SFS 新経路の達成保証には使わない。

## 6. 容量と互換
| 部品 | 見積もり/条件 |
|---|---|
| hsync.bin | decoder/取得/比較追加で text 数 KiB〜15 KiB を計画値。実ビルドの size 差を提出 |
| ゲスト RAM | 一覧最大 1 MiB + 項目 offset 8192×4 B、既存 64 KiB file_buf 再利用。終了/失敗時解放 |
| カーネル/KAPI | 追加 0 B、生成 KAPI・SFS op・payload 上限を変更しない |
| ホスト | 新module約350行+結線/helper約90行。1一覧最大1 MiB+作業領域。CRC cacheは持たない |
- newの初回open/readがNOTFOUND/NOTDIR/NOSYS/NAMETOOLONG/INVALなら、書込み前に一度だけlegacyへ戻る。最後の2値はHostDrvも対象。data/statusの旧tokenエラーにはこの救済を適用しない。
- HostDrv も仮想名を持たなければ同じ fallback。旧 hsync + 新ホストは仮想名が LIST に出ず従来動作。
- descriptor先頭4 BがHMD1でなければ実体衝突/未対応としてlegacy。HMD1なら長さ・版・state等を厳密検証し、壊れたdescriptor/dataはIOで停止（未知版も含む）。
- 一覧生成/取得のERR_FULL（8192件/1 MiB超過を含む）は書込み前に一度だけlegacyへ戻る。HOSTDOWN/TIMEOUT/STALE/途中切断・その他の失敗は停止。公開後に旧比較へ切り替えない。
- token 失効・host 再起動は今回の hsync を失敗終了。次の実行で新規取得し、古い一覧を流用しない。

## 7. 1 ファイル = 1 段 = 1 実装者
| 段/波 | ファイル（新規/変更） | 概算追加変更行 | 内容・依存 |
|---|---|---:|---|
| A/1 | userland/system/hsync_inventory.inc 新規 | 300 | v1 decoder/取得/local CRC、B と wire 定義を先に固定 |
| B/1 | tools/host_tree_manifest.py 新規 | 350 | iterator・snapshot・仮想stat/read、HostFSを直接受ける |
| C/2 | tools/serialfs_host.py 変更 | 90 | stat共用mtime/fd helperとstat/read/close結線、B後 |
| D/2 | userland/system/hsync.c 変更 | 220 | A include、列挙切替、ss 供給、コピー CRC、集計 |
| E/2 | tools/tests/test_hsync_inventory.py 新規 | 350 | A を組む C harness、CHECK inputs 宣言 |
| F/2 | tools/tests/test_host_tree_manifest.py 新規 | 350 | B の host/境界/失敗/変異、CHECK inputs 宣言 |
| G/3 | tools/tests/test_hsync_inventory_flow.py 新規 | 380 | C/D と三 adapter の統合、旧動作退行・計数 |
| H/3 | docs/archive/network/NET_HOSTSVC_DESIGN.md 変更 | 55 | 仮想ファイル wire と寿命の正典追記 |
- 波1は A/B 並列、波2は C/D/E/F 並列、波3は G/H 並列。各ファイルの担当を重ねない。
- A の静的 helper は既存 KernelAPI と CRC 核を使用。build list 変更なしの .inc 方式。
- Bは渡されたHostFSの_stat_raw/_kind/mtime関数/fd helperを呼び、serialfs_hostをimportしない。A/B着手時にhelper契約を固定し、B単体は同契約の試験用FS、C後に実HostFS両分岐で確認する。
- lanfs_host.py/rshell_serial.py/host_sfs_link.py/host_agent.py の製品変更は不要、G で全経路を検証する。

## 8. 受入 → 試験と変異・段の完了条件
| 受入（観測） | 試験 | 固有 anchor の変異（runtime RED 必須） |
|---|---|---|
| CRC の同値・分割・空ファイル | E/F: 空/123456789=CBF43926/多 chunk | final XOR を落とす |
| 全木、空 dir、other、順序、bytes保存 | F: 多階層・非ASCII bytes | 空 dir 項目を削る |
| 508/1024 境界と一覧分割 | E/G: 境界±1 B、複数 READ | offset 加算を1 Bずらす |
| 変化検出・token 不変性 | F: 生成途中差替え・再送・sid変更 | 前後 fstat 照合を無効化 |
| BUSY/期限/1ジョブ/close回収 | F/G: 時計注入、5回連続中断→new成功、前token失効 | newで前ジョブを残す |
| 壊れた一覧で宛先変更ゼロ | E/G: 長さ/版/重複/CRC/短いREAD | 構造検査の失敗を成功へ |
| same_mtime無読出しと--verify | G: 同size/mtime・異CRC、既定はlocal_crc_bytes=0/無コピー、--verifyはコピー | --verifyでもmtime一致で早期return |
| same_mtimeの無読出し維持 | G: 正常対照はローカルread=0 | mtime一致でもlocal_crcを呼ぶ |
| 日時だけ変更は本文書込みゼロ | G: mtime/未知/NOSYS/-n | meta_only を copy に変更 |
| --verify も遠隔比較READゼロ | G: source readカウンタ | compare_files を呼び戻す |
| コピー元変化で公開ゼロ | G: 列挙後に同size書換え | 一覧CRC照合を無効化 |
| 保護/FEP/mount/bootold の退行なし | G: 既存条件をfixture化し同じ結果 | 保護前にCRC/backupを実行 |
| 名札/KAPI不一致で書込みゼロ | G: 名札CRC・版・--root/boot | man_kapi_gate 呼出しを落とす |
| 古いホスト/旧hsync/HostDrv互換 | G: NOTFOUND/NOTDIR/NOSYS/NAMETOOLONG/INVAL、旧client、非HMD1実体 | 初回INVALを致命的扱い |
| 単一ファイル/一覧上限 | G: 単一対象はnew=0、除外sysだけで枠超過→書込み前legacy1回 | ERR_FULLを致命的扱い |
| LIST同等の無効名除外 | F/G: 長名/backslash/無効dirを除外しskipped_names一致 | 除外件数の加算を落とす |
| HostFS.statと一覧metadata同値 | F: secure/非secureのfile/dir/other、範囲外日時と.9999999xx境界、size/kind/mtime完全一致 | mtimeをst_mtime_ns//10**9へ変更 |
| 通信失敗をfallbackにしない | G: HOSTDOWN/TIMEOUT/STALE | エラー全てlegacyへ |
| 三ホストで同じ一覧 bytes | G: serial/LAN/SfsLink の同一fixture | 一adapterだけ結線を迂回 |
| 四条件の往復削減と内容一致 | NP21/W シリアル: 同fixtureで件数/時間/再起動後CRC | --verifyでhost注入CRC1bit反転、公開されないこと |
| x86 LANでも旧利用者維持 | NP21/W LAN: /host read と hsync、CALL統計 | 境界chunk欠落を注入 |
| PPC endian/期限/再接続 | QEMU 7450/7455 link: 同fixture、再起動、CRC/計数 | LE復号をBEへ差替え |
- 変異は各ケースの未変異正常対照がexit=0、該当固有anchorだけを変更した実行がassert失敗の非0（runtime RED）。コンパイル失敗をREDに数えず、変異を戻した同ケースがexit=0まで確認する。
- NP21/Wシリアル/LANとQEMU7450/7455の正常四条件はhsync終了値0、--verify下の注入CRC破損・STALE・不正HMD1は非0かつ対象公開なし。fallback成功は0。台本自身も全期待一致で0、不一致/timeout/終了値未取得は非0とする。
- E/F/G は CHECK dict に製品・include・fixture 入力を宣言。移動/削除する行の既存 mutation anchor を検索して移す。
- 波1/2は担当テストの --mutate と必要ビルド。D 後は hsync を含む programs/userland image を構築する。
- 波3統合は x86/PPC の対象ビルド、変更テスト --mutate、python3 -B tools/checks/run.py --lint。
- check-changed/全変異は実装者では走らせず、CI と full-check の役割を維持。全ログは最後の変更後に採る。
- PM は build size 差、NP21/W の4条件、QEMU両CPUを受入。未実施は合格とせず理由を残す。

## 9. 未決（ユーザー判断が必要なもの）
- 0件。same_mtimeの無読出し維持、必要時のみCRC比較、名札維持、旧ホスト/一覧ERR_FULLのfallbackを上記で確定する。
- CRC実読速度・一覧サイズ・open由来STAT数は実装/計測で確定する技術項目で、ユーザー決裁待ちにしない。

## 反論
- 0件。R1〜R9およびPPC-1〜4を反映。

## 確定 (2026-11-16、Opus 第 2 回で承認、PM が反映)
- R1〜R9: r2 で全部直った (第 2 回)。R2 はユーザー決定「今どおり飛ばす」。
- 注 1 (実装の依頼に書く): 生成の予算は 64KiB を読む単位の前後でしか見ないので、冷えた drvfs では 1 回の os.read が 500ms を越え得る。host_agent は期限を CALL の前に見るので応答は返る。受入で BUSY の回数を記録する。
- 注 2: 取得の ERR_FULL には、ゲストが 1MiB を確保できない場合も含め、今の経路に戻す。

## 波 1 の依頼で PM が決めたこと (2026-11-16)
- M1 (token のパスの符号化): token[16] は、token[0] から順に 1 バイトを 2 桁の小文字の hex にした 32 文字でパスに入れる (例 token[0]=0xAB → "ab")。A (ゲスト) と B (ホスト) は同じ符号化をし、試験で同じ token の往復を確かめる。
- M2 (ERR_FULL・BUSY の伝え方): SerialFS の sfs_status は応答の status の ERR_FULL を IO に変えるので、一覧の取得の結果 (BUSY・ERR_FULL・STALE など) は SFS の応答の status では返さない。ホストは STAT・READ を成功 (status 0) で返し、HMD1 の記述子の状態の欄で伝える。ゲストは記述子の状態の欄だけを見て、ERR_FULL なら今の経路へ戻る (書き込む前に 1 回)。カーネルは変えない。
- M3 (HostFS の helper の契約、B が呼び、C が serialfs_host.py に足す):
  - 既にある物: `HostFS._comps(wire) -> list[bytes]`、`HostFS._stat_raw(comps) -> os.stat_result`、`HostFS._kind(st) -> (kind, size)` (静的)、`HostFS._entries(comps)` (list() と同じ飛ばし方の列挙)。
  - C が足す物: `HostFS.wire_mtime(st) -> int` (`m = int(st.st_mtime); return m if 0 <= m <= 0xFFFFFFFF else 0`、stat() もこれを使う)、`HostFS.open_read_fd(comps) -> int` (読み専用の fd。secure は今の read() と同じ dir_fd と O_NOFOLLOW、非 secure は resolve() の道。ディレクトリなら SfsError(ERR_ISDIR)。閉じるのは呼んだ側の os.close。read() もこれを使う)。
  - B は serialfs_host を import せず、渡された fs の上の 6 つの名前だけを呼ぶ。B の単体の試験は、同じ 6 つの名前を持つ試験用の FS で行う。
- 注 3 (os32powerpc): host_agent の SFS の 1.0s の期限は壁時計 (host_agent.py:820・1082-1084) で、受付から数える。QEMU の -icount ではゲストの時計が先走り・凍る (0.5〜0.75s)。一覧の生成の 500ms と重なると 503 になり得る。受入は BUSY の数と 503 (HTTP_UNAVAIL) の数を分けて記録し、台本で期限を延ばさない。
