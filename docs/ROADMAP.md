# OS32 リリースロードマップ

*策定: 2026-04-17 / v1.x GUIシェル計画 / 2026-09-29 更新 (タグ `v2.1`、GUI の版は 1.4 で閉じる、v3 は別リポジトリ os32-v3 へ fork する段取り) / 2026-09-30 更新 (os32-v3 へ fork 実施、この写しが正典)*

## 0. 版数の対応表 (正典はここだけ)

| 線 | 現在 | 意味 | 記録 |
|---|---|---|---|
| **カーネル** | **2.1 — 完了 (os32、戻り先)** (タグ `v2.1`、2026-09-29、KernelAPI v68。os32-v3 の初期コミットはこの時点の写し) | 2.0 (2026-09-03) はリング 3 (CPL=3) ネイティブ。2.1 は **v3 へ進む前の区切り** — 実機 PC-9821Ra266 で FD 起動・CD からの HDD インストール・HDD 起動まで通し、Ra266 の PEGC 640x480 を画面を見ない条件で受け入れた (ユーザー決定 2026-09-29) | [RELEASE_v2.1.md](RELEASE_v2.1.md)、[CHANGELOG.md](../CHANGELOG.md)、[archive/kernel_v2/PLAN.md](archive/kernel_v2/PLAN.md) |
| **GUI シェル** | **1.4 で閉じた** (2026-09-29) | 1.1〜1.4 は本書 §1。1.4 の範囲 (Host Services N1〜N4、エディタ GUI 版、About、R2 計測) はすべて v2.1 に入った。N5 (実機 LAN) は実機の内蔵 82557 の票 (L-B) に吸収。**GUI の版はここで閉じ、以後の GUI の作業は v3 の線で扱う** (ユーザー決定 2026-09-29) | §1 |
| **v3** | **現行** (2026-09-30〜、このリポジトリ os32-v3。**本案は 2026-09-30 に確定、T0 (C11) 受入完了、次は T1**) | 機能を足す前に入れ物を作り直す版。本案 (目的・範囲・目標・柱と順序) は [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md)、票は INDEX の「v3」節。一部の票 (HAL_WIRING・PCM・KHEAP の切り直し・デバイス窓の帯) は v2.1 に先行して着地した。**2026-09-15 のユーザー決裁で「v2」ではなく v3 と呼ぶ** (出荷済み 2.0 と衝突するため) | §2 |
| ゲーム基盤 | v4 (草案) | [V4_GAME_PLATFORM_DRAFT.md](archive/v4/V4_GAME_PLATFORM_DRAFT.md)。v3 の後 | — |

**OS64** (草案 [LEGACY_LIVING_PRESERVATION.md](LEGACY_LIVING_PRESERVATION.md)・[AUXILIARY_CORE_SERVICE.md](AUXILIARY_CORE_SERVICE.md) に出る語) は**版数表に載せない** (ユーザー決定 2026-09-29) — OS32 とは別の 64 ビット OS の構想で、v3 / v4 の範囲に入れない (ユーザー決定 2026-09-30)。語の定義は LEGACY_LIVING_PRESERVATION.md の冒頭。

### 0-1. 現行の開発 (v3 = このリポジトリ os32-v3) と、os32 v2.x の扱い (ユーザー決定 2026-09-29)

- **現行の開発は v3、場所はこのリポジトリ `os32-v3`**。2026-09-30 に os32 から**別リポジトリとして fork した** (ブランチを切るのではない。
  新しい履歴で始め、v2.x までの履歴は os32 に)。本案は [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) (2026-09-30 確定)、草案と票は `docs/tasks/v3/` ほか (INDEX の「v3」節)。
- **[os32](https://github.com/ske-studio/os32) の v2.x — タグ `v2.1`、`main` — は「戻り先」として保つ**。新機能は入れない。
  手を入れるのは、**戻る必要が生じたとき**と**致命的な不具合のとき**だけ。os32 側の docs は v2.1 時点の記録で、更新しない。
- fork の段取りで**決まっていること** (未決は無い。**2026-09-30 に実施、os32-v3** — 段取りと結果は [tasks/v3/FORK_PLAN.md §3](tasks/v3/FORK_PLAN.md)):

| 項目 | 決定 |
|---|---|
| リポジトリ名 | **`os32-v3`** (https://github.com/ske-studio/os32-v3、2026-09-30 作成) |
| 公開範囲 | **パブリック** |
| submodule (`apps/` `game/`) | os32-v3 も同じ submodule を参照して引き継ぐ。**当面の保守は os32 側で続ける** (一旦) |
| CI | **os32-v3 で作り直す** (os32 の `.github/workflows/build.yml` をそのまま移さない) |
| 文書の正典 | **os32-v3 (このリポジトリ) に置く**。os32 の docs は v2.1 時点の記録 (os32 の INDEX 冒頭の注記) |
| 共有の道具 (`tools/`、NP21/W のフォーク `~/np21w-src`) | 共有するが、**動作保証は v3 側だけ** — v2.x 側で動かなくなっても直す義務は無い |

**KernelAPI の版と関数表の容量**: 本数・残り・オフセットの正典は [KAPI_SPEC.md](KAPI_SPEC.md) (v63 からデータ欄は 0x4B8 に固定、
関数表の容量 R = 300、票 [TASK_KAPI_DATA_FIELDS](archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md))。ここに置く規則は 1 つ:
**残りが 16 本を切ったら次の R を決める票を起こす** — R を変えるとデータ欄が動き、全バイナリの作り直し (v63 と同じ移行) になる。
トランポリン 1 ページの上限は R ≤ 318。配備順 (v64 以降は「カーネルを先、ユーザーランドを後」) の手順の正典は
[08_build.md](08_build.md#kapi-v63-移行)。

*v1.0 到達までの開発履歴は [archive/ROADMAP_v1.0.md](archive/ROADMAP_v1.0.md) を参照*

---

## 1. v1.x GUIシェル — 「デスクトップ革命」

**コンセプト**: Windows 3.1 / 早期 Windows 95 の**見た目**をモデルにしたグラフィカルデスクトップ環境。OS32のCLI・グラフィックス・ファイルシステム全技術の集大成。

API は Win16 の再現ではなく、その欠点を 386 で払える範囲の現代の様式で解消する。
設計記録: [tasks/gui/DESIGN.md](tasks/gui/DESIGN.md) / v1.1 凍結契約: [tasks/gui/API_CONTRACTS.md](tasks/gui/API_CONTRACTS.md)。

### 設計方針

| 項目 | 決定 |
|------|------|
| デザインモデル | **Win3.1 / 早期Win95 の外観** — 協調型シングルタスクGUIデスクトップ (WM は gshell 常駐) |
| 描画方式 | **全面GFX描画**。9801 = 640×400×16色 planar、9821 = PEGC 640×480×256色 / Cirrus GD54xx を HAL で切替 |
| 再描画モデル | InvalidateRect 方式 (damage + commit)、window move は XOR 枠、全画面 backbuffer + clip |
| GUI API | **libos32gui** — 非同期・ID参照・型付き16B event + retained widget tree + stateless drawing + box layout |
| app ⇄ WM | KAPI `gui_call(op,arg)` 1 本 + GUI SHM slot 4 本。wire protocol に pointer を載せない |
| GUI library | shared library band 0x400000〜0x4FFFFF、app は 0x500000 から |
| 色 | system 16色 + focused app の 14色 lease。lease 中 WM chrome は2色 |
| CUI/GUI | `/etc/system.cfg` の GUI=0/1 は**次回 boot の既定値**。実行中 shell の切替は `sys_switch_shell` |
| FEP | gshell が GFX renderer を保持。CUI へ戻る前に renderer callback を解除 |
| 性能目標 | Pentium 100MHz / 32MB で「超快適」を目標とするが、32MB をメモリの設計上限にしない |
| メモリ方針 | CUI 最低 8MB は GUI 要件・開発制約ではない。GUI 必要 RAM は実測で定義。32bit フラット空間の設計対象と現行実装上限は [02_memory.md](02_memory.md) を参照 |

### 技術基盤

| 既存資産 | GUIシェルでの活用 |
|---------|-----------------|
| libos32gfx | client drawing の下敷き。planar / packed8 の差を吸収 |
| KAPI `gfx_screen_info` / `gfx_hw_*` | backend capability / hardware fill / blit |
| KCG cache | ANK + 漢字の GFX text |
| mouse sprite | GUI cursor |
| `ime_render` | FEP 未確定文字列・候補窓を WM が描画 |
| `sys_halt` | `OP_WAIT` / gshell idle の待ち。**1回の hlt であり system shutdown ではない** |
| Ring3 + address-space isolation | app と display window / shlib / guard の保護 |
| shared lib loader | libos32gui.shlib の text共有 + data app別複製 |

### CUI / GUI 切替フロー

```text
[CUI -> GUI: 即時]
1. CUI shell で `os32gui`
2. shell: sys_switch_shell("/bin/gshell.bin")
3. shell exit
4. kernel shell loop が gshell を 0x300000 へロード

[次回 boot の既定値]
- `os32gui on`  -> system.cfg GUI=1
- `os32gui off` -> system.cfg GUI=0

[GUI -> CUI: v1.2]
1. Start -> CUI mode
2. running app があれば Quit(SWITCH_CUI)
3. app exit
4. gshell: cursor hide / ime_set_render(NULL) / gfx_shutdown
5. system.cfg GUI=0 を保存
6. sys_switch_shell("/sys/shell.bin")
7. gshell exit -> CUI shell

[Shut Down: v1.2]
1. Start -> Shut Down
2. running app があれば Quit(SHUTDOWN)
3. app exit
4. gshell: GFX/FEP cleanup
5. halt screen
6. for (;;) sys_halt()
※ v1.2 は電源OFFではなく system halt。reset で再起動する

[ハング復旧]
1. FDDから boot -> mount /hd0
2. /hd0/etc/system.cfg を GUI=0 に変更
3. reboot -> CUI
```

---

### v1.1 — 「GUI基盤」 ✅ 完了

**ゴール**: mouse / window / event / drawing / FEP / modal / 3 backend を含む GUI 基盤を完成する。

作業分担・検証履歴: [tasks/gui/TASKS.md](tasks/gui/TASKS.md)。

> **2026-09-06: main にマージ (`8e184e2`)**。G1〜G5 を NP21/W で通過、レビュー 6 回反映、KAPI v42。
> 9801 planar / PEGC / Cirrus Xe10 の3 backend、Ring3 display isolation、shared lib、FEP、palette lease、modal を実機確認。
> 同日、ai-debug `/api/mouse` を追加し drag / overlap / click delivery を自動検証可能にした。

#### v1.1 の主要成果

| 項目 | 状態 |
|------|------|
| `gui_call` + GUI SHM + event ring | ✅ |
| syscall boundary input pump / CTRL+STOP Ring3 abort | ✅ |
| gshell WM / Z-order / focus / damage / chrome / cursor / timer | ✅ |
| libos32gui retained widgets / U3 loop | ✅ |
| fixed-address `libos32gui.shlib` | ✅ |
| FEP GFX renderer | ✅ |
| 14色 palette lease / 2色 chrome | ✅ |
| modal / MessageBox / basic File Open UI | ✅ |
| PC-9801 planar 640×400 | ✅ |
| PEGC 640×480 packed8 | ✅ |
| Cirrus Xe10 640×480 + hardware ops | ✅ |
| `/api/mouse` / `/api/screenshot` regression support | ✅ |

> 初期計画では H3 (Cirrus) を「v1.1後半〜v1.2」としていたが、実際には v1.1 の G5 までに完了した。v1.2 では HAL は原則 freeze / regression のみ。

---

### v1.2 — 「デスクトップ環境」

> **状況 (2026-09-07)**: main へマージ済み (`d739494`)。G0〜G5 の検証記録と既知の検証上の制約は [archive/gui_v12/TASKS.md](archive/gui_v12/TASKS.md) §10 を参照。ESC 即時切替と上部バーは撤去済み (`DEBUG_SHORTCUTS`)。

**ゴール**: taskbar・Start・File Manager・launcher が揃い、GUIだけで基本操作が完結する。app 置換、CUI 切替、system halt を現在の single-foreground-app model を壊さず実現する。

正式設計:

- [tasks/gui/v12/CONTRACTS.md](tasks/gui/v12/CONTRACTS.md)
- [archive/gui_v12/TASKS.md](archive/gui_v12/TASKS.md)

**KAPI v42 は維持。v43 は network / Host Services 用予約。** GUI wire protocol と shlib jump table の末尾追記で進める。

| 作業 | カテゴリ | 備考 |
|------|---------|------|
| Taskbar | WM | 画面下部24px、Start・window button・clock。app window/SHM slotを消費しない |
| Start menu | WM | Programs / File Manager / Run / CUI mode / Shut Down |
| Session state machine | WM/API | `SESSION_REQUEST`、sticky Quit。nested `exec_run()` 禁止 |
| CUI switch | WM | app終了後、GUI=0保存 + `sys_switch_shell`。不要な reboot はしない |
| System halt | WM | app終了・GFX/FEP cleanup 後 `for (;;) sys_halt()`。電源OFFは対象外 |
| Standard dialogs | WM/API | MessageBox / File Open / Input。completed result を event ring と独立保持 |
| Modal result | GUI protocol | op 65。wrong/double consume = STALE、未consume次modal = FULL |
| File Manager | app | Win3.1風2 pane、navigate / mkdir / rename / delete / copy / same-FS move / launch |
| App launch request | GUI protocol | op 66。filer は `exec_run()` を直接呼ばず gshell へ LAUNCH(path) を依頼 |
| Icon16 | API | **16×16固定**、4bpp + 1bpp mask。32×32 / PNG/BMP/ICO は後段 |
| Right-click menu | WM/app | popup Window ABI は作らず WM overlay / client-area overlay |
| Regression automation | Tool | **既存 `/api/mouse`** + `/api/key` + screenshot/status を利用 |

#### v1.2 の重要な制約

- external GUI app は同時に1本。
- app 実行中の WM は app syscall 文脈で動くという v1.1 T8 を維持。
- X4 で VFS / exec / cfg 更新をしない。
- Quit / Modal completion は control event として ring full で捨てない。
- file copy は event loop に定期的に戻る。
- PC98 / PEGC / Cirrus の3 backend で同一 desktop flow を通す。

#### v1.2 Gates

| Gate | 内容 |
|---|---|
| G0 | protocol / 個別票 freeze、KAPI v42維持 |
| G1 | taskbar / Start / clock / focus |
| G2 | modal result / Input / FEP / stale組合せ |
| G3 | File Manager 基本操作 |
| G4 | app置換 / CUI / halt / CTRL+STOP後pending action |
| G5 | 3 backend + v1.1 regression + static gates |

---

### v1.3 — 「ターミナル統合とCUI抽象化」 ✅ 完了 (2026-09-14)

全項目受入済み・main にマージ済み (`fac0d89`)。残件の小物 4 件 (タスクバー経路の試験、`stat`、S6 `tar`、試験の棚卸し文書) も 2026-09-14 に feat/gui へ着地。持ち越し: S6-P (ext2 の小書き込み性能、[archive/settings/TASK_S6.md](archive/settings/TASK_S6.md))、F3a〜c 等の保留 5 件 ([archive/agents/HANDOVER_v14.md](archive/agents/HANDOVER_v14.md) §3) は **2026-09-30 に v3 の U6 で決裁「一部」** ([archive/v3/U6_PENDING_REVIEW.md](archive/v3/U6_PENDING_REVIEW.md)、[tasks/v3/TASK_MEMMAP_V3.md](tasks/v3/TASK_MEMMAP_V3.md) D29〜D34): F3a / F3c・F2・FEP_BOUNDARY は v3 P1 の T2 / T4 / T5a の要件、DEVICE_RESERVATION は改訂して P4、F3b は TASK_DICT_META の後、MEMORY_RAM_INTEGRATION は撤回 ([archive/settings/](archive/settings/MEMORY_RAM_INTEGRATION.md))。

着手計画: [tasks/gui/v13/PLAN.md](tasks/gui/v13/PLAN.md)、監査と決裁: [AUDIT_2026-09-10](archive/gui_v13/AUDIT_2026-09-10.md)。
2026-09-10 決裁: **GUI アプリ 4 本の同時実行 (契約 T2a) を v1.3 の最初に置く** ([K5](archive/gui_v13/TASK_K5_multiapp.md))。
端末は外部アプリ。hermes 期の T5b (常駐パネル) は撤去、T6a (有限実行) は破棄。

**ゴール**: GUI desktop 上で CUI command が実行でき、既存 CUI program との互換性を確保する。

**目安: v1.2 から 2〜3ヶ月**

| 作業 | カテゴリ | 備考 |
|------|---------|------|
| **GUI アプリ 4 本の同時実行** | kernel / GUI | 契約 T2/T2a。PD 切替は `OP_WAIT` の中だけ、5 本目は `ERR_FULL`、資源回収はアプリ単位。[K5](archive/gui_v13/TASK_K5_multiapp.md)。端末と CUI 子の同居の土台 |
| ターミナルウィンドウ | app | **外部アプリ**。libos32term (セルモデル) + libos32term_render (厳密 clip) を Paint に接続 |
| CUI program output redirect | kernel | console_write -> terminal window virtual console |
| full-screen GFX program | GUI | exec_run後にGUI全体再描画 |
| shell script | terminal | terminal内 script engine |
| CUI/GUI abstraction | API | console/GUI window の I/O 抽象化 |
| 設定レジストリ | system/API | `/etc/settings.db` (SQLite) + `libos32cfg`、初期値はインストール媒体のみ (リカバリモードで復元)。設計: [tasks/settings/DESIGN.md](tasks/settings/DESIGN.md)。v1.4 の設定アプリの下敷き |

#### CUI/GUI 抽象化レイヤー

```text
[CUI]                          [GUI]
 app                            app
  ↓                              ↓
 KernelAPI                     KernelAPI
  ↓                              ↓
 console.c -> TVRAM            console.c -> gshell terminal
```

- 既存 `shell_print` / `console_write` を GUI mode では terminal window へ redirect。
- `kbd_getchar` / `kbd_getkey` を GUI event queue と統合。
- GUI native app は libos32gui を直接使用。

---

### v1.4 — 「ホストサービスと最小のアプリ」 ✅ 完了 (2026-09-29、v2.1 に同梱。GUI の版はここで閉じた)

**ゴール**: Host Services (LGY-98 経由の GET / 印刷 / クリップボード、[tasks/network/HOST_SERVICES_PLAN.md](tasks/network/HOST_SERVICES_PLAN.md)) を
コマンドと GUI から使えるようにし、GUI アプリは **About とテキストエディタの 2 本だけ**に絞る
(ユーザー決裁 2026-09-14: アプリ群は葉なので後回し、エディタは libos32gui / 設定 / ホストサービスを
通しで使う受入試験として 1 本残す)。

**目安: v1.3 から 3〜6ヶ月**

| 作業 | カテゴリ | 担当 (ROLES §0) | 備考 |
|------|---------|------|------|
| Host Services N1〜N4 (**受入完了 2026-09-15**) | kernel / host / command / shlib | Claude Code PM + Opus 5 コーダー | ワイヤ v2、KAPI v51、`host_agent.py` v2、`wget` / `lpr` / `hclip` / `date -sync`、`host_*` ラッパー |
| PEGC / Cirrus の 8bpp バックエンド (R2) (**受入完了 2026-09-29**) | GUI | 基盤 | S5 から先送り。PEGC 640x480・NP21/W の Cirrus 640x480 で gui_gate v11/v12g1/v12g4 が通る ([RELEASE_v2.1.md](RELEASE_v2.1.md) §2) |
| N4 のアプリ側 (ファイラの印刷、端末のコピー / 貼り付け) (**受入完了 2026-09-15**) | app | Claude Code PM + Opus 5 コーダー (別エージェント案は 2026-09-14 に撤回) | libos32gui の `host_*` ラッパー経由 |
| About dialog (**受入完了 2026-09-29**) | app | アプリ層 | OS32 About (`about.bin`、`ver` と同じ内容) |
| text editor GUI (**受入完了 2026-09-18**) | app | Claude Code PM + Opus 5 コーダー | edit.bin GUI版。**API の退行検出を兼ねる** ([archive/gui_v14/TASK_EDIT_GUI.md](archive/gui_v14/TASK_EDIT_GUI.md)) |
| Host Services N5 (実機 LAN、Npcap + scapy) | host / driver | — | **実機の内蔵 82557 の票に吸収** ([tasks/realhw/TASK_LAN_82557.md](tasks/realhw/TASK_LAN_82557.md) の L-B、v3 の線) |
| hsync H1 / H3 (同サイズ差し替えの検出、日時前置判定、KAPI v52) | system / fs | Claude Code PM + Opus 5 コーダー | **受入完了 2026-09-15**。H2 (置換の安全化) / H4 (配備マニフェスト) も **受入完了 2026-09-16** ([archive/shell/](archive/shell/TASK_H2.md)) |
| ext2 の B8 (読み取り失敗の読み替えを塞ぐ、remount-ro 相当) | fs / vfs | 同上 | **受入完了 2026-09-15** ([archive/shell/TASK_FS_TYPE.md](archive/shell/TASK_FS_TYPE.md)) |

先送り (v3 以降、[§2](#2-長期ロードマップ-次期カーネル-v3-以降) の「GUI アプリケーション群」): 設定アプリの拡張項目、
image viewer (VBZ / VDP / BMP)、music player、`sed` / `awk`。

---

## 1.5 kernel v2.1 — 実機互換・安定化 ✅ 完了 (2026-09-29、タグ `v2.1`)

v3 の新しい設計へ進む前に、現行系で判明した実機依存の不具合を既存契約のまま直した区切りの版。
到達点・受け入れの条件・分かっている制限は [RELEASE_v2.1.md](RELEASE_v2.1.md) が正典 (ここには持たない)。

- **実機 Ra266 の PEGC 640x480**: 実機の ROM (INT 18h AH=30h) が 480 ラインへ入る・戻るときの OUT 列を `v86 -g` で記録し、
  `pegc_apply_timing` をその順序と値に合わせた。画面を見ない条件 (ROM の OUT 列との一致・NP21/W 回帰・実機 `pegcchk`) で受け入れ
  ([RELEASE_v2.1.md](RELEASE_v2.1.md) §2-1、正典の票は [tasks/realhw/TASK_PEGC480_REALHW.md](tasks/realhw/TASK_PEGC480_REALHW.md)、起票は
  [archive/realhw_v21/TASK_PEGC_RA266_TIMING.md](archive/realhw_v21/TASK_PEGC_RA266_TIMING.md))。
- **v2.1 の後に残った確認** (新機能ではない。v3 と並行、実機の前に居るときなど): ~~PEGC の GUI の目視~~ (2026-09-30 合格)、KBD_NAV の K3、FD144 の実機、
  HAL_WIRING の W7、PCM の E4〜E6、APP_BAND_PDE の §5 のゲスト受入。一覧は最新の引き継ぎ (INDEX 正典表の「引き継ぎ」) の残件表。

---

## 2. 長期ロードマップ (次期カーネル v3 以降)

### 協調型マルチタスク

**協調型の複数アプリ (最大 4 本、PD 切替、譲り合いは `OP_WAIT` だけ) は契約 T2a のとおり v1.3 で実装する**
(2026-09-10 決裁。以前ここに「v1.x は single foreground app」とあったのは v1.2 の暫定を指していた)。
v3 では timer interrupt を利用したプリエンプティブ寄りの multi-task を検討する。
→ **2026-09-30 のユーザー決定で意図を改めた**: 同時に実行する必要はなく、前景が固まったらカーネルが制御を取り戻す (協調型 + 番犬)。正典は [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) §3 P6 (下の 3 行は策定時の記述)。

- window / process の独立実行
- v1.x `gui_call` + SHM event ring を拡張した IPC
- child program 実行中も desktop が独立して応答

### 実機 (PC-9821Ra266) への展開 (ユーザー決裁 2026-09-17)

計画は [tasks/realhw/PLAN.md](tasks/realhw/PLAN.md) (実装中)。策定時 (2026-09-17) は「v1.x のあいだは着手しない」としたが、
**v2.1 で FD 起動・CD からの HDD インストール・HDD 起動・シリアル 115200・SerialFS (シリアル越しの `/host`)・PCI 列挙まで到達した**
([RELEASE_v2.1.md](RELEASE_v2.1.md) §1)。実機は**エミュレータが嘘をついている箇所を暴くため**に使う (役割が違うので両方を取る)。
残り (v3 の線): 82557 の L-B、PCM の E6、Trident (PEGC の目視は 2026-09-30 に合格)。当時の鍵だった「ホスト側のシリアル実装」は
`tools/rshell_serial.py` と SerialFS で埋まった。

**実機 LAN はオンボードの Intel 82557 を狙う** (ユーザー決裁 2026-09-17)。
当初の「C バスに LGY-98」は取り下げ — **高価で品薄**なのに対し 82557 は機体に載っていて
ゼロ円、そして **Intel の公開開発者マニュアルと Linux `e100` / FreeBSD `fxp` という
参照実装がある** (LGY-98 は一次資料が無くエミュレータの解析記事に依存していた)。

そのため **PCI の列挙を土台として先に作る**。82557 はその最初の利用者。
**NP21/W は PCI も 82557 も再現しない**ので実機でしか確かめられないが、v3 の実機段階は
どのみち検証がエミュレータから離れる (ディスク・起動位置・シリアルも同じ)。
**既存の LGY-98 ドライバは消さない** — エミュレータで回帰が取れる唯一の LAN 経路。
リンク層より上は共通なので両方を生かす。

Trident のバックエンドは**保留** — NP21/W の `tgui9680.c` が結線されておらず
書いても検証できない。**LAN と違って「無くても困らない」** (PEGC が使える) ので結論が変わる。
→ **2026-09-29 のユーザー決定で着手** (「Cirrus はエミュレータ用、実機は Trident」)。調査・設計は [tasks/realhw/TASK_TRIDENT_DRIVER.md](tasks/realhw/TASK_TRIDENT_DRIVER.md)。

### v3 の全体計画 → [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) (本案、2026-09-30。策定時の計画は [archive/v3/PLAN.md](archive/v3/PLAN.md))

**機能を足す前に入れ物を作り直す。** 策定時 (2026-09-17) の順序は
**C11 → メモリマップ再配置 → ドライバの動的読み込み → PCI → Intel 82557** (PCI と 82557 は 2026-09-22 の決裁で静的に先行し、
v2.1 に入った — 本案 §4 で順序を引き直した)。カーネル本体の大きさと残りは [02_memory.md](02_memory.md) §2-1 の生成ブロックが正典
(v2.1 の時点で予算 596KB 中 583.0KB、残り 13.0KB)。ドライバ群だけで約 89KB ある (2026-09-17 の計測)。
**静的リンクのままでは積めない** (ユーザー指摘 2026-09-17)。本案は fork 先 os32-v3 で 2026-09-30 に確定した。
アプリへのメモリの払い出しの見直しも同じ計画に入れた (帯を切る前に決める)。

### メモリマップの全体再配置と C11 への移行 (ユーザー決裁 2026-09-17)

**再配置の中身は 2026-09-30 の 3 者討論 (ユーザー・Fable・Codex) で決定した** — 正典は
[tasks/v3/TASK_MEMMAP_V3.md](tasks/v3/TASK_MEMMAP_V3.md) (決定 D1〜D22、帯の表、票 T0〜T7)。要点: カーネル・シェル・通常 RAM は
恒等写像のまま、アプリだけ 0x80000000〜 の私有写像。物理地図と所有権台帳を先に作り、固定帯はカーネル 3MB + シェル 1MB だけ。
SQLite は同一リンクをやめてモジュール、低位 640KB は V86 へ。下の「着手の前に決めること」のうち**本体の上限**はその討論で決まった
(鎖の終端 ≤ 0x3E0000 をリンカ ASSERT で、本体の予算 2,516KB、余りは KERNEL_SLACK として池へ)。ARM との順序と `u32` / `<stdint.h>` は
[archive/v3/V3_PLAN_DRAFT.md](archive/v3/V3_PLAN_DRAFT.md) §3 P0 / §1-5 (ARM は v3 の後、固定長は維持) のまま。

v3 で行う。**この 2 つは同時に動かさない** — どちらも全ファイルに触れるので、
壊れたときにどちらが原因か切り分けられなくなる。C11 が先 (T0)、再配置は T1〜T7。

| 順 | やること | なぜその順か |
|---|---|---|
| 1 | **規約を C11 へ** (変数は**固定長を維持**) | `_Static_assert` は定数式でなければ**コンパイルエラー**になる。今の自作 `STATIC_ASSERT` は定数式でないと**黙って無効化される** (`kernel/shm.c` の 2 本が毎ビルド警告を出しながら何も検査していなかった)。**再配置を検査なしでやらないため、C11 が先** |
| 2 | **メモリマップの全体再配置** | カーネル帯域 1MB に 1041KB を詰めていたのが 2026-09-17 の穴の根 ([archive/kernel_v21/TASK_KSTACK_USER.md](archive/kernel_v21/TASK_KSTACK_USER.md))。帯の中で削るのは延命でしかない |

**着手の前に決めること**:

- **カーネル本体をどこまで大きくしてよいか。** 数字は [02_memory.md](02_memory.md) §2-1 (v2.1 時点で 583.0KB / 596KB、
  残り 13.0KB。2026-09-17 は 433KB / 余裕 34KB)。上限を決めないと、削ってもまた同じ場所に戻る。
- **ARM 実装との順序。** KAPI 生成器の arch 対応が v3 まで保留なので、再配置してから
  ARM に手を付けると**配置の前提が 2 回動く**。
- **`u32` を続けるか `<stdint.h>` に寄せるか。** 固定長の維持は 32 ビット限定の前提
  ([tasks/portability/ARM_GAUGE.md](tasks/portability/ARM_GAUGE.md) §10) と噛み合う。
  どちらでもよいが**混在が最悪**。

詳しい懸念は [archive/kernel_v21/TASK_KSTACK_USER.md](archive/kernel_v21/TASK_KSTACK_USER.md) §7-5。

### 他アーキテクチャへの移植に備えた調査 (継続)

移植 (例: ARM) は v1.x の範囲外だが、**新しい層を実装するたびに CPU 依存の調査を票に含める**
(ユーザー指示 2026-09-14)。最初は Host Services N1 (ワイヤ v2 / `link.c` / KAPI v51) で
`docs/archive/portability/SURVEY_N1.md` に記す (観点は `docs/archive/network/TASK_N1.md` §0 段 7)。
以後の票も同じ観点で `docs/tasks/portability/` に追記する。**移植準備の 4 段は 2026-09-15 に着地した**
(ARM コンパイル計測 `make check-arm-compile` 55/93、`hlt`/`cli`/`sti` を `io.h` 経由に、`arch/x86` + `platform/pc98`
の骨格、kstring の C 版、LE アクセサ `include/endian_le.h`。基準値と経過は [tasks/portability/ARM_GAUGE.md](tasks/portability/ARM_GAUGE.md))。
残りは `gdt`/`tss`/`cr3` と CPL=3 降下 asm の `arch/x86/` への移設、ARM 実装、KAPI 生成器の arch 対応 (**v3 まで保留**、ユーザー決裁)。
習慣として今から守るもの: ワイヤ / ディスク上の構造は LE アクセサで読む、非アラインアクセスをしない、
絶対番地は `memmap.h` 以外に書かない、割込み制御は既存ヘルパー経由。

### GUI アプリケーション群 (v1.4 から先送り、2026-09-14)

→ **v3 後半に入れる** (ユーザー決定 2026-09-30、[tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) §5。コントロールパネルと多言語対応は票を起こした)。下は策定時の記述。

v1.4 の「アプリ群」は基盤に依存される側ではないので、協調型マルチタスクの拡張の後に回す。
設定アプリの拡張 (壁紙・色・マウス速度は設定レジストリに行を足すだけ、UI は gshell の設定ダイアログ)、
image viewer (MGX は `mgxview` が既にある。VBZ / VDP / BMP を足す)、music player (FM 音源 BGM)、
`sed` / `awk`。着手の順は、そのときに一番 API の穴を踏みそうなものから。

### 16bit DOSプログラム移植スキーム

→ **v3 の範囲に入れない** (ユーザー決定 2026-09-30、[tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) §2-1)。

```text
[DOS 16bit .COM/.EXE]
        ↓
 reverse engineering / static analysis
        ↓
 INT 21h -> KernelAPI mapping
        ↓
 32bit OS32X
```

- 半自動 + 手動修正を想定。
- 小さな COM tool から case study。
- INT 21h -> KernelAPI compatibility layer が中心課題。

### その他の長期テーマ

- PC-98 NIC (C-bus LAN) / network: [tasks/network/PLAN.md](tasks/network/PLAN.md) — LGY-98 は**エミュレータで回帰を取る経路**として残る (実機は内蔵 82557)

---

*ホビープロジェクトとして品質優先で進行。タイムラインはデッドラインではなくペース感の目安。*