# RECOVER-T4B 再開手順と 2026-10-23 の記録

正典: 票 TASK_REALHW_RECOVER §5–6 と「T4b の段の細分」、台帳 DEFERRED_TESTS の RECOVER-T4B。
設計: [RECOVER_T4B_INJECT](../../docs/decisions/realhw/RECOVER_T4B_INJECT.md)。
本ファイルは受入の台本。確定設計を変更しない。

## 今回の実行

- 作業木 `wt/t4b`、基点 `49b1ddb`。製品コード・ini・NHD は無変更。
- 証拠: `/home/hight/os32-tmp/evidence/2026-10-23/recover_t4b/RESULT.md`。
- ログ: `/home/hight/os32-tmp/run/t4b/`。全試験は `check_slot.sh t4b` 経由。
- HTTP 直接接続は `Connection refused`、Windows curl への fallback は
  `UtilBindVsockAnyPort: socket failed 1`。ゲスト命令・画面取得に至らず、NP21/W の受入は未確認。
- 停止・起動・reset は未実行。PM が用意した crt31k.ini の起動状態を変更していないが、最終生存は未確認。
- `make all MUTATE=0`、build 後の preflight、既存 `test_recover.py` / `test_recover_t3.py` /
  `test_rshell_recover.py` (変異なし)、`check_select.py --lint` は rc=0。
  ホスト模型を NP21/W の PAUSED 門を通った証拠にはしない。

## 接続が復旧したら

`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp/run/t4b HOST32_RUNNERS=qemu`
を明示し、試験は `/home/hight/os32-tmp/bin/check_slot.sh t4b <cmd>` 経由で実行。
`tools/np21w_mcp/np21w_client.py` の `post('/api/cmd', command, timeout=60)` を使う。
`/api/cmd` と rshell_recover の COM1 読取りを並行させない。

1. `ver` と boot.log で Commit `1fd0a49`、実際の Image CRC、`[pegc] hsync=31k`、
   KDEBUG と hwdev の起動を保存。`sync` を済ませる。
2. `python3 tools/rshell_recover.py --port aidebug:http://127.0.0.1:8025 --status --log <新しいraw>`。
3. IDLE/session=0 を確認してから、同じ道具に `--session 0 --image-crc <実測CRC>
   --state <新しいjson> --log <新しいraw>` を渡す。
   期待は連続した REC の `phase=final event=END result=PARTIAL target=S0 verified=1 unknown=1`。
   道具の rc=1 は PARTIAL を成功としない契約なので、それだけで製品不合格にしない。
   終端全体・seq/lost・画面・後続応答を照合する。
4. `/api/screenshot` の BMP を PNG として保存して開く。98 の文字・色・カーソルを目視し、
   後続の `echo alive` と status を保存する。復帰処理を通らない拒否だけで画面復帰を合格にしない。
5. 仮想 profile の試行・PAUSED status 拒否・期限・故障→S0、送信詰まり・tick停止時の poll 上限を
   NP21/W で採取する。現行の shell/recovery_call に仮想 profile 選択口は見つかっていない。
   試験用注入手段の準備が残る。ホスト試験の PASS で代用せず、PAUSED 門除去の変異も未実施として残す。
6. REC の途中欠落と終端/EOT 不着の host 非成功判定を実際の経路でも確認する。
   GPU 試行成功は対象外。拒否 trace は別途採取する。
7. 製品不具合を確認したら修正せず止め、再現コマンド・ログ・file:line を RESULT に追記する。

## PM: 隔離した像での旧版一式の巻戻し (未実行)

対象は `os32_t2h_install.nhd`、ローカルは `build/nhd/os32_t2h.nhd`。
ini は PM が選ぶ隔離像専用のもの (以下 `<隔離ini>`)、31kHz と媒体パスを PM が確認する。
本 NHD を指す `crt31k.ini` はこの演習に使わない。ini の作成・編集・選択は本作業では行わない。
旧版候補は T4a の `53bbee8`。旧版・新版をそれぞれ別の作業木でビルドし、世代ごとに
カーネル、全 paired modules、`/sys` と対応する userland をハッシュ付きで保存する。
旧版に無い `hwdev.mod` を残すような混在も避ける。

1. `python3 tools/np21w_ctl.py stop`。PM の承認した隔離像を準備。
2. 隔離像を扱う作業木から順に
   `python3 tools/nhd_deploy.py umount --profile t2h`、
   `python3 tools/nhd_deploy.py pull --profile t2h` を実行。
3. PM が旧版一式をその隔離像に配備する。ファイルコピーには
   `python3 tools/nhd_deploy.py copy --profile t2h --dest <guest-dir> <同一世代のfiles...>` を使える。
   全 manifest 対象と不要な旧部品の処置を確定してから実行し、カーネルだけの差替えにしない。
4. `python3 tools/nhd_deploy.py deploy --profile t2h`、
   `python3 tools/np21w_ctl.py start --ini <隔離ini> --wait-ready`。
   `ver` の旧版 Commit/Image CRC と部品起動を保存。その旧一式を復元元として保持。
5. 隔離像で新版へ更新し、`hsync boot` の `/boot/vmkernel.old` と旧起動 CRC の一致を保存。
   `/sys` と paired modules は別途旧一式を保持する (`.old` はカーネルだけを保証する)。
6. 停止→新しい pull→旧一式を復元→隔離像へ deploy→同じ隔離iniで起動。
   `.old` の kernel と同じ世代の paired modules・`/sys`・userland を揃え、
   `ver`、部品 PAIR、ハッシュ、起動・シェルの正常応答を照合する。
7. 演習終了後 `python3 tools/np21w_ctl.py stop`、
   `python3 tools/np21w_ctl.py start --ini crt31k.ini --wait-ready`。
   本像の `ver` と 31kHz の画面を保存し、起動状態で残す。

この手順は未実行であり、隔離像の配備や巻戻しの合格を主張しない。
