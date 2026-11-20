# ARCH_BRINGUP_ORDER — 新しいアーキテクチャの立ち上げ順

> 2026-11-20 の見直しで x86 の形に改めた ([DESIGN_X86_AUDIT](DESIGN_X86_AUDIT.md))。

出所: PowerPC の S-A・S-B・S-C と W6 の設計および QEMU 受入 (2026-11-12)。

> 状態: **現行 (2026-11-20)** — 製品受入の基準は x86 のシェル枠と起動ループ。段の状態は票 TASK_PPC_ALIGN_X86、段の定義は PPC_SHELL_ALIGN_X86_DESIGN の表を参照する。

## 順と受入

各段の受入を通してから次へ進む。PowerPC の例は QEMU での到達点で、実機の受入とは区別する。

| 段 | 次へ進む条件 | 受入の試験 | PowerPC の例 |
|---|---|---|---|
| 起動 | 機種の初期化を終え、診断出力を観測できる | `tools/ppc/kboot_all.py` | kboot・LAN |
| インストール用 RAM root | 読み取り専用 ISO の `rd0` を root、ramfs の `rd1` を `/tmp` に mount し読み書きできる | `tools/ppc/shell_run.py` | W2〜W3、共通化は W6 |
| USER アプリ 1 本 | hello を起動し出力と exit 値を観測、fault 後も復帰する | `tools/ppc/sb_run.py` | W5、S-B |
| USER の入れ子 | USER の親から子を起動し終了して親へ戻る | `tools/ppc/sc_run.py` の N01 | W7、N01 (`nest`) |
| 製品シェル | owner 1 の枠で起動し、子の終了から復帰、シェルの終了後に回収して次のシェルを起動する | 通常の端末で終了値・次の prompt を確認 | PPC_SHELL_ALIGN_X86_DESIGN |
| 保存先 | HDD の ext2 root と共通 VFS/link の /host で再起動後に保存を読める | 製品シェルで書込みと読み戻し | W8、S-D は /host の保存のみ |

## 各段で用意するもの

| 段 | 新しいアーキテクチャ側の用意 |
|---|---|
| 起動 | arch の入口・例外・割込み・時計と、platform の起動・診断出力・LAN provider を結ぶ。 |
| インストール用 RAM root | root image の受渡しと装置名・FS の登録を platform に用意し、共通の `init/boot_root.c`・`build/ram-root.mk` を使う。 |
| USER アプリ 1 本 | arch の USER 遷移・例外・syscall・実行像の公開と platform の exec provider を用意する。 |
| USER の入れ子 | arch の実行 context の保存・復帰と、子の資源・終了値を返す経路を通す。 |
| 製品シェル | 端末の後端をつなぎ、外部シェルを共通の起動ループで選ぶ。 |

KAPI binding は provider のない slot を型に応じた不在として扱う。
PowerPC の `sdk/kapi_target_ppc.py` の不在群を手本にし、成功を偽る stub で埋めない。
USER の ABI と不在値は [09_exec](../../09_exec.md) を参照する。

## 開発用 CLI の扱い

PowerPC では S-A の端末と S-B の USER 実行の診断に常駐 CLI を使った。常駐 CLI と旧 kboot の 5 像は、x86 に寄せる作業の最後に外す。

## 教訓

次の別アーキテクチャでは、tick・mount・exec など本線の共通箇所の設計に、x86 と新アーキテクチャの両方の道を書く。同じ役目の機能を作る前に、統合して作れるかを判断する ([POLICY_DEV](../../POLICY_DEV.md) §1「別のアーキテクチャで機能を作るとき」)。

## 出所の詳細

| 文書 | 内容 |
|---|---|
| [PPC_SHELL_DESIGN](PPC_SHELL_DESIGN.md) | S-A、常駐 CLI と W0〜W9 の計画 |
| [PPC_SHELL_EXEC_DESIGN](PPC_SHELL_EXEC_DESIGN.md) | S-B、USER 実行と実装後の決定 |
| [PPC_SHELL_SC_DESIGN](PPC_SHELL_SC_DESIGN.md) | S-C、入れ子と sh の直接起動 |
| [PPC_RAM_ROOT_W6_PLAN](PPC_RAM_ROOT_W6_PLAN.md) | W6、RAM root 共通化と PC-98 FD の計画 |

受入の試験名は PowerPC の例であり、新アーキテクチャでは同じ条件を観測する試験を用意する。
RAM root は起動後に元の媒体を必要としないことも確認する。
`/tmp` は揮発性であり、S-D で保存先 `/host` の後 mount と区別する。
S-D は CD 切離し後の作成・追記・短い上書き・64KiB 複写・列挙と、FS 不在時の期限・端末継続・再開を確認する。
入口は `tools/ppc/sd_run.py --cpu both --mutate`。AF_INET の無い環境は SKIP とし、実行済みとは数えない。
境界の単体検査は T02/T03 `ppc-hostfs-io`、T04 `ppc-hostfs-server`、T01 `ppc-hostfs-wire`。
開発用 CLI の端末と poll は platform 側に閉じ、CPU 依存を共通 CLI に持ち込まない。
PowerPC では kboot の印と LAN 診断で起動を確認した。USER の入れ子は sh の起動前に確認し、子終了後の親の継続を観測した。
製品のシェルの受入では owner 1 の資源回収と次のシェルの READY まで確認する。
