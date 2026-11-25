# 新しい検査を 1 本書く

> 状態: P1-4 起草済み (sol のレビュー待ち)。

検査名は保証する契約から付ける。期待する値・失敗条件を現行の設計書の節、KAPI 定義、装置資料に求め、登録に出所を記す。根拠が足りなければ先に設計を補う。足す順はビルドと一致、起動、木の形、デバッグで触る契約とする。旧登録の変換や網羅表の作成は行わない。（P4「新しい検査が満たす事」）

## 本体と入力の置き場

仕様 §6 の予約先は次の 20 か所。必要になったディレクトリから作る。

- `tests/{kern,hal,arch,platform,dev,gfx,sound,input,fs,net,kapi}/` の各々は同名の `sys/` 下の実装を担当する。
- `tests/{boot,lib,sdk,userland,tools}/` の各々はルートの同名の実装を担当する。
- 1 所属の 1 契約はその所属に置く。複数所属の結合は `tests/integration/`、起動・ゲスト・実機は `tests/system/`、ビルドと検査基盤は `tests/infra/` に置く。
- 本体は `tests/<所属>/<id>.py`。共通ハーネスと fixture は `tests/support/` に置き、入力データは `tests/support/fixtures/<id>/` に置く。支援だけのファイルは独立した検査に数えない。

## ホストで組む範囲と機種別の実行

対象の製品 C と実ヘッダをテスト用 main にリンクし、入力を与えて戻り値や出力を観測する。差し替えるのは I/O や特権命令などホストに無い境界だけ。支援は指定した作業木の build/config.mk を対象の ARCH・PLATFORM で make 展開し、INC_KERNEL の順序を保つ。親の MAKEFLAGS 等を除き、生成先を一時領域にする。必要な fixture の include を先頭に加え、残りも同じ作業木から読む。変異時に元の木へ戻る探索は禁止する。コンパイルだけで終わる契約は L1 として扱う。

KAPI・FS・HAL の共通契約では、入力と期待値と比較関数を本体に一度書く。例えば同じファイルを読み、PC-98 と PowerPC の実行側が返したバイト列と長さを同じ関数に渡す。起動・呼出規約・ログ採取を機種側に閉じ、結果は構成ごとに残す。割り込みや MMU 固有の契約は各所属で検査し、物理デバイスのタイミングは L5 として限界を記す。

## 最初の登録例 — PowerPC の配布像を組めること

P4 順 1 のうち `ppc_media_build` は `tests/infra/ppc_media_build.py` に置く。期待の出所は [CI 設計 §0](../ci/ci_v2.md) の PowerPC 6 像と必須ファイル欠落時の赤。対象は製品の Make 規則とそこから組む入力。各構成を清浄な候補 SHA の木で `make ARCH=ppc PLATFORM=powermac <下記の指定> all iso` で組む。既存のビルドジョブをこの呼出しに載せ、出力を配布にも使う。

| 構成 | make への指定 | build/out/ppc-kboot/ 以下の出力先 |
|---|---|---|
| core | KBOOT_LINK=0 | . |
| link | KBOOT_LINK=1 | link |
| sh | KBOOT_SH=1 KBOOT_LINK=0 | link-sh |
| link-sh | KBOOT_SH=1 | link-sh |
| shell | KBOOT_SHELL=1 KBOOT_LINK=0 | link-sh |
| neg1 | NEGATIVE=1 KBOOT_LINK=0 | neg1 |

実行器は作業木のルートで `python3 tests/infra/ppc_media_build.py <構成> build` を呼ぶ。build は上表の make を実行する。終了値 0 のときだけ、同じ木で末尾の引数を `verify` に替えて呼ぶ。verify は再ビルドせず、指定先の PPCKERN.ELF と boot.iso がそれぞれ非空の通常ファイルかを調べる。成功は 0、製品のビルド失敗・欠落・空ファイルは 1、道具不足や読出し異常は 2。診断には構成と原因を書く。6 構成の通常実行が全て通ればこの契約が通る。

変異の実行器は隔離した core の木で build が成功した直後（after_build）に、登録で指定した boot.iso を削除し、通常時と同じ verify を呼ぶ。終了値 1 と診断 `missing boot.iso` が揃えば検出となる。登録の全文は[登録設計の見本](registry_v2.md)に置く。当面は適用対象を毎回実行し、REUSED は使わない（U11）。バイト再現性と起動到達は別の検査が判定する。

## 起動の受入と既存の道具

PowerPC は QEMU で決めた到達点を採る。PC-98 は U11・U13 により、取り込みごとに pc98emu が CI の配布媒体を NP21/W で起動する。自動化する範囲は、候補 SHA、配布 run/attempt、媒体の sha256 を照合し、到達点と画面又はログの所在を集計へ渡すところまで。U21 により文書変更でも両機種の記録を取り直す。runner は hosted から始める。（DECISIONS の U11・U13・U21）

D-8 はゲストに配備する診断と入力・実機台本を userland/diag/ にまとめる。atadeny.bin と learnprb.bin のソースも含む。同じ配備物を作る Rust クレートは現行 Cargo 入力位置に保ち、ホストの QA は tests/ に書く。（P1_LAYOUT の D-8）

D-12 は配置表が「A3 で P1-4 が扱う」とした項。P4 順 3 の前に PM に諮る。推奨は、現行設計の期待を満たす層・文書・KAPI 互換の静的解析器を新しい登録から呼び、検査本体と道具の試験は現行契約から書く案。旧パスを自前で持つ層判定は所有の入口に合わせてから載せる。もう一案は解析器も含めて書き直すこと。現行の設計で使える解析処理まで作り直す工数が増える。[所有の入口](../../build/ownership.md)
