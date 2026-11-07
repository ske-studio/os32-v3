# PPC_BOOT_DESIGN — PowerPC 移植の最初の段 ppc-boot の確定設計

> 状態: **現行 (2026-11-07)** — QEMU mac99 の Open Firmware から ELF を読み、OF の write と ESCC 直叩きで 1 行ずつ出して止まるまで。段の状態は票 TASK_PPC_BOOT が持つ。
> 出所: ~/os32-tmp/run/ppc-boot/DESIGN.final.md を 2026-11-07 に写した (astra 起草 r1〜r3、Opus 5.5 第 1・2 回、PM が反映)。

## r2→r3 の変更
- M1: 手本の順に MSR 保存・BAT0 の RAM 写像・MSR 復帰を移す。
- M2: real-mode? を手本の判定で読み、真なら map を省く。
- M3: stdin/stdout の取得と stdout=0 の screen open を移す。
- M4: root の address/size cells と chosen の memory/mmu を手本順に検査する。
- M5: BSS を _edata〜_end の 4 回の stb と 4-byte 増分で消す。
- M6: MMIO mode はビット定義から求めた名前付き定数とする。
- C2: lib/kstring_c.c をそのまま組み、types.h・kstring.h を使う。
- C4: 番地・サイズ・パスを 3 層の定数にまとめ、ld も同じヘッダから生成する。
- C5: 番地とポインタの変換を専用の口に集約する。
- P1: -nostdlib、既定の toolchain 1 組、カーネルの soft-float/no-altivec を固定する。
- P4: libc なしの host fake と fixture を加え、QEMU 統合試験も残す。
- P5: 1 実装者が段ごとにビルドし、文書の配置先を docs/tasks/ppc/ とする。
- P6: PM 別枝の NOTEST を維持し、x86 回帰の比較入力変異を削る。
- P7: 手本の検査を保持し、drain は手本に無い追加と明記する。

## 1. 実物の確認
- 対象は QuickSilver（7450/7455、BE32、OF 4.x）、先行機は QEMU 8.2.2 mac99 / OpenBIOS 1.1。シングルタスク・協調切替、整数のみ。共有コード・既存ビルドは変更しない。
- OS32 読取元は `/home/hight/wt/os32-v3/ppc-env`、基点 `d524ab05`。N は `/home/hight/os32-tmp/ref/netbsd` @36eddebc、O は `/home/hight/os32-tmp/ref/openbsd` @b907757a。NB=N:sys/arch/macppc/stand/ofwboot/Locore.c、NL=N:sys/arch/macppc/stand/ofwboot/loadfile_machdep.c、NZ=N:sys/arch/macppc/dev/zs.c。
- REVIEW.r1 の実測を採用：MSR=0x2030、real-mode?=false、load-base=0x4000000、available 下端=0x4000。macio BAR=0x80000000、ch-a offset=0x13020、先頭 size=1、RR0=0x44/45、RR1=7、open 成功。ELF の CD 起動は未観測。
- 完成済み toolchain は `/home/hight/opt/cross-ppc/bin`。本改訂では §2 の旗で lib/kstring_c.c の PPC コンパイル成功を確認した（kstring.r3.o）。起動は実測せず実装段へ渡す。既定 1 組は hard-float・AltiVec なし、カーネルは soft-float/no-altivec、userland ABI は SysV PPC32 hard-float とする。
| ID | 前提と実物の差 | 決定 |
|---|---|---|
| X1 | build/config.mk:41,59,134 は x86 固定 | config を読まない独立 build/ppc.mk |
| X2 | include/cpu.h:30–34 は x86_flags.h・cdecl に依存 | 契約ヘッダ io.h・cpu.h は取り込まない |
| X3 | arch/README.md の新 arch 手順は全原始命令を要求 | 今段は独立試作、共有契約の実装は次段 |
| X5 | NZ:1048 は物理和、O:sys/arch/macppc/dev/zs.c:1040 は mapiodev | 本段は firmware に I/O 写像を依頼 |
| X6 | O:sys/arch/macppc/stand/ofwboot/ld.script:21–24 は PT_LOAD のみ | NOTE bytes を明示する PT_NOTE を追加。根拠は PM F11 と OF 用 NOTE を持つ同 stand/ofwmagic.S:37–69 |

## 2. 起動の経路
| 順 | 手本 | OS32 の処理 |
|---|---|---|
| 0 | O:sys/arch/macppc/stand/ofwboot/ld.script:19–33 | OF 自身が ELF をロードし _start へ |
| 1 | NB:69–186 | sync/isync、専用 stack、MSR 保存→0→isync、BAT、isync→MSR 復帰→isync、BSS、C へ（r5 保持） |
| 2 | NB:233–239,703–783 | CI 登録、chosen・stdin/stdout・screen、root/options/openprom/chosen、real-mode?、cells、memory/mmu、OF 行 |
| 3 | NZ:1024–1048 | 木の最初の ch-a と親 macio から物理番地、path を open |
| 4 | N:sys/arch/macppc/stand/ofwboot/loadfile_machdep.c:176–199 | real-mode なら map を省略、それ以外は chosen/mmu へ map、直接送信前に EE 禁止・sync |
| 5 | NZ:905–923,789–798 | RR0→data の直接送信、RR1 ALL_SENT 待ち |
| 6 | include/io.h の _stop 契約 | sync、EE=0、ppc_stop_loop 自己分岐 |
- ELF32 BE / EM_PPC / ET_EXEC、entry=_start。PT_LOAD は 1 個 RWX、p_vaddr=p_paddr=PM_LINK_BASE (0x20000)、整列は `-N` に従う (O:sys/arch/macppc/stand/ofwboot/Makefile:43 と同じ。実測 offset 0x74・align 4)、text/rodata/data/BSS/stack を含む p_memsz <64 KiB を ASSERT。PT_NOTE は別に 1 個、同じ bytes を LOAD にも含める（X6）。
- NOTE は owner=`PowerPC\0`、namesz=8、descsz=20、type=0x1275、descriptor={0,-1,-1,-1,-1}（virtual、他は既定）、O:sys/arch/macppc/stand/ofwmagic.S:37–69 の値を移す。
- 0x20000 は OpenBSD のリンク番地。load-base はロード作業域、real-base は firmware の予約条件であり、どちらも空き RAM の保証ではない。環境変数を変更しない。OpenBIOS の ELF/CD 起動は §6、Apple OF4.x の受理と非衝突は §7 で確認する。
- 入口は supervisor・BE32、r5=CI。OF の r1 を置換し、MSR を r8 に保存して 0 にし isync、IBAT0–3 upper→DBAT0–3 upper をゼロ、IBAT0 lower→DBAT0 lower に 0x12、IBAT0 upper→DBAT0 upper に 0x1ffe を書く（RAM 0〜256 MiB、PA=VA）。isync→保存 MSR 復帰→isync。FP/VMX 命令は生成しない。
- 601/970 分岐だけは対象が 7450/7455 なので移さない。BAT 初期化以外で OF 呼出し完了まで EE は入口値、直接送信前に EE を落とす。
- r5 を保持し、16-byte 整列の 8 KiB stack 上端を設定→上記 BAT/MSR 列→_edata〜_end を比較し stb を offset 0,1,2,3 に行い 4-byte 進む→32-byte 初期 ABI frame（backchain=0）→r3=r5 で powermac_boot。stack は手本と同じ BSS 消去範囲内、NOLOAD・p_memsz 内。消去前に stack へ保存しない。r2/r13 の SDA に依存しない。
- C 旗は `-std=gnu11 -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none -ffreestanding -fno-builtin -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -O2 -nostdlib`、include は `-I. -Iinclude -Ilib -Iarch/ppc -Iplatform/powermac`。VLA・暗黙 int・暗黙宣言はエラー。リンクは GCC driver に `-nostdlib -no-pie -Wl,-N -Wl,-m,elf32ppc` と生成 ld script を渡し、newlib/libgcc/C_KERNEL をリンクせず未定義シンボル 0。必要な整数 helper は自前、newlib nano は SDK/userland の段。
- CI 登録後 `/chosen` を finddevice（-1 は exit）→stdin getprop→stdout getprop（各 4 bytes、stdin 不正なら stdout を読まず exit）→stdout==0 なら open("screen")。NB:703–714 の順を保ち、exit が戻れば stop。
- 続けて `/`→`/options`→`/openprom`→`/chosen` を finddevice、root/chosen=-1 は bad_environment。options があれば real-mode? を読み、正の長さの先頭が y/Y/t/T/1 のとき真、他は偽（既定も偽、NB:218–230,736–749）。
- address-cells/size-cells を各 1 で初期化→root からこの順に getprop→size-cells!=1 は bad_environment。memory/mmu を各 -1 で初期化→chosen からこの順に getprop→memory==-1、次に mmu==-1 を検査し bad_environment（NB:754–783）。ac=1/2 の cell 列を使う。正常なら `OF: os32 ppc-boot\r\n` を write 1 回 (NB:429 の OF_write と同じ、再送しない)。bad_environment は FAIL:environment を表示して exit、戻れば stop。
- 木は child/peer で最初の name=ch-a、親の name が `escc` の完全一致 (NZ:203、`escc-legacy` は取らない)、その親を macio とする。ch-a の reg は 4 byte 以上で reg[0] を offset、macio の assigned-addresses は 12 byte 以上で **reg[2] を基点** (NZ:1039-1047 のとおり)。
- CSR=BAR CPU PA+ch-a reg の先頭 offset、data=CSR+PM_ESCC_STRIDE、窓=PM_ESCC_WINDOW。escc 親 offset は重ねない。先頭 tuple の size は使わない。property の cell 長とアドレス計算の上位非ゼロを検査する。根拠：NZ:1039–1048、N:sys/arch/macppc/include/z8530var.h:183–186、NZ:100–104。
- ch-a の path を package-to-path で得て suffix なしで open、ihandle は停止まで保持。OF に clock/baud/8-bit 設定を任せるのは **手本に明記なし、設計の解釈**（NZ:849,905）。根拠はこの早期 putc が送信だけを行うことと、レビューの QEMU open/RR0 実測。今段で baud/reset/WR9 は書かない。
- real-mode? が真なら NL:214–216 に従い map を呼ばず物理 CSR を使う。偽なら OF map は PA=VA=CSR、size=PM_ESCC_WINDOW、mode=PM_MAP_MODE_IO。0x2a は手本に無い値で、N:sys/arch/powerpc/include/oea/pte.h:96–107 の PTE_I|PTE_G|PTE_RW から求めた。失敗なら FAIL:map で停止し、成功時だけ直接アクセスする。
- 各 byte は CSR の RR0 bit2=0x04 待ち→data store。NZ:916 の `wait++ < 1000000` を上限にし、期限切れは失敗。`ESCC-A: os32 ppc-boot\r\n` はこの口だけから出す。
- drain は CSR に 1 を書き RR1 bit0 ALL_SENT を同じ上限で待つ。手本に無い追加、送信直後に止まるためシフトレジスタ完了を待つ（N:sys/dev/ic/z8530reg.h:390,404、NZ:789–798）。RR1 再読時も index=1 を選択する。
- byte MMIO 直後に eieio、compiler の並べ替えを許さない外部アセンブリ関数。OF→直接・drain→stop で sync。NZ:781–786 の空 ZS_DELAY と同じく独自遅延を足さない。停止ループは手本の起動列に無いが、今回の _stop 相当という到達点に必要。

## 3. 部品の境目
```c
/* arch/ppc/ppc_boot.h: unsigned int と pointer は 4 bytes */
typedef unsigned int cell;
typedef int (*ppc_ci)(void *);
unsigned int ppc_load8(cell va);
void ppc_store8(cell va, unsigned int value);
void ppc_sync(void);
void ppc_disable(void); /* MSR EE を落とし isync */
void ppc_stop(void) __attribute__((noreturn)); /* ppc_stop_loop を公開 */
void powermac_boot(ppc_ci ci);
/* platform/powermac/ofw.h: 同期・単一呼出し、heap 不使用 */
void ofw_init(ppc_ci ci);
struct ofw_env { int in, out, memory, mmu, real_mode; unsigned ac, sc; };
int ofw_setup(struct ofw_env *e); /* 0 成功、-1 console 不可、-2 bad_environment */
int ofw_option_truefalse(const char *prop, int proplen);
int ofw_call(const char *name, unsigned ni, unsigned no, cell *cells);
/* cells[0..ni) は入力、cells[ni..ni+no) は結果。CI 戻り値を返す。
 * 内部の実配列は {name,ni,no,cells...}。4-byte cell、pointer も 1 cell。 */
int ofw_finddevice(const char *path); /* 1/1 */
int ofw_getprop(int ph, const char *name, void *buf, unsigned cap); /* 4/1 */
int ofw_child(int ph); int ofw_peer(int ph); int ofw_parent(int ph); /* 各 1/1 */
int ofw_package_to_path(int ph, char *buf, unsigned cap); /* 3/1 */
int ofw_open(const char *path); /* 1/1。CI -1 または handle 0 で -1 (NB:376-398) */
int ofw_write(int ih, const void *buf, unsigned n); /* 3/1 */
void ofw_exit(void); /* 0/0 */
int ofw_call_method(const char *method, int ih, unsigned ni,
                    unsigned no, cell *cells); /* 0 成功、CI -1 / catch 非0 は失敗 */
int ofw_map_io(int mmu, unsigned ac, cell pa); /* ac は root #address-cells */
struct pm_escc { cell csr, data; int ih; };
int pm_escc_prepare(const struct ofw_env *e, struct pm_escc *out); /* 木→open→必要時 map */
int pm_escc_write(const struct pm_escc *s, const char *p, unsigned n);
int pm_escc_drain(const struct pm_escc *s); /* write/drain: 0 成功、-1 timeout */
/* boot.c が stdout と失敗表示を所有。段名は stdout/environment/tree/open/map/tx/drain。
 * setup=-1 は exit、-2 は FAIL:environment 後 exit、戻れば stop。
 * prepare は tree=-1, open=-2, map=-3。FAIL:<段>:<rc> を OF write で出す。
 * 直接送信失敗時も診断後 stop。正常の直接送信後には OF を呼ばない。 */
```
- ld↔entry.S：`_start`、`_edata`/`_end`（BSS と stack を含む 4-byte 整列の半開区間）、`__stack_bottom`/`__stack_top`（8 KiB、top は16-byte 整列）、`__image_end`。この半開区間を消去し、C が戻れば ppc_stop。entry.S は入口と上記 CPU/MMIO 原始操作を持つ。
- NOTE の入力 section は `.note.ofw`（alloc、4-byte 整列）。boot.ld は `KEEP(*(.note.ofw))` を LOAD と NOTE に割当てる。文字列は 8 bytes、descriptor は 5 cells。
- OF 探索の固定 buffer は property 4 KiB、path 1 KiB。getprop の長さと buffer 上限を確認。lib/kstring_c.c は kstring.h→types.h のみを引き、u8/u32 と unsigned long を使う。PPC32 は long/pointer が 4 bytes。lib/kstring_c.c:227–231 の memcpy/memset/strlen/strcmp/strncmp alias と memcmp を使い、そこで未定義の memmove は呼ばない。宣言は lib/kstring.h を引く。契約ヘッダ io.h・cpu.h は取り込まない。
- map の元配列は N:sys/arch/macppc/stand/ofwboot/loadfile_machdep.c:179–195、実 CI 配列は NB:654–693。環境は ofw_setup が所有する。ofw_call_method は NB:671–672 の ni>6 の拒否も保持する。
```c
int ofw_map_io(int mmu, unsigned ac, cell pa) {
    cell cells[5], *p = cells;
    if (ac == 2) *p++ = 0; /* 32-bit PA の上位 */
    *p++ = pa; *p++ = pa; *p++ = PM_ESCC_WINDOW; *p++ = PM_MAP_MODE_IO;
    return ofw_call_method("map", mmu, ac + 3, 0, cells);
}
/* call_method は method 引数だけ逆順にし、catch を結果の先頭に置く。
 * ac=1: {"call-method",6,1,"map",mmu,0x2a,0x20,va,pa,catch}
 * ac=2: {"call-method",7,1,"map",mmu,0x2a,0x20,va,pa_lo,pa_hi,catch}
 * CI=-1 は -1、catch 非0 はその値を返す。成功時、method の結果を逆順で
 * 呼出元 cells[0..no) に戻す（NB:690–691）。map は no=0、catch だけを確認する。 */
```
- C4 の 3 層は docs/POLICY_DEV.md §2 の Shared→Subsystem→Local。今段に公開 ABI 定数はなく Shared を増やさず、Subsystem の platform/powermac/ppc_layout.h に PPC の番地・サイズ・OF path を集約する（PM_LINK_BASE=0x20000、PM_ESCC_WINDOW=0x20、PM_ESCC_STRIDE=0x10、PM_MAP_MODE_IO=0x2a、BAT 値・stack/整列・poll 上限・RR mask）。Local はそこからの導出・参照だけにし同じ値を再定義しない。実行時の BAR/offset は DT から取得する。
- build/config.mk:9 の LAYOUT_CPP と同じ `$(CC) -E -P -x c -imacros platform/powermac/ppc_layout.h` で boot.ld を生成。C/asm/ld 共用の定数は suffix・型宣言なし、C 専用部はガードする。出力・ISO/ログのパスは build/ppc.mk の基点→派生パス→run.py 引数で一元化する。
- C5: ppc_boot.h の `cell ppc_ptr_cell(const void *p)` / `void *ppc_cell_ptr(cell a)` に pointer/cell 変換を集約する（static inline）。MMIO は cell のまま外部 asm に渡し、CI の pointer 引数はこの口だけで cell 化する。
- arch は OF/path/ESCC を知らず、platform は CPU 命令を書かない。共有 include/io.h・cpu.h の契約は次段でまとめて実装し、その時点から共有の I/O・CPU 契約を接続する。

## 4. 部品の大きさ（推定 KiB）
| 部品 | file / RAM 増分 |
|---|---|
| entry・CPU/MMIO・NOTE | 2 / 2 |
| OF・木・共有文字列関数 | 6 / 11 |
| ESCC・結線 | 2 / 2 |
| stack・ELF 整列 | file 約4 / stack 8 |
| 合計予算 | ELF ≤24、p_memsz <64、通常 RAM 約25。heap なし |

## 5. 新規ファイルと割当て
| ファイル | 行数目安 | 段（1 実装者が表の順に進める） |
|---|---:|---|
| platform/powermac/ppc_layout.h | 45 | A1 定数の 3 層、cpp 出力確認 |
| arch/ppc/ppc_boot.h | 45 | A2 CPU/MMIO・変換口、宣言コンパイル |
| platform/powermac/ofw.h | 65 | A3 OF/環境/ESCC 宣言、宣言コンパイル |
| arch/ppc/entry.S | 105 | B1 BAT/MSR・BSS・CPU/MMIO、object ビルド |
| arch/ppc/boot.ld | 40 | B2 cpp・リンク記号、script 生成 |
| arch/ppc/ofw_note.S | 25 | B3 NOTE、object ビルド |
| tools/ppc/flow_fixture.h | 70 | T1 手本の期待列、fixture コンパイル |
| tools/ppc/flow_fake.c | 130 | T2 CI/MMIO fake・_start、object ビルド |
| platform/powermac/ofw.c | 175 | C1 CI/setup、T2 で host TDD・PPC object |
| platform/powermac/escc_tree.c | 140 | C2 木・open・条件付き map、T2・PPC object |
| platform/powermac/escc.c | 55 | C3 送信/drain、T2 で host TDD・PPC object |
| platform/powermac/boot.c | 45 | D1 結線・FAIL、T2 の全ケース・PPC object |
| build/ppc.mk | 65 | D2 明示 source・host/ELF の全リンク |
| tools/ppc/run.py | 180 | T3 host→静的→QEMU→変異 |
- 新規 14 ファイル・約1185行、既存 lib/kstring_c.c を無変更で source に加える。1 実装者が段ごとにビルド、製品 C は実装と host TDD で進める。D2 完成前は同じ旗の直接コマンドで object/host を組み、host は完成済み製品部品のケースから順に有効にする。BSD から移す部分に著作権・許諾本文を残す。Linux/OpenBIOS/QEMU の GPL ソースは読まない・写さない。
- 実装時の文書は docs/tasks/ppc/。この設計改訂は依頼どおり作業ディレクトリだけに置き、リポジトリへは書かない。
- 独立 `make -f build/ppc.mk CROSS_DIR=/home/hight/opt/cross-ppc`、生成物は全て `build/out/ppc/`。既存 config/Makefile/.gitignore を変更しない。
- PM 別枝の前提：tools/checks/run.py の NOTEST に `('arch/ppc/**', ())`、`('platform/powermac/**', ())`、`('tools/ppc/**', ())`、`('build/ppc.mk', ())`。実装者は触らず、PM が PC-98 側 PM に渡す（同 :49,826–829 の実物確認済み）。
- CHECK は登録せず make check 外の計測器。CI runner に PPC/QEMU を要求しない。既存検査の TU は既存 make の対象、PPC C の命令は .S に置く。NOTEST の前提適用後に通常 CI check/build の success を確認する。

## 6. 受入・変異・台本
| 受入 | 観測判定 | 変異（1 個ずつ、ビルド成功） |
|---|---|---|
| ELF/ABI | readelf で BE32/PPC/entry/LOAD 1/NOTE、objdump で未定義0・FP/VMX なし、NB の BAT/MSR 列・BSS stb 列と一致 | NOTE type を変更→静的検査 RED |
| host の手順 | fixture と CI の順・cell、MMIO の順が完全一致、未消費の期待列なし | method cell の順交換／data の先行 store→host RED |
| OF 環境 | stdin/stdout 長不正→exit、stdout=0→screen、sc!=1・memory/mmu=-1→environment、real-mode 真→map なし（host） | screen 分岐抑止／real-mode 分岐反転→host RED |
| OF stdout | a.log に OF 行がちょうど1回 | OF 行の1文字変更→照合 RED |
| DT→map→直接送信 | a.log に ESCC 行が1回、b.log に同じ行なし | data store を抑止→行欠落 RED |
| map 失敗 | host fake で CI=-1 と catch≠0 のとき FAIL:map、ESCC への書き込み 0 回、停止。QEMU は不正な map でも ESCC に届く (実測: 存在しない method で CI=0・catch=0) ので、**map の効果は QEMU で証明できない**。実機で確かめる点 | fake で catch の判定を外す→fake RED |
| 送信・drain の期限 | 正常は2行後停止、期限切れは FAIL:tx/drain と停止 | RR0 mask を0へ／RR1 mask を0へ（別試行）→期限内 FAIL、通常判定 RED |
| 停止 | QMP で PC=ppc_stop_loop、MSR EE=0、再開・再停止でも一致 | stop の分岐先を別ラベルの自己ループへ→PC 判定 RED |
| x86 退行なし | `git diff --stat d524ab05..HEAD` が表の新規パスだけ、既存 CI check/build success | — |
- host fake は native gcc -m32 -nostdlib -nostdinc -isystem $(gcc -print-file-name=include) -ffreestanding -fno-builtin -fno-pie -no-pie、自前 _start と Linux exit syscall で終了コードを返す。実行は native と qemu-i386 のどちらでも走る形にする (build/sdk.mk:156 の HOST32_RUNNERS と同じ)。32bit libc を導入しない。ofw.c/escc_tree.c/escc.c/boot.c と無変更の kstring_c.c を組み、登録 CI 関数と ppc_load8/store8/sync/disable/stop を fake に差す。各ケースを別プロセスで選び、stop/exit 到達時に残りの期待列と終了条件を検査する。pointer/cell/u32 を本番と同じ 32bit にする。
- fixture 冒頭に NB:218–230,654–693,703–783、NL:179–199,214–216、NZ:789–798,905–923 と BSD 許諾・著作権、手順を列へ転記して照合し直す更新法を書く。試験時に手本ソースを読まない。map の ac=1/2・CI=-1/catch、real-mode 両枝・console/environment 失敗、RR0→data・RR1 index→read・timeout を照合する。
- fake は QEMU の property（BAR=0x80000000、offset=0x13020、size=1）、RR0=0x44/45（TX_READY 設定済み）、RR1=7 で始め、poll は本物の wait++ < 1000000 と同じ上限。遅延 ready と期限切れも小さな入力差で試す。fixture と fake の 2 ファイル約200行に収める。
- 台本：上記 make → `python3 tools/ppc/run.py --timeout 30 --mutate`。run.py は host fake を実行した後、cross readelf/objdump の静的検査、ISO 作成、QEMU 実行、ログ照合と QMP 観測を行う。BAT/MSR・BSS は逆アセンブルと手本の命令列を実装レビューでも照合する。
- 一時ディレクトリに PPCBOOT.ELF を置き `genisoimage -quiet -hfs -r -part -no-desktop -o build/out/ppc/boot.iso <一時ディレクトリ>` (OpenBIOS は HFS を読む。xorriso の ISO では open-dev が 0、実測)。OF の boot でロードする。ROM は `/usr/share/qemu/openbios-ppc`、レビュー SHA256 先頭は 277f5d3e5dd931f0、実行時は全 SHA256 と QEMU 版を記録。
```sh
qemu-system-ppc -M mac99 -cpu 7450 -m 128M -smp 1 -accel tcg \
  -bios /usr/share/qemu/openbios-ppc -cdrom build/out/ppc/boot.iso -boot d \
  -prom-env 'boot-device=cd:,\PPCBOOT.ELF' -prom-env 'auto-boot?=true' \
  -nographic -serial file:build/out/ppc/a.log -serial file:build/out/ppc/b.log \
  -monitor none -qmp unix:build/out/ppc/qmp.sock,server=on,wait=off -no-reboot
```
- 親の絶対期限30秒内で QMP capabilities→stop→human-monitor-command `info registers`。PC は objdump の ppc_stop_loop と比較、EE は MSR & 0x8000。未到達なら cont して期限内に再観測。正常停止後も cont→stop で確認、quit→wait。期限切れは terminate、2秒後 kill。
- 各変異は元の正常像で GREEN→一時コピーの一意箇所を変更して期待 RED→元へ戻して GREEN。変更前後・試験名・ログを保存。ビルド失敗は動作変異の RED に数えない。RR0/RR1 の失敗試験は FAIL と停止も期限内に確認する。
- CD パス・stdout の A 接続はまだ未観測。OF の dir/devalias・banner・boot-path を採取して台本を確定し、2行と停止が揃うまで未達。PM の NOTEST 別枝の差分は PPC 実装差分と分けて確認する。

## 7. QuickSilver 実機の観測路
- 外部 ESCC コネクタなしを前提に、USB keyboard と OF 画面を第一観測路とする。stdout=screen のまま OF 行・FAIL を撮影する（NB:711–714 の screen open、r1 の NetBSD macppc FAQ）。
- 実機で確かめる点：OF banner/version、ELF/NOTE 受理、0x20000 と予約の非衝突、MSR、load-base/real-base、chosen stdout/mmu、macio/ch-a の property、open/map 成否。
- 実機の ch-a 配線・clock/power・接続相手は未確認。QEMU の受信文字列を実機の電気信号の証明にしない。筐体改造は本段の必須条件にせず、直接送信後の画面診断は次段で行う。
- ELF 配送の代替は OF netboot（r1 の NetBSD macppc netboot 資料）。QuickSilver OF4.x の telnet は未確認で採用しない。今段の OF 画面と失敗段の観測から次段へ進める。

## 8. この先の段（順序だけ）
1. OF screen の framebuffer へ直描き、実機 ESCC 診断を加えて観測路を確保する。
2. BAT/ハッシュ表 MMU・例外ベクタ・保存復帰。共有変更候補：cpu.h の x86_flags 依存、root_phys と SDR1/SR/VSID/BAT の差、mm/ の x86 仮定。
3. arch_io/arch_cpu/platform_io の全契約をまとめて実装し共有ヘッダへ接続。例外記述子・_idle・割込み状態の契約を揃える。
4. lib の LE・非整列・ABI 仮定を先に監査し、macio/OpenPIC・タイマ、整数 C_KERNEL へ。共有変更候補：lib、kernel IRQ/time、drivers 登録、build/config.mk。
5. KAPI（int→sc）・協調切替・ABI_WORD_*・arch_call_on_stack・exec/可変引数/longjmp。r3–r10 の引数と cdecl 前提を合わせる。
6. ramdisk 相当のプログラム載せ先を確保し、SDK/ELF loader/relocation/userland へ。共有変更候補：sdk startup/linker/KAPI、公開 ABI、BE 対応。
7. block→filesystem→network。共有変更候補：DMA/cache sync・PCI LE MMIO、fs の on-disk LE、net の BE/checksum。FPU は userland hard-float ABI に合わせ保存方針を用意し、AltiVec の利用は後段でユーザー判断に上げる。

## 9. 未決
- ユーザー判断は0件。未実測事項は §6・§7、PM 別枝の前提は §5 に記載。

## 確定 (2026-11-07、Opus 5.5 第 2 回の条件付き Approve、PM が反映)
- G1 ISO を genisoimage -hfs に。G2 `-Wl,-N`。G3 map の失敗は host fake へ。G4 OF_open の handle==0。G5 macio は NZ どおり reg[2]。G6 write の再送を削除。G7 -I。G8 親は `escc` の完全一致。G9 -nostdinc・qemu-i386。
- PM が確かめた手本: NB:376-398、NZ:203,1039-1047、O:sys/arch/macppc/stand/ofwboot/Makefile:43。G1・G2・G3 の QEMU の値はレビュアーの試作の実測で、製品のコードでは未確認 (実装の段で確かめる)。
- 実機で確かめる点に足す: ESCC の窓 0x20 は PA がページ境界でない。Apple OF の map の可否は FAIL:map で観測する。
