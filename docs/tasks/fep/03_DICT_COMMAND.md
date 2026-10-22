# [P3] 辞書管理シェルコマンド `ime`

> 上位: [`00_INDEX.md`](00_INDEX.md)
> 優先度: **中** ／ 依存: KAPI 公開済 (`ime_*`)

`FEP_FUTURE.md`「ユーザー辞書管理コマンド」「辞書バリアント動的切り替え」をまとめた
外部プログラム `userland/cmds/ime.c` (シェルコマンド) の実装済み設計記録。

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
> 詳細は `04_DICT_QUALITY.md` §3。

---

## 4. 追加 KAPI (`kapi.json`)

定義元は `sdk/kapi.json`。公開済みの関数一覧は
[KAPI_SPEC.md の FEP 辞書管理](../../KAPI_SPEC.md#fep-辞書管理--ノンブロッキング入力-v35) を参照。
列挙の出力型 `IME_UserEntry` は `sdk/include/os32/os32_kapi_shared.h` で共有する。

---

## 5. KAPI 拡張手順

[KAPI_SPEC.md §3-1](../../KAPI_SPEC.md#3-1-kernelapi-関数の追加手順) に従う。生成物は手編集しない。

---

## 6. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `kernel/ime_dict.c` | `ime_dict_reopen` / `ime_user_*` 実装 |
| `kernel/ime.c` / `ime.h` | `ime_switch_dict` ファサード、`IME_UserEntry` |
| `kapi/kapi_generated.c` (生成物) | `__cdecl` ラッパー |
| `exec/exec_kapi_init.inc` (生成物) | KAPI 登録 (手編集しない) |
| `sdk/kapi.json` | KAPI 定義追記 |
| `sdk/include/os32/os32_kapi_shared.h` | 共有型定義 + `IME_UserEntry` |
| `userland/cmds/ime.c` | シェルコマンド本体 |

**テスト:**
1. `ime user list` で学習済みエントリが頻度順に表示される。
2. 何度か変換確定後、`ime user list <よみ>` に該当エントリが出る。
3. `ime dict s` 後に変換候補数/順序が変わる (S 辞書は語彙少)。
4. `ime user export` で CSV が `/tmp` に生成され、`cat` で内容確認。
5. `ime user delete` 後に該当エントリが消える。
6. [ビルド・検証手順](../../08_build.md#ビルドターゲット) (§8-4) に従って確認する。

---

*前: `02_SCROLL_GUARD.md` ／ 次: `04_DICT_QUALITY.md` ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
