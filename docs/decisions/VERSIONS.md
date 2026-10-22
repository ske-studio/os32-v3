# VERSIONS — 版数の対応と v3 の fork の決定

> 状態: **現行 (2026-10-22)** — 版数の対応表 (§1) と、現行の開発 v3・os32 v2.x の扱い・fork の決定 (§2) の正典。2026-10-22 に ROADMAP §0・§0-1 からそのまま移した (ROADMAP は版ごとの計画と経緯の索引になる)。

## 1. 版数の対応表

| 線 | 現在 | 意味 | 記録 |
|---|---|---|---|
| **カーネル** | **2.1 — 完了 (os32、戻り先)** (タグ `v2.1`、2026-09-29、KernelAPI v68。os32-v3 の初期コミットはこの時点の写し) | 2.0 (2026-09-03) はリング 3 (CPL=3) ネイティブ。2.1 は **v3 へ進む前の区切り** — 実機 PC-9821Ra266 で FD 起動・CD からの HDD インストール・HDD 起動まで通し、Ra266 の PEGC 640x480 を画面を見ない条件で受け入れた (ユーザー決定 2026-09-29) | [RELEASE_v2.1.md](../RELEASE_v2.1.md)、CHANGELOG.md、`docs/archive/kernel_v2/PLAN.md` |
| **GUI シェル** | **1.4 で閉じた** (2026-09-29) | 1.1〜1.4 は ROADMAP §1。1.4 の範囲 (Host Services N1〜N4、エディタ GUI 版、About、R2 計測) はすべて v2.1 に入った。N5 (実機 LAN) は実機の内蔵 82557 の票 (L-B) に吸収。**GUI の版はここで閉じ、以後の GUI の作業は v3 の線で扱う** (ユーザー決定 2026-09-29) | ROADMAP §1 |
| **v3** | **現行** (2026-09-30〜、このリポジトリ os32-v3。**本案は 2026-09-30 に確定。段の進捗は各票の状態行、現在地は HANDOVER**) | 機能を足す前に入れ物を作り直す版。本案のうち目的・範囲・目標は [V3_GOALS.md](V3_GOALS.md)、柱と順序は tasks/v3/V3_PLAN.md、票は INDEX の「v3」節。一部の票 (HAL_WIRING・PCM・KHEAP の切り直し・デバイス窓の帯) は v2.1 に先行して着地した。**2026-09-15 のユーザー決裁で「v2」ではなく v3 と呼ぶ** (出荷済み 2.0 と衝突するため) | ROADMAP §2 |
| ゲーム基盤 | v4 (草案) | `docs/archive/v4/V4_GAME_PLATFORM_DRAFT.md`。v3 の後 | — |

**OS64** (草案 [LEGACY_LIVING_PRESERVATION.md](../LEGACY_LIVING_PRESERVATION.md)・[AUXILIARY_CORE_SERVICE.md](../AUXILIARY_CORE_SERVICE.md) に出る語) は**版数表に載せない** (ユーザー決定 2026-09-29) — OS32 とは別の 64 ビット OS の構想で、v3 / v4 の範囲に入れない (ユーザー決定 2026-09-30)。語の定義は LEGACY_LIVING_PRESERVATION.md の冒頭。

## 2. 現行の開発 (v3 = このリポジトリ os32-v3) と、os32 v2.x の扱い (ユーザー決定 2026-09-29)

- **現行の開発は v3、場所はこのリポジトリ `os32-v3`**。2026-09-30 に os32 から**別リポジトリとして fork した** (ブランチを切るのではない。
  新しい履歴で始め、v2.x までの履歴は os32 に)。本案は [V3_GOALS.md](V3_GOALS.md) と tasks/v3/V3_PLAN.md (2026-09-30 確定)、草案と票は `docs/tasks/v3/` ほか (INDEX の「v3」節)。
- **[os32](https://github.com/ske-studio/os32) の v2.x — タグ `v2.1`、`main` — は「戻り先」として保つ**。新機能は入れない。
  手を入れるのは、**戻る必要が生じたとき**と**致命的な不具合のとき**だけ。os32 側の docs は v2.1 時点の記録で、更新しない。
- fork の段取りで**決まっていること** (未決は無い。**2026-09-30 に実施、os32-v3** — 段取りと結果は tasks/v3/FORK_PLAN.md §3 (アーカイブ: `docs/archive/v3/FORK_PLAN.md`)):

| 項目 | 決定 |
|---|---|
| リポジトリ名 | **`os32-v3`** (https://github.com/ske-studio/os32-v3、2026-09-30 作成) |
| 公開範囲 | **パブリック** |
| submodule (`apps/` `game/`) | os32-v3 も同じ submodule を参照して引き継ぐ。**当面の保守は os32 側で続ける** (一旦) |
| CI | **os32-v3 で作り直す** (os32 の `.github/workflows/build.yml` をそのまま移さない) |
| 文書の正典 | **os32-v3 (このリポジトリ) に置く**。os32 の docs は v2.1 時点の記録 (os32 の INDEX 冒頭の注記) |
| 共有の道具 (`tools/`、NP21/W のフォーク `~/np21w-src`) | 共有するが、**動作保証は v3 側だけ** — v2.x 側で動かなくなっても直す義務は無い |

**KernelAPI の版と関数表の容量**: 本数・残り・オフセットの正典は [KAPI_SPEC.md](../KAPI_SPEC.md) (v63 からデータ欄は 0x4B8 に固定、
関数表の容量 R = 300、票 TASK_KAPI_DATA_FIELDS (アーカイブ: `docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md`))。ここに置く規則は 1 つ:
**残りが 16 本を切ったら次の R を決める票を起こす** — R を変えるとデータ欄が動き、全バイナリの作り直し (v63 と同じ移行) になる。
トランポリン 1 ページの上限は R ≤ 318。配備順は
[08_build.md §8-4](../08_build.md#kapi-v63-移行) を参照する。
