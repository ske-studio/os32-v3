# TASK_EXT2_ERRORS_INVESTIGATION — NHD ext2 errors印の原因調査

> 状態: **計画 (2026-10-01)** — 調査計画。原因未確定、修復・ゲスト再現は未実施。
> 起票: GPT-6 / Codex。観測の正典: [T2親票](TASK_T2_APPBAND.md) §5-1 T2c-R末尾。
> 順序: [T2d〜h詳細](TASK_T2D_T2H.md) §0・§5・§8。PMが調査担当と媒体を指定し、dと並行に着手、T2h受入より前に§3を閉じる。

## 1. 観測と切り分けること

T2bの起動から `[EXT2] warning: mounting fs with errors` が観測されている。これは初回観測であり、T2bが印を付けた時点・原因という証拠ではない。原因未確定のため現時点では既知の不具合と断定しない。既存媒体の破損、以前の操作/停止、OS32のFS/I/Oエラー処理、配備操作を区別する。

FSドライバはカーネル層である。[POLICY_DEV §1](../../POLICY_DEV.md) に従い、実装不具合と分かった時点で新機能より修正を優先する。T2とは別件という呼称だけで後回しにしない。

## 2. 調査手順と記録

各小段は45〜75分目安、最大120分。今回の起票ではNHD・NP21/W・配備・iniを操作しない。以下はPMが後日手配する作業。

| 小段 | 作業 / 証拠 |
|---|---|
| x1 履歴 | T2b以前・T2b初回・T2cのbootログと媒体履歴を照合。最終正常/初回警告のSHA、kernel/媒体hash、日時、deploy/hsync、書込み・終了・停止操作を並べる。不明は不明と記録 |
| x2 読取り確認 | PMが停止/ロック解放を確認したNHDの保全コピーを用意し、元hash・サイズを記録。NHDコンテナのヘッダとパーティション位置を確認して、コピーからext2区間を別ファイルへ抽出。NHD全体へ直接e2fsckをかけない。ホストで `e2fsck -n <抽出したext2像>` を実行し、コマンド/tool版/rc/全診断、superblockのstate/errors関連値、前後hashを保存。rc!=0を検出結果と区別し、修復オプションを使わない |
| x3 再現 | 保全した異常像と健全な作業像を分け、同じbuildでbootのみ→読取り→書込み→同期/正常終了の操作を1つずつ追加。各試行後の停止済みコピーをx2で確認し、どの操作の前後で初めて印が立つか絞る。既存証拠で不要なら理由を記録。エラー注入が必要なら隔離fixtureへ限定 |
| x4 原因/修正 | ext2の印設定、メタデータI/O失敗、VFS/driverへの伝播、媒体履歴を突き合わせる。kernel/FSの既知不具合なら最小再現→修正→対象host/guest回帰→読取り再確認を先に閉じる。媒体/操作由来なら根拠と健全媒体の準備を記録 |

一時ファイル/抽出像/ログは `/home/hight/os32-tmp/` 以下。生きたマウントや実行中NHDを検査対象にしない。保全原本の上書き・媒体の修復は [CONSTRAINTS D2](../../CONSTRAINTS.md) の個別承認対象。修復が必要なら、対象hash、診断、具体的コマンド、保全/復旧手段をPMが提示してから承認を求める。`-n`の確認と修復を同じ依頼で暗黙に実施しない。

## 3. T2h受入前のゲート

- いつから・どの操作で立ったかを、確定した範囲と未確定範囲に分けて報告する。初回観測と原因発生時点を区別する。
- ホストの `e2fsck -n` の診断/rcとhashを残す。OS32で読めることだけを健全性の根拠にしない。
- 原因を分類し、kernel/FS不具合なら修正と再検証を先に完了する。T2h用の健全媒体をPMが指定する。
- 原因未確定のままなら本ゲートは未解消。健全媒体で試験準備を続けても、調査完了やT2h全受入としない。ゲートの延期/免除はコーダーが決めずPMからユーザーへ提示する。

担当: PMが調査/修正コーダー、テスター、独立レビュアーを割当てる。結果欄は未記録。本票の起票を調査成功・修復済みとは数えない。

## 4. x1 結果 (2026-10-01、GPT-6 / Codex)

調査基点 `7f16012`、ブランチ `docs/ext2-x1`。読む調査と本欄の追記だけ。
NP21/W・NHD (内容・hash・サイズの取得も含む)・配備・ini は未接触、commit / push なし。
以下の「確定」はログそのもの、または参照文書にその操作・観測が記録されているという意味で、媒体を再検査した意味ではない。

**履歴の結論**: 最後に errors 印が無かった起動の SHA / 日時は **不明**。
今回渡された完全な boot ログで最も早い警告は `c06df8d` (T2b) だが、
[C11 移行票](TASK_C11_MIGRATION.md) A9 は **2026-09-30 の `7cc2af0` 起動で警告を記録し、「9/29 の NHD から出ている」と明記**する。
したがって §1 と T2親票末尾の「T2b の起動から」は、今回の2ログの範囲での観測として読む。
全履歴の初回警告は確定できない。9/29 の警告起動の SHA / 時刻 / 全ログも不明。
T1 の kselftest / GUI 合格や警告の引用省略を、errors 印なしの証拠にしてはいけない。

### 4-1. 時系列と媒体の引継ぎ

日付・コミット日時は JST。Git の日時は **コミット日時**、boot ヘッダの日時は **ビルド日時**であり、実際の起動・停止時刻とは区別する。記録内で順序が示された操作以外の厳密な前後関係は不明。

| 日時 / 段・SHA | 区分 | 起動・媒体と操作の記録 | 限界 / 根拠 |
|---|---|---|---|
| 最終正常起動 | **不明** | errors 印なしを示す当該媒体の全 boot ログ・superblock 記録なし | SHA・起動時刻・kernel hash・媒体 hash とも不明。正常→異常の操作区間は閉じられない |
| 9/23、過去の cdinst 媒体 | 確定 (別件の記録) | 空名ディレクトリエントリをホスト e2fsck が検出、写しで修復を確認 | [POLICY_DEBUG](../../POLICY_DEBUG.md) §4-58。今回の NHD と同一かは不明。不整合の存在と `EXT2_ERROR_FS` の設定は別事実 |
| 9/29 21:58 の Windows 側 NHD | 確定 (文書) / **不明** (警告起動) | C11 配備元の記録。A9 はこの NHD から警告が出ていたと記す | 21:58 は文書に記された媒体日時で、警告起動時刻ではない。警告起動 SHA・作成/インストール操作・停止方式・hash 不明 |
| 9/30、C11。`7cc2af0` コミット 16:31:32 | 確定 (文書) | 停止 → `make deploy-kernel` rc=0 (上記 Windows 媒体を取り込み) → 起動。HDD `Commit 7cc2af0`、CRC `4c2645a2`、465296B、225/225・fail=0、**errors 警告あり**。試験14 PASS/2 SKIP、GUI→CUI、FD起動も実施 | C11票 A9。現存全ログは今回未提供、起動時刻と媒体 hash 不明。「インストール後 e2fsck はしない決定」の記録だけでは媒体健全性を判定できない |
| 9/30〜10/1、T1a/b。`d7ac7a0` コミット 00:08:46 | 確定 (文書) | 8/17MB の起動・GUI、test2、V86、#PF。`sleep 20` の STOP は未確認。続く `faulttest gp/de/ud`、loop/kloop+CTRL+STOP ではアプリ kill とシェル復帰を確認 | [T1票](TASK_T1_LEDGER.md) §4-2-N。faulttest は `168a47b`、kernel は `d7ac7a0` のまま。errors の有無、ゲスト hsync の実施・件数、停止方式は不明 |
| 10/1、T1c。`7d1133d` 03:19:38 / `4233715` 03:23:06 | 確定 (文書) | HDD/FD 226/226。`/host/sbin/hsync.bin` 34560B → FD `/VAR/T1C.BIN` へ cp → `emu_reset` → FDから /host へ cp、cmp一致 | T1票 §4-3-N。これは **hsync バイナリの FD コピー**で、hsync 実行の証拠ではない。NHDへの当該書込みは記録なし |
| 10/1、T1e/f。`0fedc76` 08:39:14 / `c90eed8` 10:06:21 | 確定 (文書) | PEGC/planar/Cirrus、8/17MB、GUI→CUI→GUI、gfxmode、V86、ゲスト試験14 PASS/2 SKIP、HDD/FD 226/226 | T1票 §4-5-N・§4-6-N。gfxmode の設定書込みはあり得るが、その前後の ext2 印・書込み結果は未記録。警告非掲載は正常の証明にならない |
| 10/1、T2a。`ce5a2a9` 11:44:19 | 確定 (文書) | deploy-kernel 実施記録、8/17MB boot fail=0。gp/de/ud/pf、loop/kloop+STOP、V86、GUIからCUI時のWM kill (park中アプリ含む) | [T2親票](TASK_T2_APPBAND.md) §5-1 T2a-R。検査中の配備でISO/FD鮮度チェックが失敗した記録はあるが、NP21/W稼働中NHD書込みをした証拠ではない |
| 10/1、T2a′。`279272d` 14:07:24 | 確定 (文書) | **停止 → nhd-pull → deploy-kernel → deploy → 起動**。8/17MB fault/STOP/V86/GUI→CUI。配置実測では `cat boot.log`、`hsync -n` 等も実施 | T2親票 §5-1 T2a′-R・§6-1。`hsync -n` はdry-run。正常 unmount/sync・停止時刻・強制 kill の有無・hash は不明 |
| 10/1、T2a′ kernel、T2bより前の試験記録 | 確定 (文書) | CRC `c02dc716` 472431B。PCM正常close、再生中CTRL+STOP×3→再open/再生×3、アプリ回収・IRQ解除・tick継続。`gfxmode cirrus` → `/api/reset`、planar fallback GUI→CUI | T1票 §4-6-N2、記録コミット `92dbe59` は15:20:25。初回PCM試行のopen直後killは別記。**アプリ kill とエミュレータ強制 kill を混同しない**。errors の前後は不明 |
| 10/1、T2b。`c06df8d` 15:35:16、build 15:36:32 | **確定 (提供全ログ)** | 停止 → nhd-pull → deploy-kernel → deploy → 起動 (T2親票)。`boot_prev.log`: HDD、CRC `688f8280`、477781B、**errors 警告**、227/227。受入記録は8/17MB fail=0・lease=0、fault/STOP/V86/GUI→CUI | 起動実時刻不明。警告は kselftest / PCM / GUI より前。起動中の新規 metadata I/O error 行はこのログにない |
| T2b→T2c の媒体継続 | **推測** / **不明** | 両ログとも hd0、3011/8/17、409496 sectors、partition LBA1632 +407864。配備記録と既存印保持経路から持越しが有力候補 | 同一幾何・LBAは同一内容/hashを証明しない。途中の媒体交換・hsyncの実行・書込み/終了/停止の完全な操作列は不明 |
| 10/1、T2c。`11e1c9d` 18:59:24、build 19:03:19 | **確定 (提供全ログ)** | clean→all→NHD一式配備 (T2親票)。`boot_t2c.log`: HDD、CRC `dae1888e`、477983B、**errors 警告**、227/227後に `ledger:AS alloc` FAIL / exec_init後1 fail | このselftest失敗より先に印を読んでいるため、この失敗を当該起動の印設定原因にはできない。停止→pullの実施詳細、起動時刻、媒体hash不明 |
| 10/1、T2c再受入。`6aacf43` 19:35:08 | 確定 (文書) / **不明** (印) | selftest期待値修正→再配備、8/17MB fail=0・lease=0。高位fault/STOP/V86、ring3_guard A〜E、GUI→CUI | T2親票 T2c-R末尾。再受入の完全bootログは未提供、errors印が消えた証拠なし |

提供ログは両方とも `dropped 0 bytes`。SHA-256 (ログの hash、**kernel/媒体の hash ではない**):

- `/home/hight/os32-tmp/boot_prev.log`: `b103a077094ebf3a2347ede0e482ed15d10fb29cca61a3d3e011462411bae855`
- `/home/hight/os32-tmp/boot_t2c.log`: `e584b449126468c27e881e7df675a27e5d2be086d4cfcf831a1c33fe105141ec`

**履歴の不足**: 原因発生区間を閉じるには9/29以前の正常ログと媒体履歴が要る。
各配備の元/先hash、ホストmount/unmount結果、ゲスト `sync` / 正常終了の成否、
エミュレータ `/api/quit` / reset / 強制killの実コマンド・時刻は今回の材料では揃わない。
[POLICY_DEBUG](../../POLICY_DEBUG.md) §4-60 は9/24〜25の `taskkill`→起動とロック問題の過去事例であり、今回の媒体で同じ操作があった証拠ではない。
[realhw/PLAN](../realhw/PLAN.md) §0 の SerialFS `hsync -n boot`→`hsync boot`→reboot→`hsync sys`→`hsync` と、
T1票の Ra266 実機の更新・fault/PCM 記録は **別の物理 HDD**。今回の NP21/W 媒体の hsync 履歴に足してはいけない。
そこから参照される9/24〜26の日次チェックリストは v3 に持ち込まれていない。

### 4-2. 印を設定し得るコード経路

`file:line` は調査基点のソース。Git履歴では ext2 本体と `build/deploy.mk`・`tools/nhd_deploy.py`・
`tools/np21w_ctl.py`・`tools/hostdrv_deploy.py`・`tools/prune_stale.py` は fork `a238ab1` (9/30 14:09:32) 以後 HEAD まで差分なし。
v3 の Git は fork が根のため、9/6・9/11・9/15・9/23 の修正前後の SHA はこの履歴から取得できない。
9/30〜10/1 の tools 変更は主に検査器と設定切替道具で、配備対象コードの新変更日時と媒体への実行日時は別。
旧破損を除外する根拠には [POLICY_DEBUG](../../POLICY_DEBUG.md) §4-24 (間接表/bitmap共有)、§4-32 (端数読取り越境)、§4-35 (I/O伝播/rename)、§4-58 (空名) の修正済み記録だけでは足りない。

| 設定入口 / file:line | 条件と関係する I/O・検査 |
|---|---|
| `fs/ext2_super.c:79` (`ext2_read_block`) | metadata用raw readが非0。`fs/ext2_super.c:46` の区画外block検査、`:50` / `:54` の512B読取りどちらかの失敗を含む。**読取り操作だけでも印が立ち得る** |
| `fs/ext2_super.c:86` (`ext2_write_block`) | metadata用raw writeが非0。`:63` の区画外block検査、`:65` / `:67` の512B書込みどちらかの失敗 (片側だけ成功もあり得る) |
| `fs/ext2_dir.c:1013` (`ext2_replace_file`) | `ext2_publish_entry` が未公開0 / 不明-1。`:913` 初回sector read失敗、`:915` 宛先inodeが走査時と違う、`:918` write失敗後`:920` readも失敗、または`:921`〜`:924` の値が新inodeでない。writeが失敗しても読戻しが新inodeなら公開済み1で、この入口では印を立てない。hsyncの置換renameが到達し得る経路 |
| `fs/ext2_dir.c:1087` (`ext2_rename`) | 移動元と宛先が同じinode、かつディレクトリ。ディレクトリに2名がある不整合を検出してIOを返す (通常ファイルのhardlink同士はOK) |
| 実際の設定 `fs/ext2_super.c:140`〜`:164` (`ext2_fs_error`) | ctxが有効・初回なら`:147`でメモリ `fs_error=1`。mounted時だけ警告を出し、base_lba+2を直接read、s_state offset58へ **旧値 OR 0x0002**、512Bだけ直接write。read失敗・既に印ありなら書かない。write戻り値は捨てるため、メモリerrorと媒体の印の永続化成功は同義でない |

metadata wrapper の呼び手 (上の2入口に集約): inode表 `fs/ext2_inode.c:21,64,89`、
block bitmap `:120,142,188,194`、inode bitmap `:215,229,260,264`、単一/二重間接表 `:294,307,311,354,364,367,381,391,402,409,415,423,426`、
解放前の間接表読取り `:501,513,519`、ディレクトリ走査・追加・削除・mkdir・空検査・rename補助
`fs/ext2_dir.c:31,114,199,248,258,299,354,381,478,544,682,714,726`、
super/GDT同期 `fs/ext2_super.c:263,267,284,297`、mount時GDT読取り `:474`。
formatも wrapper を使うが未mountedの一時ctxなので error hook 自体は媒体へ印を書かない (`fs/ext2_super.c:150`)。

**消去・消去しない経路**:

- mount はsuperを直接read (`fs/ext2_super.c:410,414`) して、`:445` で既存印を検出、`:447` で警告。警告自体は印を立てない。`:443` はメモリ上のerrorだけを0にし、再mountでrwを許す。
- sync (`fs/ext2_super.c:510`)、unmount (`:497`)、通常のsuper書戻し (`:260`、変更は空き数のみ) は印を消さない。既存印は正常操作の後も残る。一般データblock I/O (`:95,100`) とsector単体I/O (`:116,123`) の失敗自体はhookを呼ばず、renameの公開判定だけは上表の呼び手が決める。
- OS32側に既存FSの印を消す修復経路は見つからない。`fs/ext2_fmt.c:119` の `EXT2_VALID_FS` 書込みは **新規フォーマット**であり修復ではない。ホスト e2fsck はx2以降の担当・承認範囲。
- reset・CTRL+STOP・強制killそのものが印をORするコードは上表にない。途中停止が不整合を残す可能性と、印の直接設定を区別する。今回のbootログ中のfatfs失敗・ATAPI READ失敗・selftest用invalid free行は、このext2 hookが発生した証拠ではない。
- 配備コードはLinux ext2 mount経由でファイルを書き、unmount→媒体コピーする (`tools/nhd_deploy.py:332,378,590,1098`、`build/deploy.mk:16`)。ゲストhookを経由しないため、ホスト側のstate更新・診断と媒体コピーも別候補。実行はしていない。

### 4-3. x2 への申し送り (優先順、原因未確定)

1. **既存印の持越し**: 最低でもC11受入の警告を起点にする。PMが保全した媒体で primary superblock の state (VALID/ERROR各bit)、errors方針、mount/write/check時刻・count、バックアップsuperblockとの違い、hash/サイズ/幾何/区画位置を記録。OS32の `ext2_current_time` は固定近似値 (`fs/ext2_super.c:253`) なのでinode日時だけで操作を日付に結びつけない。同じ媒体であることはhash/保全履歴で確かめる。
2. **印だけか、現在も構造不整合があるか**: 保全コピーから確認した位置でext2を抽出し、§2の `e2fsck -n` 全診断・版・rc・前後hashを保存。交差リンク/bitmap/inode/count/ディレクトリ空名・同inode2名を分類する。過去の不具合に形が似ても原因や発生時期は断定しない。state=ERRORだけでも過去の一時I/O失敗は否定できない。
3. **hsync / rename と読取りI/O失敗**: `.hs~` 一時ファイル、`vmkernel.old`、置換先inode/link数等の残存を診断と突き合わせる。9/29以前のmetadata I/O error / writes disabled / -15 / IO / STALE / replace_partial のゲストログ、hsync実行列をPMから補う。ブロック範囲外の参照も候補で、ハードI/O失敗に限定しない。残存だけでhsyncを原因としない。
4. **配備・停止の境界**: Windows→local pull→Linux mount/write/unmount→Windows copy各段のhash/rc、どの媒体をいつ使ったかをPMが補う。ホストkernelのext2診断が保存されていれば併記。9/29以前の最後の正常bootと最初の警告bootが手に入るまで、T2b前後のfault/PCM/V86/GUI/STOPを原因区間と断定しない。

x1は履歴とソースの調査結果だけ。x2の媒体診断・修復・ゲスト再現は未実施、§3のゲートは未解消。

文書確認: `git diff --check`、`python3 -B tools/check_docs_status.py`、`python3 -B tools/check_docs_orphans.py` はrc=0。
追加したMarkdownリンク5先の存在確認も成功。`TMPDIR=/home/hight/os32-tmp`、bytecode生成なし。
`make check-changed` / 全体ビルド・試験は未実施 (今回は読む調査と文書追記だけ。全体チェックの画像・成果物検査は実行範囲に含めない)。
変更は本票の追記だけで、原因を確定した扱いにはしない。

### 4-4. x2 の結果 — 保全コピーの読取り診断 (PM、2026-10-01)

- **保全**: NP21/W を `np21w_ctl.py stop` で止め、`C:\Users\hight\Documents\np21w\os32.nhd` を `/home/hight/os32-tmp/ext2-x2/os32.nhd.copy` へ複製してから起動 (原本・`build/nhd/os32.nhd` は上書きしていない、`nhd-pull` は使っていない)。複製は 209,662,464 バイト、sha256 `ebc6d1d55d4591ee82aca1d9845be1e1ae676aa8266e40f012dfd4de298078a7`。この時点の NHD は T2c (`6aacf43`) を配備した後。
- **区画の抽出**: 起動ログの `[EXT2] hd0: partition LBA 1632 +407864` とヘッダ 512 バイトから、オフセット 836,096 バイト・407,864 セクタを `ext2.img` へ (sha256 `5ddd03d5272f2f23be2b8f0249b0bb9f57d0d153b499bff1c0510a607c1ea7c9`)。
- **superblock** (`dumpe2fs -h`): Filesystem state = **not clean with errors**、Errors behavior = Remount read-only、Mount count = 33、Last write time = 2026-10-01 19:35:40 (ホストの配備の書込み)、Last checked = 1970-01-01 (一度も検査されていない)。First/Last error の欄は無い (OS32 の ext2 は記録しない)。
- **`e2fsck -fn`** (e2fsprogs 1.47.2、読むだけ): Pass 1〜5 で**指摘 0 件、rc=0**。319/51000 files、38313/203932 blocks。診断全文は `/home/hight/os32-tmp/ext2-x2/e2fsck_n.txt`。
- **判定**: **現在の構造は健全で、errors の印だけが残っている**。x1 のとおり印は 9/29 以前から持ち越しており、過去の一時的なメタデータ I/O 失敗または rename の中断で立って、消す経路が無いまま残ったと考えるのが自然 (原因の発生時点は未確定)。T1 / T2 の変更による構造の破壊を示す証拠は無い。
- **残り**: 印を消すには NHD の superblock を書き換える必要があり ([D2] の承認対象)。承認後は保全コピーを残したまま、停止中の NHD に対して e2fsck (修復) を 1 回かけ、前後の hash・診断・起動ログの警告の消失を記録する。原因の発生時点を突き止める x3 (別の像で操作を再生) は、印が再び立つかを監視する形に切り替えるかを PM が判断する。

### 4-5. 修復 (ユーザー承認 2026-10-01「エラーを消して。事の経緯は記録」)

- **保全**: NP21/W を `np21w_ctl.py stop` で止め、修復の直前の NHD を `/home/hight/os32-tmp/ext2-repair/os32.nhd.pre-repair` に複製 (sha256 `36656e0085f75e2cd860891f409b62d4c958df3cba95285bdf9f2981100894b3`)。
- **区画の修復 (写しの上)**: 複製からオフセット 1633 セクタ (836,096 バイト)・407,864 セクタを `/home/hight/os32-tmp/ext2-repair/ext2.img` に取り出し、`e2fsck -fy` (e2fsprogs 1.47.2) を 1 回。rc=1 (修正あり)。変更は 3 つ: superblock の state を **clean** に (errors の印を消す)、**UUID の生成** (OS32 の ext2 は UUID をゼロで作る)、**`/lost+found` の作成** (inode 1・ブロック 1、配備の道具 `tools/deploy_protect.py:73`・`tools/hostdrv_deploy.py:620` はルート直下の lost+found を飛ばす)。Pass 1〜5 の構造の指摘は 0 件のまま。修復後の `e2fsck -fn` は rc=0。診断の全文は `/home/hight/os32-tmp/ext2-repair/e2fsck_fy.txt`・`e2fsck_after_n.txt`。
- **NHD への書き戻しは未実施**: 修復した区画を NHD の同じ位置 (`dd ... seek=1633 conv=notrunc`) へ書き戻すコマンドが、Claude Code の自動モードの安全判定 (取り返しのつかないローカルの破壊) で拒否された。NHD は修復前のまま (NP21/W は起動し直した)。書き戻しはユーザーの手で行うか、権限の設定を変えてから PM が行う。
