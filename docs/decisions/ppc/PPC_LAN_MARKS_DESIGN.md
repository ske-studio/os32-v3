# PPC_LAN_MARKS_DESIGN — PowerPC の LAN の serial の印と受入の自動化 (段 P29〜P32)

> 状態: **現行 (2026-11-11)** — serial に足す LAN の行 (`LAN STATE`・`LAN OFF code=`) と、QEMU の meter・変異・受入・全体 (段 P29〜P32) の形。全体の設計は PPC_LAN_DESIGN、service の境目は PPC_LAN_SERVICE_DESIGN。段の状態は票 TASK_PPC_LAN が持つ。末尾の「確定」の節が本文より優先。

> 出所: ~/os32-tmp/run/ppc-lan-m1/M1.final.md (gpt-6-astra 起草、Opus 5.5 第 1 回で条件付き可、PM が F1〜F11 を反映)。中身は変えていない。


**M1：LAN の印と P29〜P32 の受入 (final)**

- 前提はシングルタスク・協調切替・整数演算。P24/P25 境界確定を優先し、§6 の Q 欄は以下の観測へ置き換える。
- 本追記は設計案。保存・preflight・ビルド・試験・変異・lint・QEMU 実行は未実施。

**1. serial に追加する LAN 行の全一覧**

|形（末尾は CRLF）|出す時点・数値の意味|
|---|---|
|`LAN STATE state=<数値> link=<数値>`|service の状態変化後に1本。state は 0=OFF、1=RESETTING、2=PREPARE、3=PUBLISH、4=PHY_WAIT、5=RUNNING、6=QUARANTINED。link は state=5 のときだけ1、他は0。|
|`LAN OFF code=<符号付き十進数>`|LAN 無効化1回につき1本。code は既存 lan_rc：−1=引数、−2=装置なし、−3=範囲、−4=期限、−5=I/O、−6=未対応、−7=使用中。0・1は出さない。|

- 初期 OFF の状態行は出さない。無効化して OFF へ移る場合は OFF 行だけで表し、STATE の代替とする。隔離は STATE の state=6 で表す。
- start が出した OFF 行を P25 が重ねて出さない。原因は数値だけで表し、既存の禁止語を含む名前は出さない。
- §6 の TREE・CAPTURE・PCI・ALIAS・DMA・CLOCK・RESET・RING・MAC・PHY・TX・RX の個別 serial 案は採らない。状態行、既存の模型試験、QMP、実 frame で確かめる。
- LOG・LOSS・WIRE・MONITOR・HOST・LAN BUDGET・IMAGE の追加 serial 案も採らない。保存、線上統計、診断応答、件数試験、link map を証拠とする。
- §7 の文字列による追加エラー行は廃止し、上表の行とホスト側の判定理由を使う。既存 KBOOT の9印と検査は変更しない。

**2. 起動とホストの順**

- meter は `ppc_lan_host.py` の製品 `UDP`・`Host` を利用し、ホスト受信口を bind → capture 開始 → QEMU 起動の順にする。CLI の起動後に固定時間眠る方法にはしない。
- ホスト口 H は `127.0.0.1:0` に bind して取得し、その socket を保持する。QEMU 口 G は別の一時 socket で空き port を確保する。
- port 選定から G の引渡し・QEMU 起動完了までを実行間の共通ロックで直列化する。G の予約だけを起動直前に閉じ、H は閉じない。7450・7455 は別の組・保存先を持つ。
- 起動失敗は受入エラーとする。G の bind 競合を変異検出に数えない。
- ISO・QMP・128M・`-smp 1`・7450/7455 を維持し、NIC 引数は次とする。
  `-nic none -device sungem,netdev=n0,mac=52:54:00:12:34:56 -netdev socket,id=n0,udp=127.0.0.1:H,localaddr=127.0.0.1:G`
- ホスト側は `--udp 127.0.0.1:H,127.0.0.1:G --mac 02:00:00:00:00:01 --peer 52:54:00:12:34:56` 相当。user net は使わない。
- S1 で nonce 入りの60B未満の試験 frame を双方向に流し、送信長・受信長・byte を保存する。通らなければ送信側で60Bまで0詰めして再測定する。
- PLN1 は最短でも94Bなので、この確認のために PLN1 codec や通常送信へ分岐を追加しない。

**3. meter の完了と照合**

- 通常受入は、serial の9印・RUNNING、既存 QMP 継続観測、ホスト保存、5診断のすべてを必要とする。
- serial の ALIVE を見ても止めず、QEMU を再開して受信を続ける。「ホストの保存に最後の印が届く、または期限」まで待つ。
- 最後の印は ALIVE 行の末尾 CRLF。保存待ち期限は serial で同行を確認してから5秒、run 全体は起動から60秒（指定 timeout が短ければそちら）とする。
- 最後の印が保存され、診断と QMP 観測も完了したら終了する。期限到達は未完了であり、成功や変異検出に数えない。
- 通常起動では最初の `KBOOT OF-CAPTURE` から ALIVE 末尾まで、途中の LAN 行を含む serial A の raw bytes と保存 `.log` の対応部分を完全一致で照合する。改行変換・decode・行の抽出で差を消さない。
- 通常起動は seq の穴・重複・逆転なし、drop_records/drop_bytes=0を要求する。ALIVE 後の追加行はこの比較範囲外とする。
- 欠落試験は既知の入力 byte 列・保持分・捨てた分と照合する。seq の穴、累積 drop_records/drop_bytes、uncertain_tx は別々に記録し、加算して「総欠落数」にしない。
- 末尾の欠落試験は次の BEACON の next_seq と累積値まで観測する。期限まで届かなければ末尾は不明とし、完全配送と判定しない。
- BEACON で boot を得たら `Host.request()` に下表の文字列を1件ずつ渡し、`receive()`・`expire()` を回す。CLI 使用時は同じ文字列を stdin へ流す。各応答期限は既存の500ms、自動再送なし。

|要求|応答の確認|
|---|---|
|`status`|`response <id> OK u32=...`、17語、state=5、採取済み PHY ID。|
|`clock`|`response <id> OK tick=... tb_hz=... tb=... us=...`、24B、tb_hz が起動値と一致。|
|`memory`|`response <id> OK base=... size=...`、32B、admitted/image/HPT/DMA が像・QMP と一致。|
|`read-diag 128 28`|`response <id> OK <hex>`、boot_id 16B・自MAC 6B・peer 6Bの一致。|
|`peek <像内の不変領域VA> 256`|`response <id> OK <hex>`、ELF から選んだ読み取り専用256Bと一致。|

- 全応答で boot・nonce・request_id・opcode の対応を製品ホストで照合する。動くカウンタを異なる採取時点間で完全一致させない。

**4. sandbox と完了条件**

- AF_INET socket 作成の権限制限時だけ、LAN 無しの QEMU で9印と既存 QMP 観測を完了し、別行 `SKIP LAN socket rc=3` を出して終了値3にする。既定 NIC も `-nic none` で外す。
- `kboot_accept` は3を伝播し、`kboot_all` は他の検査を続け、LAN 未実施を別行と結果 JSON に残す。実際の検査不合格があればその非0値を優先し、未実施だけなら3を返す。
- 実装者の完了は該当像のビルド、対象 H の `--mutate`、lint、LAN 無しの観測と rc=3 の証拠まで。UDP 受入済みとは報告しない。
- PM が同じ版を sandbox 外で7450・7455について実行し、保存・診断・変異を完了する。時間測定の受入は負荷を抑え1本ずつ実行する。

**5. P29〜P32 の受入と変異**

|段|受入・到達証拠|変異と期待する判定|
|---|---|---|
|P29 run / H29|A7 の nonce 付き要求・応答、A8 の raw 保存、A10 の5診断、A11 の ALIVE と QMP tick/TB±5%・OF増分0。LAN 無効像は OFF 行と9印の継続。|保存比較削除＋1 byte 不一致 fixture → `H:A12 false-accept`。ALIVE 後の保存遅延 fixture で早期終了を検出。|
|P30 mutations / H30|既存の停止契約と、LAN の OS 継続契約を分ける。各変異に一意 anchor・直前到達行・期待判定を登録。|別理由、compile エラー、起動失敗、期限切れを runtime RED に数えない。到達行照合削除を H30 で検出。|
|P31 accept / H31|両 CPU の正常像・LAN 無効像・登録した LAN 変異。A12 の link map、DMA収容、禁止 symbol、x86 build_compare の証拠を揃える。|片 CPU の省略・保存照合の省略を fixture で検出し、`H:A12 false-accept`。|
|P32 all / H32|既存段と LAN 受入を集約し、PM が外で全体を完走。成功・不合格・rc=3を保持。|rc=3を成功へ変換する変異 → `H:A12 skip-as-pass`。|

- LAN guest 変異の契約例：未知 PHY は `LAN STATE state=4 link=0` → `LAN OFF code=-6` → ALIVE 継続。停止確認不能は state=1 → state=6 → ALIVE 継続。新しい serial 行は足さない。
- A7 の OWN 反転は製品模型で `H:A7 rx-owned`、A8 の満杯時加算削除は `H:A8 loss-count`、A10 の範囲拒否削除は `H:A10 read-range`、A11 の RX 予算削除は `H:A11 budget`。各 fixture は対象処理への到達を記録する。
- QEMU 側は state=5 を到達証拠に、A7 の一致した nonce 応答、A8 の保存、A10 の `response <id> RANGE` と正常5診断、A11 の flood 中の QMP 観測を残す。無応答だけを原因特定済みの RED にしない。
- A11 の件数上限 TX8/RX8/LOG4/RSP1/状態1は H24 で受ける。QEMU の250usは記録とし、MIF走査・初回DMA初期化を別記する。実行時の時間予算検査は足さない。
- 断線は S1 で QMP `set_link` が PHY に反映するか実測する。反映する場合は state=5→1→…→5、保持ログ・欠落統計・tick 継続を確認する。反映しなければ断線・再接続は H24 の模型だけで受け、その範囲を記録する。

**6. 未決と PM 決定の候補**

- ユーザー判断が必要な未決は0件。既存の線上形式・届く分だけの保存・5診断を維持することを推奨する。
- PM 決定の候補：保存待ち5秒の実測調整、port 引渡しロックの置き場、PEEK の不変領域、短 frame の詰め物、断線の QEMU 適用可否。まず本案の値と S1 の観測を採用する。


**確定 (2026-11-11、Opus 5.5 の第 1 回の条件付き Approve を PM が反映。本文と食い違う所はこの節が優先)**
- F1: sandbox の rc=3 は、LAN 以外の項目 (正常 9 印・負の像・既存変異) を全部終えてから `SKIP LAN socket rc=3` を出して 3 を返す。PASS 行は出さない。
- F2 (PM 決定): sandbox は既定 NIC (user net) のまま変えない。「LAN 無効像」は別に組まず、同じ像を `-nic none` で 1 回起動する (gmac_ready=0 → `LAN OFF code=-2`)。
- F3: 共通ロックは置かない。`UDP('127.0.0.1:0,127.0.0.1:G', mac)` を作り H は `io.sock.getsockname()[1]`、G は一時 socket で番号を得て QEMU 起動の直前に閉じる。G の bind 競合は起動失敗 = 受入エラー。§2 の「G の bind 競合を変異検出に数えない」は削る。
- F4: 60B 未満の frame の確認 (§2 の 2 行) は削る (PLN1 は 94B 以上)。
- F5: QEMU の変異は guest の 2 件 (未知 PHY → `LAN STATE state=4 link=0` → `LAN OFF code=-6` → ALIVE、停止確認不能 → state=1 → state=6 → ALIVE) と meter の fixture だけ。A7〜A11 の変異は各 H (H19/H20/H22/H24) にあるので kboot_mutations に載せない。§5 の `response <id> RANGE` は削る。
- F6 (PM 決定 = (b)): QEMU の flood は作らない。A11 の flood 中の tick/TB±5% は H24 の模型だけで受け、範囲を記録する (断線と同じ扱い)。QEMU では通常の起動の tick/TB±5% と件数だけ。
- F7: MEMORY の照合は「base=0x0 の組の size = OF-CAPTURE の admitted、HPT の組 = MMU-D の hpt・bytes、DMA の組 = link map の pool」。PEEK は「ELF の e_entry から 256B を objcopy の bytes と照合」に固定。
- F8: 照合範囲は「保存 .log の先頭 byte から、serial の範囲と同じ長さ」の前方一致で、.log の先頭が `K`。CRLF はカーネルが明示して出すので raw の一致。
- F9: §5 P29 の「ALIVE 後の保存遅延 fixture」は削る。`H:A12 skip-as-pass` は設計 §7 の A12 の行に足す。§7 の「LAN ERROR code (例 RESET_TIMEOUT)」は `LAN OFF code=`/`state=6` に書き換える。
- F10: P30 は新しい種類を作らず、既存の KERNEL_MUTATIONS の 6 要素に判定「LAN 未到達」と証拠 `^LAN OFF code=-6$` (または `^LAN STATE state=6 link=0$`) を入れる。
- F11: kboot_all は 1 件でも FAIL/TIMEOUT があれば 1、そうでなく SKIP (rc=3) があれば 3。SKIP は --fail-fast で止めず、TOTAL 行と JSON に SKIP として数える。
- PM 決定: service の enum は `OFF=0 … QUARANTINED=6` を明示して serial の契約を製品に残す (段 P24)。`link=` は DESIGN.final のとおり残す。保存待ち 5 秒は P29 の実測で調整してよい。
- PM 決定 (2026-11-11、P24 のレビュー第 1 回の P2 への判断): 未知 PHY の guest 変異の契約は実装の順に合わせて `LAN STATE state=1 link=0` (start 時) → `LAN OFF code=-6` → ALIVE 継続とする (未知 PHY は段 P17 の決定どおり gem_mif_init が EUNSUPPORTED を返し RESETTING から OFF へ。PHY_WAIT へ判定を移さない)。上の F5 の例の `state=4` はこれに置き換える。

## 実装で決めたこと (2026-11-11、段 P29・P31+P32 の取り込みで PM が決定。上の「確定」より優先)
- QEMU の guest 変異 (F5・F10 の未知 PHY と停止確認不能の 2 件、段 P30・H30) は作らない。ユーザーの「過剰に防御してませんか？」を受け、シングルタスク前提で見直した: その故障の道筋は host の模型の試験 (H14 の reset の期限、H17 の未知 PHY、H24 の OFF と隔離への移り) が製品の TU のまま固定しており、QEMU で重ねる意味が無い。P31・P32 の H (H31・H32) と P33 の H も作らない。
- P29 のレビュー第 1 回の P2 (STATUS の PHY ID の照合を足す) は採らない。PHY ID が採れていなければ RUNNING (`LAN STATE state=5 link=1`) に届かないので、state=5 の照合で足りる。
- P31 と P32 は 1 段にまとめた (最小): kboot_accept は両 CPU の通常の像の LAN の受入が SKIP なら最後に `SKIP LAN socket rc=3` を出して 3、同じ像を `-nic none` で両 CPU 1 回ずつ (`LAN OFF code=-2`)。kboot_all は子の 3 を SKIP と数え、FAIL/TIMEOUT があれば 1、SKIP だけなら 3 (F11)。
- 照合の peer は guest の既定どおり broadcast (READ_DIAG の 150..155 は ff:ff:ff:ff:ff:ff)。kboot_run.py は TMPDIR を要する (前からの形)。
