> 出所: ~/os32-tmp/run/sysinfo-rest/DESIGN.final.md (Fable 起草、sol レビュー 2 回、2026-11-19 確定)。
> 状態: **確定 (2026-11-19)** — wip の置き場 (PowerPC の P 段の参照用)。main に入れる時に file:line を「パス + 関数名」に直す。

# KAPI の埋め 段 2 の残り: mem_stat (#29)・sys_ver_info (#30) を両機種で出す — 設計の追記 r2

基点: origin/main fb27aa1d8 (読んだのは `git show origin/main:<path>`。手元の作業木の HEAD は 05160fa9b で別)。親の設計: docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md §3 (2a・2b・2c)。
読んでいない物: build_profile_*.json の中身の全体、QEMU の受入の手順、PC-98 の像ごとの今の大きさ (§4 は見積り)。

### r1→r2 の変更 (sol のレビュー第 1 回と PM 決定 U1〜U3)

- P1 kstack: 「x86 は今と同じ命令列」の断定を外した。S1・S2 の受入に大きさ 132・133・220 の戻り値・書く範囲・2 本のスタックの観測値の比べを足した (§2・§5・§6)。
- P1 Image CRC: 共通から外し後端の `image` の口へ。PowerMac は検証済みの時だけ "Root image CRC" の行、PC-98 は文言と条件をそのまま (§3-1・§3-3)。
- P1 PVR・DEC・calib: PVR は起動時に保存した値と有効の印を読む。DEC は "(configured)" と明示。calib は PowerMac では出さない (§3-1・§3-3)。
- P1 公開の slot: P1・P2 の完了条件を「sdk/kapi_target_ppc.py の欠けの集合から外す → 再生成 → 実の wrapper の結線の検査」に書き直した (§5・§6)。
- P2 U1 の出所: 「CPU の口」は PowerPC の PM の #29 の提案で親設計 §3 にもユーザーの指示にも無い、と §1-3 に書き、PM 決定で確定にした (§7)。
- P2 kdebug の行数と devices の移動元: 「状態の行 + 条件付きの詳細 1 行」に、移動元を ver_format.c:90-94 と :101-152 に直した (§1-2・§3-1・§3-3)。
- P2 PowerMac の行の表: 行ごとの文字列・kind・出す条件・順を §3-4 に新設。SER は baud だけ (mode・FIFO は固定値なので出さない)。
- P2 host の試験の追従: 抽出の範囲・変異の目印・covers・SYS_VERSION の検査の対象を、動かす段と同じ段で直す (§5 の「連れ」)。
- P2 syscall の印: P1 の先の確かめを外し (静的に確認済み)、受入を実アプリの照会に充てた (§6)。
- P2 NP21/W の比べ方と層の変異: 全行の diff 0 をやめ、host は OS32_VerInfo 全体のバイト比較、ゲストは固定の文言・行の順と可変値の形。層の変異は実の装置のヘッダ。段ごとの差の許容と大きさの上限を §4・§5 に足した。
- U1〜U3: PM 決定として §7 を「決定済み」に書き換えた。未決は 0。

## 1. 実物の確認

### 1-1. 依頼 2 件の現物

| 依頼 | 現物 | PowerPC で欠ける物 |
|---|---|---|
| #29 mem_stat | kapi/kapi_sys.c:44-149 `kapi_mem_stat`。kapi_sys.c は build/sources/common.mk:10 の C_COMMON_PC98_PENDING | kstack_high_water (kernel/kstack_hw.c、build/arch/x86-sources.mk)、memmap/as/v86_return の audit (kernel/kaudit.c:12-13、PENDING)、boot_pages_audit (init/kaudit_boot.c:9、PENDING)、kselftest_pass/fail (kernel/kselftest_report.c:6-7、PENDING)、`kdebug` (kdebug/kdebug_module.c:16、features/kdebug.mk) |
| #30 sys_ver_info | kapi/kapi_sys.c:152-161 `kapi_sys_ver_info` → kernel/ver_collect.c:17-44 → kernel/ver_format.c:87-206。2 本とも common.mk:10 の PENDING | 下の 1-2 の「機種」の行、ver_format.c:184 の kdebug_state_name、ver_format.c の PC-98 の文言 |

mem_stat のそのほかの読み先 (kmalloc_*、pgalloc_*、ledger_*、exec_heap_*、resident_heap_*、appslot_*、ring3_wm_depth・ring3_in_syscall、caller_access_get_user、exec_as_leftover_pages = exec/exec_finish.inc:20、fault_kill_count) は C_COMMON_CORE / C_COMMON_EXEC にあり PowerPC の像で解決する。

### 1-2. ver_collect が読む値 (kernel/ver_collect.c)

| 行 | VerSnapshot の欄 | 出所 | 機種依存 | PowerPC の像 |
|---|---|---|---|---|
| :22-26 | fpu_present | x86: arch_fp_present (arch/x86/kernel/x87.c:61)。PowerPC: `#ifdef __powerpc__` で固定の 1 | CPU | 実測でない固定値 (食い違い 3) |
| :27 | cpu_measured | cpu_calibrate_used_fallback (kernel/time/cpu_calibrate.c:120) | 共通 | 在る (ppc-sources.mk の C_ARCH_EXEC) |
| :28-34 | timer_kind・pit_valid・pit_hz・pit_input・irq_master/slave_vector | platform_boot_diag (platform/pc98/boot_diag.c:4、8254 と 8259A の値) | 機種 | 無い |
| :35 | ser_ready・ser_mode・ser_baud・ser_fifo | port_get_status (kernel/port_service.c:56) | 共通の口 (中身は装置) | 口は在る |
| :37 | pcm_present | pcm_present (drivers/sound/cs4231/pcm_module.c:71) | 装置 | 無い |
| :38 | gfx・gfx_kind・gfx_active | gfx_ver_state (gfx/gfx_core_api.inc:6)。kind は display_provider_ver_kind | 共通 | 在る (kind は GFX_VER_UNKNOWN、platform/powermac/display_provider.c:120) |
| :39 | api_version | KAPI_VERSION | 共通 | 在る |
| :40 | boot_ok・boot | boot_image_info (init/bootinfo.c:20) | 共通 | 在る |
| :41 | debug_ok・debug | kapi_mem_stat(0, …) | 共通 (#29 の後) | #29 で解決 |
| :42 | services | services_ver_state (kernel/sqlite_engine.c:577) | 共通の名前だが PowerPC は compile-only (common.mk:11) | リンクされない |
| kapi_sys.c:158 | build | kapi_sys_build_time_copy (kapi/kapi_sysinfo.c:59) | 共通 | 在る |

ver_format の側: 頭行 "PC-9821 OS32 v… (Ring3 Native)"・"x86 32-bit"・"8259A x2"・"8254"・"8251-compatible; IRQ"・"8251 baud="・"OPN configured; PCM=CS4231-family"・GFX の名前の表 (98/PEGC/Cirrus/GeForce/Trident) が ver_format.c:90-152 に固定で書いてある。API・Build (:153-158) は共通。Commit (:159-162) は共通、Image CRC (:163-172) は "FD/HDD loader" の文言を持つので機種。kdebug は状態の 1 行 (:174-186) と条件付きの詳細 1 行 (RUNNING なら "kdebug pages"、OFF 以外なら "kdebug file pair"、:187-196)、sqlite・services の 2 行 (:197-205) は PC-98 の像の部品。

### 1-3. 依頼・前の段の記録との食い違い (5 件)

1. **pd_phys は CPU の口に分ける理由が実物に無い**。「CPU の口に分ける」の出所は PowerPC の PM の依頼 #29 の提案で、親設計 §3 の 2a〜2c (PPC_KAPI_FILL_DESIGN.md:171-174) にもユーザーの指示にも無い。`pd_phys` の 0 は共通の構造体の無効値 (mm/paging.h:209)。`struct addrspace` は共通の mm/paging.h:207-226、mm/paging.c は C_COMMON_CORE、PowerPC は同じ PD を arch/ppc/pd_walk.c で歩く。共通の exec/appslot.c:162・exec/lease.c:142,162・mm/paging_app.c:13 が今も PowerPC の像で `as->pd_phys` を直に読んでいる。
2. **mem_stat が PowerPC に入らない本当の理由は診断の変数** (1-1 の右の欄)。依頼は pd_phys だけを挙げている。
3. **ver_collect.c:3-5・:22-26 に既に `#ifdef __powerpc__` と `fpu_present = 1` の固定値がある**。PENDING のファイルなのに PowerPC の分岐があり、値は実測でない (PowerPC の像は -msoft-float)。
4. **2b の本文 (1f4048a34) の欠けの一覧は ver_collect と ver_format を分けていない・漏れがある**。kdebug_state_name は ver_format.c:184 の側。ver_format の PC-98 の文言 (1-2) は一覧に無く、「共通へ移すだけ」だと PowerMac の `ver` に 8259A・8254・8251・OPN が出る。
5. **services_ver_state は「提供元が無い」のではなく、提供元 (kernel/sqlite_engine.c) が PowerPC で compile-only**。SQLite の段 (wt/ppc-sqlite-stack) がリンクに入れれば共通で出せる欄になる。

### 1-4. 統合の判断 3 点 (POLICY_DEV §1)

| 点 | 判断 |
|---|---|
| 既存の仕組みに乗せられるか | mem_stat の診断は 2a-i の `platform_build_info.h` と同じ型 (固定名 platform_*.h の inline)。本体の置き場は 2a-ii・2b の kapi/kapi_sysinfo.c (system_info の組)。新しい TU は作らない |
| 同じ物を 2 つ作らないか | pd_phys の判定は今の `as->pd_phys` の 1 通りのまま (口を足すと共通の 4 か所と 2 通りになる)。行を組む補助 (ver_line・ver_text・ver_number・ver_end) は ver_format.c の 1 組を公開し、機種の後端に写しを作らない |
| 消える物・残る物 | ver_collect.c の `#ifdef __powerpc__` と `extern kapi_mem_stat` を消す。kapi_sys.c は kapi_pci_bind_info だけが残る (PENDING のまま。名前と中身のずれは SRC_MOVE の別項目) |

## 2. mem_stat (#29)

- **本体の置き場**: kapi/kapi_sysinfo.c (C_COMMON_EXEC)。STATIC_ASSERT 10 本 (kapi_sys.c:31-40) も一緒に移す (MemStat は u32 だけで PowerPC ILP32 でも 220 バイト)。宣言は kapi/kapi_sysinfo.h に足し、ver_collect.c:15 の手書きの extern を消す。
- **アドレス空間の判定**: 口を作らず kapi_sys.c:75 の `!(as = slot->as) || !as->pd_phys` のまま移す (食い違い 1、PM 決定 U1)。
- **機種の診断の口** (固定名 platform_mem_stat.h。小さいので inline。呼ぶ位置は今と同じ — `_pre` は引数の検査の直後・irq_save の前、`_diag` は irq_save の中):
  ```c
  /* IF を落とす前。size は呼び手の要求の大きさ */
  static inline void platform_mem_stat_pre(MemStat *snap, u32 size);
  /* irq_save の中。出力・譲り・AS 切替をしない */
  static inline void platform_mem_stat_diag(MemStat *snap);
  ```
- **PC-98 の後端** (platform/pc98/platform_mem_stat.h): `_pre` は kapi_sys.c:54-55 (kstack_high_water)、`_diag` は :122-135 (audit の和、kselftest、kdebug の 5 欄)。extern 6 行 (kapi_sys.c:22-25) と kstack_hw.h・module.h の include もここへ。
- **PowerMac の後端** (platform/powermac/platform_mem_stat.h): 2 本とも空。`snap = {0}` のままなので kstack_high_water = 0、audit_runs/fail = 0、kselftest_pass/fail = 0、kdebug_state = KDEBUG_OFF (= 0、kernel/module/module.h:50)、kdebug_* = 0。どれも「その部品がこの像に無い・走っていない」の実際の値で、架空の PC-98 の値ではない。0 の extern は足さない (2a-i の決まり)。
- **kstack_high_water の観測値は段で動き得る**: kstack_high_water() は呼んだ時点のスタックを走査して山を覚える (kernel/kstack_hw.c:25-34)。inline 化 (S1) と TU の移動 (S2) で kapi_mem_stat のフレームの大きさが変わると、低 16 ビット・高 16 ビットの値が数十バイト動く。答えの意味 (山のバイト数) は同じなので振る舞いの変更ではないが、差は受入で測って記録し、kapi_mem_stat のフレームの差 (objdump の `sub esp`) で説明できない差は止める (§6)。
- **PowerMac の kstack_high_water = 0 は「未計測を含む」** (PM 決定 U3)。docs/design/kapi/kapi_sys.md の欄の説明に 1 行足す (S2 の連れ)。公開ヘッダは触らない。
- kaudit.c・kselftest_report.c が PENDING を出た時は、その 4 行を PC-98 の inline から共通の本体へ戻す (1 段)。

## 3. sys_ver_info (#30)

### 3-1. 欄ごとの表

| `ver` の行 | 出す所 | PC-98 | PowerMac |
|---|---|---|---|
| 頭行 | 後端の `head` (無ければ共通の "OS32 v" SYS_VERSION) | 今の文言 | §3-4 |
| CPU | 後端 | 今のまま (calib・FPU) | 起動時に保存した PVR。calib と FPU は出さない (PM 決定 U2。calib は cpu_calibrate を呼ぶ所が platform/powermac・arch/ppc に無く、未計測を "default" と書くと架空の値になる) |
| PIC / PIT | 後端 | 今のまま | 無い。代わりに MPIC の source 数 (pm_mpic_source_count、mpic.c:93) の行と、DEC の設定の周波数 (PM_DEC_HZ、arch/ppc/ppc_trap.h:18。測った値でないので "(configured)" を付ける) の行 |
| KBD | 後端 | 今のまま (input_irq) | 無い (8251 が無い。USB の行を足すかは PowerPC の段) |
| SER | 後端 | 今のまま | ser_ready の時だけ ESCC の baud。mode (固定の 0)・FIFO (固定の 1) は platform/powermac/log.c:101-106 の定数なので出さない |
| SND | 後端 | 今のまま | 無い |
| GFX | 後端 | 今のまま | 幅・高さ・bpp が 3 つとも 0 でない時だけ "OF-fb"。どれかが 0 なら無い |
| API・Build | 共通 | 同じ | 同じ |
| Commit | 共通 (boot_ok の時だけ、今の条件) | 同じ | 同じ |
| Image CRC | 後端の `image` (共通が boot_ok の時だけ呼ぶ) | 今の文言と条件 (:163-172、"FD/HDD/? loader"・"none (loader did not record)") | PowerMac の CRC はローダの記録でなく像に埋めたルートイメージの検証 (platform/powermac/root_image.c:13-22、source は BOOTINFO_SRC_NONE)。crc_valid の時だけ "Root image CRC" の行、そうでなければ無い。"? loader" は出さない |
| kdebug (状態 1 行 + 条件付きの詳細 1 行)・sqlite・services | 後端の `debug` | 今のまま | 無い (部品が像に無い。食い違い 5 が解けたら sqlite・services を足す) |

### 3-2. 「無い」の表し方

公開構造体 OS32_VerInfo (sdk/include/os32/os32_ver_info.h) は `count` と行の並び。**無い欄は行を出さない (count が小さくなる)**。シェルの `ver` (userland/shell/cmd_base.c:108) は count 個を kind で色分けして出すだけで、行の数にも表題にも依らない。"unknown" や 0 の行は足さない。KAPI の表・版・構造体の形は変えない。

### 3-3. 後端の口: ops の表 + 固定名 platform_ver.h で選ぶ

```c
/* include/ver_backend_contract.h (新) */
#include "ver_info.h"
typedef struct { OS32_VerLine *line; const char *label; u32 used; int overflow; } VerLineBuilder;
VerLineBuilder ver_line(OS32_VerInfo *out, u32 kind, const char *label);
void ver_text(VerLineBuilder *b, const char *text);
void ver_saved_text(VerLineBuilder *b, const char *text, u32 size);
void ver_number(VerLineBuilder *b, u32 value, int hex_digits);
void ver_end(VerLineBuilder *b);
struct ver_backend {
    const char *head;                                        /* 頭行の全文。NULL なら共通の文言 */
    void (*collect)(VerSnapshot *s);                         /* 保存済みの値を読むだけ。装置・SPR を読まない */
    void (*devices)(const VerSnapshot *s, OS32_VerInfo *out); /* DEVICE の行 */
    void (*image)(const VerSnapshot *s, OS32_VerInfo *out);   /* 像の CRC の行。共通が boot_ok の時だけ呼ぶ */
    void (*debug)(const VerSnapshot *s, OS32_VerInfo *out);   /* DEBUG の行 */
};
/* platform/pc98/platform_ver.h (新) */
extern const struct ver_backend pc98_ver_backend;
static inline const struct ver_backend *platform_ver_backend(void) { return &pc98_ver_backend; }
/* platform/powermac/platform_ver.h (新、本線の段では NULL を返す。PowerPC の段で &pm_ver_backend に) */
static inline const struct ver_backend *platform_ver_backend(void) { return 0; }
```

- 共通の ver_collect: kmemset → cpu_measured・ser・gfx・api・boot・debug → `if (b && b->collect) b->collect(s)`。ver_format: 頭行 → devices → API・Build → (boot_ok なら Commit → image) → debug。PC-98 の行の順は今と同じ。口が NULL の所は何も出さない。
- PC-98 の後端 (platform/pc98/ver_backend.c、新): collect = fpu_present・platform_boot_diag の 6 欄・pcm_present・services_ver_state (ver_collect.c:25-34,37,42)。devices = ver_format.c:90-94 (GFX の名前の前処理) と :101-152 (CPU〜GFX の末尾の "; active" / "; GUI target" まで)、image = :163-172、debug = :174-205 をそのまま移す。頭行の文言と VER_VERSION_MAX の STATIC_ASSERT (:20) も後端へ (共通の既定の文言にも同じ検査を残す)。
- VerSnapshot (kernel/ver_info.h、非公開) の末尾に `u32 plat[8];` を足す (後端だけが意味を決める。PowerMac は plat[0] = PVR、plat[1] = PVR の有効の印、plat[2] = MPIC の source 数を collect で入れ、devices で読む)。pit_*・irq_* の欄は PC-98 の後端だけが書いて読む。
- **ops にした理由**: (a) 機種の行は 80 行ほどの文字列の組み立てで、ヘッダの inline には大きく、ver_format.c の static に寄りかかる形になる。(b) 表が NULL なら共通の行だけが出るので、PowerMac の後端が無いあいだも架空の値が出ない作りになり、本線の段と PowerPC の段が切れる。(c) host の試験が偽の表を差せる。
- **登録の関数にしない理由**: 起動の順に 1 手足すと x86 の振る舞いの確認が増える。固定名のヘッダで const の表を選べば起動の列は変わらず、名前は機種ごとに別 (pc98_ver_backend / pm_ver_backend) で「x86 と同じ名前の新しい定義」にならない。
- mem_stat の側を inline にしたのは逆の理由 (代入 10 行で、呼び出しの段を増やさない)。
- **PowerMac の PVR の保存先** (P2): `ppc_cpu_identify()` は SPR を直に読む (arch/ppc/kernel_cpu.c:24-29) ので collect からは呼ばない。platform/powermac/kernel_boot.c:293 で identify が通った直後に `pm_ver_note_pvr(pvr)` (platform/powermac/ver_backend.c の static 2 語へ、値と有効の印) を 1 回呼ぶ。有効の印が 0 なら CPU の行を出さない。

### 3-4. PowerMac の行 (P2。この順、後端が NULL でない時)

| 順 | kind | 文字列 | 出す条件 | 値の出所 |
|---|---|---|---|---|
| 1 | OS32_VER_HEAD | `PowerMac OS32 v<SYS_VERSION>` | いつも | config.h |
| 2 | DEVICE+INDENT | `CPU: PowerPC PVR=<16 進 8 桁>` | plat[1] != 0 | 起動時の保存 |
| 3 | DEVICE+INDENT | `IRQ: MPIC sources=<10 進>` | plat[2] != 0 | pm_mpic_source_count |
| 4 | DEVICE+INDENT | `DEC: tick=1000Hz (configured)` | いつも | PM_DEC_HZ (定数。測った値ではない事を文言で示す) |
| 5 | DEVICE+INDENT | `SER: ESCC baud=<10 進>` | ser_ready != 0 | port_get_status (共通の collect) |
| 6 | DEVICE+INDENT | `GFX: OF-fb <W>x<H>x<bpp> bpp; active` または `…; GUI target` | 幅・高さ・bpp が 0 でない | gfx_ver_state (共通の collect) |
| 7 | IDENTITY+INDENT | `API: v<10 進>` | いつも (共通) | KAPI_VERSION |
| 8 | IDENTITY+INDENT | `Build: <日時>` | いつも (共通) | kapi_sys_build_time_copy |
| 9 | IDENTITY+INDENT | `Commit: <文字列>` | boot_ok (共通) | boot_image_info |
| 10 | IDENTITY+INDENT | `Root image CRC: <16 進 8 桁> (<10 進> bytes)` | boot_ok かつ crc_valid | boot_image_info |

- KBD・SND・FPU・calib・kdebug・sqlite・services の行は無い。行の数は 4〜10。頭行の機種名の最終の文言だけは PowerPC の PM が P2 で直してよい (ほかの行の形と条件はこの表で確定)。

## 4. 大きさ

| 像 | 増え方 (見積り) | 理由 |
|---|---|---|
| x86 kernel (通常・R1・起動 FD・MINIMAL。4 つとも同じ TU を選ぶ) | +0.3〜0.6 KB | 行の補助 5 本が static から外部関数になり畳み込みが外れる、表 16 バイト、NULL の分岐、VerSnapshot +32 バイト (スタック) |
| kdebug.bin・.mod | 本体 0。対の CRC の欄だけ | kernel の CRC が変わるため (2b と同じ) |
| PowerPC の exec の像 (shell・sh・link-sh) | S2 で +0.8 KB、S5 で +1.5 KB、P2 で +1 KB 前後 | mem_stat、ver_collect + ver_format の共通の部分、PowerMac の後端 |

上限と段ごとの許容:

| 物 | 上限 (出所) | 段ごとの増分の許容 |
|---|---|---|
| x86 kernel の像 | 508 KB = `KERNEL_MAX_BYTES` (tools/nhd_deploy.py:841)。圧縮の像は tools/mkvmkernel.py:276 の検査 ("vmkernel.lz4 too large") | S1・S2・S5 は ±128 B、S3 は +256 B、S4 は +512 B。4 つの像 (通常・R1・起動 FD・MINIMAL) それぞれで測る |
| kdebug.bin | image 262,144 (docs/08_build.md「起動時に読む部品の大きさの上限と由来」の表) | 全段 0 (CRC の欄だけ) |
| PowerPC の exec の 3 像 | この起草では上限の出所を見つけていない (PowerPC の PM が持つ値で照合) | S2 は +1 KB、S5 は +2 KB、P2 は +1.5 KB |

許容を超えたら止めて PM へ。実装者は段の完了の報告に像ごとの前後の大きさを書く。

## 5. ファイルごとの変更と段

| 段 | 主のファイル | 中身 | 連れのファイル | 担当 | x86 の build_compare の期待 |
|---|---|---|---|---|---|
| S1 (その場) | kapi/kapi_sys.c | 診断を platform_mem_stat.h の inline 2 本へ | platform/pc98/platform_mem_stat.h・platform/powermac/platform_mem_stat.h (新)。tools/tests/test_mem_stat.py: 抽出の範囲 (:168-169) に PC-98 のヘッダの本文を足し、module の変異 6 本 (:126-132) の目印を `snap->kdebug_… = kdebug.…;` の形へ。mem_stat を covers に持つ tools/checks/*.py に新しいヘッダ 2 本 | 本線 | kapi_mem_stat の 1 件まで (4 像とも)。ほかの関数に差が出たら止める |
| S2 (移すだけ) | kapi/kapi_sysinfo.c | kapi_mem_stat と STATIC_ASSERT を移す | kapi_sys.c、kapi_sysinfo.h、ver_collect.c:15。test_mem_stat.py の出所の path と終端の目印 (:169 の "/* The generated __cdecl wrapper" は kapi_sys.c に残るので、kapi_mem_stat の直後の目印に替える)、mem_stat_host.c・trim_kernel_host.c・trim_stop_host.c・kapi_ranges_host.c のリンクの入力、checks の covers、docs/design/kapi/kapi_sys.md (U3 の 1 行も) | 本線 | 配置の差だけ: 通常・R1 で各 8 件まで (2b と同じ種類 — TU の移動・番地)。命令の中身が変わるのは kapi_mem_stat だけ |
| S3 (その場) | kernel/ver_collect.c | 機種の読みを collect の口へ、`#ifdef __powerpc__` を消す | include/ver_backend_contract.h・platform/pc98/ver_backend.c (collect だけ)・platform_ver.h ×2 (新)、kernel/ver_info.h (plat[8])、build/platform/pc98.mk・order.mk・build_profile_x86.json (ver_backend.c を boot_diag.c の次に)、ver_collect_host.c と test_ver_collect.py の抽出・リンクの入力・変異の目印、ver-info-host.py の covers | 本線 | ver_collect・kapi_sys_ver_info・ver_format (スタックの大きさ) の 3 件と新しい関数 1 件、ver_backend.o より後ろの番地 |
| S4 (その場) | kernel/ver_format.c | 行の補助を公開、devices・image・debug を後端へ | platform/pc98/ver_backend.c、ver_format_host.c・ver_about_host.c のリンクの入力と変異の目印、ver-format-host.py・ver-api-host.py の covers、**tools/tests/test_ver_version.py:18-20 の CONSUMER・VERSION_USE・OBJECTS[0] を頭行を持つ platform/pc98/ver_backend.c / .o へ** (共通の既定の文言を持つ ver_format.o も作り直しの対象に足す)、docs/design/kernel/ver_*.md | 本線 | ver_format と補助 5 本、後端の 3 関数。.rodata の文字列の中身は不変 (置き場だけ動く) |
| S5 (移すだけ) | build/sources/common.mk | ver_collect.c・ver_format.c を PENDING → C_COMMON_EXEC、kapi_sys_ver_info を kapi_sysinfo.c へ | kapi_sys.c、build_profile_ppc.json (exec の像に 2 本)、docs/SRC_MOVE.md §3、check_layers の期待、kapi_sys_ver_info を抽出する host の試験 (test_ver_api.py) の path | 本線 | 配置の差だけ: kapi_sys_ver_info と前後の番地、通常・R1 で各 4 件まで。order.mk は不変 (2 本の位置は今のまま) |
| P1 | sdk/kapi_target_ppc.py | :122-127 の system_info の欠けの集合から `mem_stat` を外す → `$(KB_GEN)/stamp` (build/targets/ppc-powermac.mk:112-116、gen_kapi.py --target ppc-powermac) で再生成 | 生成の期待を持つ試験・fixture | PowerPC | 無し (x86 の生成物はバイト一致) |
| P2 | platform/powermac/ver_backend.c (新) | pm_ver_backend (§3-4)、platform_ver.h を NULL から表へ、kernel_boot.c:293 の後に pm_ver_note_pvr、欠けの集合から `sys_ver_info` を外して再生成 | build/platform/powermac.mk、build_profile_ppc.json、sdk/kapi_target_ppc.py | PowerPC | 無し (x86 の生成物はバイト一致) |

- 順: S1 → S2 → S3 → S4 → S5。S1・S3・S4 は NP21/W の受入が要る「その場」の段なので 1 本ずつ (本線の取り決め 3)。S2 と S5 は kapi_sys.c・kapi_sysinfo.c が重なるので直列。
- 並列: P1 は S2 の後なら S3〜S5 と並列にできる。P2 は S5 の後。S3 と S4 は同じ ver_backend.c を触るので並列にしない。
- **P1・P2 の完了条件 (slot の結線)**: (a) 生成された PowerPC の kapi_generated.c で、その slot が「欠け」の応答でなく kapi_mem_stat / kapi_sys_ver_info を呼ぶ実の wrapper になっている。(b) slot の番号・KAPI の版・構造体の大きさが x86 の生成物と同じ。(c) wrapper が sdk/kapi.json:1913-1917 (mem_stat)・:1953-1957 (sys_ver_info) の出力の範囲の契約どおりに、本体を呼ぶ前に範囲を検査している。(d) P1 の時点では sys_ver_info がまだ欠けの集合にあり、欠けの応答を返す (QEMU で確かめる)。(e) x86 の生成物が前とバイト一致。
- P1 の前提: 親設計の 2c の順 (build、mtime、time、mem、ver) のとおり、同じ集合の sys_get_build_info・sys_time_now が先に外れている事。fb27aa1d8 では 4 つとも集合に残っている — 順を飛ばすかは PowerPC の PM が決める。
- S2 の時点で PowerPC の exec の像は kapi_mem_stat をリンクする。PowerPC の 3 像の map で未解決が無いことを S2 の完了条件に入れる (公開表に出すのは P1)。
- 層の検査: kapi_sysinfo.c・ver_collect.c・ver_format.c が読むのは include/ の口と固定名 platform_mem_stat.h・platform_ver.h だけ。pcm_contract.h・boot_diag.h・cpu.h・module.h の include は platform/pc98 の側へ移る (pcm_contract.h と boot_diag.h は include/ の共通の契約なので、共通が読んでも層の違反にはならない。移すのは使う所が移るから)。

## 6. 受入 → 試験 → 変異

| 受入 | 試験 | 変異 (1 行) |
|---|---|---|
| x86 の mem_stat の答えが前と同じ | host: tools/tests/mem_stat_host.c (test_mem_stat.py)。NP21/W: mem_stat_test・mem_stat_compat_test (build/app.conf:256-257) | platform_mem_stat_diag の `audit_fail` の和から 1 項を落とす → host が落ちる |
| 大きさ 132・133・220 で戻り値と書く範囲が前と同じ (132 は kstack_high_water を書かない、133 以上は書く、範囲の外の番兵は無傷) | host: mem_stat_host の size の境界 (3 つの大きさ、出力の後ろの番兵)。NP21/W: S1・S2 の前後の版で、起動直後の同じ手順 (同じ状態保存から `mem` を 1 回) の戻り値と返ったバイト列を比べる | `_pre` の `size >` を `>=` に → host が落ちる |
| kstack_high_water の観測値の差が説明できる | NP21/W: 上と同じ手順で低 16 ビット (固定の kstack)・高 16 ビット (シェルのスタック) を前後で記録する。差は kapi_mem_stat のフレームの差 (2 つの版の objdump) 以内。kselftest の上限の検査 (kdebug/kselftest.c:210) が通る | `_pre` を irq_save の後ろへ動かす → host の「IF を落とす前に呼ぶ」の検査 (S1 で足す) が落ちる |
| x86 の `ver` の行が同じ入力に対して 1 バイトも変わらない | host: ver_format_host.c・ver_collect_host.c・ver_about_host.c が固定の VerSnapshot から作った OS32_VerInfo の全体 (1364 バイト) を、今の版の出力を固定した期待とバイト比較 | 共通の ver_format で devices と identity の呼び順を入れ替える → host が落ちる |
| NP21/W の `ver` が正しい | S3・S4 の後に `ver` の画面の文字を取り、(a) 行の表題と順が前の版と同じ、(b) 固定の文言 (頭行・CPU・PIC・PIT・KBD・SER・SND・GFX) が前の版と文字一致、(c) 可変の値 (Build の日時、Commit、Image CRC、kdebug の対の CRC、sqlite の高水位) は形と、その版の成果物の値 (配備した像の CRC・コミット) との一致を確かめる。前後の版の全行の diff は取らない | PC-98 の後端の PIT の行の "Hz input=" を落とす → host と (b) が落ちる |
| 後端が NULL なら共通の行だけ (頭行・API・Build・(Commit)。Image CRC の行も無い) | host: ver_format_host に表 NULL の場合を足す。count と各行の kind を確かめ、DEVICE・DEBUG の行が 0 本 | NULL の検査を外す → host が落ちる (NULL の参照) |
| collect の口が無い像で pit_*・irq_*・pcm_present・fpu_present が 0 のまま | host: ver_collect_host に表 NULL の場合 | 共通の ver_collect に `s->fpu_present = 1` を戻す → 落ちる |
| 共通の 3 本が機種・装置のヘッダを読まない | check-layers (tools/check_layers.py の R2・R3)、test_src_layout.py | ver_collect.c に `#include "drivers/sound/cs4231/pcm_cs4231.h"` (装置、R3) を足す → 落ちる。kapi_sysinfo.c に `#include "platform/pc98/pit.h"` (機種の私有、R2) を足す → 落ちる |
| PowerMac の mem_stat (P1) | QEMU PowerMac の実アプリ (userland/tests/mem_stat_test の PowerPC の組) から: 自分 (app_id = -1 と自分の番号)・全体 (0)・他人 (INVAL) の照会、大きさ 43 (INVAL)・44 (MEMSTAT_MIN)・132・133・220・221 (戻り値は 220)、返ったバイト列で size・app_id・phys_total_pages > 0・MEMSTAT_HAS_AS、診断の欄 (kstack_high_water・audit_*・kselftest_*・kdebug_*) が全部 0。§5 の完了条件 (a)〜(e) | PowerMac の `_diag` に `snap->kdebug_state = 1` を入れる → 落ちる。欠けの集合に mem_stat を戻す → (a) が落ちる |
| PowerMac の `ver` が §3-4 の表どおり (P2) | host: 偽の値を入れた VerSnapshot で pm の後端を回し、全部の条件が立つ時 10 行・全部落ちる時 4 行 (頭行・DEC・API・Build)、行の順と kind を確かめる。QEMU PowerMac: `ver` の行の順と kind、PVR が QEMU の機種の値、MPIC の数が 0 でない、ESCC の baud が設定の値、GFX が QEMU の画面の大きさ、Root image CRC が像の PM_ROOT_IMAGE_CRC と同じ。"8259"・"8254"・"8251"・"OPN"・"PC-98"・"x86"・"loader"・"FPU"・"calib" が無い | PVR の有効の印の検査を外す → host の 4 行の場合が落ちる。image の crc_valid の検査を外す → 同じく落ちる |

- ring3_in_syscall は PowerPC でも立つ (レビューの静的な確認: arch/ppc/kernel_trap.c:108-110 が共通のディスパッチに入り、exec/exec_syscall.inc:123-169 が wrapper の前に 1、戻りで元へ)。P1 の先の確かめは要らない。
- 延期する確かめ: 実機 Ra266 の `ver` は S5 の後の実機の更新の時に 1 回 (段ごとには回さない)。

## 7. 未決 (ユーザー判断)

無し。r1 の U1〜U3 は PM 決定で確定した。

| # | 決定 (PM) | この票での反映 |
|---|---|---|
| U1 | pd_phys の判定は CPU の口に分けず、既存の共通の判定を保つ。「CPU の口」は PowerPC の PM の #29 の提案で、ユーザーの明示の指示ではない | §1-3 の 1、§2 |
| U2 | PowerMac の `ver` は実測できる欄に絞り、FPU の欄は出さない | §3-1、§3-4 (calib も同じ理由で出さない) |
| U3 | MemStat の診断の欄は ABI を保って PowerMac で 0。kstack_high_water = 0 は「未計測を含む」と明記 | §2、S2 の連れ (docs/design/kapi/kapi_sys.md) |

## 8. レビューへの反論

- 無し (10 件とも反映)。ただし「各段に大きさの上限を記す」のうち PowerPC の 3 像の上限は出所を見つけられず、§4 に「PowerPC の PM の値で照合」と書いた (未読の申告で、反論ではない)。

## 確定 (2026-11-19、sol の第 2 回の条件付きの承認、往復 2 回)
各段の完了条件に足す (sol の返答の写し):
- **S1・S2／P2** — `mem` は `sizeof(MemStat)` 固定なので、各サイズを直接渡す試験で戻り値と番兵を測る。スタック差は保存済みピークと、呼出点までの push・引数・整列を含む深さで照合する。 [`cmd_sys.c`](/home/hight/os32-v3/userland/shell/cmd_sys.c:56) [`kstack_hw.c`](/home/hight/os32-v3/kernel/kstack_hw.c:22)
- **S2・S5・P2** — PowerPC の3像は `PM_KERNEL_IMAGE_MAX` と `PM_KERNEL_CHAIN_LIMIT` の両方にリンク時に収まることを確認し、増分も別に測る。 [`kernel.ld`](/home/hight/os32-v3/arch/ppc/kernel.ld:54)
- **S4** — `debug` に services 行末の `ver_end(&b)` まで移し、固定入力のバイト比較に含める。 [`ver_format.c`](/home/hight/os32-v3/kernel/ver_format.c:203)
- **P1** — PowerPC 用試験バイナリのビルド・配備を明記し、既存の `kstack_high_water > 0` を PowerPC ではゼロ期待に替える。`MEMSTAT_HAS_AS` は自己で立ち、全体で落ちることを確認する。 [`build/ppc-user.mk`](/home/hight/os32-v3/build/ppc-user.mk:27) [`mem_stat_test.c`](/home/hight/os32-v3/userland/tests/mem_stat_test.c:17)
- **P2** — `kernel_boot.c` は非 exec 像にも入るため、PVR 保存呼出しと後端のリンク対象を揃え、非 exec 像もリンク確認する。 [`ppc-sources.mk`](/home/hight/os32-v3/build/arch/ppc-sources.mk:6) [`kernel_boot.c`](/home/hight/os32-v3/platform/powermac/kernel_boot.c:289)
- **P2** — CRC 条件を外す変異は `boot_ok=1, crc_valid=0` で落とす。4行ケースは `boot_ok=0` なので `image` 自体が呼ばれず、変異を検出できない。
