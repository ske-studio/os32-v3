# drivers/video/geforce/gf_accept.c — 受入専用の計測

> 状態: **現行 (2026-11-01)**

`geforce-accept` だけがリンクする。受入版の backend の `gf_present_rect` 参照を
`gf_accept_transfer` にコンパイル時に置換し、この入口から実物の
`gf_present_rect` を呼ぶ。常用 backend・present・kdebug の入口と配列は増やさない。
byte の旗も受入版 present.o だけに渡す。同じ `-Os`、同じ実装の端処理を使う。

最初の呼出しは video_start 後・表示経路切替前。既存の kernel 所有 callback で
WC を停止し元の PTE/MSR を採り、WC 方式だけ再取得する。UC 方式は BIOS WC を
UC と呼ばず理由 `bios-wc` で SKIP。未分類も測定しない。再 GUI で通常の初期化が
WC を再取得しても UC 版の入口で停止する。MTRR の書込み列は gf_wc/mtrr_wc に委譲する。
CPU の観測は Intel SDM Vol.3A §11.11/11.12 の read-only CPUID/MSR とし、能力を
確認するまで MSR を読まない。PTE は書かない。取得に失敗した WC は SKIP。

2回目の present は GF への経路切替後。75頁の固定 RAM パターンを既存の
pgalloc から kernel owner で一時確保し、0..255 を繰り返して満たす。
部品の BSS に307,200 byteを置くと既存の部品上限を超えるため、計測後・失敗時に
解放する。解放の返値は成功1・失敗0で、成功時はCLIENTの復元へ進む。
表示を全面パターンに一度転送し、同じパターンで各矩形を10回 warmup、
100回×3計測する。0番のケースは時計だけの空測定。幅、高さ、座標は固定表が持つ。
時間は sys_time_now の上下32bitを結合し、呼出し直前から正常 drain 後まで。
時刻失敗・逆行・u32に収まらない時間は 0xffffffff、転送失敗は残りも失敗値にし、
以後 BAR へ触らず backend の既存停止経路へ戻す。

readback は各矩形の300回完了後、point を確認してから行う。計時中の VRAM readback
とシリアル出力はない。各100回の記録に転送byte/呼出したpoint/失敗/不一致数を付け、
計時後に report callback へ1行ずつ渡す。パターンは記録の送出中にカメラで確認する。
全ケース後に RAM CLIENT 全面を再転送して戻す。1起動で1回だけ計測する。

レコードと操作手順の正典は GF_ACCEL_DESIGN §5、集計の仕様は tools/accept/gf_accel
の個別設計書。host模型は実CPUのWC・PCI drain・表示を証明しない。
