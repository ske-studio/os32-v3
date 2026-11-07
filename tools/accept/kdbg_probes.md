# kdebug 採取点・段 D の受入台本

正典: [KDBG_PROBE_DESIGN §5・§6](../../docs/decisions/realhw/KDBG_PROBE_DESIGN.md)。
この台本は未実施。PM が NP21/W を操作し、実機の操作はノートのセッションへ依頼する。
最初に版、Image CRC、同じビルドの kernel / kdebug.bin / hwdev.mod /
geforce.mod / trident.mod / shell、KDEBUG 有効、選択バックエンドと PCI BAR を記録する。
ログ出力先は既存の書込み可能な `/host/logs` とし、各回で別名を使う。
再起動すると ARM は消える。設定後から drain まで再起動しない。

以下のコードブロックを指定名の HW-BASIC ファイルとして `/host` に置く。
`probe` の番地はリテラルのみ。GF の例は実測 BAR0=0x24000000 の場合であり、
違えばすべて実測 BAR0+オフセットへ置き換える。

共通の `reset.hwb`（各試行の最初に必ず実行）:

```text
probe stop
probe reset
probe list
```

共通の `drain.hwb`:

```text
probe stop
probe list
probe drain
```

## NP21/W: 登録されないことと未 ARM の退行

1. 変更前の版で、通常の画面表示・入力更新、`gf2 enter` / `gf2 leave`、
   `tri poll` の戻り値・既存診断と画面を保存する。同じ設定で今回の版を起動する。
2. `hw run -o /host/logs/np-probe-reset.txt /host/reset.hwb`。
   LIST に `gf4.detect.pre`・`gf4.mode.post`・`tri.screeninit.post` が無いこと。
   HWDEV 自体がロードされても表は空。GeForce/Trident のない環境で点を作らない。
3. ARM をせず、1 の操作を再実行。既存の拒否結果も含め、戻り値・診断・
   表示と入力更新が変わらないことを確認する。
4. `hw run -o /host/logs/np-probe-drain.txt /host/drain.hwb`。
   `hit=` のある採取行が0件であること。ここでハードウェア値の照合は行わない。

## 実機 GeForce: ARM → enter → STOP → -o

GeForce が選択された実機の CUI から行う。未 ARM の `gf2 enter` / `gf2 leave`
を先に1回行い、画面・戻り値と既存 `[gf4] detect state` / scanout 診断を保存する。
`gf2 leave` 後に以下を実施する。GUI 起動中のまま enter を重ねない。

`gf-arm.hwb`（BAR0=0x24000000 の例）:

```text
probe stop
probe reset
probe list
probe add gf4.detect.pre idxmmio8 0x246013d4 0x246013d5 0x1f
probe add gf4.detect.pre idxmmio8 0x246013d4 0x246013d5 0x44
probe add gf4.detect.pre idxmmio8 0x246013d4 0x246013d5 0x11
probe add gf4.detect.pre mmioread32 phys 0x24000200
probe add gf4.detect.pre mmioread32 phys 0x24600860
probe add gf4.mode.post mmioread32 phys 0x24000200
probe add gf4.mode.post mmioread32 phys 0x24600860
probe arm
probe list
```

1. `hw run -o /host/logs/gf-arm.txt /host/gf-arm.hwb`。
   GF の2点が登録され、ARM 成功、両点 `enabled=1` を確認。
2. シリアルの全受信を保存しながら `gf2 enter` を1回実行し、成功と表示を確認する。
3. `hw run -o /host/logs/gf-drain.txt /host/drain.hwb`。
   STOP 後は `enabled=0`。detect.pre の5項目が同一 hit、mode.post の2項目が
   次の hit で、計2回・7項目。全項目 `rc=OK restore_rc=OK`。
   detect.pre は直後の同じ enter の `[gf4] detect state` の
   `cr1f/cr44/cr11/pmc/ec0` と値が一致すること（read_rc/mmio_rc も0）。
4. mode.post の PMC/ENGINE_CTRL は初期 restore 成功直後の値。
   leave 前に既存診断の `peek -m -w4 0x24000200 4` と
   `peek -m -w4 0x24600860 4` を保存し照合する。後続処理が値を変えた場合は
   一致扱いにせず、採取時点と診断時点の差として実ログを PM に渡す。
   mode.post は初期モード限定で、一般の restore/set_mode の点ではない。
5. 同じ drain.hwb を別の -o ファイルへ再実行し、採取行0件（消費済み）を確認。
   STOP のまま `gf2 leave` → `gf2 enter` → drain を行い、新しい採取行0件と
   元の表示・戻り値を確認して `gf2 leave` で戻す。

GF の index/data は BAR0+0x6013d4/5、同一ページの単純 CR 対。
根拠は既存 `svc_detect_diagnostic` と
[GF レジスタ契約](../../docs/design/hwdev/gf_load_state.md)、
[設計 §2・§3](../../docs/decisions/realhw/KDBG_PROBE_DESIGN.md) の確定済み対。
採取は旧 index の保存・選択・復元だけを行い、unlock/head 切替や data 書込みを加えない。

## 実機 Trident: ARM → ScreenInit → STOP → -o

Trident が選択された実機で、CUI から `os32gui` による GUI への切替を使う。
この境界の HAL init が ScreenInit を呼ぶ（`backend_trident.c:init`）。
`tri mode` や単なる EnterVT はこの点を通る操作の代わりにならない。
まず未 ARM で同じ往復を1回行い、表示・戻り値・既存診断を保存する。

`tri-arm.hwb` の `INDEX_PHYS` / `DATA_PHYS` は実測のレジスタ BAR1 の
メモリベース+0x3d4 / +0x3d5 を計算した16進リテラルに置換してから使う (0x3d4 は既存の診断の cur_vga_base、misc bit0 が 0 なら 0x3b4)。
I/Oポート0x3d4/5と MMIO の応答を同一視しない。

```text
probe stop
probe reset
probe list
probe add tri.screeninit.post idxmmio8 INDEX_PHYS DATA_PHYS 0x39
probe arm
probe list
```

1. CUI で `hw run -o /host/logs/tri-arm.txt /host/tri-arm.hwb`。
   Trident の1点が登録され ARM 成功、`enabled=1` を確認。
2. `os32gui` で GUI に入り、表示・入力更新を確認する。
   GUI 端末で `hw run -o /host/logs/tri-drain.txt /host/drain.hwb`。
   点が `enabled=0`、1回・1項目で `rc=OK restore_rc=OK`。
3. 同じ GUI 端末で `tri cpu32` を実行しシリアルの
   `TRI phase=cpu32_pci_original ... cr39=...` と採取値を照合する。
   最初の original 値を使い、後続の no_burst 値と取り違えない。
   同診断の restore 結果も保存する。診断の既存の失敗はそのまま記録する。
4. drain を別の -o で繰り返し採取行0件、CUI へ戻って再び GUI に入り、
   STOP のままの drain でも0件と表示不変を確認する。

Trident の CR 対の根拠は
[Trident 設計の資料表とアクセス契約](../../docs/decisions/realhw/TRIDENT_FULL_DESIGN.md)、
`regs.c` の MIT XFree86/vgaHW 出典、`trident_command_test.c:cpu32` の CR39 読出し。
BAR1 の CR 対は同一ページ。選択・復元以外の書込み、SR の bank 切替、
DAC/Attribute の副作用がある対は採取項目に含めない。

## 証拠と判定

各機種の RESULT.md に版、構成、解決した物理番地、実行コマンド、時刻、戻り値、
ARM/LIST、STOP/DRAIN の -o ログ、既存シリアル診断、画面観測、項目ごとの
expected/actual を置く。不一致・点未通過・読出し/復元失敗は合格にしない。
実機は PM がノートに依頼して観測を判定する。未実施欄をホスト試験で埋めない。
