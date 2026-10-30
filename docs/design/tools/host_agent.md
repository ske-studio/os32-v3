# tools/host_agent.py — Host Services のホスト側常駐

> 状態: **現行 (2026-10-31)**

EtherType 0x88B5 のリンクを介してクリップボードや印刷などを扱う。

## 落とし穴

- **常駐不在と待ち時間**: エージェントがいないと Host Services の open は `HOST_ELINK` になる。
  `tools/host_agent.py:1305` の `--listen` は既定で 127.0.0.1:8026。接続先とエミュレータの `/api/net` の実状態を照合する。
  最初の open は最大 300 tick 待つ (`userland/lib/host/libos32host.h:54`) ため、途中の画面を完了結果と取り違えない。
  `tools/tests/host_lib_host.c:361` が待ちとタイムアウトを検査する。
