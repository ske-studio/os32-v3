# TASK_HWDEV — 実機の調査・試行・復帰の核と装置ごとの部品 (HWDEV) を分ける

> 状態: **実装中 (2026-10-22)** — M3a 実装済み、独立レビュー待ち (M3b 以降は未着手)

kdebug.bin に入っている実機の調査・試行・復帰 (realhw_recover と GeForce 6200 段 4a の gfinit) のうち、装置ごとの部分を別の部品 `/boot/hwdev.mod` に分け、kdebug には装置に依らない核を残す。安全の約束 (許可・保存・復帰・N1・承認) は [TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md) と [TASK_GEFORCE6200.md](TASK_GEFORCE6200.md) のまま変えず、変えるのは置き場・境界・寿命だけ。

## 0. ユーザー決定 (2026-10-22)

- **装置ごとの部分を別の部品に分ける**: kdebug.bin が 239,908B で KDEBUG の上限 262,144B の 92% になった件で、4 案 (上限の引き上げ / 装置ごとの部分を別の部品へ / 許可表を小さい型に詰める / kselftest を別の部品へ) から選んだ。
- **IDE・USB・SATA・TV チューナーなど様々なデバイスドライバ開発の観点で必要な状態を想定する** — 要件は [DEVICE_STATES.md](design/DEVICE_STATES.md)。
- **インタプリタ方式で軽く済むならそちらへ立ち返ってよい、重くないなら今の方式で** — 比べた結果は §2。
- **実装モデルは設計ラリー 3 回とタスクの重さで選ぶ** — 段ごとの割当ては §3。
- **設計まで進めて実装は待機** — 実装はユーザーの合図で始める。
- **文書は冗長にならないように粒度別に分割する** — 票 (この文書)・構造・参照の表・共通の要件の 4 本に分けた (§5)。

## 1. PM 決定 (2026-10-22)

- **部品の台帳 (tools/module_schema.py) の 11 枠目を HWDEV に使う** (台帳は 10 枠で 10 種類が使用済み)。定義は [HWDEV_CORE_API.md](design/HWDEV_CORE_API.md) §4。
- 設計の A 案と、初版は boot 中に入れ替えない部品の寿命は、設計の提案としてレビューで承認された (Opus 2 往復、確定 G1〜G5)。
- **owner の作業領域の上限は 267 頁** (2048 node の最悪の場合の実測、S6 送りの 251 頁を訂正)。内訳は [HWDEV_CORE_API.md](design/HWDEV_CORE_API.md) §9。

## 2. 方式の結論 — A (今の C と表をそのまま部品へ移す)

- 検証済みの GF の decode・exec・PLL を意味を変えずに移す方が軽い (5〜7 段、核 115〜150KiB の見込み)。
- 増えた約 135KB の多くは recover_gf.c の許可表のデータで、インタプリタにしても縮まない。
- B (装置部分を命令列と許可データへ) は 10〜14 段で、式の VM・PLL の有界探索・後始末の表現を作り直す費用が節約より大きい。組み合わせ (A + 小さい共通の plan 実行器) は境界が増えるので今は採らず、HW-BASIC は GeForce の実機合格の後に同じ核へつなぐ。

比較の表と概算は [HWDEV_DESIGN.md](design/HWDEV_DESIGN.md) §3。

## 3. 段

各段 0.5〜1 日 (M3・M5 が超えたら 2 段に割る)。試験と変異の全文は [HWDEV_DESIGN.md](design/HWDEV_DESIGN.md) §8、変異の実行は PM のレビューの後 (ROLES §0)。

| 段 | 重さ / 実装 | 合格条件 (要約) | 状態 |
|---|---|---|---|
| M1 計測・ABI 固定 | 中 / astra | 同じ S6 像の内訳を採り直し、267 頁の最悪条件と部品の寿命の契約を固定 | 実装済み・レビュー待ち |
| M2 台帳・loader・対照合 | 中 / astra | 11 枠目を足し既存 10 種を維持、壊れた部品を公開せず S0 は生きる | 実装済み・レビュー待ち |
| M3a 部品の像の組み立て | 中 / astra | HWDEV の import の stub と slot の生成 (CORE_API の G3)、GeForce の部品の像が組めて未解決シンボル 0・contract_crc が一致。kdebug の中身と動作は変えない | 実装済み・レビュー待ち |
| M3b 試行の入口の切替 | 中 / astra | op 5〜10 の振り分けを kdebug の中から部品へ。27 種・PLL・COPY・A4・CR1F・N1・全出口の既存の期待 trace が部品越しに一致 (kdebug の中の GF はまだ残る — 大きさは一時的に変わらない) | 未 |
| M3c GeForce の部分を kdebug から外す | 中 / astra | 核に gfinit*/recover_gf.c への直接参照が無い (S0 の最小 GF 復帰は残す)、kdebug.bin が見積もり (115〜150KiB) まで縮む、試験と対応表の追従、hwdev_audit の入力を HWDEV 側へ切替 | 未 |
| M4a 持ち主と世代の照合 | 中 / astra | v1 の拒否判定 (起動ディスクの IDE・LAN が使用中なら断る、未分類も断る)、古い世代の部品を使い回さない | 未 |
| M4b 解放と失敗の経路・回帰 | 中 / astra | PAUSED の保持と中止・外からの recover now・二重の終端・開始途中の失敗・cold の後の再登録で宙に浮く参照なし、FAILED/cold 不変 | 未 |
| M5 配備定義・計測・既存試験の追随 | 軽〜中 / sol | 核の像 ≤192KiB、両方の mem 上限、KDEBUG=0/1、既存 trace 一致 | 未 |
| 旧 S7 (shell と KAPI 共有ヘッダ) | 中 / sol | 固定 40B の wire と shell、KAPI の手順 ([ABI1]〜[ABI3]) | 未 |
| T4b (NP21/W) | 中 / astra | PAUSED と復帰の結合、拒否・復帰の trace | 未 |
| T5 → S8 (A1 → A2) → main 0〜3 → A4 → T6・T7 | 重 / astra | 各段の既定の受入、物理操作と各書込みの承認はユーザー | 未 |

- 並べ替え (2026-10-22): GeForce 4a の S7 以降と戻す処理の T4b 以降は、この票の M1〜M5 の後に回す。順序の正はこの表。各段の中身は [TASK_GEFORCE6200.md](TASK_GEFORCE6200.md) §5・[TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md) §2 のまま。
- HW-BASIC と DMA 対応はこの段に混ぜない。DMA は装置ごとに [HW3] の根拠・停止の証明・IRQ と RAM の寿命を別に設計してこの核へ足す ([DEVICE_STATES.md](design/DEVICE_STATES.md))。

- 段の分け方 (PM 決定 2026-10-22、ユーザーの問い「M3・M4 が重いならより粒度を細かくする必要はあるか」に答えて): 設計の M3 を 3 段、M4 を 2 段に分けた (1 段 = 単独で受け入れられる契約・半日〜1 日、ROLES §0)。M3a を先に置き、動作を変えずに境目 (関数表・照合) の誤りを切替の前に見つける。設計 ([HWDEV_DESIGN](design/HWDEV_DESIGN.md) §8) の試験と変異の行は M3 → M3a〜c、M4 → M4a〜b に振り分けて依頼に書く。核の関数の slot は final の表で数えると 25 (依頼文の 24 ではない) — M1 の照合で確かめる。

## 4. 未決

- 残っていたユーザー判断 (PG リセット) は 2026-10-22 に「付けない」で決着 ([TASK_REALHW_RECOVER.md](TASK_REALHW_RECOVER.md) §0)。PG リセットの未決は無い。

## 5. 設計の所在

確定 2026-10-22 (astra 起草・Opus 2 往復・PM が最終修正)。往復の版と REVIEW は `~/os32-tmp/run/devmod/` に残る。

- [HWDEV_DESIGN.md](design/HWDEV_DESIGN.md) — 構造: 実物の確認、方式の比較、核と部品の分け方、大きさ、GeForce 4a の移し方、ファイルの変更、試験と変異、確定 G1〜G5
- [HWDEV_CORE_API.md](design/HWDEV_CORE_API.md) — 参照の表: 核の関数の slot、private ABI の構造体、import の生成、contract_crc、台帳の 11 枠目、KDEBUG の ops 版 4
- [DEVICE_STATES.md](design/DEVICE_STATES.md) — 共通の要件: 想定する装置ごとに要る状態と、今の recover の前提で足りない所

確定の後に設計者の決定や送り項目を足したら、同じ取り込みで上の写しも更新する (スキル os32-design-rally §6)。

## 6. M1 の実装で設計者が決めたこと (2026-10-22)

- 同じ S6 像の map・size・header で核・GF・prefix/整列を分け、参照照合の生成器は CORE_API の slot 表を入力にする (実測は [CORE_API](design/HWDEV_CORE_API.md) §2・§7)。
- kernel imports は既存 kdebug の番号順で核の slot の後へ置き、既存 kdebug の import は変えない (CORE_API §3)。
- G4 の配置 CRC を独立の欄にし、配置 inventory と前処理テキストの生成・正規化・照合を固定する (CORE_API §8)。
- boot 寿命・同期 callback・DMA なしの manifest の形と初期化時の参照検査を固定し、call の transport は既存宣言と同じ型にする (CORE_API §9)。
- 上限は §1 を参照し、旧 251 頁へ収めるための領域削減はせず、頁ごとの丸め上げを STATIC_ASSERT と first-fit・全確保失敗点の試験で固定する (CORE_API §9)。

## M2 の実装で設計者が決めたこと (2026-10-22)

- HWDEV の facade は KDEBUG の公開後に起動し、既存 module runtime の依存 handle と rollback を使う。GeForce の振分け・owner 接続はこの段では変えない。
- M2 の台帳受入は合成像で閉じ、配布リストへの実 HWDEV の追加は M3a/M5 で行う。
- core の ABI と manifest を同じ生成契約で照合する。KMOD ABI 世代・RO 検査・slot 充填の詳細は CORE_API §8・§9 を正とする。
- 指定された CORE_API の状態「確定」を使えるよう、状態語彙の正典 POLICY_DEV §8 に設計・契約用の語を追加した。

## 送り項目 (取り込みのレビューの P3)
- (M5 まで許容、PM 決定 2026-10-22) M2: KDEBUG=1 で起動するたびに「[hwdev] rejected: NOT_FOUND」が出る (hwdev.mod を M5 で配備するまで、害なし)。

## M3a の実装で設計者が決めたこと (2026-10-22)

- 自前 .def と確定 slot 表を相互照合し、共通生成器で両像を結ぶ。ABI・生成物・像の配置は CORE_API §3・§8・§9。
- 送り M2 の生成反復は全出力の touch、頁上限の重複は GF/HWDEV 間の STATIC_ASSERT で解消。
- 送り R5 の DWARF 照合は維持する。テキスト CRC と違い inventory の列挙漏れを検出し、既存の host 生成処理だけで閉じるため過剰ではない。
- 部品の初期化と公開だけを実 GF 像で検査し、試行の振分け・核からの除外・配備はそれぞれ M3b・M3c・M5 に残す。
- 検証証拠は `~/os32-tmp/run/hwdev_m3a/`。実機・NP21/W は本段の対象外。
