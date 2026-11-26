# CRT の正常終了を newlib に渡す

> 出所: ~/os32-tmp/run/crtexit/DESIGN.final.md (gpt-6.2-astra 起草 r1〜r2、claude-opus-5-5 のレビュー 2 回、確定の直しは PM、実装後の独立レビュー P1 の直し)。行番号つきの参照は往復の版にあり、ここではパスだけにした (POLICY_DEV §9)。
> 状態: **現行 (2026-11-19)** — 確定設計。アプリの CRT が main から戻った時と --help の早期終了で、newlib の後始末 (atexit・stdio の書き出し) をしてから終わる (x86 と PowerPC で同じ約束)。

## r1→r2 の変更

1. errno の静的リンク用に exit スタブを加え、他の host リンクも照合。
2. shell・gshell にも適用し、常駐像の帯域とハッシュを確認。
3. 即時終了への変異を両 arch の host 試験で検出。
4. x86 専用アプリと PPC の nlprobe 引数を使い、親シェルで受入。
5. i386 の GC 後の増分を map の照合項目に追加。
6. libc なしの start リンク試験を追加記載。
7. CHECK、runner、正常対照、終了値の観測点を明記。

## 1. 実物の確認

|対象|現行の経路と根拠|
|---|---|
|x86|通常は crt0.o・crt0_c.o・syscalls.o・help.o、resident は専用 syscalls.o を使用（sdk/target_profile.py、build/config.mk）。ld の `-nostdlib` と明示的な `-lc -lgcc` は併用される（build/config.mk、build/programs.mk）。shell.elf・gshell.elf とも crt0_c.o と resident/syscalls.o を使い、同じ正常終了を通る（build/programs.mk）。SDK にも CRT を配る（build/sdk.mk）。|
|libc なし|ring3_hello・fault・guard は各自の .o だけでリンクし、C CRT も使わない（build/programs.mk）。一方 PPC には start.o を libc・syscalls.o なしで実行する既存試験がある（test_ppc_newlib.py (旧版 4fa2f9be2878)）。test_app_link.py (旧版 12255a2e64ac) と test_kapi_layout.py (旧版 ff44d589cebf) も libc なしで start をリンクする。|
|PPC USER|crt0・crt0_c に syscalls.o・help.o を追加し、アプリ .o、libos32nano.a、-lc、-lgcc の順でリンク（build/ppc-user.mk）。nano は専用アロケータ。|
|ガード|PPC USER でも -lc 自体は許可。link_guard は選択した vendor archive を解決し、実際の map と receipt からアロケータ提供元を検証する（sdk/link_guard.py）。|
|現在の終了|help 成功と main 復帰は x86 で直接 sys_exit（sdk/crt/crt0_c.c）、PPC でも同様（sdk/crt/ppc/start.c）。共通の即時終了は sdk/crt/syscalls.c。|
|newlib|両クロスの include/_newlib_version.h は 4.4.0。各 lib/libc.a の nm で exit.o は `T exit`、`U __call_exitprocs`、`U __stdio_exit_handler`、`U _exit`。__call_atexit.o が前者の関数、findfp.o が後者の変数（B）を定義する。旧版の構造体メンバー `_GLOBAL_REENT->__cleanup` を CRT から参照する必要はない。|

## 2. 案の比較

|案|libc なし|像の大きさ・PPC ガード|
|---|---|---|
|(a) start から exit を強参照|ring3 は影響なし。PPC の start 単体試験は未解決になる。|通常像に exit.o と依存分。既存ガードで検査可能。|
|**(b) 推奨**。start は非公開の終了関数を弱参照し、通常リンクで直接投入する syscalls.o が実装|start 単体は KAPI に直行。ring3 は対象外。syscalls.o はこの構成では libc 用アダプタ。|通常像は (a) と同じ newlib 依存に弱参照の分岐が加わる。両 arch とも既存ガードを通し、receipt は CRT 再ビルドで更新。|
|(c) fflush(NULL) を明示|強参照すると start 単体がリンク不能。|stdio を引き込み、atexit 登録を実行できない。|

- 弱参照を exit 自体に向けると、静的 libc.a から exit.o を抽出できず、stdio を使う像でも後始末が抜ける。(b) は明示投入の .o から exit を強参照する。
- 適用条件は「syscalls.o を使う通常像は -lc も使う」。libc なしのリンク規則に syscalls.o や -lc を足さない。

- i386 の GC 後の目安は exit 41 B、__call_atexit 218 B、__atexit の BSS 4 B。従来 stdio を使わない像だけ findfp.o の BSS が 316 B 増える。

## 3. コードの境目

両 start の .c 内だけに同じ宣言と分岐を置く。kapi・aux の設定と _init() の直後から次の形にする（rc は関数先頭で宣言）。

```c
extern void __os32_libc_exit(int) __attribute__((weak)); /* ファイルスコープ */

/* 起動関数内 */
if (argc > 1 && _is_help_flag(argv[1]) && os32_help_show(argv[0]) == 0)
    rc = 0;
else
    rc = main(argc, argv, kapi);
if (__os32_libc_exit)
    __os32_libc_exit(rc);
kapi->sys_exit(rc);
for (;;) {}
```

共通 syscalls.c には通常の強い定義を加える。_exit は現行どおり KAPI 直行とし、再帰を防ぐ。

```c
#include <stdlib.h>
void __os32_libc_exit(int rc) { exit(rc); }
```

- main の return と help 成功は登録済み atexit → stdio 後始末 → _exit(rc) を各一度通る。アプリ自身の exit も同じ newlib 実装に入る。公開ヘッダ・KAPI の変更なし。

- test_vfs_fd_path.py (旧版 3062ccc08e29) は実物の syscalls.c を GC なしの -nostdlib -static で組む。newlib_errno_host.c の memset・__errno と並べて noreturn の exit スタブを定義し、誤って呼ばれたら試験を異常終了させる。sbrk_crt_host.c は未使用の終了関数が --gc-sections で落ち、test_nano_adapter は -lc で解決する。両者も回帰試験に含める。

## 4. 受入と変異

|触るファイル|区分|変更行数の目安・内容|
|---|---|---|
|sdk/crt/crt0_c.c、sdk/crt/ppc/start.c|変更|各 10 行、help と main の出口を統合|
|sdk/crt/syscalls.c|変更|5 行、exit の強参照|
|newlib_errno_host.c (旧版 bcd1002cf266)|変更|5 行、errno 試験用スタブ|
|test_crt_exit.py (旧版 cbde2d3fbf10)、crt_exit_host.c (旧版 c71fe8503ac4)|新規|約 90 行と 80 行、host32 ビルド・変異と偽 KAPI|
|test_ppc_newlib.py (旧版 4fa2f9be2878)|変更|約 70 行、execute に実物の start.o・syscalls.o を渡す終了試験と start 単体の対照|
|userland/diag/crtexit.c、userland/deploy.yaml|新規・変更|約 20 行と 1 項目、crtexit.bin を収録（build/programs.mk）|
|userland/ppc/nlprobe.c、ppc_newlib.py (旧版 e9a625727f26)|変更|約 20 行と 12 行、引数 crt-return と親シェルの判定。既存の PPC_USER_APPS と KB_ROOT_BINS の nlprobe を使用|

新設 test_crt_exit.py は CHECK 名 crt-exit（host32、tier 2）を宣言。PPC は既存 CHECK ppc-newlib（ppc-cross、tier 2、qemu-ppc）を拡張。各変異は作業用コピーに単独で入れ、同じ runner で原版の PASS、変異の指定箇所での RED を確認する。

|受入項目|CHECK 名・runner|正常対照|一行変異と判定|
|---|---|---|---|
|通常像の解決とサイズ|crt-exit / host32、ppc-newlib / qemu-ppc。製品ビルドの map・guard・receipt も照合|強い終了関数、libc.a(exit.o)、CRT の _exit を確認。i386 の stdio 有無で §2 の増分を比較|① syscalls.c の終了関数の定義名を変更。map の強い定義が消え、main 復帰で後始末も抜ける|
|main の復帰で flush・atexit・終了値を保存|crt-exit / host32 と ppc-newlib / qemu-ppc。両方で実 start・syscalls・newlib、PPC は nano もリンク。偽 KAPI の write と sys_exit を host の出力・終了へ渡す|静的バッファで stdout を完全バッファ化。A を書き B、C の順に atexit 登録して return 37。親が出力全体 ACB と status 37 を検査|② exit(rc) を _exit(rc) にすると status 37 のまま ACB が欠け、両 host 試験が実行時 RED|
|help の早期終了|crt-exit / host32 の偽 KAPI、ppc-newlib / qemu-ppc の start fixture|成功時 main 0 回、終了スタブに 0 が一度。help 失敗時 main 1 回、その戻り値が一度届く|③ help 成功時の代入を直接 sys_exit(0) に変更するとスタブの呼出回数が 0|
|libc なし|同じ両 CHECK の start 単体 runner、§1 の PPC リンク試験、x86 ring3 map|終了スタブなしでリンクでき、偽 KAPI に main の値が届く。ring3 は libc 入力なし|④ 弱属性を外すと start 単体と §1 の libc なし start リンクが未解決で RED|
|製品の出力と終了値|ゲスト受入のみ。x86 は NP21/W、PPC は本線の QEMU（ppc_newlib.py (旧版 e9a625727f26)）|下記台本で改行なしの ACB 3 バイトと終了値 37 を確認|② と同じ欠落。変異の CI 判定は上記 host 側|

x86 は常駐シェルで `/usr/bin/crtexit.bin > /tmp/x` を実行し、直後の `echo $?` が 37、`cat /tmp/x` の出力全体が改行なしの ACB 3 バイトであることを確かめる。PPC は RSH で `/bin/nlprobe.bin crt-return` を実行し、出力全体が改行なしの ACB 3 バイト、直後の `echo $?` が 37 であることを確かめる。

1 段で両 arch の全アプリと SDK 配布物を再生成し、同一アプリの前後の .bin バイト差を記録する。shell.bin・gshell.bin のハッシュも変わる。帯の余裕は shell 約 108 KB、gshell 約 48 KB で、再リンク時に app_sys.ld の ASSERT を通す。x86 は次の節目の受入に合わせる。PPC 関連 4 ファイルを本線の段で変更する旨は本線 PM が PowerPC PM に知らせる。

## 5. 未決

- ユーザー判断が必要な項目は 0 件。

反論なし。

## 確定 (2026-11-18、claude-opus-5-5 の第 2 回、PM が反映)
- N1: PPC の nlprobe の crt-return の分岐は **main の先頭** (N3-float の printf より前) に置き、`setvbuf` (完全バッファ) → `A` → `atexit(B)`・`atexit(C)` → `return 37` だけにする。fflush も N3- の行も出さない (既存の末尾の `fflush(NULL)` を通ると変異 ② がゲストで RED にならず、setvbuf を最初の出力の後に呼ぶことにもなるため)。x86 の新しいアプリも同じ形。
- N2: 新しい host32 の検査の名前は `crt-exit-host`、入口は crt-exit-host.py (旧版 9b0b0056dd4f) (sbrk-crt-host と同じ慣例)。
- N3: x86 の host32 で本物の i386-elf の newlib の libc.a と crt0_c.c をつなぐ土台は前例が無い。実装者の完了条件に、正常対照の PASS と変異 ①〜④ の RED のログを必ず添える。
- N4: x86 のゲストの台本は、userland/deploy.yaml に書く配置先の絶対パスでアプリを呼ぶ。
- PM に残る 1 件: PowerPC の枝 wt/ppc-newlib-fix (97e182cf0) が main に入った後、その上に実装を載せ、nlprobe の argv の mode の並び・ppc_newlib.py (旧版 e9a625727f26) の verdict() の表・変異の目印 `    _init();` (start.c で 1 か所) を照らし直す (依頼に書く)。
- PPC の受入は RSH の出力全体で見る (sh はアプリへの > を拒否する)、2026-11-19 の独立レビュー P1
