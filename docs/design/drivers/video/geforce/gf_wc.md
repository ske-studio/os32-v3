# drivers/video/geforce/gf_wc.c — 設計
> 状態: **現行 (2026-11-01)** — GF_ACCEL_DESIGN §3–4 の S10
ソース: `drivers/video/geforce/gf_wc.c`

kernel に lease を 1 個、最初の取得から起動中保持する。GeForce module と本番 context は起動中 pin され、通常の停止・CUI 復帰・再開始では解放しない。platform の `wc_start` は起動時の module 初期化で、BAR・台帳資源/領域・DISPLAY surface・PTE 写像が存在し、production の prepare と identify の結線が済んだ直後に呼ぶ。描画開始でも video_start 後、初回 present 前に呼ぶ。active 時は前回結果を返して acquire を繰り返さない。復元未確認の負値も保持する。能力・適用条件不足は 0 (UC)、成功は 1、復元未確認は負値 (GF 起動失敗)。共通 stop は identity 喪失時だけ `wc_stop` を呼ぶ。受入版は比較のため明示的に `wc_stop`/`wc_start` を呼ぶ。`wc_stop` は CPU 側排出・復元を x86_wc_release に委譲し、identity 失敗後も装置へアクセスしない。

初期化時の負値は module 読込みを中断せず、active lease に保持した結果で後の描画開始を失敗させる。0 は UC 継続であり、描画開始で適用条件と acquire を再試行する。能力不足等は MTRR を書かないが、MTRR 書戻し不一致から保存値の復元に成功して 0 を返す場合は、再試行でも wc_begin/wc_end を通り得る。

再起動時は停止と 98 復帰に先立って WC lease を解放する。

active lease の再開始は適用条件を再評価せず前回結果を返す。固定 BAR 予約・写像は停止中も維持する。

手本は nouveau の [`drivers/gpu/drm/nouveau/nouveau_ttm.c`](https://gitlab.freedesktop.org/drm/nouveau/-/blob/master/drivers/gpu/drm/nouveau/nouveau_ttm.c): `nouveau_ttm_init` の `arch_phys_wc_add(resource_addr(device, 1), resource_size(device, 1))` と `nouveau_ttm_fini` の `arch_phys_wc_del(drm->ttm.mtrr)`。WC は装置の初期化で取得し、装置の終了で解放する寿命に合わせ、モード切替・表示停止では外さない。

BAR1 の 32bit/prefetchable・512 KiB 整列・decode 長・既存予約の資源 bit・RAM/他資源との非交差・DISPLAY の 75 頁を読み取り確認する。master と current PD を走査して表示外/別 VA の CPU alias を拒否する。他 AS の装置写像は共有 PT、private app/lease は RAM のみという既存 paging 契約を使う。BAR0/表示の PCD=1/PWT=0/PAT=0/supervisor/RW を確認し、PTE/PAT/台帳/surface は変更しない。

BAR0 の実効 UC の確認は読み取りだけの CPUID/MSR 検査で行う。ID bit/Intel/MSR/MTRR と拡張 leaf の範囲を確認した上で、BAR0 に重なる WC 組と WC default を保守的に拒否する。PAT2=UC- の確認と表示域の MTRR 選択・変更・復元は既存 x86 部品が行う。根拠は [Intel SDM Vol.3A 253668-060US](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf) §11.11–11.12、表11-6。CPU 命令のみなので [HW3] の既存装置実装参照は不要。

report は計測終了後の文字列 record を serial_puts へ渡す口。計測本体・sample 配列は後段。ホスト T1 は real C の結線と状態不変を検査し、実機 WC/PCI 排出の成立は PM の受入で確認する。
