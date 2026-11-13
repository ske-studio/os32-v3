# arch/x86/kernel/mtrr_wc.c / mtrr_wc.h — CPU の WC lease

> 状態: **現行 (2026-11-01)** — CPU 部品。GeForce は kernel の video_wc を通して利用する。

## 契約と境界

`mtrr_wc.h` は `struct x86_wc_lease` と acquire/release を公開する。
64bit の保存欄は、この木の共通型に u64 が無いため `unsigned long long` とする。
単一 CPU・単一利用者・協調切替が前提。呼出し中は描画を止める。
`base/bytes` は 1 MiB 以上、4 KiB 以上の整列した 2 の冪の区間で、32bit 物理番地内とする。
呼出側は BAR の所有・非交差・alias 不在・既存 PTE と BAR0 の実効 UC を確認する。
この部品は割当、ログ、装置アクセス、PTE/PAT/台帳/surface の変更を行わない。

`acquire` は WC 成立で 1、能力無しまたは復元確認済みの適用失敗で 0 を返す。
復元の readback が一致しなければ -1 とし、保存欄と active/owned を保持する。
-1 のあとに UC fallback として継続してはいけない。呼出側が起動を失敗させる。
`release` は復元成功・借用解除・inactive の場合 0、復元未確認の場合 -1。
未確認時は active を保持する。二重 release は命令を実行しない。
GeForce の呼出側は最初に取得した lease を起動中保持する。通常の表示停止では release せず、
identity 喪失時の共通 stop と受入版の明示的な停止だけで release を呼ぶ。
再開始は active lease の前回結果 (復元未確認の負値も含む) を返し、命令列を繰り返さない。

## 能力と組の選択

EFLAGS.ID を反転し、元の EFLAGS を戻して CPUID の存在を調べる。
GenuineIntel、leaf1 の MSR/MTRR、MTRRCAP.WC、DEF_TYPE.E を確認する。
PAT 対応時だけ PAT2=UC- を読み取り確認する。
最大拡張 leaf を先に読み、80000008h が使える場合だけ MAXPHYADDR を採る。
非対応時は 36bit。報告された幅は 32〜52bit の範囲で計算する。

全可変組を走査し、重なる非 WC は拒否する。WC 1 組が全域を覆えば借用し、
部分 WC だけなら 0。重なりが無ければ最小番号の V=0 の組を使用する。
既存 WC と競合する後続の組も検査する。元の BASE/MASK は予約 bit を含めて保存する。
他組・固定 MTRR・PAT は書かず、DEF_TYPE は全 bit を復元する。

## 命令順と失敗

[Intel SDM Vol.3A, 253668-060US](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf)
§11.11.1–4/7–8・§11.12 と確定設計 `docs/decisions/realhw/GF_ACCEL_DESIGN.md` §3–4 に従う。
CPU 命令だけのため、既存ドライバの実装は転記していない。

EFLAGS/CR0/CR4 保存 → CLI → CD=1/NW=0 → WBINVD → PGE clear（元が 1 のとき）
→ CR3 reload → DEF_TYPE.E clear → MASK.V clear → BASE → MASK → 両 MSR readback
→ DEF_TYPE 復元 → WBINVD → CR3 reload → CR0 → CR4 → EFLAGS 復元。
readback 失敗なら E=0 の同じ枠内で保存組を書き戻し、両 MSR を再確認する。
成功状態は命令列の終了後に公開する。復元も同じ枠を使う。

BIOS WC 借用は取得時に命令列を省き、終了時は MSR を書かずに同じ cache/TLB の
排出枠を通す。装置 identity が失われても CPU だけで排出できる。

## 落とし穴と検証

MTRR 非対応時は CR4/MSR/WBINVD を実行しない。SSE/SFENCE は要求しない。
PGE=1 の場合は両 CR3 reload のあいだ PGE を落としたままにする。
ホストの fake 命令 trace は設定・復元・rollback と PGE/IF 両値を検査する。
実 CPU の WC 成立・PCI 排出・表示寿命は PM のゲスト/実機受入で確認する。
