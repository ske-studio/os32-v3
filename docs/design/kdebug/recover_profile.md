# kdebug/recover_profile.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4C_DESIGN.md・GF6200_RECLOG_DESIGN.md・HWDEV_DESIGN.md・REALHW_RECOVER_ADDENDUM_R2.md・REALHW_RECOVER_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_profile.c`

## GF6200_4C_DESIGN

セル検査は `recover_profile.c:35–45` のread/write部分を共有化し、RUNのsafe point/予算で呼ぶ。restore/redrawやRESTORING専用の時計は呼ばず、試験値書込みが失敗した場合も既存どおり元セルの復元を一度試みる。

## GF6200_RECLOG_DESIGN

`recover_profile.c:recover_hal_wait_frame()` は -11=設定/引数不正、-12=PIT count範囲外、-13=sample上限。

定数は内部ヘッダで名前を付ける。PIT のラッチ・IRQ区間・待ちアルゴリズムは変えず、rcだけ区別。

これで上位 wait_frame の一般失敗と、その内側の PIT 待ち失敗を特定できる。非UARTの待ちは txd に混ぜない。

## HWDEV_DESIGN

profile は 98 画面・予算・indexed 復帰等。後者も全体を装置部品に移さない

- S0 の決着: `recover_gf_profile` の quiesce を HWDEV 不在時に自動実行する経路は作らない。モデルだけでは対象の所有・identity・BAR の照合と bus がなく、開始途中の拒否後に書込みを始めるのは試行の開始条件を迂回するため。§6 の「不在でも使用可能」を共通 98 S0 経路に訂正し、GF 停止を伴う独立経路と混同しない。FAILED/cold の終端は再実行せず結果を保持する。

## REALHW_RECOVER_ADDENDUM_R2

- UART設定が異なる場合も実bits/frame・baudで再計算して表の枠内を確認する。UART は `ceil(bits/frame*10^6/baud)` µs を待ち単位とし、正常送信は1文字1 pollで計数する。TxRDY未成立で繰り返す待ちも1回ごとに加算し、初回確認を別pollとして二重計数しない。UART以外のpoll/delayは1ms単位へ分割（指定待ちを切り下げない）し、各反復で残予算を検査する。

- 秒→tickは実HAL周波数で切上げ、checked整数演算、0禁止・半周未満。hostはfake tick周波数を明示し同じ換算を使う。host経過秒を実機根拠に転用しない。

## REALHW_RECOVER_DESIGN

| シリアルで確認する値 / 証明の限界 |
| --- |
| 新設 `recover status` で profile/version、CUI、DMA/IRQ、route/legacy、page、PTE 復元結果。09A8h 下位 2 bit=1。読めない FF は設定した値を `shadow` と明記。legacy decode 解放の読戻し成功後、A0000h の予約した空きテキストセル 1 つの元値を保存し、元値と異なる試験値を書いて読戻し、一致後に元値を戻して再照合する。98 側へ届かない、復元不一致、検査不能なら FAIL。実表示はユーザー確認 |

- [HW3] 根拠: 98 復帰の資料と FreeBSD 対照は `kernel/v86/v86_io.c:250`。NVIDIA 公開 MIT 実装 `/home/hight/os32-tmp/refs/geforce6200/xf86-video-nv/src/nv_driver.c:2290` は選別した保存状態を順序付きで復元する前例で、Ra266 の冷間復帰保証ではない。

- PBUS の意味は `/home/hight/os32-tmp/refs/geforce6200/envytools/docs/hw/bus/pci.rst:50`、`:60`。ライセンスは `/home/hight/os32-tmp/refs/geforce6200/INDEX.md:6`、`:7`。専有 driver/ROM x86 コードは解析しない。新しい装置 profile はメーカー資料と BSD/MIT 実装の照合を実装開始条件とする。

UART の待ちの実際の分解能が 1 フレーム時間 (1042µs) 以下であること
