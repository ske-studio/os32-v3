# kernel/sqlite_engine.c — SQLite エンジンの実行とモジュール管理

> 状態: **現行 (2026-10-31)**

SQLite モジュールを管理し、専用スタック上で呼出しを実行する。
再入や割込み文脈からの実行を制限する。

## 落とし穴

- 複数の接続を開いたままにすると共有 MEMSYS5 プールが枯渇し、後の DB 処理が失敗する。
  プールは `lib/sqlite3/os32_sqlite_vfs.c:21` で管理され、アプリ側の割当には
  予約を残す制限もある (`lib/sqlite3/os32_sqlite_vfs.c:54`)。
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
