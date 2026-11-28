# ext2 formatter の媒体解決（案 A）

## 1. 適用範囲

基点は `456e108f2`。本線の段 1〜6 は PC-98 の媒体選択を提供者に移し、二入口の戻り値、I/O、書き込む媒体の内容を保つ。PowerMac の登録と ATA の暗黙入口の変更は段 7〜10 で行う。両機種でシェルの `format` は共有する。

## 2. 共通の口と解決順

ext2_medium_ops の末尾に照合関数 `int (*claims)(int drv);` を追加。契約コメントに「LOOP の claims 欠落は登録時に −1。HD は claims を持たず 0 可、選択時にも呼ばない」。起動時の固定二枠で、範囲外種別・NULL ops・二重登録の拒否を保つ。協調切替で優先度・動的解除・世代・ロックを追加しない。

`loop_claims(drv)` の範囲は `OS32_DRV_LOOP(0) <= drv && drv < OS32_DRV_LOOP(LOOP_MAX_SLOTS)`。登録済みの LOOP が番号を引き受ければその提供者を返し、それ以外は HD の登録値を返す。

- 枠数の正典は sys/include/loop_contract.h の LOOP_MAX_SLOTS（4）。slot への変換は提供者内で drv − OS32_DRV_LOOP(0)、減算は照合成功後のみ。
- claims は番号のみ照合し、attach/detach や I/O と独立。選んだ ops の全コールに生の drv を渡す。未登録 HD への解決結果は NULL。

## 3. 二入口の互換表 (PC-98 は基点＝候補、PowerMac は末尾に変更前後)

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

- pm_loop_boot の loop_dev_init() 直後に §7 の条件で ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops)。
- install_backend.c の find_part を無条件 EXT2_ERR_INVAL の提供者関数へ置換、approved_table.root とその代入を除く。plan の root_ext2 は残す。
- 存在する ATA への許可済み暗黙入口は予約済み・長さ一致でも find_part で終了し書込 0。明示は raw unit の物理座標で既存の予約照合・転送・同期・失敗処理。inst_hdd.c のインストール経路 は format_at を呼ぶので find_part を経由しない。
- U2 により PowerMac の HD の format はインストーラーの経路 (format_at)、ext2_format は loop だけ。共有シェルの format (userland/shell/cmd_sys.c) は従来どおり ext2_format を呼び、存在する HD では INVAL を表示。機種分岐は加えない。

## 5. 固定値の所在

 platform_install_medium() && drv < VFS_DEV_UNIT_MAX を維持。PowerMac ATA の raw unit はすべて 0 以上この上限未満で予約管理の対象、公開 LOOP 番号はすべて上限外で通常転送。

暗黙入口の群上限は EXT2L_MAX_SECTORS(EXT2_MAX_GROUPS) = (1 + 32 × 8192) × 2 = 524,290 セクタ。最小配置は EXT2L_MIN_BLOCKS の 64 ブロック。

## 7. 実装段と担当

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

- 本線: 基点と候補に同じ媒体・入力、戻り値、各 read/write の順・装置・開始・個数、媒体差分を比較。NP21/W は使い捨て HD と ext2 上の loop 像で二入口・頭打ち・番兵・再起動。ホストは実 formatter・PC-98 実提供者を管理なしで、§3 全行、HD 有無、正常/欠損表、開始 17/18、長さ 0・容量超過・群上限前後。小媒体の BIOS 区画を開始 17 に置き暗黙成功・同じ開始の明示 INVAL を比較。再装着で旧像全体・新像範囲前後・HDI/NHD ヘッダを照合。
- PPC: ppc_ata_guest の mac99、install_begin の直後 ACTIVE で暗黙の ext2_format(0, ROOT_BLOCKS) が INVAL・raw_write の差 0 → 同じ予約で ext2_format_at → hd0 の再マウントと番兵。loop の backing は RAM 側の /tmp、全域 (開始 0) と部分 (開始 8・長さ 128) を 1 つずつ、検査専用の読み取りビューで起点ごとにマウント。変異 ppc_fmt_allow_implicit は install_begin の plan.root_ext2 を試験用の u32 2 つに保存し find_part が返す。

| 登録 id | 構成・観測 | 変異 |
|---|---|---|
| ext2_medium_select_host（新規） | 実 formatter と PC-98 の HD/loop 提供者、記録用下層で両入口の結果・装置・LBA・順・回数 | ext2_select_attached: 未装着 lo0 と存在する hd0、暗黙の即時 IO が HD の LBA 1 読取へ流れると失敗。ext2_select_mask: drv=0x0100・有効な明示範囲・書込可能な hd0 で、生番号拒否が低 8 bit 化で成功・書込に変わると失敗 |
| ppc_ata_guest（既存） | 管理付き ATA と loop、番兵・再マウント | ppc_fmt_skip_loop: LOOP 登録を外し装着済み lo の二入口の成功を失う。ppc_fmt_allow_implicit: find_part が承認済み root を返し、有効予約と同一長で暗黙が転送に達する |

## 9. 公開と受入

U1 は決定済み:「公開する (実装対象に戻す)」([PowerPC の公開設計](../ppc/PPC_KAPI_LOOP_EXT2_DESIGN.md) の「ユーザー決定 (2026-11-20 夕)」と「4-2」の D3)。受入は QEMU (mac99 の IDE) が先、G4 の実機は後。実機の HDD への書き込みは個別に承認。未決は 0 件。
