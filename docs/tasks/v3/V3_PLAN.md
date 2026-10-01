# V3_PLAN — v3 本案 (目的・範囲・目標・柱と順序)

> 状態: **実装中 (2026-09-30)** — v3 の本案。草案 [V3_PLAN_DRAFT.md](V3_PLAN_DRAFT.md) を、ユーザー判断 U1〜U11 と Codex の突き合わせ X1〜X8 (草案 §7) が揃った 2026-09-30 に昇格した (最後の U3 (OS64) と U4 (目的と範囲) は同日ユーザーが決定)。**T0 (C11) は受入完了 (2026-09-30、main `f5bcb35`)、T1 (物理地図 + 所有権台帳) も受入完了 (2026-10-01)、次は T2 (アプリ帯 + lease 窓)**。
>
> 発行: コーダー `claude-opus-5-5` (worktree `wt/v3-plan`、基点 main `c9a8973`)、PM の指示による。

置くのは**目的・範囲・目標・柱と順序・v3 後半の票**だけ。決定の本文は各正典 (メモリマップは [TASK_MEMMAP_V3.md](TASK_MEMMAP_V3.md) の D 番号、
版と fork は [ROADMAP.md §0](../../ROADMAP.md)) にあり、ここには写さない。論点の経緯 (草案の一覧と振り分け、食い違い、v2.x 互換の論点、fork の段取り、
判断の往復) は草案に残した — §7 の表から辿る。数字 (カーネル予算など) の正典は [02_memory.md §2-1](../../02_memory.md) の生成ブロックと [KAPI_SPEC.md](../../KAPI_SPEC.md)。

策定時 (2026-09-17) の計画 [PLAN.md](PLAN.md) は記録として残す (ドライバの動的読み込みの動機 §3、HAL の棚卸し §3-1、温めているアイデア §5 は各票の出典)。
その §1 の順序は、PCI・82557 が静的に先行した事実 (v2.1) を入れて本書 §4 が引き直した。

---

## 1. 目的 (ユーザー承認 2026-09-30、U4)

*PC-9821 の実機で、1 本のアプリケーションに機械の資源を渡し切れる入れ物を作り直す。入れ物とは、カーネル帯の切り方 (再配置)、
ドライバの置き場 (動的読み込み)、装置窓の資源割当、実機の装置ドライバ、そしてその上の HAL の口である。時代依存の仕事は Host Service に任せ、
OS32 側の契約は小さく保つ。*

この一文の出どころは 4 つの動機 — 入れ物を作り直す (PLAN §0)、アプリを優先する ([DESIGN_APP_FIRST.md](../../DESIGN_APP_FIRST.md))、
実機で動かす ([realhw/PLAN.md](../realhw/PLAN.md))、レガシーを生きたまま保存する ([LEGACY_LIVING_PRESERVATION.md](../../LEGACY_LIVING_PRESERVATION.md)) — で、並べ方は草案 §2。

**変更の受け入れ基準**: DESIGN_APP_FIRST §12 の判断基準 (7 項目) を、票の「しないこと」の判定に使う。迷ったら
「この変更で 1 本のアプリが PC-98 をより有効に使えるか」。

## 2. 範囲と目標

### 2-1. 範囲 (ユーザー承認 2026-09-30、U4)

| | 内容 |
|---|---|
| **v3 に入れる** | 柱 P0〜P7 (§3)。v3 後半に P8〜P10 と §5 の票 — **GUI アプリ群は範囲外から外して v3 後半に入れる** (U4 の修正、U7 の決定。コントロールパネル・多言語対応・移植アプリはここ) |
| **v3 に入れない** | 多機種・多アーキテクチャ ([V4_GAME_PLATFORM_DRAFT.md](../../V4_GAME_PLATFORM_DRAFT.md))、OS32 Fabric / Link (V4)、補助コア ACS ([AUXILIARY_CORE_SERVICE.md](../../AUXILIARY_CORE_SERVICE.md))、SMP、16bit DOS 移植スキーム (ROADMAP §2)、USB / IEEE 1394 (realhw/PLAN §9)、スワップ、**カーネルの仮想アドレス化 (高位カーネル) — v4 以降の候補** (TASK_MEMMAP_V3 D20。v3 は恒等写像のまま P2V/V2P の集約だけ) |
| **v3 / v4 の外** | **OS64** — OS32 とは別の 64 ビット OS の構想 (U3、2026-09-30)。語の定義は [LEGACY_LIVING_PRESERVATION.md](../../LEGACY_LIVING_PRESERVATION.md) の冒頭。移植は 32 ビット限定 ([ARM_GAUGE.md §10](../portability/ARM_GAUGE.md)) と「別物」として両立する |
| **v3 で守る v4 の原則** | V4 §11 の 8 原則のうち 1・2・4・6 (PC-98 固有を portable 層へ流さない、HAL 境界、固定番地を契約にしない、platform-only は明示)。ACS は HAL のバックエンド表に「補助コア / ホスト / 主コア」の差し込み口を**塞がない**ことだけ |

### 2-2. 目標の 2 段 (ユーザー決定 2026-09-30 — 正典はここ。決定の記録は TASK_MEMMAP_V3 D22)

| 段 | 内容 | 測る項目 |
|---|---|---|
| **最低条件** | **8MB 機で GUI + 私有メモリ総量 2MB のアプリ** が動く (9801 planar と PEGC の両方。「2MB」はコード・BSS・ヒープ・スタック・shlib data 複製を含む私有総量、内訳はアプリの任意) | 起動 kselftest 0 fail、地図検査 0 件、2MB の試験アプリ 3 種の起動・描画・終了 (TASK_MEMMAP_V3 §7) |
| **快適さの判定** | **前景アプリ 1 本**で、P100/32MB の Windows 95 の同種アプリより快適 (同時使用は判定に含めない) | Word/Excel 級の起動 2 秒以内、入力→描画 100ms 以内、マルチメディア 1 本が途切れない |
| **目標** | **32〜64MB 機で、前景アプリ 1 本について Windows 2000 当時のアプリの使用感** | 画面の解像度と色数、大きな文書を開く時間、再計算、入力→描画 |

## 3. 柱 (P0〜P10)

各柱の出典・論点・Codex の補足の全文は草案 §3 の行。ここは中身の一言と正典だけ。

| 柱 | 中身 | 正典 (決定・票) | 状態 |
|---|---|---|---|
| **P0 規約と道具** | C11 (gnu11) への移行、型は固定長のまま、fork 先の検査・CI・文書 | [TASK_C11_MIGRATION.md](TASK_C11_MIGRATION.md) (T0) | **受入完了** (2026-09-30、`f5bcb35`) |
| **P1 メモリマップの再構築** | システムは恒等写像のまま、アプリだけ 0x80000000〜 の私有写像、物理地図 + 所有権台帳、固定帯はカーネル 3MB + シェル 1MB、SQLite・FEP 等をモジュールに、低位 640KB を V86 へ、OpenType | TASK_MEMMAP_V3 (決定 §0、帯 §2-1、票 T1〜T7 §6、受入 §7) | **T1 受入完了** (2026-10-01、[TASK_T1_LEDGER](TASK_T1_LEDGER.md))、次は [T2 アプリ帯 + lease 窓](TASK_T2_APPBAND.md) |
| **P2 ドライバの置き場** | 専用のモジュールローダ (ロード時検証、信頼する配布物だけ、IRQ 登録と初期化状態の結び付け、停止を証明できない失敗は隔離) | TASK_MEMMAP_V3 §4-7、票 T4〜T5c・T6b | P1 の中で |
| **P3 HAL の結線** | HAL_WIRING の残 (W7)、NIC 境界 (L-C)、音源バックエンド、1kHz tick | [TASK_HAL_WIRING.md](TASK_HAL_WIRING.md)、[realhw/TASK_LAN_82557.md](../realhw/TASK_LAN_82557.md) | 一部着地 (v2.1)。1kHz tick は票が無い |
| **P4 デバイス窓の資源割当** | 予約の核は T1 の台帳の MMIO 登録、P4 には順序契約 (識別 → 予約 → 写像 → probe / enable → 面公開) と検証済み資源レコード (実測 BAR) | TASK_MEMMAP_V3 D33・§4-5、[settings/DEVICE_RESERVATION.md](../settings/DEVICE_RESERVATION.md) (改訂は P4 着手時) | T1 / T2 の後 |
| **P5 実機ドライバ** | 82557 L-B〜L-E (最初、静的で入る)、PCM E4〜E6 → 合成器、Trident、FD144、KBD_NAV K3、実機の画面取得 (PLAN §5-4) | [TASK_LAN_82557.md](../realhw/TASK_LAN_82557.md)、[TASK_PCM_CS4231.md](TASK_PCM_CS4231.md)、[TASK_TRIDENT_DRIVER.md](../realhw/TASK_TRIDENT_DRIVER.md)、[realhw/PLAN.md](../realhw/PLAN.md) | 82557 は P1 を待たない |
| **P5 (追加) ディスクの速さ** | IDE の DMA 転送と ext2 の読み書きの経路 (ブロックキャッシュ・先読み) | ユーザー決定 2026-09-30 (草案 §3 の行) — 票は無い | Ra266 の実測から |
| **P6 実行モデル** | 協調型を基本に、前景が固まったらカーネルが取り戻す (番犬・CTRL+STOP)。スケジューリングとしてのプリエンプションは求めない | 意図はユーザー決定 2026-09-30、票の要件は TASK_MEMMAP_V3 §3-5-3 (Codex X5) | 実装方式の票は未起票 (P1 の後) |
| **P7 KAPI / ABI** | 後方互換は基本考えない、fork 時に 1 回整理 (スロット順を変えてよい)、世代の識別と旧新混在の試験が必須 | TASK_MEMMAP_V3 D7・**D35** | P1 と同時 |
| **P8 GUI 層** | 8bpp のまま、音 (PCM リング) と入力の層、microUI へのマウスキー、F3a〜c 等 | TASK_MEMMAP_V3 D19 | v3 後半 (§5) |
| **P9 アプリ層・互換層** | Video HAL の共通化、VESA2 的互換層、SDL 1.2 の受け皿、移植アプリ (ZSNES を含む) | TASK_MEMMAP_V3 D19・§2-2 (lease)、[PORT_CANDIDATES.md](PORT_CANDIDATES.md) | v3 後半 (§5) |
| **P10 データ・設定層** | F2 / FEP_BOUNDARY / F3a・F3c は P1 の T2〜T5a に畳む。独立票は辞書メタ情報と F3b だけ | TASK_MEMMAP_V3 D29〜D31、[U6_PENDING_REVIEW.md](U6_PENDING_REVIEW.md) | v3 後半 (§5) |

## 4. 順序

| 段 | やること | 並行してよいもの |
|---|---|---|
| 1 | **P0** — 単独で (コンパイラの変更と配置の変更を別々に受け入れ、障害の原因を分けるため。Codex X1)。**済** | — |
| 2 | **P1 T1 → T2 → T3**、同時に **P7** の KAPI 整理。**P4** は T1 (台帳) の後・T2 の後 | **82557 L-B** (静的、P1 を待たない)、並行の実機確認 (HAL_WIRING W7、PCM E4〜E6、FD144、KBD_NAV K3)、P5 ディスクの実測 |
| 3 | **P1 T4 → T5a → T5b → T5c → T6b** (= **P2**)、**T7a → T7b** | — |
| 4 | **P3 → P5 の残り** (PCM の合成器、Trident 段 3〜5)、**P6** の実装 | — |
| 5 | **v3 後半** (§5): P8 → P9、P10 の残り、GUI アプリ群 | — |

票の中の順序と受入は TASK_MEMMAP_V3 §6・§7 が正典。策定時の「1 と 2 を同時に動かさない」「アイデアを順序に割り込ませない」(PLAN §1・§6) は維持する。

## 5. v3 後半の票 (基盤が整い次第)

| 票 | 中身 | 着手の条件 |
|---|---|---|
| [gui/TASK_CONTROL_PANEL.md](../gui/TASK_CONTROL_PANEL.md) | コントロールパネル (設定を GUI で変えるアプリ) | T4・T5a と P8 のウィジェットの後 |
| [gui/TASK_I18N.md](../gui/TASK_I18N.md) | UI メッセージの多言語対応 (SQLite のメッセージ表) | T4 とコントロールパネルの後 (他の文字体系は T7b の後) |
| [fep/TASK_DICT_META.md](../fep/TASK_DICT_META.md) | FEP 辞書のメタ情報と学習データの別ファイル化、その後に F3b (同一 DB の排他 open) | T4・T5a の後 |
| [PORT_CANDIDATES.md](PORT_CANDIDATES.md) | 既存ソフトウェアの移植 (P9、難度の低いものから、ZSNES を含む) | P9 の Video HAL と P5 の 16bit 面 |
| GUI アプリ群 (ROADMAP §2) | 設定アプリの拡張・画像ビューア・音楽プレーヤなど | 票は未起票。コントロールパネルと重なる分はそちら |
| PCM の合成器 (PLAN §5-5 の P2〜P4) | ソフト OPNA 合成器 (Rust、IRQ 文脈はまず整数演算)、`snd` の口、V86 への提供 | P2 (動的読み込み)。Rust の範囲は [RUST_VS_C11.md](RUST_VS_C11.md)・TASK_MEMMAP_V3 D36 |
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
| メモリマップ・モジュール・ABI・実行モデルの要件の決定 (D1〜D36) | [TASK_MEMMAP_V3.md](TASK_MEMMAP_V3.md) §0 (経緯 §11、Codex X1〜X8 の補足 §8-4) |
| ユーザー判断 U1〜U11 と Codex の論点 X1〜X8 の往復 | [V3_PLAN_DRAFT.md](V3_PLAN_DRAFT.md) §7 |
| 草案の一覧と振り分け、食い違い D1〜D21、v2.x 互換の論点 C1〜C8 | 同 §1、§4、§5 (C1 は D35 で決着、C7 は二段移行) |
| 版数と fork の決定、os32 v2.x の扱い | [ROADMAP.md §0](../../ROADMAP.md)、[FORK_PLAN.md](FORK_PLAN.md) |
| 保留 5 件の拾い方 | [U6_PENDING_REVIEW.md](U6_PENDING_REVIEW.md)、TASK_MEMMAP_V3 D29〜D34 |
| Rust と C11 の分担、386 下限 | [RUST_VS_C11.md](RUST_VS_C11.md)、TASK_MEMMAP_V3 D36 |
| 現在地と残件 (日々の進捗) | 最新の引き継ぎ ([INDEX.md](../../INDEX.md) 冒頭の正典表の「引き継ぎ」の行) と各票の状態行 |
