# platform/pc98/boot_splash.c — 起動のスプラッシュの全体の流れと内蔵のロゴ

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `platform/pc98/boot_splash.c`・`platform/pc98/boot_splash.h`

## 何をするか

`boot_splash()` は起動の前景で 1 回だけ呼ばれ、画像があれば画像を、無ければ内蔵の「OS32」ロゴと虹色のスクロールを見せ、最後にテキストの画面へ戻す。
呼ぶのは `platform/pc98/kernel.c` で、root の mount と割り込みの有効化の後 (通常部品の登録 → splash → rshell/SerialFS → shell の順)。Host Services や遅れて mount するものは待たない。

## 流れ

1. `platform_splash_begin()`。失敗なら `tvram_clear()` だけして戻る (CUI へ)。
2. 診断の像 (`SPLASH_RASTER_DIAG`) は `platform_splash_probe()` の後、片付けへ。
3. `platform_splash_try_image(SYS_SPLASH_IMAGE, &end_tick)`。end_tick の初期値は入口 + 8 秒。DONE なら片付けへ。
4. ロゴの期限は入口 + 3 秒 (`PLATFORM_SPLASH_TICKS`)。画像の経路が返した end_tick とその小さい方を使う。3 秒を起算し直さない。
5. try_image が NO_SYNC か、`dynamic_ok=0` なら静止のロゴ (`gfx_present_nosync` の後、`platform_splash_wait_frame` で時計の歩調)。
6. そうでなければ、毎フレーム表を作り直して `gfx_present_raster` を呼ぶ。途中で last_result が NO_SYNC になったら静止のロゴへ移る。
7. 片付けは成功・失敗とも 1 か所: 両ページの消去 → `gfx_shutdown()` (走査の判定の状態も失効) → `palette_init()` → `tvram_clear()`。画像用の状態を次の GUI へ残さない。

観測の像 (`SPLASH_RASTER_OBS`) では、本体を `platform_splash_observe()` と同じ片付けに置き換える。ロゴの表・関数は `#ifndef SPLASH_RASTER_OBS` の中にあり、観測の像には入らない。

## 内蔵のロゴのデータ

- パレット: 0 背景 (黒)・1 虹色 (表で毎行書き換える)・2 縁・7 面・8 影・15 光。全面を番号 1 で塗り、矩形のグリフで O・S・3・2 を立体に描く。
- 表: 100 段の HSV の虹 (`make_grad_table`) を、行 0,4,…,396 (`SPLASH_GRAD_LINE_STEP` = 4) に番号 1 の 1 件ずつ置く。フレームごとに offset を 1 進めてスクロールする。行 y の色は表の項目 ⌊y/4⌋ で、帯は下端まで届く。
- 公開 KAPI と同じ表 (最大 200 件、異なる行の間隔 2 以上) なので、再生器の拒否の規則に触れない。

## 時間の予算

- 内蔵のロゴの速さは「3 秒間の commits の数」で比べる。120µs の相対待ちをやめたとき、NP21/W で 171 → 167 (−2.3%、±10% の内) だった。
- 静止の fallback は VSYNC で歩調を取らないので、ループは `platform_splash_wait_frame` で 1 フレームぶん待つ (空回りさせない)。

## 検査

- `tools/tests/test_boot_splash_native.py` (boot-splash-host): 実際の gfx の核・VRAM・再生器で、ロゴの初回と継続・flip の有無・静止の fallback・片付けの順。
- `tools/tests/test_dep_dir_d_splash.py`: 描いたプレーンのバイト列と起動の寿命の対照。
- `tools/tests/test_splash_wire.py`: 画像の有無・壊れた画像・期限で、画像の経路とロゴの分岐を通す。
