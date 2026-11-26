# sys/platform/pc98/splash_image.c — 起動の画像の持ち主

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `sys/platform/pc98/splash_image.c`

## 何をするか

`platform_splash_try_image(path, &end_tick)` が、起動の画像を読み、検査し、表示し、片付ける。
画素は begin で借りた BB に直接読む。画像記述 (51,110B) と動的な再生の列 (25,608B) だけを kmalloc で確保し、どの出口でも解放する (計 76,718B、`KHEAP_SIZE` は 192KiB)。

## 画像の選び方と配布

- パスは `sys/include/config.h` の `SYS_SPLASH_IMAGE` (`/boot/splash.osri`)。`/boot/user.osri` (`SYS_SPLASH_USER_IMAGE`) があればそちらを先に使う (ユーザー決定 2026-11-04)。
- splash.osri は手元で作った画像を配備の集合で配る ([tools/raster_splash_asset](../../tools/raster_splash_asset.md))。user.osri は配備に入れないので、ユーザーが置いた画像は更新で消えない。
- どちらも無ければ FALLBACK で、内蔵のロゴになる。

## 手順と結果

| 段 | 失敗したとき |
|---|---|
| BB の記述を取る | FALLBACK (framebuffer) |
| ファイルの有無 | FALLBACK (missing) |
| 画像記述の確保 | FALLBACK (memory) |
| `osri_load` | FALLBACK (invalid) |
| 読み込みで枠を使い切った | DONE (何も見せずに終える) |
| 入口が IF=0 | NO_SYNC |
| 動的な画像: `pl_run_timing`・列の確保・`pl_v2_prepare`・前置きの 2 秒が残るか | NO_SYNC (timing) か FALLBACK (memory・prepare・preview time) |
| 全色を黒→全面の dirty→`gfx_present_nosync` (画像の転送はこの 1 回) | — |
| 動的: `pl_preview` → `pl_run_more` の間 `pl_v2_frame` | 前置きの失敗・フレームの NO_SYNC は NO_SYNC。1 フレームも OK が無ければ NO_SYNC (全 DROP を成功にしない) |
| 固定 (イベント 0 件): `pc98_splash_raster_frame` の後、`end_tick` まで `platform_splash_wait_frame` | frame の失敗は NO_SYNC |

- ここまでのどれも転送前の失敗なら、BB は呼び手 (`boot_splash`) が内蔵のロゴで全面描き直す。転送の後の失敗は黒に戻して返す。
- DONE 以外は `[splash] <理由>: builtin logo` を 1 行だけ出す。

## 時間の予算

- 枠は「入口の期限までの残り」と `PLATFORM_SPLASH_IMAGE_TICKS` (8 秒) の小さい方。読み込み・準備・転送・前置きの実際の経過を引いた残りを再生に使い、再生は 6 秒までにする。
- 動的な区間は [splash_raster_run.c](../../drivers/video/pc98/splash_raster_run.c.md) のモデルの時間と実 tick の大きい方で数える。出口では、使った時間を引いた残りを tick で切り下げて `*end_tick` に返す (期限を起算し直さない、二重に引かない、PIT を補正しない)。
- 固定画像は IF を開けたまま、入口の期限 (`*end_tick`) まで時計の歩調で待つ。
- 準備 (`pl_v2_prepare`) の実時間は実機で測っていない [未]。

## 落とし穴

- 可視区間の IF=0 で PIT の tick が遅れるので、壁時計の表示時間は 8 秒より長くなりうる (動的 3 秒で約 0.9 秒、24k で約 1.3 秒の遅れとレビューで見積もった [推]、当時の再生は 3 秒)。補正しない (ユーザー決定 2026-11-04)。
- 周期の判定が合わない (dynamic_ok=0) 構成でも、固定画像は表示できる。動的な画像は NO_SYNC でロゴになる。

## 検査

- `test_splash_v2.py (旧版 2443845d59b2)` (splash-v2-host): 実際の準備器で、非 0 件の再生・固定画像・32 本超の拒否 (転送 0・全解放・ロゴ)・転送後の失敗の片付け・31k/24k・残り 0・前置きの不足・全 DROP・tick の停止と wrap。件数の拒否の再導入・解放の欠落・前置きの削除・期限の再起算は実行時 RED。
- `test_splash_wire.py (旧版 1a5727f53cbf)` (splash-wire-host): 固定・v1・v2 の実ファイルを、実ローダー・実再生器・期限・片付けまで通す。
