# OS32 作業別の入口

> 索引: 「何をしたいか」から読む所・触る所・検証へ案内する (2026-10-23 に DEVELOPMENT §1 から切り出した)

> 状態: **現行 (2026-10-23)** — 仕様・規則・番地・手順の本文はここに置かない (正典は INDEX.md「情報単位ごとの正典」 の表)。ファイルと役割の対応は DEVELOPMENT.md §2 (ファイル地図)。

| やりたいこと | 先に読む | 触る場所 | 検証 |
|---|---|---|---|
| KernelAPI を足す | [KAPI_SPEC.md §3-1](KAPI_SPEC.md) (手順の正典)・[依頼の型](../.claude/skills/os32-delegate/SKILL.md) | `sdk/kapi.json` → 再生成 → 実体 | [08_build §8-4](08_build.md)・[ROLES §0「検査」](tasks/agents/ROLES.md) |
| カーネルを直す | 該当部の仕様 [01](01_system.md)〜[10](10_notes.md)、[CONSTRAINTS.md](CONSTRAINTS.md) | `sys/kern/init/` `sys/kern/` `sys/kern/mm/` `sys/arch/x86/kernel/` `sys/dev/` `sys/fs/` `sys/hal/gfx/` (下の地図) | [08_build §8-4](08_build.md)・[ROLES §0「検査」](tasks/agents/ROLES.md) |
| ドライバを足す / 変える | [POLICY_PCIDEV.md](POLICY_PCIDEV.md) (PCI 装置の扱い)、[05_drivers.md](05_drivers.md)、ハード資料 `docs/hw/` (UNDOCUMENTED 優先) | `sys/dev/`、定数は [C4] の三層 | [08_build §8-4](08_build.md)・[ROLES §0「検査」](tasks/agents/ROLES.md) |
| 外部プログラムを書く | [09_exec.md](09_exec.md)、[KAPI_SPEC.md](KAPI_SPEC.md)、`sdk/example/` | `userland/` (本体) / `apps/` `game/` (submodule) | [08_build §8-4](08_build.md)・[ROLES §0「検査」](tasks/agents/ROLES.md) |
| ユーザ空間ライブラリを触る | 各設計書 `tasks/lib*/`、[07_shell.md](07_shell.md) | `userland/lib/<name>/` | 対応する `userland/tests/<name>_test` |
| シェルを触る | [07_shell.md](07_shell.md) | `userland/shell/` | rshell から `/api/cmd`。パイプ・リダイレクトは `ext_cmd1 \| ext_cmd2` も |
| 日本語入力 (FEP) | 票 `tasks/fep/00_INDEX.md` (進捗の正典) | `sys/kern/ime/ime*.c` | メモリ os32-fep-testing の手順、`/api/key` |
| GUI シェル | 票 `tasks/gui/TASKS.md` (§7 = 経過の正典) → 各票、決定 `docs/design/sdk/include/os32/os32_gui_shared.md` (凍結) | 票の排他ゾーン (§3) | `os32gui` → `/api/key` / `/api/mouse` (`ax/ay`) / `/api/screenshot`、`ring3_guard cirrus\|pegc\|bb`、`gfxmode` + reset で 3 バックエンド、Cirrus は ini の WAB ([D2]) |
| CI / 取り込み | [ci_v2 §0](design/tools/ci/ci_v2.md) | `.github/workflows/restructure-build.yml`、`tools/ci/land_gate.py` | 候補 SHA の全ビルド、同じ run の最新 attempt の全件集計、両機種の起動 |
| V86 / DOS | [V86 の現行設計](design/kernel/v86/README.md)・[09_exec](09_exec.md) | `sys/platform/pc98/legacy/v86*.c` | `v86 -t` / `-b`、脱出は CTRL+STOP |
| LAN (LGY-98) | ドライバ 票 `tasks/network/PLAN.md` (M0〜M5、進捗 §9)、リンク層+Host Services 票 `tasks/network/LINK_PLAN.md` | `sys/dev/net/ne2000/lgy98.c` `ne2000.c` `ne2000_ring.c` `ne2000_io.asm`、有効化は `make kernel-lgy98` (戻すのは `kernel-nolgy98`) | [08_build §8-4](08_build.md)・[ROLES §0「検査」](tasks/agents/ROLES.md) |
| ビルド・配備の仕組みを変える | [08_build.md](08_build.md) | `Makefile` `build/*.mk` `tools/*.py` | [08_build §8-4](08_build.md)・[ROLES §0「検査」](tasks/agents/ROLES.md) |
| 障害を追う | [POLICY_DEBUG.md](POLICY_DEBUG.md) (§2 反映確認 → §4 教訓集 → §5 道具) | — | `tools/np21w_mcp/`、`/api/tvram` |
| 共通・CPU・機種・装置の境目に触る | スキル `.claude/skills/os32-arch-sep`、[層の検査 §3・§4・§8](design/tools/check_layers.md)、[ソースの選択](design/build/select.md) | 持ち主の置き場 (`sys/arch/<cpu>/`・`sys/platform/<機種>/`・`sys/dev/`・共通)、口は `sys/include/<名>_contract.h`・固定名 `platform_<名>.h` | `python3 -B -m tests.infra.ci run --check <id>` (include_layers・link_layers・build_sources)、両方の像のビルド |
| 検査を足す・変える | [ROLES §0](tasks/agents/ROLES.md) | 検査本体と `tests/registry.toml` ([登録仕様](design/tools/checks/registry_v2.md)) | `python3 -B -m tests.infra.ci run --check <id> --mutate` |
| CI の所要を調べる | スキル `.claude/skills/os32-ci-inventory` | 同じ run のジョブ所要と結果表 | 集計結果の bundle seconds を参照 |
| CI の赤を読む・回し直す | スキル `.claude/skills/os32-ci-read`、[POLICY_CI.md](POLICY_CI.md) | — | `gh run list` (数分おきに 1 回) |
| NP21/W の受入を別の機械で回す | スキル `.claude/skills/os32-emu-remote`、[08_build §8-5](08_build.md) | 枠 (ポート・ini・NHD・HostDrv) は pc98emu の機械の `C:\np21w`・`C:\os32-N` | 配布物の BUILD_INFO・manifest の `build=`・ゲストの `ver` の照合 |
| 票を完了にする・文書を減らす | スキル `.claude/skills/os32-archive`、[POLICY_ARCHIVE.md](POLICY_ARCHIVE.md) | 要約は `docs/design/<ソースと同じパス>.md`、移動は `tools/move_docs.py` | 登録の docs_* 4 本、`python3 -B -m tests.infra.ci lint` |
| ユーザーの決定を残す | スキル `.claude/skills/os32-decision-fold` | [POLICY_DEV §10](POLICY_DEV.md)・[ROLES §6](tasks/agents/ROLES.md) の表 | 文書の検査 |
| 資料を直す | INDEX.md の正典表で更新先を 1 つに決める | その 1 か所 + 参照側は要約のみ | [08_build §8-4](08_build.md)・[ROLES §0「検査」](tasks/agents/ROLES.md) |
