# K4: シェル切替 (`sys_switch_shell`)、`/etc/system.cfg`、テキスト GDC 制御、`os32gui` コマンド

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> (同日改訂: 契約 T9 のシェル切替を追加) / レーン: K / 前提: なし (今すぐ着手可)。gshell の実体は W1。**W1 は本票の 0 に依存する**
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / ロードマップ: `docs/ROADMAP.md` v1.1「CUI → GUI 切替フロー」
> 排他: `kernel/kernel.c` (シェル起動ループ)、`kernel/console.c`、`userland/shell/**`

## ゴール

ROADMAP の切替フローをカーネルとシェルに実装する。gshell が無くても
(`/bin/gshell.bin` が無い場合) CUI に落ちて起動できること。

## 作業

0. **シェル切替 `sys_switch_shell(path)`** (契約 T9。KAPI v41、K1 と同じ追記で): shell 帯
   (owner 1) からのみ受理し、パスをカーネルに記録して 0 を返す。呼んだシェルは自分で
   `exit` する。`kernel.c` のシェル起動ループは、記録があればそれを消費して Level 1 として
   `MEM_SHELL_LOAD_ADDR` に `exec_run` し、無ければ `system.cfg` に従う。shell.bin と
   gshell.bin が同時に載ることは無い (同じ帯域)。切替を 3 往復して kselftest / rshell が
   生きていることが完了条件。
1. **`/etc/system.cfg` 解析**: `kernel.c` のシェル起動ループで `GUI=0/1` を読む
   (行指向、`KEY=VALUE`、`kstr*` のみ)。1 なら `/bin/gshell.bin` を Level 0 として起動、
   無ければ警告を出して `shell.bin`。gshell が exit で戻ったら cfg を再読取して分岐
   (ROADMAP の「GUI終了プロセス」5〜6)。
2. **テキスト GDC 制御**: `console_text_gdc_stop()` / `start()` を `console.c` に。
   gshell が `gfx_init` (H1 の `enter`) の前後で呼ぶ KAPI として **K1 の v41 に相乗り**
   させるか、gshell が CPL=0 なので内部関数を直接呼ぶかは K が決める (後者なら KAPI 不要)。
   9801 バックエンドはクローム描画に TVRAM を使える (DESIGN R4) ので、STOP するのは
   `GFX_CAP_TEXT_OVERLAY` が 0 のバックエンドのときだけ。判定は gshell 側 (W1)。
3. **`os32gui` コマンド** (`userland/shell/cmd_sys.c`): 引数なしは
   `sys_switch_shell("/bin/gshell.bin")` して exit (再起動しない、その場で切替)。
   `os32gui on|off` は `system.cfg` の `GUI=` を書くだけ (次回起動から)。
4. **ハング復旧の確認**: FDD ブート → `mount /hd0` → `edit /hd0/etc/system.cfg` の
   手順が今の FDD イメージで通ることを 1 回確かめて票に残す。

## 完了条件 (ゲート G3 の起動部分)

- `os32gui` → その場で gshell に切り替わる (W1 完了後、再起動なし)。W1 前は
  `sys_switch_shell` が「gshell.bin が無い」で失敗し CUI のままであることを確認。
- gshell から「CUI へ」→ `shell.bin` に切り替わる。3 往復して kselftest / rshell が生きている。
- `os32gui on` + 再起動 → 起動時から gshell (`system.cfg` 経路)。
- `GUI=1` のまま gshell がクラッシュ (`fault_kill`) しても CUI ではなく gshell を再起動する
  ループに入らないこと: 連続 3 回失敗したら CUI に落とす保険を入れる。
