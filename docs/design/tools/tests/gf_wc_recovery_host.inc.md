# tools/tests/gf_wc_recovery_host.inc — 停止経路の回帰

> 状態: **現行 (2026-11-01)** — GF_ACCEL_DESIGN §4・§8 T1
ソース: `tools/tests/gf_wc_recovery_host.inc`

`test_geforce.py` の ILP32/qemu 試験。実物の HAL・backend・geforce_body の共通 stop・gf_wc_stop と、exec から切り出した fault 移譲/通常文脈の着地/finish/所有者通知、gui_owner_exit を結ぶ。longjmp は実物の setjmp.asm をリンクする。開始時の WC 取得と MSR/device 操作は模型であり、適用条件は既存 gf_wc_host が検査する。

取得済み WC の identity 不一致を初めて gfx_init_200 / gfx_init の probe で発見させる。shell fault は例外深さ 1 で exec_fault_recover を呼び、longjmp 後には lease がまだ active、通常文脈の exec_pending_finish 後には停止済みであることを見る。再起動・再取得を挟んで 3 回繰り返す。

owned / BIOS 借用それぞれで MTRR の保存値・lease の解放・98 route/console/高さ復帰を照合し、GF の PTE と区間/資源台帳を比較する。HAL の surface 世代更新は通常契約のまま。修正前 cead3085 は両製品ソースを git show で差し替えて実行時 RED、--mutate は probe の停止と shell 異常終了の shutdown を個別に除去して実行時 RED を要求する。
