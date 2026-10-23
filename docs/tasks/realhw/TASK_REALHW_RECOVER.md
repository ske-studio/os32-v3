# TASK_REALHW_RECOVER — 実機試験を決まった状態へ戻す処理 (realhw_recover)

> 状態: **実装中 (2026-10-23)** — T1・T1b・T2・T3 は main に着地、T4a は NP21/W で合格。段 4a の S6 (59ba733) で試行の op を結線済み。T4b 以降は未着手で、順序は `TASK_HWDEV` §3-1 (M5 の配備と 31kHz の鍵が T4b の前提)。確定設計は §6 の 4 本。

実機 (Ra266) で装置に書き込む試験 (まず GeForce 6200 の段 4a、`TASK_GEFORCE6200`) の途中で、失敗・中止・期限切れが起きても、決まった手順で「安全な 98 の CUI」(S0) に戻すための仕組み。装置ごとの「何を保存して何を戻してよいか」の表 (profile)、試験の状態機械、期限 (deadman)、98 の画面に戻す共通処理、シリアル 1 命令の復帰 (`recover now`) からなる。

## 0. ユーザー決定

- **起点 (2026-10-21)**: 「実機テストの時に特定の手順でリセットや画面復帰など特定のステータスに戻す処理の設計案を追加で作って。これがあれば安全に実機をテストできます」。この票はその設計 (§6) と実装の段の記録。
- **U1 (2026-10-21)**: CPU やバスが止まったら、当面はユーザーが実機の前で電源・リセットを手で操作して戻す。**書込みの段は立会いのときに行う**。遠隔の電源制御は後で検討する。
- **U2 (2026-10-21)**: 書込みは段ごとに結果を見て承認する — 段 4a では A1 (BAR の測定) → A2 (準備) → script ごと (main 0・1・2・3) → A4 (VRAM の確認)。この方針は個々の書込みの実施承認の代わりにはならない。
- **U3 (2026-10-21)**: 4a は GeForce 用のモニタ無しで行い、映像出力のモニタは 4b の前に決める (映像の経路は `TASK_GEFORCE6200` §4「映像の経路」)。
- **N1 (2026-10-21)**: main3 の E81E (SR01 bit5 の解除) だけを、cfg 50h・54h と PBUS 1850h・1854h が 0 のまま保たれる (実行の前後に読み戻す) ことを条件に許す。
- **起動 FD に kdebug を載せない (2026-10-21)**。recover は kdebug の部品 (KDEBUG=1) の中にあるので、FD で起動した状態では使えない前提で手順を組む。 理由: 戻す処理が kdebug.bin に入って 64.6KB → 103KB になり、起動 FD (2HD) の空きが下限 64KB を割った (71KB → 33KB)。kdebug は開発用で FD (レスキュー兼インストーラ) に不要、FD からインストールした実機は hsync boot で入る。CD には残す。詳細は [08_build.md](../../08_build.md) の「起動 FD と MINIMAL」と `TASK_DBGREF` §3-9。
- **PG リセット (§5) は付けない (ユーザー決定 2026-10-22「つけない」)**。固まったときは立会いのときに手で電源を入れ直し、その後に COLD を取り直す。cold-required の解除は「ユーザーの電源の入れ直し」だけ (PG の後をどう数えるかは付けないので決めない)。

## 1. PM 決定

- **A3 (2026-10-21)**: 「実測までは暫定値で書込み試験を許す。暫定値は超えたら FAILED に倒れる上限で、書込みの各段はユーザーの承認と立会いのもと (U1・U2)。T5 で実測して置き換え、置き換えるまでの run の記録に暫定値で走ったことを残す」。暫定の復帰予算は 10 / 18 / 33 秒 (S0 / GF 通常 / GF SIZE_BARS、[ADDENDUM_R2](../../decisions/realhw/REALHW_RECOVER_ADDENDUM_R2.md) §1 の表)。
- **A4**: 段 4a の開始の前提は T1 (契約・状態機械) + T2 (98 復帰の共有部・窓) だけ。T3・T6 は 4a と並行か後でよく、T4・T5・T7 も 4a の前提にしない。各書込みの承認は別。
- **N2**: VRAM の容量が分からなくても BAR1 先頭 1KiB の最小試験は行う。容量の探索と Haiku のライセンス確認は PM が持つ。
- **R10**: 54h=0 で VGA 別名が応答しなければ A2 を止めて共通の終端へ進み、4a を保留して設計し直す (54h=1 や I/O decode の有効化で迂回しない)。1854h への書込みは実 ROM に無い — 別の ROM に現れたら照合して非 0 なら静的に拒否する。
- **T1b を足した**: 27 種の実 opcode の引数配置とアクセス方向の表は 4a S2 の成果物。T1b は合成の opcode (2〜3 種) で表の形と検査の入口を作り、host 試験で契約を確かめる。
- **A1 で GPU 書込みが 0 件なら、①の省略だけでは FAILED にしない** (ADDENDUM r4 の変更 I、PM 推奨)。
- **e18 (PBUS 1850 への 0 書き、出力停止) は REC_LITERAL のまま** (他の RMW は REC_EXPR へ移す。決まった値で縛るのがいちばん簡単で安全、[S2_LAYOUT](../../decisions/realhw/GF6200_4A_S2_LAYOUT.md) の確定 G1)。
- **T4 を T4a / T4b に分けた (2026-10-21)**: T4a は今すぐ見られる部分 (status・KDEBUG=0 の拒否・peek の回帰・シリアル 1 命令・reboot→ver)、T4b は試行の op が結線される 4a の S6 の後 (台帳 [DEFERRED_TESTS.md](../DEFERRED_TESTS.md) の RECOVER-T4B)。
- 0FACh を Ra266 で初めて書くのは T5 の中の明示承認の 1 回。それまでは読むだけで、bit1..0≠00b なら書かずに FAILED で止める。4a は 0FACh を読み書きしない。

## 2. 段

| 段 | 中身 | 状態 | コミット・証拠 |
|---|---|---|---|
| T1 | 契約と純粋状態機械 ([kdebug/recover.c](../../../kdebug/recover.c))、host 試験 check-recover-host | main | 0cf9c28、2e744f3 (astra レビュー P2 ×2) |
| T1b | 共有 descriptor (target/rule/guard/plan/BAR 範囲/opcode layout)、bind と全経路・実行時の依存検査、local_cleanup の接続 | main | a4d37a9、92536c8 (後片付け)、85cda52 (Opus レビュー) |
| T2 | 窓の PTE/IRQ 復元、GF descriptor と Command=0102h の override、98 画面の共通復帰 (0FACh・A0000h のセル検査・GUI 拒否)、暫定予算と ② の予約・poll 計数・UART 1 フレーム待ち | main | 4f8685b、f339944 (astra レビュー)、7e28d26、c76226e |
| T3 | shell `recover status / now <session>`、ホストの [tools/rshell_recover.py](../../../tools/rshell_recover.py)、固定長 abort (10 B)、REC の連番・欠落・破棄量 | main | b14a905、34f7503 (後片付け)、07a9d83。取り込みの検査での試験の直し 02e08d3・a604c91 |
| T4a | NP21/W: KDEBUG=1 の status、KDEBUG=0 の NOSYS、peek の回帰、シリアル 1 命令、reboot→ver | **合格** (2026-10-21、像 53bbee8) | `~/os32-tmp/evidence/2026-10-21/recover_t4a/RESULT.md`。`now 0` は NP21/W が 24kHz 起動のため設計どおり拒否 |
| T4b | NP21/W: `now 0` の PARTIAL と 98 復帰の経路、試行・PAUSED・期限・故障、送信詰まり、旧版一式への巻戻し | 未 (S6 は済。前提: np21w_ini_live.py の 31kHz の鍵 (未)、hwdev.mod の配備 = HWDEV M5 (未)。rshell_recover.py の `--status` と aidebug の口は S6 9108549 で済) | [DEFERRED_TESTS.md](../DEFERRED_TESTS.md) RECOVER-T4B |
| T5 | 実機・書込みなし: status と安全な snapshot → 同値 restore → S0、続けて明示承認の 1 回で 0FACh←00h と目視、各 profile の実測 (A3 の置き換え) | 未 | — |
| T6 | 実機・承認済みの書込み: 最小変更 → 手動 abort → 短い期限切れ → 意図的なソフト失敗 → ①② | 未 (4a と並行か後) | — |
| T7 | 復旧演習: シリアル reboot、旧版一式の復旧、立会いの物理復帰を各 1 回、外部デバイス実装モデルの材料に | 未 | — |

T4b 以降の段の並べ替え (2026-10-22、装置ごとの部分を別の部品に分ける M1〜M5 を先に): 順序は `TASK_HWDEV` §3 が正。

合格条件の全文は [DESIGN](../../decisions/realhw/REALHW_RECOVER_DESIGN.md) §7 の表と [ADDENDUM_R2](../../decisions/realhw/REALHW_RECOVER_ADDENDUM_R2.md) §3。変異の実行は PM のレビューの後 (ROLES §0)。

## 3. 設計者 (astra) が実装の中で決めたことの所在

どれも [ADDENDUM](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md) の見出しの節にある。

- 「§2 追加」4 節・「§3 追加」— plan の入力と寿命、分岐・skip・呼出し・反復と全経路検査、世代をまたぐ依存と実行時照合、EXPR 引数、検証済み BAR 長、T1b と S2 以降の分担 (r4、Opus 3 回、確定 C1〜C3・D1〜D2)。
- 「T1b の欠けの補完」— 他装置・bridge 窓の照合 (`recover_bar_probe`、範囲表 256 件)、opcode layout の型。
- 「T1b の実装で設計者が決めたこと」— bus access の形、profile 版 2、局所 snapshot 32 件、local_cleanup の所有、N1 の 32bit tuple。
- 「4a S1 追補で設計者が決めたこと」— GF 許可表 ([kdebug/recover_gf.c](../../../kdebug/recover_gf.c))、619 候補の分類、別名禁止の範囲。
- 「T3 の実装で設計者が決めたこと」— abort フレーム、部分フレームの寿命、owner の共有、`now 0`、shell の op 3/4、REC の採番、UART、ホストの単一所有。
- 「4a S2 / S3 / S4 / S5 の実装で設計者が決めたこと」— profile 版 3・4、capture、index 保存、skip 中の head、実行器の課金、PLL の内部列、窓の共有 (kdebug_window)、COPY と一時再生の本番 rule、局所 cleanup の順。

## 4. 送り項目 (どの段で直すか)

| 出所 ([ADDENDUM](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md) の見出し / DESIGN 末尾) | 中身 | 直す段 |
|---|---|---|
| DESIGN 末尾「追記」の B1・B2 (Opus の ADDENDUM 第 2 回) | B1 安全点・accessor が非 0 を返した後は資源に触れずに戻る、local_cleanup は冪等で解放の唯一の持ち主。B2 UART の待ちの分解能が 1 フレーム (1042µs) 以下、HAL の手段 | B1 は T1b、B2 は T2 (コミットの記述では 85cda52・4f8685b に入った。B2 の実機の根拠は T5) |
| 「T1b の取り込みで S2 へ送る項目」 | 実行時の index 選択記録が 1 つだけ、依存の線形探索 | 4a S2 (ポート別の記録と bind 時の cache で解消) |
| 「T3 の取り込みで送る項目」 | P2 BEGIN 行の送信失敗で access をしない試験と変異、P3 status の reason の上書き、送信中の期限切れの試験、SCREEN_CALL の step 番号、flock の重複、PAUSED の門と peek の経路 | 試行の op を結線する段 (4a の S6 か T6) の前。PAUSED の門と peek は T4 (T4a で peek の回帰は合格、PAUSED は T4b) |
| 「4a S2 の取り込みで S3 へ送る項目」 | skip 中の head 変更の直書き、表の位置の直書き、53/54 以外の CR44 書込みの拒否、片付け 4 件 | 4a S3 (着地済み) |
| 「4a S3 の取り込みで送る項目」 | DATA の読みを値に依らない検査より後にしない、静的と実行時の予算の数え方、変異の足りない検査 | 4a S4 (着地済み)。本番の opcode_limit・max_polls の固定と境界の試験は 4a S6 |
| 「4a S4 の取り込みで S5 へ送る項目」 | PLL の低入力の分岐の試験と変異、探索の打ち切りを原本 (pllnv04.c) に揃える、書込み列の上限、VBIOS の version.chip=0x44 の確認 | 4a S5 (version.chip は実 ROM で、未確認のまま残る) |

## 5. 実機を戻す手段 (DESIGN §6 の要約)

| 手段 | 届く範囲 | 扱い |
|---|---|---|
| ユーザーの手の電源・リセット操作 | 現行の運用 (U1)。電源を入れ直したら COLD/S2 を取り直す。リセットだけで GPU の内部状態が消えるとはしない | 初版の標準。立会い無しで固まったら記録してユーザーを待つ |
| PG リセット (電源コネクタの POWER GOOD を GND に落とす) | 電源投入と同じなので PCI RST# まで届く見込み [推]。**資料は他機種 (V200/Xv13/Ra43 など)、Ra266 では未確認** | **付けない (ユーザー決定 2026-10-22、§0)**。固まったら立会いで手で電源を入れ直す。(付けると決めていたときの案: 手押しボタンを先に遠隔は後、PG の後の起動は S2 + warm-residue) |
| CPU リセット (F0h、`sys_reboot`) | **PCI RST# が出る保証は無い** — GPU の状態が残りうる | ⑤ の reboot。S2 で COLD と比べて差を warm-residue として記録する |
| 0FACh で 98 表示に戻す | 切替回路を 98 表示へ (GeForce の映像は抜いちゃダメボード経由で同じ切替回路に入る)。読み戻しはモニタに何が映るかを証明しない | ② の先頭、全装置共通。読んで bit1..0≠00b のときだけ 00h を書いて読み戻す。最終判定はユーザーの目視。Ra266 での初めての書込みは T5 の承認の 1 回 |
| USB リレー (RESET 接点・電源ボタン)・スマートプラグ・NMI | Ra266 の端子・仕様は未確認、NMI はバスリセットではない | 後で検討。初版は採らない |

軽い順の復帰の段 ([DESIGN](../../decisions/realhw/REALHW_RECOVER_DESIGN.md) §3):

1. ① snapshot/restore — 書く前に止めて安全な値を採り、終端で profile の順に戻す。cold-required (PLL/PFB など戻せない物) は保存値を書き戻さず、止める (quiesce) だけ。
2. ② 98 画面 — 0FACh を 98 表示へ → 装置の出力・DMA を止める → legacy decode を解放 → 共通の表示復帰 → テキストを描き直す。A0000h の空きセル 1 つで往復を確かめる。
3. ③ シリアル 1 命令 — `recover now <session>` が試験の停止 → ①② → 照合を 1 回の要求で行う。
4. ④ deadman — 命令の境目・poll・待ちの刻みごとに期限と abort を見て、超えたら ①② を自動で行う。救えるのは安全点に戻ってくる命令だけ。
5. ⑤ reboot / 旧版 — 記録して sync → reboot、新しい起動の ver で S2 と S0 を照合。版の問題なら hsync の .old で旧一式へ戻す。
6. ⑥ 無応答 — CPU・バスが止まったら最後のログを残して止まる。ソフトでは戻せないのでユーザーが物理操作で戻し、S2/COLD を取り直す。

試験の前に sync、試験中はディスク更新と hsync を並行しない (ext2 はジャーナル無し)。単なるシリアルの timeout から自動で電源を切らない。

NP21/W には PCI バスが無く通信速度も模擬しないので、NV44A の電気的な復帰・実バスの停止・モニタの入力切替は NP21/W では証明できない。host 模型で確かめた順序と実機の結果を混同しない (T4 の結果を実機の計測として扱わない)。

## 6. 設計の所在

確定設計はリポジトリの `docs/decisions/realhw/` にある (2026-10-22 に `docs/tasks/realhw/design/` から移した) (往復の版 r1〜rN と REVIEW は `~/os32-tmp/run/realhw_recover/` などに残る)。

- [REALHW_RECOVER_DESIGN.md](../../decisions/realhw/REALHW_RECOVER_DESIGN.md) — 戻す処理の本文 (復帰先 S0/S1/S2/COLD、軽い順の段、deadman、書込み試験との契約、ハードの候補、受入と段分け)
- [REALHW_RECOVER_ADDENDUM_R2.md](../../decisions/realhw/REALHW_RECOVER_ADDENDUM_R2.md) — 追記 r2 (暫定予算・A3・段の割り当てと変異)。本文の一部
- [REALHW_RECOVER_ADDENDUM.md](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md) — 追記 r4 (plan の契約) と、各段で設計者が決めたこと・送り項目。本文の一部
- [RECOVER_T4B_INJECT.md](../../decisions/realhw/RECOVER_T4B_INJECT.md) — T4b の追記 (2026-10-23 確定): NP21/W で仮想 profile の試行 (`recover virtual begin|step|end`、私有 op 11) と故障・期限・停止時の poll・送信詰まりの 5 case を起こす口、ホストの終端不着の helper。段は B1・B2
- [GF6200_4A_DESIGN.md](../../decisions/realhw/GF6200_4A_DESIGN.md) — 段 4a の設計 (recover の契約を使う側)
- [GF6200_4A_S2_LAYOUT.md](../../decisions/realhw/GF6200_4A_S2_LAYOUT.md) — 段 4a S2 の引数配置

確定の後に設計者の決定や送り項目を足したら、同じ取り込みで上の写しも更新する (スキル os32-design-rally §6)。

### 4a S6 接続 (2026-10-21)
- 実装: trial_begin/step/endへのGF接続、T3のBEGIN不達・終端理由・送信中期限・SCREEN_CALL7・host所有重複を修正。
- T4b道具: `rshell_recover.py --status` と `--port aidebug:...` を追加。
- 決定は[追記](../../decisions/realhw/REALHW_RECOVER_ADDENDUM.md#4a-s6-の実装で設計者が決めたこと-2026-10-21)、検証証拠は `/home/hight/os32-tmp/run/gf4a_s6/`（最終結果は実装報告）。
- 未確認: PAUSEDの門・peek経路はT4bのNP21/Wで確認、31kHzのini鍵はT4b前にPMが扱う。
