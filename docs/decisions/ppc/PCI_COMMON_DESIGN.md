> 出所: ~/os32-tmp/run/pci-common/DESIGN.final.md (Fable 起草、sol レビュー 2 回、2026-11-19 確定)。
> 状態: **確定 (2026-11-19)** — wip の置き場 (PowerPC の P 段の参照用)。main に入れる時に file:line を「パス + 関数名」に直す。

# PCI の本体を両機種で選べるようにする — 確定設計 (final) (KAPI の埋め 6d-i・6d-ii の続き、PPC 依頼 #26)

読んだ木: `wt/int-1119e` (5916dae4e)。確定設計 `docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md` §3 の行 77・191・192・262 の続き。
議論しない前提 (PM 決定): PowerPC は既定の 1 ドメインだけを見せる、KAPI の形と x86 の答えは変えない、[ABI] を動かさない。
REVIEW.r1 の末尾の PM 決定 4 つ (U1 = GMAC の root・窓が無ければ不在、U2 = 全台 NONE / NO_DRIVER、BAR = PowerPC は読むだけ、表示 = x86 の文字列は不変) を前提に入れた。

### r1→r2 の変更 (指摘ごと 1 行)

- P1 読み出し: PowerPC の後端は範囲を確かめてから `reg & ~3` に揃えて読む (拒否しない)。非整列の番地 (0x0e・0x19・0x3d) を使う列挙の host 試験 A7 を足した (§3・§7)。
- P1 write16: 契約に「Command (04h) 専用」を明記。PowerPC の後端は `reg == 4` だけを書き、ほかは書かない。汎用の半語書きは作らない (§3)。
- P1 起動順と BAR: PowerPC の列挙は読むだけ。口 `pci_config_bar_measure()` を足し、0 の後端では `pci_barsize_record` を呼ばない。x86 は 1 で今のまま (§3)。
- P1 後端の入力: `pm_pci_boot(window, bdf)` を `ppc_kernel.c` の直後で呼ぶ形、窓なしの結果、BDF の組み立てと範囲、write32 の失敗の扱いを書いた (§3)。
- P1 KAPI の結線: 手結線をやめ、`PROVIDER_GROUPS['pci']` を外すのと同時に `PROVIDER_EXTERNS` へ `kapi_pci_bind_info` を足し、既存の生成の経路で EXEC の 3 像をリンクする (§4・§6 P3)。
- P1 段の順: M3 と P1 の選択を同じ着地 L にまとめ、P2 (初期化) → P3 (公開) の順。6d-ii の BDF・項目順の確認を P2 の完了条件に (§6)。
- P1 PPC のシェル: `lspci` の登録・ユーザー側 `pci_decode` のリンクを段 P4 に入れ、KAPI の戻り値と写しは専用のゲストの探針で照合 (§6・§7 A9)。
- P2 像と期待表: r1・FD・MINIMAL・PPC の各像の根拠を表に入れ、fixture 2 枚と `order.mk` の差し替え、FD の空きの実測を段に入れた (§2・§5・§6)。
- P2 L08: `test_dep_dir_a_l08.py` の入力・抽出先・変異の移動を M3 に入れた (§6・§7 A6)。
- P2 試験: A1 は書式展開後のログを捕る、A3 は規定外の idx の直接の照合 + 共通側の上限、A10 (起動順の変異) を廃して偽 MMIO の「書きが 0 回」に替えた。延期票の方針を §6 と §7 で揃えた。
- P2 U1 の診断: USB 有効の像で、OHCI が実際に見つかった root を `gmac.root_pa` と比べて 1 度だけ出す段 P5 (§6)。
- P2 表示: x86 の 3 つの文字列は不変、PowerPC は `profile.h` のマクロで方式に依らない文言 (§3)。
- P3 E1〜E8: 参照行と数え方を直した (§1)。未決 U1・U2 は PM 決定で閉じた (§8)。

## 1. 実物の確認

| 依頼 | 実物 (file:line) | 判定 |
|---|---|---|
| 1 選択 | `sys/conf/features/pc98-devices.mk` が `sys/dev/pci/pci.c pci_barsize.c pci_hwb.c pci_decode.c pci_bind.c pci_bind_match.c` を選ぶ。`build/select.mk` で x86-pc98 以外は `$(error)` | 一致 (ただし E1) |
| 2 転送 | `platform/pc98/device_pci.c` に 4 本。宣言は `sys/include/platform_device.h`。KAPI の出口は `sys/kapi/kapi_generated.c` (3 本) と `sys/kapi/kapi_sys.c` の `kapi_pci_bind_info` (4 本目を :201 で呼ぶ) | 一致 (E2・E3) |
| 3 走査 | 表示 `sys/dev/pci/pci.c` `"[pci] mech#1 absent"`・`:192` `"[pci] mech#1 ok"`。起点 `:194` `pci_scan_bus(0, 0)` (probe は `:186`)。後端の口は `sys/include/pci_config_contract.h` の 4 本 | 一致 |

食い違い (8 件):

- **E1** 本体は 2 ファイルでなく 5 ファイル。`pci.c` が `pci_barsize.c` を、`pci_bind.c` が `pci_bind_one`・`:98` が `pci_bind_info_compose` (どちらも `pci_bind_match.c`) を要る。`pci_decode.c` は `pci.c` が直に使う (`pci_extract8/16`・`pci_header_layout`・`pci_bar_kind` 等)。ほかの 3 本は `pci.h` 経由で読める形で、直の利用は数えていない。
- **E2** KAPI が直に呼ぶ転送は 3 本。`pci_bind_info` の target は `kapi_pci_bind_info` (`sdk/kapi.json`) で、転送の 4 本目はその中から呼ばれる。
- **E3** `sys/kapi/kapi_sys.c`・`sys/kapi/kapi_generated.c` は `C_COMMON_PC98_PENDING` (`build/sources/common.mk`) で、PowerPC の像に入っていない。PowerPC は別に生成したラッパー (`build/targets/ppc-powermac.mk`、`gen_kapi.py --target ppc-powermac`) をリンクし、`sdk/kapi_target_ppc.py` の `PROVIDER_GROUPS['pci']` で 4 スロットが不在の答え (`pci_count` は 0 (`:206`)、ほかは `OS32_ERR_NOSYS`) になっている。診断を kapi_sys.c に残すと PowerPC で出せない。
- **E4** `sys/platform/powermac/pci_config.c` は既に在るが、口は `pm_pci_read32(struct pm_pci *, bdf, reg, u32 *value)`・`pm_pci_write32(…, u32 value)` (`lan_boot.h`) で契約の 4 本ではない。`address()` は `bdf == p->target_bdf` を要り、GMAC の quiesce (`:86-126`) も同居。probe・write16 は無い。
- **E5** PowerPC の設定空間の窓は 2 つ別に在る: GMAC 用 (`kernel_boot.c`、`gmac.root_pa`、起動時から `PPC_BOOT_PCI_ADDR_VA`) と USB 用 (`usb_boot.c`、`pm_usb_roots[]`、`PPC_BOOT_USB_VA`)。**2 つが同じ root かはコードが保証していない**。PM 決定の「GMAC・USB の載る 1 ドメイン」は実行時に確かめる物 (PM 決定 U1: 既定 = GMAC の root)。
- **E6** `pci_bind_all` の呼び手は LAN 部品 `sys/dev/net/e82557/lan_module.c` だけ。PowerPC には呼び手が無く、そのままでは台帳が空で `pci_bind_info` は全 idx で失敗する。
- **E7** `sys/dev/pci/pci_hwb.c` が mech1 の probe と取引を `io.h` で直に持つ (kdebug 用)。6d-i の後端と同じ手続きの 2 つ目が x86 に残っている。この段では動かさない (記録だけ、§6 の「動かさない物」)。
- **E8** `pci.h` を読む上位装置は ne2k の PCI 版・OHCI・EHCI には無い (USB は `sys/include/usb_platform.h` の口)。**直に include する**のは 17 ファイル (e82557 1 = `e82557_eeprom.h`・Trident 1・GeForce 2・hwdev/gf 5・kdebug 5・platform/pc98 3)。集計の範囲: 製品の木の `.c`・`.h` で `pci.h` を直に include する物。生成物 (`hwdev/generated/`・`kdebug/generated/`)・`drivers/pci/` の内部・`tools/` は除く。ヘッダ越しの間接は数えていない。全部 x86 だけの部品・機種の物。

統合の判断 3 点 (POLICY_DEV §1):

1. 同じ仕事の実装を 2 つにしない — 走査・記録・台帳・転送・診断は 1 つ。機種が持つのは設定空間の取引と起点と名前だけ。PowerPC の後端は既存の `pm_pci_read32/write32` を呼ぶ (取引を写さない)。
2. 既にある口を使う — `pci_config_contract.h` に足す (新しい口の束を作らない)。転送は 6f と同じ `kernel/dev/`。
3. 置き場は持ち主に合わせる — 本体は `drivers/pci/` のまま (移さない、差分を出さない)。選ぶ行だけを機種の feature から共通の組へ。

## 2. 選び方

**推奨: `C_COMMON_EXEC` に入れる (新しい feature は作らない)。**

| 案 | 触る登録ファイル | 評価 |
|---|---|---|
| A `C_COMMON_EXEC` (推奨) | `build/sources/common.mk` 1 行、`pc98-devices.mk` 1 行 | `build/select.mk` に触らない。KAPI の不在表は arch ごと 1 枚 (`kapi_target_ppc.py`) なので、EXEC の像は全部が提供者を持つ必要があり、PCI の有無は EXEC と一致する |
| B `FEATURE_PCI` | `select.mk` の `IMAGE_FEATURES`、像 6 枚、`build_profile_*.json` | 値が常に EXEC と同じになる feature。登録の手間だけ増える。PCI を持たない EXEC の機種が出た時に作る |

各像に入る物 (○ = 入る):

| 像 | 根拠 | 本体 5 + 転送 | 後端 |
|---|---|---|---|
| x86 通常 | `sys/conf/images/pc98.mk` EXEC=1 | ○ (今と同じ集合) | `sys/platform/pc98/pci_config.c` |
| x86 r1 | `build/kernel-objects.mk` が `C_KERNEL` を継ぐ | ○ (同じ) | 同上 |
| x86 FD・MINIMAL | `build/image.mk` が通常の `vmkernel.lz4` を梱包 | ○ (通常と同じ物) | 同上 |
| PPC kboot-sh / link-sh / shell | `build/images/kboot-*.mk:5` EXEC=1 | ○ | 新 `sys/platform/powermac/pci_backend.c` (`C_PLATFORM_EXEC`) |
| PPC kboot-core / link | EXEC=0 | × | × (`pm_pci_boot` の空の対を `C_PLATFORM_NOEXEC` に。既存の `pci_config.c` は CORE のまま) |

`pci.c`・`pci_bind.c` が今頼っている物と切り方:

| 頼り先 | 所 | 切り方 |
|---|---|---|
| `pci_config_contract.h` | `pci.c` | そのまま (口を 3 本足す、§3) |
| `kprintf.h`・`ATTR_*`・`KLOG_ATTR` | `pci.c`、`pci_bind.c` | 共通 (`sys/kern/kprintf.c` は CORE)。切らない |
| `io.h` の `irq_save/irq_restore` | `pci_barsize.c` | `sys/arch/ppc/arch_io.h` に実装が在る。切らない (PowerPC では測定を呼ばないので走らない) |
| `os32_kapi_shared.h` (`OS32_ERR_*`) | `pci.h` | 共通。切らない |
| 線の様子の hook | `pci_bind_match.c` | hook が無ければ `PCI_LINE_OK`。PowerPC は登録しない |
| `-Idrivers/pci` | `pc98-devices.mk` の `INCLUDE_FEATURES` | 本体は同じ場所のヘッダを `"..."` で読むので要らない。共通の転送は `include/` だけ読む |
| mech1 の番地 (`PCI_CFG_ADDR_PORT` 等) | `pci_decode.h` | 本体は使わない (使うのは `sys/platform/pc98/pci_config.c` と `pci_hwb.c`)。定数の移動は別段 |
| PC-98 の物 | — | **本体 5 ファイルに PC-98 のヘッダは無い** (`pci.h` 冒頭の説明文だけが PC-98 と mech#1 を名指し → 文言を直す) |

x86 に残す物: `pci_hwb.c` (mech1 直叩き)・`pci_locate.c` (部品側)・`sys/platform/pc98/pci_resources.c` (台帳への取り込み)。

## 3. 口の形

`sys/include/pci_config_contract.h` に足す 3 本と、既存の説明の直し:

```c
/* 後端の名前。起動行に出す NUL 終端の定数文字列 (空・NULL は不可)。 */
const char *pci_config_name(void);
/* 走査の起点。idx = 0..PCI_CONFIG_MAX_ROOTS-1 の順に root の bus 番号を *bus へ。
 * 0 = 在る / -1 = もう無い (規定外の idx も -1)。probe が成功した後だけ呼ぶ。 */
#define PCI_CONFIG_MAX_ROOTS 4
int pci_config_root_bus(u32 idx, u32 *bus);
/* 1 = 列挙の時に BAR を測ってよい (全 1 を書いて戻す) / 0 = 設定空間は読むだけ。 */
int pci_config_bar_measure(void);
/* read32: reg は 0..255。後端が DWORD 境界に揃える (呼び手は 0x0e・0x19 等をそのまま渡す)。
 * write16: **Command (04h) 専用**。今の呼び手は全部 Command。ほかの reg の結果は後端次第。 */
```

`sys/dev/pci/pci.c` の変更 (4 か所):

```c
if (pci_config_bar_measure())                                   /* :85 */
    pci_barsize_record(bus, dev, fn, pci_cfg_read8(bus, dev, fn, PCI_CFG_HEADER_TYPE));
kprintf(ATTR_CYAN,  "[pci] %s absent\n", pci_config_name());    /* :188 */
kprintf(ATTR_GREEN, "[pci] %s ok\n",     pci_config_name());    /* :192 */
for (i = 0; i < PCI_CONFIG_MAX_ROOTS && pci_config_root_bus(i, &bus) == 0; i++)
    pci_scan_bus(bus, 0);                                       /* :194 */
```

| 後端 | name | root_bus | bar_measure | 取引 |
|---|---|---|---|---|
| PC-98 `sys/platform/pc98/pci_config.c` | `"mech#1"` | idx 0 → bus 0、ほかは -1 | 1 | 今のまま |
| PowerMac `sys/platform/powermac/pci_backend.c` (新) | `"uni-north"` | idx 0 → 既定ドメインの `bus_first`、ほかは -1 | 0 | 下の規則で `pm_pci_read32/write32` を呼ぶ (取引は写さない) |

- **x86 の表示と測定は変わらない** (`"mech#1"` が後端から来る、`bar_measure` は 1)。NP21/W の受入の台本の文字列も変えない。
- PowerPC の後端の入力: 機種の口 `void pm_pci_boot(const struct pm_pci *window, u32 probe_bdf);` (宣言は `sys/platform/powermac/lan_boot.h`)。`ppc_kernel.c` の `if (dev->gmac_ready)` の中、early quiesce の直後に、同じ `pci` (`pci_addr_va`・`pci_data_va`・`lan_boot.bus_first/bus_last`) と `lan_boot.bdf` を渡して呼ぶ。後端は static に 1 組だけ写す。EXEC の無い像は `C_PLATFORM_NOEXEC` の空の `pm_pci_boot` (既存の `host_unavailable.c` と同じ作法)。
- 窓なし (`gmac_ready == 0`: GMAC の BAR か 3 つの alias のどれかが立たなかった、`kernel_boot.c`) → `pm_pci_boot` が呼ばれない → `pci_config_probe()` は 0 → `[pci] uni-north absent`、台数 0 (PM 決定 U1)。
- probe: 窓が在り、`probe_bdf` の ID の読みが成功して `0xFFFFFFFF` でないこと。
- BDF の組み立てと範囲: `bus > 255 || dev > 31 || fn > 7 || reg > 255` は読みが `U32_MAX`・書きは何もしない。`bdf = bus<<16 | dev<<11 | fn<<8`、`p.target_bdf = bdf`、`pm_pci_read32(&p, bdf, reg & ~3u, &v)`。`pm_pci_read32` の失敗 (`bus_first..bus_last` の外、root bus の dev < 11、`pci_config.c`) は `U32_MAX` (不在と同じ)。走査の `sec > bus` の規則と合わせて必ず終わる。
- write32: `reg & 3` は何もしない。ほかは `pm_pci_write32` を呼び、失敗は捨てる (契約が void。共通の列挙は PowerPC では書かないので、この段に呼び手は無い)。
- write16: `reg == 4` だけ `pm_pci_write32(…, 4, value)` (既存の取引が上位 16 ビットを 0 にして W1C の Status を守る、`pci_config.c`)。ほかの reg は書かない。
- バイト順: `pm_pci_read32` が既に LE → CPU の値に直している (`pci_config.c`)。後端で二重に swap しない。
- ドメイン: 口に出るのは既定 (GMAC の root) の 1 つだけ。KAPI の bus 番号はその中の実番号 (u8)。`struct pci_dev` 40 B・`pci_bind_info` 8 B は不変。
- BAR (PM 決定): PowerPC は `pci_barsize_record` を呼ばない → `d->bar[]` は読んだ生値、`pci_bar_get` は記録なしで失敗 (fail closed、利用者は x86 の部品だけ)。`pci_barsize.c` は共通の本体に残る (PowerPC では走らない符号、§5)。
- 起動の呼び出し (PowerPC、段 P2): `pm_pci_boot` の中では走査しない。`pci_init()` → `pci_bind_all(NULL, 0)` は kprintf が使える所以降・`pm_lan_start` (`ppc_kernel.c`) の前に、EXEC 側の関数 1 本 (`pm_pci_enumerate()`、NOEXEC は空) で呼ぶ。読むだけなので GMAC・USB の動作中でも順序の制約は無い。位置は実装者が file:line で報告する。
- 台帳 (PM 決定 U2): `pci_bind_all(NULL, 0)` で全台 `PCI_BIND_NONE` / `PCI_BIND_NO_DRIVER`。`pci_bind_info` は列挙と同じ idx で成功する。
- 別の root の USB (PM 決定 U1): 列挙の外。USB 有効の像では、OHCI が実際に見つかった root (`usb_boot.c` の `pm_usb_root->pa`) が `gmac.root_pa` と違う時に `[pci] usb root outside default domain` を 1 度だけ出す。比べるのは USB の側 (`usb_boot.c`、`C_PLATFORM_USB`) で、後端は USB のシンボルを参照しない。
- シェルの文言: `userland/shell/cmd_pci.c` を `"lspci: no PCI (" SH_PCI_ABSENT_NOTE ")\n"` の形にし、既定を `"mechanism #1 not present"` (x86 は 1 バイトも変わらない)、`userland/ppc/sh/profile.h` で `"no configuration access"` にする。
- `irq_line` (0x3C) は PowerMac では割り込みの番号として意味を持たない (OF の木が正)。生値をそのまま記録し、解釈しない。

## 4. 転送 4 本と診断の置き場

| 物 | 今 | 先 | 読むヘッダ |
|---|---|---|---|
| `platform_pci_count/get/cfg_read32/bind_info_get` | `platform/pc98/device_pci.c` | **`sys/dev/pci/device_pci.c`** (新、純移動) | `platform_device.h`・`pci_contract.h` (新)・`pci_bind_contract.h` |
| `kapi_pci_bind_info` | `sys/kapi/kapi_sys.c` | 同じ `sys/dev/pci/device_pci.c` (純移動、説明文ごと) | `user_read_contract.h`・`sys/kern/exec/exec.h` (`ring3_user_ranges_writable`・`ring3_fault_kill`) |

- 共通が装置の宣言を読むための契約 (check_layers R3): 新 `sys/include/pci_contract.h` に `pci_count`・`pci_get`・`pci_cfg_read32`・`pci_init` を、既存 `sys/include/pci_bind_contract.h` に `pci_bind_info_get`・`pci_bind_all` を移す。`sys/dev/pci/pci.h`・`pci_bind.h` は契約を include し、同じ宣言を消す (宣言は 1 か所)。`pci_find`・`pci_bar_get`・`pci_cfg_write*` は `pci.h` に残す。
- **import の定義 (M2 に含める)**: 生成器 (`tools/gen_hwdev_contract.py`) は `.def` に書いた出所のヘッダを直に読むので、宣言を移すと旧ヘッダの指定で失敗する。`sys/kern/module/kdebug_imports.def` の `pci_get`・`pci_cfg_read32` (:1939 付近) と `sys/dev/hwdev/hwdev_imports.def` の `pci_cfg_read32` (:222 付近) の出所を `sys/include/pci_contract.h` に直し、**スロットの番号と型を保って**再生成する。生成物の差は出所の欄だけであることを M2 の完了条件にする。`pci_find`・`pci_bar_get`・`pci_hwb_*` の定義は触らない。
- PowerPC の結線 (段 P3): `sdk/kapi_target_ppc.py` の `PROVIDER_GROUPS['pci']` (:94-99) と `ZERO_SLOTS` の `pci_count` (:206) を外し、同時に `PROVIDER_EXTERNS` (:6-11) へ `extern int kapi_pci_bind_info(u32 idx, void *out);` を足す。`sys/arch/ppc/exec_kapi.c` は手で触らない。`$(KB_GEN)/stamp` の経路で生成し直し、EXEC の 3 像 (sh・link-sh・shell) を全部リンクする。
- 出力の保護 (`ring3_user_range_ok` は `sys/include/user_read_contract.h`、`ring3_user_ranges_writable`・`ring3_fault_kill` は `sys/kern/exec/exec.h`) は、40 B の `pci_get` と 8 B の `pci_bind_info` のそれぞれを PowerPC のゲストで確かめる (A9)。働かなければ P3 で止めて戻す。

## 5. 大きさ

実測 (main の木の x86 の `.o`、`size`):

| ファイル | text | bss |
|---|---|---|
| pci.o | 2,508 | 1,312 |
| pci_barsize.o | 2,141 | 8,608 |
| pci_bind.o | 719 | 288 |
| pci_bind_match.o | 634 | 5 |
| pci_decode.o | 860 | 0 |
| device_pci.o (転送) | 49 | 0 |

| 像 | 増える量 | 上限との関係 |
|---|---|---|
| x86 kernel (通常・r1・FD) | text **+約 0.15 KB** (口 3 本・ループ・測定の分岐)。診断は kapi_sys.o → device_pci.o へ移るだけで ±0 | FD・MINIMAL は通常の `vmkernel.lz4` を梱包するので同じ量だけ動く。**FD の空きは M1 と着地 L の後に PM が実測して報告に載せる** (上限値は今回読んでいない) |
| x86 kdebug (上限 262,144 B、`docs/08_build.md`) | 0 (`pci_hwb.c` と import は不変) | 影響なし |
| PPC sh / link-sh / shell | text 約 +9 KB (x86 の 6.9 KB を PowerPC の符号密度で 1.3 倍と置いた**見積り**) + 後端 約 0.5 KB、bss +10.2 KB。うち `pci_barsize` の text 約 2.8 KB・bss 8.6 KB は PowerPC では走らない (測定を別の選択に分けるのは、x86 と共通の集合を保つために今回はしない) | 実測は PowerPC の段で `size` を報告に載せる |
| PPC core / link | 0 | — |

## 6. ファイルごとの変更と段

1 ファイル = 1 段を基本に、同じ列の中は直列、列どうしは並列。

| 段 | 列 | ファイル | 中身 | x86 の build_compare |
|---|---|---|---|---|
| M1 | 本線 | `sys/include/pci_config_contract.h`・`sys/platform/pc98/pci_config.c`・`sys/dev/pci/pci.c`・`sys/dev/pci/pci.h` (説明文)・`tools/tests/pci_scan_host.c`・`tools/tests/test_pci_scan.py` | 口 3 本、PC-98 の後端 3 関数、`pci.c` の 4 か所。口と両側が 1 段でないと組めない。`pci.c` に条件が付くので、`test_pci_scan.py` の変異のアンカー (`record_missing`・`record_after_full` の旧い行) を新しい行に合わせ直す (変異の意味は変えない) | `pci.o`・`pci_config.o` だけ差。起動行の文字列は同じ |
| M2 | 本線 | `sys/include/pci_contract.h` (新)・`sys/include/pci_bind_contract.h`・`sys/dev/pci/pci.h`・`pci_bind.h`・`sys/kern/module/kdebug_imports.def`・`sys/dev/hwdev/hwdev_imports.def` と再生成した `kdebug/generated/`・`hwdev/generated/` | 宣言の移動と import の出所の更新 (§4)。スロット・型は不変 | `.o` の差 0。生成物は出所の欄だけ |
| P1 | PowerPC | `sys/platform/powermac/pci_backend.c` (新)・`lan_boot.h` (宣言)・NOEXEC の空の対・偽 MMIO の host 試験 | 契約 7 本と `pm_pci_boot`・`pm_pci_enumerate`。**選ぶ行は入れない** (host 試験だけで完了) | 差なし |
| M3 | 本線 | `sys/dev/pci/device_pci.c` (新)・`platform/pc98/device_pci.c` (消す)・`sys/kapi/kapi_sys.c` (診断を抜く)・`build/platform/pc98.mk`・`build/sources/common.mk`・`build/sources/order.mk`・`sys/conf/features/pc98-devices.mk`・`tools/tests/test_dep_dir_a_l08.py` と `dep_dir_a_l08_host.c` | 純移動 + 選ぶ行の移動 (`C_COMMON_EXEC` に本体 5 + 転送)。`order.mk` は旧転送の位置を新しいパスに差し替える。L08 は入力 (`COPIES` に新パスと `pci_contract.h`)・診断の抽出先・既存の変異 (`reg ^ 4u`) を新パスへ | kernel は同じ並び。差が出たら配置の差だけと報告に書く |
| **L** | PM | M3 + P1 の選択 (`build/platform/powermac.mk` の `C_PLATFORM_EXEC`・`C_PLATFORM_NOEXEC`)・`build_profile_x86.json`・`build_profile_ppc.json` | **同じ着地**。M3 だけでは PowerPC の EXEC の像に本体が入って後端が無く、リンクできない。この時点で PowerPC は誰も `pci_init` を呼ばず、スロットは不在のまま | x86 は M3 と同じ |
| P2 | PowerPC | `sys/platform/powermac/ppc_kernel.c` | `pm_pci_boot` (:221 の直後) と `pm_pci_enumerate` の呼び出し。**完了条件: 6d-ii の BDF と項目順を QEMU の起動ログで確かめる** (GMAC の BDF が OF の値と同じ、idx の順は**既存の深さ優先** — ブリッヂを記録した直後に配下へ潜る (`pci.c`)。例 `00:0b.0 → 01:00.0 → 00:0c.0`。昇順ではない) | 差なし |
| P3 | PowerPC | `sdk/kapi_target_ppc.py`・`tools/ppc/test_kapi_binding.py` (:44-45 の不在の期待と、:318 付近の未定義シンボルの固定の許可表) | 不在の組と `ZERO_SLOTS` から外し extern を足す (§4)。許可表に新しい提供者 4 本 (`platform_pci_count`・`platform_pci_get`・`platform_pci_cfg_read32`・`kapi_pci_bind_info`) を足す。P2 の後 (初期化前の空の列挙を公開しない) | 差なし |
| P4 | PowerPC + 本線 (cmd_pci.c) | `userland/shell/cmd_pci.c` (文言のマクロ)・`userland/ppc/sh/profile.h` (`SH_REGISTER_COMMANDS` に pci、文言)・`build/ppc-user.mk` (`sys/dev/pci/pci_decode.c` のユーザー側の `.o`、`programs.mk` と同じ形)・ゲストの探針 | PPC のシェルに `lspci`・`pcidump`、KAPI の照合の探針 | `cmd_pci.o` の差 0 (文字列の連結の結果が同じ) |
| P5 | PowerPC | `sys/platform/powermac/usb_boot.c` | 別 root の USB の診断 1 行 (§3) | 差なし |

- 順: M1 → (M2 ∥ P1) → M3 → L → P2 → P3 → P4。P5 は L の後ならいつでも。M1 だけが「その場の変更」で NP21/W の受入が要る。
- 動かさない物: `pci_hwb.c` (E7)、`pci_locate.c`、`pci_resources.c`、E8 の 17 ファイル、`pci_decode.h` の mech1 の定数、PowerPC の `pci_config.c` (GMAC の quiesce との同居の解消は別の段)。
- 文書: `docs/KAPI_SPEC.md` は文字列が変わらないので直さない。`DEFERRED_TESTS.md` の PCI-MECH1-REALHW は M1 の取り込みで PM が 1 文足す (実機で確かめる物に「起点と名前が後端から来る形での台数・BDF・bind の行の一致」)。確定設計の行 191・192 に「続きは本票」を書き戻す (PM)。

## 7. 受入 → 試験と変異

| 受入 | 試験 (場所) | 変異 (1 行) |
|---|---|---|
| A1 x86 の起動行が今と同じ | host: `pci-scan-host`。`tools/tests/pci_scan_host.c` の kprintf は今は出力を捨てるので、**書式を展開して溜める物に替え**、不在の時 `"[pci] mech#1 absent\n"`、在る時 `"[pci] mech#1 ok\n"` をそれぞれ照合 | PC-98 の `pci_config_name` の戻り値を `"mech1"` に |
| A2 起点が後端から来る | host: `pci_scan_host.c` 付近の偽のポートは bus 0 専用なので、**bus を持つ形に広げる** (難しければ偽の後端を直に持つ別のハーネス)。root を {0}、{2}、{0,3} と変え、記録の bus と件数を照合 | `pci_init` のループを `pci_scan_bus(0, 0)` に戻す |
| A3 起点が尽きたら止まる | host: PC-98 の後端を直に呼び、`root_bus(0)` が 0 と bus 0、`root_bus(1)`・`root_bus(PCI_CONFIG_MAX_ROOTS)` が -1。共通側は上限つきのループなので、壊れた後端でも止まる | PC-98 の `root_bus` が idx を見ずに 0 を返す (直の照合が即座に落ちる。無限ループにならない) |
| A3b x86 は測定を続ける | host: 既存の `pci-scan-host` の測定の照合 (`record_missing` 等) がそのまま通る | PC-98 の `pci_config_bar_measure` を 0 に |
| A4 x86 の不在の経路 | NP21/W: 起動ログに `[pci] mech#1 absent`、`lspci` が `no PCI (mechanism #1 not present)`、`pci_bind_info(0)` が失敗。**NP21/W は PCI を持たないので通るのはここまで** | (ゲストは変異なし。host の A1 が代わり) |
| A5 x86 の在る経路 | 実機 Ra266: `DEFERRED_TESTS.md` の PCI-MECH1-REALHW に持ち越す (台数・BDF・BAR の測定・bind の行が更新前と同じ) | — (延期) |
| A6 転送と診断が 1 か所 | host: L08 (`test_dep_dir_a_l08.py`) を新しいパスで — 40 B・8 B の写し、戻り値、失敗の時に出力が変わらないこと。層の検査が通る。両 arch の EXEC の像が二重定義なしでリンクできる | 既存の `reg ^ 4u` を `sys/dev/pci/device_pci.c` に掛ける / `kapi_pci_bind_info` の写しを 7 バイトに |
| A7 PowerPC の後端の番地とレーン | host (偽 MMIO、`tools/ppc/test_lan_p8_fake.c` と同じ型): 番地の語 (root bus は `1<<dev`、配下は type 1)、`reg` 0x0e・0x19・0x3d の読みが揃えた DWORD から正しいバイトを返す、多機能・ブリッヂ配下・**ブリッヂの後ろに続く root bus 上の装置**を含む偽の木で、列挙の台数と BDF を深さ優先の期待列 (`00:0b.0 → 01:00.0 → 00:0c.0` の形) と照合 | 後端の `reg & ~3u` を外す (0x0e が `U32_MAX` になり多機能と配下を見落とす) |
| A7b バイト順 | host: 同じ偽 MMIO で vendor:device の値 | 後端で値を `__builtin_bswap32` にもう 1 回通す (確定設計 :262 の変異) |
| A8 PowerPC は 1 ドメインだけ | host: `root_bus(1)` が -1、`bus_last` 越え・root bus の dev < 11 の読みが `U32_MAX` で MMIO に触らない、窓なしの probe が 0。**後段 (`pci_config.c`) が切り詰めて通してしまう入力**で後端の確かめを試す: `dev=32` (`<<11` で隣の bus の dev 0 に化ける)・`fn=8` (`<<8` で隣の dev の fn 0 に化ける) が `U32_MAX` で MMIO に触らない。`bus=256` も同じ結果 (これは後段の `bdf & 0xff0000ff` の確かめ (`pci_config.c`) でも落ちるので、検出は dev・fn の 2 つが持つ) | `dev > 31`・`fn > 7` の確かめを外す (`dev=32` が bus+1 の装置を返す) |
| A8b PowerPC は書かない | host: 偽 MMIO で列挙 (`pci_init` + `pci_bind_all(NULL,0)`) の間のデータ側への書きが **0 回**。`write16(reg=6)` (後段でも拒否される) に加え、**揃った `write16(reg=8)`** (後段は通す) でも書きが 0 回。`write16(reg=4)` は上位 16 ビットが 0 | PowerPC の `pci_config_bar_measure` を 1 に / `reg == 4` の確かめを外す (`reg=8` の照合が落ちる) |
| A9 PowerPC の列挙と KAPI | QEMU PowerMac (mac99)、専用の探針: 起動ログに `[pci] uni-north ok` と台数。各 idx で `pci_get` の 40 B の vendor:device が同じ BDF の `pci_cfg_read32(…, 0)` と一致、`pci_bind_info` が同じ idx で成功し NONE / NO_DRIVER、範囲外の idx は失敗して出力が変わらない、GMAC の BDF が載る。出力の保護 (書けない番地) を 40 B・8 B それぞれで。`lspci`・`lspci -v`・`pcidump` が動く | `pm_pci_enumerate` の `pci_bind_all(NULL,0)` を外す (bind_info が失敗) |
| A10 走査の後も装置が生きている | QEMU: A9 の後に LAN の往復と USB キーボードの入力が通る (読むだけなので確かめの位置づけ。検出は A8b が持つ) | — |
| A11 不在表と結線 | host: `ppc-kapi-binding` の不在の期待から pci の 4 本が消え、未定義シンボルの許可表に提供者 4 本が在り、生成物がそれぞれの提供者を呼ぶ。EXEC の 3 像がリンクできる | **生成物が組める状態で**提供者を欠かす: `PROVIDER_GROUPS['pci']` から `pci_get` だけを外し忘れた形 (1 本が不在の答えのまま) に戻す → 期待の集合との差で落ちる |
| A12 別 root の診断 | host: `usb_boot` の比較の関数を root が同じ・違うの 2 通りで。違う時 1 度だけ | 比較を常に偽に |

## 8. 未決 (ユーザー判断)

なし。r1 の U1・U2 は PM 決定で閉じた (U1 = GMAC の root・窓なしは不在・別 root の USB は列挙の外で診断 1 行、U2 = 全台 NONE / NO_DRIVER)。

## 反論

なし (REVIEW.r1 の 14 件は全部入れた)。

## 確定 (2026-11-19、sol の第 2 回の条件付き承認、5 点を反映)

1. M2 に import の定義 (`kdebug_imports.def`・`hwdev_imports.def`) の出所の更新と再生成を含めた (スロット・型は不変、§4・§6)。
2. P2 の項目順を深さ優先の期待列にし、ブリッヂの後ろの root bus 上の装置を含む偽の木を A7 に足した (§6・§7)。
3. P3 に未定義シンボルの許可表の 4 本を足し、A11 の変異を「組める状態で提供者を欠かす」形に替えた (§6・§7)。
4. M1 に `test_pci_scan.py` のアンカーの合わせ直しを入れ、A2 (bus を持つ偽のポート)・A8 (`dev=32`・`fn=8` の化ける入力、`bus=256`)・A8b (`reg=8`) の検出条件を直した (§6・§7)。
5. E8 を直に include する 17 ファイルに訂正し、集計の範囲を書いた (§1)。
