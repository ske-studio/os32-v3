# tools/osri_enc2_* — 行ごとに 16 色を選び直すホストのエンコーダー (enc2)

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `tools/osri_enc2_spec.py`・`osri_enc2_master.py`・`osri_enc2_tree.py`・`osri_enc2_row.py`・`osri_enc2_plan.py`・`osri_enc2_order.py`・`osri_enc2_timing.py`・`osri_enc2_dither.py`・`osri_enc2_pack.py`・`osri_enc2_decode.py`・`osri_enc2_metrics.py`・`osri_enc2_compare.py`・`osri_enc2_native.py`・`osri_enc2_core.c` (すべて `tools/` の下)

## 何をするか

640×400 の絵から、各行の 16 色のパレットと、行の境界で書き換える順を決め、`.osri` v2 を作る。
各行の境界の OUT は B 本以下 ([sys/kern/gfx/osri/osri_profile.h](../gfx/osri_profile.h.md) の数え方)。全画面で固定する枠は設けない。
ホストだけで動き、ゲストは結果のファイルだけを読む。カーネルへは組み込まない。

## 役割の分担

- Python: PNG の読み込みと縮小 (`img2osri.resize_image` と同じ)、初期値の用意、評価、v2 への直列化 (`osri_enc2_pack`)、独立の復号 (`osri_enc2_decode`)、指標と比較 (`metrics`・`compare`)。
- C (`osri_enc2_core.c`、C11 + POSIX threads): 探索の本体。`make osri-enc2-core` で `build/out/osri-enc2-core` を作る (通常の make は作らない)。`osri_enc2_native` がファイルで受け渡す。
- 並列化は候補の評価だけで、候補の番号順に集める。threads の数によらず、パレット・順序・索引・CRC まで全バイトが一致することが契約。

## 整数の規則 (Python と C で同じ)

- 色 q=0xRGB を E(q)=17×(R,G,B) に展開し、距離は RGB の等重みの二乗和。距離は int32、和と比較は int64。浮動小数点は PSNR の表示だけ。
- 目的: F(y,P) = Σ a[k]·D(y+k, P) (k=0..3、a=(8,4,2,1)、次の 3 行にも同じ P を当てる代理の先読み) と、当行は実表示の誤差 Δ で置き換えた J。
- 同点は現状を保つ。候補は RGB444 の昇順、距離の同点は小さい枠、木は小さいノード ID。乱数は使わない。最適は保証しない。

## 段

1. **マスター**: 全画面を 256 色に (頻度 × 最短距離で中心を選び、RGB444 の格子へ射影する Lloyd を最大 20 回)。誤差拡散はしない。
2. **木**: 256 色を近い対から組にして 128→64→32→16 の 16 本の二分木にする。各行の候補は、根から F が小さい側へ降りた葉の色 (最大 16)。
3. **行の探索**: 行 0 は 16 根の代表から全枠の Lloyd。行 1〜399 は前の行を出発点に、1 枠の部分成分の入れ替え (成分マスク 1〜7) を、両隣の行の OUT ≤ B を満たす中で J が厳密に下がるものだけ採る (最大 32 手)。その後、変えた枠だけを座標ごとの Lloyd で詰める (最大 6 周)。マスターの外の RGB444 も許す。往復 (399→0、0→399) を最大 2 回。
4. **書き換えの順**: 変える枠の順 π を、当行でその番号を最初に使う x の順から始め、入れ替えで表示の誤差が減るものを最大 2 回探す。π はファイルの同じ行のイベントの順として保存する。
5. **誤差拡散**: π を固定し、時刻ごとの表示色に対して整数の Floyd–Steinberg を 1 回だけかける (蛇行なし)。拡散の後で π を変えない。

## 時刻のモデル (はみ出しの見た目)

- 定数は `osri_enc2_spec.py` の HBLANK_NS 5900・OUT_NS 650・LINE_NS 31800・ACTIVE_NS 25900・IN_NS 1150・START_DELAY_NS 575 (IN の半分)・PIXEL0_OFFSET_NS 0。Ra266 31k の観測からの概算で、推測を含む。
- 先頭の番号を除く j 本目の OUT の完了時刻 t_j = START_DELAY + j × OUT_NS。HBLANK を越えた分は、その成分の旧い値が画素 x_j まで残るとみなす (整数の交差積で比較)。
- 1 行に収まるかの判定は実機の観測の上限 (実 OUT ≤ 47) による。t_last による拒否はしない。
- START_DELAY_NS と PIXEL0_OFFSET_NS はカメラの左端の筋の幅で校正する値で、まだ校正していない [未]。

## 出力

- `--outs B` ごとに 1 本。実際の変更だけを行の昇順・行の中は π の順に書く。同値で埋めない。B を使い切る必要はない。
- 比較器 (`osri_enc2_compare.py`) は B=15/23/31/47 と旧方式の貪欲な探索を並べた表 (CSV/MD) と時刻モデルの PNG を 1 枚出す。常用の CLI からは呼ばない。
- I プレーンの明暗の対 (k と k+8 の成分の大小を縛る案) は既定 OFF で、採用していない。

## 落とし穴

- 計算量が大きい。全画像・全 B は C で回す。Python の全画像の実行は小さい fixture・一部の行の照合だけにする。
- 47 は比較の上限で、ゲストは 31 を超える行を拒否する。

## 検査

- `test_osri_enc2.py (旧版 490c424e37bd)` (osri-enc2): 小さい画像の総当たりで距離・重心・制約を照合、両隣の OUT ≤ B、往復で J が増えない、同じ入力でバイト一致、時刻モデルの境界 (48 本の拒否と 47 本の許可)、独立の復号で 400 行のパレットと索引を照合。数え方の誤り・先頭番号の二重計上・順序の無視・負の誤差の床の割り算などは実行時 RED。
- `test_osri_enc2_core.py (旧版 3856668eeb12)` (osri-enc2-core): C と Python のビット一致 (計画・時刻・符号付きの誤差拡散)。
