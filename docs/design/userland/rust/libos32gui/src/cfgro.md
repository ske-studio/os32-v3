# userland/rust/libos32gui/src/cfgro.rs — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S2 (§3、S2-W) の契約を今のソースと照らして要約
ソース: `userland/rust/libos32gui/src/cfgro.rs`

アプリから設定レジストリへの窓口。共有ライブラリのジャンプ表 101〜104 (`os32gui_cfg_get_int`・`os32gui_cfg_get_text`・`os32gui_cfg_set_int`・`os32gui_cfg_set_text`) の実体で、C の `libos32cfg.a` を呼ぶ。library の約束は `docs/design/userland/lib/cfg/libos32cfg.md`。

## 1 呼び出しで閉じる

- `CfgDb` も接続も呼び手に渡さない。1 呼び出しの中で
  - get: `cfg_open(&db, 0)` → `cfg_get_*` → `cfg_close`
  - set: `cfg_open(&db, 1)` → `cfg_begin` → `cfg_set_*` → `cfg_commit` → `cfg_close`
  を完結させ、間に yield する呼び出しを置かない (接続は構造的に同時 1 本)。
- set は `app:[a-z0-9_]+` の scope だけ (`scope_is_app`、63 byte まで)。`system`・`gshell`・`user` は gshell の設定 UI と `cfg` コマンドの領分なので、DB を開かずに `ERR_PERM` (今は `ERR_INVAL` と同じ -9) で断る。get は全 scope。
- 自分の `app:` scope にしか書けない束縛は無い (アプリが自分の名前を知る KAPI が無い)。

## 引数の検査 (スライスを作る前)

- 文字列は ptr + len で受け、`raw_span_ok` で生の引数のまま見てからスライスにする: `len > max` は不可、NULL は `len == 0` のときだけ空として可、`ptr + len` が折り返せば不可。書き込み先は `raw_out_ok` (NULL・cap 0・`i32` に収まらない cap・折り返しは不可)。
- scope・key は空を拒否、63 byte まで、埋め込み NUL を拒否 (`copy_cstr`)。text の値は空を許し (NULL とは別の空値)、255 byte まで (`copy_value`)。private な NUL 終端の領域へ写してから C へ渡す。
- 検査に落ちたら DB を開かない。get_int は `def`、それ以外は `ERR_INVAL`。`s = NULL, s_len = 1` で既存値を空 text に化けさせない。
- `os32gui_shlib_init` が `set_kapi` を呼ぶ前 (shlib には crt0 が無いので `kapi` を自分で持つ) は、`cfg_open` を呼ばず `out` にも触らず、get_int = `def`、他は `ERR_INVAL`。

## 結果の畳み方

| 関数 | 規則 |
|---|---|
| `fold_get_int` | open・close のどちらかが失敗したら `def` (close だけが失敗しても読めた値を捨てる)。`cfg_open` が 0 を返して NULL を置いたら触らず `def`。負の設定値はそのまま |
| `fold_get_text` | open の失敗はその値。get の失敗 (`NOTFOUND`・`NOSPC` ではそのまま返し `out` を書き換えない) は close の失敗より優先。close だけが失敗したら `ERR_IO` |
| `fold_set` | open → work → close の順に見て、最初の失敗を返す。commit 済みでも close が失敗したら `ERR_IO` |

`write_txn`: begin の失敗はそのまま返し rollback しない (BEGIN していない)。set の失敗は `cfg_rollback` してから返す。commit の失敗も rollback して commit のコードを返す。close は呼び手の側で必ず行う。

## 検査

`userland/rust/libos32gui/host_tests` (`make check-gui-host`): `src/lib.rs` が `cfgro.rs` を `#[path]` で取り込み、`src/fake.rs` の `cfg_*` の贋物が呼び順と引数を記録する (実 DB は出てこない)。W01〜W35 で上の分岐を固定し、`shlib_init` 前の門 (W29) は `kapi` がプロセスに 1 語なので別プロセスの `tests/init_gate.rs` に置く。
