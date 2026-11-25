# sys/kern/ime/ime_dict_info.c — 辞書のメタ情報を返す KAPI `ime_dict_info`

> 状態: **現行 (2026-11-18)** — 出所: DICT_META_DESIGN・TASK_DICT_META (要約して移した)
ソース: `sys/kern/ime/ime_dict_info.c`・`sdk/include/os32/os32_kapi_shared.h` (`IME_DICT_INFO_*`)・`sdk/kapi.json` (`ime_dict_info`)・`sys/kern/ime/fep_module.c` (stub)・`userland/cmds/ime_info.inc` (`ime info` の表示)

型と誤りの意味は [ime_store.h の契約](ime_store.md)、各キーの値は [システム辞書の形式](../../tools/fep_to_sqlite.md) にある。

## 口

```c
int ime_dict_info(int key, int offset, char *out, int capacity);
```

- 選んでいるシステム辞書のメタ情報 1 つを UTF-8 の文字列で、`offset` byte 目から最大 `capacity - 1` byte 写し、NUL を付ける。
- 戻りは写した byte 数 (NUL を除く)。残りが無ければ 0 と `out[0]=0` (offset が長さ以上なら、`INT_MAX` でも 0)。
- 呼び手は戻り値ぶん `offset` を進め、0 になるまで繰り返す。UTF-8 の文字の途中でも byte で切る。数値も十進の文字列で返す。
- `capacity` は 2 以上。`IME_DICT_INFO_CAP` (256) を超えたら 256 として扱う。
- 公開の構造体は無い。キーの番号は固定で、増やすときは末尾に足して KAPI の版を上げる。
- 学習 DB・パス・SQL・接続は返さない。

## キー (`IME_DICT_INFO_*`)

1 `STATUS`、2 `FORMAT_VERSION`、3 `MIN_FEP_VERSION`、4 `DICT_ID`、5 `KIND`、6 `LANGUAGE`、7 `CHARSET`、8 `VARIANT`、
9 `SOURCE`、10 `SOURCE_VERSION`、11 `LICENSE`、12 `ATTRIBUTION`、13 `BUILD_TOOL`、14 `BUILD_DATE`、15 `ENTRY_COUNT`、
16 `POS_TABLE_VERSION`、17 `COST_SCALE`、18 `MEM_RESERVE_KB`、19 `CACHE_PAGES`、20 `CHECKSUM`。
2〜20 は辞書の meta の同じ名前 (小文字) の値をそのまま返す。

- `STATUS` は `meta` (検査済みの形式 1)・`legacy` (検査済みの旧形式)・`unloaded` (辞書が開いていない) のどれか。
- `MEM_RESERVE_KB` は辞書が求める KiB。settings で上書きした後の実効の予約ではない。
- `ENTRY_COUNT`・`CHECKSUM` は記録された値。この口は数え直しも計算もしない。

## 状態と誤り

| 状況 | 戻り |
|---|---|
| FEP が無い・公開前 (`services_public=0`、SQLITE_ONLY・MINIMAL の stub) | `OS32_ERR_NOSYS` |
| 未知のキー・負の offset・capacity < 2・`out` が NULL | `OS32_ERR_INVAL` |
| 呼び手の書き込み域が不正 (CPL3) | `ring3_fault_kill` (負値で戻るのではない) |
| 辞書が開いていない | `STATUS` は `unloaded`、ほかのキーは `OS32_ERR_AGAIN` |
| 接続が失効 | 全キー (`STATUS` も) `OS32_ERR_STALE`。開き直さない |
| 接続が隔離 | `OS32_ERR_BUSY` |
| 形式 1 で SQL の失敗・キーが無い・NULL | `OS32_ERR_IO` (EOF に丸めない) |
| 旧形式で既定値の無いキー | `OS32_ERR_NOTFOUND` |

検査の順は、公開の可否 → 引数 → 呼び手の書き込み域 → 接続の状態 → 値の取得。誤りのときは `out` を変えない。

旧形式の既定値: `FORMAT_VERSION`=0、`DICT_ID`=`ipadic-2.7.0`、`POS_TABLE_VERSION`=1、`COST_SCALE`=`os32-log2047-boost-v1`、
`MEM_RESERVE_KB`=192、`CACHE_PAGES`=16、`VARIANT` は固定の 3 パスのときだけ S/M/L (ほかのパスは `NOTFOUND`)。
license などそれ以外は `NOTFOUND`。偽の原文や件数 0 を返さない。

## 仕組み

- KAPI の表 → `ime_dict_info_facade` (core の `sys/kern/ime/fep_module.c`) → FEP 部品の ops の `dict_info` → FEP の中の `ime_dict_info_facade` → `fep_store_info`。
  FEP が無ければ core の stub が `NOSYS`。ops の並びを変えたら `FEP_MODULE_OPS_VERSION` (`sys/kern/ime/fep_module.h`) を上げる。
- 形式 1 は `SELECT value FROM meta WHERE key = ?` を 1 回 step し、`sqlite3_column_blob`・`sqlite3_column_bytes` で長さを取り、C の側で byte を切る。
  SQL で切らないのは、製品の SQLite が `CAST` を省き、outer の口に `bind_int64` が無いため。
- SQLite の値は finalize の前に関数内の 256 byte の staging に写し、finalize の後に呼び手へ 1 回だけ写す (`copy_to_caller`)。
- `dict_ready` (開き直し) を呼ばない。辞書を開く・回復する・切り替えることをこの口から起こさない。原文の常駐の写しは持たない。
- 呼べるのは CUI・GUI の通常の処理、CPL3 のアプリ、CPL0・WM。IME が OFF でもよい。IRQ・描画や入力のコールバック・SQLite の処理中からは呼ばない。
  呼びを跨いだ一貫性は保証しない (全文を取る間に切替や yield を挟まない)。切替に成功した辞書の値だけを返し、旧い辞書に戻せたら旧い値、戻せなければ `unloaded`。

## 表示 (`ime info`)

`userland/cmds/ime_info.inc` が 20 キーを順に 256 byte ずつ取り、原文をそのまま書き出す (復号・再符号化をしない)。
旧形式で `NOTFOUND` のキーは「旧形式に未収録」と出す。誤りは理由と rc を 1 行出して終わる。

## 検査

- `fep-store-info` (`tools/tests/test_fep_store_info.py`): 全キー、状態ごとの戻り、capacity 0/1/2/256/257、EOF・`INT_MAX`、UTF-8 の途中で切った原文の再結合、失効のとき SQL を流さないこと、呼び手の域。変異 (`info-*`) は `fep-boundary-host` にもある。
- `fep-module` (`tools/tests/test_fep_module.py`): ops の表と stub の `NOSYS`、起動の接続数の中継。
- `ime-info` (`tools/tests/test_ime_info.py`): 表示が byte で切った原文を壊さないこと。
- ゲスト: `userland/diag/fep_meta_test.c` (20 キーの全 byte を辞書の meta と照合、`entry_count` が実数と一致)。
