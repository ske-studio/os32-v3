# 実機の調査・試行・復帰の核と装置部品 — 設計 r2 (構造)

> 状態: **確定 (2026-10-22)** — G1〜G5 を反映、実装はユーザーの合図待ち (段の状態は票 TASK_HWDEV が持つ)。
> 出所: ~/os32-tmp/run/devmod/DESIGN.final.md を 2026-10-22 に粒度別に分けて写した。

この文書は構造だけを持つ。決定・段の状態は票 TASK_HWDEV、関数の slot・構造体・台帳の定義は [HWDEV_CORE_API.md](HWDEV_CORE_API.md)、装置ごとに要る状態は [DEVICE_STATES.md](DEVICE_STATES.md) (元の §2)。r1→r2 の変更記録は ~/os32-tmp に残る。

- **推奨: A (C と表の装置部品化) を先行。HW-BASIC は GeForce 実機合格後に同じ核へ接続する。**
- 対象は置き場・境界・寿命。確定済みの戻す処理と段 4a の文書 ([REALHW_RECOVER_DESIGN.md](REALHW_RECOVER_DESIGN.md)・追記 2 本・[GF6200_4A_DESIGN.md](GF6200_4A_DESIGN.md)・[GF6200_4A_S2_LAYOUT.md](GF6200_4A_S2_LAYOUT.md)) の許可・保存・復帰・N1・承認を変更しない。
- 単一利用者 / 単一タスク / 協調切替、P100・32MB、整数のみ。CRC は破損・取り違え検出であって権限の根拠ではない。
- パスはリポジトリ相対。起草時は文書とソースの抽出だけを確認し、ビルド・実機試験は行っていない。

## 1. 実物の確認

| 実物・根拠 (file:line) | 確認したこと |
|---|---|
| `build/kdebug.mk:12` | gcap/math、NIC trace、selftest、CRT、main、peek/allow、window、recover/profile、GF 8 ファイル、imports を同じ像へリンク。kexports は kernel 側 (:15–16) |
| `kdebug/kdebug_main.c:100` | dbg_call が recover と gfinit を振り分け、外部 recover 後に GF 所有領域を解放 (:115)。単なる表の移動では済まない |
| `kdebug/recover.c:1`、`recover_profile.c:25` | recover は状態・検査・保存・ログ、profile は 98 画面・予算・indexed 復帰等。後者も全体を装置部品に移さない |
| `kdebug/gfinit.c:25`、`:46`、`:213` | GF owner、ページ確保、開始と bind。ROM/decode/plan/flow は試行寿命、A1 成功時だけ次の準備まで一部保持 |
| `kdebug/gfinit_decode.c:229`、`gfinit_exec.c:80`、`gfinit_pll.c:111` | ROM データ復号、検査済み plan 実行、整数 PLL 探索。exec は 125 行でも decode は 734 行、PLL は 225 行であり「汎用化 125 行」ではない |
| `kdebug/gfinit_plan.c:79`、`gfinit_resources.c:53`、`gfinit_bus.c:70`、`recover_gf.c:884` | A4 等の plan 合成、PCI 資源収集、NV44A bus、許可表と登録式。resources は将来共通化候補だが今回 GF と共に移す |
| `tools/module_schema.py:40`、`:53`、`:65`、`:143` | KDEBUG/SQLITE/FEP/LAN は既存 KMOD。image/mem 上限は各 256/256、512/512、64/64、64/64 KiB。FEP は SQLITE 依存の先例 |
| `tools/mkmod.py:144`、`:212`、`kernel/module_image.c:122` | ELF32、R_386_32/PC32、未解決参照拒否、生成 import 型契約、CRC・PAIR・範囲検査を再利用できるが、部品間 import の自動解決はない (§4 で接続) |
| `kernel/module_runtime.c:28`、`:144`、`:158` | 依存 handle 世代、停止・参照数検査、失敗時隔離がある。依存は関数解決そのものではない。装置所有権の仲裁は別途必要 |
| `kernel/module.c:39`、`tools/module_schema.py:24` | KDEBUG=1 のみロード、PAIR は kernel 像 CRC。台帳は 10 枠で 10 種類が使用済み。任意の装置部品を自動発見・交換する仕組みはない |

`size build/out/kdebug/*.o` の実測 (単位 B、text には rodata を含む。既存 .o だけ、kexports 除外):

| 群 | text | data | bss | 役割 |
|---|---:|---:|---:|---|
| recover / recover_profile | 33,814 / 5,344 | 0 | 25,816 / 0 | 状態機械・共通復帰 |
| kselftest | 35,115 | 0 | 2,368 | 自己試験 |
| v86_gcap / math | 4,998 / 1,430 | 0 | 36 / 0 | V86 観測・整数計算 |
| kpeek / allow | 1,522 / 521 | 0 | 1,090 / 0 | 読取りと許可 |
| nic_trace / crt_diag | 961 / 1,233 | 0 | 800 / 0 | NIC 記録・CRT 診断 |
| main / import slots / stubs | 1,178 / 20 / 1,554 | 28 / 1,036 / 0 | 124 / 0 / 0 | 接続 |
| 合計 | 87,690 | 1,064 | 30,234 | オブジェクト合計≠最終像 |

- 旧像のヘッダ実測: file=104,860B、payload=88,476B、mem=118,784B、prefix=16,384B。整列・表を含み、BSS は file に含まない。
- 現行 S6 の 239,908B は [REALHW_RECOVER_ADDENDUM.md](REALHW_RECOVER_ADDENDUM.md) の末尾とユーザー提供値。残り 22,236B、91.52%。現行 .o の実測値と偽って内訳を作らない。
- **実物との食い違い 2 件**: ① 手元像は 104,860B で GF/window の .o も欠け、S6 の 239,908B と世代が違う。② `docs/tasks/v3/TASK_T4_T6_MODULES.md` の 96 行は ops 2/18 口、schema は ops 3/19 口。
- 分割実装の最初 (M1) に同一ソース・同一設定の map/size/header を採り直す。旧像と S6 像の差 135,048B をそのまま GF の大きさとはしない。

## 3. 2 つの方式と組み合わせ

概算は新規・実質変更行 (移動、生成物、装置 driver 本体は別)。1 段 = 半日〜1 日、試験込み、実機待ちは別。

| 方式 | 実装の重さ / kdebug 像の概算 | 装置への広がり・HW-BASIC・実機反復 |
|---|---|---|
| A: C + 表を KMOD へ | 5〜7 段、1.3〜2.1 千行 + 約 4 千行移動。核 115〜150KiB | PLL・独自停止・I2C 等を既存 C で表現できる。変更は部品再ビルドと対照合、初版は再起動して交換。HW-BASIC との重複は主に frontend で、recover 検査は共通 |
| B: 装置部分を命令列 + 許可データへ | 10〜14 段、3〜5 千行。核 140〜185KiB、GF データ 50〜90KiB (未リンク概算) | expr VM・PLL の有界探索・bus 別名・cleanup 表現・入力寿命検査まで必要。DMA/本番 driver 所有権は別途残る。HW-BASIC と最大限共有でき、承認済みデータ変更の反復は速い |
| 組合せ: A + 小さい共通 plan 実行器、C の純計算と bus adapter | A に追加 4〜6 段、1〜2 千行、核追加 8〜20KiB を目安 | HW-BASIC は userland で構文と対話、kernel は封印済み短い plan を検査。PLL 等は登録 C 式。対話の動的書込みは別設計が要り、今まとめると境界が増える |

- **今回の選択は A のみ。** 既存 GF の検証済み decode/exec/PLL を移す方が軽い。B は 125 行の exec を移すだけでは成立せず、容量節約のために安全契約を作り直す費用が大きい。
- 増えた約 135KB の多くは recover_gf.c の表データ (2071 行の大半、関数は 862 行以降) で、インタプリタ化しても縮まない。ただし増分全体を GF 量とはせず M1 で再計測する。
- A でも核へ渡すものは既存の profile/plan/expr/layout に統一。GeForce 完了後、Trident で必要な共通命令だけ抽出するため、外部 ABI に GF opcode を固定しない。
- HW-BASIC 初版は読取り、続いて「短い plan をコピー → 全経路検査 → 承認 → 実行 → 終端」の対話単位を候補とする。実行中に任意番地を追加しない。現在の 4a 要求にユーザーポインタや任意 opcode を追加しない (`docs/tasks/realhw/TASK_TRIDENT_DRIVER.md` の 41 行)。

## 4. 推奨の構造

ABI の細部 (構造体・slot 表・import の生成・contract_crc・台帳・KDEBUG の ops) は [HWDEV_CORE_API.md](HWDEV_CORE_API.md)。

- 核: recover singleton、profile/plan 検証、snapshot・BAR 測定 cache、期限/abort/REC、共通 98 復帰、window/PTE 排他、装置所有 lease の形と拒否判定、FAILED/cold latch、S0 用の最小 GF 復帰モデル。
- GF 部品: 許可表、固定 plan、ROM decode、GF exec、式/layout/PLL、NV44A bus 変換・局所 cleanup、GF owner と A4 作業領域。
- kernel の小さい HWDEV facade が既存 module API でロード。KDEBUG≠RUNNING ならログなしで飛ばす。RUNNING 時だけ 1 回、HWDEV を依存 handle 付きで開始し、核へ登録。既存 kernel→KDEBUG の ops 末尾へ内部接続口を追加する。
- 一つの `/boot/hwdev.mod`、初版は GF のみ、boot 中交換不可。将来 Trident 等は同じ枠に選択配備。複数装置同時試行は今回不要。
- GF ソースは変更せず、既存関数名の生成 stub が HWDEV imports の slot 経由で核を呼ぶ。装置境界は recover.h:200–219 の recover_ops/opaque を再利用し、local_cleanup/screen_safe/raw_config/read/write を重複定義しない。wrapper が既存 gfinit_call と gfinit_recover_end を call/release_work へ接続する。
- GF bus は核で許可済みの `{target,dir_substep,space,bar,offset,width}` のみ受ける。核の checked 窓・PCI accessor へ戻して実アクセス直前にも identity/所有 lease を照合。装置 C の生 I/O は禁止し、リンク import と静的検査で確認する。
- 装置固有 expr は有限の純整数関数。PLL は limits を検査して事前展開し、実行時も登録式 / rule で照合。callback の番地はロード済み部品 text 内、表はその RO 領域か登録済み専有作業領域内、長さは checked 算術で検査。
- ロード順: KMOD の版/型/CRC/PAIR 検査 → HWDEV manifest と loader 実測 identity 照合 → 核の image CRC との対照合 → 登録 → 対象を照合 → claim → 確保/decode/bind → snapshot/arm。登録中は HW アクセス 0。
- kernel PAIR だけでは別 kdebug との混在を検出できない。ビルド順を kernel→kdebug→HWDEV とし、HWDEV へ最終 kdebug image CRC を埋込む。核から部品 CRC を要求する循環は作らない。
- profile CRC は正規化値と式 ID/型、plan hash は全表と ROM 識別。関数ポインタを hash に入れず、実装は部品 CRC・核 CRC・契約版で固定。未実装式・未知版は拒否。
- v1 の所有権は BDF/platform device ID・I/O/MMIO/IRQ/DMA 範囲・世代の構造体の形と拒否判定だけ。root IDE・LAN/SerialFS・表示 driver の所有中または未分類なら拒否し、動的登録・重複仲裁 registry 本体は最初の BM 装置の設計へ送る。
- v1 claim は「既に停止・非所有」だけ成功。透過的 driver 中断は作らない。将来の引渡しは driver 停止/排出 → 所有世代更新 → 試行 → 停止確認 → driver 再初期化の別契約で、snapshot の生再生による再開を禁止。
- PAUSED も lease と部品参照を保持。解放は局所 cleanup → 共通終端 → 結果保持 → plan/profile 参照解除 → 作業領域 free → lease 解放。A1 成功の ROM/decode 保持は既存例外として参照を残す。
- FAILED/cold・PTE 破損・最後の結果・session 世代は核の boot 寿命。部品の登録や free で消せない。通常終了でも初版の部品 body は常駐、stop=0 で hot unload なし。開始失敗は module の rollback/隔離に従う。
- S0 のみの recover は HWDEV 不在でも使用可能。欠落・対不一致なら GF 試行 op だけ拒否、peek/read-only と既存 recover を残す。

## 5. 大きさ・メモリの見積もり

| 項目 | 見込み / 上限 (KiB) | 根拠・受入条件 |
|---|---|---|
| 核 kdebug.bin | 115〜150 / image 256 | 旧像 102.4 に S6 共通増分・境界を加え GF を除く概算。少なくとも 64KiB 空きを合格条件 (≤192) |
| GF hwdev.mod | 105〜155 / image 192 | 移動約 4 千行、許可表・reloc・import prefix を含む。実測が 192 を超えたら移動境界を再検討、上限を黙って拡大しない |
| 常駐 body | 核 145〜185 + GF 105〜160 / 各 mem 256・192 | BSS 込み、file 上限と別検査。拒否判定/facade 追加は概算 1〜4KiB (kernel 分) |
| ロード時 | HWDEV load_span ≤256 | in-place 読込み、全像二重 staging なし。prefix 解放後も必要な契約は部品登録情報に保持 |
| 試行 RAM | 上限は [CORE_API §9](HWDEV_CORE_API.md#9-部品の寿命-manifest-v1-m2m3a-の入力契約) | M1 で最悪 node 数の実測に訂正。ROM/decode/plan/flow を含む。二重コピーしない |
| 将来 DMA RAM | 装置別の明示上限、v1 は 0 | 要件は [DEVICE_STATES.md](DEVICE_STATES.md) §2 |

- 合計の目安は常駐 250〜345KiB + 上表の試行 RAM、OS 既存使用量は別。32MB だから確保成功とはせず、必要な連続 block が取れなければ開始を拒否。
- 現行 S6 像 234.3KiB から、核だけで概算 84〜119KiB 削減。全部品合計の file 量は増え得る。分離の目的は核上限と装置追加時の増加を切り離すこと。
- 2048 node 最悪・first-fit 断片化・各確保失敗点を検査。P100 の全経路検査時間も測り、node→rule cache と整数 bitset を維持、無制限探索・圧縮展開を追加しない。

## 6. GeForce 4a S1〜S6 の移し方と S7 以降

| 既存成果 | 核に残す | GF 部品へ移す |
|---|---|---|
| S1 許可表 / S2 decode | 共通 descriptor・bind・全経路検査 | recover_gf、fixture 対応、ROM span/CRC、head/skip/layout |
| S3 exec / S4 PLL | 安全点・課金・式検査・enter/leave | gfinit_exec を意味変更なしで移動、PLL parser/solver/展開と MIT 表示 |
| S5 bus/窓 | kdebug_window、peek、PTE 失敗ラッチ | NV44A 別名、raw config 照合、CR1F 変換と局所 cleanup 列 |
| S6 統合 | REC/abort/98 復帰、単一 owner・cold/FAILED、UART | gfinit owner/plan/resources、A4・ROM/decode 作業領域、op 処理 |

| 関数・データ単位の配置 | 判断 |
|---|---|
| recover.c:294–340 recover_gf_pre / recover_gf_result / gf_bus_record | 核に残す。REC 送信・課金・失敗伝播を共有し、前 2 関数を slot 公開 |
| recover.c:472–490 の GFX port 検査、zero_target (:534) と検証/復帰側の全呼出し | 核に残す。GF の zero 保持と共通 accessor/復帰の安全条件 |
| recover_profile.c:165–281 の gf_targets / gf_rules / gf_regs / gf_quiet、recover_gf_profile / recover_gf_screen_safe | 核に残す。S0 用の最小 GF 復帰モデルと停止確認、予算。HWDEV 不在でも使用可能 |
| gfinit*.c/.h の全関数・データ、recover_gf.c の template/許可表/式/固定 plan | ソース位置を維持して HWDEV 像へリンク。核からの直接 symbol 参照は call/release_work へ置換 |

- N1・CR1F・0FACh・CUI/31kHz・S0 と S1 の区別などの安全条件は [GF6200_4A_DESIGN.md](GF6200_4A_DESIGN.md) と [REALHW_RECOVER_DESIGN.md](REALHW_RECOVER_DESIGN.md) のまま維持する (この分離で変えない)。GF 部品には 0FACh 操作を渡さない。
- 既存 fixture・host 期待 trace・変異 ID は保持、リンク/import だけ追随 (ソースパスは維持)。核単体 mock に加え、実 KMOD の型と ABI を通る結合試験で境界欠落を検出する。
- 順序: M1〜M5 → 旧 S7 → T4b → T5 → S8 (A1 承認 → 結果 → A2 承認) → main 0/1/2/3 を各承認 → A4 承認 → T6/T7 の残件 (段の表は票 TASK_HWDEV §3)。
- S6 送りの上限 assert・最悪 node 試験 (M1 の上限訂正は CORE_API §9)は M1、Ra266 0:8.0/0:7.0 の BAR 長根拠は T5 前、version.chip の実 ROM 確認も実機開始前。T4a 合格は再利用し、部品境界の回帰だけ追加。
- 60 秒実行枠と実 UART 送信時間の除外、wire 上界 2 倍 + 3 秒、復帰 18/33 秒、PAUSED 時間除外をそのまま継承 ([REALHW_RECOVER_ADDENDUM.md](REALHW_RECOVER_ADDENDUM.md) の 246 行)。

## 7. ファイルごとの変更一覧

| 新規/変更 | 対象 | おおよその行数 (手書き差分) |
|---|---|---:|
| 新規 | `include/hwdev_private.h`、`kdebug/hwdev_core.c` | 100〜160、220〜320 |
| 新規 | `kernel/hwdev_module.c`、`kernel/hwdev_owner.h`・拒否判定 | 130〜200、構造体と拒否のみ (registry 本体は BM 設計へ) |
| 新規 | `hwdev/gf6200/module.c`、import 定義・linker・`build/hwdev.mk` | 100〜160、100〜150 |
| リンク先変更 | `kdebug/recover_gf.c` と `gfinit*.c/.h` は現位置を維持 | C 計 3,926 行 + ヘッダのソース変更なし、build/hwdev.mk で GF オブジェクトを HWDEV へ振分け、核から除外 |
| 変更 | `kdebug/kdebug_main.c`、`recover.c`、`recover_profile.c`、`build/kdebug.mk` | 計 100〜180、S0 モデルは残し main の直接 GF 参照を wrapper 接続へ変更 |
| 変更 | `tools/module_schema.py`、`tools/mkmod.py` の必要な汎用化、import 生成入力・部品ロード接続・配備定義 | 計 80〜140、11 枠目の決定注記を更新、schema 生成物は別 |
| 新規/変更 | `tools/tests/` module/HWDEV 結合・既存 gfinit/recover、`tools/check_map.d/` | 計 300〜500、host 15 本・check_map 8 本の kdebug/gfinit* パスを維持、像/map 期待と T5c-6 を更新 |
| 変更 | 指定 task 票・設計追記・S7 の SDK 生成元と shell | 本分離の文書 80〜120、旧 S7 工数は別 |

## 8. 試験と変異

段・重さ・実装モデル・合格条件の要約は票 TASK_HWDEV §3。並列: M1 は後続の前提なので直列。M2 は M1 の後、M3 の移動準備と並列可。M3 は M2 と別ファイルの作業だけ並列、結合は M2 の後。M4 は M3 の後に直列。M5 は M4 の後に結合。

- M1 試験: 2048 node・断片化・全確保失敗点で書込み 0 / 全頁返却、現行 size/map を保存。
- M1 変異: 頁丸上げ又は最大 node 検査を落とすと上限試験が失敗。
- M2 試験: 版・CRC・kernel/core 対・依存世代・callback 範囲・欠落の各拒否、既存 10 部品回帰。T5c-6 の 11 番目拒否を HWDEV 受入へ変更し、未定義 12 番目・容量超過拒否を維持。HWDEV 不在で S0 の GF 復帰を検証。
- M2 変異: core CRC 比較を除去すると混在像試験が失敗。
- M3 試験: 27 種・PLL/COPY/A4・CR1F・N1・全出口の既存期待 trace を部品境界越しに一致。
- M3 変異: checked accessor を迂回 / cleanup 順を交換すると拒否・trace 試験が失敗。
- M4 試験: busy 所有者、PAUSED abort、外部 recover、二重終端、開始途中失敗、cold 後再登録を検査 (所有中・未分類は入力で注入、registry 本体は不要)。
- M4 変異: 世代照合又は参照保持を外すと再利用・解放試験が失敗。
- M5 試験: 通常/KDEBUG ビルド、schema/import 生成差分、像/body/prefix 各上限、該当 host 試験と check_map lint。KDEBUG≠RUNNING で HWDEV load/start・BAD_DEPENDENCY ログが 0 件。
- M5 変異: 上限又は KDEBUG=0 門を外すと負例が失敗 (PM 実行)。
- 旧 S7: 固定 40B wire と shell、既定 ABI 手順。T4b: 試験は拒否/復帰 trace、変異は PAUSED 門の除去。
- T5/S8/各 main/A4/T6/T7: 試験は各既定受入、変異は host で相応の停止/guard 欠落を検出し、実機で危険変異を流さない。

## 確定 (2026-10-22、Opus 第 2 回の条件付き承認、PM が反映。r3 は不要とされた)

各項の本文は [HWDEV_CORE_API.md](HWDEV_CORE_API.md) に入れてある。

- G1 (P1): hwdev_ops.call を gfinit_call と同じ 6 引数 (transport・S0 profile を含む) にし、wrapper は素通し。
- G2 (P2): recover_hal_wait_tick → recover_hal_wait_frame (存在する名前)、関数ポインタとして使う箇所の再配置を 1 行。
- G3 (P2): HWDEV は自前の .def から import の stub と slot を生成し、核と kernel の関数を 1 つの表に。kdebug_import_stubs.o はリンクしない。
- G4 (P2): contract_crc は生成 STATIC_ASSERT の表と前処理後のヘッダの正規化テキストの CRC の 2 つを照合する方式に決めた。
- G5 (P3、M2 の依頼文に書く): KDEBUG_OPS_VERSION は kernel/module.h:7 の手書きの定義で、GF の identity にも入る。新しい口には kdebug_export_contracts の型契約も 1 行要る。identity の CRC が変わるのは想定どおり。

## 実装の後の変化 (2026-10-23、PM 注記 — 上の確定本文は書き換えない)

- **大きさ**: M3c (92153ea) で kdebug.bin は 240,036 → 109,060B (KDEBUG の上限の 42%)、hwdev.mod は 131,488B (上限 196,608B)。§5 の合格条件の大きさは満たした。
- **ops の版** (§1 の「食い違い ②」): いまは版 4・20 口 (`tools/module_schema.py`、08_build §8-2)。
- **S0 の最小 GF 復帰** (§6 の表の「HWDEV 不在でも使用可能」): `recover_gf_profile`・`recover_gf_screen_safe` は核に残ったが、核の経路から呼ばれていない (M3c より前から)。扱いは M4 で PM が決める (票 TASK_HWDEV の送り項目)。
- **順序** (§8 の「M1〜M5 → 旧 S7 → T4b → T5 → S8」と「BAR 長根拠は T5 前」): 2026-10-23 に見直した (票 TASK_HWDEV §3-1)。BAR 長の根拠は新しい設計の段 BARLEN に置き換え、Trident 0:8.0 に加えて 82557 0:11.0 も対象 (NEC 0:7.0 は BAR が 0 で対象外)。
