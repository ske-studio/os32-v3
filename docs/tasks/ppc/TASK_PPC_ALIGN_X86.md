# TASK_PPC_ALIGN_X86 — PowerPC の作りを x86 に寄せる (2 重の実装を取り除く)

> 状態: **計画 (2026-11-20)** — 洗い出しは [ARCH_DRIFT_AUDIT](../../decisions/ppc/ARCH_DRIFT_AUDIT.md) (A 物理の違い 13・B 揃えられる 2 重 15・C 片方にしか無い 15)。前提は [POLICY_DEV §10](../../POLICY_DEV.md)「x86 に揃える」。PPC KAPI の埋め (票 TASK_PPC_KAPI_FILL) を先に終え、その後に本格に進める。
> 完了の基準: 下の表の B の 15 行が全部「済み」(x86 の形に寄せた、または物理の理由つきで ARCH_COMPAT_CONTRACT §9 の「残す」に移した) になり、C の行は各々「x86 に足す / PowerPC に足す / 片方のままでよい理由」が決まっている。

## 0. ユーザー決定 (2026-11-20)
- 「x86 に揃えるは大前提です。動作実績は x86 しか無いためです」。
- 「とりあえず x86 前提でまず KAPI の機能を揃えましょう。そのうえで 2 重実装を取り除きます」。
- 「もし実装が間に合わなくて PPC が暇になったら、先般の x86 の実装に寄せる作業を重さの軽いものから処理していくことを文書化して進捗を管理してください」。

- 2026-11-20 夕: 「最初の PowerPC での動作確認に作った実装です。破棄して x86 に寄せてください。まだ前提が渡っていないようです」— 5d (シェルの切り替え) の前提として、**B03・B09・B15 (と要る分の B01) を KAPI の埋めの後でなく今** 進める (シェルを x86 のシェルに寄せてから 5d を公開する)。仮の作りに合わせた分岐を足す案 (5d の受入の (a)) は取り消し。

- 2026-11-20 夕: 「今から実装するものはよせて」— 新しく実装する段は x86 の形で作る (新しい 2 重を作らない)。段 7a の S1 (FEP を x86 は module・PowerPC は静的に分けるビルド) は止めた — PowerPC でも x86 と同じ module の読み込みで FEP を使う形（SQLite は静的を維持し、module 化は任意） (B08・C04) に設計し直す。

## 1. 進め方
- **空いた時の取り方**: PowerPC の PM の手が本線の段の待ちで空いた時は、下の表の **重さの軽い順** に、状態が「未着手」の行を 1 つ取る。取ったら状態を「設計中 (担当・日付)」に書き換えてから始める (同じ行を 2 人が取らない)。
- 1 行 = 1 段の目安 (重い行は設計で分ける)。設計は astra 起草・sol レビュー、実装は astra・sol レビュー (ROLES §0)。x86 の振る舞いは変えない — x86 の形に PowerPC を寄せる。共通を PowerPC に合わせて曲げない。
- 寄せた後に残る違いは ARCH_COMPAT_CONTRACT §9 の行を「残す (理由)」に直すか、行を消す。
- KAPI の埋めの段と同じファイルを触る行は、埋めの段が main に入ってから取る (衝突させない)。

## 2. B — 揃えられる 2 重 (重さの軽い順)
| 順 | ID | 役目 | 重さ (調べの見込み) | 状態 |
|---|---|---|---|---|
| 1 | B15 | sys_switch_shell の提供の判定 (段 5d の門のマクロ) | 40–140 行、3–6 ファイル | 飛ばす (B03 と一緒に消える見込み、2026-11-20) |
| 2 | B14 | KCG の提供者の接続 | 45–150 行、4–7 ファイル | 済み (2026-11-20、main 50691f607 — ANK・JIS の読み出しを drivers/video/kcg/kcg_read_provider.inc で x86 と PowerMac が共有、x86 の vmkernel.lz4・kcg.mod は一致、ARCH_COMPAT_CONTRACT §9 に行) |
| 3 | B13 | exec の回収とカーネルの親の形 | 85–220 行、5–9 ファイル | 後回し (シェルの寄せ B03・B09 の設計の後、exec_nest_level を含む) |
| 4 | B12 | SFS の scope の実行の包み | 90–240 行、4–8 ファイル | 後回し (シェルの寄せ B03・B09 の設計の後、rshell_lan.c の SHELL_REMOTE_SFS) |
| 5 | B11 | ver/sysinfo とメモリの内訳 | 110–270 行、5–9 ファイル | 済み (2026-11-20、main fe906b179 — 共通の ver_info・ver_format・ver_backend_contract、x86 の関数差は許容の 4 つ、QEMU 3 本・NP21/W の mem_stat と互換の受入 PASS) |
| 6 | B10 | ログの経路と起動の診断の出力 | 160–360 行、6–11 ファイル | 未着手 |
| 7 | B09 | シェルの libc の接着・登録のコマンドと既定値 | 170–420 行、7–13 ファイル | 製品シェルの接続は main 未収録（§5） |
| 8 | B08 | SQLite の初期化とリンク (module か静的か) | 210–450 行、6–11 ファイル | 任意。静的 SQLite を維持する |
| 9 | B07 | ビルド・配布・試験の像の作り方 | 240–520 行、8–15 ファイル | 未着手 |
| 10 | B06 | 端末・入力の接続の方針 | 250–560 行、7–12 ファイル | 未着手 |
| 11 | B05 | root と /host・装置の名前と mount の手続き | 260–600 行、7–13 ファイル | 未着手 |
| 12 | B04 | LAN の運用・link の poll・rshell | 320–680 行、8–14 ファイル | 未着手 |
| 13 | B03 | シェルの枠・再起動 (PowerPC の sh を ID 1 に) | 350–700 行、6–12 ファイル | 共通起動ループ D1〜D3 は main 844839af0。PowerPC の製品接続は未収録 |
| 14 | B02 | 表示の選択・予約・描画の持ち主 | 400–850 行、8–15 ファイル | 未着手 |
| 15 | B01 | 起動の段取りと exec の初期化 | 450–950 行、9–18 ファイル | 未着手 |

## 3. C — 片方にしか無い (決めが先)
C01〜C15 (ARCH_DRIFT_AUDIT の C の表)。各行で「x86 に足す / PowerPC に足す / 片方のままでよい」をユーザーに確かめてから B と同じ形で表に足す。今は未決。

## 4. 進み
| 日付 | 行 | 担当 | 結果 |
|---|---|---|---|
| 2026-11-20 | B14 | os32powerpc | 設計中 (astra 起草・sol レビュー、PowerPC の機械の ~/os32-tmp/run/ppc-align-b14/) |
| 2026-11-20 | B14 | os32powerpc | 実装済み、sol のレビュー中 |
| 2026-11-20 | B11 | os32powerpc | 設計中 (B13・B12 はシェルの寄せの後に回した) |

## 5. 設計書の見直し (2026-11-20、[DESIGN_X86_AUDIT](../../decisions/ppc/DESIGN_X86_AUDIT.md))
ユーザー指示「未着手の設計書をすべて見直しして」。144 件のうち、止めるべき 3・直しが要る 12。

ユーザー決定 (2026-11-20):
- 5c (gfx の残り): **入れる** — 本線の B1〜B4 と PowerPC の 5c-A は実装済みで CI も通っている (既に実装されている物にわざわざ手を付けない)。表示の門のマクロは B02 で後に取り除く。
- 7a の辞書と学習: **書ける媒体が整うまで学習は待つ** — パスは x86 と同じ。今日入った ramfs の /db・/dbu は使わない方向。ATA (G4 の HDD) の後に学習の操作を公開。
- PowerPC の開発用の常駐 CLI と旧い kboot の 5 像: **最後に外す** (x86 に寄せる作業の終わりに)。

止めるべき (置き換え先):
| 文書 | 置き換え先 | 状態 |
|---|---|---|
| PPC_KAPI_GUI_SWITCH_DESIGN (5d) | シェルを x86 に寄せる設計 (B03・B09・B15、~/os32-tmp/run/ppc-shell-align/) の後に 5d を出し直す | PPC_SHELL_ALIGN_X86_DESIGN は origin/wip/kfill-designs で確定。段 A・B・C・E は main 未収録 |
| PPC_KAPI_FEP_INPUT_DESIGN (7a) | module の読み込みの PowerPC の接続 (C04: L1 → L2) の後に、x86 の fep.mod の形で 7a を出し直す (B08 は SQLite の任意の module 化で、7a の前提ではない) | PPC_MODULE_ALIGN_X86_DESIGN は origin/wip/kfill-designs で確定。本線側の L1 は main 779942989、L2 は e4d84e711。PowerPC 側の接続は main 未収録 |
| PPC_KAPI_GFX_REST_DESIGN (5c) | ユーザー決定で入れる (上) | B1〜B4 と 5c-A は実装済み |

直しが要る 12 件 (文書の直しの段で、各々を x86 の形に書き換える): PPC_KAPI_FILL_DESIGN・TASK_PPC_KAPI_FILL・TASK_PPC_SHELL・PPC_SHELL_DESIGN・ARCH_BRINGUP_ORDER・LAN_OPERATOR_DESIGN・TASK_LAN_OPERATOR・PPC_P3_DESIGN・PPC_P5_REST_DESIGN・TASK_PPC_LAN・ARCH_COMPAT_CONTRACT (§9 の「新しく作った違いを追記」を「物理差と既存の未整合だけ」に)・PPC_RESET_DESIGN。状態: 直した (2026-11-20、本線のレビュー待ち 4 件)。
