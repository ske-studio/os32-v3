# fs/fat/fatfs/diskio.c — FatFs のディスク I/O

> 状態: **現行 (2026-10-31)**

FatFs のディスク操作を FDD と HDD のドライバへ渡す。
FDD では媒体ジオメトリを取得してアクセスする。

## 落とし穴

- **FDD のジオメトリ**: 2HD 固定のセクタ長や SPT で変換すると 1.44MB 媒体の位置がずれる。
  読込み側もドライバの `fdc_get_geom` を使う (`fs/fat/fatfs/diskio.c:212`, `fs/fat/fatfs/diskio.c:223`)。
  セクタサイズ取得や書込みも同じ媒体情報と揃える。媒体変更時は両形式で確認する。
  関連するホスト試験は `tools/tests/test_fdc_track.py`。

## 内部設計と契約 (D1)

- `fs/fat/fatfs/diskio.c:386–397` のget_fattimeはclock_service.hを読み、ClockDateへ `clock_read_date(&t)`、年は `t.year` を使う。自前pivot補正を除き、DOS_YEAR_BASE=1980の減算・ビット詰め・秒/2・読出し1回を維持する。
