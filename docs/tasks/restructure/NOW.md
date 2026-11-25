# 作り直しの前の実物 (2026-11-25、main `92ad7c6e0`、タグ `v3-snd-hal-20261125`)

> 上位: [TASK_RESTRUCTURE.md](TASK_RESTRUCTURE.md)・[RULES.md](RULES.md) (入口と、どの段も先に読む決まり)

追跡ファイルは 6,473 本。数は `git ls-files <置き場> | wc -l`。

| 今の置き場 | 本数 | 仕様の行き先 (仮。確定は P1) |
|---|---|---|
| `kernel/` 129・`mm/` 30・`exec/` 73・`init/` 12 | 244 | `sys/kern/` (下位の分け方は D-1) |
| `arch/` (x86 63・ppc 72) | 136 | `sys/arch/` |
| `platform/` (pc98 153・powermac 162・none 1) | 317 | `sys/platform/` |
| `drivers/` 260・`hwdev/` 161 (うち `hwdev/gf` 143) | 421 | `sys/dev/` (GeForce の寄せ方は D-2) |
| `kdebug/` (kselftest 38・gfinit_* ほか) | 160 | 仕様に行き先が無い (D-3) |
| `gfx/` 72・`sound/` 14・`input/` 30・`fs/` 73・`net/` 16・`kapi/` 22 | 227 | `sys/<同名>/` |
| `sound/hal/` 14・`input/hal/` 28・`include/*_contract.h` | — | `sys/hal/` に寄せるか (D-4) |
| `include/` | 119 | `sys/include/` |
| `lib/` (softfloat 82・os32_opna 34・sqlite3 14・zlib 13 ほか) | 178 | `lib/` に残す物とカーネル専用の物の分け (D-5) |
| `build/` (`.mk` 約 60 本、所有者集合は `build/select.mk`・`build/sources/`・`build/arch/`・`build/platform/`・`build/features/`) | — | `build/` に残す。`sys/conf/` に何を置くか (D-6) |
| `boot/` 23・`sdk/` 72・`userland/` 678・`assets/` 24 | — | そのまま |
| `sample/` 11・`packages/`・`images/`・`game/`・`apps/` (submodule、H-7 で延期) | — | D-7 |
| `tools/tests/` 1,840・`tools/checks/` 317・`tools/accept/` 63・`tools/ppc/` 188 (道具と試験が混在)・`tools/ci/` 9 | 2,417 | `tests/<対象>/` と `tools/` (運用の道具) に分ける |
| `userland/tests/` 136 (ゲストで動く試験のプログラム) | — | D-8 |

検査: 登録は 814 本 (`python3 -B tools/checks/run.py --list | wc -l`)、`CHECK` を持つファイルは 847、変異を持つ試験ファイルは約 910。CI は check (static-check・ppc-compile・ppc-qemu 20 組)・build (artifacts・checks 6 組)・sep (`wt/sep-**` だけ、生成物の一致と変異込みの検査)。

既にある道具 (作り直さず、足りない所だけ足す):

| 道具 | 役目 | この票での使い所 |
|---|---|---|
| `tools/move_files.py`・`tools/include_resolve.py`・`tools/source_roots.py`・`tools/tests/kernel_includes.py` | 対応表 JSON からの `git mv` と参照の置換、include の解決先の前後比較、走査の根、試験の `-I` | P3 の移動 |
| `tools/build_compare.py`・`tools/ci/sep_identity.py`・`.github/workflows/sep.yml` | 生成物の一致 (x86 は出荷する生成物、PowerPC は `.o` の sha256) | P2-1 で「置き場が変わる」場合に対応させ、P3 の移動の合格に使う |
| `tools/check_layers.py`・`tools/layers_baseline.json` | 共通 → arch・機種・装置の読みを止める (基準 0)。持ち主は**パスで決めている** | 古い検査なので移動から回さない。所有の表から引く形で `P4.md` の 1 に載せる (D-12) |
| `build/select.mk` と `build/sources/*.mk` ほか | ビルドの所有者集合と順 (`build/sources/order.mk`) | P1-2 で「所有の表の元」にする候補 |
| `tools/checks/run.py`・`tools/ci/gate.py`・`tools/ci/land_gate.py`・`tools/check_time.py` | 検査の登録・選択・実行・表・取り込みの判定 | P6 |
| `tools/move_docs.py`・`tools/check_docs_links.py`・`check_docs_orphans.py` | 文書の移動とリンクの検査 | 移動では置換だけ。文書の検査は `P4.md` の 1 で載せる |

関係する既存の票 (重なりは P6・P8 で畳む。それまでは各票が正典):
`v3/TASK_CI_REWORK` の S3〜S7 (入口・一覧・表・SKIP・重み) = 仕様 §8・§11・§14 の一部、`ppc/TASK_PPC_SEP` の残件 = 仕様 Phase 8、`v3/TASK_REDUNDANCY` = 仕様 §15 の重複実装、`v3/TASK_POLICY_LIGHT` の試験の 4 段 = 仕様 §7 の分類、[DEFERRED_TESTS](../DEFERRED_TESTS.md) §2 の `CI-WAIT-PPC20`。
