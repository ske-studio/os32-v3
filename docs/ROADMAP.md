# OS32 リリースロードマップ

> 索引: 版ごとの計画と各計画票への入口

*策定: 2026-04-17 / v1.x GUIシェル計画 / 2026-09-29 更新 (タグ `v2.1`、GUI の版は 1.4 で閉じる、v3 は別リポジトリ os32-v3 へ fork する段取り) / 2026-09-30 更新 (os32-v3 へ fork 実施、この写しが正典)*

## 0. 版数の対応表

版数の対応表 (カーネル・GUI シェル・v3・v4、OS64 を載せないこと) は [decisions/VERSIONS.md](decisions/VERSIONS.md) §1、現行の開発 v3 と os32 v2.x の扱い・fork の決定・KernelAPI の関数表の容量の規則は同 §2 (2026-10-22 に §0・§0-1 から移した)。

*v1.0 到達までの開発履歴は `docs/archive/ROADMAP_v1.0.md` を参照*

---

## 1. v1.x GUIシェル — 「デスクトップ革命」

**コンセプト**: Windows 3.1 / 早期 Windows 95 の**見た目**をモデルにしたグラフィカルデスクトップ環境。OS32のCLI・グラフィックス・ファイルシステム全技術の集大成。

API は Win16 の再現ではなく、その欠点を 386 で払える範囲の現代の様式で解消する。
設計記録: [gfx_hal.md](design/include/gfx_hal.md) / v1.1 凍結契約: [os32_gui_shared.md](design/sdk/include/os32/os32_gui_shared.md)。

### 設計方針

| 項目 | 決定 |
|------|------|
| デザインモデル | **Win3.1 / 早期Win95 の外観** — 協調型シングルタスクGUIデスクトップ (WM は gshell 常駐) |
| 描画方式 | **全面GFX描画**。9801 = 640×400×16色 planar、9821 = PEGC 640×480×256色 / Cirrus GD54xx を HAL で切替 |
| 再描画モデル | InvalidateRect 方式 (damage + commit)、window move は XOR 枠、全画面 backbuffer + clip |
| GUI API | **libos32gui** — 非同期・ID参照・型付き16B event + retained widget tree + stateless drawing + box layout |
| app ⇄ WM | KAPI `gui_call(op,arg)` 1 本 + GUI SHM slot 4 本。wire protocol に pointer を載せない |
| GUI library | 私有 VA の shlib は 0x80000000、app image は 0x80100000 (正典: include/memmap.h / build/out/MEMMAP.md) |
| 色 | system 16色 + focused app の 14色 lease。lease 中 WM chrome は2色 |
| CUI/GUI | `/etc/system.cfg` の GUI=0/1 は**次回 boot の既定値**。実行中 shell の切替は `sys_switch_shell` |
| FEP | gshell が GFX renderer を保持。CUI へ戻る前に renderer callback を解除 |
| 性能目標 | Pentium 100MHz / 32MB で「超快適」を目標とするが、32MB をメモリの設計上限にしない |
| メモリ方針 | v3 の最低条件は 8MB 機で GUI + 私有 2MB のアプリ (目標、V3_GOALS §3)。構成別の判定は v3 最終段、それまでのゲスト受入は 64MB の 1 構成。設計対象と現行実装は [02_memory.md](02_memory.md) を参照 |

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

シェル帯の配置は [`include/memmap.h`](../include/memmap.h) の `MEM_SHELL_LOAD_ADDR`、実行モデルは [09_exec.md](09_exec.md) を参照。

```text
[CUI -> GUI: 即時]
1. CUI shell で `os32gui`
2. shell: sys_switch_shell("/bin/gshell.bin")
3. shell exit
4. kernel shell loop が gshell をシェル帯へロード

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

> **状況 (2026-09-07)**: main へマージ済み (`d739494`)。G0〜G5 の検証記録と既知の検証上の制約は `docs/archive/gui_v12/TASKS.md` §10 を参照。ESC 即時切替と上部バーは撤去済み (`DEBUG_SHORTCUTS`)。

**ゴール**: taskbar・Start・File Manager・launcher が揃い、GUIだけで基本操作が完結する。app 置換、CUI 切替、system halt を現在の single-foreground-app model を壊さず実現する。

正式設計:

- [tasks/gui/v12/CONTRACTS.md](tasks/gui/v12/CONTRACTS.md)
- `docs/archive/gui_v12/TASKS.md`

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

全項目受入済み・main にマージ済み (`fac0d89`)。残件の小物 4 件 (タスクバー経路の試験、`stat`、S6 `tar`、試験の棚卸し文書) も 2026-09-14 に feat/gui へ着地。持ち越し: S6-P (ext2 の小書き込み性能、`docs/archive/settings/TASK_S6.md`)、F3a〜c 等の保留 5 件 (`docs/archive/agents/HANDOVER_v14.md` §3) は **2026-09-30 に v3 の U6 で決裁「一部」** (`docs/archive/v3/U6_PENDING_REVIEW.md`、(記録はアーカイブ) D29〜D34): F3a / F3c・F2・FEP_BOUNDARY は v3 P1 の T2 / T4 / T5a の要件、DEVICE_RESERVATION は改訂して P4、F3b は TASK_DICT_META の後、MEMORY_RAM_INTEGRATION は撤回 (archive/settings/ (アーカイブ: `docs/archive/settings/MEMORY_RAM_INTEGRATION.md`))。

着手計画: [tasks/gui/v13/PLAN.md](tasks/gui/v13/PLAN.md)、監査と決裁: AUDIT_2026-09-10 (アーカイブ: `docs/archive/gui_v13/AUDIT_2026-09-10.md`)。
2026-09-10 決裁: **GUI アプリ 4 本の同時実行 (契約 T2a) を v1.3 の最初に置く** (K5 (アーカイブ: `docs/archive/gui_v13/TASK_K5_multiapp.md`))。
端末は外部アプリ。hermes 期の T5b (常駐パネル) は撤去、T6a (有限実行) は破棄。

**ゴール**: GUI desktop 上で CUI command が実行でき、既存 CUI program との互換性を確保する。

**目安: v1.2 から 2〜3ヶ月**

| 作業 | カテゴリ | 備考 |
|------|---------|------|
| **GUI アプリ 4 本の同時実行** | kernel / GUI | 契約 T2/T2a。PD 切替は `OP_WAIT` の中だけ、5 本目は `ERR_FULL`、資源回収はアプリ単位。K5 (アーカイブ: `docs/archive/gui_v13/TASK_K5_multiapp.md`)。端末と CUI 子の同居の土台 |
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
| Host Services N1〜N4 (**受入完了 2026-09-15**) | kernel / host / command / shlib | Claude Code PM + Opus 5 コーダー | ワイヤ v2、KAPI v51、`host_agent.py` v2、`wget` / `lpr` / `hclip` / `hdate`、`host_*` ラッパー |
| PEGC / Cirrus の 8bpp バックエンド (R2) (**受入完了 2026-09-29**) | GUI | 基盤 | S5 から先送り。PEGC 640x480・NP21/W の Cirrus 640x480 で gui_gate v11/v12g1/v12g4 が通る ([RELEASE_v2.1.md](RELEASE_v2.1.md) §2) |
| N4 のアプリ側 (ファイラの印刷、端末のコピー / 貼り付け) (**受入完了 2026-09-15**) | app | Claude Code PM + Opus 5 コーダー (別エージェント案は 2026-09-14 に撤回) | libos32gui の `host_*` ラッパー経由 |
| About dialog (**受入完了 2026-09-29**) | app | アプリ層 | OS32 About (`about.bin`、`ver` と同じ内容) |
| text editor GUI (**受入完了 2026-09-18**) | app | Claude Code PM + Opus 5 コーダー | edit.bin GUI版。**API の退行検出を兼ねる** (`docs/archive/gui_v14/TASK_EDIT_GUI.md`) |
| Host Services N5 (実機 LAN、Npcap + scapy) | host / driver | — | **実機の内蔵 82557 の票に吸収** ([tasks/realhw/TASK_LAN_82557.md](tasks/realhw/TASK_LAN_82557.md) の L-B、v3 の線) |
| hsync H1 / H3 (同サイズ差し替えの検出、日時前置判定、KAPI v52) | system / fs | Claude Code PM + Opus 5 コーダー | **受入完了 2026-09-15**。H2 (置換の安全化) / H4 (配備マニフェスト) も **受入完了 2026-09-16** (archive/shell/ (アーカイブ: `docs/archive/shell/TASK_H2.md`)) |
| ext2 の B8 (読み取り失敗の読み替えを塞ぐ、remount-ro 相当) | fs / vfs | 同上 | **受入完了 2026-09-15** (`docs/archive/shell/TASK_FS_TYPE.md`) |

先送り (v3 以降、[§2](#2-長期ロードマップ-次期カーネル-v3-以降) の「GUI アプリケーション群」): 設定アプリの拡張項目、
image viewer (VBZ / VDP / BMP)、music player、`sed` / `awk`。

---

## 1.5 kernel v2.1 — 実機互換・安定化 ✅ 完了 (2026-09-29、タグ `v2.1`)

v3 の新しい設計へ進む前に、現行系で判明した実機依存の不具合を既存契約のまま直した区切りの版。
到達点・受け入れの条件・分かっている制限は [RELEASE_v2.1.md](RELEASE_v2.1.md) が正典 (ここには持たない)。

- **実機 Ra266 の PEGC 640x480**: 実機の ROM (INT 18h AH=30h) が 480 ラインへ入る・戻るときの OUT 列を `v86 -g` で記録し、
  `pegc_apply_timing` をその順序と値に合わせた。画面を見ない条件 (ROM の OUT 列との一致・NP21/W 回帰・実機 `pegcchk`) で受け入れ
  ([RELEASE_v2.1.md](RELEASE_v2.1.md) §2-1、今の仕様は [05_drivers.md §5-5](05_drivers.md)、票はアーカイブ (`docs/archive/realhw/TASK_PEGC480_REALHW.md`)、起票は
  `docs/archive/realhw_v21/TASK_PEGC_RA266_TIMING.md`)。
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
現在地と残件は [現行引き継ぎ §1](tasks/agents/HANDOVER_2026-10-25.md)、LAN の詳細は [82557 票](tasks/realhw/TASK_LAN_82557.md) を参照。当時の鍵だった「ホスト側のシリアル実装」は
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

### v3 の全体計画 → [tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) (本案、2026-09-30。策定時の計画は `docs/archive/v3/PLAN.md`)

**機能を足す前に入れ物を作り直す。** 策定時 (2026-09-17) の順序は
**C11 → メモリマップ再配置 → ドライバの動的読み込み → PCI → Intel 82557** (PCI と 82557 は 2026-09-22 の決裁で静的に先行し、
v2.1 に入った — 本案 §4 で順序を引き直した)。カーネル本体の大きさと残りは [02_memory.md](02_memory.md) §2-1 から案内する `build/out/MEMMAP.md` が実測の地図
(v2.1 の時点で予算 596KB 中 583.0KB、残り 13.0KB)。ドライバ群だけで約 89KB ある (2026-09-17 の計測)。
**静的リンクのままでは積めない** (ユーザー指摘 2026-09-17)。本案は fork 先 os32-v3 で 2026-09-30 に確定した。
アプリへのメモリの払い出しの見直しも同じ計画に入れた (帯を切る前に決める)。

### メモリマップの全体再配置と C11 への移行 (ユーザー決裁 2026-09-17)

**再配置の中身は 2026-09-30 の 3 者討論 (ユーザー・Fable・Codex) で決定した** — 正典は
[MEMMAP_V3.md](decisions/MEMMAP_V3.md) (決定 D1〜D36、帯の表、票 T0〜T7)。要点: カーネル・シェル・通常 RAM は
恒等写像のまま、アプリだけ 0x80000000〜 の私有写像。物理地図と所有権台帳を先に作り、固定帯はカーネル 3MB + シェル 1MB だけ。
SQLite は同一リンクをやめてモジュール、低位 640KB は V86 へ。策定時に未決だった**本体の上限**はその討論で決まった
(鎖の終端 ≤ 0x3E0000 をリンカ ASSERT で、本体の予算 2,516KB、余りは KERNEL_SLACK として池へ)。ARM との順序と `u32` / `<stdint.h>` は
`docs/archive/v3/V3_PLAN_DRAFT.md` §3 P0 / §1-5 (ARM は v3 の後、固定長は維持) のまま。

策定時 (2026-09-17) は、配置超過と表明の検査不足を背景に、原因を切り分けられるよう C11 化を先に、再配置を後にする順序を選んだ。
T0 の C11 移行は受入完了。現在の順序・決定は [V3_PLAN.md](tasks/v3/V3_PLAN.md)、
型と C11 機能の採用範囲は [POLICY_DEV.md §2](POLICY_DEV.md) を参照。
策定時の懸念は TASK_KSTACK_USER.md §7-5 (アーカイブ: `docs/archive/kernel_v21/TASK_KSTACK_USER.md`) に残す。

### 他アーキテクチャへの移植に備えた調査 (継続)

移植 (例: ARM) は v1.x の範囲外だが、**新しい層を実装するたびに CPU 依存の調査を票に含める**
(ユーザー指示 2026-09-14)。最初は Host Services N1 (ワイヤ v2 / `link.c` / KAPI v51) で
`docs/archive/portability/SURVEY_N1.md` に記す (観点は `docs/archive/network/TASK_N1.md` §0 段 7)。
以後の票も同じ観点で `docs/tasks/portability/` に追記する。**移植準備の 4 段は 2026-09-15 に着地した**
(ARM コンパイル計測 `make check-arm-compile` 55/93、`hlt`/`cli`/`sti` を `io.h` 経由に、`arch/x86` + `platform/pc98`
の骨格、kstring の C 版、LE アクセサ `include/endian_le.h`。基準値と経過は [tasks/portability/ARM_GAUGE.md](tasks/portability/ARM_GAUGE.md))。
残りは `gdt`/`tss`/`cr3` と CPL=3 降下 asm の `arch/x86/` への移設、ARM 実装、KAPI 生成器の arch 対応 (**v3 まで保留**、ユーザー決裁)。
**移植の必須項目 (ユーザー決定 2026-10-14、v4 のあたりで対応)**: 物理 RAM の登録上端を、32bit CPU の上限 (4GiB) まで使い切れる設計にする。今の v3 は `MEM_PHYS_RAM_CEILING` = 2GB (D11、`[2GB, 4GB)` は窓・MMIO の帯として RAM にしない) で、PC-98 の実機 (最大 512MB 程度) には足りるが、ほかのアーキテクチャへ移すときに上限になる。RAM と装置窓の帯の分け方 (固定の帯ではなく物理地図で決める) を合わせて見直す。

**コストの見積もり (2026-10-29、sol の調べ・Opus の裏取り、どれも見積もり)**: **P (物理の上端 4GiB) だけ** 12 段・40〜60 本・6〜13 日 (カーネルは低位のまま、2GB 超の RAM は一時の窓で扱う — H は要らない)。**H (高位カーネル) だけ** 14 段・7〜16 日 (アプリ帯 0x80000000 は ABI のため動かせず、得るのは主に配置の整理)。**H と P を一緒に** 20 段・10〜22 日。4GiB 全体を常設写像することはどの案でもできない (32bit の仮想空間を RAM・アプリ・窓で分けるため)。P の受入には 2GB 超の RAM を持つ試験環境が要る (NP21/W で用意できるかは未確認、準備は別に 3〜6 段・2〜6 日の推測)。PM の推奨は v4 で P を先に、H は任意。v3 で前払いできる安い下準備: 受入の道具のシンボル→物理番地の変換を 1 か所に (0.5〜1 段)、非恒等のホスト試験の手本 (1〜2 段)。全文は `~/os32-tmp/run/cost-hh/COST.md`・`VERIFY.md` (リポジトリ外)。
習慣として今から守るもの: ワイヤ / ディスク上の構造は LE アクセサで読む、非アラインアクセスをしない、
絶対番地は `memmap.h` 以外に書かない、割込み制御は既存ヘルパー経由。

### GUI アプリケーション群 (v1.4 から先送り、2026-09-14)

→ **v3 後半に入れる** (ユーザー決定 2026-09-30、[tasks/v3/V3_PLAN.md](tasks/v3/V3_PLAN.md) §5。コントロールパネルと多言語対応は票を起こした)。下は策定時の記述。

v1.4 の「アプリ群」は基盤に依存される側ではないので、協調型マルチタスクの拡張の後に回す。
設定アプリの拡張 (壁紙・色・マウス速度は設定レジストリに行を足すだけ、UI は gshell の設定ダイアログ)、
image viewer (MGX は `mgxview` が既にある。VBZ / VDP / BMP を足す)、music player (FM 音源 BGM)、
`sed` / `awk`。着手の順は、そのときに一番 API の穴を踏みそうなものから。

### 16bit DOSプログラム移植スキーム

→ **v3 の範囲に入れない** (ユーザー決定 2026-09-30、[decisions/V3_GOALS.md](decisions/V3_GOALS.md) §2)。

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

- **PCI などの外部デバイスの実装方法のモデルを、専用の文書群として最終的に作る** (ユーザー決定 2026-10-20)。前例は実機の内蔵 82557 (LAN、[tasks/realhw/TASK_LAN_82557.md](tasks/realhw/TASK_LAN_82557.md)) で、GeForce 6200 の観測 ([tasks/realhw/TASK_GEFORCE6200.md](tasks/realhw/TASK_GEFORCE6200.md)) と 実機のメモリの読み出し (peek、[rshell_peek.py](../tools/rshell_peek.py)) もその材料にする。中身の見込み: 資料と参照実装の集め方とライセンスの確かめ方 ([HW3])、PCI の資源と C バスブリッジの確かめ方、読み取りだけの観測から書き込みの段へ進む手順と承認地点、kdebug の peek による実機観測、エミュレータ (NP21/W) 側の模型の作り方、HAL・module への結線、受入 (NP21/W と実機) の型。今ある票から一般化して書き、個々の装置の票はその実例として参照させる。
- PC-98 NIC (C-bus LAN) / network: [tasks/network/PLAN.md](tasks/network/PLAN.md) — LGY-98 は**エミュレータで回帰を取る経路**として残る (実機は内蔵 82557)

---

*ホビープロジェクトとして品質優先で進行。タイムラインはデッドラインではなくペース感の目安。*