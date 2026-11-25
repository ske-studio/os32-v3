# tools/raster_splash_asset.py — 配布するスプラッシュの画像を作って配る

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `tools/raster_splash_asset.py`・`build/core.yaml` (配備の項目)・`.gitignore`

## 何をするか

`/boot/splash.osri` に置く画像を手元で 1 本作り、受け入れた基準とバイト単位で照合してから置き換える。
通常の make からは呼ばない (明示のホストの操作)。

## 使い方と契約

- `python3 tools/raster_splash_asset.py INPUT -o assets/splash/splash.osri --reference <基準の .osri> [--threads N] [--replace]`。
- 生成器の入力 (`tools/img2osri.py`・`sys/kern/gfx/osri/osri_profile.h`・`tools/osri_*`) がコミット済みと一致しなければ止める。
- 一時ディレクトリで `img2osri.py --encoder enc2 --outs <B> --band 1 --threads N` を呼び、独立の復号で全行の OUT ≤ B を確かめ、基準と全バイトを比べる。違えば止める (期待値を黙って更新しない、違いの由来を調べる)。
- 既にある出力の置き換えは `--replace` を付けたときだけ。原画は変えない (生成中に変わったら止める)。
- 同じ場所に `splash.manifest.json` を書く: 原画の SHA256・生成器のコミットと各入力の SHA256・引数・threads・Python/Pillow/NumPy の版・出力の SHA256/CRC/バイト数/件数/最大 OUT/総 OUT。OS は sidecar を読まない。

## 配布

- 画像と manifest は公開のリポジトリに入れない (ユーザー決定 2026-11-04「リポジトリに入れない」、`.gitignore` の `/assets/splash/splash.osri` と `splash.manifest.json`)。
- `build/core.yaml` の glob の項目 (tag `data`) が、手元に `assets/splash/splash.osri` があるときだけ `/boot/splash.osri` を配備の集合 (HDD の配備・hsync) に入れ、更新のたびに上書きする。無ければ配らず、起動はロゴになる。
- `/boot/user.osri` は配備に入れない。起動はこちらを先に使う (`docs/design/platform/pc98/splash_image.c.md`)。
- 起動 FD・MINIMAL には入れない (FD の空き 64KiB 以上を保つ、8.3 に入らない名前)。

## 既定の原画と期待の出力 (受入のとき)

- 原画: `sfw_01321_rgb.png`、SHA256 `a5399f3e19b4b03c80b37e43afa93e634e85893185ba88c174d7e5b5b04d196d` (手元の作業ディレクトリにあり、リポジトリには無い)。
- 出力 (B31): SHA256 `afc3ceb70e9b7dc35115f9cc27b56b41ac05ae8c9a8fabdc84aa4f39dd760bab`、CRC `16769bea`、131,888B、474 件、実 OUT 最大 25・総 1,181。B47 で作っても同じバイト列。

## 差し替えと戻し

- 差し替え: 生成と検査 → 対応するカーネルを先に置く → 画像を置く → NP21/W と実機で確かめる → 前の画像を退避して差し替え → 読み戻した SHA を照合。
- 戻し: 退避した `.osri` を戻す。古いカーネルへ戻すときは画像も古いカーネルが受ける範囲へ戻す (受けない画像はロゴになる)。

## 検査

`tools/tests/test_raster_splash_asset.py` (raster-splash-asset-host): 手元の資産の生成、画像があるとき・無いときの HDD/hsync/パッケージの集合、FD への混入なし、上書きの禁止。
