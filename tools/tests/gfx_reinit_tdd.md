# T2e e5 — gfx 再 init / revoke

正典: [TASK_T2D_T2H.md §2](../../docs/tasks/v3/TASK_T2D_T2H.md)。
`test_gfx_reinit.py` / `gfx_reinit_host.c` は実 gfx_core・3 backend・
lease・surface_query・paging・pgalloc を ILP32 で連結する。
appslot の列挙/現在 caller は3 ASの足場、I/O・機種検出・MMU はホスト足場。
実機/ゲストの TLB と表示の合格は PM の受入へ。

正常対照は指定した全 HOST32_RUNNERS、変異は先頭 runner。
`make check-gfx-reinit-host` (`HOST32_RUNNERS=qemu`、`MUT=--mutate`)。
試験のソースは一度読み、変異 TU だけをコンパイル、正常 object を再利用する。
一組の開始/終了で入力 hash を照合し、変異は `mutpar.run_ordered` で回す。

## 正常対照

- A の CR3 / master、IF=1 / IF=0 からの再 init と入口状態の復元。
- B の CLIENT と DISPLAY 4 面、A 自身の CLIENT の PTE が NP。
  revoke 後の token は INVAL、旧 ref の acquire/query は STALE。
- C は PEGC CLIENT だけを保持。PC98 の再 init 前後で master/C の
  全 present PD/PT、C の lease 表をバイト比較し、generation/参照数も不変。
- bind 前の参照0・旧世代、regen 時の master/IF=0 を観測。
  両区間の間の publisher、query、surface_lease は not-ready/INVAL。
  旧refで直接lease_acquireする負例は取得成功後のregen拒否・not-ready・旧token不在を確認。
- 200/400 とも登録は640×400・offset 32000刻み、同一 backing を保持。
  400→200→400で各面を汚し、表示範囲の全バイトが0、200行の後半は不変と照合。
- 破損 AS の revoke 失敗でも A の token は復活せず、CLIENT は not-ready。
  修復後の再 init で残った B の lease を取り除く。
- 同一 AS の最初の壊れた lease を飛ばして後続の lease を解除し、失敗数を数える。
- prepare / shutdown / Cirrus init 失敗→PC98 fallback の世代更新。
- boot→gshell は CLIENT revoke と gen+1、同じ owner への再移譲は無変更。
  IF/CR3復元、revoke失敗・gen枯渇時の移譲拒否、packed両系のcaller AS再initを確認。
- gen 上限で周回せず not-ready、kernel BB は保持。
  regen の参照あり・非master・closing・不正geometry拒否と RAM の内容保持。

## 変異 (11 本、期待 FAIL 文言を照合)

| 変異 | 観測 |
|---|---|
| revoke 前 gen+1 | bind 前の世代 |
| revoke を bind 後へ移動 | bind 前の参照0 |
| gen を進めない | 再 init 後の世代 |
| 全 sid を revoke | C の PTE 像 |
| caller CR3 reload を除去 | backend 操作時の caller CR3/IF |
| 失敗時に旧 token を復活 | A の token slot |
| 束 generation 照合を除去 | 旧 DISPLAY refs の STALE |
| 200 行で geometry を変更 | 登録height=400 |
| 200 行BB消去を半分へ縮小 | 各面の表示範囲全バイト |
| revoke の最初の失敗で打切り | 後続 slot の解除 |
| 区間の間で publisher を公開 | not-ready/INVAL |

2026-10-03 レビュー修正後、qemu 正常対照 PASS、11/11 runtime RED。
compile/link失敗とtimeoutはREDに数えない。
初回の試験は caller frame 未設置で query の期待に失敗し、足場を修正した。
既存 fb は502チェック/10変異、lease は10変異、gfx boot は17変異を維持。
既存の init200-rebind は、regen 後の bind が pointer を復元しても
初期化の早期returnによる起動前fb (height=400) を検出する期待へ更新。
packed-bind は入口とregen後の両方の packed bind を除去して古いaliasを検出する。

レビュー修正の個別検査ではSTATIC_ASSERTの第2引数不足、packed初期化の
ホスト未写像BIOS読み、変異の期待文言不一致、fbの実表示高と登録高の混同を
検出して修正。packedは既存足場同様caller ASから初期化する (BIOS/MMUは非実機)。

全体検査初回はgfx_boot足場の未使用gfx_current_heightが-Werrorでrc=2。
全ジョブ終了後に不要な変数を削除し、17構成+静的検査/17変異がrc=0。
check-changedの2回目はfull選択でrc=0。検査中の票/ソース変更は無し。
