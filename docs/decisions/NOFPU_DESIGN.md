# NOFPU_DESIGN — x86 の FPU の判定と FPU の無い道 (SoftFloat 3e と NP21/W の BSD x87 エミュレータの移植)

> 状態: **確定 (2026-11-16)** — astra 起草 r1〜r3、Opus 5.5 の独立レビュー 2 回 (r1 要修正 P1 3 件、r3 確定可 P2 2 件)、PM の追記 (末尾)。延期の台帳の NOFPU-1 を閉じる設計。
> ユーザー決定 (2026-11-16): 「本線に fpu があるかを判定してからハードウェアの fpu を使う様に改修させてください。ハードウェアに fpu がある場合はこの限りではありません。」(背景「その仕様なら 486 が x86 の下限になってしまう」)。カーネルの SQLite は「soft-float を入れる」(カーネルに libgcc は入れない)。FPU の無い機体のアプリは「x87 のエミュレータ」。「既に実装がないかを確認」→ NP21/W の BSD 2 条項の fpemul_softfloat3 と同梱の SoftFloat 3e (BSD 3 条項) を手本・部品にする (GPL の fpemul_dosbox*.c は読まない)。FPU の無い実機は手元に無く「実機の受入は延期」。


- 2026-11-16、第3回。REVIEW.r1.md 末尾「既存の実装の調べ」を反映。r1/r2とOS32/NP21ソースは変更しない。
- 調査 worktree HEAD: `0d36fb478b0c97103df49c27eb86193336b8e0cc`。本票は実装・配備をしない設計成果物。
- 決定: SQLite は FPU 有無にかかわらず SoftFloat 一系統、REAL を保持、libgcc は本体・部品ともリンク禁止。
- 決定: FPU 無しのユーザー x87 は #NM でエミュレートし、正常終了なら命令の次へ戻る。
- 整数優先は継続。今回の FP 導入理由は既存 SQLite/アプリの互換維持であり、ユーザー承認済み。
- x86 の変更。PPC 7450/7455 には存在判定や no-FPU の経路を追加しない。9801/V86 の FP 互換は対象外。

## 1. 実物と命令棚卸し

| file:line | 確認結果と設計への反映 |
|---|---|
| `arch/x86/kernel/boot_after_paging.c:9` | EM/TS を無条件解除。probe 結果で確定する処理へ置換 |
| `arch/x86/kernel/x87.c:12` | 初手のメモリ宛て FNSTSW を AX 宛てに変更、probe 中の限定 #NM 復帰を用意 |
| `arch/x86/kernel/x87.c:19` | save/restore/init は現在 present guard 内。absent 側へ software state 操作を追加 |
| `init/kernel_boot_memory.inc:97` | paging 後、SQLite より前に probe を完了させる |
| `lib/sqlite3/os32_sqlite_config.h:59` | soft-float 使用というコメントと実際の x86 flags が不一致。実装して一致させる |
| `build/config.mk:191`、`build/sqlite-proto.mk:12` | SQLite の gnu89/機械 flags を維持し、soft 専用 flags を加える |
| `build/sqlite-proto.mk:17` | module wrapper は CFLAGS_BASE。sqlite3.o だけ soft にしては ABI が壊れる |
| `tools/prototypes/sqlite_imports.def:298` | column_double の返値が double。部品境界を u64 の binary64 bit 列へ変更 |
| `tools/gen_sqlite_imports.py:98` | facade/stub/call frame/module adapter を同じ定義から生成する |
| `lib/sqlite3/os32_sqlite_outer.h:21`、`kernel/sqlite_outer.c:8` | 外側 double 宣言と生成 wrapper の両方を整数 ABI へ変更 |
| `kapi/kapi_db_helpers.inc:347` | SQLITE_FLOAT の double→i32 変換が resident x87 を生成。bit 列→soft 変換へ変更 |
| `sdk/include/os32/os32_kapi_shared.h:61` | 現行 DB_TYPE_FLOAT の payload は i32。今回もこの公開形式を維持 |
| `lib/kmath.c:12` | fabs(double) も対象。部品内の FP 呼出し先を含めて soft ABI を閉じる |
| `lib/sqlite3/os32_sqlite_vfs.c:655` | xCurrentTime は double の Julian day を書く。VFS も soft でコンパイル |
| `lib/sqlite3/sqlite3.c:27144` | libc gmtime/strftime を使う短縮 currentTimeFunc は OMIT_DATETIME 時だけ |
| `build/programs.mk:419` | 単体版 SQLite は別の userland ビルド。newlib と通常 x87 ABI を維持 |
| `userland/tests/fp_stop.c:24` | FNSTENV/FLDENV も必須。`:31` は CW と非空 stack を用いる停止/復帰試験 |
| `arch/x86/kernel/isr_stub.asm:244`、`:354` | #NM は現在共通例外へ。成功時 IRET に戻る専用入口を設ける |
| `exec/access_walk.c:8` | owner/USER/書込み権限を検査する as_access_page を emulator のアクセス基盤に使う |

- `/home/hight/os32-v3/build/out` の11月12日既存 ELF を読取り測定。worktree の最新ビルド合格を意味しない。
- kernel.elf: **17命令** = x87.c の7 + slot_step_result の5 + os32_sqlite_column_double の3 + call/stub の2。
- sqlite.elf: **582命令**。完了条件は両 ELF と他カーネル部品で **x87.c 以外0**（FPU 有りでも同じ）。
- nano 原本: `/home/hight/opt/src/build-newlib-nano/i386-elf/newlib/libc.a`、静的 x87 命令 **1219**。
- 既存 userland ELF 141個: **660**、うち fp_stop.elf **36**、sqlite_standalone.elf **616**。archive と最終 ELF は重複集計しない。
- 命令別/関数別/各 SHA256 は同ディレクトリの `X87-INVENTORY.r2.json`。objdump -d の命令 byte 行だけを数えた。
- nano に FLDT=15/FSTPT=12、FXAM=10。userland に FNSTENV=2/FLDENV=2。これらを省く設計は不可。
- 調査集合の FSQRT/FSIN/FCOS/FPTAN/FYL2X 等は0。FSQRT は基本機能として実装、超越関数は今回対象外。
- これは未取得の apps/game や将来の libm の保証ではない。完成時の全配布 ELF/archive の再棚卸しを受入に含める。

## 2. 起動時の存在判定、CR0、表示

- `arch_boot_after_paging()`→`x87_probe()` を一回だけ呼ぶ。IDT/#NM 入口を準備し、IF を保存して禁止する。
- CR0 は元値を保存し、probe 中だけ EM/TS を落とす。NP21/W に EM=1 を強制されても停止しない経路を用意する。
- probe は **AX=0xffff → FNINIT → FNSTSW AX → status==0 の場合だけ FNSTCW memory** の順。
- AX は inline asm の入出力 operand とし、最適化で sentinel 初期化が消えない制約にする。
- control の初期値も0xffff、判定は status==0 かつ `(control & 0x103f)==0x003f`。FWAIT/FINIT は挿入しない。
- probe の3命令に限定した EIP→復帰 label を定義。CPL0/#NM/boot probe active が全て一致した時だけ absent として復帰。
- 任意の CPL0 fault を握り潰さない。成功・失敗双方で probe active を解除し、元 IF を復元する。
- 386 では CPUID 不要。NE の有無は整列済み stack で EFLAGS.AC の変更可否を調べ、元 flags を復元する。
- CPUID をログに使う場合だけ ID bit 可変を先に確認。CPUID.FPU を唯一の判定根拠にしない。

| CPU/確定状態 | EM | MP | TS | NE/例外 |
|---|---:|---:|---:|---|
| 386+387 | 0 | 1 | 0 | 予約 bit 不変、既定 CW 全 mask |
| 486+ hardware | 0 | 1 | 0 | NE=1、非 mask 演算エラーは #MF |
| 386 no-FPU | 1 | 1 | 1 | NE に触らず software 処理 |
| 486+ no-FPU | 1 | 1 | 1 | NE=0 要求、software 処理 |

- no-FPU の MP=TS=1 は **FWAIT も #NM で捕まえるため**。r1 の MP=TS=0 のままでは deferred 例外を FWAIT で通知できない。
- hardware の TS=0 は eager な協調切替を維持。absent で kernel の save/init が実命令を出さないことが必須。
- NE は従来明示設定していなかった。486DX では FERR# 外部 IRQ から #MF への通知変更として受入試験する。
- 387 外部エラー配線は CW 全 mask の現行利用を前提に別段へ。非 mask の387実機互換まで今回合格とはしない。
- `arch_fp_present()` は内部 getter のみ。**sys_fp_caps の新設、sdk/kapi.json の追加、公開 KAPI ABI 変更はしない**。
- ver 私有 snapshot→CPU 行に **FPU=hw / FPU=none(emu)**。アプリは問い合わせず既存 x87 ABI で動く。
- PPC 表示は hardware 固定。x86 の AC/CR0/probe を共通層から呼ばない。
- メーカー根拠: [Intel 80386 PRM §11.1](https://www.ardent-tool.com/CPU/docs/Intel/386/manuals/prref386/s11_01.htm)、[Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) Vol.1 x87/Vol.2 各命令/Vol.3 #NM・CR0。

## 3. 同梱 SoftFloat 3e の取り込みと SQLite

### 3.1 取り込む原本・ビルド単位

- 取得元は `/home/hight/np21w-src/src/i386c/ia32/instructions/fpu/softfloat3/`。公式最新版を別途取り直さず、NP21/W の固定版を使う。
- 以下、`F/` は `src/i386c/ia32/instructions/fpu/`、`S/` は `F/softfloat3/`、`E:行` は `F/fpemul_softfloat3.cpp:行` の略。
- NP21/W HEAD は `a535090400660e8b37350d91aa959a9a2442b7ba`。調査対象の git status は clean、行番号は CP932→UTF-8 表示の元行番号。
- 同梱品は `S/COPYING.txt:2` の **SoftFloat Release 3e**、BSD 3条項。NP21変更を含むため「公式3e無改変」とは記録しない。

| 用途 | 選択する関数/原本 | OS32 の置き場・境界 |
|---|---|---|
| SQLite binary64 | `f64_add/sub/mul/div`、`f64_eq/lt/le/isSignalingNaN`、`i32/ui32/i64/ui64_to_f64`、`f64_to_{i32,ui32,i64,ui64}` | `lib/softfloat/`。GCC helper 経由、必要なものだけ link |
| SQLite float変換 | `f32_to_f64`、`f64_to_f32`、必要なら f32 四則/比較/整数変換 | nmで実要求を確定。未要求の演算はリンクしない |
| x87 数値本体 | `extF80_add/sub/mul/div/sqrt/roundToInt/eq/lt/le/isSignalingNaN` | 同じ `lib/softfloat/` の本体側subset |
| x87 load/store | `f32/f64_to_extF80`、`extF80_to_f32/f64`、`i32/i64_to_extF80`、`extF80_to_i16/i32/i64` | `x87_emu_convert.c` から呼ぶ。i16版は `S/extF80_to_i16.c:37` のNP21追加を保持 |
| 共通内部依存 | 選択rootが呼ぶ `s_*`、round/pack、NaN、integer primitive、`softfloat_raiseFlags.c`、`softfloat_state.c` | source/includeとundefined symbolの閉包を生成し固定リスト化 |
| 共通header | `softfloat.h`、`softfloat_types.h`、`internals.h`、`primitiveTypes.h`、`primitives.h`、`specialize.h` と選択依存 | vendor原本を保存、OS32 port差分を明記 |
| 移さない計算 | f16/bfloat16/f128、使わないFMA/rem、SoftFloat 2 の `F/softfloat/` | 宣言がheaderにあっても未使用objectを入れない |

- 型は同梱の `sw_float32_t{v}` / `sw_float64_t{v}` / `sw_extFloat80_t{signif,signExp}`（softfloat_types.h:54/:55/:81）。native float/long double に置換しない。
- r2 の not-FAST_INT64/extF80M への変更方針を撤回し、移植元と同じ **SOFTFLOAT_FAST_INT64 と値渡しextF80 API** を基準にする。
- 同梱 platform.h:43 **と** softfloat_types.h:41 の両方に定義がある。一方だけ変更して異なるABIを作らない。
- FAST_INT64 は64bit CPU必須という意味ではない。32bitで生成される整数helperを自前実装で満たし、nmで閉包確認する。
- LITTLEENDIAN は x86 port 内だけに固定。PPC の既存flags/型へ横流ししない。structのpaddingはwire/108byteへコピーしない。
- platform.h:71 の builtin CLZ は portで無効化し、選択した整数 primitive を使う。`__clzdi2` や128bit compiler runtime の意図しない導入を防ぐ。
- SQLite関連は GNU89、移植した命令層/helperは GNU11。どちらも `-march=i386 -msoft-float -mno-fp-ret-in-387`、libgcc/host libc/libmなし。
- vendorのC++化はしない。Eは `.cpp` だが移植対象の処理をC関数として切り出せる。boolはstdbool、型変換は明示し、C++ runtimeを導入しない。
- 計算本体のsource/port規則はSQLiteとx87で共有し、kernelとsqlite.modへ必要subsetを別リンクする。部品の寿命/既存importsを壊す共通常駐APIは新設しない。
- 二つのcodeコピーと各mode globalsを容量表に計上する。vendor全ファイルをwildcardでリンクせず、選択リストとmapを照合する。

### 3.2 GCC の補助関数の薄い包み

| 自前 helper | SoftFloat 3e の呼出しと戻り値 |
|---|---|
| `__adddf3/__subdf3/__muldf3/__divdf3` | 引数doubleのbitをunionで `.v` へ、`f64_add/sub/mul/div`、結果bitをdoubleとして返す |
| `__negdf2` | binary64 sign bitをXOR。C doubleの算術演算を書かない |
| `__eqdf2/__nedf2` | NaNまたは非等値なら非0、等値（+0/-0を含む）なら0 |
| `__ltdf2/__ledf2` | 大小に-1/0/+1、unorderedは+1。quiet NaNの判別を先に行う |
| `__gtdf2/__gedf2` | 大小に-1/0/+1、unorderedは-1。前行と同じ実装にしない |
| `__cmpdf2/__unorddf2` | cmpはunorderedを+1、unordはNaNなら1。実生成要求の署名/返値を対照する |
| `__fixdfsi/__fixdfdi/__fixunsdfsi/__fixunsdfdi` | `f64_to_i32/i64/ui32/ui64` に minMag を指定。符号と範囲外は各specialize定義で検証 |
| `__floatsidf/__floatdidf/__floatunsidf/__floatundidf` | `i32/i64/ui32/ui64_to_f64`。32/64bit引数を取り違えない |
| `__extendsfdf2/__truncdfsf2` と sf群 | `f32_to_f64/f64_to_f32` 等。実際に要求されたものだけ同じbit方式で実装 |

- 置き場は `lib/softfloat_abi.c`。bit unionの出入り以外にC double演算/比較/castを置かず、同じhelperへの自己再帰を禁止する。
- helper自身もsoft戻り値ABIで組む。単に関数名を同じにして整数関数を充てず、GCC生成callerとstack/EDX:EAX/署名を照合する。
- NetBSD softfloat-for-gcc はPMが挙げた追加候補。今回原本確認していないためコードは取り込まず、この薄い包みはOS32自作と記録する。
- 64bit整数 helper は `lib/divdi3.c`/`lib/kmath.c` 等で解決し、足りないものは整数実装する。`__*` という名前だけでlibgcc由来と判定しない。
- `specialize.h:48` は tininess-after-rounding、`:55` はNP21追加のi16 indefinite、`:72` はi32 indefinite。NaN/overflowを含め原本の意味を試験する。
- mode scopeは roundingMode/detectTininess/exceptionFlags/extF80_roundingPrecision の4値を保存復元。SQLiteはnearest-even、x87はCW指定で毎操作設定する。
- kernelとmoduleはmodeを別所有。IRQ/再入なしを維持し、グローバル値をアプリ間に漏らさない。TLSは導入しない。

### 3.3 SQLite の境界と日時（r2 の決定を維持）

- SQLiteはFPU有りでも常にsoft一系統。OMIT_FLOATING_POINT/OMIT_DATETIMEは定義せず、REAL拒否やAtoFのOMIT修正は行わない。
- sqlite3.o、生成VFS.o、os32_sqlite_module.o、部品kmath.o/fabsをすべて同じsoft flagsで組む。CFLAGS_BASEのwrapperを取り残さない。
- `sqlite_imports.def:298` を `column_f64_bits/result=u64/stub=0ULL` にし、gen_sqlite_imports.pyでbit codecを生成する。
- moduleは内部double→bit union、outer/header/call/stubはu64。kapi_db_helpers.incは `softfp_f64_to_i32_trunc(bits)` を使いresident doubleを除去する。
- 既存DB_TYPE_FLOATのi32 payloadは維持。非有限/範囲外はINT32_MIN、SQLite内部のREAL保持と公開KAPI変更を混同しない。
- 内部facadeのOPS世代を更新し旧部品混在を拒否、clean→all。公開KAPI追加は不要、生成物は手編集しない。
- sqlite3.c:27144のlibc版currentTimeFuncはOMIT_DATETIME時だけで今回含まれない。OMIT_LOCALTIME=1は維持する。
- 通常current_timestamp→datetimeFunc→sqlite3StmtCurrentTime→VFS（os32_sqlite_vfs.c:655）のsys_time整数秒をsoftでJulian dayへ変換する。
- time/gmtime/gmtime_r/strftime/localtimeのlibc依存をmap/nmで禁止し、固定時刻でSQLのdate/time/julianday/strftimeを実動確認する。
- programs.mk:419のuserland単体SQLiteはnewlib・通常x87 ABIを維持し、no-FPUでは命令エミュレータで動く。

## 4. NP21/W の BSD x87 実装を移植する

### 4.1 方針と許諾の確認

- r2の「原本が無いので命令処理を全面自作」を撤回。**Eの命令処理をCへ移植**し、CPU/メモリ/例外境界をOS32へ付け替える。
- E:2/5/14 は NONAKA Kimihiro (2012) の著作権/BSD 2条項/免責、`:27` はSimK改変表示。
- `src/LICENSES/LICENSE-I386C-FPU.TXT:32` はsoftfloat系、`:35` NONAKA、`:36` SimK (2018–2025) を列記。同書の `.c` 表記と実際の `.cpp` を対応記録する。
- `F/fpemul_softfloat.c:1` はBSD 2条項のSoftFloat 2版。実数変換等のCへの移し方の副資料とし、SoftFloat 2本体を混ぜない。
- `F/fp.h:2` はNONAKA (2003)、`F/fpumem.h:2` はSimK (2018)。各冒頭の許諾を保持し、総括LICENSEの年で上書きしない。
- `F/fpemul.c:2` の手元原本はNONAKA (2012)。PM追記の2003と差があるため原本値を台帳に記録し、host-double演算本体は取り込まない。
- この調査ではGPLのfpemul_dosbox.c/fpemul_dosbox2.cを開かない。fp.h内のDB/DB2宣言を見ても対応実装を取り込まない。

### 4.2 関数ごとの移植表

- 置き場の短縮名は `arch/x86/kernel/x87_emu_<短縮名>.c`。表の「移植」は計算/命令の再利用を指し、CPUマクロの無変更コピーではない。
- 同系関数を一行に列記するが、各行番号はEに実在する関数先頭。移植しない機能も後表で明示する。

| 手本の関数（E:行） | 置き場 | 移す処理 / OS32で付け替える所 |
|---|---|---|
| FPU_MakeConst:113、FLD1:641/FLDL2T:645/FLDL2E:649/FLDPI:653/FLDLG2:657/FLDLN2:661/FLDZ:665 | state | RC別constant bit表を保持、FPU_STAT.roundをCW読出しへ、pushは作業contextへ |
| FPU_SetCW:121、FPU_FLDCW:475 | state | CW/RC対応を保持、cpu_vmemoryread_wを検証済みbufferへ、SoftFloat設定は操作scope内 |
| FPU_BeginPrecisionControl:147 / EndPrecisionControl:169 | arith | PC→32/64/80を保持。PC=01は手本同様80（r2の24から変更、予約値の互換保証はしない） |
| FPU_SoftFloatBegin:174 / End:179 | exception | flags clear/変換/ORを移す。旧globals復元、DE/C1/ES/Bと非mask判定をOS32側で補う |
| c_float_to_floatx80:191 / c_double_to_floatx80:195、floatx80_to_c_float:209 / to_c_double:213 | convert | native float/double引数/返値とpointer type-punは撤去。u32/u64 bit→sw型→extF80に置換 |
| int16_to_floatx80:199 / int32:202 / int64:205 | convert | 符号拡張とi32/i64_to_extF80を移す、native FP不要 |
| floatx80_to_int16_np2:217 / int32_np2:220 / int64_np2:223、同無suffix:226/229/232 | convert | extF80_to_i16/i32/i64 + RC/exactを移し、重複wrapperは一元化 |
| floatx80_to_int16_round_to_zero:235 / int32:238 / int64:241 | convert | minMag変換は必要時のみ。FISTTP opcodeは今回公開しない |
| floatx80_round_to_int:245、add:249/sub:252/mul:255/div:258/sqrt:277 | arith | extF80計算へ薄く転送。native doubleに落とさない |
| floatx80_eq:261/lt:264/is_nan:267/is_inf:272 | compare | extF80比較/bit分類を移し、unsupported encodingの入口判定を追加 |
| fpu_check_NM_EXCEPTION:288 / 2:295 | dispatch | 本体を移さない。実CPUの#NMで既に入っており、再度#NMを起こさずowner/CPL/EM確認へ |
| fpu_checkexception:302 | exception | SWとCWのpending条件を手本に、EXCEPTION(MF)を結果enum→既存アプリ回収へ |
| FPU_FLD80:313 / FPU_FLD_F80:326、FPU_ST80:383 / FPU_FST_F80:396 | convert | 10byteのsignif/signExp転送。REG80/union aliasを明示LE codecへ |
| FPU_FLD_F32:318 / F64:322、FPU_FST_F32:388 / F64:392 | convert | sw f32/f64⇔extF80。memoryread_f32/f64のnative型を使わずbit入出力 |
| FPU_FLD_I16:330 / I32:334 / I64:338 | convert | 検証済みsigned整数を読み、extF80へ。読込み失敗時はpushをcommitしない |
| FPU_FLD_F32_EA:369 / F64_EA:372 / I32_EA:375 / I16_EA:378 | convert | メモリoperand変換を移す。第9register=reg[8]は命令ローカルtempへ |
| FPU_FST_I16:400 / I32:406 / I64:412 | convert | 変換→例外判定→全頁検証→store/popへ順序変更。直接書込み後の例外判定を移さない |
| FPU_FINIT:446 / SF_FPU_FINIT:464 | state | 前者はユーザーFINITでregister内容を保持、後者のゼロ化はOS所有context新規作成だけ |
| FPU_GetTag:481 / FPU_SetTag:579 | state | 2bit TWのpack/unpackを108byte codecへ。physical tagとlogical ST順を区別 |
| FPU_RecordInstruction:496 / IsControlInstruction:503 / RecordInstructionIfNeeded:523 | state | CPU_PREV_EIP/CSをfault frameへ、FOP構築とcontrol命令のpointer不更新を保持 |
| FPU_EnvRead8:531/16:535/32:539/64:543、EnvWrite8:547/16:551/32:555/64:559 | memory | cpu_vmemory*をOS32全頁検証bufferへ。ENV入出力自身でFDPを上書きしない |
| FPU_prepush:597 / push:606 / pop:611 | state | TOP回転/tag更新を移す。Validだけを見る満杯判定は全non-emptyへ直し、mask/commit規則を追加 |
| FPU_FST:626 / FPU_FXCH:630 | state | 値とtagのcopy/swapを移す。空stackの例外を一元化 |
| FPU_FADD:673 / FMUL:682 / FSUB:691 / FSUBR:700 / FDIV:709 / FDIVR:718 | arith | operand順・precision scope・flagsを移し、結果を作業contextへ。非mask時のcommitを分離 |
| FPU_FADD_EA:727 / FMUL_EA:730 / FSUB_EA:733 / FSUBR_EA:736 / FDIV_EA:739 / FDIVR_EA:742 | arith | reg[8]参照をdecoded memory operandへ。整数メモリ形式も同じ計算経路 |
| FPU_FSQRT:882 / FPU_FRNDINT:891 | arith | extF80 sqrt/roundToIntをそのまま数値本体に使用しPC/RC/flagsを保存 |
| FPU_FCHS:965 / FPU_FABS:968 | state | byte[9]直接操作をsignExpのbit操作へ。padding/endian依存を除去 |
| FPU_Compare:973 / FCOM:997 / FUCOM:1014 / FCOM_EA:1021 / FTST:1024 | compare | C0/C2/C3、ordered/sNaNを移す。Special tag即invalidはinf/NaN/denormal分類へ修正 |
| FPU_FXAM:1080 | compare | sign/C1と分類を移す。unsupported/pseudo形式もIntel仕様と実x87で補完 |
| FPU_FSTENV:1120 / FPU_FLDENV:1179 | state | protected-mode14/28byteを移す。旧CW保存後mask、副作用とpointer復元を保持、real/V86分岐は除外 |
| FPU_FSAVE:1232 / FPU_FRSTOR:1246 | state | ST(i)順80byte＋環境を移す。restore中の読込みで保存FDPを潰さない処理を保持 |
| EA_TREE:1398 | decode / arith | ModRM.regの算術/比較選択を移す。EA計算はOS32 decoder、clock/trace削除 |
| SF_ESC0:1454 | decode | D8 のST/m32算術・比較のcaseを移す |
| SF_ESC1:1524 | decode | D9 のload/store/stack/CW/ENV/定数を移し、対象外超越関数caseを拒否へ |
| SF_ESC2:1804 | decode | DA のm32整数算術/比較とFUCOMPPを移し、FCMOVを除外 |
| SF_ESC3:1867 | decode | DB の整数/80bit load/store・FINIT/FCLEXを移し、FCOMI/FCMOV/FISTTPを除外 |
| SF_ESC4:1996 | decode | DC のST/m64算術と逆演算のoperand順を移す |
| SF_ESC5:2065 | decode | DD のm64/FFREE/FUCOM/ENV/SAVE/RESTOREを移す、FISTTP除外 |
| SF_ESC6:2166 | decode | DE のm16整数算術/比較・pop形式を移す |
| SF_ESC7:2239 | decode | DF の整数変換・FNSTSW AXを移す。AXはsaved EAX上位16bitを保持、BCD/FCOMI/FISTTP等を除外 |

### 4.3 移さない物とOS32固有の入口

| 手本・機能 | 移さない理由 / 代替 |
|---|---|
| E:745 FPREM_partial、:773 FPREM、:793 FPREM1、:926 FSCALE、:1098 FXTRACT | r2棚卸しの対象外。将来必要なら別試験とともに移植 |
| E:818 FSIN/:832 FCOS/:846 FSINCOS/:861 FPTAN/:875 FPATAN/:896 F2XM1/:908 FYL2X/:912 FYL2XP1 | sin/cos/pow/log等のhost libm/native double経路。今回は取り込まずunsupported命令として診断 |
| E:342 FBLD / :418 FBST | BCD対象外。SoftFloatだけでdecimal互換を満たしたとしない |
| E:1000 FCOMI/:1017 FUCOMI、FCMOVB:1030/E:1036/BE:1042/U:1048/NB:1054/NE:1060/NBE:1066/NU:1072 | i386配布命令の対象外。CPU_EFLAG直接更新を導入しない |
| E:569 GetTag8/:588 SetTag8、:1270 FXSAVE/:1311 FXRSTOR/:1349 SF_FPU_FXSAVERSTOR | SSE/MMX/FXSAVEと圧縮tagは今回不要。XMM/MXCSR/CR4を移さない |
| E:564 FPU_RealLinear、ENVのreal/V86分岐 | protected-mode userだけを対象。V86入口は既存経路へ |
| F/fp.h:41 のDB/DB2宣言、:79のNOFPU宣言 | 別backendの選択器は不要。SF_ESC等の必要宣言だけOS32型へ |
| F/fpumem.h:37 のread_b/w/d/q/f、:58 のwrite群 | 幅と80bit転送の手本。NP21のmemory実装そのものは移さずuser page検証で置換 |
| fpumem.h:52/:55/:73/:76 のf32/f64 native型API | u32/u64 bit APIへ置換。ホストのFP ABIやalias依存を持ち込まない |
| CPU_WORKCLOCK、TRACEOUT、GET_MODRM_PCBYTE、calc_ea_dst、CPU_*、FPU_STAT.mmxenable、compiler.h/sse.h | clock/UI/仮想CPU依存を削除。OS32のtrap frame/decoder/context/検証済みoperandへ |

- 起点は§2の実#NM。移植元はCPUをエミュレートするため自分で#NMを発生させるが、OS32内でそのcheckを呼ぶと無限再入になるので除く。
- active software stateは8本の80bit、CW/SW/TW/FIP/FDP/FOP。保存域は既存108byte、align1、28byte環境＋ST0..7各10byteのFNSAVE形を維持する。
- CPUのreg[8]、mmxenable、cycle計数は保存域に足さない。sw_extFloat80_tのpaddingを含む構造体copyはせずLE codecを使う。
- #NM入口でPUSHAD/元segmentを保存し、CPL3・VM=0・正規owner・!present・EMを確認。CPL0は限定probe復帰以外をemulateしない。
- decoderは15byte上限、必要byteだけfetch。66/67/segment override、ModRM/SIB、16/32bit EA、SS既定、segment limitを検査する。
- `as_access_page`で全頁USER/owner/RW、overflow、lease失効を確認。最大108byte bufferでread/writeを分離し、MMIOへ書かない。
- 命令全体の作業context/出力/GPRを作り、例外別のcommit規則適用後にmemory→state/GPR→saved EIPを反映。成功だけ次命令へIRETする。
- 直接memwriteする元FST/SAVE、push後に読む元FLDを機械的移植しない。失敗時の部分store/pop/TOP更新を防ぐ。
- maskedは結果とstickyをcommit、unmaskedは例外ごとの抑止/完了とES/Bを記録、次のwaiting命令/FWAITでsoftware #MF相当へ。
- no-waitのFNSTSW/FNSTCW/FNSTENV/FNSAVE/FNCLEX/FNINIT等はpending処理を分ける。MP=TS=1によるWAIT trapとNP21環境差は§2/§8を維持。
- Begin/EndだけではDE、C1丸め方向、stack fault、unsupported ext80、pending/commit優先順位が不足する。再利用範囲とOS32の補完を明記して対照試験する。
- native x87 oracleを最終基準にし、移植元と一致しただけで正しいとしない。対象外命令は配布ELF監査で検出し、必要なら追加実装してから配る。

### 4.4 THIRD_PARTY.md に追加する記録の形（今回リポジトリには書かない）

| 部品 | 版 | ライセンス | 置き場 | 改変 | 原文の所在 |
|---|---|---|---|---|---|
| NP21/W x87命令処理 | HEAD a535090400660e8b37350d91aa959a9a2442b7ba | BSD-2-Clause | arch/x86/kernel/x87_emu_*.c | Eの命令処理をCへ分割、CPU/memory/exceptionの置換、対象外機能削除、精度/commit修正。対応表はUPSTREAM.json | arch/x86/kernel/x87_emu_vendor/LICENSE-I386C-FPU.TXT と各移植ファイル冒頭 |
| Berkeley SoftFloat（NP21/W同梱） | Release 3e、同じNP21/W HEAD | BSD-3-Clause | lib/softfloat/ | NP21のsw型名/i16変換等を保持、OS32 port/選択buildを追加。公式無改変とはしない | lib/softfloat/COPYING.txt と各原本冒頭 |

- `x87_emu_vendor/UPSTREAM.json` は source_root、git_head、source_file、line_start/end、raw_sha256、encoding=CP932、destination、change_summary を各移植片に記録。
- `lib/softfloat/UPSTREAM.json` は selected_sources/headers/license と元hash、patches、生成symbol閉包、除外一覧を記録。全体HEADだけで部品hashを代用しない。
- x87の主原本E SHA256: `379f111c301196709f83099ad0176561162bca36c198a5e3ea882a18ff7b453b`。
- SoftFloatのCOPYING.txt SHA256: `145ea96b4a4a04a1a7738d2a2bf9e830f861971e69606187b018d9e8fc0b95c7`。
- LICENSE-I386C-FPU.TXT SHA256: `f388292a2c1c71cb84f5495bd7a6770c8fc7237ea098ab237aa15043a2eeba12`。
- 原本byteのhashを先に取り、`iconv -f CP932 -t UTF-8`による表示/移植の変換後hashと区別。改行/文字コード変換も変更記録に残す。
- 著作権は原ファイルごとに保持し、SimKの2018–2025表示とNONAKAの2012/2003表示を一つの年にまとめない。分割した派生ファイルにも許諾全文を残す。
- バイナリ配布へ両ライセンスの全文・著作権・免責を同梱し、BSD-3のendorsement禁止も保持。THIRD_PARTYの短い一覧だけで済ませない。
- GCC helper/OS32 decoderの新規部分は自作と明記し、再利用したNP21部分との出典を分ける。今回の成果物は記録案であり、導入済みとは表示しない。

## 5. 保存・復元と割込み境界

- present: 現在の FNSAVE/FWAIT、FRSTOR、FNINIT を x87.c の guard 内で維持。
- absent save: active contextを108byte imageへ保存し、FNSAVE同様に環境/tagを初期化（register内容は保持）。restoreはimageからactiveへ復元、OS新規initだけ全域をゼロ化する。
- ユーザー FNSAVE/FRSTOR も同じ codec を使う。66 prefix の94byte/14byte環境は別 encode/decode で扱い、AppSlot は108byte固定。
- FNSTENV は保存後 CW 全 mask、FLDENV は環境だけ復元。既存 fp_stop の往復と同じ副作用を実装する。
- 親子 launch、park/resume、異常終了は既存 arch_fp_* 呼出しを使う。emulator 専用の別 AppSlot や所有者表を新設しない。
- 通常 KAPI では保存追加不要。SQLite は常時 soft なのでユーザーの hardware/software x87 stack/CW を触らない。
- SQLite と emulator の SoftFloat mode globals は各リンク単位内で scope 保存復元し、IRQ/再入は禁止。emulator は毎命令 CW から再設定する。
- CPL0 の probe guard と CPL3 の emulation を別入口条件にし、kernel の誤混入 x87 が動いてしまう抜け道を作らない。

## 6. 容量・速さ

| 対象 | 基準実物/設計予算/受入 |
|---|---|
| kernel.elf | 既存731852byte（symbol込み）。常駐 section 増分は soft計算+emulator 約60–160KiBを仮置き |
| sqlite.elf / sqlite.mod | 既存518780 / 396280byte。f64 subset/helper 増分20–80KiBを仮置き、重複分も数える |
| vmkernel.lz4 | 既存337365byte、上限520192byte（boot/boot_defs.h:116）。差182827byteだが未圧縮増分とは比較しない |
| 起動2HD FD | 1261568byte、FAT/cluster込み、tools/tests/test_packages.py:82 の空き64KiB以上 |
| RAM | AppSlot の108byteは増やさない。active108byte、作業state/buffer、SoftFloat globals/stackを別計測 |
| SQLite pool | 既存512KiB。code/work stack の追加をこの値に含めた扱いにしない |

- 数値は既存成果物と設計用の幅。新規 emulator/圧縮率/FD の実測値ではない。全配布 image と owner pages を統合時に測る。
- hardware アプリは保存 guard 以外の追加計算なし。soft SQLite は有り/無し双方で演算単位が遅くなる。
- no-FPU アプリは1命令1trapとSoftFloat演算で hardware の数十倍以上も想定。SQL/printf全体の倍率は実測する。
- 固定clock/媒体/DBで起動、1000検索/学習、REAL集計、fp_stop Newton、nano実数printfを各5回測り中央値/最悪を記録。
- 508KiB/FD余裕を超えたら容量未達として PM へ差分を出す。勝手な機能削除や上限引上げで通さない。

## 7. 一機能・一ファイル・一担当の実装段

- Aは型/意味の契約確定、BはA後に別ファイルで並列、Cは結合後。生成物は生成担当が一括所有し手編集しない。
- 下表の新規 x87_emu_* は `arch/x86/kernel/` 配下。既存ファイルも同時に複数担当へ渡さない。

| 段 | ファイル | 一機能/受渡し |
|---|---|---|
| A1 | `arch/x86/kernel/x87_emu.h`（新） | trap/decode/state/結果型と commit 契約 |
| A2 | `lib/softfloat/UPSTREAM.json`（新） | NP21同梱3eの原本/hash/選択閉包/port差分、vendor配置とCOPYINGは同担当 |
| B1 | `lib/softfloat_abi.c`（新） | GCC helper、soft flags で自己再帰なし |
| A6 | `lib/softfloat_bits.h`（新） | integer bit API の宣言、公開 SDK には露出させない |
| B2 | `lib/softfloat_bits.c`（新） | u64 bit API と DB 切捨て変換 |
| B3 | `arch/x86/kernel/x87_emu_decode.c`（新） | prefix/ModRM/SIB/length、純粋 decode |
| B4 | `arch/x86/kernel/x87_emu_state.c`（新） | 108byte codec、TOP/TW、環境命令/初期化 |
| B5 | `arch/x86/kernel/x87_emu_arith.c`（新） | 四則/逆算/FSQRT/FRNDINT、RC/PC |
| B6 | `arch/x86/kernel/x87_emu_compare.c`（新） | FCOM/FUCOM/FTST/FXAMと条件bit |
| B7 | `arch/x86/kernel/x87_emu_convert.c`（新） | load/store/int変換、80bit分類 |
| B8 | `arch/x86/kernel/x87_emu_memory.c`（新） | user segment/page検証と二段階copy |
| B9 | `arch/x86/kernel/x87_emu_exception.c`（新） | sticky/pending/mask、例外別commit規則 |
| C1 | `arch/x86/kernel/x87_emu_dispatch.c`（新） | decode→execute→commit、ownerと再入制御 |
| B10 | `arch/x86/kernel/x87.c` | AX probe/限定復帰・CR0・hw/software保存切替 |
| A3 | `arch/x86/kernel/x87.h` | 内部getterとprobe guard宣言のみ |
| C2 | `arch/x86/kernel/boot_after_paging.c` | 一元化した初期化を呼ぶ |
| C3 | `arch/x86/kernel/isr_stub.asm` | #NM専用保存/復帰、元segmentを保持 |
| C4 | `arch/x86/kernel/isr_handlers.c` | probe優先→user emu成功return→既存fault |
| A4 | `tools/prototypes/sqlite_imports.def` | column_f64_bits の wire 定義 |
| B11 | `tools/gen_sqlite_imports.py` | bit codec adapter/OPS世代を生成、全生成物更新 |
| B12 | `lib/sqlite3/os32_sqlite_outer.h` | u64宣言/aliasへ変更 |
| C5 | `kernel/sqlite_outer.c` | 生成整数wrapperを取り込み、型assert |
| C6 | `lib/sqlite3/os32_sqlite_module.c` | bit adapter結合、内部soft ABI型確認 |
| C7 | `kapi/kapi_db_helpers.inc` | double排除、既存i32 payload維持 |
| B13 | `lib/sqlite3/os32_sqlite_config.h` | OMIT不採用、REAL/日時とsoft方針を記載 |
| B14 | `build/sqlite-proto.mk` | 全FP翻訳単位とhelperのflags/閉包を統一 |
| B15 | `build/softfloat.mk`（新） | kernel/module用subset、FAST_INT64統一、整数helper閉包、flags規則 |
| C8 | `build/config.mk` | 新build規則を読込む位置を固定 |
| C9 | `build/sources/order.mk` | 新規resident sourceとABI順序登録 |
| A5 | `kernel/ver_info.h` | 私有snapshotへFPU欄 |
| B16 | `kernel/ver_collect.c` | 確定値を収集 |
| B17 | `kernel/ver_format.c` | hw/none(emu)表示、長さ制限維持 |
| B18 | `tools/tests/x87_emu_host.c`（新） | libcなしの実x87対照・状態/境界fixture |
| B19 | `tools/tests/test_nofpu.py`（新） | 命令棚卸し/ABI/日時/変異runner |
| C10 | `tools/checks/nofpu-host.py`（新） | CHECK inputs/依存宣言 |
| C11 | `tools/tests/test_x87.py` | 既存host試験のprivileged probeをhook化、hw/emu両系 |
| C12 | `userland/tests/fp_stop.c` | 既存check/loopを両系で共通期待、停止復元検証 |
| C13 | `tools/tests/guest_tests.txt` | fp_stop checkは無しでもPASS 3/3、skip/kill期待を禁止 |
| C14 | `docs/09_exec.md` | software state/例外/通常KAPIの契約 |
| C15 | `docs/tasks/DEFERRED_TESTS.md` | 17/582からの除去結果と残る実機検証を記録 |
| A7 | `arch/x86/kernel/x87_emu_vendor/UPSTREAM.json`（新） | 関数別由来/hash/行/派生先と原文LICENSEを保存 |
| C16 | `THIRD_PARTY.md` | §4.4の二部品を登録、配布ライセンスの所在を記載 |

- §4.2の移植先ごとに担当を分け、Cへの分割/境界置換/修正履歴を残す。原本LICENSE/追加headerの所有者も固定し同じファイルを分担しない。
- module世代変更は既定 generator/manifest の原典から再生成し、clean→all で混在を排除する。KAPI追加の段は無い。

## 8. 受入・host/NP21/W・変異

| 受入項目 | 試験 | 実行時REDにする変異 |
|---|---|---|
| 起動probeで停止しない | AX sentinel不変/正常、不正CW、限定#NM復帰 | メモリprobeを先行、guardのEIP条件除去 |
| CR0/NE/WAIT | 386予約bit不変、486DX #MF、no-FPU FWAIT trap | MP/TS片方解除、NE設定を除去 |
| 全FP境界一致 | REAL=1.5/-2.75/大数をmodule→wire→DBへ、stubゼロ | bit castを数値castへ、片側ABIをhard化 |
| 日時がlibc不要 | 固定sys_timeでdate/time/julianday/strftime | VFS時刻を0化、OMIT_DATETIMEを定義 |
| decoder正確 | 全合法形式/15byte/16bit EA/SIB/segmentと拒否例 | length+1、SIB base/SS選択を誤る |
| memory保護 | 跨頁/readonly/非USER/失効owner/overflow | 2頁目検査除去、write権限無視 |
| 80bit/丸め | 実x87と全RC/PC/NaN/denormal/境界値、Special tagのinf比較、定数load | f64経由化、RC/PC map反転、Special即invalid |
| 状態保存 | 非空stack/親子/park/stop/ENV往復、FINITで値保持、Zero/Specialで満杯 | TOP回転逆、tag/pop/FNSTENV mask除去 |
| 例外/再開 | masked続行、pending+FWAIT、bad operand時非commit | EIP更新除去、unmaskedでもstoreする |
| 完全性 | kernel/sqlite/全.modのx87はx87.c以外0、nm/mapでlibgccなし | 禁止命令/依存をfixtureへ注入 |
| userland互換 | fp_stop check=PASS 3/3、単体SQLite REAL、nano printf | FNSTENV未実装化、AX上位破壊 |
| ver/容量 | hw/none(emu)、両FD/508KiB、stack high-water | 表示固定、境界+1byte、余裕検査を無効化 |

- host executable は -nostdlib、独自 _start/exit、libc/libmなし。真のx87側はinline asm、入力/出力bit列と状態を比較する。
- Sandbox の host32 は qemu を使用し、privileged CR0/IDT操作はhookで検査。QEMU対照を「実hardware x87」と呼ばない。
- 実hardware対照は同一fixtureを sandbox外の通常WSLでnative実行して採取する。既存SIGSYS制限を回避できたと仮定しない。
- 変異anchorは一意、実行assert/exit REDを記録。静的命令監査のREDは別列扱い。変更/削除anchorは既存tools/testsから移す。
- NP21/W は **iniでno-FPUを作れる**。NekoProject21 の cpu_fami=3/cpu_mode=0（386）、4/2（486SX）、または cpu_feat bit0=0。
- 手元 source: `/home/hight/np21w-src` HEAD `a535090400660e8b37350d91aa959a9a2442b7ba`。cpu.h:519/:647、system_inst.c:298 を確認した。
- 同版 pccore.c:723 は全設定0だと既定全機能に戻る。再現iniは family/model と明示feature maskを保存し、実効値を読戻す。
- no-FPU MOV CR0 は EM/NEを強制、MP/ETを落とす。要求値と実際のreadbackを区別し、386予約NEのhardware試験と混同しない。
- レビューの「memory FNSTSW/FNSTCWは#NM」は同手元版と異なる。fpdummy.c:31/:109 は0xffffを書き、:144 はAX=0xffff、FNINIT/FLDCWもtrapしない。
- AX-firstと限定probe guardは両挙動で安全。iniのno-FPU判定試験は行えるが、同版では制御命令がOS emulatorへ来ずfp_stopが正常に通らない可能性がある。
- まず対象実行binary/hashと各制御命令のtrapログを採る。この版差を製品の強制absent、命令先読み実行、skip期待で隠さない。
- 全命令/WAITのguest受入には、EM/MP/TSを仕様どおり扱うNP21隔離修正版または実無FPU機を使う。環境修正は別成果物でOS本体へ入れない。
- 標準NP21のini試験、隔離修正版の完全emulation試験、実機の検出/WAIT試験を別結果にし、未実施を合格としない。
- FPU有り（486DX 4/1・bit0=1）もcold bootし、#MF、保存復帰、soft SQLiteを確認。配備前停止、ini原本保存、終了後復元を守る。
- r3では原本/許諾/関数境界を読取り照合した。命令数はr2棚卸しを継承。実装時は指定base preflight、clean→all、変更試験--mutate、lintを最終source後に完走する。
- make check-changed/全mutationは実装者が回さずCI/full-checkへ。NP21/WSL/実機の数値・復帰試験は今回未実行。

## 9. 未決

- **ユーザー判断の未決は0件**。SQLite soft一系統とx87 emulatorは決定済みで、hardware SQLite案へ戻す確認はしない。
- 実装上の残作業: 選択symbol閉包の実測、NP21原本の導入/許諾同梱、C移植、trap適合性と精度/容量/実機受入。原本探索や採用案は未決に戻さない。

## r2→r3 の対応

| 変更点 | 反映箇所 |
|---|---|
| 全面自作から既存BSD実装の移植へ | §4.1 原本/許諾確認、§4.2 全採用関数と置換、§4.3 非採用関数 |
| 公式3e再取得からNP21同梱3eへ | §3.1 sw型/i16拡張/FAST_INT64/port、kernel/moduleで同じ本体を共有 |
| helperの抽象案から具体的な包みへ | §3.2 関数対応/NaN比較/変換/ABI/整数helper閉包 |
| CPU/memory/例外の置換 | §4.2–4.3 trap frame/作業context/検証buffer/結果enum、直接storeや再#NMを禁止 |
| 実物との差を修正 | PC=01は手本同様80、FINIT/FNSAVEとOS新規ゼロ化を区別、host libm経路は除外 |
| 出典と許諾の実装段 | §4.4 THIRD_PARTY追記案/元行/hash/両license、§7に台帳担当追加 |

- r2のユーザー決定、u64 SQLite境界、日時、#NM/WAIT、ver、容量/host/NP21受入は本票へ継承。ユーザー判断の未決は **0件**。


## 確定の前の PM の追記 (2026-11-16、r3 の Opus レビューの P2・P3)

- **#NM の受け口の番地 (P2、§4.3)**: ESP を基にした番地 (例: newlib の _printf_float の `fstpl (%esp)`) は、PUSHAD の ESP (カーネルのスタック) ではなく、特権の切り替えのフレームのユーザー ESP/SS で計算する (r2:160 の記述を戻す)。0x9B (WAIT/FWAIT) は 1 byte の命令として decode する。
- **受入の道 (P2、§7・§8)**: 案 (b) を採る。NP21/W は変えない。FPU の有る NP21/W の構成で、**試験の版のカーネルだけ** CR0 を EM=MP=TS=1 にして全部の x87 命令を #NM でエミュレータに通す (NP21/W の i386c は全部の ESC で EM を見る fpu_check_NM_EXCEPTION、FWAIT は MP と TS で #NM になる fpu.c:430)。標準の NP21/W の 386/486SX の構成は、FNSTSW AX が 0xffff を返し FLDCW が捕まらない (fpdummy.c:44・144) ので、判定の手順の受入 (§2) にだけ使う。試験の版の旗は製品の像に入れない (製品は判定の結果だけで EM を決める)。この段 (試験の版のカーネルの旗と受入の台本) を §7 に 1 段足す。
- **SoftFloat の specialize (P3、§3)**: NP21/W 同梱の SoftFloat 3e は 8086 (x87) 型の specialize (s_propagateNaNF64UI は大きさの大きい方の NaN)。8086-SSE ではない。カーネルの SQLite の f64 は、旧来の x87 (PC=64 の二重の丸め) と最後の 1 bit が違い得る。受入の期待値はこれを前提にする。
- **変異の名前 (P3、§8)**: 「OMIT_DATETIME を定義」の変異は、実際のマクロ名 `SQLITE_OMIT_DATETIME_FUNCS` で書く。
- **x87 の命令の数の検査 (P3、§1・§8)**: 「x87.c と x87 エミュレータの外は 0 件」を恒久の検査にするとき、HAL の float の段 (MMX Pentium 以上で CPU を検査して段を分けるコード) を許す規則の置き場を予約する (その段の設計で足す)。
- **実機 (ユーザー決定)**: FPU の無い実機の受入は延期。延期の台帳に NOFPU-RH を立てる。


## 波 A の実装で設計者が決めたこと (2026-11-16)

- A1: 下記の内部契約を固定する。trap は ASM の生配置ではなく正規化 view、GPR は ModRM 順で ESP/SS は特権切替時のユーザー値、state の register は physical 順、108byte wire は明示 LE codec とする。
- A1: decode は byte-fetch callback で必要 byte だけ読み、各演算は effect に仮書込みする。exception が命令別 commit mask を決め、dispatch だけが全頁検証後に memory→state/GPR→EIP を commit する (pending のある waiting 命令は無変更で MF、非 mask 例外を今回検出した命令は許された副作用と ES/B を保存して次へ進む)。
- A3: `int arch_fp_present(void)` は probe 後の内部 getter、`int x87_probe_nm(u32 saved_cs, u32 saved_eflags, u32 *saved_eip)` は #NM 専用で CPL0・!VM・probe active・3 個の正確な EIP の一致時だけ復帰 EIP を書き換えて 1、それ以外は無変更で 0。
- A4: 波 A から外し B11 に移す。wire 原典・OPS 世代・adapter・全生成物を同じ段で更新し、その ABI 切替時に clean→all を実施する。
- A5: 私有 `VerSnapshot` の末尾に `u32 fpu_present` (1=hw、0=none(emu)) を追加する。収集・PPC の hw 固定値・表示は B16/B17 が実装し、公開 KAPI と snapshot の既存フィールド位置は変えない。sizeof に依存するゼロ化・スタック確保はこの段でも変わり得る。
- A6: `i32 softfp_f64_to_i32_trunc(unsigned long long bits)` だけを内部 integer bit API として宣言する。unsigned long long は既存の u64 bit 境界の C 表記で、非有限・範囲外は INT32_MIN、4 個の mode の保存復元は B2 の責任。
- A2: 同梱 3e の §3 root から i386/gnu89/Os/soft-float の object 未解決 symbol 閉包を採る。platform の builtin CLZ だけを無効化し、両 header の FAST_INT64/LITTLEENDIAN と 8086 specialize を保持、選択原本・全 hash・port 差分・除外一覧・symbol 集合は UPSTREAM.json に固定する (像へのリンクは B15)。
- A7: CP932 原本の関数ごとの行範囲・その生 byte SHA256・派生予定先を記録する。除外関数も destination=null で区別し、原文 LICENSE とファイル冒頭の著作権/許諾を byte 不変で保存する。B/C の派生 C がまだ存在しないことを台帳に明記する。

A1 の宣言 (実装者が使う正本は `arch/x86/kernel/x87_emu.h`):

```c
/* OS32-owned x87 integration contract. No native FP types or packed C ABI.
 * Implementations belong to NOFPU_DESIGN waves B/C; nothing links in wave A. */
#ifndef OS32_X87_EMU_H
#define OS32_X87_EMU_H

#include "types.h"
#include "exec_arch.h"
#include <stdbool.h>

#define X87_EMU_INSN_BYTES 15
#define X87_EMU_MEMORY_BYTES 108
#define X87_EMU_REGISTERS 8
#define X87_EMU_EXT_BYTES 10
#define X87_EMU_ENV_BYTES 28
#define X87_EMU_ENV16_BYTES 14
#define X87_EMU_SAVE16_BYTES 94

enum x87_emu_gpr { X87_EAX, X87_ECX, X87_EDX, X87_EBX,
                   X87_ESP, X87_EBP, X87_ESI, X87_EDI };
enum x87_emu_segment { X87_ES, X87_CS, X87_SS, X87_DS, X87_FS, X87_GS,
                       X87_SEGMENTS };
/* A normalized view, NOT the assembler stack layout. gpr[ESP] and segment[SS]
 * come from the privilege-transition user ESP/SS, never PUSHAD's saved ESP.
 * C3/C4 translate saved registers/segments and write back only committed data. */
struct x87_emu_trap {
    u32 gpr[X87_EMU_REGISTERS];
    u32 eip, eflags;
    u16 segment[X87_SEGMENTS];
};
/* Canonical LE byte arrays; no SoftFloat struct padding enters a save image. */
struct x87_emu_ext80 { u8 bytes[X87_EMU_EXT_BYTES]; };
struct x87_emu_state {
    u16 cw, reserved_cw, sw, reserved_sw, tw, reserved_tw;
    u32 fip;
    u16 fcs, fop;
    u32 fdp;
    u16 fds, reserved_fds;
    /* Physical R0..R7; wire FNSAVE order is logical ST0..ST7 via SW.TOP. */
    struct x87_emu_ext80 reg[X87_EMU_REGISTERS];
};
enum x87_emu_result {
    X87_EMU_OK = 0, X87_EMU_UNSUPPORTED, X87_EMU_MEMORY_FAULT,
    X87_EMU_PENDING_MF, X87_EMU_NOT_USER, X87_EMU_REENTERED
};
/* Result OK includes deferred unmasked exceptions: commit only effects allowed
 * by that exception, set ES/B, advance EIP; next waiting op returns PENDING_MF.
 * Fault/unsupported/pending results leave memory, state, GPR and EIP unchanged. */
enum x87_emu_commit {
    X87_COMMIT_STATUS = 1u << 0, X87_COMMIT_ENV = 1u << 1,
    X87_COMMIT_REGS = 1u << 2, X87_COMMIT_MEMORY = 1u << 3,
    X87_COMMIT_GPR = 1u << 4, X87_COMMIT_EIP = 1u << 5
};
struct x87_emu_decoded {
    u32 offset;                 /* segment-relative EA, not a host pointer */
    u16 memory_bytes;           /* 0 for register/WAIT; at most 108 */
    u8 length, opcode, modrm, sib;
    u8 segment;                /* enum x87_emu_segment, including SS default */
    bool operand16, address16, memory, write, waiting;
    /* 0x9b is a separate one-byte WAIT; ESC is 0xd8..0xdf. */
};
struct addrspace;
struct x87_emu_access {
    const struct addrspace *as; /* borrowed live owner; IRQs held throughout */
    const struct x87_emu_trap *trap;
};
/* Fetch only requested bytes, checking CS limit and all owner/USER pages.
 * Decode uses this callback and the normalized GPRs; it does not mutate them. */
typedef enum x87_emu_result (*x87_emu_fetch_fn)(void *opaque, u32 offset,
                                              u8 *value);
struct x87_emu_effect {
    struct x87_emu_state state; /* speculative copy of the active state */
    u32 gpr[X87_EMU_REGISTERS];
    u32 next_eip, commit;       /* enum x87_emu_commit mask */
    u16 raised;                /* x87 IE/DE/ZE/OE/UE/PE/SF/C1, not SF flags */
    u8 output[X87_EMU_MEMORY_BYTES];
};
struct x87_emu_softfloat_scope {
    u8 rounding_mode, detect_tininess, exception_flags, ext80_precision;
};

enum x87_emu_result x87_emu_decode(const struct x87_emu_trap *trap,
    x87_emu_fetch_fn fetch, void *opaque, struct x87_emu_decoded *out);
/* Validate segment, overflow, every page, owner/lease and read/write access.
 * Write validates the ENTIRE range before any copy; rejects MMIO. No yield. */
enum x87_emu_result x87_emu_read(const struct x87_emu_access *access,
    const struct x87_emu_decoded *insn, u8 *buffer);
enum x87_emu_result x87_emu_write(const struct x87_emu_access *access,
    const struct x87_emu_decoded *insn, const u8 *buffer);
/* new_owner clears all bytes; FINIT/FNSAVE retain register payloads. */
void x87_emu_state_init(struct x87_emu_state *state, bool new_owner);
void x87_emu_state_encode(const struct x87_emu_state *state, u8 *image,
                         bool operand16, bool environment_only);
void x87_emu_state_decode(struct x87_emu_state *state, const u8 *image,
                         bool operand16, bool environment_only);
/* Caller supplies validated input, at most memory_bytes. Each group changes
 * only effect; no store, owner lookup, EIP writeback or nested #NM here. */
enum x87_emu_result x87_emu_arith(const struct x87_emu_decoded *insn,
    const u8 *input, struct x87_emu_effect *effect);
enum x87_emu_result x87_emu_compare(const struct x87_emu_decoded *insn,
    const u8 *input, struct x87_emu_effect *effect);
enum x87_emu_result x87_emu_convert(const struct x87_emu_decoded *insn,
    const u8 *input, struct x87_emu_effect *effect);
enum x87_emu_result x87_emu_state_op(const struct x87_emu_decoded *insn,
    const u8 *input, struct x87_emu_effect *effect);
void x87_emu_scope_begin(u16 cw, struct x87_emu_softfloat_scope *saved);
void x87_emu_scope_end(const struct x87_emu_softfloat_scope *saved,
                       struct x87_emu_effect *effect);
/* Apply instruction-specific suppression/completion (including DE, stack
 * faults, C1 and priority); do not treat all unmasked exceptions alike. */
enum x87_emu_result x87_emu_exception(const struct x87_emu_state *before,
    const struct x87_emu_decoded *insn, struct x87_emu_effect *effect);
/* Sole commit owner. Checks CPL3, !VM, live owner, !present, CR0.EM, no reentry.
 * After policy: memory -> state/GPR -> EIP. Only OK returns via IRET.
 * active is the one cooperative software context; no new AppSlot storage. */
enum x87_emu_result x87_emu_dispatch(struct x87_emu_trap *trap,
    const struct addrspace *as, struct x87_emu_state *active);

#endif
```

A3 の宣言 (`arch/x86/kernel/x87.h` に追加):

```c
int arch_fp_present(void);
int x87_probe_nm(u32 saved_cs, u32 saved_eflags, u32 *saved_eip);
```

## 波 A の取り込みで後の段へ送る項目 (2026-11-16、Opus レビュー)

- B16・B17: `VerSnapshot.fpu_present` は 0 が none(emu)。B17 (表示) だけ先に入ると FPU の有る機体でも none(emu) と出るので、B16 (収集) と B17 は同じ統合の枝で入れる。
- B4: `x87_emu_state` は FNSAVE と同じ 108 byte だが reg は物理順 (R0..R7)。arch_fp_context との受け渡しは必ず codec を通し、memcpy しない (依頼に 1 行書く)。
- B12: 依頼に §3.3 の wire (`column_f64_bits`、u64) を明記する。
