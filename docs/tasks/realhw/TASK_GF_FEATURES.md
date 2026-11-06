# TASK_GF_FEATURES — GeForce 6200 (NV44A) の機能を手本 nouveau と同じ範囲までそろえる (GUI へのつなぎ込みを除く)

> 上位: [実機の票の索引](PLAN.md)

> 状態: **実装中 (2026-11-05)** — 層 1〜5 は main (層 3 c3e018a6、層 4 5a29f69c、層 5 23a19141)。実機 Ra266 の受入は入口の不具合を 1 つずつ直している (立会いなし、カメラ): 時計・待ちの未結線 (gf-prodtime)・PMC の期待値・黙った pc98 への落ち・live AS の PT (gf-bootpt)・devinit 前の PFB 読み、の後、67f1b3ca で `gf1 status`・`gf2 status` は rc=0、hello_gfx は `[gf4] start fail stage=objects_init rc=-205`。`[gf3] wait timeout site=push_reserve … now-dl≒2 秒、GET=PUT` から、devinit の前に作った 2 秒の期限を objects_init まで使い回していたと判明し、devinit より後の段は段の入口で期限を取り直す直しを ee6bfbbd で main (astra レビュー指摘 0)。実機での再試験 (hello_gfx) がノートで進行中。取り込みを実機合格と数えず、表示が出たら層 1 から受入。設計と段の状態は §4、機能の比較は [GF_FEATURE_COMPARE](../../decisions/realhw/GF_FEATURE_COMPARE.md)。

## 0. ユーザーの決定 (2026-11-02、原文)

- 「GUIなどに繋ぐ部分は後回しでもいいのですが、PCIのBus Masterの件は何で決まっていますか？ たぶん、内蔵IDEの件で誤解していると思いますが、追加のPCIデバイスにはこれは当たらなかったはず。」
- 「実際の動作確認は実装後のテストでもいいし実機でテストで動作チェックすればいいですが、基本的にすべての機能が同じレベル（GUI以外）には用意されるべきです。」
- 範囲の選択: 「１から５で。それ以降は残タスクしてドキュメント化」 (1〜5 は §1 の表。3D・Xv・MPEG は §3 の残タスク)。
- 「足りる」の基準を PM が決めない (票 TASK_GF_ACCEL の状態行の訂正、2026-11-02)。
- 熱のしきい値ごとの処理 (2026-11-06、手本のガードの洗い出しの A11・A12)。最初の回答は「クロックを順次下げる」。その後、PM が VBIOS の熱の表の 4 つのしきい値 (低い順に fan_boost・downclock・critical・shutdown) を示した。このうち downclock と critical は、手本 (`therm/temp.c:104-130`) も OS32 (`hwdev/gf/therm.c:643-646`) も口 (emergency.downclock / pause) だけで中身がつながっていない。これを受けて「段階ごとに分ける」を選んだ:
  - fan_boost: ファン 100% (今のまま、手本どおり)。
  - downclock: クロックを順次下げ、下がれば段階的に戻す。
  - critical: 描画のエンジンを一時停止する (表示は保つ)。
  - shutdown: 電源断 (手本の orderly_poweroff) の代わりに、GeForce を止めて 98 の画面に戻し、警告を出す。
  - shutdown の追加の決定 (原文): 「緊急避難的なシャットダウンと言う事であればPC98の場合はネイティブに戻ってください。そうでない描画機能がない場合はなるべく安全にシャットダウンするように。アプリのデータ消失よりハードウェア保護を優先して」。動いているアプリの終了を待たずに GeForce を止める。アプリのデータが失われても止める。
  - 刻み・下限・戻す条件、描画と独立した周期の監視 (A11) は [GF_GUARD_THERM_DESIGN](../../decisions/realhw/GF_GUARD_THERM_DESIGN.md) で決めた (2026-11-06 確定)。
- 手本のガードの洗い出し (2026-11-06) の追記の確定設計: [GF_GUARD_LIFETIME_DESIGN](../../decisions/realhw/GF_GUARD_LIFETIME_DESIGN.md) (監査の台帳の複数の窓・保存像・tile)、[GF_GUARD_THERM_DESIGN](../../decisions/realhw/GF_GUARD_THERM_DESIGN.md) (層 5 の時計・熱・perf)。

## 1. 範囲 (1〜5)

| 層 | 中身 | 手本 (nouveau の経路、詳細は比較の表) |
|---|---|---|
| 1 土台 | PCI の Bus Master の有効化 (§2)・表示面と別の VRAM の置き場 (PRAMIN)・割り込み (PMC・PFIFO・PGRAPH・VBLANK) | `nvkm/subdev/instmem/nv40.c`・`nvkm/engine/fifo/nv04.c` の intr・`dispnv04/crtc.c` の vblank |
| 2 コマンドの投入 | PFIFO のチャネル・DMA のプッシュバッファ・RAMHT/RAMFC/RAMRO・PGRAPH の初期化と文脈・完了待ち (fence)・時間切れと障害の停止 | `nvkm/engine/fifo/nv40.c`・`nvkm/engine/gr/nv40.c`・`ctxnv40.c`・`nouveau_chan.c`・`nv17_fence.c` |
| 3 2D | SURFACE_2D・GDI (塗り・文字の展開)・IMAGE_BLIT (コピー)・ROP/パターン/クリップ/beta・IFC・SIFM・M2MF | `nv04_fbcon.c` (v6.1)・xf86-video-nouveau の `nv_accel_common.c`・`nv04_exa.c` |
| 4 表示 | 複数のモードと色深度・EDID/DDC・DPMS・ハードウェアカーソル・VBLANK の待ちとページフリップ・ガンマ | `dispnv04/crtc.c`・`cursor.c`・`nouveau_connector.c` |
| 5 その他 | クロックの切り替え・温度と電圧・タイリング・サスペンドと復帰 | `nvkm/subdev/clk/nv40.c`・`fb/nv44.c`・`nouveau_drm.c` の suspend |

- 手本の写し方は [HW3]・記憶「GeForce の設計は nouveau を手本に」に従い、書くレジスタごとに nouveau の経路・順序・前提を file:line で示す。
- 動作の確認は実装後の試験と実機 (ユーザー決定)。NP21/W に GeForce は無いので、ホストの試験 (偽の MMIO) と Ra266 で確かめる。
- GUI からは呼ばない (§3)。カーネルの中の試験の入口と HAL の hook (`fill_rect`・`blit` など) までを作る。

## 2. Bus Master を切っていた理由と撤回

- 電源投入時の Command は `0106h` (BIOS が Bus Master を立てる、実測 2026-10-15 の ra266_snapshot:228–232)。
- `0102h` に固定したのは 4a の設計の Opus レビュー R4 (`~/os32-tmp/run/gf6200_4a/REVIEW.r1.md`): 「PMC_ENABLE を立てた engine が DMA を始めると RAM に書く恐れ (推測)。→ 4a の間は Command=0102h、BM は 4b の設計で」。仕様や実機の不具合が根拠ではなく、診断の段の暫定の決まりが見直されずに本番ドライバ (`docs/design/hwdev/gf/devinit.md` の N1)・復元 (`kdebug/recover.h:160`)・加速の設計 (GF_ACCEL_DESIGN §1) に残った。
- 層 1 で撤回する。直す所は devinit・recover の override・設計書・試験の期待値。いつ立てるか (チャネルを作る前・PGRAPH を止めてから落とす順) は層 1 の設計で決める。

## 3. 残タスク (この票では作らない、ユーザー決定 2026-11-02)

| 項目 | 中身 | 手本 |
|---|---|---|
| 3D (Curie) | NV44 の 3D の class 0x4497・TCL・合成 | `nvkm/engine/gr/nv44.c:99`・xf86-video-nouveau の `nv40_exa.c` |
| Xv | テクスチャによる動画の表示 (NV44A は KMS の overlay の対象外) | `nv40_xv_tex.c`・`nouveau_xv.c` |
| MPEG | NV44 の MPEG エンジン | `nvkm/engine/mpeg/nv44.c` |
| GUI へのつなぎ込み | gshell・libos32gui から 2D の加速と VBLANK・フリップを使う。今の GUI は RAM の裏画面が正で、VRAM の中だけの塗り・コピーは次の present で上書きされる — 描き方の設計から要る | 比較の表の「GUI 経路の検索確認」 |

## 4. 段の状態

| 段 | 中身 | 状態 |
|---|---|---|
| 1 | 土台 | main に取り込み済み。仕様・受入は [GF_FEAT1_DESIGN](../../decisions/realhw/GF_FEAT1_DESIGN.md)。実機受入は wt/gf-prodtime の後 (初回は 2026-11-04 に `gf1 status` rc=-107 で止まった) |
| 2 | コマンドの投入 | main に取り込み済み。仕様・受入は [GF_FEAT2_DESIGN](../../decisions/realhw/GF_FEAT2_DESIGN.md)。実機受入は wt/gf-prodtime の後 (初回は 2026-11-04 に `gf2 status` rc=-201 で止まった) |
| 3 | 2D と M2MF | main に取り込み済み (c3e018a6)。層 2 に依存。実機受入は層 1・2 の後。仕様・受入は [GF_FEAT3_DESIGN](../../decisions/realhw/GF_FEAT3_DESIGN.md) |
| 4 | 表示 | main に取り込み済み (5a29f69c)。層 3 に依存。実機受入は [持越し台帳](../DEFERRED_TESTS.md) の GF-L4-Ra266-*。仕様・受入は [GF_FEAT4_DESIGN](../../decisions/realhw/GF_FEAT4_DESIGN.md) |
| 5 | その他 | 機能 PM の試行で wt/gf-l5 に進行中 (ユーザー「試してみて」)。層 4 に依存。仕様・受入は [GF_FEAT5_DESIGN](../../decisions/realhw/GF_FEAT5_DESIGN.md) |

### 実機の記録 (2026-11-04)

- 本番のドライバが GeForce の画面への切り替えを実機で行ったことがあるか: ユーザー「いや、切り替えは行ったことがあります。ドキュメントに残しそびれているだけと思います。」(日付は不明、ユーザー談)。
- main 5a29f69c での実機の受入は入口で止まった。`gf2 status` は precheck rc=-201 (本番の foundation の時計・待ちが未結線、`hwdev/gf/devinit.c:104-105`)、`gf1 status` は rc=-107 (起動前から Command 0106h = BusMaster を lspci で確認。OS32 は本番の start を一度もしていない)、`gf2 display 0` はシリアル・画面とも無出力。証拠はノートの `~/os32-tmp/evidence/2026-11-04/gf-accept-5a29f69c/`。
- 直しは wt/gf-prodtime (sol) が進行中。取り込みの後に層 1 から実機の受入をやり直す (立会いなし、カメラ)。
