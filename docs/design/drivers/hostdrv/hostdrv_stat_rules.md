# sys/dev/hostdrv/hostdrv_stat_rules.inc — HostDrv の stat・サイズ・open 失敗・時刻の規則

> 状態: **現行 (2026-11-18)** — 出所: HSYNC_IMPROVEMENT_PLAN・TASK_H3・TASK_FS_TYPE の契約を今のソースと照らして要約
ソース: `sys/dev/hostdrv/hostdrv_stat_rules.inc` (`hdrv_stat_fill`・`hdrv_size_result`・`hdrv_create_status_to_vfs`・`hdrv_filetime_to_unix`・`hdrv_stat_mtime`)・`sys/dev/hostdrv/hostdrvfs.c` (`hdrv_stat`・`hdrv_get_file_size`・`hostdrv_create`)

ハイパーコールを叩く `hostdrvfs.c` はホストで組めないので、応答から結果を決める規則だけを純関数にしてある。
`hostdrvfs.c` はこの `.inc` を取り込んで呼ぶだけで、判定の写しを持たない。取り込む側が全部を使うとは限らないので、
関数には `HDRV_STAT_MAYBE_UNUSED` (GNU の `unused` 属性) を付ける。

原則は 1 つ: **知らない状態・取れなかった値を「無い」「空」「通常ファイル」と言わない。**

## stat (`hdrv_stat_fill`)

`hdrv_stat` は FileBasicInformation と FileStandardInformation を問い合わせ、結果をここで組む。

| 入力 | 戻り値 |
|---|---|
| Basic の問い合わせが失敗 | `OS32_ERR_IO` (種別が分からない。ゼロの `st_mode` を通常ファイルと読ませない) |
| Standard の問い合わせが失敗 | `OS32_ERR_IO` (サイズが分からない。ゼロを空ファイルと読ませない) |
| `EndOfFile` が 0xFFFFFFFF を超える | `OS32_ERR_IO` (64bit を u32 へ黙って切り詰めない) |
| `buf` が NULL | `OS32_ERR_IO` |
| それ以外 | 0。属性のディレクトリビットで `OS_S_IFDIR` / `OS_S_IFREG`、サイズは `EndOfFile` |

Basic 情報は次の問い合わせで共有バッファが上書きされる前に値として退避する。

## 時刻 (`hdrv_filetime_to_unix`・`hdrv_stat_mtime`)

FILETIME (1601-01-01 UTC 起点、100ns 単位) を Unix 秒 (UTC) へ。起点差は 116444736000000000 (100ns) = 11644473600 秒。
変換も比較も 64bit の中間値で行い、秒未満は切り捨てる。**JST の 9 時間を足さない** (ローカル時刻にするのは表示側)。

| 入力 | 結果 |
|---|---|
| 0 (未提供) | 判定できない (戻り値 0、`*out = 0`) |
| 起点差より小さい (1970 年より前) | 判定できない (回り込ませない) |
| 秒が u32 の上限を超える (2106 年以降、0xFFFFFFFFFFFFFFFF を含む) | 判定できない (回り込ませない) |
| ちょうど u32 の上限 (秒未満の端数つきでも) | 0xFFFFFFFF |
| ちょうど 1970-01-01 00:00:00 | 変換できて 0 (= 現行 ABI では不明と区別が付かない。どちらも同一の根拠にならない) |
| `out` が NULL | 0 |

`hdrv_stat_mtime` は Basic の問い合わせが失敗していれば 0 (不明)。`atime` / `ctime` は埋めない (Windows の CreationTime は inode の状態変更時刻ではない)。
ゲストからの mtime の設定 (`set_mtime`) は HostDrv には無い (`NOSYS`)。

## サイズ取得 (`hdrv_size_result`)

`hdrv_get_file_size` は FileStandardInformation だけを問い合わせ、ここで決める。

| 入力 | 戻り値 |
|---|---|
| 問い合わせが失敗 | `OS32_ERR_IO` |
| `Directory` が非 0 | `OS32_ERR_ISDIR` |
| それ以外 | 0 (サイズを返してよい) |

**ディレクトリに答えない。** 種別の分からなくなった経路で「サイズが取れた = 通常ファイル」の根拠に使われると、`cat <ディレクトリ>` が通る。
問い合わせは 1 回のまま (種別は同じ応答の `Directory` で分かる)。get_file_size は open のたびに走るので回数を増やさない。

## open の失敗 (`hdrv_create_status_to_vfs`)

`hostdrv_create` (IRP_MJ_CREATE) の NTSTATUS を VFS のエラーへ写す。HostDrv の操作 (list_dir・read・get_file_size・read_stream・stat・
rmdir・unlink・rename・write・mkdir・write_stream) はこの値を**そのまま返す**。

| NTSTATUS | 写し先 |
|---|---|
| SUCCESS | 0 |
| OBJECT_NAME_NOT_FOUND・OBJECT_PATH_NOT_FOUND | `OS32_ERR_NOTFOUND` (**「無い」はこの 2 つだけ**) |
| OBJECT_NAME_COLLISION | `OS32_ERR_EXIST` |
| FILE_IS_A_DIRECTORY / NOT_A_DIRECTORY | `OS32_ERR_ISDIR` / `OS32_ERR_NOTDIR` |
| DIRECTORY_NOT_EMPTY | `OS32_ERR_NOTEMPTY` |
| OBJECT_NAME_INVALID | `OS32_ERR_INVAL` |
| それ以外 (共有違反・拒否・書き込み保護など)・応答が無い (番兵) | `OS32_ERR_IO` (**既定が IO であることが安全性の要**) |

畳んで NOTFOUND にすると、実在する通常ファイルのサイズ取得の open が一度失敗しただけで `vfs_open` の `O_CREAT` 経路へ進み、
`OVERWRITE_IF` の作成が既存の中身を切り詰める。
