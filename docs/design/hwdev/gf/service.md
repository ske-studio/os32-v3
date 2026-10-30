# hwdev/gf/service.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/service.c`

HAL に GF のレジスタ・ROM・recover token を出さない。
成功 0、引数・範囲不正は既存 `OS32_ERR_INVAL`、未対応は `OS32_ERR_NOSYS`、装置処理失敗は既存下層と同じ -1。新しい失敗分類を作らず、最後の段・対象・既存 rc を保持する。
`video_start` は初回だけ devinit→modeset。stop は局所後始末→quiesce、続けて上層が 98 切替を行う。停止済み stop・同値 blank は成功。start-offset は BAR1 相対 byte、0 だけ成功、非零は NOSYS。
局所 index/lock/PLL 途中の退避値は基準値と分ける。初期化失敗、bus/窓/stop 失敗、ID/BAR 変化は再利用不可。再 devinit はせず、正常 stop 後の ID/BAR/Command/legacy/PMC 照合成功時だけ C 相当の再 modeset を許す（同:78–92）。
順番は **route00→ID/定数長/base/資源/写像確認→Command=0x0102 設定・生の 16bit 読戻し確認→ROM 採取・解読→A2 の legacy 無効化/解錠→main0〜4→基準値採取→mode 計算→共有列→必須 CHECK→初回転送/セル→route01**。A1 は本番の全経路から外す。
