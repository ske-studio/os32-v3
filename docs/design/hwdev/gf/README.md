# hwdev/gf/ — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/`

根拠の `O/` は `/home/hight/os32-v3/`（参照基点 `39a3461a`）、`D/` は `O/docs/decisions/realhw/`、`T/` は `O/docs/tasks/realhw/`。
`R/` は `/home/hight/os32-tmp/refs/geforce6200/`、`N/` は `R/nouveau/drivers/gpu/drm/nouveau/`、`I:行` は `N/nvkm/subdev/bios/init.c:行`。
nouveau は c03c1a1 の MIT 部を使う（`R/INDEX.md:9`、`N/dispnv04/hw.c:6`）。ライセンスと著作権表示を移動先にも保持する。
VBE は [VESA VBE Core Functions 3.0（1998-09-16、公開 PDF）](https://www.read.seas.harvard.edu/~kohler/class/04f-aos/ref/hardware/vbe3.pdf) の機能一覧だけを参照する。以下の VBE 頁は本文の頁番号。
NV44A のメーカー製プログラミングマニュアルは今回の参照ディレクトリでは確認できない。既に採用済みの nouveau・確定設計・実測を根拠とし、新しい未知レジスタ操作を補わない（`O/docs/CONSTRAINTS.md:94–101`）。

## 1. 目的と範囲

**決定**：下層＝初期化命令と表示操作の両方を 1 機能 1 ファイル、中層＝VBE に近い少数のサービス、上層＝OS32 HAL の三層にする。
OS32 は単一利用者・単一タスク・協調切替。頑丈さの対象はハードの誤動作とバグであり、有限待ち・アクセス範囲・後始末・98 への復帰を守る。権限分離や攻撃者向けの検査は追加しない。
**初版の決定範囲**は Ra266 の 0:13.0、10de:0221、subsys 10fc:1270 の NV44A、既知 ROM の 27 種・193 命令、head A・ANALOG OR0・RGB-IN。
OS32 固有差分も保持：PMC_ENABLE bit0 を立てる、RESET 後 1850 は 0、CR3C の組を確認する。根拠は `O/kdebug/gfinit_op/pll2_zm_reg_decode.inc:3`、`reset_expand.inc:10`、`docs/design/kdebug/recover_gf.md`。nouveau 全機種対応への拡張はしない。
> [要確認] CR3C の組の照合は、現行実装では COPY の直後に読み返していない。
HAL に GF のレジスタ・ROM・recover token を出さない。

これで再起動時は GF devinit に入らず 98 CUI へ戻れる。起動時上書きキー等の新しい脱出機構や auto 優先候補化は足さない（`O/init/kernel.c:343–366,577–582`）。
順番は **route00→ID/定数長/base/資源/写像確認→Command=0x0102 設定・生の 16bit 読戻し確認→ROM 採取・解読→A2 の legacy 無効化/解錠→main0〜4→基準値採取→mode 計算→共有列→必須 CHECK→初回転送/セル→route01**。A1 は本番の全経路から外す。
本番は既知 27 命令専用 runner。解読・整数 PLL・資源検査・A2 のハード手順・局所復元を共有し、recover の plan/node/capture/REC 実行器や kpeek を本番から呼ばない（`O/kdebug/gfinit_exec.c:80`、`gfinit.c:176`）。
配置は **`geforce.mod`＋核の薄い予約/復帰口**（U5）。下層/中層は `hwdev/gf/`、HAL のバックエンドは `drivers/video/geforce/` (共通層は `gfx/`、TASK_SRC_LAYOUT L2 で移った)、診断 adapter は `kdebug/` と従来 hwdev.mod。本番は KDEBUG=0 で動く。
終了/障害/reboot 前は **局所 index/lock/窓復元→1850=0→1854=0→PMC140=0→route00→decode 安全確認→98 復元・予約セル往復・再描画**（`docs/design/kdebug/recover_common/s0.md`）。
S0 は PLL/PFB/旧 timing の完全復元ではなく PARTIAL/S1 未保証を保つ。無応答 MMIO/CPU 停止は既存の物理リセット手順へ戻す。
## 索引

- [hwdev/gf/display/](display/README.md)
- [hwdev/gf/op/](op/README.md)
- [drivers/video/geforce/](../../drivers/video/geforce/README.md)
- [hwdev/gf/internal.h](internal.md)
- [hwdev/gf/runner.c](runner.md)
- [hwdev/gf/save.c](save.md)
- [hwdev/gf/pll.c](pll.md)
- [include/video_service.h](../../include/video_service.md)
- [hwdev/gf/bus](bus.md)
- [hwdev/gf/devinit](devinit.md)
- [hwdev/gf/native](native.md)
- [hwdev/gf/production](production.md)
- [hwdev/gf/quiesce_rows](quiesce_rows.md)
- [hwdev/gf/resources](resources.md)
- [hwdev/gf/rom](rom.md)
- [hwdev/gf/scripts](scripts.md)
- [hwdev/gf/service](service.md)

## 移し替えた設計

- BSD/MIT ソースのみ保存。メーカー資料は閲覧と要約のみ。GPL/Linux は根拠・挙動確認・構造参照に不使用。

- `N/` = `R/nouveau/drivers/gpu/drm/nouveau/nvkm/`、`I:行` = `N/subdev/bios/init.c:行`。参照版は `R/INDEX.md:6–10`。
- nouveau の引用元は MIT 本文のあるファイル。派生実装に著作権・許諾を残す。Haiku は計画のライセンス確認待ちを守り、挙動の照合だけに使う。

手本は `N/dispnv04/crtc.c:708` prepare、`:731` commit、`N/dispnv04/hw.c:827` load_state。新レジスタ列を推測で追加しない。

- [HW3] 根拠: 98 復帰の資料と FreeBSD 対照は `R/kernel/v86_io.c:250`。NVIDIA 公開 MIT 実装 `T/refs/geforce6200/xf86-video-nv/src/nv_driver.c:2290` は選別した保存状態を順序付きで復元する前例で、Ra266 の冷間復帰保証ではない。
- PBUS の意味は `T/refs/geforce6200/envytools/docs/hw/bus/pci.rst:50`、`:60`。ライセンスは `T/refs/geforce6200/INDEX.md:6`、`:7`。専有 driver/ROM x86 コードは解析しない。新しい装置 profile はメーカー資料と BSD/MIT 実装の照合を実装開始条件とする。
