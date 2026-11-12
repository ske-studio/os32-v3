# ARCH_BRINGUP_ORDER — 新しいアーキテクチャの立ち上げ順

出所: PowerPC の S-A・S-B・S-C と W6 の設計および QEMU 受入 (2026-11-12)。

> 状態: **現行 (2026-11-12)** — 起動から USER シェルまでの順と開発用 CLI の扱い。

## 順と受入

各段の受入を通してから次へ進む。PowerPC の例は QEMU での到達点で、実機の受入とは区別する。

| 段 | 次へ進む条件 | 受入の試験 | PowerPC の例 |
|---|---|---|---|
| 起動 | kboot の印が出て LAN の診断が通る | `tools/ppc/kboot_all.py` | kboot・LAN |
| RAM root | 読み取り専用 ISO の `rd0` を root、ramfs の `rd1` を `/tmp` に mount し読み書きできる | `tools/ppc/shell_run.py` | W2〜W3、共通化は W6 |
| 開発用 CLI | 端末で prompt・内蔵コマンドと root の読書きが通る | `tools/ppc/shell_run.py` | W1〜W3、S-A |
| USER アプリ 1 本 | hello を起動し出力と exit 値を観測、fault 後も復帰する | `tools/ppc/sb_run.py` | W5、S-B |
| USER の入れ子 | USER の親から子を起動し終了して親へ戻る | `tools/ppc/sc_run.py` の N01 | W7、N01 (`nest`) |
| USER の sh | root mount 後、kernel から sh を製品の経路で直接起動し端末で使える | `tools/ppc/sc_run.py` | W7、S-C (`KBOOT_SH=1`) |

## 各段で用意するもの

| 段 | 新しいアーキテクチャ側の用意 |
|---|---|
| 起動 | arch の入口・例外・割込み・時計と、platform の起動・診断出力・LAN provider を結ぶ。 |
| RAM root | root image の受渡しと装置名・FS の登録を platform に用意し、共通の `init/boot_root.c`・`build/ram-root.mk` を使う。 |
| 開発用 CLI | `kernel/cli/resident_cli.c` の `resident_cli_ops` に端末と poll をつなぎ、`KBOOT_SHELL=1` の像だけに組む。 |
| USER アプリ 1 本 | arch の USER 遷移・例外・syscall・実行像の公開と platform の exec provider を用意する。 |
| USER の入れ子 | arch の実行 context の保存・復帰と、子の資源・終了値を返す経路を通す。 |
| USER の sh | platform の端末入力・fd1 出力・直接起動を結び、必要な KAPI provider を用意する。 |

KAPI binding は provider のない slot を型に応じた不在として扱う。
PowerPC の `sdk/kapi_target_ppc.py` の不在群を手本にし、成功を偽る stub で埋めない。
USER の ABI と不在値は [09_exec](../../09_exec.md) を参照する。

## 小さな CLI を凍結して残す

ユーザー了承 (2026-11-12「それでOK」) により、機能は help・echo・cat・外部起動 (`run`) で止める。
製品の起動経路には入れず、新しいアーキテクチャの立ち上げ、USER 実行の足場、実機の初期確認に使う。
`sh.bin` が QEMU と実機で安定したら hello・fault の受入を sh 上へ移す。
その後の CLI 像の試験は prompt と外部起動の最小限にする。
CLI を消すかは次のアーキテクチャの着手時に決める。

## 教訓

次の別アーキテクチャでは tick・mount・exec 等の本線の共通箇所の設計に x86 と新アーキテクチャの両方の道を書く。PowerPC の B29 は decrementer を物理 100Hz にした後、本線の物理 1kHz・論理 100Hz の設計と逆向きと分かった。

## 出所の詳細

| 文書 | 内容 |
|---|---|
| [PPC_SHELL_DESIGN](PPC_SHELL_DESIGN.md) | S-A、常駐 CLI と W0〜W9 の計画 |
| [PPC_SHELL_EXEC_DESIGN](PPC_SHELL_EXEC_DESIGN.md) | S-B、USER 実行と実装後の決定 |
| [PPC_SHELL_SC_DESIGN](PPC_SHELL_SC_DESIGN.md) | S-C、入れ子と sh の直接起動 |
| [PPC_RAM_ROOT_W6_PLAN](PPC_RAM_ROOT_W6_PLAN.md) | W6、RAM root 共通化と PC-98 FD の計画 |

受入の試験名は PowerPC の例であり、新アーキテクチャでは同じ条件を観測する試験を用意する。
RAM root は起動後に元の媒体を必要としないことも確認する。
`/tmp` は揮発性であり、永続化する媒体の後 mount は別の段で扱う。
開発用 CLI の端末と poll は platform 側に閉じ、CPU 依存を共通 CLI に持ち込まない。
USER の入れ子は sh の起動前に確認し、子終了後の親の継続を観測する。
製品の sh の受入では、直接起動と端末入出力に加え子の終了からの復帰を確認する。
