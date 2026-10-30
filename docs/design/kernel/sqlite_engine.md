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
