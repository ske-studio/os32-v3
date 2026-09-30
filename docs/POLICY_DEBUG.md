# OS32 デバッグポリシー

OS32 のバグ調査・修正・検証における公式手順とプロセスを定める。
ベアメタル OS 特有の落とし穴や、AI との協調デバッグにおけるルールを含む。

開発全般のポリシーは [POLICY_DEV.md](POLICY_DEV.md) を参照。
既知のハードウェア制約と技術ノートは [10_notes.md](10_notes.md) を参照。

---

## §1. デバッグの基本原則

### 壊す前に理解する

コードを変更する前に、必ず以下を行う:

1. **現象を正確に記述する**: 何が起きているのか、何が期待される動作なのか
2. **仮説を立てる**: 最低2つ以上の原因候補を優先度順に列挙
3. **検証計画を作る**: どの仮説をどの順序で検証するか

> 「なんとなくここが怪しい」でコードを修正してはならない。仮説が明確でない修正は、別のバグを生む温床になる。

### 仮説駆動デバッグの3ステップ

```
1. 仮説を立てる (Hypothesize)
   ↓
2. 最小限の変更で検証する (Verify)
   ↓
3. 結果を記録し、次の仮説に進む (Record & Iterate)
```

---

## §2. バイナリ反映の確認 (最優先)

**デバッグの最初のステップは「バイナリが正しくデプロイされているか」の確認である。**

NP21/W 上でコード変更が反映されていないように見える場合、ロジックの調査に進む前に以下を**必ず**確認する:

### チェックリスト

| # | 確認項目 | 方法 |
|---|---------|------|
| 1 | ビルドタイムスタンプが更新されているか | `ver` コマンドで Build タイムスタンプを確認 |
| 2 | カーネルのロードパスとデプロイパスが一致しているか | `config.h` の `SYS_SHELL_BIN` 等 と `deploy.yaml` の `guest:` パスを照合 |
| 3 | NHD 上のファイルが最新か | ファイルのサイズ・MD5 がビルド成果物と一致しているか確認 |
| 4 | `make clean` が必要ではないか | KernelAPI 構造体変更後は古い `.o` ファイルが ABI 不整合を起こす |

> ⚠️ **教訓**: デバッグ出力が反映されない = 「コードのバグ」ではなく「バイナリが古い」可能性を**最初に**排除すること。これはプロジェクト開始以来、何度も繰り返された最も時間を浪費する問題である。

### カーネルの到達確認は kselftest で行う

`kernel/kselftest.c` のブート時セルフテストは毎回走り、失敗項目を赤で表示する。
画面を見ない場合は `kselftest_pass` / `kselftest_fail` を `emu_read_mem` (または
`/api/mem`) で読む。**番地は新しい `build/out/kernel.map` から引くこと** —
古い map の番地で読んだ値や、初期化前のゼロ値を合格と判定しない。

- `kstring` / `kmalloc` / `kprintf` のプリミティブを触ったら、kselftest に項目を足す。
- `userland/tests/klibc_test` は newlib をリンクするので、カーネル側の検証にはならない。

### 実機では起動ログを `cat /var/log/boot.log` で読む

実機では起動画面の `[selftest] N/N passed`・`[boot] Image CRC`・`[fdc]` などがすぐ流れて読めず、
rshell が立つ前の kprintf はシリアルにも出ない。カーネルは最初の kprintf から常駐シェルを exec する
直前までの出力 (文字だけ、16KB、あふれたら**後ろを**捨てる) を溜め、ルートが ext2 / FAT なら
`/var/log/boot.log` に上書きで書く。前回の分は `/var/log/boot.log.1` (FD 起動 = FAT では 8.3 の
`/var/log/bootlog.1`)。起動後にシリアルで **`cmd "cat /var/log/boot.log"`** を読めば、流れた行を全部見られる。

- 1 行目がヘッダ `# OS32 boot log  Build … Commit … Image CRC … uptime-ticks N`、最終行が
  `# end  kept N bytes  dropped M bytes`。`dropped` が 0 でなければ後ろが切れている。
- 書き出しに失敗しても起動は続き、画面に `[bootlog] <段> failed rc=…` が 1 行だけ出る
  (その行とシェル起動後の出力はファイルに入らない)。ルートが HostDrv / iso9660 なら書かない。
- 今回分はまず `/var/log/boot.new` に書き (残っていたものは先に消す)、書けたと確かめてから
  `boot.log` → `.1` → `boot.new` → `boot.log` と回す。`boot.log` が無ければ `.1` には触らない
  (唯一の旧世代を残す)。途中で落ちてもそこで止め、**既存の `boot.log` は上書きしない** (今回分は
  `boot.new` に残る)。`boot.new` が残っていたら、その起動の保存が途中で止まった印 (次の起動で消される)。
  書けなかった後の後始末も落ちうるので、残存だけでは中身が完全とは限らない。
- **いつの起動のログか**はヘッダの Commit / Image CRC を `ver` と突き合わせて決める。
  実装: `kernel/bootlog.c`、記録: `tools/tests/bootlog_tdd.md`。

---

## §3. 仮説の提示と検証プロセス

### プロセスフロー

```
  ┌──────────────────────────────┐
  │ 1. 現象の整理                  │
  │    - 何が起きているか          │
  │    - 何が期待される動作か      │
  └──────────────┬───────────────┘
                 ▼
  ┌──────────────────────────────┐
  │ 2. 仮説リスト作成              │
  │    - 優先度順に3つ以上列挙     │
  │    - 各仮説に検証方法を付記    │
  └──────────────┬───────────────┘
                 ▼
  ┌──────────────────────────────┐
  │ 3. 仮説を1つ選び検証          │
  │    - 最小限のコード変更で       │
  │    - ビルド → デプロイ → テスト │
  └──────────────┬───────────────┘
                 ▼
           ┌──────────┐
           │ 解決した？ │
           └─────┬────┘
          Yes ┌──┴──┐ No
              ▼     ▼
           完了   次の仮説へ
                  (結果を記録)
```

### AI との協調時のルール

1. **コードを変更する前に**、仮説リストをユーザーに提示して承認を得ること
2. 仮説を1つ検証して棄却されたら、次の仮説に移る前に**結果をユーザーに報告**し、方針を確認する
3. **3回リトライルール**: デバッグコードの追加 → ビルド → デプロイ → テスト のサイクルを3回以上繰り返しても解決しない場合は、必ず一度立ち止まってユーザーに状況を報告する

---

## §4. よくある落とし穴と教訓集

過去に OS32 開発で実際に遭遇し、大きな時間を費やした問題のカタログ。
新しい問題に直面した際は、まずこのリストに該当するものがないか確認すること。

### 4-1. ABI 不整合 (KernelAPI 構造体変更後の clean 忘れ)

- **現象**: `malloc` が常に ENOMEM で失敗する、KAPI 関数が間違った引数を受け取る
- **原因**: `os32_kapi_shared.h` を変更したが `make clean` をせずに差分ビルドした結果、古い `syscalls.o` が構造体の旧レイアウトでリンクされた
- **対策**: KernelAPI 関連ヘッダを変更したら**必ず `make clean` → `make all`**

### 4-2. ブートローダーの `org` 設定

- **現象**: PM 遷移直後にハングする、データ参照が壊れる
- **原因**: 第2ステージローダーが `org 0` でアセンブルされた (正しくは `org 8000h`)
- **対策**: DS=0 で絶対アドレス参照を行うローダーは必ず `org 8000h` で記述する

### 4-3. GCC 最適化と配列境界外アクセス (UB)

- **現象**: FAT12 のファイル名読み取りで後半 (ext[3]) が空になる
- **原因**: `name[8]` と `ext[3]` を連続配列として `name[11]` でアクセスした結果、GCC `-O2` が配列境界外アクセスを UB として最適化除去
- **対策**: 各フィールドは必ずその名称で個別にアクセスする

### 4-4. インラインアセンブラでのレジスタ保存

- **現象**: `exec_run()` 後にカーネルスタックが壊れる
- **原因**: 旧 ESP を `eax` に保持したまま関数呼び出しを行い、呼び出し先で `eax` が破壊された
- **対策**: レジスタではなく**`static` 変数 (メモリ)** に保存・復帰する

### 4-5. BSS ゼロクリアの必要性

- **現象**: 起動直後に `exec_nest_level` 等のグローバル変数がゴミ値で動作不良
- **原因**: ベアメタル環境では CRT0 相当の BSS ゼロクリアを自前で行う必要がある
- **対策**: `kentry.asm` 内で `rep stosd` により BSS セクション全体をゼロクリア

### 4-6. ext2 パーティション重複

- **現象**: ファイルシステムのマウントが失敗する、スーパーブロックが壊れる
- **原因**: カーネルバイナリの成長により、NHD イメージ上でカーネル後部が ext2 スーパーブロックに重なった
- **対策**: ext2 の開始をシリンダ12 (LBA 1632) に配置 (詳細は [NHD_FORMAT.md](NHD_FORMAT.md))。カーネル直接配置経路の成長猶予は 128KB (LBA 262 の sqlite.bin 領域まで)

### 4-7. シリアル API タイムアウトによる「ハング」誤診

- **現象**: `exec` 実行後にシェルが復帰しない。`exec_exit` にデバッグ出力を追加しても表示されない
- **原因**: NP21/W デバッグ HTTP API (`curl -X POST http://127.0.0.1:8025/api/cmd`) の `curl -m` タイムアウトが短すぎた。エミュレータの処理速度（~16MHz）でプログラム実行に時間がかかり、応答が返る前にタイムアウト。タイムアウト後はシリアルプロトコルが非同期状態になり、後続コマンドもすべて失敗する
- **対策**:
  - `curl -m` は十分長く設定する（最低15秒、長時間プログラムは60秒以上）
  - タイムアウト発生後は NP21/W を再起動してシリアルを再同期する
  - 「シェルが復帰しない」場合は、まず画面を直接確認する (`/api/tvram` か `/api/screenshot`)

### 4-8. NP21/W 実行中の NHD デプロイが反映されない (ファイルロック)

- **現象**: `make deploy-kernel` / `deploy-nhd` が成功したように見えるのに、修正が実機に反映されない。何度デプロイ+再起動しても古いバイナリが動き続ける
- **原因**: NP21/W が os32.nhd を開いたまま (実行中) の状態では、`nhd_deploy.py deploy` の NHD コピーが失敗またはサイレントに無効化される。パイプで出力を `tail` すると失敗メッセージも exit code も見えなくなる
- **対策**:
  1. NHD デプロイ前に必ず NP21/W を停止する: `python3 tools/np21w_ctl.py stop` (プロセスが消えるまで待つ)
  2. `make deploy-kernel` → `python3 tools/np21w_ctl.py start --ini np21x64w.ini --wait-ready` の順で実行 (§5)
  3. デプロイ後は §2 のチェックリスト通り `ver` の Build タイムスタンプで反映を確認する
- **教訓** (2026-08-05): この問題により「修正が効かない」調査サイクルを3回空転した。§2「バイナリ反映の確認」を最初に行っていれば1回で気づけた

### 4-9. エミュレータ共有メモリとコンパイラ最適化 (volatile欠落)

- **現象**: NP21/W との通信 (例: HostDrvのhypercall) で、エミュレータがステータスをメモリに書き込んでいるにも関わらず、OS32側では値が更新されていない (番兵値のまま) ように見える。
- **原因**: 通信用メモリ構造体に `volatile` 修飾子が付与されておらず、かつ I/Oアクセス関数 (`outp` 等) のインラインアセンブラに `"memory"` clobber（コンパイラバリア）が無かった。このため、GCCの最適化 (キャッシュ) によりメモリの再読み込みが省略され、レジスタに残った古い値が評価されてしまった。
- **対策**: エミュレータが外部から直接読み書きする通信バッファや状態変数は、必ず `volatile` 宣言を行うこと。また、初期化時にエミュレータへの明示的なリセットシーケンスを送信して状態を同期させること。

### 4-10. ビットマップフォントの焼き方 (`tools/gen_font16.py`)

- **現象** (〜2026-08-18): 'W' が 'A'、'M' が 'V' に見える。行がガタつく
- **原因**: IPAex 系は欧文がプロポーショナルで、'W' (14px) や 'M' (13px) を 8px の ANK セルへ中央寄せすると左右が切られる。字ごとの ink box を縦位置の基準にすると 'g' 'j' 'y' や '.' ',' が上下にばらつく
- **対策**: セルからはみ出す字だけ横に畳む。**縦は必ずベースライン基準** (`anchor='ls'`)、縦は縮めない。16x16 の漢字は明朝だと細い横画が飛ぶので本文はゴシック (`assets/fonts/ipaexg16.kcgfont`)。半角 (size15/baseline12) と全角 (size16/baseline13) のベースライン 1px ずれは、16px セルに「漢字の高さ 13 + 欧文 descender 4」が収まらないための妥協 (共通ベースラインにすると漢字が size13 に落ちて見劣りする。比較検証済み)
- **検証**: `--preview out.png` で等倍と 3 倍を並べる。**等倍で読めるかだけが判断基準**。カーネルは起動時に `/sys/font/default.kcgfont` を読む (`kernel.c` の `kcg_load_font`)

### 4-11. 外部プログラムで漢字が全部 □ になる (Unicode→JIS 表の未有効化)

- **現象**: 仮名は出るのに漢字だけ □
- **原因**: カーネルは `/sys/unicode.bin` を `MEM_UNICODE_TABLE_BASE` (0x4A000) へ読み `utf8_set_jis_table_ready(1)` を呼ぶが、このフラグは `lib/utf8.c` の static 変数で、外部プログラムは自分の `utf8.o` を持つため別実体。未有効化だと `unicode_to_jis()` が 0 を返し `kcg_draw_utf8()` が JIS 0x2222 にフォールバックする。仮名はハードコードの範囲変換なので出てしまい、原因が分かりにくい
- **対策**: 表は共有物理メモリにあるので、プログラム側で既知の対応 (U+4E9C→0x3021 など) を数点検証してから `utf8_set_jis_table_ready(1)` を立てる。無条件に立てるとロード失敗時の残骸を表として読む (実例: os32-game `app/main.c` の `enable_kanji_table()`)

### 4-12. deploy.yaml に無いバイナリが NHD 上で stale 化し rshell が沈黙する

- **現象** (2026-08-13, sndctl): KAPI レイアウト変更後、古いバイナリの KAPI 呼び出しが別関数へ飛び、exit 後の `jmp $` で永久スピン。rshell ごと沈黙
- **対策**: 起動対象は PKG 配布でも必ず自層の deploy.yaml に載せる ([V2])。2026-09-04 から `make deploy` / `deploy-nhd` / `deploy-kernel` は同期後に `tools/prune_stale.py` でマニフェストに無いシステム側 *.bin を消す (`NO_PRUNE=1` で一覧のみ、`make prune-stale` で手動確認)

### 4-13. SQLite MEMSYS5 プールの枯渇 (`db_query` が -2)

- **現象** (2026-08-18): game で econ 常時接続 + battle/items/rpg/events の順次ロードの最後の `db_query` が `out of memory`
- **原因**: MEMSYS5 は固定プール (`lib/sqlite3/os32_sqlite_vfs.c`) で、カーネル側 FEP 辞書を含む全接続が共有する。当時 200KB
- **対策**: 384KB に拡大 (SQLite 拡張域 0x200000〜0x2FFFFF 内、残り約 280KB)。エンジンライブラリは `*_init()` の末尾で接続を閉じる。**枯渇の診断は `db_last_error()` を必ず出す** — 戻り値だけでは「テーブルがない」と区別できない。実機で任意 DB を調べるには `dbq` (`userland/tests/dbq.c`)。**2026-09-17 まで `db_last_error()` はカーネル番地を返しており、CPL=3 のアプリが読むと #PF で死んだ** (票 [`archive/kernel_v21/TASK_DB_ERRSTR.md`](archive/kernel_v21/TASK_DB_ERRSTR.md) で修正)。いまは共有メモリを指すので安全。直前の `db_exec` のエラーだけでよければ `db_errmsg()` (KAPI 呼び出し無しで共有メモリを直接読む) のほうが速い

### 4-14. `mui_pump_input()` がキー待ち行列を食う

- **現象** (〜2026-08-17): os32-game のキーボードショートカットが全部効かない。自動プレイのデバッグタイマはこの回避策だった
- **原因**: `userland/lib/ui/libos32ui_core.c` の `mui_pump_input()` が内部で `kbd_trygetchar()` を呼ぶので、同じフレームでアプリが `kbd_trygetchar()` を呼んでも何も来ない
- **対策**: アプリが 1 回だけ読んで `mui_pump_input_ch(ctx, ch)` に渡す

### 4-15. `exec_exit()` の全回収と `exec_heap_init_at()` による親ヒープ破壊

- **現象**: (a) 2026-08-07 「FEP 候補ゼロ」— `ime on` を実行した ime.bin の終了で常駐辞書の SQLite FD が閉じられた。(b) `ext_cmd1 | ext_cmd2` がキーボード入力で固まる — 1 段目の終了でシェルのパイプバッファが kfree された。(c) `[exec_heap] bad magic feeefeee (double free?)`
- **原因**: 2026-09-03 まで `exec_exit()` は FD ≥ 3 とパイプを所有者に関係なく全部回収していた。(c) は子から戻るとき `exec_heap_init_at()` で親ヒープを再初期化し、先頭ブロックヘッダを書き直したため
- **対策**: 所有者タグ (`res_owner_get()` = ネスト段) で終了段の分だけ回収、カーネル常駐 FD は `vfs_fd_set_protect(fd, 1)`。親へ戻るときは `exec_heap_restore_state()`。仕様は [10_notes §10-9](10_notes.md)

### 4-16. シェルの 2 つのヒープが重なる

- **現象**: `ls > file` の化け、`pipe: out of memory`、double-free 警告
- **原因**: newlib の sbrk (malloc / stdio) と KAPI `mem_alloc` の exec_heap が両方 BSS 終端から始まり互いを上書き
- **対策** (2026-09-03): exec_heap を `MEM_SHELL_HEAP_BASE` (0x380000, 512KB) へ分離。`kernel/paging.c` は 0x380000〜0x3FFFFF を present に保つ (以前は NP ギャップ)。番地は [02_memory.md](02_memory.md)

### 4-17. NHD 作業イメージが `/tmp` にあり、消えても deploy が exit 0 で返っていた

- **現象** (〜2026-09-04): WSL 再起動で `/tmp/os32.nhd` が消え、その状態の `make deploy-nhd` が NHD を書かずに成功扱い
- **対策**: 作業イメージを `build/nhd/os32.nhd` へ移動。無ければ Windows 側から自動 pull (NP21/W 停止中のみ)、失敗は exit 1。`os32-cycle deploy` はブート後にゲストの `/boot/vmkernel.lz4` サイズが手元の成果物と一致しなければ FAIL。**「配備完了」の文言を信じない** — カーネル変更の検証は `kselftest_pass` を新しい kernel.map の番地で読むか、この一致チェックで行う (§2)

### 4-18. テキスト GDC のカーソルが「左下でちかちか」

- **現象** (〜2026-09-04): V86 で DOS を動かした後、DOS 最後のカーソル位置で点滅が残る。OS32 自身はハードウェアカーソルを一度も表示していなかった
- **原因**: 旧 `GDC_CMD_CSON` (0x0B) は uPD7220 に無いコマンド。カーソル表示は CSRFORM (0x4B) の DC ビットでしか制御できない
- **対策**: `console_hw_cursor_enable()` (起動時 / V86 終了時 / `console_set_cursor`) で表示し、`console_hw_cursor_sync()` が文字列出力の末尾で論理位置へ追従させる。GDC の実状態は MCP `emu_gdc` の `m_csrform` (`8f0e7b` = 表示・2 ライン下線)

### 4-19. CPL=3 からの KAPI 呼び出し中は割り込みが止まっていた (`int 0x80` 割込みゲート)

- **現象** (〜2026-09-06): CPL=3 の `gdi_test` が最後の `kbd_getchar()` で固まり、`/api/cmd` も `/api/key` も効かない。`less` (`kbd_trygetchar` + `sys_halt`) も同じ。`/api/regs` で EFLAGS=0x004/0x084 (IF=0)、EIP は `hlt`、`tick_count` が止まり、`/api/pic` の IRR に IRQ0/1 が滞留
- **原因**: `int 0x80` は割込みゲート (0xEE) なので入口で IF=0 になるが、`int80_stub` → `ring3_syscall_dispatch` → `kapi_invoke` → `wrap_*` のどこにも `sti` が無かった。KAPI 本体は CPL=0 の `call` 経路 (IF=1) と同じ前提で書かれ、`hlt` で IRQ を待つ関数 (`kbd_getchar` / `kbd_getkey` / `sys_halt`、GUI の `gui_call(OP_WAIT)`) は IF=0 だと二度と起きない。ポーリング型のアプリは syscall の合間 (ユーザコード、IF=1) に IRQ が届くので気づきにくく、rshell 経由 (シリアルはポーリング) の検証ではさらに隠れる。M3 の「`less` 常駐中の CPL3 同時試験」はこの理由で実質検証になっていなかった疑いがある
- **対策**: `kernel/ring3_entry.asm` の `int80_stub` でカーネルセグメント復元直後に `sti`、出口 (`popad` の前) で `cli`。出口を IF=0 で通すのは、USER_DS を DS に載せてから `iretd` までに IRQ が入ると IRQ スタブが DS=KERNEL_DS にしたまま (フレームの CS が CPL=0 なので `IRETD_USER` が戻さない) CPL=3 へ帰り、最初のメモリ参照で #GP するため (gdi_test の `gfx_pixel` で実測)。`exec_run` の setjmp 復帰点にある `_enable()` は従来どおり (sys_exit の longjmp 経路)
- **検証**: `gdi_test` が rshell から約 3 秒で自動復帰してプロンプトに戻る、キーボードから `less` → `q` で終了する、`ver` が返る
- **関連 (K2、同日)**: CTRL+STOP で CPL=3 アプリを畳む経路はそれまで存在せず (V86 脱出のみ)、K2 が `ring3_abort_request` / `ring3_abort_check` を新設した。CUI からでも暴走した CPL=3 アプリを CTRL+STOP で kill できる (`ring3_abort_count` / `fault_kill_count` が +1)。KAPI 実行中に押された場合は次の syscall 入口で畳む

### 4-20. gshell 配下のアプリだけ漢字が描けない (PACKED8 判定の抜け)

- **現象** (2026-09-06、G5 後半): PEGC / Cirrus で gui_demo のテキストボックスに FEP で「日本語」を入れると、
  カーソルは 3 文字ぶん進むのに字が出ない (白い矩形だけ)。ANK は出る。gshell 自身の候補窓には漢字が出る。
- **原因**: gshell 配下のアプリは `gfx_init` を呼べない (デスクトップを消す) ので `libos32gfx_init` ではなく
  `attach_gfx` で framebuffer だけ取り直していたが、そこで `gfx_packed` (PACKED8 判定) を立てておらず、
  共有ライブラリ内の `gfx_draw_font` (漢字経路) が 4 プレーン経路に落ちていた。ANK は libos32gui が自前で
  画素を置くので気付かなかった。
- **対策**: 判定を C 側の `libos32gfx_attach()` に集約し、`libos32gfx_init` と attach の両方から呼ぶ。
  SDK ライブラリを変えたので `make external` も必要 (§4-12 と同じ罠)。
- **検証**: PEGC / Cirrus で「日本語abc」が textbox とラベルに出る。9801 (`gfxmode pc98`) は元から正常。

### 4-21. Cirrus: 窓を shutdown で畳むと単独アプリが #PF、再 init でリレーが倒れたまま

- **現象** (2026-09-06、レビュー #5 ② を入れた直後): (a) `gdi_test` (単独 CPL=3 アプリ) がクライアント面
  0104B000h で #PF。gshell 配下では通る。(b) gshell でアプリを CTRL+STOP で畳むとデスクトップが 98 側の
  黒画面に隠れる (`/api/status` の `wab_relay=0`)。
- **原因**: (a) exec がアプリ PD にクライアント面を USER で写すのは **アプリが `gfx_init` を呼ぶ前**。
  直前の shutdown が窓を畳んで `bb_base=NULL` にしていたうえ、再 init の `paging_map_phys` が共有 PT の
  PTE を supervisor で上書きして USER を消していた。(b) gshell はアプリ終了後に `gfx_init` を呼び直すが、
  グルーの init (FF82h) がリレーを 98 側へ倒すのに `s_relay_on=1` のままで `enter()` が書かなかった。
- **対策**: 窓は最初の init で一度だけ張り (supervisor + PCD)、shutdown では畳まず `bb_base` も保持。init で
  `s_relay_on=0` に戻して `enter()` に必ず書かせる。
- **検証**: `gdi_test` present_bytes=0 hw_ops=9、`ring3_guard bb` 生存、`ring3_guard cirrus` は表示面で kill、
  gui_busy → CTRL+STOP でも `wab_relay=1` 維持。

### 4-22. gshell の X4 がボタンエッジを先に見ると WM の状態機械に届かない

- **現象** (2026-09-06、`/api/mouse` で初めて実測): ドラッグの離しが失われて XOR 枠が残ったまま次の押下まで
  動き続ける、× を押すと閉じずに前面化だけ起きる、背面窓への押下がクライアントへ届く。
- **原因**: アプリの syscall 境界ポンプ (X4) が `prev_buttons` を進めてアプリへ転送していたため、X3 (WM の
  状態機械) には次のエッジが立たない。レビュー #4 ④でモーダルだけ直した問題の一般形。
- **対策**: `wm_owns_edge()` — ドラッグ中 / 窓の外 / 背面窓 / 閉じる / タイトルバーのエッジは X4 で据え置き
  (prev_buttons を進めない) → 次の X3 が拾う。前面窓のクライアント・枠だけを X4 で配る。判定順は
  `wm_button_down` と同じにする (ずれると二重配送か取りこぼし)。
- **検証**: ドラッグ → drop → 重なり再描画 → 背面クリックで前面化 → × で閉じる → チェックボックス / OK →
  デスクトップ無反応 (スクリーンショット、TASKS §7)。

### 4-23. NP21/W 実機検証の罠 (GUI 版)

- `/api/key` の `seq=SHIFT+SPACE` は **`--data-urlencode`** で送る。`-d` だと `+` が空白になり FEP が入らず、
  「Cirrus で FEP が効かない」と誤診しかけた。
- `make deploy*` は `/etc/system.cfg` を巻き戻す。`gfxmode` / `os32gui` の設定切替は `/api/reset` で検証する。
- `hotdeploy` は CUI で rshell が生きているときだけ効く (`hotdeploy_poll` は `kbd_trygetchar` から)。gshell 中や
  `ime on` 中 (`/api/key` の文字が FEP に吸われる) は先に CUI へ戻す。**GUI から CUI へ戻る経路は
  Start → "CUI mode" → 確認ダイアログ Yes だけ** (G5 で ESC の即時切替は撤去。契約 S6 / 票 W3 §4.1) —
  `tools/gui_gate.py` の `leave_gshell()` がその手順 (Start (30,H-12) → 行 3 (82, `start_row(H,3)` — 項目数から導く。§4-31) →
  Yes (410,H/2+11) → 約 6 秒待ち → `abs=off` → `rshell`)。その後 SHIFT+SPACE → `ime off` → `rshell`。
  この経路は `/etc/system.cfg` に `GUI=0` を永続化するので、GUI 自動起動へ戻すときは `os32gui`
  (その場で GUI へ入る) か cfg の `GUI=1` 書き戻しを使う。
- OS32 は NP21/W ではシームレスマウス (np2sysp `getmpos`) なので、`/api/mouse` は **`ax`/`ay`** (絶対座標
  0..65535、`ax = px*65535/639`、`ay = py*65535/(H-1)`)。`dx/dy` はバスマウス計数で効かない。検証後は `abs=off`。
- `gfx_stats` のカウンタは起動からの累計。`gdi_test` の値は起動直後の 1 回目だけが基準。
- `/api/key` の `text=` は **8 文字ずつ** 送る (`tools/gui_gate.py` の `key()`)。raw リングは 32 エントリ
  (make+break で 1 文字 2 本) しか無く、長いパスを一度に注入すると後ろが落ちる (Run... のパスが
  `/usr/bin/gui_dem` で切れ「Launch failed」と誤診しかけた)。連打も 0.3s 間隔で。
- 自己完結テストの終了は `int 0x80` で **eax=KAPI_SLOT_SYS_EXIT (84)**。eax=0 はスロット 0 = `gfx_init` で
  終了しない (ring3_guard がそれで GFX モードに入ったまま無限ループしていた)。
- NP21/W の停止・起動は `tools/np21w_ctl.py stop` / `start` (§5) で PM が行える。ini の編集
  ([D2]) と np21w-src の `make deploy` は停止中に。
- **エミュレータは同時に 1 人**。コーダーに「CUI で再現してよい」と hotdeploy を許可したら、PM の GUI 検証と
  混ざってゲストのファイルが差し替わり、壊れたバイナリを判定してしまった (2026-09-06)。

### 4-24. ext2: 解放系がスクラッチバッファを共有していてファイルが相互リンクする

- **現象** (2026-09-06、v1.2 G2): 15KB の `/usr/bin/v12_api_test.bin` を 21KB で上書き (hotdeploy) すると、
  新しいファイルの先頭ブロックのオフセット 12〜19 に別ファイルの間接エントリが現れ、OS32X ヘッダの
  `flags` / `entry_offset` が化けて `exec_run` が `load + 0xC639` へ飛んで #PF。`gui_demo.bin` も
  「invalid OS32X binary」になった。ホスト側のビルドは正常。
- **原因**: `ext2_free_all_blocks()` が単一間接テーブルを `ext2_g_aux` に読んで解放ループを回すが、
  `ext2_free_block()` はブロックビットマップを同じ `ext2_g_aux` に読み直す。1 本目を解放した瞬間に表が
  ビットマップに化け、以降はビットマップのバイト列をブロック番号と誤読して**他のファイルのブロックを
  片端から解放**する。解放されたブロックは次の割り当てで再配布され、ファイルが混ざる。二重間接も同型。
  12KB (直接ブロック 12 本) を超えるファイルの上書き / 削除のたびに起きていた。
- **対策**: 表を `ext2_g_blk` (単一間接) と `ext2_g_dat` (二重間接の内側) に置く (`fs/ext2_inode.c`)。
  **規則**: `ext2_g_aux` はビットマップ用。解放・割り当てを呼ぶ経路で表やデータを `g_aux` に置かない
  (§4-26 の `sys_ls` コールバックの注意と同じ根)。
- **診断の手順**: ゲストの `hexdump` でファイル先頭をホストのビルドと比べる → 差分の位置がヘッダなら
  ローダの `entry` を疑う → `/api/regs` / breakpoint で `_start` に届かないことを確認。NHD の健全性は
  `dd skip=1633` で ext2 部分を切り出して `e2fsck -fn` (読み取り専用)。**ゲストが壊した内容は Windows 側の
  コピーにしか無く、次の NHD 配備で WSL 側 (ホストの Python が書く) から丸ごと上書きされる** ので、
  配備し直せば消える (逆に、ゲストが書いた物は配備で消える — 従来どおり)。
- **検証**: 修正カーネルで 21KB のファイルを繰り返し上書き → `hexdump` がホストと一致、`v12_api_test` が
  起動する (v1.2 G2)。

---

### 4-25. GUI の「反応が遅い」は起床経路を疑う前に present と EIP を測る

- **現象** (2026-09-07、v1.2 G3): filer の右ペインで DOWN / ROLLDOWN を押すと画面が 1.3〜2 秒後に変わる。
  マウスを 2px 動かすと直後に変わるように見えた (実際は時間の一致)。gui_demo のリストは即時。
- **迷走**: 「Paint が次の周にしか出ない」「OP_WAIT が起きない」と決めつけて、アプリの二重 POLL、
  OP_INVALIDATE で Paint 即時配送、可視領域の回転、と 3 回直しても効かなかった (いずれも撤回)。
- **決め手**: (1) カーネルの `gfx_counters` (`kernel.map` の番地を `/api/mem` で読む) — 打鍵後 1.3 秒間
  `commits` / `present_bytes` が増えない = **present が 1 回も呼ばれていない**。(2) gshell の一時カウンタで
  Key 到着・OP_INVALIDATE・起床・2 回目の POLL がすべて同じ tick、present だけ 136 tick 後。
  (3) `/api/status` の `eip` を 30ms ごとに 1.6 秒サンプル → 11/11 が shlib の
  `libos32gui::draw::Painter::fill_solid` の内側。**真因**: 1 ピクセルごとに clip 4 回 + 枠 4 回の比較をする
  per-pixel ループで、右ペイン ~108k px の塗りに 1.3 秒。gui_demo は塗り面積が小さいだけだった。
- **対策**: `fill_solid` は矩形を clip と物理枠で 1 回だけ交差させ、行ごとに `write_bytes` (memset)。
  glyph / icon も行単位化。filer は選択変更で「前後の 2 行 + パス行」だけ再描画。打鍵→present が
  0.27〜0.45 秒 (HTTP 往復込み) に。PEGC / 9801 / Cirrus で同じ。
- **副産物として直した実欠陥**: gshell が Pointer を `moved` に関係なく毎周 ring に積み、
  `OP_WAIT` が眠らず 33 回/秒の空 commit をしていた (`gfx_counters` の idle 増分で発覚)。
  起床判定と配送判定が別実装で乖離し得たので `damage::deliverable_cand` に統合。
- **手順 (30 秒で決まる)**: ① `gfx_counters` を打鍵前後で読み present の有無を見る → 無ければ
  「アプリが描いていない or 起きていない」。② `/api/status` の `eip` を連続サンプルし、0x5xxxxx (アプリ) /
  0x4xxxxx (shlib) / 0x3xxxxx (gshell) / 0x1xxxxx (カーネル idle) のどこに居るかを数える。
  ③ shlib / アプリなら `i386-elf-nm -n` でシンボルに落とす。起床経路のカウンタはそのあと。

### 4-26. `sys_ls` のコールバックから FS を触ると一覧が崩れる

- **原因**: `ext2_list_dir` のコールバックの中で書き込み系の FS 操作をすると、
  共有スクラッチ `ext2_g_aux` が上書きされ、一覧の途中から別の内容を読む (§4-24 と同じ根)。
- **対策**: `ext2_list_dir` は各ブロックを先に私有バッファへコピーしてからコールバックを呼ぶ。
  呼ぶ側も、コールバック内では名前を自分のバッファに集めるだけにし、FS は戻ってから触る。
- **未監査**: FatFs / HostDrv の `list_dir` は同型の問題を抱えていないか確かめていない。

---

### 4-27. 日本語テキストの幅と切り詰め

- **現象**: `char buf[64]` のつもりで組んだ行が溢れる、切った末尾が □ になる。
- **原因**: UTF-8 の漢字・仮名は **1 文字 3 バイト**、画面では **2 桁 (16px)**。
  バイト数と桁数と文字数が全部違うので、どれを数えているのかを変数名で区別する。
- **対策**: バッファは桁数 × 3 + 1 で取る。切り詰めるときは **UTF-8 の先頭バイト境界でだけ**切る
  (続きバイト `0x80〜0xBF` の途中で切らない)。

---

### 4-28. 9MB 構成で `v86 -t` が落ちていた (**2026-09-16 に再現しないことを確認**。2026-09-10 の記録)

- **現象**: 物理 9MB (`ExMemory=8`) で `v86 -t` が
  `[ring3] #PF (CPL=3 / syscall) addr=0x00000000 EIP=0x00000000 -> kill app` で死ぬ。
  **8MB と 15MB では通る**。バックエンド (pc98 / pegc) には依存しない。
- **観測**: 落ちた後に `backing_phys` を読むと **0**。ただし `backing_phys` は起動時も 0 なので、
  これは「確保に失敗した」証拠にならない。
- **撤回した推測 (2026-09-16)**: 「呼び出し側が戻り値を見ていない疑いがある」と書いていたが、
  **コードで確かめて否定した**。`v86_mem_setup()` の呼び出し元は 3 か所しかなく、
  **3 か所とも戻り値を見て早期に戻る**:
  `v86.c:623` (`v86_smoke_test` → `v86_smoke_result = 0x8001`、`-1`)、
  `v86.c:764` (ディスク試験 → `0x8003`、`-1`)、
  `v86.c:841` (`v86_boot2` → ディスクを外して `-2`)。
  ユーザーランドの `userland/cmds/v86.c:58` も `rc < 0` を見て中止する。
  `backing_phys` を読むのは `v86_mem.c` の内部だけで、**確保に失敗したままゲストへ入る経路は無い**。
  加えて、落ち方は `[ring3] #PF (CPL=3 / syscall)` = **CPL=3 のアプリがシステムコールの文脈で落ちている**
  もので、V86 ゲストに 0 番地で入った姿ではない。落ちているのは `v86.bin` 側。
- **切り分けの結果 (2026-09-16、実施済み)**: **9MB でも再現しない。**
  試験用 ini (`np21x64w-9mb.ini`、`ExMemory=8`。原本は不変) で起動し、ゲストの `mem` が
  `Physical : 9216 KB (9 MB)` を報告する構成で `v86 -t` を実行 → **`result : OK`**、
  `v86_smoke_result` = **0** (`0x8001` = 確保の失敗ではない)。14MB (`ExMemory=16`) も OK。
  よって「159 ページの連続確保が 9MB で失敗する」という観測自体が現在は成り立たない。
  9 月 10 日以降のメモリ経路の再編・pgalloc の修正・`exec` の資源回収の見直しのいずれかで
  解消したと見られるが、**どれが効いたかは特定していない**。
- **再発したときの切り分け**: `v86 -t` の後に `v86_smoke_result` (`kernel.map` の番地) を読む。
  **`0x8001` なら確保の失敗**、そうでなければ `v86_mem_setup()` に到達する前の別の問題。
- **ini の落とし穴 (2026-09-16 に踏んだ)**: 実際に使われる ini は**実行ファイルと同名の
  `np21x64w.ini`** (ANSI 符号化)。`np21w.ini` (UTF-16) は使われていない。
  `aidebug=true` / `aidbport=8025` も前者にある。`tools/np21w_ini.py` は仕様で UTF-16 を拒否するので、
  UTF-16 の ini には使えない。
- **アイドル時の pgalloc (9MB)**: `limit_pfn=generic_end=2304`、`total_pages=1278`、
  `used_pages=256` (shlib 帯)。空きは 1022 ページあり、159 は十分に見える。
  アプリ実行中は `exec_child_claim` が a=[0x500000,0x7BD000) と b=[0x8BD000,0x8FE000) を
  取り、残る A/B 穴は [0x7BD000, 0x8BD000) の 256 ページ。**なぜここから 159 が
  取れないのかは未解明**。
- **撤回した推測**: 「A/B 穴が 0x800000 (アプリ固有 PDE の境界) をまたぐのが原因」と
  一度書いたが筋が悪い。V86 のバッキングは**物理**ページで、写像先はゲスト線形
  0x1000-0x9FFFF = PDE 0 (`kernel/v86_mem.c` の `paging_map_range`)。バッキングの
  物理位置が PDE 1 の境界をまたぐかどうかはこの写像に影響しない。
- **窓の撤去とは無関係と考えている**が、旧カーネルでの再現はしていない。
- **次に見るべき所**: 失敗時の `pgalloc_free_pages()` / `used_pages` と、
  `alloc_n_pfn` が実際に走査した範囲。`v86` コマンドが CPL=0 か CPL=3 かで
  `exec` のレイアウトが変わるので、そこも確認する。
- **影響**: PEGC GUI の下限として 9MB を採る場合にぶつかる。CUI 8MB と 15MB は無事。

---

### 4-29. NHD 満杯を `make deploy-nhd` が黙って通していた (2026-09-10、修正済み)

- **現象**: 配備は「完了! 183 ファイル」「Done! (199.9 MB copied)」と出て **exit 0**。
  だが NHD の `/boot/vmkernel.lz4` が **446,464 B に切り詰められて**いて、
  手元の成果物 (448,812 B) と一致しない。ゲストは古いカーネルで動き続ける。
- **本当のエラー**: 出力の途中に 1 行だけ出ている。grep しないと流れる。
  `Error: vmkernel.lz4 -> /boot/vmkernel.lz4: cp: error writing ...: No space left on device`
- **`df` が嘘をつく**: 「69M 空き / 63% 使用」と出るのに ENOSPC。
  `e2fsck -fn` で `Free blocks count wrong (80315, counted=2)` — スーパーブロックの
  空きブロック数が壊れていた。修復後の実数は **203,931 / 203,932 ブロック使用**。
- **満杯の原因**: NHD ルート直下にホスト側のディスクイメージが入っていた
  (`dos5hd.nhd` / `dos5hdmaster.nhd` で 82MB、`Ys*.D88/NFD`・`dos5*.fdi`・
  `fd98_2hd.img`・`os32_serial_log.txt` で約 10MB)。原本は `C:\os32` にあり
  ゲストからは `/host` で見えるので、NHD 側は重複。削除して 92MB 空けた。
- **対処**: `sudo losetup -f --show --offset 836096 build/nhd/os32.nhd` で
  ループを張り `sudo e2fsck -fy <loop>` で修復 → 不要ファイルを削除 → 再配備。
- **教訓**: 配備の成否を「完了/Done の文言」で判断しない ([V4])。
  **必ずゲストの `ls -l /boot/vmkernel.lz4` と手元の `stat -c%s` を突き合わせる**。
  `os32-cycle deploy` はこの照合を持つが、`make deploy-nhd` を直接叩くと素通りする。
- **修正済み (2026-09-10)**: `nhd_deploy.py` の `do_sync` / `do_sync_from_hostdrv` が
  失敗を数えるようにし、1 件でも失敗したら `False` を返す (末尾は「完了!」ではなく
  「失敗! N ファイルをコピーできなかった」)。`sync-from-hostdrv` は戻り値すら
  見ていなかったので `sys.exit(1)` を足した。あわせて**失敗した宛先を消す**
  (`remove_partial`) — `cp` は書き込み前に宛先を切り詰めるので、残すとゲストが
  「存在するが壊れた成果物」を掴む。消えていれば NOT FOUND で失敗が見える。
  回帰は `tools/tests/test_nhd_deploy_failure.py` (`make check-tools-host` に登録)。
  **それでもサイズ照合はやめない** — 失敗の形は ENOSPC だけではない。

### 4-30. `/fd0` が hd0 に化けて ext2 を二重マウントし、スーパーブロックを巻き戻していた (2026-09-10、修正済み)

- **現象**: きれいな ext2 に 106KB のファイルを 1 個 `cp` して `sync` するだけで、
  ホスト側 `e2fsck -fn` が必ず `Free blocks count wrong for group #0 (4097, counted=3991)` /
  `Free inodes count wrong for group #0 (1761, counted=1760)`。ずれはちょうど
  ファイル 1 個分。**強制終了とは無関係** (`CloseMainWindow` の通常終了でも同じ)。
  ビットマップ側は常に正しい。
- **切り分け**: `mounts[0].fs_ctx` の `free_blocks_count` (ctx+52) は
  cp 前 91386 → cp 後 91280 (−106) と**メモリ上は正しい**。一方 `sync` 後の
  イメージ `836096+1024+12` は 91386 のまま。**ディスク上だけが巻き戻る**。
- **踏んだ落とし穴**: `ext2_write_super_raw` にブレークを置いたら 0 ヒットだったので
  「sync がスーパーブロックを書いていない」と判断した。**誤り** —
  `ext2_write_super_raw` は `ext2_sync` に**インライン展開**されていた
  (`i386-elf-objdump -d` で確認)。書き込み自体は走っていた。
  **static でない関数でもインライン化される。ブレーク 0 ヒットを未実行の証拠にしない。**
- **真因**: `vfs_sync()` から `ext2_vfs_sync` が **2 回**呼ばれていた。マウント表を実機
  メモリから読むと `mounts[1] prefix=/fd0 dev=fd0` の `Ext2Ctx` が `base_lba=1632`、
  `dev` ポインタまで `/` と同一。`vfs_mount()` は
  `ops->mount((dev_type << 8) | dev_id)` と種別を上位バイトに載せて渡すが、
  ext2 は下位バイトしか見ていなかったので `fd0` (=`0x100`) が
  `ide_drive_present(0x100 & 3)` → hd0、`ext2_dev_for` → `"hd0"`、
  `ext2_find_partition` → `drive_info[0]` と**すべて hd0 に化けていた**。
  ブート時の自動マウントループ (`kernel/kernel.c`) が root 以外の全ブロック
  デバイスに ext2 を試すので、`/fd0` として 2 つ目の `Ext2Ctx` が必ずできる。
  `vfs_sync()` は全マウントを回すため、`mounts[0]` が正しい空き数を書いた直後に
  `mounts[1]` が**マウント時点のスナップショット**を同じセクタへ書き戻す。
  ビットマップはこの経路で触らないので真値が残り、「counted=」不一致になる。
- **修正**: `fs/vfs.h` に `VFS_DEV_*` と `VFS_MOUNT_DEV_ENCODE/TYPE/ID` を公開し、
  `ext2_vfs_mount` は `VFS_DEV_HD` 以外を ext2 本体に届く前に拒否。
  `iso9660_mount` も同じ取りこぼし (`'0' + (char)dev_id`) があったので CD 限定に。
  さらに `vfs_mount()` が同じ (ops, 種別, unit) の二重マウントを `VFS_ERR_EXIST` で
  断る網をクラスごと張った。回帰は `tools/tests/test_vfs_mount_dev.py`
  (`make check-vfs-mount-dev-host`)、経緯は `tools/tests/vfs_mount_dev_tdd.md`。
- **実機確認**: 新カーネル配備後、マウント表は `/` と `/host` のみ (`/fd0` が消えた)。
  438KB のファイルを `cp` + `sync` → 通常終了 → `e2fsck -fn` が **RC=0 クリーン**。
  コピーは md5 一致。
- **教訓**: 「FS ドライバは下位バイトしか見ないので互換」というコメントが
  `fs/vfs.c` にそのまま書いてあった。**呼び出し側が広げたエンコードは、
  受け側全部を数えて確かめる**。片方が無視すると、別デバイスが同じ実体に化ける。

### 4-31. `gui_gate.py` で GUI を叩くときの 2 つの罠 (2026-09-10、修正済み)

- **rshell を抜けてから `/api/key` の text を打つ。** rshell は `kbd_trygetchar` の
  生読みで、入力が途切れるたびに 1 コマンドとして実行する。4 文字ずつ送る
  `key(text="os32gui")` は `os32` / `gui` という別々のコマンドになり、GUI には入らない
  (画面に `os32: command not found` / `gui: command not found` が並ぶ)。先に
  `key(seq="ESC")` で `[Remote shell closed]` を出してから `enter_gshell()`。
  `leave_gshell()` は末尾で `rshell` を打って復旧するので、**台本側で二重に打たない**
  (GUI 内で打つとターミナルが rshell を起動し、以後の打鍵を全部食う)。
- **Start メニューの行座標は項目数から導く。** メニューはタスクバーから上へ伸びるので、
  v1.3 (T5a) で "Display fixture" が足されて 5 → 6 行になった時点で全行が 18px 上がり、
  5 行前提の固定値 `H-107+18r` は行 r が r+1 に当たっていた。「CUI mode」(r=3) の
  クリックが **Shut Down** に当たり、確認 Yes でゲストが `System halted` になった
  (リセットで復旧、NHD は無傷)。`gui_gate.py` は `startmenu.rs` の `ROOT_ITEMS` /
  `ITEM_H` / `BORDER` と `taskbar.rs` の `TASKBAR_H` から計算する形に直した。
  **項目を足したら `START_MENU_ITEMS` も更新する。** (2026-09-10 の T5b 撤去で
  "Display fixture" は消え、項目は 5 行に戻した。)
- 観測は `gui_bench` の `CLICK n` (text VRAM) が便利。`on_raw` で `Button` を数えるので、
  WM がアプリへ配ったかそのものが見える。1 クリック = +2 (押下+解放)。
- **rshell は重なる。** CUI シェルは起動時 (GUI から戻ったときも) 自動で rshell に入る。その上から
  `rshell` を手打ちすると 2 段になり、次の ESC は内側の 1 段しか閉じない → 以降の `/api/key` の text が
  外側の rshell に食われて GUI へ入れない (2026-09-11: リセット直後の台本だけ成功し、`leave_gshell` 後の
  台本が全滅した原因)。`leave_gshell()` は `ver` が通るなら打たない形に直した。台本は先頭で ESC を 2 回。
- **台本の入口は道具が確かめる (2026-09-29)。** 上の「先頭で ESC」を台本が守っておらず、
  rshell 有効のまま回すと GUI に入らないまま **RESULT: OK** になっていた (R2 の予備調査、
  `docs/archive/settings/TASK_S5.md` §6)。`gui_gate.py` の台本は `begin_gui()` から入る:
  ESC → tvram の末尾に `[Remote shell closed]` が新しく出たのを確認 (出る限り ESC を最大 2 回足して
  重なりも閉じる) → `os32gui` → `/api/status` が「`scrn_ymax == --h` かつ `grph_disp == 1`」(98 の GDC /
  PEGC) でも「`wab_relay == 1` かつ `wab_height == --h`」(Cirrus) でもなければ NG (rshell を戻して終わる)。
  ホスト試験は `make check-gui-gate-host`。
- **Cirrus の GUI は 98 の表示レジスタに出ない (2026-09-29 夕)。** NP21/W の Cirrus (WAB 中継) では gshell 中も
  `scrn_ymax 400`・`grph_disp 0` のままで、画面は `/api/status` の `wab_relay 1`・`wab_height 480` が表す。
  `grph_disp` だけで「GUI に居るか」を決めると、Cirrus の GUI を CUI と読んで入口が NG になり、後始末が
  CUI の前提で打つ `rshell` が **gshell に入る** (試験担当が踏んだ。ゲストは GUI に残る)。高さ違いの NG も同じ
  (`grph_disp == 1` のまま高さだけ違う 98 / PEGC でも起きる)。`back_to_cui` は `gui_entered` と同じ規則の
  `gui_height` で GUI に居るかと実際の高さを決め (WAB 中継中は `wab_height`、でなければ `grph_disp == 1` の
  `scrn_ymax`)、その座標で `leave_gshell` を通してから戻す。撮影は `X-Screen-Source` を `--out` の
  `shots.json` に残す (Cirrus では `wab` のはず)。

### 4-32. `ext2_read_file` は端数ブロックでも 1KB 書いていた — 小さな static バッファへ読むと隣を潰す (2026-09-11、修正済み)

- **現象**: K5b-K (アプリ 4 本) の新カーネルが初回起動で `FATAL: shell.bin load failed`
  (`kernel/kernel.c:582`)。kselftest は 44/0 で通り、`[DBG] NOMEM:` も出ない。画面 2 行目の
  `14000` は `kernel/shlib.c:191` のロード報告 `@414000` が 80 桁で折り返した尻尾で、無関係。
- **原因**: `fs/ext2_file.c:34` の `ext2_read_file()` が端数ブロックでも `ext2_read_block()` で
  **1024 バイト**を宛先へ書いていた (`max_size` で `remaining` は頭打ちにするが、書き込み長は
  頭打ちにしていない)。K5b-K 前は exec がファイル全体をロード番地へ読んでいたので無害。
  K5b-K がヘッダを `static u8 hdrbuf[108]` へ先読みするようにした瞬間、隣の `.bss` の
  `resolved[]` (解決済みパス) がファイルの中身で塗り潰され、本体の `vfs_read(resolved, …)` が
  ゴミのパスを引いて `EXEC_ERR_NOT_FOUND` になった。
- **修正**: 端数ブロックは `ext2_g_blk` を中継して `to_copy` だけ写す (`ext2_read_stream` と同じ)。
  回帰は `tools/tests/test_ext2_read_bound.py` (`max_size=108` で溢れ / `resolved[] clobbered` を
  RED で捉える)。
- **教訓**: (1) **FS の read が「要求長ちょうど」しか書かないと仮定しない** — 小さな static
  バッファへ読むときは `kernel.map` で隣に何が居るかを見る。(2) PM の見当 (NOMEM 経路) は
  外れていた。実機の画面に `[DBG] NOMEM:` が無い時点で除外できたはず — **症状にある/ない
  出力で経路を先に絞る**。(3) `ext2_g_aux` はビットマップ用 (§4-24) なので中継に使わない。

---

### 4-35. 「読めなかった」を「無い / 未割当 / その型ではない」と読み替えない — ext2 の B8、6 往復の教訓 (2026-09-15、修正済み)

- **症状**: 実在する `/etc` に `cat /etc` でディレクトリの生データが読める。`O_CREAT` だけで既存ファイルが空になる。
  ディレクトリの `rename` が途中で失敗すると 2 つの名前が 1 つの inode を指し、片方を `rmdir` すると残りの名前が
  解放済み inode を指す。解放途中の失敗で媒体上の inode が解放済みブロックを指したまま残り、次の割り当てで別ファイルと共有される。
- **原因**: 読み取り失敗を、`ext2_lookup` は `NOTFOUND` に、`ext2_bmap` は `0` (= 未割当) に、`fs_is_dir` /
  `vfs_open_internal` は「ディレクトリではない」に、それぞれ**読み替えていた**。1 段直すと次の受け手で同じ形が出る
  (`fs_is_dir` → `vfs_path_kind` → `vfs_open_internal` → `mkdir` / `create` / `rename` → `ext2_free_block` → `alloc_block`)。
- **修正**: 票 [`archive/shell/TASK_FS_TYPE.md`](archive/shell/TASK_FS_TYPE.md) §2。解放は「媒体上の参照を先に消し、解放は後」
  (ジャーナル無し、漏れは許容し交差リンクは原理的に起きない)。ディレクトリ rename は旧名を先に消す。メタデータの
  I/O エラーで以後の書き込みを断る (`OS32_ERR_ROFS` = -15、Linux の `errors=remount-ro` 相当。再起動で警告付き rw)。
  空なのに `links_count > 2` の `rmdir` を断る。
- **教訓**:
  1. **soft updates の 3 規則で全書き込み箇所を点検すれば往復せずに済んだ**: 初期化前の構造を指さない / 古い参照を
     全部消す前に再利用しない / 新しい参照を張る前に古い参照を外さない。往復 3・4・5 はこの 3 規則の再発見だった。
  2. **判定関数の戻り値だけを試験しても、受け手が追従しているかは分からない。** 往復 4 の B7 は `vfs_path_kind` の
     戻り値までしか見ていない試験がすり抜けた。実物の `vfs_open` / `vfs_open_sqlite` を通す。
  3. **自前の媒体検査を育てるより、本物の `e2fsck -fn` を正解にする** (`tools/tests/b8_e2fsck.py`)。自前の検査が
     整合と判定した像を e2fsck が不整合と判定した例が 2 回あった。
  4. ジャーナルは作らなかった。守る不変条件が「媒体上のどの参照も解放済みブロックを指さない」の 1 つなら順序で足りる。
     Linux (GPL) のコードは持ち込めない。設計の参考は FreeBSD の ext2fs と soft updates (BSD)。

### 4-36. `hsync` は同サイズの差し替えを届けていなかった — 判定を日時で絞り、候補だけ内容比較する (2026-09-15、修正済み)

- **症状**: `.bss` だけ変わった `libos32gui.shlib` (サイズ同一) を `hsync sys` が「同一」と判定してスキップし、
  修正が効いていないように見えた (2026-09-14)。
- **原因**: 同一判定がサイズだけだった。
- **修正**: 票 H1 で内容比較 + CRC 読戻し検証に変えたところ、変更 0 件でも `hsync sys` 25.8 秒 / 全体 135 秒になった
  (両側を全部読むため)。票 H3 で HostDrv の `stat` に `mtime` を載せ、`sys_set_mtime` (KAPI v52) で宛先へ保存し、
  **サイズか日時が違うものだけ内容比較、両方同じならスキップ、日時が不明なら内容比較** (証拠が無いことを同一の根拠にしない)
  に。2 回目以降 `hsync sys` 0.26 秒 / 全体 2.9 秒。見逃すのは「サイズも日時も同じで中身が違う」ものだけ (`--verify` で全件比較)。
  [`archive/shell/TASK_H1.md`](archive/shell/TASK_H1.md) / [`TASK_H3.md`](archive/shell/TASK_H3.md)。
- **教訓**: 「読まずに同一を証明する」には前回一致・コピー元不変・宛先未変更の 3 条件が要る (Codex 相談)。
  MD5 に変えても I/O は減らない。日時は省略の根拠ではなく絞り込みに使う。
- **続き (票 H2、2026-09-16)**: H1 / H3 の時点では宛先を `O_CREAT|O_TRUNC` で**直接上書き**していたので、
  コピー途中・検証・mtime のどこで落ちても**旧内容は既に切り詰められていて戻らなかった**
  (失敗行に「直接上書きなので旧内容は残らない」と出していた)。H2 でここを変えた:
  宛先と同じディレクトリの**予約名 `.hs~<名前>`** へ `O_EXCL` (KAPI **v53**) で作り、
  書き込み → `vfs_sync` → 読戻し (長さ + CRC) → `mtime` まで済ませてから `sys_rename` で置き換える。
  ext2 側は置き換えの順序を変え、**宛先エントリの inode 番号をその場で (フィールドを含む 1 セクタだけ)
  書き換える**ようにした — 宛先の名前がどの段でも消えない。
  - **公開の前**の失敗 … 旧宛先の名前・内容・日時がそのまま残る。一時ファイルは**片づけられれば**片づける
    (メタデータの失敗で `ROFS` に落ちた後は `unlink` も通らないので `STALE` と表示し、次の実行が消す)。
  - **公開の後**の失敗 … 宛先には検証済みの新しい内容が現れる。**旧内容へ戻そうとしない**。
    `replace_partial` と報告し、成功には数えない。
  - 公開したかどうかの判定は **宛先の `st_ino`**。サイズと CRC は証拠にしない
    (`-f` で新旧が同じ内容だと未公開を公開と誤る)。
  - ジャーナルは無いので「原子的」とは書かない。電源断では一時ファイルが残り得る。
  - `.hs~` は **hsync の予約接頭辞**。`st_nlink` を見ずに片づけるが、保護対象 (`/etc/settings.db*` の実体) は
    予約より保護を優先して消さない。利用者はこの接頭辞を使わないこと (man ページと `06_filesystem.md` に明記)。
  - **KAPI v53 未満のカーネルでは既定で 1 件も書かずに断る** (`kernel_too_old`)。
    `--unsafe-overwrite` を明示したときだけ旧来の直接上書きで進み、そのときは旧内容が残らない。
    `build/app.conf` の hsync の要求版は **52 のまま** — 53 にすると exec が
    `min_api_ver > KAPI_VERSION` で弾き、この警告自体が届かない。
  [`archive/shell/TASK_H2.md`](archive/shell/TASK_H2.md)、記録は `tools/tests/hsync_h2_tdd.md` /
  `vfs_excl_tdd.md` / `b8_tdd.md` §13。

### 4-37. Agent の worktree は古い基点 (`main` 側のマージ) から始まることがある — コーダーは最初に基点を報告する (2026-09-15)

- **症状**: コーダーが「`net/link.c` に直書きが無い」「移植性調査の文書が無い」と報告。実際は worktree が `fac0d89`
  (2 日前の `main`) から始まっており、コーダーは HEAD を確かめずに PM の指示の値を書いていた。B8 のコーダーも同じ基点だった。
- **修正**: [`tasks/agents/ROLES.md`](tasks/agents/ROLES.md) に規則 — PM は依頼文に期待する SHA を書く。コーダーは
  `git log -1` を報告の先頭に書き、違えば `checkout` で合わせる。確かめられなければ推測で書かない。PM は着地前に照合する。
  **「無い」と報告されたらまず基点を疑う。**
- **教訓**: 着地は「差分を退避 → 正しい基点へ → `git apply --3way`」で載せ替えられる。溜まった worktree (69 本) は
  着地のたびに消す。

### 4-38. 切り詰めた名前・引数・パスで**別のもの**が起動する — シェルは切り詰めたら実行しない (2026-09-16、修正済み)

- **症状 (T4)**: `run_cmd_internal` はコマンド名を `PATH_MAX_LEN - 5` (251) で切ってから `.bin` を足していた。
  251 バイトの接頭辞が実在すると、`Pextra…` と打ったのに **`P.bin` が起動する**。
  同じ型が `try_exec_from_path` の PATH 分割 (T17、254 で切って残りが**次のディレクトリ**になる)、
  glob のパターン / ディレクトリ部 (T7、切ったパターンが別のファイルに当たる)、
  `try_exec` / `exec` / `time` の引数再構築 (T3、溢れた引数を落として起動する) にもあった。
- **なぜ気づきにくいか**: どれも**成功したように見える**。赤字も終了コードも出ないまま、
  意図と違うものが走る。`glob` の `mem_alloc` 失敗 (T6) は一致の**一部だけ**を渡すので
  `rm /tmp/item*` が 1 件だけ消えて成功に見えた。
- **修正**: 票 [`archive/shell/TASK_SH_TRUNCATION.md`](archive/shell/TASK_SH_TRUNCATION.md) §2 の規則
  **「切り詰めたら実行せずに断る」**を経路ごとに入れた。赤字 1 行 (`sh_refuse(何が, 上限)`) +
  印 (`sh_refused_flag`)、スクリプト中なら `script_exec` が打ち切る。
- **数え方の落とし穴**: `try_exec` は `parse_args_and_glob` が剥がしたクォートを**付け直す**ので、
  素直に長さを足すと足りない。空白 / `"` / `'` / `\` を含む引数と空引数は前後の `"` で **+2**、
  本体の `"` と `\` は `\` が付いて **2 倍**。数え落とすと上限のすぐ内側で切り詰めたまま起動する。
- **層が 2 つあること**: `sh.bin` では `try_exec` の 510 を通っても `sh_launch` が要求表の
  上限 (`LAUNCH_CMDLINE_MAX - 1` = 255) で断る。**510 が効くのは常駐 `shell.bin` だけ**。
  断る理由を取り違えないよう、`launch_req` を呼ぶ**前**に測る (呼ぶと `INVAL` が返り、
  `sh.bin` はそれを「GUI 外」と読んでしまう)。
- **断ったら走査を止める**: `try_exec` は PATH 候補ごとに呼ばれるので、断りを戻り値だけで
  伝えると同じ赤字が候補の数だけ出て最後に `command not found` で閉じる。印の**差分**
  (呼ぶ前に `sh_refused_peek()` を控え、0 → 1 に変わったときだけ止める) で見る。
  peek の値そのものを見ると、入れ子の `execute_command` が持ち込んだ印で無関係な行まで止まる。
- **同じ型がもう 1 つ**: 引数が多すぎて行を捨てる I1 (`sh: too many arguments`) も赤字だけで印を
  立てておらず、**スクリプトが次の行へ落ちていた**。上限ではないので文言は据え置きで
  `sh_refuse_mark()` だけ足した (空の段 / glob の確保失敗と同じ扱い)。
- **いちばん重い症状 (T1)**: `if "<長い値 A>" == "<長い値 B>" rm -rf /data` で、A と B が違うのに
  **条件が真になって `rm -rf` が走る**。`strip_quotes` が両辺を 255 文字に切り詰めてから比べていた。
  比較・起動・登録・展開のどれであっても、切り詰めた値で先へ進めば誤動作になる。
- **直すときの罠 (ここが本題)**: 断った行を捨てて**次の行へ進むと、修正が新しい危険を生む**。
  `if … goto done` を断ると、飛ぶはずだった行を飛ばして**その下の破壊的なコマンドに落ちる**。
  スクリプト中で断ったら打ち切る。対話では打ち切らない。起動時の `/etc/profile` は続行する。
  パイプの段で断ったら**後続の段も実行しない** (走らせるとリダイレクト先が空で上書きされる)。
- **閉じ方**: 独立レビュー 4 往復でも同種の経路が出続けた。最後は `userland/shell/` の固定長バッファを
  **1 つずつ表にして洗った** (`tools/tests/sh_truncation_tdd.md` §2 に 26 件)。
  同種の欠陥は「1 件直す」ではなく「経路を全部数える」でしか閉じない。
- **試験**: `tools/tests/sh_truncation_host.c` (178 検査) + `test_sh_truncation.py --mutate` (変異 27 本)。
  経緯と RED → GREEN は [`../tools/tests/sh_truncation_tdd.md`](../tools/tests/sh_truncation_tdd.md) §0-4。

### 4-39. 終了コードと起動エラーが同じ値の空間に混ざっていた — 1 コピーでも 2 回実行されていた (2026-09-16、修正済み)

- **症状**: `/usr/bin/xxx.bin` を 1 本置いて `xxx` と打つと、`return -1` (または `-3`) で
  終わる子が **2 回実行され**、最後に `xxx: command not found` と出る。
  `exec_run` は正常終了で子の値 (`exec_exit_status`) を、起動失敗で `EXEC_ERR_*` (-1〜-5) を
  返す — **同じ値の空間**なので、`try_exec` の戻り値 `-1` / `-3` を
  `run_cmd_internal` が「このディレクトリには無い」と読み、次の候補へ進んでいた。
  カーネルの `exec_launch` はパスに `/` が無いと自分で `/bin` `/sbin` `/usr/bin` を探すので、
  シェルが 2c で同じ名前を渡すだけで**同じファイルがもう一度走る**。同名を 2 か所に置く必要は無い。
- **なぜ値では直らないか**: `exit(-2)` と fault はどちらも `exec_exit_status = -2` になる。
  `exec_exit` の中では区別できない (`exec_fault_recover` も `kapi_sys_exit` も同じ関数へ入る)。
  **値から種別は作れない**。
- **修正**: KAPI v55 で `exec_last_result(int *kind, int *code)` を足し、
  **種別は畳んだ側が渡す**形にした (`exec_exit` の引数を 1 つ増やす):
  `kapi_sys_exit` → `EXITED` / `exec_fault_recover` → `FAULT` /
  CTRL+STOP (`ring3_abort_check` → `ring3_abort_kill`) → `ABORTED`。
  シェルは **種別だけ**で PATH 走査を止めるか決める — `NOT_FOUND` と `INVALID` のときだけ次の候補へ。
- **起動失敗は `exec_exit` を通らない**: `exec_launch` には早期 return が 13 か所あり、
  そこを通ると記録が前回のまま残る。`exec_run` が **すべての return 点で** 記録を書く
  (`NONE` のままなら戻り値から写す) 形にして塞いだ。成功の直後に未知のコマンドを打っても
  前の子の終了コードは返らない。
- **GUI の子は記録しない**: `exec_start` / `exec_resume` の子 (`AppSlot.gui`) は
  同期起動の記録を書かない。読み口である `sh.bin` はカーネルの記録を読まず
  (要求表の状態を自分で写す) ので、書くと紛れるだけ。
- **起動の口を 1 つに**: `try_exec` からは `sh_exec_result(cmdline, &kind, &code)` だけを呼ぶ。
  常駐は `exec_run` の**直後**に `exec_last_result` を読み、`sh.bin` は要求表の
  `DONE` / `FAILED` を写す。**`sh.bin` がカーネルの静的な記録を読むと、自分の子とは限らない**。
- **`$?` と「断りの印」は役割が違う**: 印 (`sh_refused_flag`) は「実行そのものを拒否したか」の
  制御信号、`$?` はその結果の状態値。二重管理にしない — 断った行は印で止め、値としては 2 を入れる。
- **`exit` は値を書いてから要求を立てる**: `exit 3 | echo tail` で要求を先に立てると、
  段ループを抜けた後の `$?` が 0 になる。要求の有無と値は**別の変数** (`sh_exit_flag` /
  `sh_exit_code`) — 真偽値に値を入れる作りだと `exit 0` で終われない。
  段ループの見張りは**末尾の 1 か所だけ**にしてある (入口にも置くと片方を壊しても挙動が
  変わらず、変異試験に歯が立たない)。
- **試験**: `tools/tests/sh_status_host.c` (常駐 186 + `sh.bin` 128 = 314 検査) +
  `test_sh_status.py --mutate` (変異 23 本)。**登録表も `execute_command` も実物**を通す
  (`exit` を文字列で直接見るスタブを置くと、本物の登録・伝播が壊れていても緑になる)。
  経緯と受入の対応は [`../tools/tests/sh_status_tdd.md`](../tools/tests/sh_status_tdd.md)。

## §5. デバッグ道具箱

### カーネル内デバッグ出力

| 関数 | 用途 | 出力先 |
|------|------|--------|
| `kprintf()` | カーネル内デバッグメッセージ | テキストVRAM (コンソール) |
| `serial_printf()` | シリアル経由デバッグ出力 | RS-232C → ホスト側ターミナル |

シリアル出力はコンソールを汚さないため、画面描画に影響するバグの調査に特に有用。

### NP21/W の停止・起動 (`tools/np21w_ctl.py`)

NP21/W の停止と起動はこの道具で行う。`taskkill` や `Start-Process` を手で打たない —
落とした直後に起動すると媒体がまだロックされていて、起動が途中で止まる (§4-60)。
停止はエミュレータ自身に頼む (ai-debug フォークの `/api/instance` → `/api/quit`、
仕様は `np21w-src/docs/03-api-reference.md` の「アプリ層の口」)。

```bash
python3 tools/np21w_ctl.py stop                          # /api/quit save=0 → その pid が消えるまで待つ
python3 tools/np21w_ctl.py start --ini np21x64w.ini      # プロセス 0 → 媒体が続けて 5 秒開ける → 起動 → pid/exe 照合 + 10 秒生存
python3 tools/np21w_ctl.py start --ini np21w-trial-cdinst.ini --fd os32_boot.d88 --wait-ready
python3 tools/np21w_ctl.py wait-ready                    # /api/tvram に "Waiting for commands" (既定 180 秒)
python3 tools/np21w_ctl.py fdd --drive 1 --insert os32_boot.d88   # FD の出し入れ (--eject)。ini は変えない。/api/instance に反映されるまで待つ (--ready-wait 5)
python3 tools/np21w_ctl.py cd os32_install.iso           # 動いている NP21/W の CD-ROM に ISO を入れる (/api/cd、ini は変えない)。--drive N (IDE スロット、既定は CD-ROM の最初)、cd --eject で取り出し
python3 tools/np21w_ctl.py status [--ini <name>]         # プロセス・aidebug (instance)・ダイアログ・媒体の free/locked/missing
```

- **トークン**: `/api/quit`・`/api/fdd`・`/api/cd` は NP21/W が起動時に exe の隣へ書く `np21w_aidebug_<port>.token`
  (利用者だけが読める ACL) の中身を `X-Aidebug-Token` で要求する。ctl は `NP21W_AIDEBUG_TOKEN_FILE`
  (WSL パス) → `/api/instance` の `token_file` → `NP21W_DIR/np21w_aidebug_8025.token` の順で読む。
  中身は出力しない ([D3])。読めなければ `stop` は exe 一致の強制終了に落ち、`fdd` / `cd` は失敗する。
  `/api/state/save` (ホストにファイルを書く) も同じトークンが要り、`tools/np21w_mcp` の `emu_state_save`
  は `np21w_client.token_headers()` で付ける。読み取り系の口 (`/api/status` `/api/cmd` `/api/key` など)
  はトークン不要のまま。Origin ヘッダを付ける要求は 403 (ブラウザ経由を閉じる) — curl / urllib は
  付けないので手順は変わらない。トークンは NP21/W がサーバを (再) 起動するたびに変わる (ini の再読込を含む)
  ので、道具は要求のたびに読む。
- `fdd --insert` の 200 は「受理」で、FD は 0.4 秒のエミュレーション時間の後に入る (`DISK_DELAY`)。
  ctl は `/api/instance` の `fdd[].path` に現れるまで待って `ready` と言う。`pending` のまま
  なら理由 (ブレーク中 / 一時停止 / 背景で停止) を出して失敗する。
- `/api/fdd` と `/api/cd` は NP21/W 側が同じ状態機械 (UI 側の待ちは有界、期限切れ・取り下げた要求は後から
  実行されない — `np21w-src/docs/02-architecture.md` §18「媒体要求の状態機械」) で、HTTP の待ちは最悪 21 秒
  (受け取り 5 + 実行待ち 6 + 適用中 10)。ctl の POST は両方 25 秒で切る。503 `being applied` (適用は始まったが
  終わらなかった) は失敗と決めつけず、`fdd[]` / `ide[]` に反映されるまで待って結果を言う。それ以外の 503
  (`nothing was changed` — UI が拾わない・停止や共有区間が取れない・期限切れで取り下げ・前の要求が実行中) は
  何も変わっていないので失敗。`/api/fdd` の insert のパスもローカルの `X:\…` だけ (UNC・ネットワークドライブは 400)。
- `cd` は ini の `CD3_FILE` が空で毎回空のドライブで起動する (ini の `IDE3TYPE=2` はドライブを作るだけ)
  ことへの対処で、ini は書き換えない ([D2])。ISO は NP21W_DIR 直下の名前・ローカルの `C:\…`・`/mnt/<x>/…` のどれか (UNC は NP21/W の HTTP スレッドを
  止めうるので受けない)。ISO はローカルの固定ディスクに置く (ネットワークドライブの割り当ては 400、ジャンクションは検出しない)。
  空のドライブへはすぐ入り、別の CD が入っていれば NP21/W が古い媒体を出して 6 秒 (エミュレーション時間)
  後に入れる (`changing`)。ctl は `/api/instance` の `ide[]` に現れるまで待ち (`--ready-wait 15`)、
  `changing` のままなら理由を出して失敗する。媒体の情報が更新されない (`media_fresh:false`) ときは
  `trap_pause` で「ブレーク中」と「UI スレッドが答えない」を分けて言う。ブレーク中の `/api/cd` は 409。NP21/W 側は api_version 3 (2026-09-26〜) が要る — 古ければ
  「make deploy が要る」と出す。`status` は空の CD ドライブも `ide3 (cdrom): empty` と出す。
  CD の中身は `np2cfg.idecd` に残るので、別の操作で設定が汚れた状態で `save=1` 終了すると ini に
  残りうる (`stop` は `save=0`)。仕様は `np21w-src/docs/03-api-reference.md` の「POST /api/cd」。
- `--api-timeout 0` は HTTP を 1 回も呼ばない (`/api/dialog` も)。HTTP の待ちには残り時間を渡し、
  期限を過ぎてから届いた応答は成功に数えない。

- **止める対象**: `NP21W_DIR` の exe (`--exe`、既定 `np21x64w.exe`) と CIM の `ExecutablePath` が
  一致するプロセスだけ。名前に np21 を含むだけのもの、別の場所に入っている NP21/W、exe の
  パスが読めないものには触らない (「対象外」と表示する)。
- `stop` の順: `/api/instance` で pid と instance_id を得る → exe が一致すれば
  `/api/quit` (`save=0`、ini と resume を書かない) → その pid が消えるまで待つ。API が応答しない・
  quit が効かない・**フォークが古い** (`/api/instance` が 404 — 「make deploy が要る」と出す) ときだけ、
  exe 一致のプロセスを `taskkill /F` する。
- `start` の順: この exe のプロセスが無いか確かめる (残れば待つ) → 各媒体を Windows 側から
  `[IO.File]::Open(path,'Open','ReadWrite','None')` で **続けて `--stable` 秒 (既定 5、1 秒おき)
  開けるまで**待つ。**安定した媒体も毎回プローブし続け**、1 回でもロックされたら数え直す →
  全部安定したら 2 秒置いて**全部をもう一度**プローブ (ロックされていれば数え直し) →
  `Start-Process "<exe>" "/i<ini>" ["<fd>"]` → `/api/instance` の pid と exe が起動したものと一致し、
  プロセスが `--alive` 秒 (既定 10) 生きていることを確かめ、成功を返す直前にもう一度 pid を見る。
- `--timeout` (既定 60) は起動までの**全体の**期限。プローブには残り時間を渡し、期限を過ぎてから
  得た結果は成功に数えない。`--timeout` < `--stable` は引数の誤り (2)。
- `--api-timeout` (既定 60) は起動後に `/api/instance` の応答を待つ上限。**0 は「API を待たない」**
  (プロセスの生存だけを `--alive` 秒見る。aidebug を切った ini 用)。
- 起動の確認中に API が黙る・進まないときは `/api/dialog` を見る。NP21/W がモーダルの
  ダイアログを出していれば、そのタイトル・本文・ボタンを出して 1 で終わる。ダイアログ中は
  コアに触る口 (`/api/status` `/api/cmd` など) が即座に 503 `{"dialog":true}` を返す。
  窓の見た目は `/api/appshot` (ゲスト画面の `/api/screenshot` とは別)。
- 1 回開けただけで通さないのは、`make nhd-pull` の直後に Windows 側 (Defender の走査など) が
  一時的に掴み直し、1 回だけのプローブがその隙間を通して NP21/W がすぐ終了したため (§4-60)。
- 待つ媒体は ini の `[NekoProject21]` 節の `HDD1FILE`〜`HDD4FILE` / `CD1_FILE`〜`CD4_FILE` /
  `FDD1FILE`〜`FDD4FILE` / `SCSIHDD0`〜`SCSIHDD3` と `--fd`。値は NP21/W と同じ規則で読む
  (前後の空白、両端の `"` を 1 組外す、節内で最初の値)。ini が読めなければ既定の
  `os32.nhd` / `os32_install.iso` / `os32_boot.d88`。時間切れのときは**ロックされたままの
  ファイルを名指しして** 1 で終わる。
- プローブそのものが壊れたとき (PowerShell の失敗、ロック以外の例外、報告の欠け) はロックと
  区別して、stderr と例外の型名を添えて即座に 1 で終わる。
- `--ini` / `--fd` / `--exe` / `--insert` は `NP21W_DIR` 直下の**名前**だけを受け付ける。ini は読むだけ ([D2])。
  ini を変える trial は `tools/np21w_trial.py` の領分で、こちらは使わない (スキル `os32-emu-config`)。
- 終了コード: 0 成功、1 失敗 (プロセスが残る・ロックが解けない・プローブが壊れた・起動直後に
  終了した・ダイアログ・API が応答しない・起動完了しない)、2 引数の誤り。
- ホスト試験: `make check-np21w-ctl-host` (変異 24 本 + 恒等の対照)。

### NP21/W リモート実行 (HTTP API)

NP21/W (ai-debug フォーク) は内蔵のデバッグ HTTP サーバを持つ。`np21x64w.ini` で
`aidebug=true` / `aidbport=8025` を設定すると、OS32 の rshell と HTTP で対話できる。
外部の中継プロセスは不要。

```bash
# コマンド実行 (行単位。生の打鍵を読む相手には届かない)
curl -X POST http://127.0.0.1:8025/api/cmd --data-binary "ver"

# キーイベントの注入 (FEP 変換、エディタ、ゲームなど)
curl -X POST http://127.0.0.1:8025/api/key -d "seq=SPACE"
curl -X POST http://127.0.0.1:8025/api/key --data-urlencode "seq=SHIFT+SPACE"   # FEP on/off

# マウス (OS32 はシームレス絶対座標)。ax = px*65535/639, ay = py*65535/(H-1)
curl -X POST http://127.0.0.1:8025/api/mouse -d "ax=32818&ay=32851&btn=1&hold=80"

# 画面テキスト (UTF-8) — 画面判定はこちらの方が速い
curl -s http://127.0.0.1:8025/api/tvram

# スクリーンショット取得
curl -s http://127.0.0.1:8025/api/screenshot > screenshot.png
```

- `+` を含む `seq` は `--data-urlencode` を使う (`-d` だと `+` が空白になる)。
- `btn=1` / `btn=0` でドラッグ、`abs=off` で人間にマウスを返す。
- `-m` は短くしない ([V3]、最低 15 秒、長いプログラムは 60 秒以上)。
- GUI 検証の罠は §4-23 にまとめてある。

> WSL からの `127.0.0.1:8025` が届かない環境 (NAT モード + ファイアウォール) では、
> Windows 側の curl (`/mnt/c/Windows/System32/curl.exe`) を使うか、
> WSL をミラーモード (`.wslconfig` の `networkingMode=mirrored`) にする。
> レジスタ・メモリ・逆アセンブル・ブレークポイントまで要るときは
> `tools/np21w_mcp/` の MCP サーバを使う。

### `emu_read_mem` で読むカーネルシンボル

KAPI ではない**カーネルの大域変数** (カウンタ・印)。番地は毎回その版の `build/kernel.map` から引き、
`emu_read_mem` (または `/api/mem`) で読む。どれも起動時 0。

| シンボル | 定義 | 意味 |
|---|---|---|
| `fault_kill_count` | `exec/exec.c` | CPL=3 アプリを例外・ポインタ検証で畳んだ数 |
| `ring3_abort_count` | `exec/exec.c` | CTRL+STOP (`ring3_abort_request`) で畳んだ数 |
| `ring3_range_reject_count` / `_last` / `_addr` / `_page` / `_heap_top` | `exec/exec.c` | KAPI のポインタ早期検証が断った数と、最後に断った種別 (7〜10 は書き側、§4 の該当節)・番地・ページ・その時の heap の上端 |
| `ring3_switch_count` / `ring3_transition_count` / `ring3_park_reject_count` / `ring3_resume_bad_frame_count` | `exec/appslot.c` | 多重アプリの切り替え (受入 G7、[KAPI_SPEC](KAPI_SPEC.md)) |
| `gui_ime_render_rejected` | `kernel/gui.c` | アプリ (CPL=3 由来) の `ime_set_render` を断った数 (常駐側だけが差し替えられる、2026-09-26) |
| `ring3_wm_depth` | `exec/exec.c` | いまカーネルが WM (gshell) のコードへ入っている深さ。ふだん 0、アプリの syscall の中で WM を呼んでいる間だけ 1 以上 |
| `ring3_wm_depth_underflow` | `exec/exec.c` | WM の出入りの対が崩れて深さを 0 未満へ下げかけた数 (0 でなければ対の崩れ) |
| `ring3_wm_fault_count` | `exec/exec.c` | WM (gshell) のコードへ入っている間 (`ring3_wm_depth` 1 以上) にアプリをフォールトで畳んだ数。WM の中の #PF/#GP も KAPI の門の拒否もアプリの kill として畳まれ、`fault_kill_count` しか増えない (§4-61) — これが一緒に増えたら落ちた場所は WM の文脈。ISR の kill の行にも ` (in WM)` が付く |
| `kselftest_pass` / `kselftest_fail` | `kernel/kselftest.c` | 起動時の自己試験 (§2) |

### 有用なゲスト側コマンド

| コマンド | 用途 |
|---------|------|
| `ver` | ビルドタイムスタンプ・バージョン確認 |
| `mem` | メモリマップ・使用量確認 |
| `ls -l` | ファイル一覧 (サイズ・inode 確認) |
| `cat /etc/profile` | 起動設定の確認 |

### ビルド→デプロイ→テストの標準サイクル

| 変更対象 | 手順 | NP21/W再起動 |
|----------|------|:---:|
| プログラムのみ (HostDrv実行) | `make all` → `make deploy` | 不要 |
| プログラムのみ (ホット) | `make hotdeploy FILE=<path>` | 不要 |
| カーネル / ブートFS | `np21w_ctl.py stop` → `make all` → `make deploy && make deploy-kernel` → `np21w_ctl.py start` | **必要** |

```bash
# カーネル変更時のフルサイクル例
python3 tools/np21w_ctl.py stop
make all && make deploy && make deploy-kernel
python3 tools/np21w_ctl.py start --ini np21x64w.ini --wait-ready
curl -X POST http://127.0.0.1:8025/api/cmd --data-binary "ver"   # Build タイムスタンプ確認
```

---

## §6. AI との協調デバッグ

### AI に期待する役割

| 段階 | AI の役割 | 人間の役割 |
|------|----------|-----------|
| 仮説立案 | コードベースを分析し、可能性のある原因を網羅的に列挙 | 仮説の妥当性を評価し、優先順位を承認 |
| 検証コード作成 | 最小限のデバッグコードを提案 | ハードウェア制約違反がないかレビュー |
| ビルド・デプロイ | ワークフローを実行 | NP21/W 上の動作を目視確認 |
| 結果分析 | 出力を解析し、仮説の正否を判断 | 最終的な修正方針を決定 |

### AI が守るべきルール

1. **コード変更前に仮説を提示**: いきなりコードを修正しない
2. **バイナリ反映を最初に疑う**: デバッグ出力が出ない → まず §2 のチェックリストを実行
3. **3回リトライで報告**: 3イテレーション以内に解決しなければ立ち止まる
4. **ハードウェア制約の遵守**: §3 (POLICY_DEV.md) の制約に違反するコードは絶対に提案しない
5. **教訓集を参照**: 新しい問題に直面したら、まず §4 に類似パターンがないか確認する

---

*OS32 Debug Policy — Created: 2026-04-18*

### 4-40. `make check` を途中で止めると**変異が当たったまま**ソースに残る (2026-09-17)

- **現象**: `make check` を時間切れとメモリ不足で 2 度打ち切った後、次の実行が
  `check-sh-status-host` で `MUTATE exit_arg_unchecked SKIP (目印が 0 か所)` を出して
  非ゼロ終了した。他の変異はすべて RED、本体は `ALL PASS` なのにターゲットだけ落ちる。
- **原因**: 変異試験は**実物のソースを書き換えて戻す**作りなので、書き換えと復元の
  あいだで殺されると**変異が当たったまま残る**。`git status` に
  `M userland/shell/main.c` が出ていた。残っていたのは `exit` の引数検査を
  `break` に潰す変異で、`exit abc` を黙って受ける状態。次の実行はその変異の目印を
  探して見つけられず、SKIP になって落ちた (**落ちたのは正しい** — 目印が無いのに
  緑にすると変異が試験されていないことに気づけない)。
- **本当に危ないのはここ**: 打ち切った直後に `git add -A` すると
  **壊したコードがそのままコミットに入る**。今回は commit した回の `make check` が
  別の地点 (`check-host-lib-host`) で死んでおり、`check-sh-status-host` は
  完走して復元していたので無事だった (`git show <sha>:userland/shell/main.c` で確認)。
  **運が良かっただけ。**
- **対策**: `make check` を打ち切ったら、コミットの前に必ず `git status` と
  `git diff --stat` を見る。追跡ファイルに身に覚えのない差分があれば
  `git checkout -- <path>` で戻してから回し直す。
  `git add -A` の前に状態を見るのはこれが理由。
- **所要の目安**: `make check` は 55 ターゲットで **15 分以上**かかる。900 秒では足りない。
  待ち時間を短く切らない ([V3] と同じ考え方をホスト側の試験にも当てる)。
- **解消 (2026-09-26)**: 実物を書き換える変異試験 14 本を全部「一時ディレクトリの写しの木に変異を当てる」
  作り (`tools/tests/mutpar.py`) へ移した。打ち切っても実物に変異は残らない。番人は残してある
  (票 [TASK_CHECK_MUT_PARALLEL](archive/tools/TASK_CHECK_MUT_PARALLEL.md) §5-1)。

### 4-41. ビルドと検査が遅い — 並列を一度も指定していなかった (2026-09-17、解消)

- **現象**: 16 コアあるのに `make` が 1 コアしか使っていなかった。`-j` の指定が
  `Makefile` にも `build/*.mk` にも 1 つも無い。`make check` は 9 分以上かかっていた。
- **対策 1 — ビルド**: `Makefile` で `MAKEFLAGS += -j$(nproc)` を既定にした
  (`make -j1` と書けば従来どおり)。実測: `kernel` 23.7 → **11.8 秒**、
  `all` 70.1 → **31.0 秒**。
- **対策 2 — 検査**: **遅さの正体は逐次ではなく、変異試験が同じソースを奪い合うこと**
  だった。58 本のうち**ソースを書き換えるのは 10 本だけ**で、残り 48 本は並列にできる。
  `check:` を 2 段に割った (1 段目 = 書き換えない 48 本を並列、
  2 段目 = `--mutate` を渡す 10 本を `-j1`)。**152 秒**になった。
- **一緒に入れた番人**: 各段の後で `tools/check_tree_unchanged.py` が
  「試験がソースを書き換えたまま戻していないか」を追跡ファイルの内容で見る。
  §4-40 の罠 (打ち切りで変異が残り `git add -A` が拾う) を機械で止める。
  1 段目で引っかかったら、その試験を 2 段目へ移す。
- **全並列にはできない**。変異試験が実物を書き換える作りのままだと、同時に走ると
  互いを壊し合い**壊れ方が再現しない**。全並列にするには各試験が写しの上で
  変異する作りに変える必要がある (別作業)。
- **追記 (2026-09-26) — 3 段に分けた**: 列が 94 本に増えて `make check` は約 10 分
  (make all 35 秒 + check-par 497 秒 + check-mut 103 秒、a61072e で実測) に戻っていた。
  重さの中心は並列段の中の**変異試験** (上位 6 本で 909 秒)。変異は「その試験が見ている
  ソースを変えたとき」にしか意味が無いので、変異の有無を `MUTATE` で切り替え
  (recipe は `$(MUT)` / `$(MUTS)`)、`check-fast` (変異なし、**約 40 秒**) と
  `check-changed` (変えたファイルに関係する検査だけ変異、対応表 `tools/check_map.yaml`、
  漏れは `check-map` が静的に照合) を足した。`check` は全部変異込みのまま (**約 9 分 (529 秒)**)。
  使い分けは [`08_build.md`](08_build.md#検査の3段) §8-4。
- **追記 (2026-09-26 夕) — 全並列になった**: 2 段目 `check-mut` (14 本、`-j1`) を写しの木へ移して消した。
  `make check` は **238 → 129 秒**。あわせて `check-changed` の「表に無い → 全部変異」を減らした
  (`-I` の探索先でヘッダを解決、`notest:`)。
- **同じ測定で見つかった 2 つの無駄と偶発**:
  - `check-pcm-cs4231-host` の変異 30 本のうち 4 本が**待ちの切れない無限ループ**になり、
    ケースごとの上限 60 秒を満了していた (計 241 秒)。正常なケースは 1 本 0.01 秒未満なので
    変異の上限を 3 秒にし、**最初に落ちたケースで打ち切る** (RED は 1 件で決まる) ようにした
    → 248 秒が約 8 秒。時間切れは RED に数える。
  - `check-multiapp-model-host` が**並列段の負荷で subprocess の timeout=10 を満了して偶然に落ちた**
    (単独 1.05 秒)。試験バイナリの実行に 10 / 20 秒の上限を付けていた 12 本を 60 秒に広げた。
    **並列段の上限は単独の実測ではなく、16 本同時の負荷を見込んで決める。**

### 4-42. GUI アプリの窓が静かに出ない — 共有ライブラリが古い (2026-09-17)

- **現象**: `edit_gui` を Start → Run... で起こしても**窓が出ない**。例外も中断も 0 件
  (`fault_generation` / `fault_kill_count` / `ring3_abort_count` すべて 0)。
  増えたのは `launch_orphan_count` だけ。
- **原因**: **共有ライブラリが古いまま**だった。ジャンプ表を 111 → 118 本に増やしたのに、
  ゲストの `/sys/lib/libos32gui.shlib` は 113,752 バイト (手元は 125,384 バイト)。
  `bind()` が本数の不一致で `sys_exit` していた。**アプリは起きてすぐ死ぬので何も出ない。**
- **なぜ起きたか**: **`hsync` は既定で `/sys` を外す** (稼働中のシェルと共有ライブラリのため)。
  `make deploy` → `hsync` の普段の手順では**共有ライブラリが永遠に古いまま**。
- **対策**: **GUI アプリや `libos32gui` を入れ替えたら `hsync sys` + リセット。**
  共有ライブラリは起動時に読まれるので、同期だけでは切り替わらない (`hsync sys` 自身が
  そう案内する)。**静かに窓が出ないだけなので気づきにくい。**
- **見分け方**: 窓が出ないのに例外が 0 件なら、まずゲストとホストで
  `ls -l /sys/lib/libos32gui.shlib` の大きさを突き合わせる。

### 4-43. `tools/gui_gate.py` の `key(text=)` は**英大文字を送れない** (2026-09-17、未解決)

- **現象**: `key(text="ABC")` を送るとゲストには `abc` が届く。`_` は通るのに英大文字だけ落ちる。
- **切り分け済み**: **CUI のシェルでも同じ**なので、アプリの問題ではなく**注入道具の側**。
- **影響**: 大文字や特殊キーが要る経路 (**FEP の起動、日本語入力、クリップボード、印刷**) を
  自動で確かめられない。2026-09-17 の `TASK_EDIT_GUI` の受入 E2〜E7 が
  **これで止まった**。
- **性格**: 実機計画の「ホスト側にシリアルの実装が無い」と同じで、**アプリではなく検証の側の穴**。

### 4-44. 自分で移したフォーカスがアプリに返らない — **カーソルキーだけが死ぬ** (2026-09-18、穴 H13)

- **現象**: `edit_gui` の編集面で**カーソルキーが効かない**。30 行のファイルで 16 行目から下へ送れない。
  **文字は入る** (`ZZZ` が本文に届く)。ファイル選択の一覧では同じ `DOWN` が効く。
- **最初の見立ては外れた**: 「`WK_TEXTAREA` のキー処理がカーソルキーを扱っていない」と読んだが、
  `app.rs` の `GUI_EV_KEY` は**ウィジェット側の処理と関係なくアプリの `on_key` を必ず呼ぶ**。
  キーはアプリに届いていた。**受け手を疑う前に、配送の経路を 1 本ずつ読むこと。**
- **本当の原因**: `widget::set_focus()` が**自分で作った `WEV_FOCUS` を捨てていた**。
  入力から合成したフォーカス移動 (TAB / クリック) は `WidgetOut` でループへ返って配られるが、
  **アプリが自分で呼んだぶんは返す先が無く、ローカル変数ごと消えていた。**
- **なぜカーソルキーだけか**: アプリはフォーカスを `on_widget_focus` だけで追っていた
  (`widget::focused()` は窓スロットの添字を取り、アプリが持つ WindowId から引けない)。
  ボタンを押す → `WEV_FOCUS`(ボタン) が届いて `focus_area = false` → `on_click` が
  `set_focus(編集面)` で戻すが**通知が来ない** → `focus_area` は false のまま。
  文字は `GUI_EV_TEXT` → フォーカス中のウィジェット経由で入り `focus_area` を見ないので、
  **アプリ側の門を通る編集キーだけが落ちる。**
- **だから「起動直後は効くのに、一度ボタンを押すと死ぬ」**。前回の調査は `Open` で
  ファイルを選んでから試したので、最初から死んでいるように見えた。
- **対策**: `set_focus` は通知を溜め (`UiState.pending_focus`)、ループが 1 件ずつ配る
  (`app.rs` の `drain_pending_focus`)。窓を組む間のぶんと、ハンドラの中で移したぶんの
  2 か所で配り、往復には上限 (`FOCUS_DRAIN_MAX`) を置く。ジャンプ表も契約も変えていない。
- **教訓**: **同じ状態を変える口が 2 つあって、片方だけが通知を出すのは穴になる。**
  通知のある API は、入力経由と API 経由の**両方の入口**を見ること。
- **試験**: 実行時は GUI サーバが要るのでホストでは踏めない。`tools/tests/test_edit_doc.py` の
  静的検査に**配線の規則**として足した (textbox が textcore を通っている検査と同じ形)。
  通知を捨てる版に戻すと STATIC FAIL になることを実測した。

### 4-45. Host Services (クリップボード・印刷) は**ホスト側の常駐が要る** (2026-09-18)

- **現象**: `edit_gui` の Copy / Paste が状態行に `copy failed, error -100` と出る。
- **-100 = `HOST_ELINK`** (`userland/lib/host/libos32host.h`) =「STALE / リンク未確立」。
- **原因**: Host Services はホスト側の常駐 `tools/host_agent.py` (raw Ethernet、EtherType 0x88B5、
  `NP2NETSOCK=127.0.0.1:8026`) と話す。**その常駐が動いていなかった。**
- **対策**: 常駐を 1 本起こすだけ (**承認不要**)。起動した瞬間に 3-way HELLO が通り、
  E4 / E5 がその場で合格した。

  ```bash
  python3 tools/host_agent.py --listen 127.0.0.1:8026 \
    --state-dir <dir> --print-dir <dir> --spool-dir <dir> --clip file:<path>
  ```
- **やりかけた誤り (ここが本題)**: 最初「`np21x64w.ini` に LAN のキーが無いから
  **[D2] の承認が要る**」と判断し、ユーザーに承認を求めかけた。**ini を読んで環境を判断したのが誤り。**
  `/api/net` に聞いたら `enabled:true` `backend:"socket"` `spec:"connect 127.0.0.1:8026"` で、
  **`tx_dropped` が 158,823**。ゲストはずっと送っていて、受け手が居なかっただけだった。
  (`np21x64w.ini` には実際にキーが無い。設定はその ini 以外から来ている。)
- **規則**: **設定は設定ファイルではなく、生きている口に聞く。** エミュレータの状態は
  `/api/net` `/api/status` が正典で、ini は「そこに書いてあること」しか証明しない。
  **承認 ([D2]) を求める前に、本当にその変更が要るかを実測で確かめる。**
- **撮る間合いの罠**: 最初の `open` は**最大 3 秒**待つ (`HOST_OPEN_STALE_TICKS`)。その間 UI は止まる。
  クリックの 2 秒後に撮ったスクリーンショットには結果がまだ出ておらず、次のクリックの後に
  前回の結果が出る。**「状態行が 1 手遅れる不具合」と読み違えかけた。**
  落ち着いてから撮り直すこと。

### 4-46. 保存の試験を「行の中身」で見ると往復の増殖を見逃す (2026-09-18、穴 H16)

- **現象**: `edit_gui` で 275 バイトのファイルを開き、**編集せずに保存すると 1 バイト増える**。
  開き直して保存するとまた 1 バイト。**単調に増える。**
- **原因**: `Doc::load` が**末尾の改行を空行 1 本として数えていた**。`save_file` は行ごとに
  改行を書くので、その空行にも改行が付く。`"L1\nL2\n"` → 3 行 → `"L1\nL2\n\n"`。
- **試験が捕まえられなかった理由 (ここが本題)**:
  - 往復の試験 `load_file_round_trips` は **`d.line(0)` / `d.line(1)` しか見ていなかった**。
    行数も、保存し直したバイト列も見ていない。
  - 保存の試験に至っては **`"ab\nあ\n"` → `"ab\nあ\n\n"` を期待値として書いており、
    不具合を固定していた**。
- **規則**: **保存や書き出しの試験は、入れたものと出てきたものを**バイト列**で突き合わせる。**
  「行の中身が合っている」は往復の証明にならない。増減は端 (先頭・末尾・終端子) に出る。
- **併せて**: 何度往復しても変わらないこと (冪等) を**繰り返して**見る試験を 1 本置く。
  1 回の往復だけでは単調増加を見逃す。
- **見つけ方**: 人が使う経路を通した (開く → 保存する)。**機械の試験は行を見ていて、
  人の目はバイト数を見た。** 角度の違う 2 つを両方通すこと。

### 4-47. 1.44MB フロッピーで踏んだ 4 つ (2026-09-18、票 TASK_FD144)

**セクタ長が変わると、効く場所が「読む所」だけではない。**

1. **IPL が 512 バイトに縮む。** NP21/W の `boot_fd1()` は rpm=1 のとき
   `1FE0:0000` へ **512 バイトだけ**読む (rpm=0 は `1FC0:0000` へ 1024B)。
   1024B 前提のブートセクタは**後半が読まれないまま実行される**。
   → nasm のパディングを `times (SECT_SZ - 2) - ($ - $$)` にして**溢れたら止める**。
2. **LBA→CHS のシフト演算が使えない。** spt=18 は 2 の冪ではない。
   `and al,07h` / `shr ax,3` を実 `div` に書き換える。
   **2HD 側も同じ `div` に寄せた** — 分岐を残すと片方だけ直して静かにずれる
   (実コードは両方 381 バイトでサイズ中立だった)。
3. **一括で読むバッファがスタックに当たる。** IPL はルートディレクトリを
   `0x6000` へ一括で読む。1.44MB 標準の 224 エントリだと 14 セクタ × 512B = 7168B で
   **終端が `0x7C00` = SP にちょうど当たる**。192 エントリに落として回避した。
4. **FAT の読み込みが決め打ちだった。** 2 セクタ固定のところ 1.44MB の FAT は 9 セクタ。
   **足りないぶんは黙って化ける** (後ろのクラスタを引いたときだけ壊れる)。

**併せて — マクロで固定したジオメトリは必ず取り残される。**
`fs/fatfs/diskio.c` が `FDC_SPT` / `FDC_SECTOR_SIZE` を直に読んでいたため、
`fdc_read_sector` をジオメトリ駆動にしても LBA→CHS が 2HD のままだった。
**ドライバに抽象があるなら、呼ぶ側もそこを通す。**

### 4-48. D88 の 1.44MB は `rpm_flg` を知らないと丸一日溶ける (2026-09-18)

- **1.44MB の D88 は `fd_type = 0x21` かつ全セクタの `rpm_flg = 1`** が要る
  (`np21w-src/src/fdd/d88head.h`、`diskimage/fd/fdd_d88.c` の `rpmcheck()`、
  `x11/gtk2/dialog_newdisk.c` の `{ "1.44", (DISKTYPE_2HD<<4)+1 }`)。
  合わないと **`rpmcheck()` が読みを静かに弾く**。
- `tools/mkd88.py` は `header[0x1B] = 0x20` / `rpm_flg = 0` しか書かない。
- **リポジトリの `docs/D88_FORMAT_SPEC.md` はこれに触れていない** (`0x20=2HD / 0x30=1D`
  としか書いておらず、NP21/W の実装と食い違う)。**仕様書を信じて詰まる形。**
- **逃げ道**: D88 をやめて **1,474,560 バイトちょうどの生イメージ**にする。
  NP21/W は `diskimage/fd/fdd_xdf.c` の表でサイズ一致を見て rpm=1 を自動で立てる。
  実機へ D88 で渡すときだけ `mkd88.py` を直すことになる。

### 4-49. シリアル 38400bps は**実機では出ていなかった** (2026-09-18)

- **きっかけ**: ユーザーの「速度が 9600bps ではなかったですか？」という問い。
  `include/config.h` は **38400** だったので「38400 です」と答えかけたが、**記憶の側が正しかった。**
- **速度は 8253 TCU カウンタ#2 の分周 (整数) で決まる。** 割り切れなければ必ずずれる。
  `serial_init` は `count = (u16)(clk / 16 / baud)` を**切り捨てていた**。

  | クロック | clk/16 | 9600 | 19200 | 38400 |
  |---|---|---|---|---|
  | **1.9968MHz** (8MHz系) | 124800 | count 13 **ちょうど** | 6.5 → **20800bps (+8.3%)** | 3.25 → **41600bps (+8.3%)** |
  | 2.4576MHz (5/10MHz系) | 153600 | 16 | 8 | 4 (すべてちょうど) |

  **+8.3% は UART の許容 (±3% 程度) を超える。** 実機では通らない。
  **9600 は両方のクロックでちょうど出る唯一の標準速度。**
- **なぜ気づかなかったか**: `serial_init` は **1996800Hz を決め打ち**し
  (`TIMER_CLK_2458` は定義だけで未使用)、**NP21/W は通信速度をモデル化していない**
  (ソースにクロック定数も `0434h` も無い)。**エミュレータでは何を指定しても通る。**
  「NP21/W で動作確認済み」というコメントは、この項目については何も保証していなかった。
- **規則**: **エミュレータが模擬していない量は「確認済み」と書かない。**
  何を模擬していないかはエミュレータのソースで確かめられる。
- **対策**: `0000:0501h` bit 7 でクロックを判定 (1=8MHz系→1.9968 / 0=5/10MHz系→2.4576)、
  分周比が割り切れなければ **`[ser] WARN ...` で実効値を出す**、既定を **9600** に戻した。
  `serial` コマンドは速度を引数で取れるようにし、「initialized」と言い切るのをやめた
  (カーネルの行と矛盾するため)。

### 4-50. 資料どうしが食い違ったときの決着のつけ方 (2026-09-18)

シリアルのクロックで、**同じ UNDOCUMENTED の中で 2 か所が逆**だった。

| 出典 | 8MHz系 | 5/10MHz系 |
|---|---|---|
| `io_tcu.md` の表 | 2.4576MHz | 1.9968MHz |
| `memsys.md` の 0000:0501h | **1.9968MHz** | **2.4576MHz** |

**`memsys.md` を採った。根拠は自己整合性**: 同じ項目に併記された SCLK1 の表が
「値1 → 7.9872MHz / 値0 → 9.8304MHz」で、**4 分周すると 1.9968 / 2.4576 になる**。
`io_tcu.md` の表は 2 行が入れ替わった転記ミス。FreeBSD の `pc98_ttspeedtab`
(8MHz系 = 1996800) も `memsys.md` 側と一致する。

**教訓: 表が食い違ったら、別の量との整合 (ここでは分周関係) で決める。**
どちらが新しいか・どちらが詳しいかでは決まらない。

**決着しなかったもの**: `I/O 0434h` bit6 (入力クロック 4 分周) の**極性は機種依存で、
しかも 1999 年の読者指摘として逆の可能性が併記されている**。自己整合で決められない。
だから**自動では触らない** — `serial_set_div4()` を用意して、実機で確かめてから
明示的に呼ぶ形にした (`drivers/serial.h` の注記)。bit0 は「ポート切り離し」なので
read-modify-write で保つ。

### 4-51. FDC の IRQ 待ち 200ms は**エミュレータに合わせた値**で、実機のシークに足りなかった (2026-09-22)

- **症状**: 実機 PC-9821Ra266 で FD (1.2MB / 1.44MB とも) から起動すると `MOUNT...` で **`root panic`**。
  HDD は未フォーマットだったので「ext2 が無いから」と見えたが、**FD 起動の経路に ext2 の依存は無い**。
  `root panic` は fd0 の FAT マウント失敗で、その下はカーネル自前の FDC ドライバ (`drivers/fdc.c`)。
  IPL とローダは BIOS で読むので、**FDC ドライバが実機で走ったのはこれが最初**だった。
- **原因**: `drivers/fdc.h` の `FDC_IRQ_TIMEOUT_TICKS = 20` (200ms)。コメントに「NP21/W で IRQ 未到達時の
  遅延を抑制」とあるとおりエミュレータ基準。実機は SPECIFY の SRT=8ms で **RECALIBRATE / SEEK が最大
  77〜80 トラック × 8ms ≒ 620〜640ms**、READ DATA も 1 回転 (167〜200ms) + ヘッドロードで 200ms 前後。
  ローダがカーネルを読んだ直後のヘッドはシリンダ 20〜40 付近なので `fdc_init()` の recalibrate が落ちる。
  **NP21/W はシーク時間を模擬していない** (`src/io/fdc.c` の `fdc_intwait` は 512 サイクル後に割り込み)。
- **連鎖**: タイムアウトの後に SENSE INTERRUPT で回収しないまま次の SEEK を出すと、µPD765 の INT 線が
  上がりっぱなしになり PIC (エッジ) に次のエッジが来ない → 以後の待ちが全部落ちる → 3 回リトライしても
  読めず `root panic`。媒体に依らないので 1.2MB / 1.44MB の両方で同じになる。
- **対策**: 票 [`archive/realhw_v21/TASK_FDC_REALHW.md`](archive/realhw_v21/TASK_FDC_REALHW.md)。時間定数を機構から導き
  (シーク 1.5s / R/W 1s)、SEEK の前に SIS で排水、タイムアウト後は SIS で「取りこぼし」を判定、
  リトライの間に FDC リセット + recalibrate、最終失敗だけ `[fdc]` の 1 行を画面に出す。
- **第 2 の原因 (レビューで発見)**: `I/O 0439h bit2` は「1MB 以上のアドレスへの DMA アクセス禁止」で、
  **ノーマルモードの起動時設定は 1** (`docs/hw/undocumented/io_dma.md`)。`dma_buffer` はカーネル BSS
  (0x156000、1MB 超) にあるので、実機では READ DATA が正常終了してもデータがバッファに届かない。
  NP21/W は `necio_o0439` が値を保存するだけで DMA が見ない — **これもエミュレータでは出ない**。
  `fdc_init()` で RMW して bit2 を落とす (bit7 はプリンタ I/F の選択なので他ビットは保つ)。
- **教訓**: §4-49 と同じ型 — **エミュレータが模擬していない量 (今回はシーク時間・回転待ち・DMA の
  1MB 制限) を「確認済み」に しない**。時間の上限は「エミュレータで困らない値」ではなく**機構の最悪値から導いてコメントに根拠を書く**。
  「NP21/W で〜を抑制」という理由の定数は、実機の前に必ず疑う (`grep -rn NP21 drivers/*.h`)。
- **続き (2026-09-24、速度)**: 通るようになった後、1 セクタごとに SEEK + 20ms + READ DATA で**ほぼ 1 回転ずつ**待ち、
  フォント (188KB) で 1 分以上止まった (これも回転待ちを模擬しない NP21/W では出ない)。同じシリンダのシークを省き、
  要求セクタから EOT までを 1 回で読んで持つ形にした (`drivers/fdc_track.c`、FatFs の窓は count=1 で来るので束ねるだけでは効かない)。
  まとめ読みの時間上限も同じく最悪値 `2 × (2 回転 + ceil(count/spt) 回転 + HLT)` から導く。票 [`TASK_FDC_REALHW.md`](archive/realhw_v21/TASK_FDC_REALHW.md) の v3 の節。
  **ただし 1 本の先読みは効かなかった** — FatFs (`FF_FS_TINY=1`) は FAT とデータで窓を取り合い、クラスタを越えるたびに
  シリンダ 0 の FAT を読み直すので、FAT とデータのトラックが追い出し合う (1KB ごとにシーク 2 回)。2 本にした。
  「読みを速くしたのに速くならない」ときは、**上の層の読み方 (どのセクタがどの順で来るか) を先に数える**。
  2 本でもまだ当たらなかった (実測 seek=751) — VFS (`fatfs_vfs_read_stream`) が 1KB ごとにファイルを開き直し、
  ディレクトリ 3 本 + FAT + データを巡回していた。8 セクタの LRU を足して解消。読み方は実物の FD イメージと
  実物の `ff.c` で再現して数える (`tools/tests/test_fdc_track.py` の `font_replay`。6541ef1 の形で実測とほぼ同じ 752 が出る)。

### 4-52. `kprintf` の行は**実機でもエミュレータでも最初から見えていなかった** — 属性の流儀違い (2026-09-22)

- **症状**: 実機の写真に `[fdc] …` `[ide] …` の診断行が 1 行も無い。エミュレータの `/api/tvram` には載っている。
- **原因**: `kprintf(0x07, …)` などの属性は PC/AT (CGA) 流で、`kernel/console.c` の `tvram_putchar_at` が
  PC-98 の属性 VRAM へそのまま書く。PC-98 の属性は **bit0 = 表示** (`TATTR_VISIBLE`)、bit5/6/7 = B/R/G。
  0x0A / 0x0C / 0x0E は bit0=0 で非表示、0x07 は黒の反転。0x07 の呼び出しは 73 か所。
  黄色 (0xC1) の `[selftest]` だけが見えていたのは、そこだけ PC-98 流だったから。
- **なぜ気づかなかったか**: `/api/tvram` は**文字コード**を返すので、テスターも PM も文字の存在で判定していた。
  `/api/screenshot` で見た目を確かめると、エミュレータでも同じ行が黒かった。
- **対策**: `lib/kprintf_attr.c` の `kprintf_attr_to_pc98()` を kprintf の入口で 1 回適用 (PC-98 流は素通し)。
  kselftest とホスト試験 (`check-kprintf-attr-host`) で 256 通りの「必ず見える」を固定。
- **教訓**: 「画面に出ている」は**文字コードではなく見た目**で確かめる。tvram を読む検証は属性を見ない。
  実機に持ち込む前に 1 度は `/api/screenshot` を人が見る。

### 4-53. FDC は **FRY (0x94 bit6) を立てないと実機で Not Ready** (2026-09-22)

- `docs/hw/undocumented/io_fdd.md` 191 行 (要旨): FRY は RDY を強制的に有効にするビットで、ドライブの RDY と
  OR を取った結果が FDC に入る。READY 線を出さないドライブでは、FRY 無しだと µPD765A が全コマンドを
  ST0 の NR (bit3) で即終了する。
- 旧コードは RECALIBRATE の結果を `SE` だけで見ていたので NR でも「成功」し、READ で落ちて root panic。
  §4-51 の修正で NR を即失敗にしたら、今度は `fdc_init` の `ER` として同じ原因が正しく見えた。
- NP21/W は `ctrlreg & 0x40` を RECALIBRATE と SENSE DEVICE STATUS でしか見ず、媒体入りのドライブは
  `fdd_diskready` で Ready 扱い。**FRY 無しでもエミュレータでは全部通る** — §4-49・§4-51 と同じ型。
- **対策**: 0x94 の動作時の書き込みを `CTRL_FRY | CTRL_MTON | CTRL_DMAE` に。

### 4-54. PIT の分周が**クロック決め打ち**で、2.4576MHz 系の tick は 8.125ms だった (2026-09-23)

- **症状**: 見た目には何も起きない。実機 PC-9821Ra266 で `tick_count` を使う待ち・番犬
  (FDC のタイムアウト、シリアルの TX 予算)・CPU 校正が**一律 23% 速い**。
  100Hz を頼んだはずの tick が実際は **123Hz (8.125ms)**。
- **原因**: `kernel/idt.c` の `pit_init()` が `PIT_CLOCK` = 1,996,800Hz **決め打ち**で
  `divisor = PIT_CLOCK / hz` を計算していた。PC-98 の 8253 TCU に入るクロックは機種で
  2 通り (`0000:0501h` bit7: 1 = 8MHz 系 → 1.9968MHz / 0 = 5･10MHz 系 → 2.4576MHz)。
  Ra266 は後者 (`[ser] 9600bps (clk 2457600Hz, count 16)` と出る) なので、19968 を積むと
  19968 / 2457600 = 8.125ms になる。正しくは 24576。
- **なぜ気づかなかったか**: **NP21/W は 1.9968MHz 設定**なので、エミュレータでは
  決め打ちの値がたまたま正解で、100Hz ちょうどが出ていた。§4-49 (シリアルの分周)・
  §4-51 (FDC のシーク時間)・§4-53 (FRY) と同じ型 — **エミュレータが模擬しない機種差**。
  クロックの判定自体は `drivers/serial.c` の `serial_detect_clock()` に既にあったが、
  呼ばれるのは `kernel.c:228` = `pit_init` (194 行) の **34 行あと**で、しかも
  CR3 が master と一致しないと何もしない作りだった (シリアルしか見ていなかった)。
- **対策**: 判定を `kernel/sysclk.c` (`sysclk_detect()` / `sysclk_hz()` /
  `sysclk_is_8mhz()`) に切り出し、**`pit_init` より前**に `kernel.c` から 1 回だけ呼ぶ
  (`paging_init` より前なので PG=0、低位物理がそのまま見える → CR3 検査は不要)。
  `pit_init()` は `reload = sysclk_hz() / hz` で割り、積んだ内容を
  `struct pit_setup { clk_hz, hz, reload, period_us, mode, valid }` に残して
  `pit_get_setup()` で読めるようにした。算数は `kernel/pit_math.c` に分けてホストで試験する。
  `serial_detect_clock()` は廃止し、シリアルも保存値を見る。
  起動画面の 2 行目に `PIT 1.9968M` / `PIT 2.4576M` を出す (実機で目視できる)。
- **どう確かめるか**:
  (1) `make check-pit-clock-host` — 両クロックのリロード値 (19968 / 24576) と周期
      (どちらも 10000µs)、100Hz 以外と未知クロックの拒否。記録は
      [`tools/tests/pit_clock_tdd.md`](../tools/tests/pit_clock_tdd.md)。
  (2) 起動時の `kselftest` の `test_pit_setup()` — `valid` / `reload == sysclk_hz() / 100`
      / `period_us == 10000` / `mode == PIT_MODE_TIMER0` / `sysclk_detected()`。
      `kernel.map` の `kselftest_fail` を読む (§2)。
  (3) **実機での実時間**: ホストの単調時計で 60 秒を測りながら `tick_count` の差を取る
      (シリアル経由で前後に読む)。10ms ちょうどなら 6000±数十。直す前の機械では
      約 7385 (123Hz) になる。**この照合だけは NP21/W では意味がない** (通信速度も
      クロックも模擬していない)。
- **教訓**: 「エミュレータで 100Hz が出ている」は**機種差を確かめたことにならない**。
  機種で 2 通りある値を定数で持ったら、**判定する側と使う側の順序**まで見る
  (判定器はあったのに、使う側より後に呼ばれていた)。

### 4-55. `io_wait()` (0x5F 書き) を万単位で連打すると、以後の FD の読みが古いデータを返す (NP21/W、2026-09-23)

- **症状**: kselftest のストーム試験で tick の変わり目を `while (tick_count == t) io_wait();` で待つようにしたら、
  その後の `/sys/unicode.bin` と `/sys/shell.bin` が **NOTFOUND (-2)** になり `FATAL: shell.bin load failed`。FDC の
  エラー行は出ない (= FDC は「成功」を返し、ディレクトリのデータが古いまま)。待ちを `nop` にすると 187/187 で起動する。
- **切り分け**: 2 段の bisect (修正コミット前の木 → 待ち呼び出しだけ除去 → `io_wait` を `nop` に) で `io_wait()` の連打だけが要因。
  `io_wait()` は `outp(0x5F, 0)` (PC-98 の CPU ウェイトポート)。fm.c の `wait_ms` も同じ関数を使うが回数が桁違いに少ない。
- **原因**: 未特定。NP21/W 側の 0x5F 書きの模擬 (クロック消費) と FDC/DMA の模擬の相互作用と見ている。実機での再現は未確認。
- **教訓**: 待ちループに `io_wait()` を入れない (待ちたいだけなら `nop`、寝たいなら `hlt`)。「FS が NOTFOUND なのに I/O エラーが無い」は
  **データが届いていない**兆候で、直前に走ったコードの I/O ポート操作を疑う。

### 4-56. ISR が書く状態を foreground で待つループは `volatile` で読む (PCM の close が毎回 IO になった、2026-09-23)

- **症状**: `pcm_close` が 5 秒のストリームでも 1 frame のストリームでも `OS32_ERR_IO` → FAULTED (sticky)。NP21/W の `/api/sound` は
  PEN=0 / IEN=0 / PI=0 で装置は止まっているのに、driver の証拠読みは 1 度も走らず (診断 `pcm_diag_evidence` = 0、`stop_calls` = 2)。
- **原因**: `while (g_pcm.state != STOP_DONE && != FAULTED) { if (past(dl)) return 0; }` の `g_pcm.state` を素の読みで回していた。
  ループの中に state を書く呼び出しが無いので GCC (-O2) が読みをループの外へ持ち上げ、ISR (tick / IRQ10) が STOP_DONE にしても
  foreground は期限まで回って失敗を返した。`tick_count` は `volatile` だったので期限だけは進んだ。
- **教訓**: ISR が書く変数を foreground が待つときは **`volatile` 経由で読む** (`volatile u8 *st = &g_pcm.state`) か、構造体の
  その欄を `volatile` にする。`inp()` などの asm volatile が同じループにあれば持ち上げは起きないが、それに頼らない。
  切り分けは「どこで失敗したか」を記録する診断 (fault site / 最後の証拠 / 呼び出し回数) を kernel.map から読むのが速かった —
  `/api/mem` は `/api/cmd` の実行中は返らない (HTTP サーバは 1 本) ので、実行中の状態遷移は取れない。

### 4-57. NP21/W はキーボード 8251 の DTR (RTY#)・RTS (RDY#)・RxE を見ない — 実機の定常値は BIOS と同じ 0x16 (2026-09-23)

> **実装レビュー (2026-09-23)**: bda95fa + f924275 は Codex / Opus のラリー 2 で両者 Approve。レビューで出た KAPI データ欄のずれ (旧バイナリの malloc が ENOMEM) は既存の構造問題として別票 [`archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md`](archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md)。実機での打鍵は未確認。

- **症状**: 実機 PC-9821Ra266 で本体キーボードの打鍵が**一切**効かない。シリアル (rshell) は動く。NP21/W では効く。
- **原因 (本命)**: `kbd_init()` が 0043h にコマンド語 **0x14** (ER + RxE) を書いていた。bit1 (DTR) = 0 は
  **RTY# を LOW にする = キーボードへの再送要求** (`docs/hw/undocumented/io_kb.md` の 0043h [WRITE] の要旨:
  bit1 は RTY# の信号レベル (1 = HIGH、0 = LOW)。通常は HIGH で、LOW はキーボードへのデータ再送要求)。
  BIOS の定常値は **0x16** (NP21/W の BIOS `src/bios/bios09.c` も 0x3A → 0x32 → 0x16)。コメントは「FreeBSD は空関数
  → BIOS 初期化済みを前提」と書きながら、その前提を自分で壊していた。Bible の「D1: リトライ 1:有効」という書き方は
  極性の根拠にしない (信号レベルで書いてある io_kb.md を採る、§4-50)。
- **エミュレータで出ない理由**: NP21/W の `keyboard_o43` (`src/io/serial.c`) は bit3 の立ち下がり (リセット) と
  bit4 (ER) しか見ず、`keybrd.cmd` に保存するだけ。bit1 (DTR) も bit5 (RTS) も bit2 (RxE) も効かない。
  `keyboard_i43` は `status | 0x85` を返し、IRQ1 の前に必ず RxRDY を立てる — §4-49・§4-51・§4-53 と同じ型。
- **対策**: コマンド語を `KBD_CMD_ERRRST_RXE_RTYHIGH` = 0x16 に (kselftest `kbd cmd word is 0x16` /
  `kbd cmd keeps DTR=1` が見張る)。モード語 (0x5E) からのやり直しは**第 2 段**としてコメントに残すだけ。
  同じ変更で IRQ1 ハンドラが **0041h の前に 0043h を読む**: RxRDY = 0 は空 IRQ (0041h を読まない)、
  PE/OE/FE は読み捨て + 0x16 を書き直して解除 (ホスト試験 `make check-kbd-status-host`)。
- **「RTY# LOW で沈黙する」機構そのものは資料では証明できない**。0x16 で直らなかったときのために観測を入れた:
  起動行 `[kbd] st=XX -> YY cmd=16 flushed=N lock=XX` (lock は 2026-09-26 から) と、シェルの **`kbdstat`** (KAPI v62 `kbd_diag`、rshell から読める)。
  `kbd irq=… empty=… err=… flushed=… init=XX->YY cmd=16 st=… code=… now=…` の読み方:
  - `irq=0` かつ `now` の RxRDY (bit1、0x02) = 1 → 8251 は受けている。**PIC / IRQ1 の経路**を疑う (IMR、ICW)。
  - `irq=0` かつ RxRDY = 0 → **キーボードが送っていない** (RTY# / RST# / 電源・コネクタ、次は第 2 段のモード語)。
  - `irq>0` なのに文字が出ない → 8251 までは来ている。`empty` / `err` の割合と `code` を見て、**配送側**
    (cooked リング / GUI モード / rshell の読み口) を疑う。
  - 同じ `code` のまま `irq` が打鍵の数より桁違いに増える → **再送ストーム** (RTY# が LOW のまま等)。
- **教訓**: 「BIOS 初期化済みを前提」にするなら、書くのは BIOS の定常値だけにする。ハンドシェイク線 (DTR/RTS) の
  極性は NP21/W では検証できない — 実機でしか分からない値は、観測の口を同時に入れる。
- **実装レビューの後の修正 (同日、Codex / Opus)**:
  1. **V86 への偽の ESC**: 空 IRQ・エラーで `kbd_irq_handler` が早く戻っても `irq_stub_1` は `V86_REFLECT 1` を
     無条件に通し、ゲストは空の仮想 FIFO から 0x00 (= ESC のメイク) を読んでいた。ハンドラは「反射してよいか」を返し
     (`kbd_status_reflects`: 0041h の実データ = DATA / OVERRUN のときだけ 1)、スタブは 0 なら反射を飛ばす。脱出ホットキーは
     DATA なので従来どおり反射してから畳む。二重の防御として `v86_kbd_in` の空読みは**前回のバイト** (開始時 0xFF) にした。
  2. **OE は正しいバイト**: 8251A の OE は「前のバイトを読む前に次が来た」で、レジスタの新しいバイトは正しい。OE だけなら
     `KBD_ST_OVERRUN` として使い、ER (0x16) で解除し `kbdstat` の `ovr=` (KbdDiag の旧 reserved、u16 飽和) に数える。
     PE / FE は従来どおり読み捨て (`err=`、OE と重なってもこちら)。
  3. **`sh` の要求版**: `userland/sh` は常駐シェルと同じソース (`kbdstat` / `lspci` を含む) なのに app.conf が 55 のままで、
     v61 以前のカーネルでは kbdstat が表の外へ飛ぶ。62 に上げた (kbd_diag を呼ぶのは shell と sh の 2 本だけ)。
- **`kbdstat -w` — 受信 1 バイトごとの行 (KAPI v67 `kbd_diag_log`、2026-09-26)**: `kbdstat` の `code` は最後の 1 件だけ、
  `irq` は EMPTY / ERROR も数え、`now` は 8251 のステータスなので、「どのキーで何が届いたか」の**順序**は分からない。
  `-w` は IRQ1 が 0041h から使うバイトを読むたびに積む 32 件の循環リングを毎 tick 読み、1 行ずつ出す:
  `seq=12 code=F2 break key=72 KANA mods=CAPS|KANA` (`code` は 0041h の生の値、`mods` はそのバイトを処理した**後**の
  修飾、`[OE]` / `[V86]` / `[GUI]` は印)。最初の行 `kbdstat -w: ESC or 30s to stop. start seq=N mods=…` が始めた時点の
  修飾 (カナの初期値)。**seq が飛んだら `LOST seq=a..b (n): cannot judge this span`** — 読み手が 32 件以上遅れて
  上書きされた区間で、そこは判定しない (9600bps では 1 行 ≈ 50ms なので、押しっぱなしの連打で出やすい。それ自体が
  「繰り返している」の印)。EMPTY / ERROR で捨てたバイトは行にならず、終わりの行
  `kbdstat -w: end (ESC|timeout) shown=… lost=… mods=… empty+… err+… ovr+…` に増えた数だけ出る。ESC (本体でも
  rshell のシリアルでも) か 30 秒で終わり、本体の ESC はそれ自身も `key=00 ESC` の make の行として出る。
  用途は票 [`tasks/gui/TASK_KBD_NAV.md`](tasks/gui/TASK_KBD_NAV.md) §3 (カナ / CAPS が「ロックで make、解除で break」か
  「押すたびに make だけ」か): シリアルから `kbdstat -w` を始め、本体のキーだけを触って行を読む。
  ホスト試験は `make check-kbd-dlog-host`。
  最長 30 秒 (+ 出力の分) 走るので、`/api/cmd` や `rshell_serial.py` の待ちは **60 秒以上**にする ([V3]) — 短いと写しが途中で切れ、LOST の判定を欠けた写しでしてしまう。
- **カナ・CAPS は方式 B (2026-09-26)**: NP21/W の `kbdstat -w` でカナ・CAPS が機械式ロック (ロックで make・解除で break) と
  分かり、make で反転・break を捨てるドライバでは外しても KANA のままだった。make で ON・break で OFF に直し、起動時は
  BIOS の 0000:053Ah から引き継ぐ (起動行 `lock=XX`)。実機の確認は CHECKLIST_2026-09-26 の手順 10 — 根拠と残りは票 §2。

### 4-58. OS32 の ext2 が読めることは**正しい ext2 である証拠にならない** — cdinst の NHD に名前の無いディレクトリ (2026-09-23)

- **症状**: NP21/W 上の `cdinst` で作った NHD を Linux で ro マウントするとルートの stat が I/O エラー、`e2fsck -n -f` は
  `Directory inode 2, block #0, offset 132: directory corrupted`。ルートに **inode 23・rec_len 8・name_len 0・type 2**
  の項目があった。OS32 自身の読み手 (`ext2_list_dir`) は name_len 0 を読み飛ばすので、NP21/W 上では誰も気づかなかった。
- **原因 (確定)**: パッケージ展開 (`userland/lib/rt/pkg.c` の `ensure_parent_dirs`) はファイルごとにパスの各 `/` で
  `sys_mkdir` する — cdinst が付け替えた `/hd0/boot/vmkernel.lz4` なら最初に **`mkdir("/hd0")`**。VFS はこれを
  `/hd0` のマウントへ相対パス `"/"` で渡し、`ext2_split_path` が親 `"/"` + 名前 `""` に分け、`ext2_mkdir` は空の名前を
  断らずに作った (`find_entry("")` は一致する項目が無いので NOTFOUND → 作成へ)。2 回目以降は作った項目自身に一致して
  EXIST なので 1 個だけ。inode 23 は cdinst の mkdir (11〜22) の直後、MINIMAL.PKG の最初のファイル vmkernel.lz4 (24) の
  直前で、ホストで同じ並びを再現した像が**同じ `offset 132`** で e2fsck に落ちる (`make check-ext2-empty-name-host`)。
  同じ形は `mkdir /`、マウント点で `mkdir .`、`tar -x` の `mkdir_parents` でも作れた。
- **対策**: VFS はマウント点そのもの (相対 `"/"`) を FS へ渡さない — mkdir は **EXIST** (POSIX の `mkdir("/")`)、
  rmdir / rename は INVAL、write / unlink は ISDIR。ext2 は `ext2_name_check` で空・`.`・`..`・255 超 (name_len は u8、
  256 は 0 に回り込む)・`/` 入りの名前を**何も書く前に** INVAL で断る (mkdir / create / rename / rmdir / unlink /
  add_entry)。`find_entry` は長さ 0 と 255 超を探さずに NOTFOUND — 既存の NHD に残っている名前の無い項目を
  `""` で掴んで、ディレクトリ inode にファイルの中身を書かないため。末尾 `/` の `mkdir /a/b/` は `vfs_resolve_path` が
  畳むので従来どおり作れる。`tools/mkpkg.py` はゲストパスの形 (先頭 `/`、空・`.`・`..` の要素なし) を検査する。
- **既存の NHD**: `e2fsck -fy` で直る (写しで確認): offset 132 を salvage、inode 23 (空) は `/lost+found/#23` へ、
  `lost+found` (OS32 のフォーマッタは作らない) と UUID が新しくでき、`db` (inode 43、`fep.db`) は残る。
- **教訓**: 「OS32 で読める・動く」は自前の読み手が寛容なだけかもしれない。**FS を作る / 書く経路の受入には
  本物の `e2fsck -fn` を当てる** (clean = 終了コード 0)。ホスト試験で像を書き出して当てる形は `test_b8_open.py` /
  `test_ext2_empty_name.py`。インストーラの受入 (TASK_HDD_INSTALL) も、できた NHD をホストで `e2fsck -fn` する。

### 4-59. 0035h (8255 ポート C) は全体で書かない — rshell 中に実機のビープが鳴り続けた (2026-09-24)

- **症状**: 実機 PC-9821Ra266 で rshell (シリアル) で会話している間、ビープが鳴り続ける。NP21/W では鳴らない。
- **原因 (2 経路、どちらも BUZ = 0 を書く)**:
  1. `drivers/serial.c` が RS-232C の割り込み許可を **0035h へ全体で**書いていた — 初期化で `0x00` → `0x01`、
     ISR では**受信のたびに** `0x00` → `0x01` (IRQ4 のエッジを作り直すため)。0035h はポート C で、bit0-2 の
     RXRE / TXEE / TXRE と同じバイトに **bit3 = BUZ (0 = 鳴動)**、bit4 = MCHKEN、bit5 = SHUT1、bit6 = PSTBM、
     bit7 = SHUT0 がいる (`docs/hw/undocumented/io_syste.md` の I/O 0035h)。1 バイト受けるごとに BUZ・SHUT0・
     SHUT1 が 0 になり、ブザーが鳴る。SHUT0 = 0 のままリセットがかかると ITF は初期化せず 0000:0404h から続行する。
  2. `include/pc98.h` の `BSR_BUZ_OFF` / `BSR_BUZ_ON` が **Bible §2-2 の向き (06h = OFF)** で名前を付けていた。
     UNDOCUMENTED (I/O 0037h) は **06h = 鳴動、07h = 停止**。rshell は応答の始めと終わりに `buz_off()` を呼ぶので、
     実機ではコマンドのたびに**鳴らしていた**。serial.c が書いていた `BSR_BUZ_ON` (07h) は値としては停止で正しく、
     添えてあった「NP21/W では極性逆」は読み違い。
- **資料の食い違い**: BUZ の極性は Bible と UNDOCUMENTED で逆。UNDOCUMENTED を採る (§4-50 と同じ判断)。
  NP21/W の `sound/beepc.c` (`buz = (sysport.c & 8)?0:1`) も UNDOCUMENTED と同じ向き。
- **エミュレータで出なかった理由は未確認**: NP21/W は BUZ を**模擬している** (0035h / 0037h の書き込みで
  `beep_oneventset()`) のに鳴らなかった。ビープの音量設定か PIT #1 の状態かは調べていない (ini は見ていない)。
  「NP21/W で鳴らない」は書き方が正しい証拠にならない。
- **対策**: 割り込み許可は 0037h の BSR (00h/01h、02h/03h、04h/05h) で 1 ビットずつ書く (`ser_ien_bsr`)。ISR は
  許可しているビットだけを落として戻す (Bible §2-10 の「一度落として戻す」意図は残す)。**TXRE (bit2) を用もなく
  書かない** — NP21/W の `sysp_o37` は bit2 への BSR を送信要求と読み、IRQ4 を立てうる。`pc98.h` は
  `BSR_BUZ_ON = 06h` / `BSR_BUZ_OFF = 07h` に改め、`buz_on()` / `buz_off()` の意味が名前どおりになった
  (ほかの BSR 行は両資料で一致)。ホスト試験 `make check-serial-portc-host` (偽の outp で 8255 を模型にし、
  0035h への書き込みを違反に数える)。
- **教訓**: **0035h は全体で書かない**。同居するビットの面倒を見られるのは BSR だけ。ポート C のように
  別機能が 1 バイトに同居するレジスタへの全体書きは、エミュレータで副作用が見えなくても実機で必ず表に出る。
  極性が資料で割れる出力ビットは、UNDOCUMENTED の信号レベルの記述を採り、名前と値を同じ行に書く。

### 4-60. NP21/W を落とした直後に起動すると、媒体のロックで起動が途中で止まる (2026-09-24〜25)

- **症状**: `taskkill` → すぐ `Start-Process` で NP21/W を起動すると、FD / HDD が読めずに起動が途中で止まる
  (ユーザーから繰り返し指摘された)。
- **原因**: プロセスが消えた直後も、前のプロセス (または Windows 側の後始末) が `os32.nhd` / ISO / FD イメージを
  握ったままのことがある。新しい NP21/W はそれを開けないまま起動を進める。`taskkill` の成功やプロセスの消滅は
  媒体が開けることの証拠にならない。**1 回開けたことも証拠にならない** — `make nhd-pull` (Windows の os32.nhd を
  読んで写す) → 1 回のプローブで通過 → 起動、の順で NP21/W が立ち上がらずに終了した。後から見ると 3 つとも
  free だったので、コピー直後に Windows 側 (Defender の走査など) が一時的に掴み直し、プローブがその隙間を
  通したと見ている (2026-09-25、推定。掴んだ主体は未確認)。
- **対策**: `tools/np21w_ctl.py` (§5)。プロセス 0 を確かめ、ini にある媒体と `--fd` が Windows 側から排他で
  **続けて 5 秒**開けるまで待ってから起動し (途中でロックされたら数え直し)、起動後はプロセスが 10 秒生きていて
  `/api/status` が応答することまで見る。解けなければロック中のファイルを名指しし、起動直後に消えたら
  「起動直後に終了した」と言って失敗する。ホスト試験は `tools/tests/test_np21w_ctl.py` (`make check-tools-host`)。
- **教訓**: 起動の前提は「プロセスが無い」でも「1 回開けた」でもなく「媒体を排他で**開け続けられる**」で
  確かめる。起動の成否は Start-Process の戻りではなく、プロセスの生存と API の応答で確かめる。
- **追記 (2026-09-25)**: `taskkill /F` による停止も、名前の部分一致で別の場所の NP21/W まで巻き込み、
  起動直後の失敗がモーダルのダイアログで止まっていても外から見えなかった。NP21/W フォークに
  `/api/instance` `/api/quit` `/api/fdd` `/api/dialog` `/api/appshot` を足し、np21w_ctl は
  `/api/quit` で止め、強制終了は exe パス一致だけ、起動後は pid/exe の照合とダイアログの確認をする。

### 4-61. GUI アプリが窓も出さずに静かに消える — WM はアプリの syscall の中で走るので、出力検査が gshell 自身のポインタを弾いていた (2026-09-26)

- **症状**: GUI で File Manager (`filer.bin`) を Start メニューからでもマウスからでも起動すると、窓が出ずに消える。
  例外は 0 件、シリアルにも何も出ない。`fault_kill_count` だけが起動ごとに +1。ブレークで捕まえると
  `ring3_fault_kill` の呼び手は **`wrap_mouse_poll+0x35`** (KAPI ラッパ先頭の出力検査
  `ring3_user_ranges_writable` が偽)。そのとき CR3 = アプリの PD、渡されたポインタは **0x0037ea88 = シェル帯
  (gshell のスタック)**。`ring3_range_reject_count` は 0 のまま。
- **原因**: WM (gshell、CPL=0 の常駐シェル) は**アプリの syscall の中でしか走らない** (契約 T8 の X1 / X3)。
  `wrap_gui_call` → `gui_call` → `gshell_gui_handler(OP_WAIT)` → `wm_cycle` → `mouse_poll(&mut mi)` の間も
  `ring3_in_syscall` は 1 のままなので、出力検査 (7ef4437 / 7ec4023、2026-09-23) が gshell のスタックを
  「アプリの出力先」としてアプリの PD で PTE を見た。シェル帯には USER が無いので拒否 → **アプリを kill**。
  この検査が入って以後、ポンプ / OP_WAIT を通る GUI アプリは全部この経路で落ちていた (デスクトップまでしか
  確かめていなかった)。書き側の拒否は数えていなかったので観測点も沈黙した。
- **直し**: 「カーネルが WM のコードへ入っている深さ」`ring3_wm_depth` を `gui_call` のハンドラ / ポンプ /
  `gui_owner_exit` の前後で数え、3 つの門 (`ring3_user_ranges_writable` / `ring3_user_range_ok` / `tramp_copy`) の判定を
  `ring3_guard_active(ring3_in_syscall, ring3_wm_depth)` (`exec/ring3_str.c`) に寄せた。深さ 1 以上 = 常駐側の直呼び扱い。
  `ring3_in_syscall` の意味 (#PF/#GP の帰属) は変えない。longjmp で WM を抜ける地点 (`exec_park*` / kill / `sys_exit`) と
  ディスパッチャの入口で深さを 0 に戻す (CPL=3 の syscall は必ず深さ 0 から始まる)。書き側の拒否も
  `RING3_RANGE_WR_*` (7〜10) で数える。試験は `tools/tests/ring3_guard_tdd.md` (ホスト) + kselftest `test_ring3_wm_guard` (ゲスト)。
- **見分け方**: 「例外 0 件・窓だけ出ない・`fault_kill_count` が増える」なら §4-42 (古い shlib) ではなくこれ。
  `ring3_fault_kill` にブレークを置き、呼び手が **`wrap_*+小さなオフセット`** (= ラッパ先頭の出力検査) で、
  渡されたポインタが **0x300000 台 (シェル帯)** なら確定。`ring3_range_reject_last` が 7〜10 なら書き側の拒否。
- **教訓**: 「CPL=0 の直呼びは `ring3_in_syscall = 0` で見分けられる」は、**カーネルが CPL=0 のシェルのコードを
  syscall の中から呼び返す**設計 (契約 T8) では成り立たない。呼び手の判定は「いまどの文脈のコードが走っているか」で
  持つ。検査を足したら、その検査が**常駐側の経路**でも通ることを GUI アプリの起動まで見て確かめる — デスクトップの
  表示は WM の top-level (syscall の外) なので、この穴を踏まない。
- **追記 (同日、代行レビュー P2)**: 深さで素通しにしてよいのは **WM がいま渡すポインタ**だけ。アプリが**前に登録した**
  ポインタ (`fd_redirect_to_buffer` のバッファ) を WM の文脈で書く経路は、登録時の由来 (`FdRedirect.user_origin`) を見て
  `ring3_user_ranges_writable_always` で必ず表を歩く — 門は「呼び手の文脈」ではなく「ポインタの由来」で決める。
  深さ 0 での `ring3_wm_leave` は `ring3_wm_depth_underflow` が数える。
- **追記 2 (同日、代行レビュー P2)**: KAPI `ime_set_render(table)` は呼び手を見ずに関数表を `g_ime.render` に控え、カーネルは
  以後その `putc` / `putw` / `clear_row` を **CPL=0 で**呼んでいた — アプリが自分のメモリの表を渡すとアプリのコードが CPL=0 で走り、
  控えたまま別アプリの syscall や WM の top-level でも呼ばれ、登録したアプリが消えた後は解放済みの物理へ飛ぶ。正当な呼び手は
  gshell の top-level (起動時 / CUI 切替 / 停止、owner 1、ディスパッチの外) だけ。KAPI の target を `kernel/gui.c` の
  `gui_ime_set_render` に替え、`gui_register` と同じ「owner 1 から」に加えて `ring3_call_from_user()` が真なら黙って断る
  (戻りは void のまま、`gui_ime_render_rejected` が数える。NULL も同じ規則)。gshell が抜けたときの NULL 戻しは従来どおり
  `gui_owner_exit(1)`。試験は `ring3_guard_host.c` §5 (変異 11/11 RED) + kselftest `test_ime_render_gate`。
  **関数ポインタを受け取って後で呼ぶ KAPI は、ポインタの帯ではなく呼び手 (常駐側か) で断る** — 帯検査は「呼べるか」を保証しない。
- **追記 3 (同日、代行レビュー P3) — 「WM は信頼境界の内側」と「WM の中で落ちるとアプリの kill」の関係**: 深さ 1 以上で門を
  素通しにするのは **WM (gshell) を信頼境界の内側 = カーネルと同じ側に置く**ということで、WM のポインタは検査しない。ところが
  **フォールトの帰属は `ring3_in_syscall` のまま** (#PF/#GP も KAPI の門の拒否も) なので、WM のコードが落ちると「そのとき syscall を
  出していたアプリ」の kill として畳まれ、赤画面は出ず `fault_kill_count` だけが増える。これは意図した非対称で、
  WM のバグでカーネルごと止めずにアプリ 1 本の犠牲で済ませる代わりに、**落ちた場所が見えなくなる** — 今回の障害
  (例外 0 件・窓だけ出ない) がまさにそれだった。そこで挙動は変えずに観測点を足した: `exec/exec.c` の `ring3_kill_kind` が
  深さを 0 に戻す前に `ring3_wm_fault_count` を +1 (FAULT のみ)、ISR の kill の行 (`[ring3] exception` / `[ring3] #PF`) に
  ` (in WM)`。**`fault_kill_count` と一緒に `ring3_wm_fault_count` が増えたら WM の文脈で落ちている** (アプリではなく gshell か、
  gshell が渡したポインタを疑う)。登録の門 (`gui_register` / `gui_ime_set_render`) は同じ「owner 1 かつ `ring3_call_from_user()`
  が偽」に揃えた — 信頼境界の内側に入れるのは gshell の top-level が登録したコードだけ。試験は `ring3_guard_host.c` §6 と
  `test_ring3_guard.py` の静的検査 (変異 17/17 RED)。

### 4-62. 実機の画面モード設定は NP21/W の BIOS 値ではなく**実機 ROM の OUT 列**に合わせる — Ra266 の PEGC 640x480 (2026-09-25〜29)

- **症状**: 実機 PC-9821Ra266 で `os32gui` から PEGC 640x480 へ入ると、モニターは 31kHz / 640x480 と認識するのに表示が崩れる
  (桁がずれる)。NP21/W では正しく出ていた。
- **原因**: `pegc_apply_timing` の 480 ラインへの入り方は、SYNC / SCROLL の値の一部を **NP21/W の BIOS 実装から写し**、
  GDC の PITCH やクロックの状態は**起動時の BIOS の残りに依存**していた。NP21/W の BIOS は実機の ROM と同じ手順を踏んでいない —
  実機 ROM (INT 18h AH=30h) の OUT 列を `v86 -g` で全部記録して比べると、NP21/W では出なかった違いが並んだ:
  GDC の RESET と表示の停止をしていない、グラフィック GDC の CSRFORM・SCROLL の長さ (3FFh)、テキスト GDC の 480 ライン設定
  (SYNC・PITCH 80・CSRFORM・SCROLL)、6Eh とテキスト CRTC、09A8h を毎回書いて bit7 を落としていた、GDC クロック 5MHz は 6Ah の
  83h・85h の両方 ([TASK_PEGC480_REALHW](tasks/realhw/TASK_PEGC480_REALHW.md) §3-3 の比較表)。
- **直し**: 入り・戻りの順序と値を実機 ROM の記録 (各 94 行) に合わせ、`gdc_send` は FIFO を有界に待つ。
  受け入れは**画面を見ない条件**: (A) 実物の `backend_pegc.c` の OUT 列が ROM の記録と一致するホスト試験 (意図的な違いは理由付きの除外表、
  **A0h DFh / A2h 28h の組は未解明の差分**)、(B) NP21/W の回帰、(C) 実機で `pegcchk` ([RELEASE_v2.1.md](RELEASE_v2.1.md) §2-1)。
- **見分け方**: 実機でだけ崩れる画面モードは、まず `v86 -g` で**実機 ROM が同じモードへ入るときの OUT 列**を取り、OS32 の OUT 列と
  並べる (NP21/W の BIOS のソースを正解にしない)。実機で崩れが残るなら最初に疑うのは除外表の未解明の組。
- **教訓**: エミュレータの BIOS は「エミュレータの上で辻褄が合う値」であって実機の手順の写しではない。
  **実機のハードウェアへ書く列の正典は実機の ROM が流す列**で、取れるなら取ってから合わせる (§4-50 の資料の決着、§4-51・§4-54 の
  「エミュレータが模擬しない」と同じ系統)。

### 4-33. `hsync` は HostDrv の**古い**ファイルで NHD を上書きする (2026-09-12)

- **症状**: NHD 配備 (`os32-cycle deploy`) 直後に、試験用ファイルを 1 本足す目的でゲストの `hsync` を実行したら、
  `/boot/vmkernel.lz4` `/bin/gshell.bin` `/usr/bin/t5a_display.bin` が**数世代前**のものに戻った (サイズが手元のビルドと不一致)。
- **原因**: `hsync` は `/host` (HostDrv = `C:\os32`) → `/` の差分コピーで、HostDrv は `make deploy` を回したときの状態のまま。
  NHD 配備 (`make deploy-nhd` / `os32-cycle deploy`) は HostDrv を更新しないので、その後に `hsync` すると HostDrv の古いビルドが「差分」として NHD に戻る。
- **規則**: `hsync` / `hsync sys` の**直前に必ず `make deploy`** (HostDrv 同期) を回す。HostDrv へ手で置いた試験ファイルも同じ経路で運ぶ。
  実行後はゲストの `ls -l` と手元のサイズを突き合わせる ([V4]、§4-29 と同じ)。
- **PM の作業上の罠 (同日)**: この節を書くときに引用なしのヒアドキュメントを使い、本文のバッククォートがシェルで実行された。
  文書をスクリプトで書くときは `<<'EOF'` (引用付き) にする。

### 4-34. RAM の**上端**でデバイス窓の可否を決めない — K6 以後は「穴」の有無で決める (2026-09-12、修正済み)

- **症状**: 15MB 構成 (NP21/W `ExMemory 16`) + `gfxmode pegc` + リセットで、`hal_test` が
  `backend pc98 (planar 4bpp)` のまま。`sys_top_reserved` = 0。BIOS の PEGC ビット
  (0x045C bit6 = 0x40、0x0597 bit2 = 0x84) は立っているので、機種判別 (probe 段 1) は通っている。
- **原因**: `gfx/backend_pegc.c` の probe 段 2 が `sys_get_mem_kb() * 1024 > PEGC_LINEAR_BASE` で
  「自分の RAM が窓に届いているか」を見ていた。K6-RAM (`f6ec520`, 2026-09-11) で `sys_mem_kb` の
  定義が「**RAM の上端アドレス / 1024**」に変わり、15MB 機でも高位 RAM (16-17MB) のぶん 17408 を
  返す。窓 `[0xF00000, 0x1000000)` は K6 の検出器が **RAM にしない穴** (`MEMORY_BOOT_LEGACY_END`
  クランプ + `PHYSMEM_RESERVED`) なのに、上端で見たせいで「RAM が届いている」と誤判定し、
  9801 プレーナへ落ちていた。K6 以前は上端が 0xF00000 だったのでたまたま通っていた。
- **修正**: 判定を「窓に RAM が登録されているか」に変える。`kernel/pgalloc.c` に
  `pgalloc_range_has_ram(first, end)` (PFN 半開、ブート時に凍結した物理地図を `physmem_count` で
  見るだけの問い合わせ) を足し、probe 段 2 は
  `pgalloc_range_has_ram(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE, MEM_HIGH_RAM_BASE / PAGE_SIZE)` で決める。
  8MB (legacy 経路) は窓まで RAM が届かないので従来どおり真。
- **教訓**: (1) **「上端 > 窓」は「窓が RAM」ではない** — 穴のある物理地図では上端は窓の可否を
  答えない。デバイス窓の可否は必ず**その範囲**を物理地図に問い合わせる。
  (2) 同じ型の誤判定が `gfx/backend_cirrus.c` の `cirrus_win_usable()` にもあった
  (`sys_get_mem_kb() > base / 1024`)。RAM 17,408KB の NP21/W で**穴の中のバンク窓 0xF60000 まで**
  拒み、Cirrus の probe が ID 判定の前に落ちた (2026-09-29、TASK_S5 §6 R2)。同じ口
  (`pgalloc_range_has_ram`、窓の全ページ) に直した (`tools/tests/test_cirrus_win.py`)。
  **リニア窓 0x1000000 (2MB) は高位 RAM のある構成では本当に RAM と重なる** (NP21/W 17MB、実機 64MB) ので、
  ユーザー決定 (2026-09-29、A2) で **v3 のデバイス窓の帯 0xFE000000** (`MEM_DEVICE_APERTURE_*`、物理地図で MMIO、
  PT は paging_init が静的に 1 枚) へ移した。Cirrus は NP21/W 互換のためだけなので、auto の probe は
  `np2_detect()` が真のときだけボードの ID (0FAAh/0FABh) を読む (`GFX=cirrus` の明示時は読む)。
  この門は実機でも auto の probe ごとに np2_detect の検出通信 (07EFh、mouse_init と同じポート) を 1 組足す —
  新しいポートは無いが I/O の回数は増える。
  (3) 定義を変えた関数 (`sys_mem_kb`) は、**呼び出し側の意味**まで洗う。
- **回帰**: `tools/tests/test_memory_boot.py::test_ram_kb_is_the_registered_total_not_the_top`
  (17408 / 33792 / 8192 の 3 構成) と `tools/tests/test_pgalloc_range.py` の `device_window_ram`。
