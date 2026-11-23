# PowerMac の HW-BASIC 調査
対象は `origin/main` (`2b705d0e2`) のソース。設計・票は `docs/decisions/realhw/HW_BASIC_DESIGN.md` と `docs/tasks/realhw/TASK_HW_BASIC.md`。

## 製品像と実行可否
**`hw` は常駐シェルに入る。現行の PowerMac では `hw run` の実行に必要な `HW_OP` が未提供。**
`build/ppc-user.mk:89–117` は共通の `userland/shell/*.c`（`cmd_hw.c` と `hwb_*.c` を含む）を resident の `shell.bin` にリンクし、`cmd_sys.c:687` が通常コマンド表に登録する。
`build/targets/ppc-powermac.mk:30–46` は root に `/sys/shell.bin` を入れ、`ppc_kernel.c:560` に共通シェルループの起動経路がある。段 E の受入記録は今回確認していない（`TASK_PPC_SHELL.md:12` の段の記載は未収録）。
KAPI v78 の `kdebug_call` は存在する。モジュール無しなら `OS32_ERR_NOSYS`。同梱は `FEATURE_KDEBUG` 次第で、PowerMac 版 `module_diag.c` が処理するのは `PROBE_OP` の LIST のみ。台本はロード時の `HW_POINT` で止まり、終了値は NOSYS (3)。

## 命令と提供状況
W は 8/16/32。KAPI の対応は `cmd_hw.c`、命令と中止点は `hwb_exec.c`、PC-98 側の特権処理は `kdebug/hwb_access.c` と `drivers/pci/pci_hwb.c`。
`sdk/kapi_target_ppc.py` の不在群には IDE（構成で解除）・geometry・platform（NP2）・V86 などがある。下表の共通 KAPI は不在群の対象外。`PROVIDER_EXTERNS` の列挙は不在スロット指定と別。

| 命令 | KAPI／カーネルの口 | PowerMac での提供状況 |
|---|---|---|
| `device d = bdf …` / `device d = id …` | BDF はシェル内。ID は `pci_count` / `pci_get` | 両 KAPI あり。列挙は起動時に登録できた UniNorth の範囲。 |
| `pcireadW` / `pciwriteW` | `kdebug_call(HW_OP)`、幅付き PCI 構成アクセス | HW-BASIC 経路なし。UniNorth の内部 32bit 読み書きあり。既存 16bit 書き口は Command 専用。 |
| `window v = bar d n` / `bar d n + offset` | 参照作成はシェル内。利用ごとに PCI header type (8bit)、BAR (32bit) を読む | PCI 読み口が必要。シェルは BAR をマスクして直に物理番地として扱うため、OF の PCI バス番地→CPU 物理番地変換も必要。 |
| `mmioreadW` / `mmiowriteW` | `HW_OP` の `HW_PHYS`、一時物理窓 | HW-BASIC 経路なし。装置ごとの MMIO マップとアクセス関数は既存。 |
| `mmiofillW` / `mmioblock32` | 前者は単発の反復、後者は `HW_OP` の一括要求 | 未提供。一括は現行 x86 の命令列に依存。PPC でも全範囲を先にマップし、途中の再マップや読戻しなしで実行する口が必要。 |
| `ioreadW` / `iowriteW` | `HW_OP` の `HW_IO`、x86 のポート命令 | PC-98 の 16bit ポート空間なし。`platform_io.h` のバイト名は ESCC の MMIO 別名。ポート要求は NOSYS を返す。 |
| `wait us` / `poll W` | `sys_time_now` (µs)、`sys_yield`、`HW_POINT`、poll は対象の読み口 | 時計・yield は提供済み。POINT と対象アクセスが不足。 |
| `print` / `log` / `hw run -o` | `kprintf`、ファイルの `sys_open/write/close`。台本読込は `sys_open/read/close` | 共通 KAPI あり。ファイルログは書込み可能なマウント先が必要。命令実行は POINT に依存。 |
| `probe` / 中止点 | `PROBE_OP` / `HW_POINT` → `exec_hwb_abort_take` | LIST のみ。中止印の消費関数は共通 exec にあり、HW_OP と PowerMac 入力からの要求接続が必要。 |

OF がデバイスと窓を記述し、構成空間の実取引は UniNorth の address/data MMIO。現行の列挙用 backend は単一 root を保持し、起動時の窓登録は `gmac_ready` に依存する（`ppc_kernel.c:276–286`、`pci_backend.c`）。`pci_config.c` は Type 0/1、LE 変換、割込み保護を実装する。OF の `assigned-addresses` と親の `ranges` による変換には `mpic_tree.c`・`usb_tree.c` の先例がある。列挙側の BAR サイズ測定は無効。

## 本線に頼む物
- `HW_OP` の wire・検証・結果コード、POINT とログを共通化し、PCI 構成・BAR のアドレス変換・物理窓・ポートを後端の契約にする。現行の命令名、幅別の単発アクセス、明示的な読戻し、終了値を維持する。
- BAR は実機の構成値を読み、後端で物理番地に変換してから既存形式でログに残す。生の `pcireadW` 値は維持する。PPC 専用の命令や新しい公開 KAPI は不要。バーストのストア列は機種側で実装する。

## PowerPC で足す物
- PowerMac の kdebug 部品・型付き import に HW_OP を接続。OF から必要な host bridge 窓を GEM と独立に得て、列挙と幅別構成アクセスを接続する。サブワード書込みは実幅で扱い、汎用の 32bit read-modify-write で隣接ビットを触らない。
- BAR 経由の OF 変換と `phys` 用の PPC 物理窓を接続。cache-inhibited/guarded の装置マッピング、装置のバイト順と順序保証、アクセス失敗時の窓の解放を実装する。停止入力を常駐シェルの中止印に接続する。
- **OHCI** — `usb_tree.c` / `usb_platform.c` の OF 窓で LE の HcRevision・状態を読む。**ATA (KeyLargo)** — `ide_tree.c` / `platform_ide_bus.h` の MMIO 窓で 16 バイト間隔の task-file と制御窓を使う。HPT370 の台本からログと反復の形を借り、ポート番地を KeyLargo に持ち込まない。
- **FB** — OF が渡す framebuffer の物理基点・stride・depth を引数にし、`backend_of_fb.c` の資源を基準に画素メモリを読む。GPU 固有レジスタは該当 PCI BAR が得られた場合に扱う。**GEM** — `gmac_tree.c` の OF 変換と `lan_gem_io.c` の LE・順序を利用して状態を読む。
- mac99 と G4 で、まず読取りだけの台本で構成値・窓・ログを確認する。読取りで状態が変わるレジスタも装置資料で確認し、稼働中のドライバと競合する操作は避ける。

検証範囲は静的なソース照合。ビルド・QEMU 起動・G4 実機試験は未実施。
