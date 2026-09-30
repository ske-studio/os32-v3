# S2-C — `libos32cfg` / `cfg` コマンドのホスト TDD 記録

票: [`docs/archive/settings/TASK_S2.md`](../../docs/archive/settings/TASK_S2.md) 第 5 版 §0 の **S2-C**。
契約の正典は [`S0_FOUNDATION.md`](../../docs/archive/settings/S0_FOUNDATION.md) §2 と
[`DESIGN.md`](../../docs/tasks/settings/DESIGN.md) §3〜§5。
走らせ方:

```bash
python3 -B tools/tests/test_cfg.py                 # 42 ケース + tsv 58 fixture
python3 -B tools/tests/test_cfg.py --target        # + i386-elf の -Werror コンパイル
python3 -B tools/tests/test_cfg.py --sanitize      # + ASan
python3 -B tools/tests/test_cfg.py missing corrupt # ケース指定
```

ケースの内訳: §1 の 16 本 (票 §4 の (1)〜(23))、§C の 15 本 (実装レビュー往復 1)、
§C2 の 7 本 (往復 2)、§C3 の 5 本 (往復 3) = **42 本** + tsv fixture 58 件。

コーダーの範囲はホスト TDD までなので、**ゲスト受入 (票 §5 の C1〜C7) は未実行** ([V4])。
`make all` / `make check` / 配備 / エミュレータも触っていない。

---

## 0. 組み方

`tools/tests/cfg_host.c` は `kapi_db_v50_host.c` と同じ作法で「実物だけ」を組む。

| 実物 | 模型 |
|---|---|
| `userland/lib/cfg/libos32cfg.c` / `cfg_enum.c` / `cfg_tsv.c` / `cfg_init.c` | `exec` のポインタ検証 `ring3_user_range_ok` |
| `userland/cmds/cfg.c` (`main` を `cfg_main` に差し替えて丸ごと) | SHM の置き場 (`test_shm`) |
| `kapi/kapi_db.c` (KAPI v50 の本物) | VFS の `stat` / `rename` / `unlink` / `open` / `read` (RAM fixture) |
| `lib/sqlite3/sqlite3.c` + `os32_sqlite_vfs.c` + `fs/vfs_fd.c` + RAM backend | 「KAPI の `db_close` / `db_exec` / `db_prepare_only` が失敗する」の注入 |

ホストのファイルシステムには触らない。tsv の fixture は **stdin** から渡して RAM fixture に置く。

`libos32cfg.c` が呼ぶ KAPI は 1 枚の関数ポインタ表 `CfgBackend`
(`userland/lib/cfg/cfg_internal.h`) に集めてある。ゲストの実体は `cfg_backend.c`
(`kapi->db_*`)、ホスト試験は自前の `cfg_backend_platform()` を与えて実 `kapi_db.c` へ向ける。
**ライブラリ本体は 1 行も分岐していない** (`#ifdef` も無い)。

### 翻訳単位の切り方

`libos32cfg.a` は 5 本に割ってある。読むだけのアプリ (S2-W の `os32gui_cfg_get_*`、
gshell の起動時読み込み) が列挙 / 生成の作業領域を背負わないため。

| オブジェクト | text | bss | 何が要るときに引き込まれるか |
|---|---:|---:|---|
| `libos32cfg.o` | 7149 | 72 | `cfg_open` / `get` / `set` / txn (常に) |
| `cfg_enum.o` | 1376 | **19456** | `cfg_enum` / `cfg_enum_scopes` |
| `cfg_tsv.o` | 2731 | 0 | tsv reader (`cfg_init` 経由) |
| `cfg_init.o` | 2888 | **11256** | `cfg_init` |
| `cfg_backend.o` | 314 | 0 | 既定の KAPI 境界 |
| (`userland/cmds/cfg.o`) | 8592 | 46916 | — |

`libos32cfg.a` = 25.0KB。`cfg.bin` の見込みは text 約 23KB + bss 約 78KB (crt0 / newlib 別)。

---

## 1. RED → GREEN

「RED」は **対象の挙動が assertion で落ちること**。既存挙動の初期 GREEN は回帰証拠であって
RED の代わりではない、という S0 の規則をそのまま使う。

| # | 票 §4 | ケース | RED (最初に落ちたもの) | GREEN |
|---|---|---|---|---|
| 1 | (1) | `missing` | `cfg_open` が実装前は 0 を返さない / DB が無いのに `cfg_get_int` が def を返さない | MISSING でも `cfg_open` は 0。`get` は def、`begin`/`set` は `OS32_ERR_INVAL`。`/etc/settings.db` も `-journal` も**作られない** (writable=1 でも) |
| 2 | (2) | `corrupt` | 0 バイト / hot journal / `meta` 表なしが区別されず `CFG_ERROR` になる | `NOTADB` → CORRUPT、`BUSY_RECOVERY` → CORRUPT (**journal を消さない・自動回復しない**)、prepare の `SQLITE_ERROR` (= no such table) → CORRUPT。0 バイトのファイルは中身も長さも変わらない |
| 3 | (3)(12) | `version` | `meta` の版 2 を `writable=1` で開くと RW に切り替えてしまう | RO 検査が `CFG_OK` のときだけ RW へ切り替える。版 2 は **RO 接続を保持**して VERSION。`get_int` / `get_text` は読める、`begin` / `set` / `delete` は `OS32_ERR_INVAL` |
| 4 | (4)(19) | `types` | 型違いが値を返す / cap 不足で `out` を部分更新する / NULL と空 text が同じになる | 型違いは NOTFOUND。cap 不足は `OS32_ERR_NOSPC` で **out を 1 バイトも書かない** (番兵 `'Z'` で確認)。`tval IS NULL` は NOTFOUND、`tval = ''` は長さ 0。空 blob も長さ 0 |
| 5 | (5) | `roundtrip` | commit 後に再 open して読めない / 4096B blob が欠ける | `begin` → `set_int`/`set_text`/`set_blob(4096B)` → `commit` → `close` → 再 open で全部一致。`delete` も往復する |
| 6 | (6)(7)(15) | `txn` | txn 外の `set` が通る / set 失敗後の `commit` が通る / 成功した `ROLLBACK` が診断を 0 に戻す / close 失敗が成功扱い | txn 外の set は INVAL。set 失敗 (prepare 障害を注入) で `txn=failed` → `commit` は拒否して rollback。**失敗コードは rollback の前に保存**するので `cfg_last_sqlite` が保たれる。未 commit の close は rollback。close 失敗は `cfg_last_close_error()` にコードを残し負を返す |
| 7 | (8) | `limits` | 64B の key / 256B の text / 4097B の blob が通る / 不正 UTF-8 が入る | 63/255/4096 は通り 1 バイト超は `OS32_ERR_INVAL`。`SYSTEM` / `app:` / `app:My App` / 64B scope / `Desktop` / `desktop/` / `/desktop` / 空 key を拒否。`\xff\xfe` / 冗長符号 `\xc0\x80` / 代理符号 `\xed\xa0\x80` を拒否し、`あ` は通る |
| 8 | (9)(14) | `enum` | prefix に LIKE を使うと `a_b` のワイルドカードで `axb` が混ざる / 257 件目で溢れる / callback から再入できる | `substr(key,1,?) = ?` の前方一致なので `a_` は `a_b`/`a_c` だけ。`ORDER BY key`。`cfg_enum_scopes` は DISTINCT + 昇順。257 件目は `OS32_ERR_NOSPC` で **callback を 1 度も呼ばない**。callback 内の `cfg_enum` は `OS32_ERR_INVAL` |
| 9 | (13)(18) | `meta` | 0 行 / 2 行 / 非整数 / 範囲外が版 1 に見える | 1 本の SQL (`COUNT` / `typeof` の MIN・MAX / 値の MIN・MAX / `BETWEEN 1 AND 2147483647` の MIN) で 0 行・2 行・`'abc'`・`x'01'`・`1.5`・`0`・`-1`・`2^32+1`・正常行との混在をすべて CORRUPT、`1` は OK、`2` は VERSION |
| 10 | (17) | `shm_copy` | 別接続が SHM を上書きすると取り出した値が化ける | `get_text` は SHM の row から呼び手のバッファへ**即コピー**。別接続で `SELECT 'AAAA…'` を step しても取り出し済みの値は不変 |
| 11 | (11)(22) | `init` | 既存 / 0 バイト / journal 残存 / `.new` 残骸で作ってしまう、あるいは残骸を消してしまう | `sys_stat` が NOTFOUND のときだけ作る。`already exists` / `needs recovery` で拒否し、**残骸は消さない** (`.new` の中身とサイズが不変)。生成物は meta 1 行 (版 1)、settings 3 行、6 列、WITHOUT ROWID。tsv 欠損 / 規則違反では `.new` すら残らない |
| 12 | (16)(20) | `rename` | rename 失敗後に `.new` を消して本体まで壊す | 「新名あり・旧名削除も巻き戻しも失敗」= 同じ inode を 2 名が指す → **どちらも消さず** `CFG_INIT_AMBIGUOUS`。「何も起きなかった」→ `.new*` を片付けて失敗。「旧名は消えたが失敗を返した」→ 本体を RO で検査して成功 |
| 13 | (10)(21) | `tsv` (58 fixture) | C の reader と `tools/mk_settings_db.py` の判定が食い違う | 58 件すべてで accept/reject が一致 (下記 §2) |
| 14 | — | `args` | — | `cfg_cmd_parse` と `fmt_int` / `parse_int` / `fmt_hex` / `parse_hex` / `fmt_b64` / `fmt_json_str` / `status_name` / `type_name` を純関数として直接叩く |
| 15 | (1)(23) | `cmd` | `export` のヘッダが固定 1 になる / MISSING の export が成功する | 受入 C1〜C4 のホスト版。`status` は MISSING で終了 1、`init` → `OK schema_version 1`、`list` に tsv の 3 行、2 度目の `init` は `already exists`、`set`/`get`/`del`、`export` は 行数 = 件数 + 1。**版 2 の DB のヘッダは実値 `"schema_version":2`**、MISSING の export は `cannot export: MISSING` で終了 1 |
| 16 | — | `list_big` | (初回から GREEN — 回帰の番人) | 255B の text 100 件 = 約 22KB を `cfg list`。8KB の溜め場が一杯になるたび **DB を閉じて吐き出し、開き直して続きから**出す。100 行ちょうど、key の重複ゼロ、`sys_yield` が呼ばれている |

### 実装中に落ちて直した 2 件 (RED の実物)

1. **`cfg_tsv.c` の列上限が 1 バイト甘かった** — `read_field` が `n < cap` で書いていたので、
   `cap = CFG_TEXT_MAX + 1` のとき **256B の text を受理**し (`text_256` fixture が
   `C=True py=False` で落ちた)、しかも 64B の scope では終端 NUL が書かれず
   `cfg_i_valid_scope` が領域の外を読んでいた。`n < cap - 1` へ直し、超過時も必ず
   終端するようにした (ASan でも再確認)。
2. **試験側の誤り** — `meta` に `'1'` を入れても `schema_version INTEGER` の
   **親和性**で整数 1 に化けるので `typeof` は `integer` のまま。TEXT の検査は
   化けない値 (`'abc'`) と blob / 実数で見るよう直した (実装は無罪)。

---

## 2. tsv の判定を Python と揃える

`tools/tests/test_cfg.py` の `TSV_FIXTURES` (58 件) を、**同じバイト列**のまま

- C: `cfg_tsv_parse` (`cfg_host tsv`、stdin から RAM fixture へ)
- Python: `tools/mk_settings_db.py --tsv … --out …`

の両方へ通し、**accept / reject が一致すること**を固定する。fixture は S0-T の
`test_mk_settings_db.py` と同じ形 (`assets/settings/defaults.tsv` 本体、先頭ゼロ 9000 桁、
CR、コメント中の不正 UTF-8、末尾空欄、最大 blob 行、63B key、255B text、`あ`×85 など)。

規則の写し方で決めたこと:

- **CR と不正 UTF-8 はコメントの中でも拒否**する。生成ツールはファイル全体を先に検査するので、
  同じ入力を同じ判定にするにはコメントも読み飛ばさずに検査する必要がある。
- **NUL はコメントの中では通る** (Python も `'\x00'` を UTF-8 として復号し、コメント行は
  スキップする)。scope / key / text / type / blob の中では拒否。
- `int` は**先頭ゼロを畳みながら** 10 桁まで数える。9000 桁の `0` も `-0…0` も 0 として受理し、
  畳んだ後が 11 桁以上、または int32 の範囲外なら拒否。u32 の累算で桁溢れを事前に見るので
  64bit 整数を使わない。
- 行長の上限を持たない (1 バイトずつ引き取るストリーム)。列ごとの上限だけで判定する。

**Python と違う点 (意図的)**:

- 誤りが複数あるときに**どれを報告するか**が違う。Python は CR → UTF-8 → 行の規則の順に
  ファイル全体を見るので、行 2 の規則違反と行 9 の CR があれば CR を報告する。C はストリーム順に
  最初に出会ったものを報告する。**accept / reject の判定は一致**するので、試験はそこだけを固定した。
- 重複 `(scope, key)` は reader では見ない。流し込み先の `settings` が
  `PRIMARY KEY (scope, key)` なので、素の `INSERT` が `SQLITE_CONSTRAINT` で弾く
  (レビュー往復 1 の ⑯)。行数にも名前の長さにも上限が無く、判定は Python と一致する。
  **突き合わせの単位は reader 単体ではなく `cfg_init`** (その tsv で DB が作れるか)。

---

## 3. 票 §1〜§2 の実装で決めた細部

1. **`cfg_open` は「二重 open」を `OS32_ERR_INVAL` で断る**。`CfgDb` は静的 1 本 (票 §1-4) なので、
   1 プロセス 1 接続をライブラリ側で強制する。`cfg_close` の後は再び開ける。
2. **読みの引数検査は長さだけ**。`set` / `delete` は scope / key の字句規則 (`[a-z0-9_]`、
   `app:<name>`) と UTF-8 まで検査するが、`get` / `enum` は 1〜63B であることだけを見る。
   規則から外れた key が DB に入っていても (`dbq` で手で入れた等) 読み出せなくならないため。
   票 §1-3 が上限と規則を set / delete の側に置いているのに合わせた。
3. **空の text / blob は NULL にしない**。`bind_text(…, "", 0)` / `bind_blob(…, p, 0)` を
   非 NULL のポインタで渡す (KAPI は長さ 0 でも `db_user_range_ok(p, 0)` に NULL を渡せない)。
4. **`cfg_enum` の callback が非 0 を返したら中断**し、それまでに呼んだ件数を返す。
   `cfg_enum_scopes` も同じ。
5. **`cfg_init` の失敗理由**は `cfg_last_init_reason()` で引く (票 §1 に無い**追加**、§5 参照)。
   戻り値自体は `OS32_ERR_EXIST` / `NOTEMPTY` / `INVAL` / `NOTFOUND` / `IO` に写像している。
6. **`cfg_init` は tsv を 2 巡する**。1 巡目は検証だけ (`emit = NULL`)、通ったときだけ `.new` を
   作って 2 巡目で流し込む。「全行検証してから作る」を素直に満たす代わりに読み直しが 1 回増える。
7. **`cfg_init` の `db_close` 失敗コードは `cfg_last_close_error()` に残す**。`cfg_close` と
   同じ欄を使う (「直前の close 失敗」という意味は同じ)。コマンドはこれで
   `init failed: close` を出し分ける。
8. **`meta.created`** には `get_tick()` の 10 進を入れる (DESIGN §3 では情報のみ)。
   `mk_settings_db.py` は ISO8601 を入れるが、列の型 (TEXT) は同じ。
9. **`cfg list` は blob の中身を出さない** — `blob:<n>` と長さだけ。4096B の hex は 8192 文字で
   con_sink (8KB) を溢れさせるので、hex が要るときは `cfg get` を使う。
10. **コンソール出力は DB を閉じた後**に 1KB ごと `sys_yield` を挟んで書く (票 §2)。
    `cfg list` は 8KB の溜め場が一杯になったら **DB を閉じて吐き出し、開き直して続きから**
    出す (列挙は `(scope, key)` 順なので最後に出した組より後だけを出せば再開できる)。
    これで「open〜close の間に yield しない」を崩さずに件数の上限も作らない。
11. **`cfg export` も DB を閉じてから書く**。list と同じ再開の仕組みで、溜め場が
    一杯になったら DB を閉じて出力ファイルへ流し、開き直して続きを書く
    (レビュー往復 1 の non-blocker)。標準出力へ出すのは要約 1 行だけ。
    途中で落ちたら出力ファイルを消す (中途半端なバックアップを残さない)。
12. **終了コード**: `status` は `CFG_OK` のときだけ 0。`get` は既定値を使ったときも
    「(not set)」のときも 0 (`CFG_ERROR` と close 失敗だけ 1)。`set` / `del` は
    MISSING / CORRUPT / VERSION / ERROR で 1。`export` は MISSING / CORRUPT で 1。
13. **`cfg get` の型引き**は `cfg_get_info()` の**完全一致 1 行照会**。列挙は使わない
    (レビュー往復 1 の ⑪: 同じ scope に 257 件あるだけで NOSPC になっていた)。
    型が分かってから `cfg_read_int` / `cfg_get_text` / `cfg_get_blob` を 1 回呼ぶ。

---

## 4. ホストでは踏めなかったもの ([V4])

- **票 §5 の C1〜C7 (ゲスト受入)** — 配備もエミュレータもコーダーの範囲外。`cmd` ケースは
  C1〜C4 と同じ手順をホストで踏んだだけで、CPL=3 / 端末 / `hsync` の検証にはならない。
- **`PRAGMA user_version` がカーネルの SQLite では効かない** — `os32_sqlite_config.h` の
  `SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS` により、読みも書きも無効 (エラーにはならず無視される)。
  `cfg init` が作る DB の `user_version` は **0 のまま**で、`mk_settings_db.py` が作る媒体の
  マスタ (ホストの CPython SQLite、1 が入る) と 1 点だけ形が違う。版の正典は
  `meta.schema_version` で、`libos32cfg` は `user_version` を読まないので動作には影響しない。
  `PRAGMA user_version=1` の実行自体は将来の再有効化に備えて残してある。**要判断**。
- **`.new` を隔離された接続が掴んだままの状態** — F1 の隔離 slot は同じプロセスからしか作れず、
  「別プロセスが掴んだ `.new`」はホストでは作れない。試験は「残骸があれば拒否して消さない」
  という**振る舞い**だけを固定している (票 §1-7c の根拠もそこにある)。
- **ext2 の rename の実物** — `fs/ext2_dir.c` の「新名追加 → 旧名削除」を模型で注入している。
  `st_nlink` は模型では常に 1 で、ライブラリは nlink を見ない (見ずに「両名あれば触らない」)。
- **con_sink の溢れ** — `sys_write` はホストでは捕捉バッファなので、1KB ごとの yield が
  実機の端末で十分かはゲストでしか測れない。
- **SQLite プールの実測** (票 §5 C7 / DESIGN §6) — `db_mem_used` の推移は実機の話。

### ホスト固有の注意 (試験を読む人向け)

`os32_kapi_shared.h` の `i32` は `signed long` なので **ホスト (LP64) では 8 バイト**になる。
`kapi_db.c` の `shm_write_row` は整数列を 4 バイト刻みで並べるため、ホストで `i32` として
読むと隣の列の下位 32bit が混ざる。`libos32cfg` は必ず `(int)` に落としてから使うので影響は
無いが、**試験側で `cfg_i_col_int()` を直接比べるときは `(int)` を付ける**こと
(付け忘れて 1 度落ちた)。i386 では `i32` が 4 バイトなのでこの差は出ない。

---

## 5. 票からずらした点

| 何を | なぜ |
|---|---|
| `cfg_last_init_reason(void)` を**追加**した (票 §1 の一覧に無い) | `cfg` コマンドが `already exists` / `needs recovery: journal present` / `needs recovery: stale .new` / `init failed: close` / `needs recovery: rename left both names` を出し分けるには理由が要る。`OS32_ERR_*` は新規番号を取らない規則 (票 §1-8) なので既存番号に写像すると 5 通りが潰れる。**§1 の既存シグネチャは 1 つも変えていない** (S2-W が依存する `cfg_get_*` を含む) |
| `libos32cfg` を 5 つの翻訳単位に分けた | 票は「`userland/lib/cfg/`」としか言っていない。`cfg_enum` の 19KB と `cfg_init` の 11KB の bss を、読むだけのアプリ (S2-W / gshell) に背負わせないため |
| `cfg list` が blob を `blob:<n>` としか出さない | 票 §2 は「blob は hex を出す」を `get` について書いている。`list` で 8192 文字を並べると con_sink を溢れさせるので長さだけにした |
| tsv の重複検出を reader ではなく `settings` の PRIMARY KEY に任せた | C 側で全行の `(scope,key)` を覚えると 32KB 級の bss が要る。DB に任せれば正確で上限も無い (レビュー往復 1 の ⑯) |

---

## 6. PM が登録する行 (コーダーは触っていない)

`build/libs.mk`:

```make
# 依存なし (KAPI の共有ヘッダだけで完結する)
INC_libos32cfg      = -Iuserland/lib/cfg

# libos32cfg — 設定レジストリ (/etc/settings.db) のクライアント
$(eval $(call DEFINE_LIB,libos32cfg,userland/lib/cfg,,))
LIBCFG_OBJ = $(LIBDIR)/libos32cfg.a
```

`ALL_LIB_ARCHIVES` に `$(LIBDIR)/libos32cfg.a \` を 1 行足し、`clean-libs` に
`rm -f userland/lib/cfg/*.o` を足す。

`build/programs.mk` — `cfg.bin` は既定の `userland/cmds/%.elf` パターンではライブラリを
引けないので明示規則が要る:

```make
# cfg — 設定レジストリの CUI (libos32cfg を静的リンク)
userland/cmds/cfg.o: userland/cmds/cfg.c userland/lib/cfg/libos32cfg.h
	$(CC) $(PROGRAM_FLAGS) -c $< -o $@

userland/cmds/cfg.elf: sdk/link/app.ld $(CRT0_OBJ) userland/cmds/cfg.o $(LIBCFG_OBJ)
	$(LD) $(PROGRAM_LDFLAGS) -o $@ $(CRT0_OBJ) userland/cmds/cfg.o \
	      $(LGRP_BEG) $(LIBCFG_OBJ) $(LGRP_END) -lc -lgcc
```

(S2-W の shlib も `libos32cfg.a` を足す — そちらは W が報告する。)

`build/app.conf`:

```
userland/cmds/cfg                          50  0
```

`userland/deploy.yaml` — `userland/cmds/*.bin` の glob (`/bin/`) に自動で乗るので
**追加は不要**。`/usr/bin/cfg.bin` として置きたいなら明示エントリを 1 行足す:

```yaml
    - host: userland/cmds/cfg.bin
      guest: /usr/bin/
      tags: [programs]
```

`build/sdk.mk`:

```make
SDK_LIB_HEADER_DIRS = math gfx db ui input asset snd tilemap md filer mgx save ecs cfg

# libos32cfg / cfg コマンドのホスト TDD (票 S2-C)。実 SQLite + 実 kapi_db.c + RAM backend。
check-cfg-host:
	python3 -B tools/tests/test_cfg.py
```

`check:` と `.PHONY:` の並びに `check-cfg-host` を足す。

---

---

---

---

## §W — libos32gui の `os32gui_cfg_*` wrapper (S2-W)

### 対象と道具

| | |
|---|---|
| 実装 | `userland/rust/libos32gui/src/cfgro.rs` (表 101..=104 の実体) |
| 試験 | `userland/rust/libos32gui/host_tests/` (`src/lib.rs` = 34 本、`tests/init_gate.rs` = 1 本、`src/fake.rs` = C の贋物) |
| 実行 | `cargo test --manifest-path userland/rust/libos32gui/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline` |

`src/lib.rs` が `#[path = "../../src/cfgro.rs"]` で**実装そのもの**を取り込み、
`src/fake.rs` が `cfg_open` / `cfg_close` / `cfg_get_int` / `cfg_get_text` /
`cfg_begin` / `cfg_set_int` / `cfg_set_text` / `cfg_commit` / `cfg_rollback` の
9 本を `#[no_mangle]` で定義してリンクを閉じる。**実 DB も SQLite も出てこない**
(それは §C の `test_cfg.py` の領分)。贋物は
**呼び順 (`Call` の列) と引数を記録**し、試験が仕込んだ戻り値を返すだけ。

`cargo test` は試験ごとに別スレッドで走るので、贋物の状態は `thread_local!`。
`kapi` (プロセスに 1 語の `static mut`) への書きだけは `fake::reset()` の中の
`std::sync::Once` で **1 回に畳んで直列化**した — 毎回書くと同じ値でも並列試験
どうしの競合になる (レビュー往復 2 の non-blocker)。
ただし `kapi` (下の W29) は**プロセスに 1 語**なので、init 前の分岐だけは
cargo が別バイナリ = 別プロセスにする `tests/init_gate.rs` に置いた
(`src/lib.rs` 側は `fake::reset()` が毎回 init 済みにする)。

### 固定した分岐 (35 本)

| # | 試験 | 固定した振る舞い |
|---|---|---|
| W01 | `fold_get_int` | 全成功 = 値 / open 失敗 = def / **close 失敗 = def** / 負の設定値はそのまま通す |
| W02 | `fold_get_text` | open 失敗 = その値 / NOTFOUND = そのまま / close だけ失敗 = `ERR_IO` / **get と close の両方が失敗なら get (直前の失敗) を優先** |
| W03 | `fold_set` | open → work → close の順に見る / **work と close の両方が失敗なら work を優先** |
| W04 | `scope_is_app` | `app:a` `app:filer` `app:my_app9` は可。`app:` (本体が空) `system` `gshell` `user` `App:x` `app:X` `app:a-b` `app:a/b` `xapp:a` は不可。63B ちょうどは可、64B は不可 |
| W05 | `copy_cstr` / `copy_value` | scope / key は空を拒否・63B まで・埋め込み NUL を拒否。**値 (text) は空を許す** (NULL とは別の「空値」)・255B まで |
| W06 | get_int 正常 | `Open(0)` → `GetInt` → `Close` で閉じきる。scope / key が NUL 終端で C へ渡る |
| W07 | get_int open 失敗 | def。**close を呼ばない** |
| W08 | get_int open が NULL | 0 を返しながら NULL を置かれたら def。**触らない** |
| W09 | get_int 未設定 | `cfg_get_int` が def を返す (MISSING / CORRUPT / 未設定) → def |
| W10 | get_int close 失敗 | **読めた 42 を捨てて def** |
| W11 | get_int 引数不正 | 空 scope / 空 key / 64B scope / 埋め込み NUL → def。**DB を開かない** |
| W12 | get_text 正常 | 長さ 4、`out` は `"blue\0"`、cap がそのまま C へ |
| W13 | get_text open 失敗 | その負値を素通し |
| W14 | get_text NOTFOUND / NOSPC | そのまま返し、**`out` を書き換えない** |
| W15 | get_text close 失敗 | `ERR_IO` |
| W16 | get_text get + close 失敗 | NOTFOUND (直前の失敗を優先) |
| W17 | get_text 引数不正 | `out` が NULL / cap 0 / 空 scope / 空 key → `INVAL`。**DB を開かない** |
| W18 | set の scope の門 | `system` `gshell` `user` `app:` `app:BAD` `""` → `ERR_PERM`。**DB を開かない** (set_int / set_text の両方) |
| W19 | set_int 正常 | `Open(1)` → `Begin` → `SetInt` → `Commit` → `Close` を 1 呼び出しで閉じる。scope / key / 値が C へ届く |
| W20 | set_text 正常 | 同じ順。**空値も書ける** |
| W21 | set_text 値の上限 | 255B は可、256B は `INVAL` で**開かない** |
| W22 | set open 失敗 | その負値。begin も close も呼ばない |
| W23 | set begin 失敗 | `INVAL` (MISSING / CORRUPT / VERSION)。**`ROLLBACK` を呼ばない** (BEGIN していないので) |
| W24 | set 失敗 | `Rollback` してから `Close`、返すのは set のコード |
| W25 | commit 失敗 | `Commit` → `Rollback` → `Close`、返すのは commit のコード |
| W26 | set close 失敗 | commit 済みでも `ERR_IO` (握りつぶさない) |
| W27 | set + close 失敗 | set のコード (直前の失敗を優先) |
| W28 | set key 不正 | 空 key / 64B key / 埋め込み NUL → `INVAL`。**DB を開かない** |
| W29 | `shlib_init` 前の呼び出し (`tests/init_gate.rs`、別プロセス) | get_int = def / get_text・set_* = `ERR_INVAL`。**`cfg_open` を呼ばず `out` にも触らない**。`set_kapi` の後はふつうに読める |
| W30 | `raw_span_ok` | `len > max` は不可 / 非 NULL + len 0 は空 / **NULL は len 0 のときだけ** / `ptr + len` の折り返しは不可 |
| W31 | `raw_out_ok` | NULL・cap 0・`i32` に収まらない cap・折り返しは不可 |
| W32 | set_text に `s = NULL, s_len = 1` | `ERR_INVAL`。**DB を開かない** (= 既存値を空 text で上書きしない) |
| W33 | set_text に `s = NULL, s_len = 0` | 0。**空値として書ける** (NULL 終端の無い空) |
| W34 | 4 本すべてに `len = u32::MAX` | scope / key / 値のどれでも、**スライスを作る前に** def / `ERR_INVAL`。DB を開かない |
| W35 | scope / key が `NULL, len = 1` | 4 本とも def / `ERR_INVAL`。空スライスに化けない |

### 追記 (2026-09-13、着地後のリンク失敗の修正)

着地した `6aa8b7a` の `make all` が shlib のリンクで落ちた:

```text
libos32cfg.a(cfg_backend.o): undefined reference to `kapi' (be_db_open_existing 等)
```

`cfg_backend.c` の `extern KernelAPI *kapi;` は**アプリの .bin では
`sdk/crt/crt0_c.c` が定義する**が、**shlib には crt0 が無い**。libos32gfx が
`libos32gfx_attach(api)` で自前に持つのと同じ構図なので、shlib 側で実体を出す:

- `cfgro.rs` に `#[no_mangle] pub static mut kapi: *mut c_void = null_mut();`
  (os32api に依存させないため型は不透明ポインタ)。
- `os32gui_shlib_init(api)` が `os32api::os32_init(api)` の直後に
  `cfgro::set_kapi(api as *mut c_void)`。
- C 側 (`cfg_backend.c`) は**変えていない** (アプリ側では crt0 の `kapi` がそのまま)。

shlib の `.data` / `.bss` はアプリごとの物理ページ (K3) なので、この 1 語も
アプリごとに別。リンク後の実配置で確認した:

```text
$ i386-elf-objdump -t userland/libos32gui.elf | grep -w kapi
00419e48 g     O .bss   00000004 kapi          ← data_vaddr 0x416000 + 4 ページの中
```

`shlib_init` 前に表 101..=104 を呼ばれると C の backend が NULL を辿るので、
wrapper 4 本に門を足した (W29):

| 呼び出し | `kapi` が NULL のとき |
|---|---|
| `os32gui_cfg_get_int` | `def` |
| `os32gui_cfg_get_text` | `ERR_INVAL` (`out` に触らない) |
| `os32gui_cfg_set_int` / `set_text` | `ERR_INVAL` (scope が正しくても通さない) |

いずれも **`cfg_open` を呼ばない**。

### 追記 (2026-09-13、Codex 実装レビュー 往復 1 の ⑮)

**ptr + len の検証がスライス作成より後にあった。** `slice(ptr, len)` が
`ptr.is_null() || len == 0` を空スライスに畳んでいたので:

1. 有効な scope / key と **`s = NULL, s_len = 1`** を `os32gui_cfg_set_text` に
   渡すと空スライスに化け、`copy_value` も通り、**既存値を空 text で上書きして
   0 (成功) を返した**。
2. 非 NULL + 巨大 `len` は、63 / 255B の拒否より**前**に
   `core::slice::from_raw_parts` の前提を破っていた。

直し: 生の引数の段階で検査してからスライスを作る。

```rust
pub fn raw_span_ok(ptr: *const u8, len: u32, max: u32) -> bool {
    if len > max { return false; }          /* 上限は slice の前 */
    if ptr.is_null() { return len == 0; }   /* NULL は「空」のときだけ */
    (ptr as usize).checked_add(len as usize).is_some()   /* 折り返さない */
}
pub fn raw_out_ok(out: *const u8, cap: u32) -> bool { /* 書き込み先も同様 */ }
unsafe fn checked_slice(ptr, len, max) -> Option<&[u8]>  /* 通らなければ None */
```

`os32gui_cfg_get_int` / `get_text` / `set_int` / `set_text` の **ptr + len 引数
すべて** (scope / key / s / out) をこれに通した。`set_*` では
`scope_is_app` の**前**に scope の span を見る (NULL + len != 0 の scope は
`ERR_PERM` ではなく `ERR_INVAL`)。

RED (検査を `checked_slice` の中からスライス作成の後へ戻した素朴版):

```text
test tests::w32_set_text_null_value_with_nonzero_len_is_rejected ... FAILED
  assertion `left == right` failed
  left: 0        ← 空 text で上書きして「成功」
 right: -9
test result: FAILED. 33 passed; 1 failed
```

GREEN: 35 本 (`src/lib.rs` 34 + `tests/init_gate.rs` 1) すべて通過。

W30 / W31 / W34 / W35 は**戻り値だけでは素朴版と区別がつかない**ものを含む
(巨大 `len` は素朴版でも `copy_cstr` の長さ判定で `ERR_INVAL` になり、
`ERR_PERM` は `ERR_INVAL` と同じ -9)。これらは
`from_raw_parts` の前提を破らせないための**番人**として置いている。

### RED

素朴な実装 (下の 4 点) に戻して同じ 28 本を走らせた結果:

1. `fold_get_int` が `close_rc` を見ない
2. `fold_get_text` が close を get より**先に**見る
3. `fold_set` が close を work より**先に**見る
4. `set_*` に `scope_is_app` の門が無い

```text
running 28 tests
...
test result: FAILED. 21 passed; 7 failed; 0 ignored; 0 measured; 0 filtered out

w01_fold_get_int_is_def_on_any_failure    close 失敗 → def        left: 5   right: 7
w02_fold_get_text_prefers_earlier_failure 直前の失敗を優先        left: -1  right: -2
w03_fold_set_prefers_earlier_failure      直前の失敗を優先        left: -1  right: -9
w10_get_int_close_failure_is_def          close 失敗なら値も捨てる left: 42  right: 7
w16_get_text_get_failure_wins_over_close  直前の失敗を優先        left: -1  right: -2
w18_set_refuses_os_scopes_without_opening scope "system"          left: 0   right: -9
w27_set_earlier_failure_wins_over_close   直前の失敗を優先        left: -1  right: -9
```

落ちた 7 本が、票 §3 が名指しした分岐 (close 失敗の扱い、直前の失敗の優先、
`app:` scope の門) そのものであることを確認した。

### GREEN

`cfgro.rs` を票どおりの実装に戻して再実行:

```text
running 28 tests
test result: ok. 28 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out

running 1 test          (tests/init_gate.rs — W29)
test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

### 併せて通した検査

```text
$ python3 tools/mkshlib.py --check
  mkshlib: 番号表 OK (105 本, version=1)

$ python3 tools/check_gui_proto.py
GUI プロトコル照合: 定数 105 件 / 構造体 31 件を突き合わせ
  一致

$ cd userland/rust && cargo build --release -p libos32gui -p libos32gui_stub -p filer -p gui_demo --offline
    Finished `release` profile [optimized] target(s)
```

### shlib のリンク (2026-09-13 に通った)

```text
$ CROSS_DIR=~/opt/cross make userland/libos32gui.elf userland/libos32gui.shlib
i386-elf-ld ... --start-group libos32gfx.a libos32math.a libos32cfg.a liblibos32gui.a --end-group -lc -lgcc
  mkshlib: 番号表 OK (105 本, version=1)
  SHLIB: libos32gui.shlib (nfunc=105, version=1, text_pages=22,
         data_vaddr=0x416000, data_pages=4, raw=105504, bss=596)
```

`CROSS_DIR` を渡しているのは、この作業環境のクロス一式が
`~/opt/cross` にあり `build/config.mk` の既定 (`/usr/local/cross`) と
違うため (S2 の変更とは無関係)。

### まだ通っていないもの ([V4])

`make all` / `make check` の全体ゲートと**ゲスト上の確認 (票 §5 の C1〜C7) は
していない**。W レーンで通したのは上の targeted make と `make check-shlib` /
`check-gui-proto` / `check-gui-host` だけ。
