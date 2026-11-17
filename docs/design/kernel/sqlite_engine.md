# kernel/sqlite_engine.c — SQLite エンジンの実行とモジュール管理

> 状態: **現行 (2026-10-31)**

SQLite モジュールを管理し、専用スタック上で呼出しを実行する。
再入や割込み文脈からの実行を制限する。

## 落とし穴

- 複数の接続を開いたままにすると共有 MEMSYS5 プールが枯渇し、後の DB 処理が失敗する。
  プールは `lib/sqlite3/os32_sqlite_vfs.c` で管理され、アプリ側の割当には
  予約を残す制限もある (`lib/sqlite3/os32_sqlite_vfs.c` の `budget_malloc()`)。
  利用を終えた接続とステートメントを閉じ、失敗時は DB の診断を確認する。
  `make check-sqlite-engine-host` がエンジンの実行契約を検査する。

## 内部設計と契約 (D1)

辞書が `stat` で NOTFOUND のときだけ SQLITE_ONLY とし、SQLite の失敗・辞書ありの FEP init 失敗・fep.mod 欠落は従来どおり MINIMAL。
**SQLite / FEP の失敗は回復構成 (MINIMAL) へ移る**
画面に失敗したモジュールと理由、「MINIMAL で起動」を表示し、`db_*` は `OS32_ERR_NOSYS` 系、FEP は API 別 stub (§5-6)。

起動時SQLite → settings close → FEP、既定192KiBを優先予約。settingsで引下げも可、下限未満は診断して丸める
APP+SYSTEM上限 (既定319,488B、予約の増減に追従) を守り、FEP取り分を侵す割当はNULL。

| 項目 | 決定 |
|---|---|
| MINIMAL | SQLite を読まないので取り分も無い (D21・D26) |

- kernel/sqlite_engine.h に内部型 `ServicesVerState { u32 stack_used, stack_size, public; char state[24]; }` と `void services_ver_state(ServicesVerState *out);` を宣言する。
- kernel/sqlite_engine.c が high_water、SQLITE_ENGINE_STACK_SIZE、services_public、既存 services_state_name() をコピー。getter は出力・I/O・割当なし。collector が一回呼ぶ。

## 辞書の probe と起動の予約 (DICT_META)

> 出所: DICT_META_DESIGN・TASK_DICT_META (2026-11-18 に要約して移した)。関係するソース: `kernel/sqlite_engine.c` (`services_boot`・`services_settings`・`fep_meta_probe`・`fep_boot_config_get`・`fep_boot_user_path_get`)・`lib/sqlite3/os32_sqlite_vfs.c` (`os32_sqlite_boot_check`)

core は FEP 部品を読む前に辞書を検査し、FEP の予約を決めてから FEP を読む。辞書の形式は [システム辞書の形式](../tools/fep_to_sqlite.md)、FEP と共有する型は [ime_store.h の契約](ime/ime_store.md) にある。

### 起動の順 (`services_boot`)

1. SQLite を起こす。`services_settings` が settings.db を短い常駐の接続で読み、閉じる。
   - `fep_mem_reserve_kb` (type=0 の int): あれば予約の値。99 KiB 未満は 99 KiB に丸めてログを出す。KiB から byte への桁あふれは -1 にして後の照合で落とす。
   - `fep_user_dict_path` (type=1 の text): 学習 DB のパスの写し。DB・表・行が無ければ既定 `/etc/fep_user.db`。読めなければその rc を写しに残す (学習だけが止まる)。
2. `os32_sqlite_outer_boot_check(0, 0)`。辞書 `/db/fep.db` が無ければ SQLITE_ONLY (予約 0、FEP なし)。
3. `fep_meta_probe("/db/fep.db")`。**失敗すると MINIMAL へ戻る** (学習の失敗と違い、起動の辞書の形の誤りは FEP 全体を止める)。
4. 予約を決める: settings の値 → 辞書の `mem_reserve_kb` (99 KiB 未満は丸める) → 既定 192 KiB の順。
   `os32_sqlite_outer_boot_check(0, reserve)` が容量と課金を照合する。
5. probe の結果と予約を core の写し (`boot_meta`・`boot_reserve_kb`) に保存してから `fep_module_load`。FEP は初期化で `fep_boot_config_get`・`fep_boot_user_path_get` から写しを受け取る。
6. FEP の初期化の後、`os32_sqlite_outer_boot_check(ime_boot_connections(), reserve)`。常駐の接続の実数 (学習ありで 2、なしで 1) と一致しなければ MINIMAL。
7. ここまで済んでから `services_public=1` (アプリの `db_open` を開く)。

失敗の戻り (rollback) では写しを消し (`boot_config_saved=0`、学習のパスは `AGAIN`)、FEP の文と group を先に片付けてから SQLite を止める。

### `fep_meta_probe` の契約

- 先頭 100 byte を VFS で読み、magic (`SQLite format 3`)・page_size 1024・user_version を SQL より先に検査する。user_version が 1 を超えれば `INVAL`。
- 一時の **常駐** の group (EXEC ではない) を取り、RO で開き、`dict` の 4 列を `LIMIT 0` で確かめる (行は読まない)。
- 形式 1 は meta を全部読み、キーごとに検査する。形式 0 は既定値を入れる。
- 帰る前に必ず group を閉じる。予約は変えず、FEP も読まない。結果は成功のときだけ写す。
- 切替のときは FEP が同じ口を import して呼ぶ (system・user と合わせて常駐 3 本、`OS32_SQLITE_RESIDENT_GROUPS` = 3)。

### 予算

- 予約は MEMSYS5 の 512 KiB の単一のプールの中の FEP の取り分。アプリと SYSTEM の割当は `BUDGET_CAPACITY − reserve` まで (`lib/sqlite3/os32_sqlite_vfs.c` の `budget_malloc`)。
- FEP 専用の強制の上限は無い。FEP の最大の使用量 (2 接続・文・cache・切替の probe) が予約に収まることは受入の測定で確かめる。
- 切替で予約は増やさない。大きい辞書へは再起動で入る。

### 検査

- `fep-core-boot` (`tools/tests/test_fep_core_boot.py`): 実物の SQLite で probe の順・予約の優先順・丸め・接続数・rollback、probe の group の寿命。
- `sqlite-module-proto-host`: 起動の経路の既存の場合 (settings の予約が辞書より小さいときなど)。
- `fep-budget-host`・`fep-worst-load-host`: 2 接続と probe の最大の使用量と 192 KiB の基準。
