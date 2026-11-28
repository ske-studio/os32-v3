# ext2 formatter の媒体解決（案 A）

## 1. 適用範囲

基点は `456e108f2`。本線の段 1〜6 は PC-98 の媒体選択を提供者に移し、二入口の戻り値、I/O、書き込む媒体の内容を保つ。PowerMac の登録と ATA の暗黙入口の変更は段 7〜10 で行う。両機種でシェルの `format` は共有する。

## 2. 共通の口と解決順

ext2_medium_ops の末尾に照合関数 `int (*claims)(int drv);` を追加。契約コメントに「LOOP の claims 欠落は登録時に −1。HD は claims を持たず 0 可、選択時にも呼ばない」。起動時の固定二枠で、範囲外種別・NULL ops・二重登録の拒否を保つ。協調切替で優先度・動的解除・世代・ロックを追加しない。

`loop_claims(drv)` の範囲は `OS32_DRV_LOOP(0) <= drv && drv < OS32_DRV_LOOP(LOOP_MAX_SLOTS)`。登録済みの LOOP が番号を引き受ければその提供者を返し、それ以外は HD の登録値を返す。

- 枠数の正典は sys/include/loop_contract.h の LOOP_MAX_SLOTS（4）。slot への変換は提供者内で drv − OS32_DRV_LOOP(0)、減算は照合成功後のみ。
- claims は番号のみ照合し、attach/detach や I/O と独立。選んだ ops の全コールに生の drv を渡す。未登録 HD への解決結果は NULL。

## 3. 二入口の互換表 (PC-98 は基点＝候補、PowerMac は末尾に変更前後)

PowerMac の行・段 9・10 は §11 が置き換える。

「正常」は書き込みができ、配置が成り立つ入力。

| 入力・状態 | ext2_format | ext2_format_at |
|---|---|---|
| HD 0〜3、正常 | OK／OS32 区画先頭から要求・群上限・区画長で切った配置内 | OK／明示範囲の配置内 |
| HD 不在、または HD 提供者なし | IO(-1)／0 | IO／0 |
| HD 存在、区画なし | NOPART／0（表読出し失敗は IO） | 有効な明示範囲なら OK |
| 4・0x0604 | present・幾何は drive & 3 で hd0。通っても装置名は低 8 bit で hd4。未登録なら LBA 1 の前に IO／0 | hd0 不在・容量の取得の失敗は IO、それ以降は引数か生の番号の range_ok で INVAL／0 |
| −1 | present は hd3。不在ならすぐ IO。存在して幾何が取れても、装置名の末字は '0' + (drv & 0xFF) の char の値で、この名が未登録なら表を読む前に IO／0 | hd3 不在・容量の取得の失敗は IO、それ以降は INVAL／0 |
| 0x0100・0x10000、LOOP 未登録の 0x0600 | hd0 の present・幾何と装置名 hd0 の解決を通れば LBA 1 を読む。読み取りの失敗は IO、探索で NOPART・INVAL、配置が足りなければ NOSPC、配置が成り立てば生の番号の range_ok で INVAL／全て 0 | hd0 不在・容量の取得の失敗は IO、有効な範囲でも生の番号の range_ok で INVAL／0 |
| LOOP 未登録の他の lo 番号 | 生番号で HD 行へ | 同左 |
| HD も LOOP も未登録 | IO／0 | IO／0 |
| loop 登録済み、未装着 / detach 後 | IO／0 | IO／0 |
| 装着済み、読取専用 / D88 / 非 512 B | INVAL／0 | INVAL／0 |
| lo0〜lo3 各々、正常 | OK／0 起点、要求長で決めた配置内 | OK／指定開始から配置内 |
| 各枠を別容量・別像に再装着 | 新容量で配置し新像だけに書く | 新容量で境界を判定し新像だけに書く |
| 書込可能な HDI/NHD | データ開始が論理 LBA 0。ヘッダ保存 | データ開始＋指定 LBA から配置内。ヘッダ保存 |
| PowerMac ATA 不在 | IO／0（従来どおり） | IO／0 |
| PowerMac ATA 存在 | 承認にかかわらず INVAL／0。従来は未承認なら NOPART、承認済みで予約・長さが揃えば成功 | 既存の予約付き明示経路を維持 |

明示の入口は、要求の範囲が有効でも、配置が最小のブロック数に足りないか群の上限を超えれば NOSPC／0。

PC-98 暗黙 HD: present → 幾何取得 → 機器名から装置取得 → LBA 1 を 1 セクタ読む（ここまで IO）→ BIOS 区画幾何 → OS32 区画探索。幾何なし・区画なし NOPART。IDENTIFY 幾何で区画が見つかれば診断して INVAL。成功時のみ群上限で切って共通書込へ。暗黙には開始下限 18 を適用しない。
PC-98 明示 HD: present → 容量取得（IO）→ 長さ非零 → 開始≧18 → 容量非零 → 開始＜容量 → 長さ≦容量−開始 → 生 drv で要求全域の range_ok（INVAL）→ 共通書込。表読取なし。
共通書込: 再 present（IO）→ 区画長で切詰め → layout（NOSPC）→ 生 drv で計画範囲の range_ok（INVAL）→ 装置取得（IO）→ I/O。PC-98 HD は各書込前に読取キャッシュ無効化、失敗時再無効化、共通書込中の読取失敗でも既存の二回の無効化を維持。

## 4. PowerMac 後端

PowerMac の行・段 9・10 は §11 が置き換える。

- pm_loop_boot の loop_dev_init() 直後に §7 の条件で ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops)。
- install_backend.c の find_part を無条件 EXT2_ERR_INVAL の提供者関数へ置換、approved_table.root とその代入を除く。plan の root_ext2 は残す。
- 存在する ATA への許可済み暗黙入口は予約済み・長さ一致でも find_part で終了し書込 0。明示は raw unit の物理座標で既存の予約照合・転送・同期・失敗処理。inst_hdd.c のインストール経路 は format_at を呼ぶので find_part を経由しない。
- U2 により PowerMac の HD の format はインストーラーの経路 (format_at)、ext2_format は loop だけ。共有シェルの format (userland/shell/cmd_sys.c) は従来どおり ext2_format を呼び、存在する HD では INVAL を表示。機種分岐は加えない。

## 5. 固定値の所在

 platform_install_medium() && drv < VFS_DEV_UNIT_MAX を維持。PowerMac ATA の raw unit はすべて 0 以上この上限未満で予約管理の対象、公開 LOOP 番号はすべて上限外で通常転送。

暗黙入口の群上限は EXT2L_MAX_SECTORS(EXT2_MAX_GROUPS) = (1 + 32 × 8192) × 2 = 524,290 セクタ。最小配置は EXT2L_MIN_BLOCKS の 64 ブロック。

## 7. 実装段と担当

PowerMac の行・段 9・10 は §11 が置き換える。

| 段 | 担当・ファイル | 変更 |
|---|---|---|
| 1 | 本線 sys/include/loop_contract.h | 既存契約に枠数を追加 |
| 2 | 本線 sys/dev/block/loop_dev.c | 私有の同一定義を除き共有定義を参照 |
| 3 | 本線 sys/include/ext2_medium_contract.h | 末尾に claims、既定 HD と生 drv の契約 |
| 4 | 本線 sys/dev/block/ext2_medium_loop.c | claims と ops、五か所の 0x0600 を OS32_DRV_LOOP(0) に、slots[4] を slots[LOOP_MAX_SLOTS] に、減算は照合成功後 |
| 5 | 本線 sys/fs/ext2/ext2_fmt.c | LOOP 登録時に claims の有無を検査、番号分岐を §2 に置換 |
| 6 | 本線 試験 | §8 のホスト検査、NP21/W 比較 |
| 7 | PPC build/sources/common.mk | FEATURE_PC98_PENDING + FEATURE_LOOP を FEATURE_LOOP=1 に、ext2_medium_loop.c の C_COMMON_LOOP と C_ORDER_TAIL を同条件 |
| 8 | PPC sys/platform/powermac/loop_boot.c | 登録参照を OS32_FEATURE_IDE で囲む。C_EXT2_FORMATTER は IDE 選択のまま |
| 9 | PPC install_backend.c | §4 |
| 10 | PPC 既存 ppc_ata_guest | §8 の観測と変異 |

段 1〜6 は各段で既存登録とリンクを保つ。段 3 は欄の追加のみ、段 4 で LOOP が実装してから段 5 で必須化と選択変更を同時。formatter に装置内部・機種分岐を入れない。実装後 include_layers・link_layers・src_owner・build_sources・ppc_common_compile。PowerMac は LOOP のみ / IDE のみ / 両方 / 両方 0 の各場合を記述、現行像は両方 1 か両方 0 なので単独構成の像・検査は新設しない。

## 8. 受入と登録検査

PowerMac の行・段 9・10 は §11 が置き換える。

- 本線: 基点と候補に同じ媒体・入力、戻り値、各 read/write の順・装置・開始・個数、媒体差分を比較。NP21/W は使い捨て HD と ext2 上の loop 像で二入口・頭打ち・番兵・再起動。ホストは実 formatter・PC-98 実提供者を管理なしで、§3 全行、HD 有無、正常/欠損表、開始 17/18、長さ 0・容量超過・群上限前後。小媒体の BIOS 区画を開始 17 に置き暗黙成功・同じ開始の明示 INVAL を比較。再装着で旧像全体・新像範囲前後・HDI/NHD ヘッダを照合。
- PPC: ppc_ata_guest の mac99、install_begin の直後 ACTIVE で暗黙の ext2_format(0, ROOT_BLOCKS) が INVAL・raw_write の差 0 → 同じ予約で ext2_format_at → hd0 の再マウントと番兵。loop の backing は RAM 側の /tmp、全域 (開始 0) と部分 (開始 8・長さ 128) を 1 つずつ、検査専用の読み取りビューで起点ごとにマウント。変異 ppc_fmt_allow_implicit は install_begin の plan.root_ext2 を試験用の u32 2 つに保存し find_part が返す。

| 登録 id | 構成・観測 | 変異 |
|---|---|---|
| ext2_medium_select_host（新規） | 実 formatter と PC-98 の HD/loop 提供者、記録用下層で両入口の結果・装置・LBA・順・回数 | ext2_select_attached: 未装着 lo0 と存在する hd0、暗黙の即時 IO が HD の LBA 1 読取へ流れると失敗。ext2_select_mask: drv=0x0100・有効な明示範囲・書込可能な hd0 で、生番号拒否が低 8 bit 化で成功・書込に変わると失敗 |
| ppc_ata_guest（既存） | 管理付き ATA と loop、番兵・再マウント | ppc_fmt_skip_loop: LOOP 登録を外し装着済み lo の二入口の成功を失う。ppc_fmt_allow_implicit: find_part が承認済み root を返し、有効予約と同一長で暗黙が転送に達する |

## 9. 公開と受入

U1 は決定済み:「公開する (実装対象に戻す)」([PowerPC の公開設計](../ppc/PPC_KAPI_LOOP_EXT2_DESIGN.md) の「ユーザー決定 (2026-11-20 夕)」と「4-2」の D3)。受入は QEMU (mac99 の IDE) が先、G4 の実機は後。実機の HDD への書き込みは個別に承認。未決は 0 件。

ユーザー決定 ②（2026-11-28）で U2 は取り消された。

## 10. ユーザー決定 (2026-11-28) — format の UX を両機種で揃える

ユーザーの問い (原文):「なぜformatコマンドを通らない？UX/UIが統一されないと」。選択肢「断って理由を出す / シェルからも format できる / 両機種とも HD はインストーラー」から「**シェルからも format できる**」を選んだ。

- §4 の U2 (PowerMac の暗黙の入口は存在する ATA に INVAL、find_part を無条件 INVAL) を取り消す。PowerMac でもシェルの `format 0` で HD を format でき、内部ではインストーラーと同じ予約の照合 (承認・予約・長さ) を通す。照合が成り立たない時は理由を出して何も書かない。
- シェルの `format` の綴りは両機種で同じ: `0`〜`3` と `lo0`〜`lo3` だけを受け、それ以外は使い方を出して何も書かない。
- 受入は両機種ともシェルの `format` を通す (losetup → format lo0 → mount → 読み書き、HD は使い捨ての像)。KAPI を直に呼ぶ検査は境界の補助として残す。
- §3 の PowerMac の行・§4・§8 の PPC の部分は r4 の差分で書き直す (PowerPC が起草、本線が読み直して確定)。段 9・10 はそれまで止める。

## 11. シェルの format と PowerMac の HD（r4 確定、2026-11-28）

ext2 媒体解決（案 A）r4 確定版 (final = r4b + r4c + r4d + 本線 sol 第 3 回の P2、PM 反映)

### r4c→r4d の変更

- fable P2-1　HD の許可照会と mount 数検査を LOOP から分離。
- fable P2-2　install の全エラーを提供者で変換し、9c で BUSY を定義。
- fable P3-1　unit 1 は hd1 に公開し、hd0 と独立に扱う。
- fable P3-2　mac99 の RAW_ONLY 再試行を追加できる条件を明記。
- fable P3-3　本線の手順書追従を段の表に追加。
- sol P1　HD の許可拒否はシェルで表示し、公開 KAPI の -1 を維持。
- sol P2-1　暗黙 format 専用の区画判定を 9c・9d に分担。
- sol P2-2　旧 Device の参照が尽きるまでの寿命を規定。
- sol P2-3　復旧結果と次回の入場可否を区別。
- sol P3　本人予約の長さ不一致と AVAILABLE 枝の変異を受入に明記。

### 1. 現物と利用者の操作

- 現行の `userland/shell/cmd_sys.c` の `cmd_format` は省略時に 2880 を渡す。現物は先頭一文字を数値化する。本線の完全一致修正を前提とする。
- `sys/fs/ext2/ext2_fmt.c` の `ext2_format` は HD の存在を確認し、find_part の失敗をそのまま返す。管理媒体では要求長と取得長が等しい場合だけ format_at へ進む。
- `sys/platform/powermac/install_backend.c` の `validate`・`find_part` で validate に成功すると root の開始・長さが残る。起動直後の通常シェルでは root が空なので NOPART。APM を置くだけでは承認にならない。
- root が残っていても長さ不一致なら INVAL。一致しても `sys/kern/dev/install_management.c` の `install_format_begin` で ACTIVE・同じ owner・範囲などが揃わなければ BUSY。
- シェル組込みに install_begin の呼出しはない。hdprep も明示 format_at（`userland/shell/cmd_hdprep.c` の `cmd_hdprep`）。別プロセスで先に予約しても owner が一致しない。
- PC-98 は BIOS 幾何と LBA 1 の OS32 区画から開始・長さを得る。暗黙入口は要求長を群上限・区画長で切る。PowerMac は既存 APM の root 全長を予約し、superblock のブロック数まで検証する（`sys/platform/powermac/install_backend.c` の `verify`）。

| POLICY_DEV §1 の判断・UX | PC-98 | PowerMac |
|---|---|---|
| ①同じ役目 | 既存 OS32 HD 区画の ext2 作成 | 同じ。追加の前提は APM の検証と予約 |
| ②統合する境界 | 共通シェル・配置・表示、下層で BIOS 区画探索 | 同じ入口・配置・表示、下層で APM と予約管理 |
| ③確定設計との整合 | 案 A の媒体優先順と直接二入口の挙動を保存 | 同じ優先順。U2 のみ撤回、明示入口の所有者検査を再利用 |
| 利用者が打つもの | `format 0`、loop は `format lo0` | 同左。事前コマンドも追加の問いも不要 |
| 引数 | 0〜3・lo0〜lo3 の完全一致。SECTORS は十進 1〜4294967295、省略は 2880 | 同左 |
| HD の FS サイズ | 区画先頭から L = min(要求, 区画長, 群上限) を配置に渡す。format 0 は 4000 セクタ区画の頭 2880 に 1440 ブロック、2400 セクタ区画には 1200 ブロック | 予約 root の先頭から同じ式。明示 N も同じ規則。実ブロック数は双方とも ext2_layout_plan(L) の結果 |
| 開始・成功表示（案） | `Formatting 0...` → `Format complete.` | 同左。対象名には検証済みの入力を使う |
| 拒否表示（案） | `Format failed: <理由> (<rc>)` | 同左。同じ理由は同じ文字列 (§2) |
| 綴り不正 | 本線の共通 usage、書込 0 | 同左。HD 0〜3、lo0〜lo3 のみ |
| 対象 HD を mount 中 | BUSY。mount とファイルを保持 | 同左 |
| HD を指定した GUI 端末で書込み許可なし | 許可なし。媒体 I/O なし | 同左 |

- r3 の受入を現物に適用したときの食い違いは 3 件。① ACTIVE・全長一致で暗黙入口は成功する。② 現シェルは lo0 を loop 番号に変換しない。③ /tmp に最小 ext2 像が収まらない。
- AC-BUILD-1 の製品シェル・提供者の結線を両像で確認する。UX は §4 の同じ入力と表示で判定する。

### 2. §3 の末尾二行と §4 の置換

| PowerMac の状態 | ext2_format（既存 KAPI） | ext2_format_at（既存 KAPI） |
|---|---|---|
| ATA 不在 | IO(-1)、書込 0 | IO、書込 0 |
| ATA 存在 | 本人 ACTIVE は要求が root 全長と一致する時だけ承認済み root に作成し TABLE_READY で返す。AVAILABLE・RAW_ONLY は既存 APM から予約し commit まで実行。他人の予約は BUSY | 従来どおり本人 ACTIVE・開始と全長の一致が必要 |

- シェルは許可済み HD または LOOP に対し ext2_format(drv, sects) を一回呼ぶ。LOOP の claims を先に評価し、PC-98 HD と loop は現行経路。管理 HD だけ find_part の前で提供者の固定名の内部関数に渡す。`sys/fs/ext2/ext2_fmt.c` の `ext2_format` は未予約で承認表が空なら find_part で終了するため順序が重要。
- 本人 ACTIVE は既存 handle・owner・開始・全長で format_at を通す。root 4000 に要求 2880 は INVAL・書込み 0、元の ACTIVE を継続。他 owner と遷移中は BUSY。自動予約の入場は install_begin に任せ、取得した自分の handle だけを完了・破棄する。
- 自動予約は begin 前に現媒体から root を特定し、容量・開始・全長の範囲、L と ext2_layout_plan(L) を検査する。G は EXT2L_MAX_SECTORS(EXT2_MAX_GROUPS)。承認は root 全長、共通 ext2_format_range に渡す配置長は L。begin の validate でも媒体と計画の一致を確認する。
- `sys/kern/dev/install_management.c` の `install_format_begin` の begin は短い予約を拒むので全長で入場する。`sys/kern/dev/install_management.c` の `install_format_transfer`・`transfer`・`install_transfer_allowed` の transfer は発行範囲内の個別転送を許し、`sys/kern/dev/install_management.c` の `install_format_finish` の finish は sync と状態のみを確認する。短い FS にこの三つの変更は不要。
- `sys/platform/powermac/install_backend.c` の `validate` の validate は群上限超えの root を予約前に拒む。ここは min(root 長,G) で配置可能性を調べ、APM・予約の範囲は全長を保持する。`sys/platform/powermac/install_backend.c` の `verify` の verify は全長から期待個数を算出するので短い FS を拒む。approved_table に期待ブロック数を一つ保持し、validate で上記配置の値に初期化、自動予約の format_range 成功直後・finish 前にローカルに候補を取り、finish の sync 成功後、rescan 前に layout_plan(L) の値に更新する。verify はこの期待値との完全一致を要求し、identity・表・magic・ブロックサイズの照合を維持する。rescan と commit は同じ verify を通る（`sys/platform/powermac/install_backend.c` の `rescan`、`sys/kern/dev/install_management.c` の `inspect`）。

- plan は既存媒体の identity、容量、512 B、DDR と現 APM の件数・CRC、map・boot_hfs・root_ext2 の entry/start/sectors からローカルに組む。用途を特定できる既存 OS32 root が一つのときだけ採用する。
- fresh、Apple_Free の転用、表の書換えが必要な配置は対象外。root 不明なら NOPART、壊れた表なら INVAL、読取不能なら IO。表の書込は成功時も 0。 9d の計画作成は ext2 magic を要求する pm_apm_root() を使わず、DDR・APM の構造と範囲を検証してから root の有無を判定する。
- `sys/kern/dev/install_management.c` の `install_begin` が identity・VFS・validate・drain を通して ACTIVE にする。マウント使用中などで予約できなければ format に入らない。残存 tables[unit].root を新規予約の根拠にしない。
- LOOP claims の後、正規 HD 0〜3 のみ対象 unit の mount 実数を調べる。root と FD の無い別名も数え、正なら PC-98 は探索前、PowerMac は自動予約・detach 前に BUSY。範囲外の番号の負の照会結果を BUSY にしない。vfs_install_precheck 単独では別名を検出しきれない。
- 自動予約の成功は TABLE_READY → rescan（表・superblock 読戻し、VIEW）→ commit（再検証・公開）。commit まで成功して初めてシェルに成功を返す。
- begin 後の失敗では自分で取得した handle だけを ABORT する。先行処理が既に復旧を開始して handle を失効させた場合の STALE は先行処理の結果を保持する。復旧の成否・再入場は §3 の状態表による。上書きした root の内容は復元できない。
- PowerMac の format N は物理 IDE unit N を選ぶ。bind はその root の開始と全長を持つunit ごとの論理 hdN（format 1 は hd1） を登録する。通常の pm_install_publish は読取専用のまま。自動予約の commit 成功直後、提供者内で既存 writable を立てる条件を、登録 hdN == current[N]、bus_id == N、view_generation[N] == 今回の handle とする。既存 view_write の範囲・現行ポインタ検査を通し、古い view・生ディスクの書込みを許さない。§10 上、利用者の mount 後の書込みにはこの公開切替だけが必要で、追加の状態・ロック・世代・KAPI は不要。
- 9d は開始・書込み可否・現行 Device も unit ごとに持つ。探索・bind・invalidate・公開は対象 hdN だけに作用する。hd0 が起動用でも未 mount の unit 1 は format できる。
- 旧 Device の寿命（sol P2-2、第 3 回で簡素化）: 参照数は持たない。順は ① その物理 unit に属する旧ビューの全 mount の解体（unmount が FD を失効させ fs_ctx をクリアしてから解放、`sys/fs/ext2/ext2_vfs.c` の `ext2_vfs_mount`・`ext2_vfs_umount`・`sys/fs/vfs_fd.c` の `vfs_fd_invalidate_mount`）② invalidate で登録解除と現行参照の除去、旧 view の I/O は STALE ③ 次の bind の時に、①②の済んだ同じ unit の動的 Device を解放。名前のブロック KAPI は毎回検索する（`sys/kapi/kapi_generated.c` の `wrap_dev_blk_read`）ので旧 FD に view の参照は残らない。固定 view は静的寿命。登録解除だけでアドレスを再利用しない。umount→format→mount 反復で生存割当数が増えないことを検査する。
- 起動ビュー（sol 第 3 回 P2）: 今は物理 unit 1 から起動しても固定ビューを hd0 として登録し / に mount する（`sys/platform/powermac/ide_boot.c` の `pm_root_select`、`sys/platform/powermac/partition_view.c` の `view`、`sys/platform/powermac/root_boot.c` の `pm_root_boot`）。VFS の mount 数は論理 dev_id で数える（`sys/fs/vfs.c` の `vfs_dev_mount_count`）ので、物理 unit 1 の / を見落とし、未使用の unit 0 を BUSY にし得る。起動ビューも hdN 規則（物理 unit N → hdN）に合わせ、起動時の名前・VFS の hd1〜hd3 の認識・mount 検査・detach・invalidate が参照する unit と論理名の対応を 9d で 1 つに揃える。
cmd_format は綴り検査と HD/LOOP の識別後、HD だけ ide_write_sectors(drv,0,0,NULL) で許可を照会する（媒体 I/O なし）。拒否なら理由を表示して終了。両 formatter の KAPI ラッパー先頭の exec_disk_write_allowed() と拒否値 -1 は維持し、LOOP に HD 用照会・mount 検査を追加しない。

| 拒否 | PC-98 の判定 | PowerMac の判定 | format の戻り値と共通表示 |
|---|---|---|---|
| 区画なし | BIOS 幾何なし・旧配置・PC98PT_ERR_NOTFOUND | 有効な表に対象 root なし | NOPART (-13) / no approved partition |
| 表の破損 | 対象項目の CHS・範囲不正 | APM 構造・範囲・重複不正、validate の INVAL | INVAL (-10) / invalid partition or length |
| 容量不足 | 有効な区画で配置不能 | 同じ配置判定を validate の INVAL より先に適用、install NOSPC も変換 | NOSPC (-5) / insufficient space |
| 使用中（mount 中を含む） | mount 数が正 | 同じ物理 unit を mount 中、予約競合 | BUSY (-17) / device busy or reservation unavailable |
| HD の許可なし | シェルの許可照会で拒否 | 同左 | disk write not allowed (-1)。formatter を直接呼んでも -1 |
| I/O | 現行の装置・表読取・書込失敗 | 装置・読書き・同期失敗 | IO (-1) / I/O error |

9c は暗黙 HD format だけで既読の LBA 1 を分類する。幾何なし・旧配置・pc98pt_find_os32 の NOTFOUND は NOPART、RANGE は INVAL。正常時の転送列と mount・既存探索 API の戻り値は維持する。明示 format_at の installer・hdprep は対象外。

提供者は install 呼出しごとに次表で変換する。STALE (-11) と NOSYS (-10) を同値の ext2 ROFS・INVAL と解釈しない。ROFS は既存のエラー状態の mount への書込み拒否に限る。

| install の結果 | ext2 の結果 | シェルの理由と番号 |
|---|---|---|
| IO | EXT2_ERR_IO | I/O error (-1) |
| NOSPC | EXT2_ERR_NOSPC | insufficient space (-5) |
| INVAL | EXT2_ERR_INVAL | invalid partition or length (-10) |
| NOSYS | EXT2_ERR_IO | I/O error (-1) |
| STALE（失効・媒体 identity 不一致） | EXT2_ERR_IO | I/O error (-1) |
| BUSY | EXT2_ERR_BUSY | device busy or reservation unavailable (-17) |
| FULL（予約枠なし） | EXT2_ERR_BUSY | device busy or reservation unavailable (-17) |
| 復旧エラー（identify・復元書込・sync・読戻し・照合） | 復旧が返した install 値を上の対応で変換 | 対応する理由と番号。次回 begin の BUSY とは別に表示 |


### 3. §7 段 9・10

| 小段 | ファイル | 変更 | 担当 |
|---|---|---| --- |
| 9a | sys/kern/dev/install_management.c、install_access.h | 内部で予約状態・owner を照会 | PPC |
| 9b | 両機種の platform_install_management.h | 固定名の内部関数を宣言。PC-98 は同じ固定名の空定義を置き通常 HD 経路を通す。媒体 ops 追加 0 | PPC |
| 9c | ext2_fmt.c、ext2_medium_ide.c、ext2.h、内部宣言 | LOOP 選択後の HD mount 数検査、PC-98 の暗黙区画判定、EXT2_ERR_BUSY(-17) の新設、管理 HD の振分けと共通 range | 本線 |
| 9d | sys/platform/powermac/install_backend.c、partition_view.c、ide_boot.c、root_boot.c | 現 APM の構造・root 判別と計画・予約・完了、期待ブロック数の保持と照合、unit 別 hdN（起動ビューを含む）と旧 Device 回収 | PPC |
| 9e | KAPI | 変更 0 | — |
| 9f | userland/shell/cmd_sys.c | 既存入口のままwt/fs の綴り検査・loop 解決・HD 専用許可照会と共通理由表示。カーネル私有ヘッダは参照しない | 本線 |
| 10a | tests/support/fixtures/ppc_ata/write.inc | 予約境界を実 KAPI で照合 | PPC |
| 10b | tests/system/ppc_ata_guest.py | 境界試験の結果と終了後媒体を照合 | PPC |
| 10c | tests/system/ext2_format_shell.py（新） | 両機種で使うコマンド列・出力期待値、使い捨て像の比較 | 本線（共通列・PC-98 比較）、PPC（mac99） |
| 10d | tests/registry.toml | 下記構成・変異を登録 | PPC |
| 手順書追従 | docs・実機更新・hdprep・install 手順 | format の記載を §1 の HD 許可・umount 条件に合わせる | 本線 |

9b→9c は既存の固定名契約、提供者→formatter は共通宣言を使い、両製品構成で include_layers・link_layers を確認する。

| 失敗後と再実行 | PC-98 | PowerMac |
|---|---|---|
| 入場前の拒否 | mount 状態を保持。原因解消後に再実行 | 同左 |
| 書込み失敗・復旧成功 | FS が不完全なら mount 不可。再 format 可 | RAW_ONLY。元のエラーを表示。生読み可、mount・生書込み不可。再 format で begin に入り、commit 成功で AVAILABLE |
| 復旧保留 | — | RECOVERING の間は begin が BUSY。idle の復旧成功後に再試行可。失効 handle への ABORT の STALE は復旧成功を示さない |
| ABORT 自体の復旧失敗 | — | 元の format エラーより復旧エラーを優先し §2 で変換。identity 不一致は STALE→IO、sync・照合失敗は IO、転送待ちは BUSY。RECOVERING を保持 |

### 4. §8 シェル受入と境界

- 主検査案 `ext2_format_shell`。PC-98 は NP21/W、PPC は mac99 の製品シェル＋IDE＋LOOP。起動媒体と使い捨て HD を分け、対象 hd0 は未マウントで開始する。 NP21/W は CI 登録外の pc98emu 受入。BIOS 幾何を用意し、認可付き常駐 CUI でローカル打鍵と画面を記録する。CI 登録は mac99 のみ。
- PPC は `tests/system/ppc_link_session.py` の `exercise` の vm.keys と画面待ちを使う。コマンドごとに表示・プロンプト復帰を照合する。
- 両機種に正規の既存区画を持つ同長の試験 HD を用意する。長さは 2880 以外の 2400 と 4000 セクタの二組。省略時の SB の blocks_count は順に 1200、1440。4000 の像は format 直後、末尾 1120 セクタも不変。明示 4000 / root 2400 でも 1200 ブロック。別の像で区画長と要求をともに G 超とし layout_plan(G) の個数と G 以降の不変を確認する。
- `format 0` → `mount /hd0 hd0 ext2` → ファイル書込・読戻し・再マウント。前後の区画表、root 両側の番兵、root 内の古いファイル消失と新ファイル保持を照合。 開始時に /hd0 が mount 済みなら先に format 0 が BUSY、既存ファイルが読めることを確認し、umount /hd0 後に同じ成功列へ進む。未 mount 開始でも再マウント後にこの拒否を照合する。
- マウント後の /hd0 に 64 KiB 以上の書込可能な raw ファイルをコピーする。HD 初期化前に用意した backing に依存しない。
- `losetup /hd0/loop.img 0` → `format lo0` → `mount /loop lo0 ext2` → ファイル書込 → umount → 再マウント → 内容一致。最後に umount と `losetup -d 0`。
- 使用不能な区画と綴り不正は同じ表示・媒体全体不変を照合。予約不成立は PPC で使用中の媒体を指定し、BUSY と内容不変を照合する。 GUI 端末でも format 0 を打ち、許可なし表示と媒体不変を照合。綴り不正は usage・I/O なし。区画なし、表破損、有効だが小さすぎる区画は別像で理由表と照合する。
- 補助 `ppc_ata_guest` は mac99 の既存 fixture。`tests/support/fixtures/ppc_ata/write.inc` の `ata_prepare` の明示入口成功・再マウント・番兵を維持する。
- 明示入口は未予約・本人 ACTIVE で短い長さ・他 owner の各拒否で書込 0。本人 ACTIVE の暗黙入口は全長一致のみ成功。root 4000・要求 2880 は INVAL・書込み 0・元の予約継続。短い FS の verify は製品シェルの 4000 区画で rescan・commit・再マウントまで確認する。RAW_ONLY の再試行と旧 view の書込拒否は予約境界で確認する。 mac99 で安価に失敗を注入できれば ext2_format_shell に RAW_ONLY からの format 再成功を一場面追加し、それ以外は予約境界で検査する。別 unit の hd0 を mount したまま format 1→hd1 を確認し、hd1 の FD なし別名 mount 中は BUSY を確認する。 物理 unit 1 から起動した時（/ が hd1）に format 1 が BUSY、未使用の unit 0 の format 0 が BUSY にならないことを確認する。
- 変異 `ppc_fmt_skip_loop` は LOOP 登録を外す → シェルの format lo0 と再マウントが RED。
- 変異 ppc_fmt_deny_approved は提供者の AVAILABLE の枝を BUSY にする → 未予約からの format 0 が RED。
- 変異 ppc_fmt_ignore_root_cap は提供者の L から root 上限を除く。共通 range の再切詰めで書込が収まっても期待個数が過大になり、明示 4000 / root 2400 の rescan 成功検査が RED。
- 変異 ppc_fmt_ignore_group_cap は L の群上限を除く。区画・要求がともに G 超の成功試験が配置失敗で RED。 上記 4 変異はすべて mac99 の登録検査で RED を確認する。

### 5. サイズ

- 基準は 4c91eaf2f＋段 7・8、比較先は r4 実装。本線の綴り修正は両者に同じものを適用する。
- 両機種それぞれ、同じコンパイラ・像・最適化で FEATURE_KDEBUG=0 と 1 の出力先を分けてクリーンビルドする。依存する HWDEV は比較対ごとに揃える。
- 各 ELF の size で text/data/bss、配布カーネルと shell.bin の実バイト数を取り、構成・基準・候補・符号付き差を表にする。合否は kdebug 無しの既存像予算で判定する。

### 6. 未決

0 件。

### 反論

0 件。

### 確定 (2026-11-28)

- 往復: r4b（fable 第 1 回 P1 2・P2 3・P3 4、本線 sol 第 1 回 P1 1・P2 2）→ r4c（fable 第 2 回「P2 2 点で確定可」、本線 sol 第 2 回 P1 1・P2 3・P3 1）→ r4d（本線 sol 第 3 回「全部満たす」、新 P2 1 件）。3 回で確定、4 回目のレビューは置かず実装のレビューで照らす（os32pm）。
- PM が反映: 本線 sol 第 3 回の P2（起動ビューを hdN 規則に、受入に物理 unit 1 起動の format 1 BUSY）と、旧 Device の寿命を参照数なしの順序に簡素化（PPC の提案を本線 sol が十分と判定）。
- 背景のユーザー決定（2026-11-28、本線 PM 経由）: ② PowerMac の HD もシェルの format から format できる（U2 取り消し）。POLICY_DEV §10「機種の間で UX/UI を揃える」。本線の決定（PM の推奨）: mount 中は両機種 BUSY、GUI の端末からの HD の format は両機種とも書込み許可を要求。
- 分担: 本線 9c・9f・10c（共通の列・PC-98）・手順書の追従。PPC 9a・9b・9d・10a・10b・10c（mac99）・10d。順は本線の綴りの直し → 本線 9c・9f → PPC。段 7・8 は PPC で実装済み。
