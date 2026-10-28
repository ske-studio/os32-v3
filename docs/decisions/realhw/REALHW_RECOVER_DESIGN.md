# 実機試験を決まった状態へ戻す設計 r3

> 状態: **現行 (2026-10-21)** — 実機試験を決まった状態へ戻す処理の確定設計。r3 + PM の修正 + Opus 第 3 回。追記は [ADDENDUM_R2](REALHW_RECOVER_ADDENDUM_R2.md)・[ADDENDUM](REALHW_RECOVER_ADDENDUM.md)、段の現在地は票 TASK_REALHW_RECOVER が持つ。
> 出所: `~/os32-tmp/run/realhw_recover/DESIGN.final.md` の確定版を 2026-10-21 に写した。往復の版 r1〜rN は ~/os32-tmp に残る。中身は変えず、リポジトリ内の文書への参照だけリンクに直した。


## r2→r3 の変更

- B1: MMIO quiesce 前の生 config 検証と失敗時の②限定を追加。
- B2: Command を0102h目標のoverride項目とし保存値を再生しない。
- B3: PAUSED と kdebug_call 内だけの期限積算を追加。
- N4: 独立 SIZE_BARS trial 終端にも Command override を適用。
- U1: 当面の立会い・物理復帰を §7 で確定。
- U2: 4a の段別承認を §7 で確定。
- U3: 4a モニタ無し、4b 前の決定を §7 に反映。
- N1: E81E の条件付き許可を §7 と descriptor 契約に反映。

## r1→r2 の変更

- A1: 4a の既存 op を recover 契約へ対応付け、状態機械・許可表・REC ログ・終端 cleanup を recover 側へ一本化する案を記載。
- A2: cold-required 項目の書戻しを 0 件とし、自動復帰を quiesce と②に限定、受入と変異を追加。
- A3: S0 に A0000h の空きテキストセルの書込み・読戻し検査を追加。
- A4: PM 決定として 4a の前提を T1+T2 のみに変更、T3/T6 は並行または後と明記。
- A5: CUI 復帰コードの所属を訂正し、CUI 開始を必須、GUI 中の開始を拒否。
- A6: S2 で GF profile の PMC_ENABLE・1850h・1854h を COLD 値と比較し warm-residue を記録。
- A7: 試験中の abort フレーム以外の受信を破棄し、破棄バイト数を REC に記録。
- A8: indexed descriptor に index 保存・data 復元・lock 復元・index 復元の順序を追加。


- 2026-10-21 / astra 起草。対象は PC-9821Ra266、実装担当 sol・レビュー astra。
- 本稿だけを作成する。実機操作・配備・装置への書込みは行わない。以下の `recover` と API は新設案。
- 原則: **復帰可能と証明した範囲だけ書く。snapshot は装置全体の巻戻しでも、冷間リセットでもない。**
- シングルタスク・協調切替、整数演算、9801 互換なし。[C1]〜[D3]・[HW3] は固定条件。
- パスの略記: `R=/home/hight/os32-v3`、`T=/home/hight/os32-tmp`、`M=/home/hight/.claude/projects/-home-hight-os32-v3/memory`。`R/` 以下は参照のみ。

## 1. 実物の確認

| 既存の物・根拠 (file:line) | 使える範囲 / 限界 |
|---|---|
| `R/tools/rshell_serial.py:236`、`:253`、`R/userland/shell/rshell.c:491` | シリアルでコマンドと EOT を受け渡す。ホスト timeout は待つのをやめるだけで、ゲストの命令を止めない |
| `R/userland/shell/rshell.c:95`、`:113`、`:402` | 既存 watchdog は速度変更後の ack 不着でシリアル設定を戻すもの。装置試験の deadman ではない |
| `docs/archive/v3/TASK_KPEEK.md:9`、`:21`、`:22`、`:25` | KDEBUG=1 の部品、kdebug_call・ops の入口、許可リスト、LAN ダンプがある。peek 自体は読み専用のまま |
| `R/kdebug/kpeek.c:134` | PTE を退避→短い irq_save 内で写像→読出し→PTE 復元→irq_restore。復元失敗は WINDOW。CPU/バス停止から戻す仕組みではない |
| `R/kernel/v86.c:957` | コア側の `v86_cui_display_restore` は kdebug から利用可能で、 graphics STOP・表示許可・text START 等。6Ah/EGC/GRCG 全体の復帰ではない |
| `R/kernel/v86_io.c:240`、`:254`、`:282` | static の `gfx_state_for_os32` が起動時 BIOS flags を用いて EGC/GRCG/6Ah、ページ、パレット、カーソルを復元。`R/build/v86-module.mk:8` の v86_io.o として v86.mod 側にあり、共通部品へ切り出す必要がある。`v86_bios_real_lowmem` はコア側 (`R/kernel/v86_snapshot.c:24`) |
| `R/kernel/sys.c:16`、`R/userland/shell/cmd_sys.c:144` | reboot は SHUT0/1→CPU_RESET_PORT。PCI 装置が冷間値になる保証はコードにない |
| `R/userland/system/hsync.c:1745`、`:1770`、`:1801` | 起動 CRC と一致するカーネルを .old に複製・検証・rename。起動不能時の自動選択機能ではない |
| `R/docs/manpages/hsync.1:494`、[docs/08_build.md:137](../../08_build.md) | FD 起動後の .old 巻戻し手順と制約。対の kdebug.bin/sqlite.mod、必要な /sys も同じ版へ戻す |
| `docs/tasks/realhw/PLAN.md:18`、`:51` (2026-10-22 に [RA266.md](RA266.md) §0・§1-1 へ移った) | ノート→SerialFS/hsync→reboot→ver の運用と現在の PCI 実測。LAN 82557 自体の試験では制御経路をシリアルに固定 |
| `M/os32-realhw-31k-default.md:11`、`:14`、`M/os32-realhw-headless-tests-ok.md:15` | 31kHz 既定、立会いの最後はユーザーが GRPH+2+RESET。無応答時は記録して止め、物理操作はユーザー |
| `docs/ROADMAP.md:380` | 外部デバイス実装モデルの文書群に、復帰契約・試験表・実例を組み込む位置づけ |

- 食い違い F1 (1 件): `docs/tasks/realhw/PLAN.md:29` (今は RA266 §1) は 0:12.0 を Creative とする旧記録。`:51` の新しい実測は HighPoint と GeForce 0:13.0。後者を基準とし、毎回 ID/BAR を再照合する。
- 4a は `T/run/gf6200_4a/DESIGN.r1.md` の op・DIRTY・W 行・GF ログを確認済み。§5 の recover 側への一本化案を接続条件とし、4a 側は r2 でこれに合わせる。

## 2. 復帰先と証明

| 状態 | 定義 | シリアルで確認する値 / 証明の限界 |
|---|---|---|
| S0: 安全な 98 CUI | 31kHz、98 テキスト表示、graphics 非表示・アクセス/表示 page 0、外部 GPU の legacy 窓を解放、試験装置の DMA/IRQ を停止。PEGC を備える既定構成を維持するが GUI は起動しない | 新設 `recover status` で profile/version、CUI、DMA/IRQ、route/legacy、page、PTE 復元結果。09A8h 下位 2 bit=1。読めない FF は設定した値を `shadow` と明記。legacy decode 解放の読戻し成功後、A0000h の予約した空きテキストセル 1 つの元値を保存し、元値と異なる試験値を書いて読戻し、一致後に元値を戻して再照合する。98 側へ届かない、復元不一致、検査不能なら FAIL。実表示はユーザー確認 |
| S1: 試験前の装置状態 | 同一起動・同一装置・同一 profile の安全に保存/復元できるレジスタだけが一致。除外した内部状態/VRAM/FIFO は別扱い | snapshot ID/CRC、BDF・vendor/device/revision・BAR、各許可レジスタの `(actual ^ saved) & verify_mask == 0`、未検証数。全体一致とは表示しない |
| S2: 再起動後 | 起動完了、期待版の CUI と通信が成立し、新しい baseline を採取済み | `ver` の Commit/Image CRC・KAPI、kdebug の対、host が観測した起動世代、PCI ID/BAR、S0 の検査。GF profile の PMC_ENABLE・1850h・1854h を同一装置/profile の COLD 記録値と比較し、差があれば `warm-residue` と各差分を記録。COLD 記録なし/読出し不能は unknown として冷間一致を宣言しない。旧 snapshot は破棄（比較用 COLD 記録は保存） |
| COLD: 電源断後の観測基準 | ユーザーの電源断/再投入記録と、その後の初回採取値 | シリアルの値だけでは電源断を証明できない。冷間でも全レジスタが 0 とは限らない |

- S0 と S1 は別の保証。既定の復帰は S1 のうち安全な項目を戻した後 S0 に収束する。S1 が VGA 窓を再び開く値なら S0 を優先し、その項目を `override` と記録、S1 完全一致を宣言しない。
- write-only の設定記録は読戻しの代用ではない。`verified / shadow / unknown / override` を分け、必須項目が unknown なら成功にしない。
- GeForce 冷間観測の PMC_ENABLE=03010000 は記録値であり初期化解除用の命令ではない (`docs/archive/v3/TASK_KPEEK.md:33`)。

## 3. 軽い順の復帰階梯

| 段 | 処理 | 成功条件・次の段 |
|---|---|---|
| ① snapshot/restore | 書く前に quiesce→安全な値の採取→CRC/valid 確定。終端とエラー出口で profile の順序で復元。cold-required の場合は保存値を書き戻さず quiesce のみ（§5） | 安全な readback が一致。復元不能項目は採取前から明示し、失敗時に②、必要なら⑤⑥へ |
| ② 98 画面 | 0FACh を 98 表示へ (下の手順) → 装置の出力/DMA を止め → legacy decode を解放 → 共通 display restore → テキスト再描画 | S0 の検査。映像の物理入力はユーザー。PCI の decode が解放できなければ VRAM を叩かず停止/⑤⑥ |
| ③ シリアル 1 命令 | `recover now <session>` が試験停止→①②→verify を一度の要求で行う。未試験時も S0 profile のみ実行可 | `REC ... result=OK state=S0`。試験中は§4 の専用 abort 受信に同じ要求を渡し、shell の再入実行はしない |
| ④ deadman | 試験命令境界・poll・delay の各刻みで deadline/abort を確認し、超過時に①②を自動実行 | 復帰側にも別の期限・反復上限。ホストの監視は補助で、ゲストの復帰能力を作らない |
| ⑤ reboot / 旧版 | 復帰不能を記録→可能なら FS sync→reboot。新起動の ver を待ち、S2 と S0 を照合。版の問題なら旧一式へ戻す | reboot の EOT 不着は既知。起動の期限で停止、無限再起動しない。CPU リセットの `sys_reboot` (`R/kernel/sys.c:16`) で PCI RST# が出る保証はなく、GPU 状態が残る可能性がある。S2 の COLD 比較で warm-residue を記録し、装置不調が残れば⑥ |
| ⑥ 無応答 | CPU/バス停止、割込み禁止で処理不能、MMIO が帰らない場合は最後のログを保存して止める | ソフトでの復帰は保証しない。ユーザーの物理操作で再投入し S2/COLD を再採取 |

- ①②の内部順序は固定の逆順書戻しにしない。装置ごとに quiesce→decode/出力の安全化→依存レジスタ→画面→verify を定義する。cold-required では依存レジスタの書戻しを省き、§5 の quiesce と②だけを自動実行する。
- quiesce 前に生の PCI config で Command.Mem=1 と BAR0 が snapshot と一致することを確認する。不一致・確認不能なら MMIO quiesce を省き FAILED に固定し、独立に安全を確認できる②（98画面）だけを行う。decode 解放を確認できなければ②でも VRAM に触れず停止する。
- GeForce は PBUS 1850h を 0 にして出力を停止し読戻し、1854h を 0 にして legacy を解放し読戻す。BAR0 の decode を先に閉じて自分の復帰口を失わない。
- 4b の開始条件は **1854h←0 の確認を先に、1850h←1 は後** (`T/run/gf6200/PLAN.final.md:88`)。保存値 1854h=1 を機械的に戻さない。
- **(PM 修正 2026-10-21、ユーザー情報) GeForce の映像は「抜いちゃダメボード」経由で 98 の切替回路の RGB-IN につながる** (1 本のモニタ、`docs/tasks/realhw/TASK_GEFORCE6200.md:61`)。切替は 0FACh (bit1 = 内蔵/RGB 側 か 98 か、bit0 = RGB-IN スルー か 内蔵 [推]、`docs/tasks/realhw/TASK_TRIDENT_DRIVER.md` §3-7)。r2/r3 の「別コネクタ・0FACh は GeForce に使わない」は取り消す。
- **②の 98 画面復帰は全装置共通で、最初に 0FACh を 98 表示へ戻す**。0FACh は [READ/WRITE] (`R/docs/hw/undocumented/io_wab.md:345-366`、bit7〜2 は常に 1)。手順: 読む → bit1..0 (mask 03h) が 00b なら書かない → 違えば 00h を書いて読み戻し、mask 03h で 00b なら verified、違えば S0=FAIL。Trident の Disable が 0FACh←00h を最初に書く順と同じ (`docs/tasks/realhw/TASK_TRIDENT_DRIVER.md:356`)。I/O ポートで VGA 範囲外なので、MMIO quiesce を省く失敗経路でも書ける。
- **読み戻しはモニタに何が映るかを証明しない** (Ra266 の割当ては [推]、LSI 統合)。経路の最終判定は**ユーザーの目視**。A0000h の往復が確かめるのは CPU→98 テキスト VRAM の到達だけ。
- **Ra266 で 0FACh を初めて書くのは T5 の中の明示承認の 1 回** (Trident T9 の関門と同じ扱い、戻らなければリセット)。それまでは読みだけで、bit1..0≠00b なら書かずに FAILED として止める。GeForce を映す値 (RGB-IN 選択) は 4b の前に確かめる項目で、recover は書かない。
- 共通画面処理は `gfx_state_for_os32` の 6Ah/EGC/GRCG 等と text START を統合して共有。V86 を起動せず、v86.mod 不在でも使える最小の HAL/kernel 入口にする。`PEGC_FF2_STD_GFX` を書くため PEGC GUI の既定構成へは戻せない。CUI での開始を必須とし、GUI 中（CUI を確認できない場合を含む）の開始は書込み前に拒否する。PEGC GUI 復帰は別試験。
- 31kHz を試験中に変更しない。09A8h だけ書いて周波数を変えるのは同期再設定を欠くため不可 (`R/docs/hw/undocumented/io_disp.md:1865`)。

## 4. deadman の到達範囲と置き場

| 置き場 | 責務 |
|---|---|
| `kdebug/recover.c`・profile 群 (新設) | KPEEK と並ぶ ops。固定長 snapshot、許可表、状態機械、期限、restore/verify。KDEBUG=0 は入口で拒否し書込み 0 件 |
| kernel/HAL の共通部 | 98 表示復帰・短い窓操作・tick・serial の必要最小限の import。汎用の任意番地 write API は作らない |
| shell `recover` (新設) | `status / now <session>` の引数と結果。既存 kdebug_call 経由。snapshot/arm は試験セッション開始と一体化し利用者の実行忘れをなくす |
| ホスト `tools/rshell_recover.py` (案) | /dev/ttyUSB0 を単一所有し session/seq/CRC を保存、期限監視・abort 要求・起動検証。LAN は大量ログと配備用、制御の必須依存にしない |

- 状態機械: IDLE→SNAPSHOTTING→ARMED→RUNNING、段間はRUNNING→PAUSED→RUNNING、終端はRUNNING/PAUSED→RESTORING→VERIFIED または FAILED。失敗を IDLE に戻して次の試験を許すことはしない。
- 単一セッション、入れ子禁止。snapshot 完成前に試験書込み禁止。profile CRC・起動世代・装置 ID/BAR 不一致、再起動後の再利用は拒否。
- 初版は既に停止した装置を試験開始条件とする。quiesce が設定を変更する profile では、その変更対象の安全な元値を先に保存し、準備途中の失敗も cleanup 対象にする。稼働中 LAN/PCM の透過的な中断・再開は保証しない。
- `trial_begin(profile, plan_hash, budget)` が snapshot+arm、`trial_step` が有界な 1 操作、`trial_end(reason)` が全出口共通 restore。成功・失敗・中止のすべてが同じ cleanup を通る。
- PREPARE と各 RUN_ONE の正常帰還時は kdebug_call の外で PAUSED とし、承認待ちの期限を止める。PAUSED 中の復旧要求は `recover now <session>` だけを受け、次段の承認済み kdebug_call で RUNNING に戻す。trial の期限は kdebug_call 内の時間だけを積算し、承認待ちを数えない。 recover の status 等の通常要求は PAUSED 中には受けない。復帰処理にも独立の有限予算を適用する。
- 期限は整数 tick 差分 (wrap 安全、予算は半周未満)、全体と opcode ごとの上限・最大命令数・poll 回数を持つ。profile に実測根拠付き上限を固定し、無期限指定を認めない。
- IRQ 内は期限超過フラグを立てるだけ。復帰の MMIO/表示/ログを IRQ/NMI 内から行わない。irq_save は窓の最小操作だけで、script 全体を囲まない。
- 各安全点で固定長の abort 要求だけを受信し session を照合、復帰へ移る。受信量にも上限。試験中は有効な当該 session の abort フレーム以外（通常行・不正/古いフレーム）を捨て、破棄したバイト数を session ごとに累計して `REC ... phase=receive event=DISCARD discarded_bytes=...` と最終行へ記録する。通常のコマンド解釈器を呼ばず、二重 restore は状態で抑止。
- cooperative deadman が救えるのは安全点へ戻る命令だけ。停止した tick、無限ループ、同期バス待ち、kernel fault の全経路を救えるとはしない。これらは⑥へ。
- ログ送信も有界にし、送信詰まりで cleanup を妨げない。失われた行数を保持。ホストは連番欠落や最終 verify 不着を成功と扱わない。

## 5. 書込み試験との契約・安全規則

| 項目 | 必須の約束 |
|---|---|
| register descriptor | 装置/版、BDF・device/subsys、BAR・相対 offset、幅、R/W、read 安全性、write/verify mask、許容値・phase、head/port/index、依存順序、停止条件、根拠 file:line を固定。4a のレビュー済み W 行もこの単一 descriptor に収容し、別の許可表は持たない。indexed 型は操作前に元の index と安全に読める lock/data を保存し、必要な解錠（CR1F・CR11 bit7）→data 復元→lock 復元→元の index 復元の順。未分類は拒否 |
| 保存禁止 | read-to-clear・FIFO pop・DAC index を進める読出し・属性 FF を変える読出しは採取しない。必要なら既存 driver の shadow を出典付きで使う |
| 書戻し禁止 | W1C/W0C status、IRQ ack、reset/trigger、doorbell、FIFO、予約 bit、EEPROM/flash、DMA 起動を生の保存値で再生しない |
| mask の扱い | RW だけ安全に保存/復元。W1C 混在は単純 RMW 禁止、許された幅と非作動値を profile に定義。例: PCI Command と Status をまとめた DWORD 書戻し禁止 |
| DMA/IRQ | LAN の queue・PCM の DMA は先に停止・完了確認。古い DMA pointer の再生で転送再開しない。descriptor/RAM/所有権まで保全できなければ再初期化専用 profile |
| 復元不能 | PLL/PFB 初期化、VRAM 破壊、装置内状態は逆操作が証明できなければ `cold-required`。cold-required 項目への保存値の書戻しは **0 件**。自動の①は §3 の生 config 検証を満たす場合だけ GF の quiesce（1850h=0・1854h=0・PMC 割込み=0）のみとし、②へ進む。PLL/PFB の保存値は再生せず S1 成功を返さない。安全なアクセスすら完了しなければ FAILED として停止し、⑤⑥を手順に組み込む |
| restore 自体の失敗 | 最初の失敗・段・番地を保持し FAILED に固定。同じ危険アクセスを反復しない。独立性が証明された 98 復帰だけ継続。窓復元失敗も成功扱い禁止 |
| 再試行 | 同一 session の二重要求は既存結果を返す。部分失敗後の再開は checkpoint/冪等性が証明された操作だけ。それ以外は再起動・新規採取 |

4a op と recover の対応（4a r3 と共通の契約）:

| 4a の op | recover 側の契約 |
|---|---|
| INSPECT | PROM 取得・静的検査のみ。trial を開始せず、許可された読取りだけを共通 descriptor で検査 |
| SIZE_BARS | 測定専用の有界 trial_begin→trial_step→trial_end。下記の BAR 保存・復元を局所 cleanup として行い、終端は recover が所有 |
| PREPARE | trial_begin(profile, plan_hash, budget)。session・snapshot・arm は recover の単一状態機械が所有 |
| RUN_ONE | 1 opcode ごとに trial_step（安全点）。内部 poll/delay も安全点を持ち、main 正常終了だけ次の番号へ進む |
| VERIFY | trial_end(normal)。recover が唯一の終端 cleanup と verify を実行 |
| ABORT | trial_end(abort)。エラー・期限超過も同じ入口へ集約し、重複要求は既存結果を返す |

- Command は descriptor の override 項目とし、snapshot の0106hは記録だけで書き戻さず、目標0102h（BM=0）を生 config の16bit読取りで verify する。SIZE_BARS の独立 trial 終端にも適用する。BAR復元失敗時は decode を止めたまま FAILED とし、目標値のために Mem を再有効化しない。 4a の全終端で元値と目標値・検証結果を override として記録し、元値との相違を未検証にしない。
- N1 の descriptor は main3/E81E の SR01 bit5 解除だけを条件付き許可し、cfg50h/54h と PBUS1850h/1854h の0の読戻し・保持を検査する。
- 4a の DIRTY は recover の FAILED または cold-required による再実行禁止を示す派生表示とし、独立した復旧状態機械を持たない。4a は実行位置などの解釈器状態だけを持つ。局所 PTE/IRQ/index の後始末を終えてから recover の終端 cleanup を一度だけ呼び、その後に 4a が GPU を再設定しない。cold-required は新 session だけで解除せず、電源再投入記録と COLD 再採取を必要とする。
- 4a 側は実行前に分岐先を含む write-set、BAR sizing、PLL/PFB、VRAM test の範囲を確定。全 write を descriptor へ照合し、未知 opcode/動的番地は書く前に拒否する。
- BAR sizing は quiesce、PCI Command の該当 decode 停止、BAR 保存→測定→復元→照合→Command=0102h（override）の独立した有界区間。サイズ測定値を snapshot の元値にしてはいけない。
- 実験 VRAM は予約範囲のみ。保存で戻すか破棄してよいかを plan に明記。通常 RAM の固定領域へ snapshot を置き、試験装置の VRAM や LAN 越し保存だけに依存しない。
- 4a の PLL/PFB/メモリ初期化は当初 `cold-required` 推奨。①の quiesce は §3 の生 config 検証成立時だけ行い、不成立なら FAILED として②だけを安全条件内で行い、cold-required 項目は書き戻さない。4a 終了状態を 4b に残す抜け道は設けず、後段も承認された init を含む単一セッションにする。
- 設定書込みの直前に `REC sid=... seq=... phase=restore event=BEGIN step=... addr=...`、後に `... event=END rc=... elapsed=...` を各 1 行。採取・停止・窓・画面・verify・reboot も同形式。4a の `GF PRE` は `REC ... phase=gf_pre event=BEGIN main=... pc=... opcode=... exec=...`、`GF BUS` は `REC ... phase=gf_bus event=BEGIN pc=... bar=... off=... width=... old=... new=...` に統一し、後者の END に readback/rc を残す。sid/seq は recover が一元採番し、独立した GF 行を出さない。命令前・書込前の行は期限付き送信完了を必須とし、失敗時は LOG_FAIL で試験を止め共通 cleanup へ進む。
- 最終行は `result=OK|PARTIAL|FAIL target=S0 verified=... unknown=... override=...`。試験結果と復帰結果を別フィールドで残す。host 側に版・plan/profile hash・snapshot CRC・最後の開始行を保存。
- [HW3] 根拠: 98 復帰の資料と FreeBSD 対照は `R/kernel/v86_io.c:250`。NVIDIA 公開 MIT 実装 `T/refs/geforce6200/xf86-video-nv/src/nv_driver.c:2290` は選別した保存状態を順序付きで復元する前例で、Ra266 の冷間復帰保証ではない。
- PBUS の意味は `T/refs/geforce6200/envytools/docs/hw/bus/pci.rst:50`、`:60`。ライセンスは `T/refs/geforce6200/INDEX.md:6`、`:7`。専有 driver/ROM x86 コードは解析しない。新しい装置 profile はメーカー資料と BSD/MIT 実装の照合を実装開始条件とする。

## 6. 無応答時のハード候補 (導入・操作はユーザー判断)

| 候補 | 到達範囲と確認事項 | 推奨 |
|---|---|---|
| ユーザーのリセット/電源操作 | 現行運用。電源再投入後は COLD/S2 を再採取。リセットだけで GPU 内部状態が消えるとはしない | 初版の標準。無人試験で固まれば記録してユーザー待ち |
| **POWER GOOD (PG) を GND に落とすコールドリブート** (PM 追記 2026-10-21) | 後期 PC-9821 の ATX 類似電源 (V200/Xv13/Ra43 など) で電源コネクタ 8 ピン (白) の PG を GND に落とすとコールドリブート ([98epjunk](https://98epjunk.shakunage.net/pow/reset_sw.html))。Ra18/Ra20/Rv20 は VRM コネクタにも PG が出る (ピン番号は資料間で食い違う)。**Ra266 での適用は未確認** (同世代で見込みは高い)。電源投入相当なので PCI RST# まで届く見込み [推]。PG はオープンコレクタ [推] (同ページの記述、回路図で未確認) | 第一候補。手押しボタン (立会い) を先に、遠隔はフォトカプラ/USB リレーで PG–GND を有限パルスで短絡 (運用中のシリアルの DTR は流用しない)。導通確認・取り付けはユーザー。PG 後の起動は S2 + warm-residue の扱い (レジスタが COLD と一致しても GPU 内部状態の復帰は証明しない、l.59)。cold-required の解除条件に足すかはユーザー決定。筐体改造を伴う (下のスマートプラグの「改造を伴わない候補を優先」とは、立会いの手押しボタンを先にすることで両立させる) |
| ノート→USB リレー→RESET 接点 | 対象 LAN に依存せず CPU が止まっても外から操作できる構成候補。Ra266 の外部 reset 端子・内部接点の有無/電気仕様は今回の資料では未確認、AT 機の端子を仮定しない | 回路資料とユーザーの現物確認後に検討。絶縁した無電圧接点、有限パルス、USB 再接続時に勝手に押さない構成を要件にする |
| ノート→スマートプラグ/認証済み電源制御器 | 装置の電源を切れる。ローカル制御、PC と独立した管理網、定格/突入電流、切断時間、AC 復帰時の本体起動、停電復帰設定を実機確認 | 遠隔化するなら筐体改造を伴わない候補を優先。手作り USB リレーで商用電源を配線しない。型番購入は別途選定 |
| USB リレーで電源ボタン接点 | ボタン動作・保持時間・主電源切断の有無が機種依存。RESET と別に資料確認が必要 | スマートプラグが使えない場合の次候補 |
| NMI 注入 | NMI は CPU の割込み経路であり電源/バスリセットではない。Ra266 の注入端子・配線は未確認。バス停止からの回復を保証しない | 初版は採用しない。将来、最小レジスタ採取の診断用途に限定 |

- 外部電源制御の例としてローカル網での動作と power-on 設定は [Shelly 公式ガイド](https://www.shelly.com/blogs/documentation/shelly-plug-s-web-interface-guide) を確認。Ra266/日本向け適合や購入推奨の根拠にはしていない。
- NMI の位置づけは [Intel SDM Vol.3A](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf) の割込み/HLT の説明を参照。Ra266 の外部配線の根拠とは別。
- 遠隔化を導入しても初版はユーザーによる明示操作のみ。単なるシリアル timeout から自動電断しない。ext2 非ジャーナルのため試験前に sync、試験中はディスク更新・hsync を並行しない。

## 7. 受入と段分け

ユーザー判断の未決は 0 件。決定事項は以下のとおり。

- **U1・ユーザー決定（2026-10-21）**: CPU/バス停止時は当面ユーザーが実機の前で電源・リセットを操作する。書込み段は立会い時に行う。遠隔電源制御は後で検討する。
- **U2・ユーザー決定（2026-10-21）**: A1（BAR測定）→A2（準備）→script ごと（main 0・1・2・3）→A4（VRAM確認）の順に、各結果を見て次の段を承認する。この方針決定は個々の書込み実施承認を代替しない。
- **U3・ユーザー決定（2026-10-21）**: 4a は GeForce 用モニタ無しで行い、映像出力用モニタは 4b の前に決める。
- **N1・ユーザー決定（2026-10-21）**: main3 の E81E に限り SR01 bit5 の解除を 4a で許す。cfg50h=0・54h=0（PBUS1850h・1854h）を読み戻し、実行前後を含め保持することが条件。安全の根拠は cfg50h=0 で GeForce の出力が止まること（envytools bus/pci.rst:50–53）と、4a の間は 0FACh が 98 表示のまま（RGB-IN を選ばない）であること。

- **PM 決定（N2）**: 容量不明でも BAR1 先頭 1KiB の最小試験を行う。その後の容量探索・別設計への技術的進路と Haiku のライセンス確認は PM が担当し、現行承認範囲のまま探索へ切り替えない。
- **PM 決定（R10・別名不応答）**: 54h=0 で別名が応答しなければ A2 を停止し共通終端へ進み、4a を保留して設計し直す。54h=1／I/O decode 有効化で迂回しない。
- **PM 決定（R10・1854h 書込み）**: 実 ROM に該当書込みは無い。別 ROM/展開 write-set に現れた場合の条件付き記録とし、descriptor の phase・値を照合して非零または承認外なら静的拒否する。命令の黙示改変・許可拡張はしない。

- **PM 決定（A4）: 4a の前提は recover の T1（契約・状態機械）+T2（98 復帰の共有部・窓）だけ。T3（deadman/host）と T6 は 4a と並行または後でよく、T4/T5/T7 も 4a の開始前提とはしない。** 各書込み実施のユーザー承認は別途必要。

| 段 (sol 実装→astra レビュー) | 合格条件 |
|---|---|
| T1 契約・純粋状態機械 | host fake bus/clock で開始前拒否、全出口 cleanup、session/CRC/版不一致、期限 wrap、PAUSED中の時間非加算・recover now以外の拒否・承認済み次段の再開と残予算、二重要求、保存途中失敗を検査。KDEBUG=0 の書込み 0 件。cold-required の正常終了・中止・期限超過・失敗で当該項目への書戻し 0 件、生 config 検証成立時はquiesceと②のみ、不成立時はMMIO quiesce 0件・FAILED・安全な②のみであることを bus trace で検査 |
| T2 窓・profile・98 復帰 | host で各 read/write/map/unmap に失敗注入、PTE/IRQ復元、独立SIZE_BARS終端でもsnapshot Command=0106hを再生せず0102hをverifyするoverrideを検査。A1失敗でMem=0またはBAR0不一致を注入し、MMIO quiesceが0件でFAILEDとなることを検査。既存 V86 表示 cleanup 試験を共有部移設に追従。GUI 開始拒否、indexed の data→lock→index 復元順、A0000h の空きセル検査と不達/復元失敗の FAIL を検査 |
| T3 shell/host/deadman | 分割受信、古い session、ログ欠落、timeout、送信詰まり、終端不着を検査。通常行と abort の混在・分割で通常行を実行せず捨て、破棄バイト数の REC 記録を検査。安全点での中止が有限時間に cleanup に至る。ホスト timeout 単独を成功としない |
| T4 NP21/W | KDEBUG 有無、CUI/表示復帰、仮想 profile のエラー/期限→S0、シリアル 1 命令、reboot→ver、隔離した像で旧版一式の巻戻し。停止→配備→起動を守る |
| T5 実機・無書込みから | ノートの serial を確認→status と安全な snapshot のみ (0FACh は読みだけ)→同値 restore→S0。続けて**明示承認の 1 回**で 0FACh←00h の書込みと読み戻し、98 表示をユーザーが目視 (戻らなければリセット)。各 profile を単独試験、before/after の mask 比較、繰返しでも漂移なし |
| T6 実機・承認済み書込み | 復元可能な最小変更→手動 abort→短い期限超過→命令途中の意図的なソフト失敗→①②を確認。4a と並行または後に実施でき、4a〜4c の各書込みは個別の承認地点に従う。 |
| T7 復旧演習・文書化 | シリアル reboot、旧版一式の復旧、ユーザー立会いの物理復帰を各 1 回。共通契約と装置別記録を外部デバイス実装モデルの材料にする |

- 必須変異: snapshot valid を早める、cleanup を除去、W1C を許可、mask を全 bit にする、restore 順序を交換、PTE 戻しを省く、deadline 比較を反転、stale session を許可、verify 失敗を OK にする、cold-required 項目を保存値で書き戻す。各変異を対応ケースが検出すること。
- sol は関連 host 試験を MUTATE=0 で実行し、変異実行は repo 運用どおり PM のレビュー後にまとめる。今回の設計作業ではコード試験は未実行。
- NP21/W には PCI バスがなく (`docs/archive/v3/TASK_KPEEK.md:32`)、NV44A/82557/内蔵 Trident の電気的な復帰、実バス停止、モニタ入力切替は証明できない。host 模型で証明した順序と実機結果を混同しない。
- 実機の画面受入: decode 解放の読戻し後に A0000h の予約した空きテキストセル 1 つで保存→異なる試験値の書込み→読戻し→元値復元→再照合を行い、98 側への不達・復元不一致・検査不能は S0=FAIL。98 の A0000h テキスト継続、page 0、31kHz、PEGC の既定構成を確認。0FACh の読み戻し (mask 03h=00b) とユーザーの目視で 98 表示を確かめる。ケーブルの抜き差しはユーザー。終了時は **GRPH+2+RESET**、再起動後の ver と 09A8h を記録する。

## 8. 未決 — ユーザー判断 0 件

- U1・U2・U3・N1 は §7 のユーザー決定に反映済み。各段の実施承認は運用上のゲートであり、設計上の未決ではない。PM 担当の技術的選択は §7 の PM 決定として扱う。

- reset 端子の実在、profile の正確なレジスタ集合、時間上限、4a 設計との API 照合は実装側の調査・レビュー条件であり、ユーザーへの仕様判断に転嫁しない。

反論: なし（0 件）。


## PM の修正 (2026-10-21、r3 の後)
- §3 ②: 0FACh←00h を 98 画面復帰の最初に置く (GeForce は切替回路経由、ユーザー情報)。r2/r3 の「0FACh は GeForce に使わない」を取り消し。
- §6: PG リセットを第一候補に追加 (資料は他機種、Ra266 は未確認、導通確認と取り付けはユーザー)。
- §3 ⑤ の F0h による CPU リセット (`sys_reboot`) は PCI RST# を保証しないという r3 の記述はそのまま。


## 確定 (2026-10-21、Opus 第 3 回、PM が反映)
- R1: 0FACh は読む→bit1..0≠00b のときだけ 00h を書いて読み戻す (verified)、経路の判定はユーザーの目視、Ra266 で初めての書込みは T5 の明示承認の 1 回。
- R2: ② の表の順序を 0FACh 先頭に揃えた。
- R3: T5 と画面受入に 0FACh の読み・書き・目視を足し、古い「入力切替はユーザー」を直した。
- R4: PG のオープンコレクタ・RST# は [推]、PG 後は S2 + warm-residue、解除条件への追加はユーザー決定、改造との関係を 1 句。
- R5: 節番号を §3 ⑤ に。
- G1 と同じ N1 の根拠の直し。


## 追記 (2026-10-21、T2・4a S1 の設計の欠けを埋める) — 全文は [ADDENDUM.r2.md](REALHW_RECOVER_ADDENDUM_R2.md)
- astra 起草、Opus 2 回 (REVIEW_ADD.r1.md、第 2 回は Approve)。ADDENDUM.r2.md を本文の一部として扱う。
- PM 決定 A3: 実測までは暫定値で書込み試験を許す (超えたら FAILED に倒れる上限、書込みの各段はユーザー承認と立会い、T5 で実測して置き換え、暫定で走った記録を残す)。
- 送り項目 (Opus 第 2 回の P3、依頼文で縛る): B1 安全点・accessor が非 0 を返した後、operation は資源に触れずすぐ戻る。local_cleanup は冪等で、解放はこれだけが持つ (T1b の試験に入れる)。B2 UART の待ちの実際の分解能が 1 フレーム時間 (1042µs) 以下であること、HAL のどの手段で作るか (T2 の合格条件、host の fake tick で通っても実機の根拠にしない)。
- 追記の続き (2026-10-21): plan の契約は [ADDENDUM.final.md](REALHW_RECOVER_ADDENDUM.md) (r3→r4→PM 確定、Opus 3 回)。ADDENDUM.r2.md と合わせて本文の一部。

## 後の決定 (2026-10-23、PM 注記 — 上の確定本文は書き換えない)

- §6 の表の「POWER GOOD (PG) を GND に落とすコールドリブート」と、往復の記録の PG の項 (第一候補・解除条件) は採らない (ユーザー決定 2026-10-22「つけない」、票 TASK_REALHW_RECOVER §0)。固まったら立会いで手で電源を入れ直す。
