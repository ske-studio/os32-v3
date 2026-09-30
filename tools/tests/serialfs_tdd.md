# serialfs 検証記録 (シリアル越しの /host、`sfs run`)

票: [TASK_SERIAL_HOSTFS](../../docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md) 部品 B
(§1-v2 / §1-v3 / ユーザー決裁 2026-09-24)。受入 T1 と T5 のホスト部分。

## この文書の性格

実装を先に書き、試験は後から足した。RED の根拠は**変異がすべて RED になること**
(下の表) で、変更前のコードで試験を回した RED→GREEN の経過ではない ([V4])。
試験を書く途中で本物の欠陥を 1 つ見つけた: `fs/serialfs.c` がホストの status を
`(i32)` で読んでおり、ホスト試験の 64 ビット環境では負の値が正に化けて
「知らない status を IO に畳む」が効かなかった (i386 では 32 ビットなので起きない)。
`int` で読むように直した。

## 実行

```
python3 -B tools/tests/test_serialfs.py --target --mutate   # make check-serialfs-host
```

| 段 | 何を | どうやって |
|---|---|---|
| C | フレーム・CRC32・番号・再送・長さの境界・VFS の契約・障害の注入 | `serialfs_host.c` が実物の `fs/sfs_proto.c` / `fs/sfs_client.c` / `fs/serialfs.c` を取り込み、偽の線 (仮想の tick、届く時刻つきのバイト) と C の偽ホストで回す |
| GATE | ゲート (呼び口で分ける) | `serial_gate_host.c` が実物の `drivers/serial.c` を偽のポートで回す |
| PY | ホスト側の振り分け・パス・キャッシュ・期限・BYE・行の外 | 実物の `tools/serialfs_host.py` と `tools/rshell_serial.py` を偽のポートで |
| PY | NP21/W の COM1 を HTTP で読む口 (`--port aidebug:...`) | 偽の urlopen で `/api/serial/read`・`write` の分割・空の回・読み捨て |
| XC | C ⇔ Python | C の組んだ列を Python が読む / 逆、定数の照合、**実時間のパイプで結合** (RENAME の応答を 1 回落とし、READ の応答の CRC を 1 回壊す) |

障害の注入 (T5): 応答の喪失 (1〜3 回は通る / 4 回で IO / 3 要求続けて死ぬ)、遅延
(遅れた応答が次の要求の最中に届いても取り違えない)、CRC 破損、番号違い・セッション
違いのフレーム (数えずに捨てて期限を延ばさない — 流れ続けても抜ける)、ホストの停止、
ごみの連続、ERR (未知のセッション)、契約違反の応答 (要求より多い READ・`/` を含む名前・
進まない cookie・知らない status)、再送で副作用が二重にならない (ホストのキャッシュ、
結合でも)、セッション中に rshell へフレームが漏れない (ゲート)、`cat` の本文にフレームが
あってもセッション外では答えない。

`hsync` の vmkernel.old の門は、純粋な判定 (`hsync_bootold.inc`) をこの試験で、
実物の hsync.c を通した流れ (一致 → BACKUP してから置き換え / 不一致・記録なし →
置き換えない / `--no-backup` / dry-run / 同じ内容なら作り直さない) を
`tools/tests/hsync_h2_host.c` の `case_boot_old` (make check-hsync-h2-host) で見る。
既存の `/boot` の案件 (rename 後の sync 失敗) は贋カーネルが v53 で起動イメージを
答えないので `--no-backup` を足した。

## 変異

組めない・当てはまらない変異は ERROR として数える。恒等の対照を C と Python に 1 本ずつ
置き、それが GREEN であることを見る。変異の組み立ては `-Werror` を外す (使われなくなった
引数の警告で ERROR にしない)。実物の組み立ては `-Werror`。

2026-09-25: 51 本 (C 33 + ゲート 6 + Python 12)、うち対照 2 本 GREEN、残り 49 本すべて RED、
ERROR 0。一覧は `test_serialfs.py` の `C_MUTATIONS` / `GATE_MUTATIONS` / `PY_MUTATIONS`。

レビュー往復 1 の後 (2026-09-25): 66 本 (C 36 + ゲート 7 + Python 23)、対照 2 本 GREEN、
残り 64 本すべて RED、ERROR 0。足した試験と変異:

| 指摘 | 試験 | 変異 |
|---|---|---|
| Codex 1 期限切れの応答 | 偽の線 (到着時刻つき) で「STAT の処理が 20 秒止まる間に再送 3 回・BYE・EXIT・EOT」、「処理が 2.5 秒 → 応答は送らず、再送に保存した応答で答える」、「先に BYE が届いていれば期限内でも送らない」 | 期限切れの要求を実行する / 送る前に取り込まない / 先の BYE を見ない / 送る直前に期限を見ない |
| Fable M1 要求ごとの 200ms | pyserial の read を模した偽物 (揃わなければ待つ = stalls) で send_cmd と実物の PortReceiver (スレッド) + serve_line の 20 往復が stalls 0 | read(MAX_FRAME) / read(256) に戻す |
| Codex 2 壊れた .old | `hsync_h2_host.c` の `case_boot_old`: image_crc 欄だけ壊す → 断る、既存の .old がある状態で書き込み・sync・読戻し・判定後の差し替え・rename の各段で落とす → 旧 .old も本名もそのまま | (h2) 一時ファイルの自己検査なし / 欄を見ない / 門の失敗を無視、(C) 欄の検査を外す |
| Codex 3 / Fable m2 Windows のパス | ntpath で `C:\hostile` / 別ドライブ / `..` / ADS の `:` | 前方一致に戻す / `:` を通す |
| Codex 4 / Fable m1 パイプの後段 | rshell が行全体を引き受ける (組込みの `sfs` は常に断る) | — (rshell のループはホストで回していない) |
| Codex 5 symlink の差し替え | 固定の作り (dir_fd + O_NOFOLLOW) で、操作の直前に要素を根の外への symlink に差し替えても辿らない。根の中の symlink も辿らない | O_NOFOLLOW を外す |
| Codex 6 UART / FIFO の残り | ゲートの上げ下げで ISR 前のバイトを読み捨てる | 読み捨てを外す |
| Codex 7 REPL の EXIT | 2 行目で EXIT が来なければ exit は None | begin_line を空にする |
| Codex 8 ESC の拒否 | `rsh_line_*` (行の組み立てを純粋な関数に) で、拒否は行末か 2 秒の沈黙まで解けない | 間で解ける / 行頭の ESC を数える |
| PM 決定 (書き込み) | 既定は ROFS、`--allow-write` は要素単位 (`subx` は `sub` でない) | 禁止が無い |
| Fable m4 | `--serve-host` 無しの `sfs run` は送らない | 送る |

レビュー往復 1 の仕上げ (2026-09-25、コーダー交代の後): **セッション層のホスト試験
`tools/tests/serialfs_session_host.c`** を足した (段 SESSION)。実物の `fs/serialfs_session.c`
を `drivers/serial.c`・`fs/sfs_*.c` と組み、偽物はポート I/O (届く tick つきの受信)、
hlt で進む tick、VFS の mount / umount / fstype、kprintf (console の複写と同じく
serial_putchar へ)、常駐シェルの持ち主だけ。5 案件:

| 案件 | 見ること |
|---|---|
| `flow` | HELLO → mount (`/host` `COM1` `serialfs`、permit の中) → 子の kprintf は線に出ない → BYE → LOG (子の出力・計数・`sfs: exit=7`) → EXIT (code 7、flags 0) の順、終わった後は線に何も残らず rshell にも渡らない、2 度目の begin は BUSY、owner 違い / IF=0 は INVAL、`/host` 使用中は BUSY (HELLO を出さない) |
| `hello_fail_quiet` | 決定 11: 答えの無い HELLO (1 + 3 試行) の後、ごみが最後の期限の後も 100 tick 続く → **ごみが止まってから 500ms 静まるまで下ろさない**、`sfs: no answer to HELLO` は下ろした後に生で流れる、ごみは時計を進めて ISR を回しても rshell に 1 バイトも渡らない |
| `hello_fail_flood` | 相手が送り続ける → 隔離は 5 秒で打ち切り、`line not quiet` を画面と線 (生) に出して下ろす (決裁 1B) |
| `end_late_hold` | Fable m6: 2 回目の sfs_send_log の送信中に入った 'Y' は同じ汲み出しの LOG フレームで EXIT の前に、EXIT の送信中に入った 'Z' は下ろした後に生で流れる (どちらも捨てない)。保留は空になる |
| `end_not_quiet` | 終わりに相手が送り続ける → sfs_end は 1、EXIT の flags に NOT_QUIET |

変異 (`SESSION_MUTATIONS`、対照 1 本を含む 7 本): HELLO が通らなかった回は隔離せずに下ろす /
EXIT の後の保留を捨てる (4bde23b の `if (sid == 0)`) / BYE を送らない / NOT_QUIET を出さない /
`/host` 使用中でも始める / IF=0 のまま始める。**組んでみて外した変異 2 本**: 「EXIT の直前の
3 回目の sfs_send_log を外す」は GREEN のまま (2 回目の汲み出しループが送信中に入った分も
拾うので 3 回目は空振り。窓を閉じるのは下ろした後の生の流しなので、3 回目の呼び出し自体を
削った)、「`g_active` の BUSY を外す」も GREEN (マウント中は `/host` の検査が先に BUSY を
返し、単一スレッドでは到達しない)。

合計 73 本 (C 36 + ゲート 7 + セッション 7 + Python 23)、対照 3 本 GREEN、残り 70 本すべて
RED、ERROR 0 (`make check-serialfs-host`)。`--allow-write /` は根の全体、`..` を含む指定は
起動時に ValueError で断る (試験 `paths`)。

レビュー往復 2 (2026-09-25、Codex ラリー 2 の未完了 3 件 + 新規 2 件):

| 指摘 | 直し | 試験 | 変異 |
|---|---|---|---|
| Codex 1 [P1] リンク越しに `--allow-write` の外へ書ける | パスの作りは `resolve_write` が wire の名前に加えて **realpath で解決した実体** (`real_comps`) にも許可を当てる (write / mkdir / rmdir / unlink / rename)。固定の作りは名前を辿らないので穴が無いことを試験で確かめた (最後の要素の symlink も `O_NOFOLLOW`) | `paths`: `out/link → protected`、`out/f2 → protected/x` に対する write / mkdir / rename / unlink / rmdir が、パスの作りでは ROFS、固定の作りでは INVAL (rmdir は ENOTDIR)、protected には何も届かない。許可の中に留まるリンク (`out/self → out/inner`) はパスの作りで通る。ntpath は `real_comps` と大文字小文字を無視した許可 | 実体に許可を当てない / 固定の作りの最後の open から O_NOFOLLOW を外す |
| Codex 2 [P2] openat の起点が固定されていない | 起動時に根を `O_RDONLY\|O_DIRECTORY\|O_NOFOLLOW` で開いて `root_fd` に保持、`_open_dir` は `dup` した起点から辿る。`close()` を足した | `paths`: HostFS を作った後に根を rename して外を指す symlink に差し替えても stat / list / write は元のディレクトリに当たる。realpath を解かない pm で根が symlink なら起動時に ELOOP | 操作のたびにパスで開き直す / 起点の open から O_NOFOLLOW を外す |
| Codex 3 [P2] ESC の拒否が 2 秒の沈黙で解ける | `rsh_line_idle` は拒否した行に対して**常に** MORE (沈黙で閉じない、`RSH_JUNK_IDLE_TICKS` を廃止)。閉じるのは行末 (\n / \r) だけ — ホストの次の行の改行 (その行も拒否、EOT 1 つ) か本体の Enter。本体の ESC は rshell を抜ける (回復の口) | `rshell_rules`: idle を 100,000 回呼んでも MORE、沈黙の後の続きも拒否のまま、`\n` / 本体の `\r` で閉じる、本体の ESC で EXIT | 沈黙で解ける (元) / 200 回の idle で解ける / 本体の Enter で閉じない |
| Codex 4 [P2] 終わった行の受信スレッドが次の行の応答を奪う | `PortReceiver.close()` は `port.cancel_read()` で待ちを切りながら join し、**スレッドが終わったことを確かめて** True を返す (上限 30 秒)。`serve_line` は終わらなければ RuntimeError (ポートを次へ渡さない)。`AidebugPort` に `cancel_read` (read の 10ms 刻みの待ちを切る、pending は残す) | `read_size`: read が 10 秒待つポートで close が 2 秒未満に戻り、ポートに待ち手が残らず、次の読み手がバイトを受け取る。`AidebugPort(timeout=3)` の read が cancel_read で 1.5 秒未満に空で戻り、pending は残る | close が終わりを確かめずに戻る / cancel_read が起こさない |
| Codex 5 [P2] pwrite の一部書き込み | `_pwrite_all` が戻り値を見て残りをくり返す (0 なら IO)。パスの作りは `f.write` の戻りを検査 | `paths`: 3 バイトずつしか書かない pwrite で 10 バイトが 4 回で全部届く、進まない pwrite は IO | 戻り値を見ない / 進まない pwrite を成功にする |

## 試していないこと

- NP21/W と実機 (T2 / T3 / T4)。**NP21/W は通信速度を模擬しない**ので、115200 の
  16 バイト FIFO の取りこぼし・ISR が汲み残す FIFO の末尾は実機でしか踏めない
- `fs/serialfs_session.c` はホストで回す (段 SESSION) が、VFS・kprintf・tick は偽物。
  実物の `vfs_mount` が cwd を `/` に戻す件と `sfs run` の cwd の復元は rshell の側で、
  ホストでは回していない
- rshell の行ループ (`sfs run` を行全体で引き受ける、ESC の拒否を行末まで保つ) は
  純粋な関数 `rsh_line_*` / `rsh_sfs_child` だけをホストで見ている。ループ本体は
  NP21/W / 実機
- 常駐シェルの `sfs run` と rshell の行の読み取り (ESC の単独判定の待ち) は、純粋な
  判定 (`rsh_esc_classify` / `rsh_sfs_child`) だけを回している
