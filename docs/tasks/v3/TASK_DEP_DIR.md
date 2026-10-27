# TASK_DEP_DIR — カーネルからドライバへの依存を、登録の口に寄せて外す

> 上位: [ROADMAP](../../ROADMAP.md)・開発規約 [POLICY_DEV](../../POLICY_DEV.md) §1 (カーネル層の不具合を先に)

> 状態: **実装中 (2026-10-27)** — D1〜D3 (優先 1〜3) と群 B (割込み・時刻・reset) は main、NP21/W と Ra266 のシリアルで受入。kselftest の合格数が D1 から 3 件減った理由は未調査 (失敗は 0)。kernel.c が直接読むドライバのヘッダは dev.h (抽象の口) だけ。KAPI の番号・ABI は不変。残りは §3 の「後」の行 (優先 4〜6、大)、粒度を決めてユーザーに出す。実機の確認は DEFERRED_TESTS の DEPDIR-D1-FD。

## 0. ユーザー指示 (2026-10-26)

「sol にドライバとカーネルの依存方向の状態を調査させてください。…カーネルがドライバ側への依存があると移植性が下がると思うので。ドライバがカーネルに依存するのは普通と思います。」→ 調べの後「1. から順に」(直す)。

## 1. 調べの結果

調べは sol (読み取りだけ、基点 d0f5b0e8)、表は `~/os32-tmp/run/depdir/SURVEY.md`。VFS・ブロックデバイス表・描画 HAL・部品の関数表は口を経ていて良い。残る逆向きは IRQ・タイマ・標準入力・時計・起動の配線・画面の復旧に装置の名前と知識が入っているもの。`.c` を `.mod` に移すだけでは PCI ID・IRQ 番号・型・起動の方針が常駐側に残り、向きは変わらない。

## 2. 方針 (PM 決定)

- 直すのは「カーネル → 具体ドライバ」の向きだけ。ドライバ → カーネルは普通の依存として残す (ユーザー指示)。
- 口は今ある形に寄せる (irq_register、VFS の登録表、部品の関数表)。新しい仕組みは最小にする。KAPI の番号と ABI は変えない (内側だけ差し替える)。
- 機械語は変わる。振る舞いは変えない — 各段のゲスト (FD 起動・HDD 起動・LAN・シリアル・入力) で前と同じことを確かめる。

## 3. 段

| 段 | 群 (SURVEY の #) | 中身 | 大きさ | 状態 |
|---|---|---|---|---|
| D1 | 16・17 | FDC の IRQ を FDC が irq_register で持つ、NIC・通信の周期処理を登録表に。設計 [DEP_DIR_D1_DESIGN](../../decisions/DEP_DIR_D1_DESIGN.md)、段は D1a (FDC/IRQ11) → D1b (tick の表) を sol、D1c (ゲスト・変異) は PM | 小〜中 | **main** (00d2a2ba、astra 取り込み可、PM のゲスト合格 `~/os32-tmp/evidence/2026-10-26/depdir-d1/RESULT.md`、Ra266 の FD 起動は DEPDIR-D1-FD) |
| D2 | 18〜25・33〜35・38・45 (ポート側) | 入力・コンソール出力・時計・シリアル診断を口の後ろへ (VFS の stdin・exec・GUI・KAPI の内側、FatFs の時刻も)。D1 の取り込みの後の main を基点に直列 | 中 (A1・A2・B・C の 4 段)。設計 [DEP_DIR_D2_DESIGN](../../decisions/DEP_DIR_D2_DESIGN.md) | **main** (959da3ea・3622900d、astra 取り込み可、P3 2 件は D3a へ、PM のゲスト合格 `~/os32-tmp/evidence/2026-10-26/depdir-d2/RESULT.md`) |
| D3 | 1〜14・31・32 | 起動の配線を platform の登録一覧に、root の選び方と装置名の解決をレジストリに。設計 [DEP_DIR_D3_DESIGN](../../decisions/DEP_DIR_D3_DESIGN.md)、段は D3a→b→c1→c2→d (codex)、D3e (PM) | 中 | **main** (97ffad3f・76f85591、astra 取り込み可 (P2 の disk.h を PM が直す)、PM のゲスト合格 `~/os32-tmp/evidence/2026-10-27/depdir-d3/RESULT.md`、Ra266 は DEPDIR-D1-FD) |
| 後 | 15・26〜30・36〜44・45 の PCI 側・46〜54 | 段の分け方は計画 `~/os32-tmp/run/depdir-later/PLAN.final.md` (astra 起草・Opus 1 往復、18 段 = B: L11〜L13 割込み・時刻・reset / C: L14〜L15 メモリ / D: L16〜L20 表示 / A: L01〜L10 部品・専用 KAPI の呼び先、配置の世代と KAPI の版は変えない)。**ユーザー決定 (2026-10-27): B の 3 段を先に**、C 以降は節目ごとに聞く | 大 | B は設計確定 ([DEP_DIR_B_DESIGN](../../decisions/DEP_DIR_B_DESIGN.md)、L11 → L12 → (Ra266 のシリアルの確認) → L13)。**L11・L12 は main** (d3047ef9、astra 取り込み可、PM のゲスト合格 `~/os32-tmp/evidence/2026-10-27/depdir-b12/RESULT.md`、host_test も PASS)。Ra266 の DEPDIR-B-PIC **合格** (2026-10-27、d3047ef、tick 約 100Hz・LAN の hsync 4 本・シリアル・boot.log の並びが 76f8559 と一致、FD は入っておらず読み出しは未実施、証拠はノートの ~/os32-tmp/evidence/2026-10-27/depdir-b-pic/)。全体の hsync で LAN の再送 1 回・2.9 秒の待ち 1 回 (前回は 0) — L13 の後の更新で再び出るかを見る。**L13 は main** (3679d23c・69321ced、astra 取り込み可、PM のゲスト 21/21 `~/os32-tmp/evidence/2026-10-27/depdir-l13/RESULT.md`)。Ra266 の L13 **合格** (2026-10-27、69321ce: PIT の行は 397ad51 から 4 版とも input=2457600Hz で同じ、tick 約 100Hz、boot.log の並びが一致、LAN の再送 0 — d3047ef の 1 回は再現せず)。**群 B は main と実機で受入**、beep は聴く手段が無く閉じた。**群 C (メモリ) は設計確定** ([DEP_DIR_C_DESIGN](../../decisions/DEP_DIR_C_DESIGN.md)、L14 → Ra266 のシリアルの確認 → L15、ユーザー「３．」2026-10-27)。**L14 は main** (0cffe292、astra 取り込み可、NP21/W で起動直後の mem の出力が main と同じ・ゲスト 21/21、取り込みの検査 rc=0)。Ra266 の L14 **合格** (e27fe52、mem の Physical・RAM・usable・phys pages・Memory Map の行が更新の前と同じ、違いは起動ごとの diag の 3 行だけ、部品 9 行 rc=0・db_test 9/9、証拠はノートの ~/os32-tmp/evidence/2026-10-27/depdir-l14-realhw/)。**L15 は main** (b3dc6a0c、astra 取り込み可、全 PTE が基点の列と一致、NP21/W で起動直後の mem の出力が L14 と同じ・ゲスト 21/21、取り込みの検査 rc=0)。Ra266 の L15 **合格** (51ed7aa、cmd mem の全行が e27fe52 と同じ、部品 9 行 rc=0・db_test 9/9、描画の時間は画面に触る試験しか無く未測定、証拠はノートの ~/os32-tmp/evidence/2026-10-27/depdir-l15-realhw/)。**群 C は main と実機で受入**。次の群はユーザー「双方着手」(2026-10-28、機能はエミュレータで足りる、実機の目視は立会いのついで)。**群 D (表示) は main** ([DEP_DIR_D_DESIGN](../../decisions/DEP_DIR_D_DESIGN.md)、L18・L16a・L16b・L19・L20、astra 取り込み可、NP21/W で表示・V86 の戻り・アプリの例外・#DF (DF_GUARD_TEST=1/2 とも赤の行と hlt) を確認 `~/os32-tmp/evidence/2026-10-28/depdir-d/RESULT.md`、取り込みの検査で変異の段の 2 本を PM が直す、実機の目視は立会いのついで)、**群 A (部品と専用 KAPI) も設計確定** ([DEP_DIR_A_DESIGN](../../decisions/DEP_DIR_A_DESIGN.md)、群 D と並べて進め L10 だけ群 D の L19 の後、PCM・FM・LAN の部品と kdebug の世代は変わる — 組で配る)。持ち越し: lib/sqlite3/os32_sqlite_test.c:12-19 の TVRAM の直書き (群 D の F8) |

### L15b 実装・検査記録 (2026-10-27)

- 基点 `0c1c993c`、群 C の差分基点 `407c2de4`。既存 C2(b) の 10 host は既存 include 経路を維持し、paging 利用者の選択入力へ固定名ヘッダ 2 本を追加。
- surface_bundle の initial native UC 変異先を `native_cache_bits(phys)` に追従。期待値・成功件数・KAPI・製品コードは変更しない。
- 証拠は `~/os32-tmp/run/depdir-l15b/`。原本、変異の -Werror ビルド、ヘッダ可視性、容量比較、候補と静的検査の結果は同所の RESULT.md に記録。
- 候補初回は memory_boot/v86_low の関数内表への追従漏れで失敗。前者は登録済み低位行を観測し、後者は新しい位置から表を切り出す。固定期待値・成功件数は維持。
- `--mutate` は未実施。unicode_surface を誤って native で起動して SIGSYS (合格に含めず)、明示 qemu で再確認。NP21/W・Ra266 の L15 受入は PM に持越し。
- 設計からの逸脱・設計の欠けはなし。未決: 既存 host は新規 runner ではなく原本ヘッダを参照するため、不要な写しを増やさない。
