# exec/ring3_str.c — ユーザー向け文字列とアクセス判定の補助

> 状態: **現行 (2026-10-31)**

CPL=3 に返す文字列を写し、ページ権限とガードの判定を補助する。
実際のページ走査と実行文脈は呼出し元が管理する。

## 落とし穴

- WM はアプリの syscall の中でも動くため、syscall 中というだけで WM の出力先を
  ユーザーポインタ扱いすると、常駐シェルのスタックを拒否してアプリを落とす。
  `exec/ring3_str.c:60` は WM 深さを見て現在の呼出し文脈を判定する。
  一方、以前登録したバッファは登録時の由来を保持して検査する
  (`fs/fd_redirect.c:99`, `exec/redir_access.c:162`)。
  `make check-ring3-guard-host` と `make check-access-host` が各経路を検査する。
