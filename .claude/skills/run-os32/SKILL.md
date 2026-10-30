---
name: run-os32
description: OS32 をビルドして、NP21/W の中で動いているゲストを叩く。起動確認、シェルコマンドの実行、画面のスクリーンショット、ファイルの持ち出し、HostDrv 経由の配備。run / start / build / screenshot / スモーク / 動作確認 の依頼で使う。NHD とブート領域への配備は扱わない。
---

# run-os32 — 組んで、動かして、叩く

OS32 は PC-9801/9821 向けの 32 ビット ベアメタル OS。**WSL で組み、Windows 側の
NP21/W (ai-debug fork) の中で走る。** だから「起動する」はホストでプロセスを
立ち上げることではなく、**すでに走っているゲストに手を届かせる**ことを指す。

その手は 1 本にまとめてある。

    .claude/skills/run-os32/driver.py

以下のパスはすべてリポジトリのルート (`~/os32-v3`) からの相対。

## 前提

エミュレータは**すでに動いている**必要がある。このスキルは NP21/W を起動しない
(起動は ini の選択を伴い、[D2] の承認対象)。まず次を実行する。

```bash
python3 .claude/skills/run-os32/driver.py doctor
```

実際の出力:

```
i386-elf-gcc             OK   ~/opt/cross/bin/i386-elf-gcc
nasm                     OK   /usr/bin/nasm
.env の鍵                  OK   3 個ある
HostDrv                  OK   /mnt/c/os32
NP21/W                   OK   phase=p2 protected_mode=1 eip=0x0010d130
```

`NP21/W NG` なら `python3 tools/np21w_ctl.py status` でプロセスを確認する。
使用中 ini は固定名と決めつけず、[os32-emu-config §2](../os32-emu-config/SKILL.md#構成切替の-3-経路--開始停止復帰) の管理された起動情報で確認し、
その ini の `aidebug` / `aidbport` を照合する。コマンド行だけでは起動後の設定切替を証明できない。

`.env` (gitignore 済み) に `CROSS_DIR` `NP21W_DIR` `HOSTDRV_DIR` の 3 つが要る。
**中身を出力に出さないこと** ([D3])。`doctor` は鍵の有無しか言わない。

## 実行 (エージェントの道) — まずこれ

### 生きていることを示す

```bash
python3 .claude/skills/run-os32/driver.py smoke /tmp/os32.png
```

`ver` / `ls /bin` / `echo` をゲストで回し、最後に画面を PNG で保存する。
**保存した PNG を実際に開いて見ること。** 真っ黒なら起動していない。

実際に出た `ver`:

```
PC-9801 OS32 v2.0 (Ring3 Native)
  CPU: Intel 386+ (Protected Mode + Paging)
  API: v55
  Build: Sep 16 2026 21:45:19
```

### 短いコマンド

```bash
python3 .claude/skills/run-os32/driver.py cmd 'ls -l /usr/bin/kstr_bench.bin'
```

### 長いコマンド (数十秒〜数十分)

```bash
python3 .claude/skills/run-os32/driver.py cmd --wait 'kstr_bench > /tmp/k.txt'
```

長い処理では HTTP が時間切れになることがある。timeout だけでは実行継続・通信不整合・停止を区別できない。
「実行制御と通信」 に従い、画面・状態・完了印を照合する。
`--wait` は完了印が出るまで待つ (仕組みは Gotchas)。

### ゲストのファイルをホストへ

```bash
python3 .claude/skills/run-os32/driver.py pull /tmp/k.txt ./k.txt
```

大きな出力を `cat` で引くと応答を取り逃す。`pull` はゲスト側で `/host` へ
写してからホストの実ファイルとして読む。

### 画面だけ

```bash
python3 .claude/skills/run-os32/driver.py shot /tmp/screen.png
```

`/api/screenshot` は 640x400 の 4bit BMP を返す。`.png` を指定すると PIL で変換する。

## ビルド

```bash
python3 .claude/skills/run-os32/driver.py build kernel
python3 .claude/skills/run-os32/driver.py build kernel programs
```

引数なしは `kernel programs`。中身は `make -C <ルート>` なので、**必ず `.env` の
ある本体で回る** (worktree には `.env` が無く、`-lc` が見つからず落ちる)。
worktree で直接 `make` するなら `make CROSS_DIR=~/opt/cross <target>`。

`make check` はホスト試験と検査の全体を回すもので、このスキルの範囲外。
どのビルドと検証が要るかは `docs/08_build.md` §8-4 を参照。

## 配備

```bash
python3 .claude/skills/run-os32/driver.py deploy
```

`make deploy` (ホスト → `C:\os32`) の後、ゲストで `hsync` を回し、最後に
`ls -l /bin/sh.bin` で実物を突き合わせる ([V1] 文言では判断しない)。再起動は要らない。

**このドライバは NHD とブート領域には触らない。** そこへの配備は NP21/W を
止めてから行う操作で ([D1])、承認の対象 ([D2])。`make deploy-kernel` /
`deploy-boot` は手で、`docs/08_build.md` §8-4 の手順に従って行う。

`hsync` は既定で `/sys` を外す (走っている常駐シェルと共有ライブラリ)。
入れ替えるときだけゲストで `hsync sys` と明示する。

## 人の道

Windows 側で `np21x64w.exe` を起動すると窓が開く。WSL からは見えないので、
このスキルの範囲では使わない。WSL から起動・停止するときは `tools/np21w_ctl.py`
(媒体のロックが解けるまで待ってから起動する) を使い、`taskkill` / `Start-Process` を
手で打たない。

## 配備と起動の注意

`tools/np21w_ctl.py stop` で停止を確認してから配備し、`start --ini <name> --wait-ready` で起動する。媒体のロックやダイアログを確認し、手動の taskkill と起動を連続させない。FD は `--fd` で渡す。配備の終了コードだけで合格にせず、作業イメージ・配備先・起動媒体と実行ファイルを照合する。`ver`、サイズ・ハッシュ、必要なら対応する `kernel.map` と実行メモリで反映を確認する。

## GUI 操作

## 前提と GUI への入口

最初に `emu_status` (`GET /api/status`) を読む。98 面は
`scrn_ymax == H && grph_disp == 1`、Cirrus は
`wab_relay == 1 && wab_height == H` なら GUI。高さ H は WAB 中継中なら
`wab_height`、それ以外は `scrn_ymax`。Cirrus で残った `scrn_ymax=400` を座標に使わない。
GUI でなければ `emu_mouse` と `emu_type` はエラーになる (`off` は後始末用に通る)。

入口は CUI で rshell を閉じ、**今回の ESC によって** `[Remote shell closed]` が
画面末尾に現れたことを確かめてから `os32gui` を打つ経路だけ。
`emu_key` の `seq=ESC` の前後に `emu_tvram` を採る。重なった rshell はそれぞれ閉じる。
閉じた後、共有 `gui.key(text="os32gui")` → RETURN → `emu_status` で GUI を照合する。
既存の台本では `tools.gui_gate.begin_gui(H)` がこの確認をまとめて行う。
**rshell 中に `text=` を打たない**。rshell は分割入力を別々のコマンドとして吸う。

## 入力と起動

`emu_mouse` は `action=move|click|press|release|drag|off`、画素座標 `x,y` を受ける。
範囲は `0..639, 0..H-1`。drag の終点は `x1,y1`。絶対座標 ax/ay は渡さない。
操作が終わったら、成功・失敗にかかわらず **`emu_mouse {"action":"off"}`**。

`emu_type {"text":"abcd", "escapes":true}` は **4 文字 / 0.35s** で送る。
`\e \n \r \t \b \xNN \\` は共有の逃がし記法。GUI のみで使う。CUI は `emu_cmd`。
`seq` に `+` が入る HTTP 要求は urlencode (`CTRL+ESC` → `CTRL%2BESC`) を使う。
MCP/共有実装が符号化するので、MCP 引数では `CTRL+ESC` のまま渡す。

`emu_gui_launch {"path":"/usr/bin/gui_demo.bin"}` は CTRL+ESC → DOWN (Run の行まで)
→ RETURN → パス → RETURN。絶対 ASCII パスだけ、空白・相対パス・255B 超・引数は不可。
引数の必要な fixture は CUI 起動か fixture の対話キーを使う。
新しい kernel owner (2..5) が空から非空に変わり、OP_WAIT の `PARKED` に入るまで待つ。
v1 の返り値は `owner` と `slot:null`。**owner と SHM slot の添字を結びつけない**。
launch が時間切れになった後は Start メニューが開いたままのことがある。画面で確かめてから再試行する (CTRL+ESC は開閉の切替)。
マウス経路は明示的な `fallback:true`。時間切れ後の自動再起動は二重起動になりうるので行わない。

## 観測・待ち・証拠

- `emu_gui_state`: `slots[]` は v1/v2 とも SHM 0..3、`kernel[]` は owner 1..5、
  `counters` は trim の値。v2 は窓と `wm_slots[]` (used slot の owner 表) を追加する。
  `front` は前景 owner、無ければ 0。窓 ID とは区別する。
- `emu_consink`: GUI 中の fixture 出力の第一の読み口。
  前回の `since` (head 位置) を渡すと差分。`lost:true` はその位置が保持されていない。
  リング 1 周やリセットを読み逃すと位置だけでは検出できない。短い区間で採取し、
  受入では drop カウンタや fixture の区間印も照合する。
  端末アプリが con_sink の読み手になるとリングが消費され、consink/wait_text に出ない。
- `emu_screen_text`: 第二の読み口。ゲスト ANK キャッシュの 8×16 二色字形と完全一致する
  ASCII だけを読む。日本語・未知字形は `?`、戻りは `lines[]` と `unknown_count`、`ambiguous`。
  同形字は `?` とし、`ambiguous` に行・列と候補 (例 `Il`) を返す。未知字形と別に数える。
  screen の待ちは候補のどれにも一致する。候補外の文字へ推測しない。
  同形セルを私用文字へ置き換え、regex を候補に合わせて広げて一度だけ照合する。
  先読み・後読み・境界条件・後方参照・atomic group・所有的繰返しは候補契約を保てないため理由付きで拒否する。
  `region:[x,y,width,height]` はセル原点を固定する。省略時は ASCII の一致を足場に行を探すため、
  ASCII が 2 字以上一致しない行は発見できない。未知字形だけの領域は region を指定する。
  `path` に PNG の証拠パスを渡す。4bit の 98 面と 24bpp の WAB を RGB に展開する。
- `emu_wait_mem {"symbol":"appslot_trim_epoch","op":"gt","value":0,"timeout":60}`。
  kernel の u32 が既定。gshell は `gshell:gshell_trim_delivered`。
- `emu_wait_text {"regex":"PASS","source":"consink","timeout":60}`。
  source は consink または screen、0.5s ポーリング。consink は呼出し時点以降の差分だけが既定。
  操作前に保存した位置から待つ場合は `since` を明示する (screen では不可)。

待ちの下限は **15s**、既定 60s。時間切れは例外でなく `{"ok":false,"last":...}`。
これを受入失敗として最後の値を残す。`ok:true` は条件を読んだというだけ。
**読めた = 合格ではない** ([V1][V4])。fixture の期待、区間差分、`unknown_count`・`ambiguous` と
保存 PNG を合わせて判定する。HostDrv に置いたバイナリと実行中の版も照合する。

受入では同じ観測区間で probe の前後値、`wm_state`、consink の位置をひとまとまりに採る。
開始: probe → state → consink (since を保存)、操作、終了: probe → state → consink(since)。
個別メモリ読み取りは同時刻の原子的スナップショットではないので、前後に時刻と fixture の
区間印を付ける。証拠は `~/os32-tmp/evidence/<日付>/` に JSON・PNG として残す。

## GUI からの出口

唯一の出口は **Start → CUI mode → Yes**。ESC はアプリへ渡り、GUI の終了にはならない。
既存台本は `tools.gui_gate.leave_gshell(Mouse(H))`。戻った後、rshell が既に生きていれば
重ねて起動しない。`ver` で確認する。
この出口は `/etc/system.cfg` の **`GUI=0` を永続化**する。
再び GUI へ入るには rshell を閉じて `os32gui`。次回の GUI 自動起動へ戻すときは、
CUI で `os32gui on` を実行して `GUI=1` を永続化する (既存の設定値を保つ)。


## 観測と調査

## 入口と調査範囲

未読なら `CLAUDE.md` と `docs/CONSTRAINTS.md` を読む。
`docs/DEVELOPMENT.md` で対象の仕様・ソースを特定し、`docs/POLICY_DEBUG.md` の
§1〜§3・§6と症状に関係する§4の項目を参照する。同一セッションで読んだ資料は再読不要。
規則・ハードウェア番地をこのスキルに複製せず、対応する正典とソースで確認する。
現行の依頼・承認範囲を引き継ぐ。承認済みの調査・修正の各手順を再承認事項にしない。
調査だけの依頼を、再配備・設定変更・メモリ書換えへ拡大しない。

## 観測してから変更する

1. 再現操作、期待結果、実際の症状、直前の変更を整理する。
   `git status --short` で既存変更を確認し、関係する差分だけ読む。
2. 接続済みの MCP ツールを探す。利用可能な名称・引数を確認してから呼ぶ。
   接続がない場合は `tools/np21w_mcp/server.py` と `np21w_client.py` を読み、
   既存 HTTP クライアントを使える。スキル導入だけでは MCP 接続は追加されない。
3. 最初に `emu_status` と `emu_tvram`、例外時は `emu_fault` / `emu_regs` を取得する。
   fault で凍結していれば resume/reset 前に状態を保存する。
   接続失敗はゲストのハングと区別し、URL・起動状態・サーバー有効化を確認する。
   起動状態は `python3 tools/np21w_ctl.py status` (プロセス・aidebug・ダイアログ・媒体のロック)。
   HTTP が 503 `{"dialog":true}` を返すのは NP21/W がモーダルのダイアログを出している印で、
   ゲストのハングではない。本文は `GET /api/dialog`、窓の見た目は `GET /api/appshot`
   (ゲスト画面の `/api/screenshot` とは別)。
   起動・停止し直すときも `tools/np21w_ctl.py start` / `stop` を使う — 手で続けて打つと
   媒体のロックで起動が途中で止まり、ゲストの障害と取り違える (`docs/POLICY_DEBUG.md` §4-60)。
   `.ini` を自動で書き換えない。ネットワーク制限で失敗した場合は実行環境の承認手順に従う。
4. 実行中バイナリの同一性を `docs/POLICY_DEBUG.md` §2 に沿って確認する。
   正常応答できる場合は `ver`、配備先ファイルのサイズ・ハッシュ等を使う。
   フォールト中はゲストのメモリと対応するビルド成果物を比較する。
   再ビルド前のゲストに新しい `kernel.map` を当てない。同一性が未確認なら
   シンボル名による推論を暫定と明記し、数値アドレス・実バイトも記録する。
5. 原因候補と、それぞれを区別できる最小の観測を提示して調べる。
   修正は観測が支持する範囲に限定し、棄却した仮説と根拠を残す。
   同じ失敗を繰り返す場合は、追加変更より先に配備・接続・再現条件を見直す。

## 症状別の道具

ツール定義の正典は `tools/np21w_mcp/server.py`。追加 API を推測して呼ばない。

| 症状 | 観測と注意 |
|---|---|
| 例外・ハング | `emu_fault`、`emu_regs`、`emu_disasm`、必要範囲の `emu_read_mem`。CS:EIP、CPUモード、ページ変換を照合する |
| IRQ が来ない | `emu_pic` と EFLAGS.IF、IDT/ISR、デバイスの許可状態を照合する。PC/AT の IRQ/EOI 定数を持ち込まない |
| 画面 | テキストは `emu_tvram`、描画は `emu_screenshot`、モードは `emu_gdc`。スクリーンショットを取得しただけで正常としない |
| 入力・対話 UI | 行単位は `emu_cmd`、生キー入力は `emu_key`。キー列の形式はツール定義に従う |
| 音源 | `emu_sound` の出力レベル・レジスタと音源処理を照合する。I/O 回数だけで発音成功とは判定しない |
| 経路・性能 | `emu_trace` を短く取得し、必要な再現区間だけ trace/profile を有効化する。計測オーバーヘッドと実機との差を区別する |

`emu_read_mem` は既定が物理、`emu_disasm` は既定が線形アドレス。
CPL3 のページングと V86 のセグメントを考慮し、`space` と16/32bitモードを明示する。
`emu_disasm` はホスト objdump を使う。フォールバック時の命令長警告を無視しない。
ホスト objdump が必要なら実在するパスを確認して `OS32_OBJDUMP` に指定する。
`OS32_KERNEL_MAP` は起動中のビルドに対応させる。設定変更後の読み込み方法は
`server.py` / `symbols.py` を確認する。

## 実行制御と通信

- `emu_step` は breakpoint による `trap_pause=1` が前提。`emu_pause` だけでは足りない。
  既存 breakpoint と実行状態を記録し、自分が追加したものだけを解除する。
  最初から停止していたゲストや、ユーザーが残したい fault を勝手に resume しない。
- カーネルが停止中なら `emu_cmd` は完了しない。コマンドを連投する前に状態を調べる。
- 現行 `emu_cmd` の timeout は30秒。長時間試験は既存 HTTP クライアントの
  `post('/api/cmd', command, timeout=...)` で十分な時間を指定する。
  下限は `docs/CONSTRAINTS.md` [V3] に従う。長時間待ちは進捗を伝えられる実行方式にする。
- timeout はコマンドのキャンセル・未実行を意味しない。画面と実行状態を読み、
  実行中／終了／シリアル不整合を切り分けてから再送・再起動を判断する。
- reset、state load、メモリ書換えは観測とは異なる。必要な対象と目的を特定し、
  依頼範囲と正典の規則に従う。単なる接続エラーを理由に実施しない。

## 修正後と成果

修正した場合は `docs/DEVELOPMENT.md` と `docs/POLICY_DEBUG.md` に従い、
変更に必要なビルド・配備・元の再現試験・関連回帰を行う。
ビルド成功、ゲストでの再現解消、実機確認を別の結果として扱う。
配備の前提や必要な承認が未充足なら、先にローカルの修正・ビルドを完了させ、
具体的な対象と未実施項目を示す。

報告は「再現条件／確認したビルド／原因を示す観測／修正と検証／残る未確認事項」。
関係するレジスタ・アドレス・コマンド結果だけを添え、大量ダンプは出力しない。
恒久的な教訓の更新先は `docs/INDEX.md` で選ぶ。`.env` の内容や
`docs/hw/` の資料本文をログ・コミットに取り込まない。

## Gotchas — ここで実際に踏んだもの

- **長く開いたままの rshell セッションは腐ることがある (2026-09-17 実測)。**
  症状は 2 つ同時に出た。`$?` が直前の結果に関わらず **3 に固着**し、
  `source` が**出力も副作用も出さなくなる**。どちらも rshell を開き直したら
  正常に戻った (`nosuchcmd` → 127、`echo` → 0、`source` の副作用も出る)。
  **腐ったセッションで見たことを「そういう仕様」と書かないこと** — 2026-09-17 に
  ここで実際にやってしまい、「`source` は rshell では動かない」という誤った注記を
  残した。開き直してから測り直すのが先。
  `userland/shell/rshell.c` の注記にある「断った印が立ちっぱなしになると
  **以降どのスクリプトも 1 行目で打ち切られる**」と症状が一致するので、
  動いている常駐シェルがその修正 (2026-09-16) より前の版である可能性が高い。
  **おかしいと思ったら rshell を開き直す**: 画面で `exit` してから `rshell`
  (`/api/key` の `text=` と `seq=RETURN`)。常駐シェルは起動時に読まれたきり
  入れ替わらない (hsync は既定で `/sys` を外す) ので、再起動するまで直らない。
- **終了の判定に CPU の居場所を使ってはいけない。** `sleep` のようにカーネルの
  中で待つプログラムは、**待機中のシェルと全く同じ番地に居る** (どちらも
  `sys_halt` (番地は起動中のビルドの `kernel.map` で引く)、CS も `0x0008`)。`cs == 0x0023` を抜けたことで
  終わったと判定すると、`sleep 30` が 10 秒で終わったことになる。
  だから `--wait` は**ゲスト自身に印を置かせる**。
- **`/api/cmd` は複数行を受け付ける。** これが `--wait` の土台。1 行目が本体、
  2 行目が `echo done $? > /host/<印>`。
- **入れ子の `sh` に複数行で入力を送れない。** 2 行目以降は rshell の待ち行列に
  残り、入れ子のシェルは鍵盤待ちのまま止まる。抜けるには `/api/key` で
  `text=exit` と `seq=RETURN` を送る。**入れ子のシェルを rshell から起動しない。**
- **シェルは `;` での連結を持たない。** `echo a ; echo b` は `a ; echo b` と
  そのまま出る。
- **`source` は動くが、出力が戻らない。** スクリプトは実行され副作用も出る
  (実測: `/tmp` にファイルが作られた) が、その `echo` は HTTP の応答に乗らない。
  画面には出ているので、確かめたければ `shot` で撮る。
- **複数行を 1 要求で送ると、2 行目以降の出力も戻らない。** 1 つ目の終端で
  応答が閉じるため。実行自体はされるので、`--wait` はこれを使っている。
- **`/mnt/c` のディレクトリ一覧は書かれた直後に古いままのことがある。**
  ゲストが `/host` に作った直後、`ls` は「無い」と言うのに `cat` は読めた。
  存在確認ではなく**開いてみる**こと (ドライバの `_read_done` がそうしている)。
- **`/host` は常にあるとは限らない。** カーネルは `hostdrvfs_detect()` が真の
  ときだけ繋ぐ。実機には HostDrv が無いので、`pull` はそこでは使えない。
- **HostDrv に置いたゴミはゲストへ同期される。** 検証用の一時ファイルを
  `C:\os32` に置いたら、次の `hsync` でゲストに入る。使い終えたら消すこと。
- **[V3] 待ち時間を縮めない。** 遠隔実行は 15 秒以上、長い処理は 60 秒以上。
  ドライバの `T_CMD` は 60 秒。

## Troubleshooting

| 症状 | 原因と対処 |
|---|---|
| `... に届かない` | NP21/W が動いていない、または確認した使用中 ini の `aidebug` が落ちている |
| `[aidebug: timeout waiting for EOT]` | 「実行制御と通信」 に従い、画面・状態・完了印で判定する |
| `cannot find -lc` / `-lgcc` | `.env` の無い worktree で `make` した。`CROSS_DIR=~/opt/cross` を渡す |
| `pull` が「現れなかった」 | ゲストの `/host` が繋がっていない (実機構成)、または `cp` が失敗した |
| 画面が真っ黒 | 起動していないか落ちている。`status` の `protected_mode` と `fault_generation` を見る |
| ゲストが固まった | CTRL+STOP で抜ける。深追いは上の観測と調査 |
