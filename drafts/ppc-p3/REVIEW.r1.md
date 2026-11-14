# P3 設計 r1 のレビュー (第 1 回) と決定

## ユーザー決定 (2026-11-14)
- P3 の対象を、常駐 CLI から共通の SH (案 B) に変える。ユーザーの回答は「共通のシェルに変える (推奨)」。常駐 CLI (KBOOT_SHELL) は、PLN1 の退避像として残す。

## Opus 5.5 第 1 回 (Request changes。案 B の向きには賛成)
- 照合: E1〜E7 と E9 は実物と合う。E2 は rshell.c:364 (GUI handoff)・:478 (poll)・:610 (cmd)。rshell_lan.c は SH に組まれるが、--gc-sections で落ちる。E3 は、加えて kapi_target_ppc.py:266 の ZERO_SLOTS (host_notice_serial) と gen_kapi.py:520 の include の除外も対象。E9 の呼び手は exec_finish.inc:208 で、実体は空。
- **E8 は一部が誤り。** USER は MSR[EE]=1 で入る (arch/ppc/exec_enter.S:25)。SH が USER に戻るたびに、DEC から link_tick と net_jiffy が進む。rshell の待ちは毎回 USER に戻る (rshell_lan.c:94 の sys_yield、rshell.c:488 の sys_halt)。欠けているのは前景の仕事で、link の bind・host_mailbox_init (lan_service.c:291-299) と pm_usb_poll が pm_terminal_poll (terminal_input.c:73-78) の中にあり、link 像ではその TU が外れている (ppc-kboot.mk:89)。

| ID | 重さ | 節 | 問題 | 直し |
|---|---|---|---|---|
| R1 | P1 | §3 EE・§6 | syscall の中で EE を開く理由が成り立たない。変異「協調窓の EE 許可削除」は生き残る。I4 で shell_putchar・console_stream に進行口を入れると、console の更新の途中で EE が開く | pm_link_service_once は「EE を変えずに、前景の仕事 (USB の poll、link の bind・reset・PHY) を 1 回」にする。EE の窓・TB の期限・busy の記述は消す |
| R2 | P1 | §3 H7 | 共通の kapi_host_notice.c に「非 link では不在」を足すと、x86 で KBOOT_LINK が未定義 (0) になり、LAN の rshell が不在に落ちる | 共通のファイルは変えない。PLN1 の SH には、platform の側に host_mailbox_stage・commit・latest_serial の NOSYS/0 の代わりを置く。H5・H6・H7 は不要 |
| R3 | P1 | §3 H9/H10 | 進行口が notice・status・入力・出力に散っている | PPC の sys_halt と sys_yield の endpoint の 1 か所にする (rshell の待ちは全部ここを通る)。共通の KAPI 層に触らない |
| R4 | P2 | §3 I1 H10 | ESCC の KAPI の提供 (port 群、約 370 行) と USB/ESCC の調停は、範囲を超える。G4 では ESCC が外に出ていない | P3 は LAN と USB の local だけにする。serial_* は不在 (serial_is_initialized=0)。ESCC は後の段へ送る |
| R5 | P2 | §3 S1 | APP の入口が未定。cmd_rshell_resident は serial (rshell.c:445)・ser_wd・rsh_serial_follow に依存する | 新しい関数 (例 rsh_lan_app_loop) の形を決める。rsh_lan_poll と USB の行の組み立て、exit の返り値までを file:line で書く |
| R6 | P2 | §3 S4 | shell_boot.c:38-41 は、SH が異常終了すると pm_terminal_poll の無限ループに入る。link 像にはその関数が無い | link-SH は fault の後も再起動する (または前景の poll を続けて READY を出し続ける)。どちらかを明記し、§6 の寿命に入れる |
| R7 | P2 | §5 | 1 段 1 ファイルでは、途中で組めない状態ができる (H8 だけでは PLN1 SH の platform_host_* が未定義)。生成器を固定する既存の試験 tools/ppc/test_kapi_binding.py・tools/tests/test_remote_x9.py の更新が段に無い | 約 6 段にまとめる: ①文書 (D1・D2)、②provider (H3・H4・H8・H9・stub・既存の 2 試験・T1)、③前景と入出力 (H1・H2・I1-I4・S4)、④共通 shell (S1-S3、直列)、⑤像 (B1-B3)、⑥QEMU (T2-T4) |
| R8 | P3 | §5 B3 | ROOT_BINS が build/out/ppc-user/sh.bin に固定 (ppc-kboot.mk:47)。別に出力する link-SH の sh.bin を root に入れ忘れる | B2・B3 で ROOT_BINS の出力先も切り替えると明記する |
| R9 | P3 | §1 E3 | 生成器の対象に ZERO_SLOTS の host_notice_serial (:266) と include 群 (:520) が漏れている | H8 に加える |

- 所見: S1・S2 の `#if !defined(SHELL_AS_APP) || defined(SHELL_REMOTE_APP)` は、x86 の常駐シェルと sh.bin では今と同じ結果になる。capture は owner で絞らずに積む (con_sink.c:154-186)。子の出力も OUT に入り、問題は無い。ABI の slot と署名は変わらない。KB_GEN は像ごとだが、PLN1 SH と link-SH は同じ TARGET なので、生成を像ごとに分けるか、R2 の stub かが要る。

## 本線 PM の条件 (2026-11-14)
1. **進行口は 1 つ。** IO_TIER_DESIGN (docs/decisions/IO_TIER_DESIGN.md、N4) で、「PR=1 から入った syscall の中で EE を一時的に許可して戻す」口を arch/ppc/io_pump.c に 1 つだけ置くと決めた。io_pump と LAN の一括転送の待ち (LAN_BULK_DESIGN の J) が共用する。
   - P3 で EE の窓が要るなら、別の口を作らずこの口を使う (あるいは P3 がこの口の最初の実装になる)。r2 でどちらかを明記する。
   - ARCH_COMPAT_CONTRACT の改訂 (AC-NET-3) は、IO_TIER の AC-TIME-4・AC-IO-1 と食い違わないようにする。
   - PM の判断: R1 のとおり、P3 は EE の窓が要らない (USER に戻るたびに DEC が進む)。したがって、P3 は io_pump の口を作りも使いもしない。r2 にそう明記し、「EE=0 の syscall の中で待つ物が出てきたら、io_pump の口を使う」を AC-NET-3 の側に 1 行書く。
2. rshell.c・main.c の SHELL_REMOTE_APP の分岐は、#ifdef を関数の途中に散らさない。LAN の部分を、既存の rshell_lan.c の側の関数 1〜2 個の呼び出しにまとめる (x86 の受入済みの経路の差分を小さくする)。x86 の生成物は、build_compare で MATCH を目標にする。

## PM 決定 (未決の振り分け)
- ESCC を P3 から外す (R4)。
- EE の窓を外す (R1)。
- 段は約 6 段にまとめる (R7)。

## 本線 PM の条件 3 (追加、2026-11-14)
- 前景の仕事 (link の bind・USB の poll) は、PPC の sys_halt と sys_yield の endpoint に直接書かない。既存の idle_work (kernel/idle_work.h、登録 4 本) に登録し、endpoint は idle_work_run() を呼ぶだけにする。
  - 理由: x86 も sys_halt と syscall の入口で idle_work_run を呼んでいる。IO_TIER_DESIGN では、この idle_work を Tier の振り分けへ育てる (互換枠は常に pending) と決めている。
  - 登録の枠が 4 本で足りなければ、r2 にそう書く (IO_TIER の段で 16 枠になる)。
