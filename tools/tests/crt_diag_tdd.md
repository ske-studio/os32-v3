# CRT diagnostic — read-only boot snapshot

票: `/home/hight/os32-tmp/run/crtfreq/CRTFREQ_DESIGN.r2.md` §1・§3、依頼 `PACK_step1.md`。
対象: `include/crt_diag.h`、`kdebug/crt_diag.c`、`init/kernel.c`。
実行: `make check-crt-diag-host [MUTATE=1]` (`HOST32_RUNNERS=qemu`)。

- RED: 空の read/format/bootlog 実装で実行し、`out_n == 0 && ev_n == 14 && mem_n == 21` が失敗 (make rc=2)。最初の試行は未使用 fake_map の -Werror で止まり、ハーネス修正後に assertion の RED を確認。
- GREEN: 実物の C を include、ホスト ILP32 と i386-elf GNU11 -Werror で検査する。
- ① 独自 shim は inp/inpw/outp/outpw/io_wait の全イベント (幅・ポート・値) を記録。shim 自体の 8/16-bit・wait 記録を確認してからゼロ OUT を検査。
- ② 独立した期待列で IN 14 本の順序・幅・生値を比較。FFh も解釈・マスクせず保持。
- ③ P2V/P2V_IO を 1MB 偽配列へ割り当て、BIOS 6 番地 + 偶数 MSW 15 番地、写像種別、読んだ値を比較。
- ④ 初期値を設定後、偽配列全体を mprotect(PROT_READ)。同じ値の書き戻しを含め、書き込みは fault になる。
- ⑤ 独立した完全な期待文字列で鍵の順序、全番地、(15)/(12)、sdip_bank=unknown を比較。
- ⑥ 容量 0〜必要長+2 を 1 バイトずつ試し、前後の sentinel、切り詰め prefix、NUL、長さ/-1 を検査。負容量・NULL/0 も検査。
- ⑦ 同じ raw を 2 回整形して完全一致し、raw 不変・I/O/メモリ写像呼出しゼロを検査。
- bootlog: kprintf を 1 回呼び、console と同じバイト単位取り込みで実物の bootlog_push/compose に渡す。2 行が欠けず、dropped=0 で残ることを比較。
- 変異 4 本: OUT (outpw) 追加 / SDIP 11 本 / MSW 8 個 / sysa 鍵欠落。写しの木で回し、コンパイル成功後の assertion rc=1 だけを RED と認める。変異なしの写しも GREEN を要求。

書式は `[crt]` (218 bytes) と `[crt2]` (129 bytes) の **2 行** (改行を除く)。
1 行なら 341 bytes で票の 220 bytes を超えるため、パックの許容に従い分割した。
`BOOTLOG_HDR_MAX=192` はファイル先頭ヘッダの予約領域で本文の行上限ではない。
`kernel/console/console.c` は `%s` を bootlog_push に長さ付きで渡し、`init/bootlog.c` は
本文全体 `SYS_BOOTLOG_TEXT_MAX=16384` bytes まで積む。lib/kprintf.c の `%s` は
固定長の中間バッファで切らず shell_print_utf8 へ渡す。各行は票の 220 bytes 以内。
全体容量を使い切った後は既存 bootlog 方針のとおり破棄される (本試験は容量内の完全保持)。

最終検査の結果・ログは依頼の完了報告に記載。エミュレータ・実機の採取は PM、未実施。
