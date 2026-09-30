# TASK_CONTROL_PANEL — コントロールパネル (設定を GUI で変える)

> 状態: **計画 (2026-09-30)** — ユーザー指示 (2026-09-30「v3 の後半にコントロールパネルのようなものの設計を入れる」) で起票。**着手 (設計票の作成) は v3 後半** — P1 の T2〜T5a (settings.db の経路が乗る SQLite のモジュール化と接続の所有) と P8 GUI 層の後。ここは範囲・材料・設計で決めることの一覧だけで、画面や API はまだ決めていない。
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-09-30)。関係: [V3_PLAN_DRAFT.md](../v3/V3_PLAN_DRAFT.md) §3 P8、[settings/DESIGN.md](../settings/DESIGN.md) (設定の置き場の正典)、[TASK_KBD_NAV.md](TASK_KBD_NAV.md) §1-3 (「MouseKeys の速さは設定画面で決める」を先送りした行)。

## 1. 目的

今の OS32 の設定は、置き場ごとに別々の道具で変える: `system.cfg` は `edit` か `gfxmode`、`settings.db` は CUI の `cfg get/set`、ほかはソースの定数。
Windows 98 のコントロールパネルに当たる **GUI の設定アプリを 1 本**用意し、ふだん変える項目をマウスまたはキーボードだけで変えられるようにする
(基準は Windows 98、[TASK_KBD_NAV](TASK_KBD_NAV.md) と同じ — Windows を使う人が迷わない配置)。

## 2. 前提 (決定済み — 再議論しない)

| 前提 | 出典 |
|---|---|
| 設定は 2 層: 起動可否に効く数キーは `/etc/system.cfg` (テキスト)、ほかは `/etc/settings.db` | [settings/DESIGN.md](../settings/DESIGN.md) §0・§1 |
| **「初期値に戻す」機能は GUI にも CUI にも置かない** (戻すのはリカバリモードだけ) | 同 §0・§2 |
| 読み書きは `libos32cfg` の `cfg_*`、書き込みは 1 トランザクション、X4 (syscall 境界ポンプ) では触らない | 同 §0・§4 |
| `system.cfg` は廃止しない | 同 §7 |
| 前景アプリ 1 本に資源を集中する (設定アプリも常駐させない) | [V3_PLAN_DRAFT](../v3/V3_PLAN_DRAFT.md) §2 |

## 3. 材料 — いま設定として外に出したい項目 (候補、設計で取捨する)

| 区分 | 項目 | 今の置き場 / 変え方 | 備考 |
|---|---|---|---|
| 画面 | グラフィクスの種類 (`GFX=pc98\|pegc\|cirrus\|auto`) | `system.cfg`、`gfxmode` | 起動可否に効く — 変えたら再起動。映らなかったときの戻し方 (時間切れで元に戻す確認など) が要る |
| 画面 | 起動時のシェル (`GUI=0/1`) | `system.cfg`、Start →「CUI mode」 | 同上 |
| デスクトップ | 背景色 `desktop/color`、壁紙 `desktop/wallpaper` | `settings.db` (gshell scope) | 既存キー |
| タスクバー | 時計の 24 時間表示 `taskbar/clock_24h` | 同上 | 既存キー |
| マウス | **マウスキーの速さ** (加速の段と移動量) | `userland/gshell/src/kbdnav.rs` の定数 `MK_ACCEL` | KBD_NAV で「設定画面で決める」を先送り。2026-09-30 に実機で遅いとの指摘で定数を 3 倍にした — 設定値にする最初の候補 |
| マウス | ダブルクリックの間隔 | gshell `modal.rs` の `DBLCLICK_TICKS`、filer の 30 tick | アプリごとに別の定数になっている — 1 か所にまとめるか |
| キーボード | リピートの開始・間隔、カナ / CAPS の扱い | ドライバの定数 | 実機 (KBD_NAV K3) の結果しだい |
| 日本語入力 | FEP の取り分 `fep.mem_reserve_kb`、辞書 S/M/L の選択 | settings (TASK_MEMMAP_V3 D27)、[TASK_DICT_META](../fep/TASK_DICT_META.md) | 変えたら再起動 (D27: 辞書の入れ替えは再起動で再確保) |
| 言語 | UI の表示言語 (`en` / `ja`) | 未整備 | [TASK_I18N](TASK_I18N.md) (ユーザー発案 2026-09-30) |
| 日付と時刻 | 時計の設定 | CUI の `date` 系 | RTC への書き込み |
| 音 | 音量・出力先 (FM / PCM) | 未整備 | P3 / P5 (PCM の合成器) の後 |
| ネットワーク | 82557 / LGY-98 の設定 | 未整備 (`lgy98.flags` はビルド時) | P5 (82557 L-B〜) の後 |
| システム情報 | 版 (`ver`)、メモリ、PCI 一覧、ドライバの状態 | CUI の `ver` / `lspci` | 表示だけ (Win98 の「システム」) |

## 4. 設計で決めること (設計票で答える)

| # | 問い | 考える材料 |
|---|---|---|
| Q1 | 1 本のアプリの中にページを並べるか、Win98 のように項目ごとの小アプリ (アプレット) に分けるか | 前景 1 本の方針、shlib・メモリ予算 (2MB の最低条件)、項目を足す手間 |
| Q2 | 変更の反映: 即時 / 「適用」ボタン / 再起動が要る項目の区別と表示 | settings/DESIGN §7 は変更通知を対象外にしている (「アプリが自分で読み直す」)。gshell は起動時に読んだ値をメモリに持つ (§0) — gshell に読み直させる口が要る |
| Q3 | gshell への通知の口 (新しい GUI プロトコルの op か、settings を見直す要求か) | `os32_gui_shared.h` (C が正典)・[API_CONTRACTS](API_CONTRACTS.md)、P7 (ABI の世代) |
| Q4 | 起動できなくなる設定 (`GFX=` など) の保護 — 試しに切り替えて N 秒で戻す確認、戻せなかったときの復旧手順 | settings/DESIGN §1 (テキストに残す理由 = FD 起動で直せる) |
| Q5 | 書き込み権限: どのアプリが `system` scope を書けるか | settings/DESIGN §7 (アクセス制御は命名規則だけ)、P1 T4 の接続の所有 |
| Q6 | キーボードだけで全項目を操作できること (Ra266 はバスマウス無し) | TASK_KBD_NAV、libos32gui / microUI のフォーカス移動 |
| Q7 | CUI の `cfg` と GUI の見え方をそろえる (同じキー名・同じ範囲検査) | `userland/cmds/cfg.c`、`assets/settings/defaults.tsv` (初期値の正典) |
| Q8 | 項目ごとの値の範囲・既定値・単位をどこに持つか (定義表 1 か所から GUI と検査を作るか) | defaults.tsv は初期値だけで範囲を持たない |

## 5. 依存と順序

- **前**: P1 の T4 (SQLite のモジュール化、`db_*` の経路) と T5a (FEP の接続) — settings.db を読む経路がここで変わる。P8 GUI 層 (ウィジェット・フォーカス)。
- **項目ごとの後**: 音は P3 / P5 (PCM)、ネットワークは P5 (82557)、辞書の選択は TASK_DICT_META。これらは最初の版に入れず、口だけ空けておく。
- **最初の版の候補** (設計票で確定): 画面 (GFX・GUI)、デスクトップ・タスクバー、マウスキーの速さ、ダブルクリック、日付と時刻、システム情報。

## 6. しないこと

- 「初期値に戻す」ボタン (settings/DESIGN §0 の決定)。
- `system.cfg` の DB 化・廃止。
- マルチユーザーの設定分離 (`user` scope は予約だけ、settings/DESIGN §7)。
- 常駐する設定サービス。
