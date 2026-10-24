# V3_PLAN — v3 本案 (柱と順序・v3 後半の票)

> 索引: v3 の段の票を並べる親 (子の票は `> 上位:` でここを指す)

> 状態: **実装中 (2026-10-19)** — v3 の本案。草案 `docs/archive/v3/V3_PLAN_DRAFT.md` を、ユーザー判断 U1〜U11 と Codex の突き合わせ X1〜X8 (草案 §7) が揃った 2026-09-30 に昇格した (最後の U3 (OS64) と U4 (目的と範囲) は同日ユーザーが決定)。**T0 (C11) は受入完了 (2026-09-30、main `f5bcb35`)、T1 (物理地図 + 所有権台帳) も受入完了 (2026-10-01)、T2・T3・T4・T5 は受入完了、T7 は受入完了・実機確認待ち (台帳 T7B-REALHW、2026-10-20)。詳細は T2 (`TASK_T2_APPBAND.md`)・T3 (アーカイブ: `docs/archive/v3/TASK_T3_LAYOUT.md`)・[T4〜T6b](TASK_T4_T6_MODULES.md)・[T7](TASK_T7_AND_FOLLOWUPS.md) の状態行を参照** (T2d〜h の経緯: `docs/archive/v3/TASK_T2D_T2H.md`)。
>
> 発行: コーダー `claude-opus-5-5` (worktree `wt/v3-plan`、基点 main `c9a8973`)、PM の指示による。

置くのは**柱と順序・v3 後半の票**だけ。決定の本文は各正典 (目的・範囲・目標の 2 段は [V3_GOALS.md](../../decisions/V3_GOALS.md)、メモリマップは [MEMMAP_V3.md](../../decisions/MEMMAP_V3.md) の D 番号、
版と fork は [VERSIONS.md](../../decisions/VERSIONS.md) §1・§2) にあり、ここには写さない。論点の経緯 (草案の一覧と振り分け、食い違い、v2.x 互換の論点、fork の段取り、
判断の往復) は草案に残した — §7 の表から辿る。数字 (カーネル予算など) の正典は [02_memory.md §2-1](../../02_memory.md) から案内する `build/out/MEMMAP.md` と [KAPI_SPEC.md](../../KAPI_SPEC.md)。

策定時 (2026-09-17) の計画 `docs/archive/v3/PLAN.md` は記録として残す (ドライバの動的読み込みの動機 §3、HAL の棚卸し §3-1、温めているアイデア §5 は各票の出典)。
その §1 の順序は、PCI・82557 が静的に先行した事実 (v2.1) を入れて本書 §4 が引き直した。

---

## 1. 目的

目的の一文と変更の受け入れ基準は [V3_GOALS.md](../../decisions/V3_GOALS.md) §1 (ユーザー承認 2026-09-30、U4)。

## 2. 範囲と目標

範囲 (v3 に入れる / 入れない / v3・v4 の外 / v3 で守る v4 の原則) は [V3_GOALS.md](../../decisions/V3_GOALS.md) §2、目標の 2 段 (最低条件・快適さの判定・目標、確かめる時期) は同 §3。

## 3. 柱 (P0〜P10)

各柱の出典・論点・Codex の補足の全文は草案 §3 の行。ここは中身の一言と正典だけ。

| 柱 | 中身 | 正典 (決定・票) | 状態 |
|---|---|---|---|
| **P0 規約と道具** | C11 (gnu11) への移行、型は固定長のまま、fork 先の検査・CI・文書 | `docs/archive/v3/TASK_C11_MIGRATION.md` (T0) | **受入完了** (2026-09-30、`f5bcb35`) |
| **P1 メモリマップの再構築** | システムは恒等写像のまま、アプリだけ 0x80000000〜 の私有写像、物理地図 + 所有権台帳、固定帯はカーネル 3MB + シェル 1MB、SQLite・FEP 等をモジュールに、低位 640KB を V86 へ、OpenType | MEMMAP_V3 (決定 §0、帯 §2-1、票 T1〜T7 §6、受入 §7) | **T1 受入完了** (2026-10-01、TASK_T1_LEDGER (アーカイブ: `docs/archive/v3/TASK_T1_LEDGER.md`))、T2 は受入完了 (詳細 (アーカイブ: `docs/archive/v3/TASK_T2D_T2H.md`))、T3・T4 は受入完了 (2026-10-18)、T5 は完了 (2026-10-19、[詳細](TASK_T4_T6_MODULES.md))、T7 は設計中 ([詳細](TASK_T7_AND_FOLLOWUPS.md)) |
| **P2 ドライバの置き場** | 専用のモジュールローダ (ロード時検証、信頼する配布物だけ、IRQ 登録と初期化状態の結び付け、停止を証明できない失敗は隔離) | MEMMAP_V3 §4-7、票 T4〜T5c・T6b | P1 の中で |
| **P3 HAL の結線** | HAL_WIRING の残 (W7)、NIC 境界 (L-C)、音源バックエンド、1kHz tick | [TASK_HAL_WIRING.md](TASK_HAL_WIRING.md)、[realhw/TASK_LAN_82557.md](../realhw/TASK_LAN_82557.md) | 一部着地 (v2.1)。1kHz tick は票が無い |
| **P4 デバイス窓の資源割当** | 予約の核は T1 の台帳の MMIO 登録、P4 には順序契約 (識別 → 予約 → 写像 → probe / enable → 面公開) と検証済み資源レコード (実測 BAR) | MEMMAP_V3 D33・§4-5、[settings/DEVICE_RESERVATION.md](../settings/DEVICE_RESERVATION.md) (改訂は P4 着手時) | T1 / T2 の後 |
| **P5 実機ドライバ** | 82557 L-B〜L-E (最初、静的で入る)、PCM E4〜E6 → 合成器、Trident、FD144、KBD_NAV K3、実機の画面取得 (PLAN §5-4) | [TASK_LAN_82557.md](../realhw/TASK_LAN_82557.md)、[TASK_PCM_CS4231.md](TASK_PCM_CS4231.md)、[TASK_TRIDENT_DRIVER.md](../realhw/TASK_TRIDENT_DRIVER.md)、[realhw/PLAN.md](../realhw/PLAN.md) | 82557 は P1 を待たない |
| **P5 (追加) ディスクの速さ** | IDE の DMA 転送と ext2 の読み書きの経路 (ブロックキャッシュ・先読み)。**実測 (2026-10-16、Ra266、86ba40c)**: 実機の中だけで 10 MiB を `cp /data → /data` が 53.6 秒 (0.20 MB/s)、LAN で読んでディスクへ書く `cp /host → /data` は 56.6 秒で、ほとんどがディスク側 (`~/os32-tmp/evidence/2026-10-16/lan_10m/split/`)。台帳 DISKPERF-1 | ユーザー決定 2026-09-30 (草案 §3 の行)。**起票の指示 (ユーザー 2026-10-16)**: v3 のドライバ関係で起票する。現状の使用には足りている | 票は未起票。T3 の後、NP21/W と Ra266 で cp の時間の内訳 (IDE の転送方式・1 回の転送量・ext2 のメタデータの書き戻し) を測ってから |
| **P6 実行モデル** | 協調型を基本に、前景が固まったらカーネルが取り戻す (番犬・CTRL+STOP)。スケジューリングとしてのプリエンプションは求めない | 意図はユーザー決定 2026-09-30、票の要件は MEMMAP_V3 §3-5-3 (Codex X5) | 実装方式の票は未起票 (P1 の後) |
| **P7 KAPI / ABI** | 後方互換は基本考えない、fork 時に 1 回整理 (スロット順を変えてよい)、世代の識別と旧新混在の試験が必須 | MEMMAP_V3 D7・**D35** | P1 と同時 |
| **P8 GUI 層** | 8bpp のまま、音 (PCM リング) と入力の層、microUI へのマウスキー、F3a〜c 等 | MEMMAP_V3 D19 | v3 後半 (§5) |
| **P9 アプリ層・互換層** | Video HAL の共通化、VESA2 的互換層、SDL 1.2 の受け皿、移植アプリ (ZSNES を含む) | MEMMAP_V3 D19・§2-2 (lease)、[PORT_CANDIDATES.md](PORT_CANDIDATES.md) | v3 後半 (§5) |
| **P10 データ・設定層** | F2 / FEP_BOUNDARY / F3a・F3c は P1 の T2〜T5a に畳む。独立票は辞書メタ情報と F3b だけ | MEMMAP_V3 D29〜D31、`docs/archive/v3/U6_PENDING_REVIEW.md` | v3 後半 (§5) |

## 4. 順序

| 段 | やること | 並行してよいもの |
|---|---|---|
| 1 | **P0** — 単独で (コンパイラの変更と配置の変更を別々に受け入れ、障害の原因を分けるため。Codex X1)。**済** | — |
| 2 | **P1 T1 → T2 → T3**、同時に **P7** の KAPI 整理。**P4** は T1 (台帳) の後・T2 の後 | **82557 L-B** (静的、P1 を待たない)、並行の実機確認 (HAL_WIRING W7、PCM E4〜E6、FD144、KBD_NAV K3)、P5 ディスクの実測 |
| 3 | **P1 T4 → T5a → T5b → T5c** (= **P2**) → **T7a → T7b** (T6b-1/2 は凍結・T6b-M は不要) | — |
| 4 | **P3 → P5 の残り** (PCM の合成器、Trident 段 3〜5)、**P6** の実装 | — |
| 5 | **v3 後半** (§5): P8 → P9、P10 の残り、GUI アプリ群 | — |

T3以降の実装用詳細設計: T3 配置 (受入完了 2026-10-18、契約は 02_memory §2-1、経緯: `docs/archive/v3/TASK_T3_LAYOUT.md`)、[T4〜T6b モジュール・起動](TASK_T4_T6_MODULES.md)、[T7 低位・OpenType / P3〜P10 接続ゲート](TASK_T7_AND_FOLLOWUPS.md)。T3・T4 は受入完了 (2026-10-18)、T5 は完了 (2026-10-19)、T7 は設計中。各票の状態行を参照し、方針決定と実装・受入を区別する。

T3 着手前の文書再整備は済み (経緯: `docs/archive/v3/DOCS_REORG_T3.md`)。予算再計算の経緯: `docs/archive/v3/TASK_T2D_T2H.md` §6-1。

票の中の順序と受入は MEMMAP_V3 §6・§7 が正典。策定時の「1 と 2 を同時に動かさない」「アイデアを順序に割り込ませない」(PLAN §1・§6) は維持する。

## 5. v3 後半の票 (基盤が整い次第)

| 票 | 中身 | 着手の条件 |
|---|---|---|
| [gui/TASK_CONTROL_PANEL.md](../gui/TASK_CONTROL_PANEL.md) | コントロールパネル (設定を GUI で変えるアプリ) | T4・T5a と P8 のウィジェットの後 |
| [gui/TASK_I18N.md](../gui/TASK_I18N.md) | UI メッセージの多言語対応 (SQLite のメッセージ表) | T4 とコントロールパネルの後 (他の文字体系は T7b の後) |
| [fep/TASK_DICT_META.md](../fep/TASK_DICT_META.md) | FEP 辞書のメタ情報と学習データの別ファイル化、その後に F3b (同一 DB の排他 open) | T4・T5a の後 |
| [PORT_CANDIDATES.md](PORT_CANDIDATES.md) | 既存ソフトウェアの移植 (P9、難度の低いものから、ZSNES を含む) | P9 の Video HAL と P5 の 16bit 面 |
| GUI アプリ群 (ROADMAP §2) | 設定アプリの拡張・画像ビューア・音楽プレーヤなど | 票は未起票。コントロールパネルと重なる分はそちら |
| PCM の合成器 (PLAN §5-5 の P2〜P4) | ソフト OPNA 合成器 (Rust、IRQ 文脈はまず整数演算)、`snd` の口、V86 への提供 | P2 (動的読み込み)。Rust の範囲は [RUST_VS_C11.md](RUST_VS_C11.md)・MEMMAP_V3 D36 |
| ネットワーク越しの CD・HostDrv の Host Services 経路 (PLAN §5-6・§5-7) | 82557 の上で `cdinst` 無変更のインストール、実機の `/host` | 82557 L-B〜L-E |

PLAN §5 の残り (5-1 ホストを仮想メモリに、5-2 V86 の装置要求をホストへ、5-3 EMS をネットワークへ) は**柱に入れていない** — 草案 §1-6 の振り分け (5-1 は v3 では捨てる、5-2 は 5-5 で置き換え (実機に無いプリンタを DOS に見せる部分だけ v3 後半の候補)、5-3 は v4 か保留) によるもので、**ユーザーの明示の決定ではない**。取り上げるときはユーザーに判断を仰ぐ。

## 6. 本案に持ち越した宿題 (草案 §4 の解消案のうち未実施のもの)

| 草案 §4 | 宿題 | 行き先 |
|---|---|---|
| D7 | 「v3」の多義 (カーネル版 / OS32X ヘッダ v3 / 設計票の改訂 v3) — 文書の改訂は「改訂 N」「往復 N」と書く規約 | POLICY_DEV に足す (未実施) |
| D12 | 動的 vs 静的 — 外に出す順を 1 表に (合成器・Trident・82557・USB、ブートに要るものは静的) | P2 (T5c) の設計票 |
| D14 | ACS / Host Service / Fabric の用語表 | v4 側。v3 では Host Service だけを使う |
| D16 | 1kHz tick の票 | P3 に起こす (実機と NP21/W で割り込み負荷を実測してから) |
| D21 | PLAN §3-1 と HAL_WIRING §0 などの重複 | 票側を正典に (PLAN は記録として残した) |

## 7. 決定と経緯の在りか

| 知りたいこと | 正典 |
|---|---|
| メモリマップ・モジュール・ABI・実行モデルの要件の決定 (D1〜D36) | [MEMMAP_V3.md](../../decisions/MEMMAP_V3.md) §0 (経緯 §11、Codex X1〜X8 の補足 §8-4) |
| ユーザー判断 U1〜U11 と Codex の論点 X1〜X8 の往復 | `docs/archive/v3/V3_PLAN_DRAFT.md` §7 |
| 草案の一覧と振り分け、食い違い D1〜D21、v2.x 互換の論点 C1〜C8 | 同 §1、§4、§5 (C1 は D35 で決着、C7 は二段移行) |
| 版数と fork の決定、os32 v2.x の扱い | [VERSIONS.md](../../decisions/VERSIONS.md) §1・§2、`docs/archive/v3/FORK_PLAN.md` |
| 保留 5 件の拾い方 | `docs/archive/v3/U6_PENDING_REVIEW.md`、MEMMAP_V3 D29〜D34 |
| Rust と C11 の分担、386 下限 | [RUST_VS_C11.md](RUST_VS_C11.md)、MEMMAP_V3 D36 |
| 現在地と残件 (日々の進捗) | 最新の引き継ぎ ([INDEX.md](../../INDEX.md) 冒頭の正典表の「引き継ぎ」の行) と各票の状態行 |
