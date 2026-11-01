# TASK_GF_FEATURES — GeForce 6200 (NV44A) の機能を手本 nouveau と同じ範囲までそろえる (GUI へのつなぎ込みを除く)

> 上位: [実機の票の索引](PLAN.md)

> 状態: **設計中 (2026-11-02)** — ユーザー決定で範囲を 1〜5 に確定。**層 1 の設計は確定** ([GF_FEAT1_DESIGN](../../decisions/realhw/GF_FEAT1_DESIGN.md)、astra r1〜r2、Opus 2 回)、実装はこれから。**層 2 の設計も確定** ([GF_FEAT2_DESIGN](../../decisions/realhw/GF_FEAT2_DESIGN.md)、astra r1〜r2、Opus 2 回)。**層 3 の設計も確定** ([GF_FEAT3_DESIGN](../../decisions/realhw/GF_FEAT3_DESIGN.md))。層 4・5 は設計中。機能の比較は [GF_FEATURE_COMPARE](../../decisions/realhw/GF_FEATURE_COMPARE.md)。

## 0. ユーザーの決定 (2026-11-02、原文)

- 「GUIなどに繋ぐ部分は後回しでもいいのですが、PCIのBus Masterの件は何で決まっていますか？ たぶん、内蔵IDEの件で誤解していると思いますが、追加のPCIデバイスにはこれは当たらなかったはず。」
- 「実際の動作確認は実装後のテストでもいいし実機でテストで動作チェックすればいいですが、基本的にすべての機能が同じレベル（GUI以外）には用意されるべきです。」
- 範囲の選択: 「１から５で。それ以降は残タスクしてドキュメント化」 (1〜5 は §1 の表。3D・Xv・MPEG は §3 の残タスク)。
- 「足りる」の基準を PM が決めない (票 TASK_GF_ACCEL の状態行の訂正、2026-11-02)。

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
| 1 | 土台 (Bus Master・PRAMIN・割り込み) | 設計確定 — [GF_FEAT1_DESIGN](../../decisions/realhw/GF_FEAT1_DESIGN.md) §7 の段 (A0 → B0〜B6 並列 → C0〜C10)。要点: Bus Master は開始ごとに最初に立て、停止は engine の reset パルス → IRQ の登録解除 → BM 解除 (nouveau の remove/suspend の順)。latency は 0Ch に write16。instmem は VRAM 末尾、BAR2 を直に UC で。割り込みは IRQ6 の共有登録、未知の要因は PMC を閉じて IRQ_DEFERRED、tick の 7 枠目で拾い直す。import は約 20 → 28〜30 (上限 32) |
| 2 | コマンドの投入 (PFIFO・push・dmaobj/RAMHT・PGRAPH・fence) | 設計確定 — [GF_FEAT2_DESIGN](../../decisions/realhw/GF_FEAT2_DESIGN.md) §8 の段 (A0〜A3 → B0〜B8 並列 → C)。層 1 の A0 の取り込み後に着手。要点: チャネル 0、push は VRAM、PCI の staging、nv17 型の reference で完了待ち、PCI dmaobj の前に NV44 FB の 100850/100800 を手本どおり、割り込みの trap は前景で採取 (層 1 は変えない)。import は 29〜31 (上限 32) |
| 3 | 2D と M2MF (SURFACE_2D・GDI・IMAGE_BLIT・ROP・IFC・SIFM・M2MF、HAL の fill_rect・blit の hook と試験の入口) | 設計確定 — [GF_FEAT3_DESIGN](../../decisions/realhw/GF_FEAT3_DESIGN.md)。要点: ROP は手本の表 (nv_rop.h)、1bpp は CGA6 (fbcon)、M2MF には notifier (instmem 32B・class 0x3d)、hook は Cirrus の G5 の約束 (切り方と rc) に合わせ gf_hook.c に、GEFORCE_MODULE_OPS_VERSION 2→3。層 2 の実装の後 |
| 4〜5 | §1 の表 | 設計中 (astra 起草 r1、Opus レビュー中) |
