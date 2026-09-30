# [P3] 辞書管理シェルコマンド `ime`

> 索引: [`00_INDEX.md`](00_INDEX.md)
> 優先度: **中** ／ 依存: KAPI 公開済 (`ime_*`)

[`FEP_FUTURE.md`](FEP_FUTURE.md)「ユーザー辞書管理コマンド」「辞書バリアント動的切り替え」をまとめた
外部プログラム `programs/ime.c` (シェルコマンド) を新設する。

---

## 1. コマンド体系

```
ime                      現在のモード/辞書を表示
ime on | off             FEP 有効/無効 (ime_set_mode 経由)
ime mode hira|kata       入力モード切替
ime dict s|m|l           辞書バリアント切替 (再オープン)
ime user list [yomi]     学習辞書の閲覧 (全件 or 読み前方一致)
ime user delete <yomi> [kanji]   学習エントリ削除
ime user export [path]   CSV エクスポート (既定 /tmp/userdict.csv)
ime user clear           学習辞書全消去 (確認プロンプト)
```

---

## 2. アーキテクチャの判断

ユーザー辞書 (`dict_user`) はカーネル空間の `IME_Dict` が握る SQLite ハンドルに属する。
外部プログラムから直接 SQLite を触らせる選択肢もあるが (KAPI `db_open`)、
**辞書ファイルの二重オープン (RW) はロック競合・学習データ破損のリスク**がある。

→ 方針: 辞書管理操作はカーネル側に新 KAPI を追加し、外部プログラムはそれを呼ぶ。
カーネルは自身が開いている同一ハンドル上で操作するため一貫性が保たれる。

---

## 3. 追加カーネル関数 (`ime.c` / `ime_dict.c`)

```c
/* ime_dict.c に追加 */
int  ime_dict_reopen(IME_Dict *dict, const char *path);  /* finalize→close→open */
int  ime_user_list(IME_Dict *dict, const char *yomi_prefix,
                   IME_Result *out, int max);            /* dict_user 列挙 */
int  ime_user_delete(IME_Dict *dict, const char *yomi, const char *kanji);
int  ime_user_export(IME_Dict *dict, const char *path);  /* CSV 書出 (VFS) */
int  ime_user_clear(IME_Dict *dict);                     /* DELETE FROM dict_user */

/* ime.c に薄いファサード (グローバル g_ime.dict を渡す) */
int  ime_switch_dict(int variant);   /* 0=S,1=M,2=L → reopen */
```

`ime_switch_dict()` のパス対応:

| variant | パス |
|---------|------|
| 0 (S) | `/db/fep_s.db` |
| 1 (M) | `/db/fep.db` (既定) |
| 2 (L) | `/db/fep_l.db` |

> 現状 `assets/fep_s.db` / `fep_l.db` は存在するが `deploy.yaml` で配備されているのは
> `fep.db` のみ。動的切替を使うには `deploy.yaml` に S/L も追加 (`tags:[data]`) する。
> 詳細は [`04_DICT_QUALITY.md` §3](04_DICT_QUALITY.md)。

---

## 4. 追加 KAPI (`kapi.json`)

`kapi/` に `__cdecl` ラッパーを作り、`kapi.json` に登録 → `kapi_generated.*` 再生成。

```jsonc
{ "name": "ime_switch_dict", "ret": "int", "args": ["int variant"] },
{ "name": "ime_user_list",   "ret": "int",
  "args": ["const char *yomi_prefix", "IME_UserEntry *out", "int max"] },
{ "name": "ime_user_delete", "ret": "int",
  "args": ["const char *yomi", "const char *kanji"] },
{ "name": "ime_user_export", "ret": "int", "args": ["const char *path"] },
{ "name": "ime_user_clear",  "ret": "int", "args": [] }
```

> `IME_Result` を外部プログラムへ公開するには `programs/os32api.h` にも構造体定義
> (yomi/kanji/pos_id/cost) を複製する必要がある。
> 列挙専用には軽量構造体 `IME_UserEntry { char yomi[32]; char kanji[32]; int freq; }`
> を新設する方がクリーン (**推奨**)。これを `ime_user_list` の出力型とする。

---

## 5. KAPI 拡張手順 (CLAUDE.md 準拠)

1. `exec/exec.h` の `KernelAPI` 構造体**末尾**に関数ポインタ追加 (バイナリ互換)。
2. `kapi/kapi_ime.c` (新規 or 既存) に `__cdecl` ラッパー実装。
3. `exec/exec.c::exec_init()` にテーブル登録。
4. `programs/os32api.h` に宣言追加 (+ `IME_UserEntry` 定義)。
5. `KAPI_VERSION` インクリメント + `KAPI_SPEC.md` 更新。
6. `tools/kapi.json` 追記 → `kapi_generated.c` / `os32_kapi_generated.h` / `kapi_generated.rs` 再生成。

KAPI 追加分の全体一覧は [`00_INDEX.md` §9](00_INDEX.md#9-kapi-追加一覧-全フェーズ集約)。

---

## 6. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `kernel/ime_dict.c` | `ime_dict_reopen` / `ime_user_*` 実装 |
| `kernel/ime.c` / `ime.h` | `ime_switch_dict` ファサード、`IME_UserEntry` |
| `kapi/kapi_ime.c` (新規) | `__cdecl` ラッパー |
| `exec/exec.h` / `exec.c` | KAPI 登録 |
| `tools/kapi.json` | KAPI 定義追記 |
| `programs/os32api.h` | 宣言複製 + `IME_UserEntry` |
| `programs/ime.c` (新規) | シェルコマンド本体 |

**テスト:**
1. `ime user list` で学習済みエントリが頻度順に表示される。
2. 何度か変換確定後、`ime user list <よみ>` に該当エントリが出る。
3. `ime dict s` 後に変換候補数/順序が変わる (S 辞書は語彙少)。
4. `ime user export` で CSV が `/tmp` に生成され、`cat` で内容確認。
5. `ime user delete` 後に該当エントリが消える。
6. `build.sh` / `build_programs.sh` エラー 0。

---

*前: [`02_SCROLL_GUARD.md`](02_SCROLL_GUARD.md) ／ 次: [`04_DICT_QUALITY.md`](04_DICT_QUALITY.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
