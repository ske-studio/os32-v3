# T2e e3 — DISPLAY 4面束と native alias

票: [TASK_T2D_T2H](../../docs/tasks/v3/TASK_T2D_T2H.md) §2-5 e3。

`test_surface_bundle.py` は `test_surface_lease.py` の ILP32 ビルド足場を共有し、
実 lease/query/B1/access_walk/paging/pgalloc を走らせる。`host32.py` 経由で
正常対照は明示された全 runner、変異は先頭 runner。sandbox は qemu のみ。
exec は起動時の VRAM USER 化の実文を切り出し、V86 は setup/teardown の全文を使う。
V86 の BIOS/I/O は stub、CPU による低位仮想 RAM のクリアだけ backing へ向ける。
実 exec ローダ全体、実 V86 CPU、実 backend probe の検証ではない。

## 観測

- boot → 実 exec の map 文 → V86 setup → teardown → 再 exec の各時点で
  低位1MBの全 PCD/PWT を照合。TVRAM/BRG/I は UC、CG/ROM/RAM は WB。
- CUI GFX と GUI 全画面所有者は4面 RO/RW可、通常GUI/GFX未宣言は拒否。
  PC98 source/4refを保ったまま、模擬選択backendをPEGC/Cirrusへ変更すると
  INVAL・out不変。PC98選択へ戻すと成功 (実probeは呼ばない)。
  本数・順序・欠落・重複・別面・gen0・旧世代、後ろINVAL優先、IRQ/例外も拒否。
- 既存 filler の後ろに置いた4面目だけが追加PTを必要とする配置で NOSPC。
  master・別AS・既存lease・slot全バイト・台帳・ページ会計・出力・IF/CR3不変。
- 最終出力ページROでcopyout拒否 → 今回4tokenのみ巻戻し、採番復元。
  空slotに異なる残存バイトを置き、slot復元の変異が見逃されないことを確認。
- 4本のrelease全失敗は診断+4、途中1本失敗は+1、成功した分だけ回収。
  失敗したtokenを残し、全成功でない限りslot/採番は復元しない。
- 既存1本+4面で残り3slotとなる状態から再取得FULL、token別releaseとrevoke。

## RED → GREEN (2026-10-03)

旧native WBに対するTVRAM成功期待は実行時FAIL。gfx足場では追加FIXED記録のため
識別時region数3→4も追従が必要だった。束の初回ビルドではmap関数名とkmemset足場を
修正し、既存AS生成後のboot限定登録は既存lease_host同様の台帳fixtureへ修正。
コンパイル失敗はREDに数えない。

新束18変異はすべて正常対照成功後の rc=1/FAIL に限定して **18/18 runtime RED**。
最初はslot復元変異がSURVIVED (以前のslot内容が同じ) → 上記残存バイト注入で閉じた。
世代変異はquery/lease/pagingの3防壁を同時に外し、各置換は1箇所固定。
その他も置換数1固定、コンパイルエラー・signal・timeoutはREDに数えない。

既存 lease **10/10**、単面 **17/17** は意図を維持。gfxは既存14にstride丸め・
DISPLAY UC落ち・最後の登録欠落の3本を追加し **17/17 runtime RED**。
gfx正常対照は17構成+1静的検査。既存queryも34/34 runtime REDを個別確認済み。

初回全体検査はrc=2。con_sink足場の旧TVRAM_BPR undefを除去し既存期待のまま成功。
gfxのbb-whole-poolは旧判定ではsignalもREDに数えていたが、SIGSEGVは算入対象外。
実allocatorの返す範囲をクリア前にassertし、修正後は17/17すべてrc=1/FAILで確認。
この補強は高位RAMを誤確保したという元の検出意図を保つ。

## 独立レビュー対応 (2026-10-03)

内部入口を `surface_lease_bundle` に改名。選択中backend照合・出力事前検査・
IRQ/例外の入口拒否を外す3変異を追加し、束は **21/21 runtime RED**。
出力事前検査拒否はcopyout未到達も照合し、後段の巻戻しでは代用できない。
単面CLIENT/DISPLAYにもbackend照合を追加し **18/18 runtime RED** (既存17の意図を維持)。
世代のquery+lease+paging変異はそのまま。同一unitの重複差替えと、同じfixture TUを
再構築するpaging/V86/execの複数同時差替えはrun()のassertで拒否する。
旧17構成+静的1検査とgfx17変異も維持。結果の正典は票§2-5 e3。
