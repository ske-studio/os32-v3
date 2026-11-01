# drivers/video/geforce/gf_wc.c — 設計
> 状態: **現行 (2026-11-01)** — GF_ACCEL_DESIGN §3–4 の S10
ソース: `drivers/video/geforce/gf_wc.c`

kernel に lease を 1 個保持する。platform の `wc_start` は video_start 後、初回 present 前に呼ぶ。active 時は前回結果を返して acquire を繰り返さない。復元未確認の負値も保持する。能力・適用条件不足は 0 (UC)、成功は 1、復元未確認は負値 (GF 起動失敗)。`wc_stop` は CPU 側排出・復元を x86_wc_release に委譲し、identity 失敗後も装置へアクセスしない。

BAR1 の 32bit/prefetchable・512 KiB 整列・decode 長・既存予約の資源 bit・RAM/他資源との非交差・DISPLAY の 75 頁を読み取り確認する。master と current PD を走査して表示外/別 VA の CPU alias を拒否する。他 AS の装置写像は共有 PT、private app/lease は RAM のみという既存 paging 契約を使う。BAR0/表示の PCD=1/PWT=0/PAT=0/supervisor/RW を確認し、PTE/PAT/台帳/surface は変更しない。

BAR0 の実効 UC の確認は読み取りだけの CPUID/MSR 検査で行う。ID bit/Intel/MSR/MTRR と拡張 leaf の範囲を確認した上で、BAR0 に重なる WC 組と WC default を保守的に拒否する。PAT2=UC- の確認と表示域の MTRR 選択・変更・復元は既存 x86 部品が行う。根拠は [Intel SDM Vol.3A 253668-060US](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf) §11.11–11.12、表11-6。CPU 命令のみなので [HW3] の既存装置実装参照は不要。

report は計測終了後の文字列 record を serial_puts へ渡す口。計測本体・sample 配列は後段。ホスト T1 は real C の結線と状態不変を検査し、実機 WC/PCI 排出の成立は PM の受入で確認する。
