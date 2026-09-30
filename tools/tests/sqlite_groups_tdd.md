# F2b scoped — 接続専用 SQLite VFS 内部基盤の HOST TDD

## 範囲

`lib/sqlite3/os32_sqlite_vfs.c/.h` と専用試験のみ。F2a の実 FD 表を使用。
KAPI/FEP/exec の呼出し先は移行していない。default `os32` は従来の GENERIC
file open を維持する。**これは full F2 の受入ではない**。境界 prerequisite、
F2b の F1 接続部分、F2c/F2d、独立 R1、kernel link、guest/FEP は未検証。

`DB_MAX_CONNECTIONS=8` を容量の管理元として使用し、常駐用を別枠 1 個予約。
既存 `g_ime.dict` と `ime_dict_open()` の一つの SQLite 接続を静的確認した。
EXEC owner=0 と RESIDENT は別 class。group generation は wrap しない。

## 実装前 RED → 最小実装 → GREEN の記録

各 RED は `python3 tools/tests/test_sqlite_groups.py <case...>` で実行し、
HOST compile 成功後の assertion failure / exit=1 を観測した。
新 API が存在しない最初の段階のみ、harness 内に CANTOPEN/MISUSE/NULL を返す
一時 shim を置き、リンク失敗を RED に数えなかった。GREEN 後に shim と
その feature macros は全撤去した。実装本体のコピー／fake SQLite 出力はない。

| 順 | ケース | 観測した RED assertion | GREEN で追加したもの |
|---|---|---|---|
| 1 | registration | `v != NULL` (`os32-g0`) | 9 個の boot-lifetime 専用 descriptor を非 default 登録 |
| 2 | capacity | EXEC acquire が OK でない | 8 EXEC + 1 RESIDENT、引数検査、固定 cookie 世代、容量事前拒否 |
| 3 | owned_open | main FD の lifetime が SQLITE でない | pAppData → group の明示 owner/cookie、main/後発 journal/named temp の lease open/close |
| 4 | lifecycle | vfs_name が NULL | OPENING/LIVE/CLOSING/FREE、close 中の追加 FD、世代 cookie による stale API 拒否 |
| 5 | busy_partial / busy_zero / orphan | state が QUARANTINED でない（3 ケース） | 部分 close / 0FD BUSY / close OK + 残存の隔離。将来 xOpen の無操作拒否 |
| 6 | sticky_close | `first_rc == SQLITE_IOERR_CLOSE` が不成立 | xClose の戻り直前に固定 group へ保存。file 領域 memset/free 後、0FD + close OK でも finish 失敗 |
| 7 | callback_read/write/truncate/sync/size/lock/unlock/reserved/control/sector/device | `first_rc != SQLITE_OK` が不成立（11 ケース） | 全 file callback の group/lease 検証。再利用 FD の offset/内容/metadata と出力引数を保護 |
| 8 | scoped_open | `:memory:` の scoped open が OK でない | cookie 検証付き open helper、一接続一回、URI flags/`file:` 拒否、nonNULL failed open の db/最初の診断保存 |
| 9 | diagnostics | snapshot が OK でない | 固定表のみから値をコピーする診断 API、隔離の最初の stage/rc を維持 |
| 10 | path_delete/access/fullpath | `rc != SQLITE_OK` が不成立（3 ケース） | FREE/隔離済み専用 VFS の path callback も無操作拒否 |
| 11 | null_open | FREE group の snapshot が OK でない | MEMSYS5 枯渇で実 sqlite3_open_v2 が NOMEM + db=NULL、正常 teardown 後も旧 cookie の診断を次 acquire まで読める |
| 12 | register_retry | `register_success[i] == 1` が不成立 | 登録途中の失敗注入後も成功済み descriptor の再コピー／再登録をしない進捗 counter |
| 13 | uri_callback | URI flag 付き専用 xOpen が CANTOPEN でない | wrapper に加え専用 callback も URI flag を I/O 前拒否。legacy は変更しない |

各サイクルで対象 GREEN と既存ケースを反復。最後に全ケースを一括実行した。
追加の `edge_opens`, `stale_group`, `repeated_lifetimes`, `real_normal`, `real_stale`
は上記実装の境界／実エンジン回帰検証であり、新しい本体変更なしに合格したものを
「そのケース自体で先行 RED を見た」とは数えない。

## Harness と実 SQLite

- `sqlite_groups_host.c` は実 `os32_sqlite_vfs.c` を取り込む。
  F2a harness を main 名だけ変更して取り込み、実 `fs/vfs_fd.c` の表・lease 検証を使用。
- `sqlite_groups_backend.h` は専用 RAM filesystem fixture。host の実ファイルへ
  DB を書かず、FS/TTY/owner/time/logging 境界だけを置き換える。
- `sqlite3.c` は同梱の実 amalgamation を別 object としてリンク。変更なし。
  実 `os32_sqlite_config.h` を強制 include、MEMSYS5 384KB、TEMP_STORE=3、
  OMIT_WAL/OMIT_ATTACH/THREADSAFE=0 を維持。system SQLite/VFS は使用しない。
- ホスト差分: native pointer/size_t、libc memory + kstring shim、`-O0`、任意 ASan。
  ターゲット側 VFS object は i386-elf GNU89 / freestanding / soft-float / `-Os`。
  host fixture は durability、実 ext2、実 FD backend close failure の証拠ではない。
- 故障注入は SQLite register の前に一度 BUSY を返す seam、実 FD lease の先行解放、
  世代上限、SQLite pool の実枯渇。実在しない backend close 障害は主張しない。

実エンジンの観測（正常・stale の両ケースで同じ journal trace）:

```
REAL journal=/real.db-journal group=0/1 owner=1 current=2 fd=4/2 flags=806
REAL close SQLite=OK finish=0 temp_rows=80 temp_FD=0
REAL close SQLite=OK finish=10 temp_rows=80 temp_FD=0
```

`CREATE TABLE` 後 BEGIN + 512-byte blob 80 行を挿入。main だけの 1FD から
遅れて MAIN_JOURNAL (`flags=0x806`) を含む 2FD へ増えることを実表で検査。
current owner=2 のまま両 FD は open_owner=1 / 同 cookie。
汎用 owner cleanup 後も生存、COMMIT で journal が閉じ、TEMP TABLE の 80 行を
実 SELECT で確認した。TEMP_STORE=3 では temp の追加 FD は 0。
これは named journal が専用 pVfs を継承する実証であり、全 sorter/spill 経路を網羅した
主張ではない。synthetic named temp は匿名 temp の実装を意味しない。

実 SQLite close 前に main lease を stale 化すると、SQLite 自体は OK を返し file
領域を破棄するが、固定 group の sticky error により finish=10 (SQLITE_IOERR)、
db=NULL / 0FD / QUARANTINED になる。synthetic xClose 単体では
SQLITE_IOERR_CLOSE の即時保存を別途検査する。

## 最終実行結果

```
python3 tools/tests/test_sqlite_groups.py --target --sanitize
HOST GNU89 -Werror compile PASS (bundled SQLite)
TARGET i386-elf GNU89 -Werror compile PASS
SUMMARY 32/32 PASS
exit=0

python3 tools/tests/test_vfs_fd_sqlite.py --target
SUMMARY 15/15 PASS
exit=0

python3 tools/tests/test_kapi_db_owned.py
Ran 1 test ... OK
exit=0
```

ASan は bundled SQLite と harness の両方へ適用。検出報告なし。
SQLite 固定 pool 内部の個別 free を ASan がすべて識別するという意味ではない。
synthetic file の malloc/memset/free と、その後の固定表診断も実行した。
F1 は既存 mock ベースの回帰であり KAPI と専用 VFS の統合試験ではない。

## 遭遇した問題を成功に数えない

1. 最初の host compile は既存 io_methods の xUnfetch 暗黙ゼロ初期化への
   `-Wmissing-field-initializers` で失敗。runner の当該既存警告だけを除外後、
   改めて registration の assertion RED を得た。本体初期化は変更していない。
2. 同梱 amalgamation は従来の OMIT 構成により `sqlite3DbIsNamed` と
   `sqlite3RenameTokenMap/Remap`, `sqlite3RenameExprUnmap/ExprlistUnmap` の
   「used but never defined」警告を出す。SQLite object の警告は隠していない。
   実際の host link/SQL 実行は成功。VFS/harness は選択した警告除外込みで -Werror。
3. 100 寿命の登録リスト試験の初稿は「全 VFS は 10 個」と誤って仮定し
   30/31 で失敗。`sqlite3MemdbInit()` (`sqlite3.c:56139–56151`) が `memdb` を
   別に登録することをソースで確認。専用 9 + legacy 1 + SQLite memdb 1 の
   リンク同一性検査へ harness を修正した。本体の故障ではない。

## 後続呼出し側の必須契約

1. acquire は cookie 値を caller にコピーする。再利用される固定 slot の pointer を
   handle にしない。vfs_name は診断／SQLite 内部伝播用であり、保存名だけで直接
   sqlite3_open_v2 を呼ぶ代替 API にしない。接続は `os32_sqlite_group_open()` 経由。
2. 一度 open を試した group は、失敗時も NULL/nonNULL db を区別して teardown する。
3. begin_close → finalize/必要な rollback → sqlite3_close。
   close が OK なら **caller 自身の db=NULL を finish/診断より先に行う**。
4. finish が FREE にできるのは close OK + 残存 FD=0 + sticky error=0 のみ。
   通常の open エラー診断と sticky lease/xClose エラーは別。最初の診断を上書きしない。
5. QUARANTINED は再試行・解除・再利用しない。FREE の診断は次 acquire まで読める。
   snapshot は SQLite/file ポインタを返さず has_db と固定値だけを返す。

Make、kernel 全 link、make check、エミュレータ、配備、ネットワーク、環境変更、
秘密情報、docs/hw、commit、追加 agent は実施していない。F3 の seek/truncate/sync/
access/delete/locking/RO 契約修正と crash durability は範囲外。
