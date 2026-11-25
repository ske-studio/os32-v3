# sys/kern/ime/ime_user_store.c — 学習 DB (`/etc/fep_user.db`) の検査と作成

> 状態: **現行 (2026-11-18)** — 出所: DICT_META_DESIGN・TASK_DICT_META (要約して移した)
ソース: `sys/kern/ime/ime_user_store.c`・`sys/kern/sqlite_engine.c` (`services_settings` が学習のパスを読む)

型・定数・誤りの意味は [ime_store.h の契約](ime_store.md) にある。

## 何をするか

学習データをシステム辞書と別のファイルに置き、読み書き (RW) の接続で開く。
無ければ作り、あれば版・ID・品詞の版を確かめてから使う。
使えないとき (書けない・不一致・壊れている) は学習だけを止め、システム辞書の変換は続ける。

## 置き場

- 既定は `/etc/fep_user.db`。settings (scope=`system`、キー `fep_user_dict_path`、type=1 の text) で変えられる。
- 変えた先も `/etc/` 直下に限る (`user_path_valid`)。`/etc/` で始まり、その後に `/`・`\` が無く、`.`・`..` でない。
  パスに `-journal` を足して NUL まで `OS32_MAX_PATH` に収まること。カーネルの側では正規化しない (大文字小文字も区別する)。
- core は起動時に settings を 1 回読んで写しを持つ (`fep_boot_user_path_get`)。DB・表・行が無ければ既定のパス。それ以外の読み取りの失敗は学習を止める (推測で別のファイルを開かない)。
- 置き場を変えるときは、旧い学習データを新しいパスへ移してから設定を変える。旧いパスを探しに行く仕組みは無い。

## 形 (形式 1)

```sql
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE dict_user(yomi TEXT NOT NULL, kanji TEXT NOT NULL,
  freq INTEGER DEFAULT 1, last_ts INTEGER DEFAULT 0, PRIMARY KEY(yomi,kanji));
CREATE INDEX idx_user_yomi ON dict_user(yomi);
```

| meta の key | 値 | 判定 |
|---|---|---|
| `format_version` | `1` | 十進の文字列全体を検査 (符号・前置き・桁あふれは不可) |
| `kind` | `user` | 完全一致 |
| `dict_id` | 作ったときのシステム辞書の `dict_id` | 今のシステム辞書と完全一致 |
| `pos_table_version` | 作ったときの品詞の版 | 今のシステム辞書と一致 |
| `created` | UTC ISO 8601 (`YYYY-MM-DDThh:mm:ssZ`) | 形だけ検査 |

- 版の判定は meta の `format_version` だけ。page_size は 1024 を要求する。ヘッダの user_version は設定も判定もしない。
- S/M/L は同じ `dict_id` なので、どの辞書でも同じ学習を使う。
- 旧い形 (辞書と同じファイルの学習表) は移さない。新しい学習 DB は空から作る。

## 開く手順 (`fep_user_open`)

1. パスを検査する。ファイルが無いか 0 byte なら「作る」。
2. 常駐の group を取り、RW (作るときは CREATE も) で開く。
3. 作るとき: `PRAGMA page_size=1024`、表と meta を 1 つの transaction (`BEGIN IMMEDIATE` … `COMMIT`) で作る。
4. page_size を確かめ、meta を照合し、`dict_user` の 4 列を `LIMIT 0` で確かめる。**既存の DB には照合が済むまで書く SQL を流さない。**
5. `PRAGMA cache_size=8`。
6. 既存の DB には `UPDATE dict_user SET freq=freq WHERE 0` を流して書けるかを確かめる。空の `BEGIN`/`COMMIT` は VFS が RO に落としても通るので使わない。書けなければ `ROFS`。
7. どこかで失敗したら接続を閉じ、`user_enabled=0`、理由を赤で表示する。不一致の DB を消す・作り直す・別の ID で使うことはしない。
   作る途中で失敗したら「incomplete; delete it to recreate」と出す (既存の学習と取り違えない)。

## 他の部品との境目

- 学習の SQL (検索・学習・一覧・削除・書き出し・全消去) は `sys/kern/ime/ime_dict.c` が持つ。ここは開くことと検査だけ。
- 学習 DB は配布物 (deploy・packages) に入れない。hsync は学習 DB とそのジャーナル・一時の名前を上書きしない (`userland/system/hsync_protect.inc`)。

## 検査

- `fep-user-store` (`tools/tests/test_fep_user_store.py`): 作成・検査・不一致・作りかけ・ROFS の故障の注入 (libc なし)。
- `fep-store-search`: 学習の有無で接続数が 1/2、不一致で学習が止まり system の検索が続く。
- ゲスト: `userland/diag/fep_meta_test.c` (専用の学習 DB で ID・品詞の版を一時に変えて学習が止まること)。
