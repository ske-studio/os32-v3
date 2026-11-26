# userland/system/hsync.c — 同一判定・内容比較・コピー後の検証

> 状態: **現行 (2026-11-18)** — 出所: HSYNC_IMPROVEMENT_PLAN・TASK_H3 の契約を今のソースと照らして要約
ソース: `userland/system/hsync.c` (`sync_file`・`compare_files`・`copy_body`・`verify_readback`・`apply_mtime`・`sync_directory`・`dst_dir_type_ok`・`start_point_ok`・`main`)

この文書は「写すかどうかを決める」と「写したものを確かめる」を扱う。
一時ファイルによる置き換えは同じ置き場の `hsync_replace.md`、配備の名札は `hsync_manifest.md`、
settings.db などの保護は `hsync_protect.md` にある。HostDrv が返す stat の規則は
`docs/design/drivers/hostdrv/hostdrv_stat_rules.md` にある。

## 原則

- **読めなかったことを「同じ」「無い」「空」と読み替えない。** 失敗は `errors` に数え、終了コードを非ゼロにする。
- 理由は `HR_*` の**固定文字列**で出す (このマクロ以外で組み立てない)。試験は文言ではなく理由コードで判定する。
- mtime の 0 は「不明」。現行 ABI に時刻の有効性ビットが無いので、0 を同一の根拠に使わない。

## 1 ファイルの判定順 (`sync_file`)

保護・範囲の判定はこの前に `sync_directory` で済んでいる (内容を開く前、mkdir の前)。

| 順 | 条件 | 結果 |
|---|---|---|
| 1 | コピー元の stat が失敗 | `io_error` |
| 2 | コピー元の種別が 0 / 通常ファイル以外 | `type_unknown` / `type_conflict` (特殊ファイルを通常ファイルとして読まない) |
| 3 | コピー元が `HS_MAX_FILE_SIZE` (0x7FFFFFFF) を超える | `size_unsupported` (切り詰めない) |
| 4 | 宛先の stat が NOTFOUND | コピー (`new_file`) |
| 5 | 宛先の stat がそれ以外で失敗 | `io_error` |
| 6 | 宛先が通常ファイルでない | `type_conflict` (消さない・切り詰めない) |
| 7 | `-f` | コピー (`forced`)。省くのは比較だけで、保護・型検査・コピー後の検証は省かない |
| 8 | サイズが違う | コピー (`size_changed`) |
| 9 | サイズが同じで、両側の mtime が 0 でなく等しく、`--verify` が無い | **1 バイトも読まずに** `unchanged` (`size_mtime_same`) |
| 10 | サイズが同じで、上以外 (どちらかの mtime が 0・mtime が違う・`--verify`) | 内容をバイト比較する |

比較の結果:

- 違う → コピー (`content_changed`)。mtime の新旧は問わない (古い日時のコピー元でも写し、その古い日時を宛先へ保存する)。
- 比較中の I/O 失敗・予定サイズに届かない EOF → `io_error`。**同一扱いにしない。**
- 同じ → 宛先の mtime がコピー元と「一致していると言えない」(コピー元が 0 の場合を含む) なら、
  本体を書かずに mtime だけ直す (`MTIME <path> reason=mtime_only`、`metadata_updated`)。
  一致していれば `unchanged` (`content_same`)。

**既定で見逃すもの**: サイズも mtime も同じで中身だけ違うファイル (時刻を保存する複製、同じ 1 秒の中の 2 回のビルドなど)。
`--verify` は 9 を通らず全件を比較する。この見逃しは仕様で、試験はそのまま固定している。

## 内容比較 (`compare_files`)

- 64KB のバッファを 32KB ずつ (`cmp_a` / `cmp_b`) に割って両側を読む。ファイル全体を確保しない。
- `read_fill` が short read を詰めるので、両側の読み取りの分割が違っても比べるオフセットと有効長が揃う。
- 戻り値 0 = 全内容一致 / 1 = 不一致 / -1 = I/O 失敗・早期 EOF。予定サイズまで一致した後にまだ続きがあれば 1 (stat の後で伸びた)。
- CRC ではなくバイト比較。CRC の関数が違う内容に同じ値を返しても、比較は違いを見つける。
- 呼ぶたびに `g_content_compares` を数える (-v で `content_compares=` を出す)。名札を信じて比較を省いていないことの観測窓。

## コピー中の CRC と読戻し検証

- CRC-32 は `lib/crc32_core.inc` のストリーム核 (`crc32_core_update`・`crc32_core_final`)。既存の一括版 `crc32_calc` と同じ値
  (反転多項式 0xEDB88320、初期値 0xFFFFFFFF、最後の XOR は 1 ストリームに 1 回)。既知の値: 空列 `00000000`、`123456789` → `CBF43926`。
  チャンクごとに完成させた CRC を XOR しても全体の CRC にはならない。
- `copy_body` はコピー元を読みながら CRC と総バイト数を作る。書き込みの 0 進捗・負値は失敗、`NOSPC` / `FULL` は `no_space`。
  読めた総数が stat のサイズと違えば `source_changed`。
- `verify_readback` は書いたファイルを開き直し、長さと CRC の**両方**を照合する。不一致・失敗は `verify_failed`。
  その前の `vfs_sync` の失敗も `verify_failed`。読戻しはキャッシュを経由するので、媒体からの物理的な再読の保証ではない。
- コピー・検証の失敗は `copied` に数えない。

## mtime の保存 (`apply_mtime`)

- **データを書き終えてから**設定する (書き込みは mtime を現在時刻で上書きする)。置き換え経路では一時ファイルに設定してから rename する。
- 戻り値 1 = 設定した / 0 = 省略した (失敗ではない) / -1 = 有効な時刻の設定に失敗した。
  - コピー元の mtime が 0 → 書かない (`mtime_unknown`)。
  - 宛先 FS が `sys_set_mtime` に `NOSYS` → 書かない (`mtime_unsupported`)。内容の同期は続ける。
  - それ以外の失敗 → `FAIL <path> reason=metadata_failed`、`errors` に数えて非ゼロ終了。
- 省略は集計の 6 区分とは別に数え、集計の後に `NOTE:` の行で件数を**必ず**出す (-v なら 1 件ずつ `NOTIME`)。

## dry-run (`-n`)

- mkdir・書き込み・一時ファイル・予約名の削除・明示の `vfs_sync`・mtime の設定をしない
  (`sys_write`・`sys_mkdir`・`vfs_sync`・`O_CREAT` の open が 0 回)。読み比べと型検査は行う。
- 予定は `PLAN <path> reason=<r> size=<n>`。`copied` と `metadata_updated` は予定件数で、集計行にそう書き添える。
- 断る門 (`kernel_too_old`・`dest_on_fd`・名札の門) は dry-run でも同じ。

## 範囲と経路の検査

- 引数: 知らないオプションはエラー、dir は 1 つだけ (2 つ目はエラー)。
- dir に `\` があれば `bad_name` で断る。OS32 の区切りは `/` だけなので `..\x` は 1 要素として `..` の検査を通るが、
  HostDrv の先では区切りに化けて同期元の外を指す。列挙で得た名前に `\` があれば取り込まず、ディレクトリごとに `bad_name` で errors。
- dir は `hsp_normalize` で正規化してから使う。root の外へ出る・長すぎるものは断る。`/` (`hsync .`) は全体同期と同じ。
  `/host` とその下は同期元そのもの (自己コピー) なので断る。
- 要素数は `/host` を前置した**後**で見る。`HS_MAX_PATH_DEPTH` (32) を超えたら `path_too_deep` で errors。
  `sys/fs/vfs.c` は上限を超えた要素を黙って捨てるので、超えたパスは別のパスとして成立してしまう。
  32 は `sys/fs/vfs.h` の `VFS_MAX_PATH_DEPTH` の写しで、`test_hsync_h1.py (旧版 5528cf72894a)` が一致を検査する。
- `/sys` の既定除外は**全体同期のルート直下だけ** (`default_sys_exclusion`、-v で表示)。`hsync usr` の `usr/sys` は除外しない。`-f` でも解除しない。
- 保護の判定で正規化できないもの (判定できないもの) は `path_rejected` の errors にし、`PROTECTED` と呼ばない。
- 列挙 (`ls_cb`) は名前と種別だけを私有の `FileList` へ写し、コールバックの中で FS に触らない。
  `MAX_FILES` (128) を超えた分・`NAME_CAP` (64) に入らない名前は切り詰めずに数え、errors にする。
  `sys_ls` が負値なら `FAIL: ls` で errors (途中で切れた列挙を成功と呼ばない。dry-run でも同じ)。深さ `MAX_DEPTH` (8) を超えたら errors。

## ディレクトリの型 (`dst_dir_type_ok`・`start_point_ok`)

- 子のディレクトリ: `sys_mkdir` が `EXIST` を返したら宛先を stat し、ディレクトリであることを確かめてから入る
  (ext2 は種別を問わず `EXIST` を返す)。通常ファイル等は `type_conflict`、stat の失敗は `io_error`。
  dry-run は mkdir をしないが、この型検査は行う。mkdir のそれ以外の失敗は errors。
- 明示 dir の起点: 同期を始める**前に 1 度**確かめる (空のディレクトリでは子の検査が走らないため)。
  コピー元は stat 失敗 `io_error`・種別 0 `type_unknown`・ディレクトリ以外 `type_conflict` (`sys_ls` のエラー任せにしない)。
  宛先は子と同じ検査。食い違えば同期を始めず、集計行と終了コードは通常の末尾を通る (FAILED、非ゼロ)。
  通常 / dry-run / `-f` のどれでも同じ。全体同期の起点は検査しない。

## 集計と終了

- 集計行は `copied unchanged excluded protected metadata_updated cleaned errors`。errors が 1 以上なら頭が `FAILED:`、終了コード 1。
- dry-run 以外は最後に `vfs_sync` を呼び、失敗は errors。
- 出力の形: `UPDATE <path> reason=<r> size=<n>`、`SAME <path> size=<n> reason=<r>` (-v)、`MTIME <path> reason=mtime_only size=<n>`、
  `PLAN …`、`FAIL <path> reason=<r> err=<n>`。
- `/sys` か `/boot` の内容が入れ替わったら、ディスクを更新しただけで稼働中の版は切り替わっていないこと、
  シェルの再起動 / 再起動が要ることを `NOTE:` で出す。mtime だけの更新では出さない。自動では再起動しない。

## 範囲の外

- インベントリ (`hsync_inventory.inc`) が使えるときは列挙とコピー元の stat を表で置き換え、同サイズの比較を宛先のローカル CRC と表の CRC で行う (`compare=crc`)。
  上の内容比較の規則は `compare=legacy` の経路のもの。
- HostDrv がホストの `LastWriteTime` を返さない実装なら全件が mtime 0 になり、全件を内容比較する (遅いが正しい) 側に落ちる。
