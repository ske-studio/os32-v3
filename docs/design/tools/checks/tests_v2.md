# 新しい検査を 1 本書く

> 状態: P1-4 確定 (2026-11-26、sol 確定可)。

検査名は保証する契約から付ける。期待する値・失敗条件を現行の設計書の節、KAPI 定義、装置資料に求め、登録に出所を記す。根拠が足りなければ先に設計を補う。足す順はビルドと一致、起動、木の形、デバッグで触る契約とする。旧登録の変換や網羅表の作成は行わない。（P4「新しい検査が満たす事」）

## 本体と入力の置き場

仕様 §6 の予約先は次の 20 か所。必要になったディレクトリから作る。

- `tests/{kern,hal,arch,platform,dev,gfx,sound,input,fs,net,kapi}/` の各々は同名の `sys/` 下の実装を担当する。
- `tests/{boot,lib,sdk,userland,tools}/` の各々はルートの同名の実装を担当する。
- 1 所属の 1 契約はその所属に置く。複数所属の結合は `tests/integration/`、起動・ゲスト・実機は `tests/system/`、ビルドと検査基盤は `tests/infra/` に置く。
- 本体は `tests/<所属>/<id>.py`。共通ハーネスと fixture は `tests/support/` に置き、入力データは `tests/support/fixtures/<id>/` に置く。支援だけのファイルは独立した検査に数えない。

対象パスの一致は検査の分類候補にしかならない。実際に組んだ入力、実行した製品経路、観測した戻り値や出力から契約を決め、観測できない範囲を登録の `limits` に書く。模型を単独で動かした結果やコンパイル成功を製品の実行結果として数えない。

## ホストで組む範囲と機種別の実行

対象の製品 C と実ヘッダをテスト用 main にリンクし、入力を与えて戻り値や出力を観測する。差し替えるのは I/O や特権命令などホストに無い境界だけ。支援は指定した作業木の build/config.mk を対象の ARCH・PLATFORM で make 展開し、INC_KERNEL の順序を保つ。親の MAKEFLAGS 等を除き、生成先を一時領域にする。必要な fixture の include を先頭に加え、残りも同じ作業木から読む。変異時に元の木へ戻る探索は禁止する。コンパイルだけで終わる契約は L1 として扱う。

KAPI・FS・HAL の共通契約では、入力と期待値と比較関数を本体に一度書く。例えば同じファイルを読み、PC-98 と PowerPC の実行側が返したバイト列と長さを同じ関数に渡す。起動・呼出規約・ログ採取を機種側に閉じ、結果は構成ごとに残す。割り込みや MMU 固有の契約は各所属で検査し、物理デバイスのタイミングは L5 として限界を記す。

kapi_sync は target 名・rustc の版・target JSON の内容で検査専用の Cargo 出力先を分け、同じ target の core は Cargo のロックの下で共有する。CI は runner OS・束・toolchain・Cargo.lock・target JSON をキーに取得物とこの出力先を保存し、キャッシュに起因し得るコンパイルエラーでは空の置き場で再試行する。

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

D-12 は決定済み (DECISIONS の U22、ユーザー 2026-11-26「現行の内容を精査し新規でつくる」): P4 の順 3 で、今の層・所有・文書・KAPI の互換の検査と道具が何を保証しているかを読み、要る契約を設計書に書いてから、検査本体も解析の道具も新しく書く。今の道具をそのまま新しい登録に載せ直さない。

## 配布物と再生成の合否

PC-98 は all・kernel-r1 と deploy-u3・iso を組み、kernel.elf・kernel.map・vmkernel.lz4、D88・生 2HD・1440 KiB FD・インストール ISO、PKG、deploy-set.json、hostdrv.tar.gz と格納した HostDrv の manifest を要求する。両 FD と HostDrv と配布カーネル、D88 と生 2HD、deploy-set と配備元を照合する。

main の再生成比較は各構成を同じコミット ID・時刻で清浄な木から 2 回組む。x86 は P2-0 の build_compare.artifacts(all_products=True) と packages の配布物、PowerPC は各指定の ELF とオブジェクトを比較し、パス集合の差も落とす。P2-0 と同じく PowerPC の boot.iso は媒体の時刻が入るため存在だけを要求する。link-sh/root_image_blob.o を生成する構成でもこのファイルは存在だけを比較する（現行の ROOT_BLOB は空）。

PowerPC の common-compile は ppc-ci-image と同じ KBOOT_SH=1・KBOOT_LINK=1 で C_COMMON_PC98_PENDING と C_COMMON_COMPILE_ONLY_PPC の C 入力ごとに PowerPC ELF の再配置可能オブジェクトを生成する。リンクと起動は対象外。

## PowerMac の起動到達点

QEMU mac99・7450・128 MiB に構成ごとの boot.iso を入れる。core はシリアルの
`KBOOT READY stage=pre-user`、link はその後の LAN link=1 と `LINK HEAP used=`、
sh・link-sh・shell は `Loading /sys/shell.bin` に続くシェルのコマンド待ち通知（シリアル EOT）と `LINK HEAP used=`
までを確認する。neg1 は `FATAL trap vector=0x300 reason=0x2 addr=0x80000`
で停止し、READY に進まないことを確認する。PC-98 は配布媒体を pc98emu が手で受け入れる。

QEMU は構成の到達点を観測した時点で終了し、観測中の時間切れは ERROR（終了コード 2）、採取済みのログに必要な到達点が欠ける場合は FAIL（終了コード 1）とする。

CI の媒体検査と PowerMac 起動は同じ attempt の配布物を読む。手元では同じ検査の build 段で媒体を作る。再現性比較は二つの空の木で組み、KAPI の Rust 検査は C の実測値から作る専用 crate を対象 ABI で検査するため、専用の Cargo キャッシュを使う。
