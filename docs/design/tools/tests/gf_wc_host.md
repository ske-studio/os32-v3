# tools/tests/gf_wc_host.c — 設計
> 状態: **現行 (2026-11-01)** — GF_ACCEL_DESIGN §8 T1
ソース: `tools/tests/gf_wc_host.c`

test_geforce.py から起動する ILP32/qemu fixture。gf_wc.c の読み取り CPU primitives だけを fake に置換し、実物の適用条件・geforce_body の共通 stop・backend の init/leave/enter/shutdown/present を接続する。MTRR の命令列自体は T0 の担当。

owned と BIOS 借用それぞれで初回、再GUI、同一 init、再設定、abort 用 stop callback、初期失敗、identity 失敗、二重 stop の元状態を比較する。PTE 全頁、PD、資源/区間/surface 配列の不変を検査する。active な lease を acquire に再度渡すと試験が実行時に失敗する (Opus 送り項目 A)。復元不能の負値は再 init でも WC 成功に化けず、後続の stop で再試行する。

RAM/他資源の重なり、非 prefetchable/短い BAR、res_mask、PWT/PAT、別 VA alias、表示外 76 頁目、BAR0 の WC、CPU 非対応を拒否し、非対応 CPU は MSR read 無し。report callback を確認する。--mutate は共通 stop/二重 acquire 防止/開始 callback/alias 検査を 1 行ずつ除去し、コンパイル成功後の実行時 RED を要求する。
