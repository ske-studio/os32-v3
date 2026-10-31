# kernel/gui.c — 常駐 WM の登録と呼出し

> 状態: **現行 (2026-10-31)**

GUI 操作を登録された WM ハンドラへ転送する。
所有者終了の通知と IME 描画関数の登録も仲介する。

## 落とし穴

- アプリが後で CPL=0 から呼ばれる関数表を登録できると、終了後の解放済みコードへ
  飛ぶこともある。`gui_register` と `gui_ime_set_render` は owner 1 かつ
  ユーザーからの呼出しでないことを要求する (`kernel/gui.c:103`, `kernel/gui.c:155`)。
  関数ポインタの番地だけで呼出し元の信頼性を判断しない。
  `make check-ring3-guard-host` と `kdebug/kselftest/ime_render_gate.inc` が検査する。

## 内部設計と契約 (D1)

- GUIの118/146行だけ `input_set_gui_mode(1/0)`。cooked抑止開始/解除の順序、登録失敗時・終了時の復帰位置を保つ。

---

## T. 呼び出し経路 (アプリ ⇄ WM)

### T1. KAPI 入口は 1 本

```c
i32 gui_call(u32 op, u32 arg);
```

- `op` は追記のみ (ABI2 と同じ)。`arg` は op ごとの小さな値 (ハンドルなど)。
- 大きな引数と応答は SHM の固定ブロック (T2) 経由。**ポインタは渡さない**。
- 同期呼び出しだが、**WM からアプリへのコールバックは無い** (再入の根絶)。
  WM がアプリへ伝えたいことはすべてイベント (T3) になる。

- 内部通知 OWNER_EXIT の arg は EXEC_KIND_*。ABORTED は未処理 STOP を合流して消費する。
  gui_owner_exit は直接 WM を呼ぶ。公開 gui_call の OWNER_EXIT は OS32_ERR_INVAL。

OWNER_EXIT の内部 arg は EXEC_KIND_*。ABORTED (3) は窓の除去前に WM の
abort_seen と退避 raw の CTRL+STOP make を捨てる。カーネルも未読 raw の同 make を
除去する。1 個の STOP が前景変更後の相手を二度目に畳まないための内部契約で、
公開 KAPI の変更はない。

- KAPI `sys_switch_shell(path)` (K4、shell 帯からのみ): 次に起動するシェルのパスをカーネルに
記録する。呼んだシェルは自分で後始末をして `exit` する。

### U8. 所有者と後始末

OWNER_EXIT はカーネル内部専用で arg = EXEC_KIND_* (T4)。ABORTED は WM の
捕捉済み・退避済み STOP も消費してから所有者の窓を除去する。

### 9.2 決定 (承認 2026-09-04)

- **ウィンドウマネージャ (Z 順・フォーカス・ウィンドウ一覧・入力の配送) は常駐の gshell に
置く。**
`gui_call(op, args)`
経由で呼び、要求と応答は SHM で渡す。

**Wayland 型の協定** (非同期・ID 参照・損傷 + commit・入力 serial・バージョン付き)
イベントは 1 回で一括取り出し
