# TASK_HWDEV — 実機の調査・試行・復帰の核と装置ごとの部品 (HWDEV) を分ける

> 状態: **実装中 (2026-10-24)** — M1〜M5・BARLEN・T4b・T5 の無書込みは済み、0FACh の書込みの口 route-test も main (7e9be2b、実機も更新済み)。次は S8 (立会い、`tools/accept/gf_s8.md`)。2026-10-23 に順序を見直した (§3-1、ユーザー承認)

kdebug.bin に入っている実機の調査・試行・復帰 (realhw_recover と GeForce 6200 段 4a の gfinit) のうち、装置ごとの部分を別の部品 `/boot/hwdev.mod` に分け、kdebug には装置に依らない核を残す。安全の約束 (許可・保存・復帰・N1・承認) は `TASK_REALHW_RECOVER` と `TASK_GEFORCE6200` のまま変えず、変えるのは置き場・境界・寿命だけ。

## 0. ユーザー決定 (2026-10-22)

- **装置ごとの部分を別の部品に分ける**: kdebug.bin が 239,908B で KDEBUG の上限 262,144B の 92% になった件で、4 案 (上限の引き上げ / 装置ごとの部分を別の部品へ / 許可表を小さい型に詰める / kselftest を別の部品へ) から選んだ。
- **IDE・USB・SATA・TV チューナーなど様々なデバイスドライバ開発の観点で必要な状態を想定する** — 要件は [DEVICE_STATES.md](../../decisions/realhw/DEVICE_STATES.md)。
- **インタプリタ方式で軽く済むならそちらへ立ち返ってよい、重くないなら今の方式で** — 比べた結果は §2。
- **実装モデルは設計ラリー 3 回とタスクの重さで選ぶ** — 段ごとの割当ては §3。
- **設計まで進めて実装は待機** — 実装はユーザーの合図で始める。
- **文書は冗長にならないように粒度別に分割する** — 票 (この文書)・構造・参照の表・共通の要件の 4 本に分けた (§5)。

## 1. PM 決定 (2026-10-22)

- **部品の台帳 (tools/module_schema.py) の 11 枠目を HWDEV に使う** (台帳は 10 枠で 10 種類が使用済み)。定義は [HWDEV_CORE_API.md](../../decisions/realhw/HWDEV_CORE_API.md) §4。
- 設計の A 案と、初版は boot 中に入れ替えない部品の寿命は、設計の提案としてレビューで承認された (Opus 2 往復、確定 G1〜G5)。
- **owner の作業領域の上限は 267 頁** (2048 node の最悪の場合の実測、S6 送りの 251 頁を訂正)。内訳は [HWDEV_CORE_API.md](../../decisions/realhw/HWDEV_CORE_API.md) §9。

## 2. 方式の結論 — A (今の C と表をそのまま部品へ移す)

- 検証済みの GF の decode・exec・PLL を意味を変えずに移す方が軽い (5〜7 段、核 115〜150KiB の見込み)。
- 増えた約 135KB の多くは recover_gf.c の許可表のデータで、インタプリタにしても縮まない。
- B (装置部分を命令列と許可データへ) は 10〜14 段で、式の VM・PLL の有界探索・後始末の表現を作り直す費用が節約より大きい。組み合わせ (A + 小さい共通の plan 実行器) は境界が増えるので今は採らず、HW-BASIC は GeForce の実機合格の後に同じ核へつなぐ。

比較の表と概算は [HWDEV_DESIGN.md](../../decisions/realhw/HWDEV_DESIGN.md) §3。

## 3. 段

各段 0.5〜1 日 (M3・M5 が超えたら 2 段に割る)。試験と変異の全文は [HWDEV_DESIGN.md](../../decisions/realhw/HWDEV_DESIGN.md) §8、変異の実行は PM のレビューの後 (ROLES §0)。

| 段 | 重さ / 実装 | 合格条件 (要約) | 状態 |
|---|---|---|---|
| M1 計測・ABI 固定 | 中 / astra | 同じ S6 像の内訳を採り直し、267 頁の最悪条件と部品の寿命の契約を固定 | main (c9a4817) |
| M2 台帳・loader・対照合 | 中 / astra | 11 枠目を足し既存 10 種を維持、壊れた部品を公開せず S0 は生きる | main (0387ab1) |
| M3a 部品の像の組み立て | 中 / astra | HWDEV の import の stub と slot の生成 (CORE_API の G3)、GeForce の部品の像が組めて未解決シンボル 0・contract_crc が一致。kdebug の中身と動作は変えない | main (911464c) |
| M3b 試行の入口の切替 | 中 / astra | op 5〜10 の振り分けを kdebug の中から部品へ。27 種・PLL・COPY・A4・CR1F・N1・全出口の既存の期待 trace が部品越しに一致 (kdebug の中の GF はまだ残る — 大きさは一時的に変わらない) | main (c1986c3) |
| M3c GeForce の部分を kdebug から外す | 中 / astra | 核に gfinit*/recover_gf.c への直接参照が無い (S0 の最小 GF 復帰は残す)、kdebug.bin が見積もり (115〜150KiB) まで縮む、試験と対応表の追従、hwdev_audit の入力を HWDEV 側へ切替 | main (92153ea) |
| M4a 持ち主と世代の照合 | 中 / astra | v1 の拒否判定 (起動ディスクの IDE・LAN が使用中なら断る、未分類も断る)、古い世代の部品を使い回さない | main (3bd99c9、2026-10-23、M4a と M4b を同じ作業木で順に実装) — S0 の最小 GF 復帰は独立経路を作らず HWDEV_DESIGN §6 を訂正 (設計者が決めたこと 4 件は HWDEV_DESIGN 末尾) |
| M4b 解放と失敗の経路・回帰 | 中 / astra | PAUSED の保持と中止・外からの recover now・二重の終端・開始途中の失敗・cold の後の再登録で宙に浮く参照なし、FAILED/cold 不変 | main (3bd99c9、2026-10-23、M4a と M4b を同じ作業木で順に実装) — S0 の最小 GF 復帰は独立経路を作らず HWDEV_DESIGN §6 を訂正 (設計者が決めたこと 4 件は HWDEV_DESIGN 末尾) |
| M5 配備定義・計測・既存試験の追随 | 軽〜中 / sol | 核の像 ≤192KiB、両方の mem 上限、KDEBUG=0/1、既存 trace 一致 | main (067da15、2026-10-23) — hwdev.mod を KERNEL_PAIRED_MODULES・core・R1 版・世代表の依存へ、起動 FD からは kdebug.bin と同じく除外 (PM 決定)、GF 8 オブジェクト名は hwdev.mk の HWDEV_GF だけ。NP21/W で NOT_FOUND が消え hwdev の handle を得た (`~/os32-tmp/evidence/2026-10-23/m5_np21w/RESULT.md`) |
| 旧 S7 (shell、private wire) | 中 / sol | 固定 40B の wire と shell (KAPI は変えない、GF6200_S7_SHELL) | main (S7a・S7c 9d9a8ad、2026-10-23、private な `kdebug/gfinit_wire.h`)。実機の S7b は合格 |
| T4b (NP21/W) | 中 / astra | PAUSED と復帰の結合、拒否・復帰の trace | **合格** (2026-10-23、TASK_REALHW_RECOVER の T4b-1〜11 — 31kHz は CRT31K、`now 0` の PARTIAL と 98 復帰、仮想の試行・PAUSED・期限・故障・送信詰まり・終端不着、隔離像の巻戻し) — `~/os32-tmp/evidence/2026-10-23/recover_t4b/RESULT.md` |
| T5 → S8 (A1 → A2) → main 0〜3 → A4 → T6・T7 | 重 / astra | 各段の既定の受入、物理操作と各書込みの承認はユーザー | T5 の無書込みは合格 (TASK_REALHW_RECOVER の T5 行)。S8 はユーザーの立会いの日時待ち (台本 `tools/accept/gf_s8.md`、承認 3 つ: A1・A2・route-test)。main 0〜3 以降は未 |

- 並べ替え (2026-10-22): GeForce 4a の S7 以降と戻す処理の T4b 以降は、この票の M1〜M5 の後に回す。順序の正はこの表。各段の中身は `TASK_GEFORCE6200` §5・`TASK_REALHW_RECOVER` §2 のまま。
### 3-1. 順序の見直し (2026-10-23、PM 提案・ユーザー承認)

実装の状態を確かめ直して、上の表の後半の順序を次に替える (表の各段の合格条件は変えない)。

1. **M5 を M4 より先に**: 大きさの条件はもう満たした。配備定義・`KERNEL_PAIRED_MODULES`・generations-manifest・KDEBUG=0 の門の試験だけの軽い段。起動のたびの `[hwdev] rejected: NOT_FOUND` もここで消える。
2. **新しい段 BARLEN (設計、astra 起草 → Opus レビュー)**: A1 の資源の確認 (`kdebug/gfinit_resources.c:79-85`) は、メモリを有効にした他の装置に 0 でないメモリ BAR があると長さを知る手段が無いので断る。2026-10-15 の基準構成の生データでは **0:8.0 Trident (Command 0002h、BAR 0x20000000/0x20400000/0x20800000) と 0:11.0 82557 (Command 0147h、BAR0 0x20410008・BAR2 0x20500000) が該当し、いまのコードのままでは A1 は必ず断られる** (0:7.0 NEC は BAR が全部 0 で通る)。装置ごとの BAR の長さの根拠 ([HW3] の資料・BSD 実装) と照合の形を決める。M4 と並行でき、S8 の前提。旧 T5 前の「Trident・NEC の BAR を読む」はこの段に置き換える。 **main (1fd0a49、2026-10-23、sol 実装・astra 取り込み可)** — NP21/W で起動・hwdev の handle は変わらず (`~/os32-tmp/evidence/2026-10-23/m5_np21w/BARLEN.md`)、実機の照合は S8。
3. **M4a と M4b**: 対象は誰も持っていない 0:13.0 だけなので、まとめて 1 段にできるかを依頼の前に PM が決める。送り項目の「S0 の最小 GF 復帰」の扱いはここで決める。
4. **旧 S7**: `recover` の先例 (`userland/shell/cmd_recover.c` が部品の private な `kdebug/recover_wire.h` を直接読み KAPI を変えない) に揃え、KAPI の版上げを避ける方向で設計を見直す ([GF6200_4A_DESIGN.md](../../decisions/realhw/GF6200_4A_DESIGN.md) §8 の S7 行と [HWDEV_CORE_API.md](../../decisions/realhw/HWDEV_CORE_API.md) §6 の書換えが要る)。M5 と同じ段にできるかは設計の後に決める。 **2026-10-23 確定**: [GF6200_S7_SHELL.md](../../decisions/realhw/GF6200_S7_SHELL.md) — private wire、KAPI 不変、段 S7a (wire と `gfinit inspect`) → S7b (実機の回、ROM と version.chip) → S7c (op 6〜10、`--allow-write` 必須)。 S7a は main (2026-10-23、NP21/W の入口は `~/os32-tmp/evidence/2026-10-23/s7a_np21w/RESULT.md`)。 S7c は main (a5f3351)。**S7b は合格** (2026-10-23、実機で `gfinit inspect` rc=0・chip=44・match=1、abort で lease を返す、`~/os32-tmp/evidence/2026-10-23/realhw_s7b/RESULT.md`) — 途中で不具合 2 件を直した (BIT の 'i' 表の版 5385f90、PLL 表のキーの一致が無いとき最初のエントリ 53793df)。**S7b で注意**: INSPECT は chip=0x44 を前提にする (kdebug/gfinit.c:193) — 実 ROM の version.chip が違えば `match=0 rc=-1` で GF の流れが止まるので、S8 に入る前にそこで止めて設計に戻す (REALHW_RECOVER_ADDENDUM.md:226 は未確認のまま)。
5. **T4b**: NP21/W に GeForce は無いので、GPU への試行が通ることは T4b では確かめない (実機の S8 以降)。戻す処理そのものの項目 (`recover now 0` の PARTIAL と 98 復帰、仮想の試行・PAUSED・期限・故障、送信詰まり・poll 上限・終端不着、旧版一式への巻戻し) は DEFERRED_TESTS の RECOVER-T4B の行のまま残す。先に `np21w_ini_live.py` に 31kHz の鍵を足し、M5 の配備確認と同じ回に回す。
6. **T5 と S8**: T5 は A1・A2 の測定と併合してよい ([ADDENDUM_R2](../../decisions/realhw/REALHW_RECOVER_ADDENDUM_R2.md))。書込みなしの部分 (status・version.chip の実 ROM 確認 — snapshot は S8 の A1 の REC で取る、2026-10-23 PM 決定) は立会いなしでシリアルで先に回し、0FACh の初めての書込みと目視は S8 と同じ立会いの回にまとめる。

並びの要約: M5 (+旧 S7 の可否) ∥ BARLEN 設計 → M4 → T4b → T5 の無書込み → S8 (A1 → A2) → main 0〜3 → A4 → T6・T7。

- HW-BASIC と DMA 対応はこの段に混ぜない。DMA は装置ごとに [HW3] の根拠・停止の証明・IRQ と RAM の寿命を別に設計してこの核へ足す ([DEVICE_STATES.md](../../decisions/realhw/DEVICE_STATES.md))。

- 段の分け方 (PM 決定 2026-10-22、ユーザーの問い「M3・M4 が重いならより粒度を細かくする必要はあるか」に答えて): 設計の M3 を 3 段、M4 を 2 段に分けた (1 段 = 単独で受け入れられる契約・半日〜1 日、ROLES §0)。M3a を先に置き、動作を変えずに境目 (関数表・照合) の誤りを切替の前に見つける。設計 ([HWDEV_DESIGN](../../decisions/realhw/HWDEV_DESIGN.md) §8) の試験と変異の行は M3 → M3a〜c、M4 → M4a〜b に振り分けて依頼に書く。核の関数の slot は final の表で数えると 25 (依頼文の 24 ではない) — M1 の照合で確かめる。

## 4. 未決

- 残っていたユーザー判断 (PG リセット) は 2026-10-22 に「付けない」で決着 (`TASK_REALHW_RECOVER` §0)。PG リセットの未決は無い。

## 5. 設計の所在

確定 2026-10-22 (astra 起草・Opus 2 往復・PM が最終修正)。往復の版と REVIEW は `~/os32-tmp/run/devmod/` に残る。

- [HWDEV_DESIGN.md](../../decisions/realhw/HWDEV_DESIGN.md) — 構造: 実物の確認、方式の比較、核と部品の分け方、大きさ、GeForce 4a の移し方、ファイルの変更、試験と変異、確定 G1〜G5
- [HWDEV_CORE_API.md](../../decisions/realhw/HWDEV_CORE_API.md) — 参照の表: 核の関数の slot、private ABI の構造体、import の生成、contract_crc、台帳の 11 枠目、KDEBUG の ops 版 4
- [DEVICE_STATES.md](../../decisions/realhw/DEVICE_STATES.md) — 共通の要件: 想定する装置ごとに要る状態と、今の recover の前提で足りない所

- [GF6200_S7_SHELL.md](../../decisions/realhw/GF6200_S7_SHELL.md) — 旧 S7 (2026-10-23 確定): シェルの `gfinit` コマンドで GF の op 5〜10 を呼ぶ。private な kdebug/gfinit_wire.h、KAPI 不変。INSPECT は ROM・version.chip の 1 行、書込みを伴う op は `--allow-write` 必須
- [CRT31K_DESIGN.md](../../decisions/realhw/CRT31K_DESIGN.md) — CRT31K (2026-10-23 確定): NP21/W フォークに 31kHz で起動する ini 鍵 `CRT31K` (既定 false) を足し、T4b の S0 を NP21/W で回せるようにする。INT18h の修正は範囲外 (bios18.c:567 の AH=31h の rate は別票)。ctl の停止では ini が保存されないので、鍵は原本の写しに足してから `np21w_ini.py derive … crt31k-on`
- [BARLEN_DESIGN.md](../../decisions/realhw/BARLEN_DESIGN.md) — BARLEN (2026-10-23 確定): 他の装置のメモリ BAR は正確な長さではなく**上限** (番地の整列と、Mem が有効な装置の隣の BAR までの距離の小さい方) と、根拠のある既知の長さの表 (82557) で GeForce の窓との重なりを判定する。BAR への書込み・Trident の資料は採らない (ユーザー承認)。実装は M4 の後 (host の試験のファイルが重なる)

確定の後に設計者の決定や送り項目を足したら、同じ取り込みで上の写しも更新する (スキル os32-design-rally §6)。

## 6. M1 の実装で設計者が決めたこと (2026-10-22)

- 同じ S6 像の map・size・header で核・GF・prefix/整列を分け、参照照合の生成器は CORE_API の slot 表を入力にする (実測は [CORE_API](../../decisions/realhw/HWDEV_CORE_API.md) §2・§7)。
- kernel imports は既存 kdebug の番号順で核の slot の後へ置き、既存 kdebug の import は変えない (CORE_API §3)。
- G4 の配置 CRC を独立の欄にし、配置 inventory と前処理テキストの生成・正規化・照合を固定する (CORE_API §8)。
- boot 寿命・同期 callback・DMA なしの manifest の形と初期化時の参照検査を固定し、call の transport は既存宣言と同じ型にする (CORE_API §9)。
- 上限は §1 を参照し、旧 251 頁へ収めるための領域削減はせず、頁ごとの丸め上げを STATIC_ASSERT と first-fit・全確保失敗点の試験で固定する (CORE_API §9)。

## M2 の実装で設計者が決めたこと (2026-10-22)

- HWDEV の facade は KDEBUG の公開後に起動し、既存 module runtime の依存 handle と rollback を使う。GeForce の振分け・owner 接続はこの段では変えない。
- M2 の台帳受入は合成像で閉じ、配布リストへの実 HWDEV の追加は M3a/M5 で行う。
- core の ABI と manifest を同じ生成契約で照合する。KMOD ABI 世代・RO 検査・slot 充填の詳細は CORE_API §8・§9 を正とする。
- 指定された CORE_API の状態「確定」を使えるよう、状態語彙の正典 POLICY_DEV §8 に設計・契約用の語を追加した。

## M3a の実装で設計者が決めたこと (2026-10-22)

- 自前 .def と確定 slot 表を相互照合し、共通生成器で両像を結ぶ。ABI・生成物・像の配置は CORE_API §3・§8・§9。
- 送り M2 の生成反復は全出力の touch、頁上限の重複は GF/HWDEV 間の STATIC_ASSERT で解消。
- 送り R5 の DWARF 照合は維持する。テキスト CRC と違い inventory の列挙漏れを検出し、既存の host 生成処理だけで閉じるため過剰ではない。
- 部品の初期化と公開だけを実 GF 像で検査し、試行の振分け・核からの除外・配備はそれぞれ M3b・M3c・M5 に残す。
- 検証証拠は `~/os32-tmp/run/hwdev_m3a/`。実機・NP21/W は本段の対象外。

## M3b の実装で設計者が決めたこと (2026-10-22)

- 試行の入口と外部 recover 後の解放先を公開済み部品へ切替え、S0 は部品なしで維持する。取得口と拒否・解放の契約は CORE_API §10。
- host 受入は実像の ops 呼出し (再配置 stub から核へ届く) と、同じ実像を `module_image_relocate` で読んで公開 GF 関数を差し替えた既存 S3〜S6 の trace oracle (装置・PCI/PTE・時間は既存 mock、期待 trace は不変、白箱用の static helper は host のまま) で閉じる。配備・実機の検証の代わりではない。M4 の所有照合・終端整理と M5 の配備は進めない。
- 送り M3a の stub 実呼出しは本段で回収。証拠は `~/os32-tmp/run/hwdev_m3b/`。

## M3c の実装で設計者が決めたこと (2026-10-22)

- GF 8 オブジェクトだけを核から除外し、S0 復帰と共通 GF ログは維持。旧 import の保持・nm 境界検査は CORE_API §11。
- 送り M3b の 2 件は、部品内 exec 本体だけを壊す trace 変異と、不在時の buf 不変検査で回収。M1 R2 の残りは audit を HWDEV の map/ELF/header へ切替えて回収。
- 証拠は `~/os32-tmp/run/hwdev_m3c/`。M4 の所有照合・終端と M5 の配備は本段に含めない。

## 送り項目 (取り込みのレビューの P3)
- (BARLEN か T4b で) M4: kernel/hwdev_module.c:121-126 — registration ref の取得に失敗すると rc≠0 なのに body は published のまま (直前の条件で実際には起きない) → 失敗時に published=0 にするか不変条件をコメントに。 **済** (0a2bf5b、失敗時に published=0)
