# OS32 作業別の入口

> 索引: 「何をしたいか」から読む所・触る所・検証へ案内する (2026-10-23 に DEVELOPMENT §1 から切り出した)

> 状態: **現行 (2026-10-23)** — 仕様・規則・番地・手順の本文はここに置かない (正典は INDEX.md「情報単位ごとの正典」 の表)。ファイルと役割の対応は DEVELOPMENT.md §2 (ファイル地図)。

| やりたいこと | 先に読む | 触る場所 | 検証 |
|---|---|---|---|
| KernelAPI を足す | [KAPI_SPEC.md §3-1](KAPI_SPEC.md) (手順の正典) | `sdk/kapi.json` → 再生成 → 実体 | `make clean && make all`、`make check`、NHD 配備後に呼ぶテスト |
| カーネルを直す | 該当部の仕様 [01](01_system.md)〜[10](10_notes.md)、[CONSTRAINTS.md](CONSTRAINTS.md) | `kernel/` `drivers/` `fs/` `gfx/` (下の地図) | `make kernel` → NP21/W 停止 → `make deploy-kernel` → 起動 → kselftest と `ver` ([POLICY_DEBUG §2](POLICY_DEBUG.md)) |
| ドライバを足す / 変える | [05_drivers.md](05_drivers.md)、ハード資料 `docs/hw/` (UNDOCUMENTED 優先) | `drivers/`、定数は [C4] の三層 | 同上。GUI 向けバックエンドは 決定 `decisions/gui/DESIGN.md` §7 の指針 |
| 外部プログラムを書く | [09_exec.md](09_exec.md)、[KAPI_SPEC.md](KAPI_SPEC.md)、`sdk/example/` | `userland/` (本体) / `apps/` `game/` (submodule) | 自層の `deploy.yaml` に登録 ([V2])、`make hotdeploy` で回し、最後は NHD 配備 ([V1]) |
| ユーザ空間ライブラリを触る | 各設計書 `tasks/lib*/`、[07_shell.md](07_shell.md) | `userland/lib/<name>/` | 対応する `userland/tests/<name>_test` |
| シェルを触る | [07_shell.md](07_shell.md) | `userland/shell/` | rshell から `/api/cmd`。パイプ・リダイレクトは `ext_cmd1 \| ext_cmd2` も |
| 日本語入力 (FEP) | 票 `tasks/fep/00_INDEX.md` (進捗の正典) | `kernel/ime*.c` | メモリ os32-fep-testing の手順、`/api/key` |
| GUI シェル | 票 `tasks/gui/TASKS.md` (§7 = 経過の正典) → 各票、決定 `decisions/gui/API_CONTRACTS.md` (凍結) | 票の排他ゾーン (§3) | `os32gui` → `/api/key` / `/api/mouse` (`ax/ay`) / `/api/screenshot`、`ring3_guard cirrus\|pegc\|bb`、`gfxmode` + reset で 3 バックエンド、Cirrus は ini の WAB ([D2]) |
| CI / 静的ゲート | `.github/workflows/check.yml` (ツールチェーンも rustc も不要の検査だけ)、`build.yml` (本体ビルド + `make check-fast`、[08_build.md §8-6](08_build.md)) | `tools/check_*.py` | push で自動。実機は WSL 側 |
| V86 / DOS | `docs/archive/v21/v86v2/README.md` | `kernel/v86*.c` | `v86 -t` / `-b`、脱出は CTRL+STOP |
| LAN (LGY-98) | ドライバ 票 `tasks/network/PLAN.md` (M0〜M5、進捗 §9)、リンク層+Host Services 票 `tasks/network/LINK_PLAN.md` | `drivers/net/ne2000/lgy98.c` `ne2000.c` `ne2000_ring.c` `ne2000_io.asm`、有効化は `make kernel-lgy98` (戻すのは `kernel-nolgy98`) | `make check` のホスト試験 (リング計算)、`make check-net-m2` (NP21/W で inject → 反射 → capture)、ini は [D2] |
| ビルド・配備の仕組みを変える | [08_build.md](08_build.md) | `Makefile` `build/*.mk` `tools/*.py` | `make check`、`os32-cycle deploy` |
| 障害を追う | [POLICY_DEBUG.md](POLICY_DEBUG.md) (§2 反映確認 → §4 教訓集 → §5 道具) | — | `tools/np21w_mcp/`、`/api/tvram` |
| 資料を直す | INDEX.md の正典表で更新先を 1 つに決める | その 1 か所 + 参照側は要約のみ | `make check` (件数・制約 ID)、`make docs-win` |
