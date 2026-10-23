# S8 (GeForce A1 → A2) 立会いの台本

正典: 票 TASK_HWDEV §3-1 の 6、TASK_REALHW_RECOVER の T5、設計 [GF6200_4A_DESIGN](../../docs/decisions/realhw/GF6200_4A_DESIGN.md) §3 (A1・A2 の行)・§7 (実機 A1・A2) と
[GF6200_S7_SHELL](../../docs/decisions/realhw/GF6200_S7_SHELL.md) §3・§4。本ファイルは受入の台本で、確定設計を変えない。
**書込みを伴う段ごとにユーザーの承認を取る** (U2: A1 → 結果確認 → A2、main 0〜3 と A4 はこの回に含めない)。

## 0. 前提 (立会いの前に PM)

- 実機の版が push 済み・CI build 成功の SHA (スキル os32-realhw-update)。`ver` の Commit と Image CRC を控える (`--image-crc` に使う)。
- `recover status` が `state=IDLE`。inspect は §1 の 2 で 1 回だけ (同じ sid の 2 回目は行が出ない — GF6200_S7_SHELL の確定 C1)。
- ノートの rshell の受信者は 1 個だけ (`rshell_recover.py` と `rshell_serial.py` を並行させない)。9600、`sg dialout`。
- 証拠はノートの `~/os32-tmp/evidence/<日付>/realhw_s8/`、ROM のバイトは出さない・コミットしない ([HW3])。

## 1. A1 (BAR の長さ) — 承認 1

1. `rshell_serial.py --port /dev/ttyUSB0 cmd 'recover status'` → 現在の sid を控える (例 5)。
2. `rshell_serial.py … cmd 'gfinit inspect 5'` → 2 行 (`GF inspect … chip=44 expect=44 match=1` と `GF inspect-end sid=5 rc=0`)。`match=1` でなければ止める。
3. **承認を取ってから**:
   `rshell_recover.py --port /dev/ttyUSB0 --session 0 --state a1.json --image-crc <CRC> --log a1.raw --command 'gfinit size-bars 5 --allow-write'`
4. 合格: 最初の REC の sid (例 6) が新しい session、`phase=final event=END result=OK verified=1`、終端に Command=0102h (BM=0) の verify。
   `GF A1 reject=…` の行が出たら BARLEN の照合で断られた — 行の bdf・reg・raw を控えて止める (設計に戻す)。
5. 記録: BAR0/1/2 の mask・size・復元値、BAR1 の窓のサイズ (A2 の前提)、REC の snapshot の行 (`snapshot_crc`)。
   `recover status` の state と session。

## 2. A2 (準備) — 承認 2 (A1 の記録をユーザーに見せてから)

1. `rshell_recover.py --port /dev/ttyUSB0 --session 0 --state a2.json --image-crc <CRC> --log a2.raw --command 'gfinit prepare 6 --allow-write'`
   (コマンドの sid は A1 の sid、monitor は 0 と別の新しい state)。
2. 合格: 新しい sid で PAUSED に止まる。REC で PBUS 1854h=0・cfg54h=0・1850h=0・PMC 割込み=0、Command=0102h・BM=0、
   PMC.ENABLE bit0・head0 の選択の読み戻し。
3. **ユーザーの目視**: 98 のテキストが表示されていて更新されること (G1)。映らない・崩れたら次の 4 で止める。
4. 止め方 (main 0 はこの回に進めない): PAUSED の継続として同じ state と log で
   `rshell_recover.py --port /dev/ttyUSB0 --session <A2 の sid> --state a2.json --image-crc <CRC> --log a2.raw --command 'gfinit abort <A2 の sid> --allow-write'`
   → `result=OK` (終端の ② で 98 復帰)。応答が無ければ `recover now <sid>`、それも無ければユーザーが電源を入れ直す。
5. 記録: CR・head0/1 の SR/GR/MISC の別名の読み、PSTRAPS から導く RAM 選択 (組 3 の見込み)、PAUSED に入るまでの時間と poll 数。

## 3. 終わり

- `recover status` (IDLE または VERIFIED)、`ver`、`mem` (leftover・audit_fail が 0)。
- CUI のまま、最後に **GRPH+2+RESET** (31kHz)、再起動後の `ver` と 09A8h。
- 0FACh の初めての書込み — 承認 3 (A2 を止めた後、IDLE・session 0 の新しい起動で。route-test は session 0 だけ受ける):
  `rshell_serial.py … cmd 'recover route-test 0 --approve'` → route の step 0→1→0 が 1 組ずつ、終端 `PARTIAL target=S0 verified=1`。
  直後にユーザーが 98 画面を目視。戻らなければユーザーがリセット。口は [RECOVER_ROUTE_TEST](../../docs/decisions/realhw/RECOVER_ROUTE_TEST.md)。
- 結果は `RESULT.md` に項目ごとの 合格/不合格/未確認。票には合否 1 語と証拠の所在だけ。
