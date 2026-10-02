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

`NP21/W NG` なら Windows 側で `np21x64w.exe` が動いていないか
(`python3 tools/np21w_ctl.py status` で見る。起動は `start --ini np21x64w.ini --wait-ready`、
`docs/POLICY_DEBUG.md` §5)、`np21x64w.ini` の
`aidebug=true` / `aidbport=8025` が落ちている。**`np21w.ini` ではない** —
使われているのは `np21x64w.ini` のほう。

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

ゲストのシェルは 1 行を実行し終えるまで応答を返さないので、長い処理は HTTP が
**必ず時間切れになる。それは失敗ではない** — ゲストは走り続けている。
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
どのビルドとどの検証が要るかはスキル `os32-build-verify` が決める。

## 配備

```bash
python3 .claude/skills/run-os32/driver.py deploy
```

`make deploy` (ホスト → `C:\os32`) の後、ゲストで `hsync` を回し、最後に
`ls -l /bin/sh.bin` で実物を突き合わせる ([V1] 文言では判断しない)。再起動は要らない。

**このドライバは NHD とブート領域には触らない。** そこへの配備は NP21/W を
止めてから行う操作で ([D1])、承認の対象 ([D2])。`make deploy-kernel` /
`deploy-boot` は手で、スキル `os32-build-verify` の手順に従って行う。

`hsync` は既定で `/sys` を外す (走っている常駐シェルと共有ライブラリ)。
入れ替えるときだけゲストで `hsync sys` と明示する。

## 人の道

Windows 側で `np21x64w.exe` を起動すると窓が開く。WSL からは見えないので、
このスキルの範囲では使わない。WSL から起動・停止するときは `tools/np21w_ctl.py`
(媒体のロックが解けるまで待ってから起動する) を使い、`taskkill` / `Start-Process` を
手で打たない。

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
| `... に届かない` | NP21/W が動いていない、または `np21x64w.ini` の `aidebug` が落ちている |
| `[aidebug: timeout waiting for EOT]` | 失敗ではない。ゲストがまだ走っている。`cmd --wait` を使う |
| `cannot find -lc` / `-lgcc` | `.env` の無い worktree で `make` した。`CROSS_DIR=~/opt/cross` を渡す |
| `pull` が「現れなかった」 | ゲストの `/host` が繋がっていない (実機構成)、または `cp` が失敗した |
| 画面が真っ黒 | 起動していないか落ちている。`status` の `protected_mode` と `fault_generation` を見る |
| ゲストが固まった | CTRL+STOP で抜ける。深追いはスキル `os32-emu-debug` |

## 関連スキル

- `os32-build-verify` — 変更に応じてどのビルドとどの検証が要るかを選ぶ
- `os32-emu-debug` — ハング・例外・IRQ 不達の調査
- `os32-emu-config` — `*.ini` の限定変更 ([D2] の承認対象)
