;; ============================================================
;; isr_stub.asm — 割り込みハンドラASMスタブ (32ビット)
;;
;; CPU例外とIRQの割り込みを受け取り、Cハンドラにディスパッチする。
;; 全レジスタを保存・復帰し、IRETDで復帰する。
;; ============================================================

cpu 386

;; テキストVRAM直接書き込み用定数
TVRAM_CHAR  equ 0xA0000
TVRAM_ATTR  equ 0xA2000

;; PICポート (PC-98)
PIC1_CMD    equ 0x00
PIC2_CMD    equ 0x08
OCW2_EOI    equ 0x20

;; ユーザデータセグメント (CONTRACTS C1, kernel/gdt.h の USER_DS)。
;; CPL=3 中断からの復帰でセグメントを再ロードするのに使う。
USER_DS     equ 0x2B

;; ============================================================
;; V86 からの遷移では CPU が DS/ES/FS/GS を null セレクタにクリアする。
;; C ハンドラは DS がカーネルデータセグメントであることを前提に動くので、
;; 復元しないまま呼ぶと即座に #GP を起こす。しかもその #GP ハンドラも
;; DS=0 のまま C を呼ぶため、例外が無限再帰してカーネルスタックを食い潰し、
;; ガードページに当たって #DF → トリプルフォルトになる。
;;
;; 実際 Phase 1 の初回検証でこれを踏んだ (#PF の一次原因が完全に隠れ、
;; 「原因不明のトリプルフォルト」に見えた)。全スタブで必ず復元すること。
;;
;; PUSHAD の後に置けば EAX を潰しても POPAD で戻るので安全。
;; ============================================================
%macro RESTORE_KSEG 0
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
%endmacro

;; ============================================================
;; IRETD_USER — CPL=3 (リング3) 対応の iretd。全復帰点で iretd の代わりに使う。
;;
;; isr_stub は入口で RESTORE_KSEG により DS/ES/FS/GS を KERNEL_DS にするが、
;; popad/iretd は汎用レジスタと CS/SS/EIP/ESP/EFLAGS しか復元しない。
;; CPL=0 中断や V86 (CPU がフレームからセグメントを復元) では無害だが、
;; **保護モード CPL=3 (v2 リング3) を中断した場合**、復帰後の DS/ES/FS/GS が
;; KERNEL_DS のまま残り、CPL=3 が DS 経由でメモリに触れた瞬間に
;; 「CPL=3 が DPL=0 セグメント使用」で #GP する
;; (V3 実測: PIT 割り込みを跨いだ後の movl が EIP=0x40003D, CS=0x23 で #GP)。
;;
;; 対策: iretd 直前 (スタックが [EIP][CS][EFLAGS](+[ESP][SS]) に正規化された
;; 時点。全スタブで esp+4=CS, esp+8=EFLAGS) に復帰先を判定し、保護モード
;; CPL=3 へ戻るときだけユーザデータセグメント USER_DS を再ロードする。
;; システムのユーザデータセレクタは USER_DS ただ 1 本 (フラット, GDT 固定)
;; なので、割り込みごとに元値を保存せずとも USER_DS を入れれば正しい。
;;   - V86 (EFLAGS.VM=1): iretd がフレームからセグメントを復元 → 触らない
;;   - CPL=0 (CS.RPL=0):  KERNEL_DS のまま (再ロードしない)
;;   - CPL=3 (CS.RPL=3):  USER_DS を DS/ES/FS/GS に再ロード
;;
;; V86 経路 (ISR_*_V86 / V86_REFLECT) は EFLAGS.VM=1 で必ず素通しになるので
;; 壊さない。CPL=0 割り込みも RPL=0 で素通し。追加コストは分岐 2 つのみ。
;; ============================================================
%macro IRETD_USER 0
        test    dword [esp + 8], 0x00020000    ;; EFLAGS.VM?
        jnz     %%do_iret                      ;; V86 → iretd がフレームから復元
        test    dword [esp + 4], 3             ;; CS.RPL == 3 (CPL=3 へ戻る)?
        jz      %%do_iret                      ;; CPL=0 → KERNEL_DS のまま
        push    eax
        mov     ax, USER_DS
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        pop     eax
%%do_iret:
        iretd
%endmacro

;; 外部Cハンドラ (GCC ELFではアンダースコアなし)
extern exception_handler
extern page_fault_handler
extern timer_handler
extern kbd_irq_handler
extern serial_irq_handler
extern tick_count
extern fdc_irq_handler
extern mouse_irq_handler
extern v86_reflect_irq
extern v86_tick_and_check_timeout
extern v86_check_exit_request
extern v86_exit_to_kernel
;; CPL=3 アプリの強制脱出 (CTRL+STOP)。要求があれば longjmp して戻らない
;; (exec/exec.c、票 K2 作業 4)。
extern ring3_abort_check

;; ============================================================
;; V86 実行中なら、この IRQ をゲストにも見せる (割り込み反射)。
;;
;; ハードウェア割り込みはエラーコードを積まないので、PUSHAD 後の
;; EFLAGS は [esp+40] にある ([esp+32]=EIP, +36=CS, +40=EFLAGS)。
;; VM ビットが立っていればゲストからの割り込みなので反射する。
;;
;; 引数は (frame*, irq 番号) の順。**ベクタ番号ではない。**
;; どのベクタに落とすかはゲストが ICW2 に書いた値で決まるので、
;; C 側が仮想 PIC に問い合わせて決める。マスクの判定も同じ場所。
;; ============================================================
%macro V86_REFLECT 1
        test    dword [esp + 40], 0x00020000    ;; EFLAGS.VM
        jz      %%skip
        push    dword %1                        ;; IRQ 番号
        lea     eax, [esp + 4]                  ;; frame 先頭 (PUSHAD)
        push    eax
        call    v86_reflect_irq
        add     esp, 8
%%skip:
%endmacro

section .text

;; ============================================================
;; CPU例外スタブ: 例外番号をpushしてからCハンドラへ
;; ============================================================

;; マクロ: エラーコードなし例外
%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
        cli
        push    0               ;; ダミーエラーコード
        push    %1              ;; 例外番号
        jmp     isr_common
%endmacro

;; マクロ: エラーコードあり例外
%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
        cli
        ;; エラーコードはCPUが自動push済み
        push    %1              ;; 例外番号
        jmp     isr_common
%endmacro

extern v86_fault_handler

;; マクロ: エラーコードなし例外 — V86 分岐付き
;;
;; V86 中の例外を汎用ハンドラに流してはいけない。EFLAGS.VM を見ずに
;; 「カーネルが EIP=0x120 で落ちた」と表示してしまい、ゲストのフォルトが
;; カーネルのバグに化けて見える (実際 #UD でこれを踏んだ)。
;;
;; エントリ時のスタックは [EIP][CS][EFLAGS] なので VM は [esp+8]。
%macro ISR_NOERR_V86 1
global isr_stub_%1
isr_stub_%1:
        cli
        test    dword [esp + 8], 0x00020000  ;; EFLAGS.VM
        jnz     %%from_v86

        push    0               ;; ダミーエラーコード
        push    %1              ;; 例外番号
        jmp     isr_common

%%from_v86:
        ;; #GP スタブと同じフレーム形にするため errcode 相当を積む
        push    0
        pushad

        RESTORE_KSEG

        mov     eax, esp
        push    dword %1                ;; 引数2: ベクタ
        push    eax                     ;; 引数1: フレーム先頭 (u32*)
        call    v86_fault_handler
        add     esp, 8

        ;; 復帰できる種類の例外ではないのでセッションを畳む (戻らない)
        call    v86_exit_to_kernel
%%hang:
        cli
        hlt
        jmp     %%hang
%endmacro

;; マクロ: エラーコードあり例外 — V86 分岐付き
;;
;; エラーコードは CPU が自動 push 済みなので、エントリ時のスタックは
;; [errcode][EIP][CS][EFLAGS]。VM ビットは [esp+12] にある。
;; V86 側フレーム形は ISR_NOERR_V86 と同じ ([PUSHAD][errcode][EIP]...)。
%macro ISR_ERR_V86 1
global isr_stub_%1
isr_stub_%1:
        cli
        test    dword [esp + 12], 0x00020000  ;; EFLAGS.VM
        jnz     %%from_v86

        push    %1              ;; 例外番号
        jmp     isr_common

%%from_v86:
        pushad

        RESTORE_KSEG

        mov     eax, esp
        push    dword %1                ;; 引数2: ベクタ
        push    eax                     ;; 引数1: フレーム先頭 (u32*)
        call    v86_fault_handler
        add     esp, 8

        call    v86_exit_to_kernel
%%hang:
        cli
        hlt
        jmp     %%hang
%endmacro

;; 例外スタブ生成 — ベクタ 0-31 をすべて専用スタブで受ける。
;; 以前は未登録の例外がデフォルトスタブ (EOI なし iretd) に落ちて
;; 無限フォルトループになっていた。
ISR_NOERR_V86 0                ;; #DE ゼロ除算
ISR_NOERR_V86 1                ;; #DB デバッグ (V86シングルステップ用)
ISR_NOERR_V86 2                ;; NMI
ISR_NOERR_V86 3                ;; #BP ブレークポイント
ISR_NOERR_V86 4                ;; #OF オーバーフロー
ISR_NOERR_V86 5                ;; #BR BOUND範囲外
ISR_NOERR_V86 6                ;; #UD 未定義命令
ISR_NOERR_V86 7                ;; #NM コプロセッサ不在
ISR_ERR   8                    ;; #DF ダブルフォルト
ISR_NOERR_V86 9                ;; コプロセッサセグメントオーバーラン
ISR_ERR_V86   10               ;; #TS 無効TSS
ISR_ERR_V86   11               ;; #NP セグメント不在
ISR_ERR_V86   12               ;; #SS スタックフォルト
;; #GP (13) は V86 分岐が要るので専用スタブ (下記)
;; #PF は専用スタブを使用 (下記 pf_stub)
ISR_NOERR_V86 15               ;; 予約
ISR_NOERR_V86 16               ;; #MF x87 FPUエラー
ISR_ERR_V86   17               ;; #AC アライメントチェック
ISR_NOERR_V86 18               ;; #MC マシンチェック
ISR_NOERR_V86 19               ;; #XM SIMD FPU例外
ISR_NOERR_V86 20               ;; 予約 (20-31)
ISR_NOERR_V86 21
ISR_NOERR_V86 22
ISR_NOERR_V86 23
ISR_NOERR_V86 24
ISR_NOERR_V86 25
ISR_NOERR_V86 26
ISR_NOERR_V86 27
ISR_NOERR_V86 28
ISR_NOERR_V86 29
ISR_NOERR_V86 30
ISR_NOERR_V86 31

;; ============================================================
;; #GP 一般保護例外 (ISR 13) — V86 分岐付き
;;
;; V86 ゲストの IN/OUT・未対応命令はすべてここに落ちる。
;; 通常の #GP と区別するため、CPU が積んだ EFLAGS の VM ビットを見る。
;;
;; CPU自動push後のスタック (ESP低位→高位):
;;   [error_code] [EIP] [CS] [EFLAGS] (V86ならさらに [ESP][SS][ES][DS][FS][GS])
;; ============================================================
extern v86_gp_handler
extern v86_exit_to_kernel

global isr_stub_13
isr_stub_13:
        cli
        test    dword [esp + 12], 0x00020000  ;; EFLAGS.VM
        jnz     .from_v86

        ;; --- 通常の #GP: 従来どおり例外ハンドラへ ---
        push    13
        jmp     isr_common

.from_v86:
        pushad

        RESTORE_KSEG

        mov     eax, esp
        push    eax                     ;; 引数: フレーム先頭 (u32*)
        call    v86_gp_handler
        add     esp, 4

        test    eax, eax
        jnz     .v86_exit

        ;; 0 が返った → V86 に復帰
        popad
        add     esp, 4                  ;; error_code をスキップ
        IRETD_USER

.v86_exit:
        ;; 非0 が返った → セッション終了 (longjmp するので戻らない)
        call    v86_exit_to_kernel
.hang:
        cli
        hlt
        jmp     .hang

;; ============================================================
;; 例外共通ハンドラ
;; pushad後のスタック (低→高):
;;   [PUSHAD(32)] [vector(4)] [error_code(4)] [EIP(4)] [CS(4)] [EFLAGS(4)]
;;
;; exception_handler(u32 error_code, u32 vector, u32 fault_eip, u32 *regs)
;; ============================================================
isr_common:
        pushad                  ;; 全汎用レジスタ保存 (32B)
        RESTORE_KSEG            ;; V86 由来だと DS/ES/FS/GS が null
                                ;; ESP = base とする
                                ;; base+0..31: PUSHAD
                                ;; base+32: vector
                                ;; base+36: error_code
                                ;; base+40: fault EIP

        ;; 引数4: regs (PUSHAD配列先頭)
        mov     eax, esp
        push    eax             ;; ESP=base-4

        ;; 引数3: fault_eip
        mov     eax, [esp + 4 + 32 + 4 + 4] ;; base-4+44 = base+40 ✓
        push    eax             ;; ESP=base-8

        ;; 引数2: vector
        mov     eax, [esp + 8 + 32]          ;; base-8+40 = base+32 ✓
        push    eax             ;; ESP=base-12

        ;; 引数1: error_code
        mov     eax, [esp + 12 + 32 + 4]     ;; base-12+48 = base+36 ✓
        push    eax             ;; ESP=base-16

        call    exception_handler
        add     esp, 16

        popad
        add     esp, 8          ;; error_code + vector をスキップ
        IRETD_USER

;; ============================================================
;; #PF ページフォルト専用スタブ (ISR 14)
;;
;; CPUが自動pushするエラーコードに加え、
;; CR2 (障害アドレス) をCハンドラに渡す。
;;
;; CPU自動push後のスタック (ESP低位→高位):
;;   [error_code] [EIP] [CS] [EFLAGS]
;;
;; page_fault_handler(u32 error_code, u32 fault_addr, u32 fault_eip, u32 *regs)
;; regs[0]=EDI, [1]=ESI, [2]=EBP, [3]=ESP_orig, [4]=EBX, [5]=EDX, [6]=ECX, [7]=EAX
;; ============================================================
global isr_stub_14
isr_stub_14:
        cli
        ;; V86 ゲストの #PF はカーネルのページフォルトではない。
        ;; 汎用ハンドラに渡すとカーネルのバグとして表示されてしまう。
        test    dword [esp + 12], 0x00020000  ;; EFLAGS.VM
        jz      .not_v86

        pushad
        RESTORE_KSEG
        mov     eax, esp
        push    dword 14
        push    eax
        call    v86_fault_handler
        add     esp, 8
        call    v86_exit_to_kernel
.v86_hang:
        cli
        hlt
        jmp     .v86_hang

.not_v86:
        pushad                  ;; 全汎用レジスタ保存 (32B)
        RESTORE_KSEG            ;; V86 由来だと DS/ES/FS/GS が null

        ;; PUSHAD配列のポインタ (引数4: regs)
        mov     eax, esp
        push    eax             ;; 引数4: regs (PUSHAD配列先頭)

        ;; フォルト時EIP取得 (PUSHAD=32B + push×1=4B + error_code=4B の上)
        mov     eax, [esp + 40] ;; EIP
        push    eax             ;; 引数3: fault_eip

        ;; CR2 (障害アドレス) 取得
        mov     eax, cr2
        push    eax             ;; 引数2: fault_addr (CR2)

        ;; エラーコード取得 (PUSHAD=32B + push×3=12B の上)
        mov     eax, [esp + 44] ;; error_code
        push    eax             ;; 引数1: error_code

        call    page_fault_handler
        add     esp, 16

        popad
        add     esp, 4          ;; error_code をスキップ
        IRETD_USER

;; ============================================================
;; デフォルトハンドラ (ベクタ 0x30 以降の未使用ソフトウェア割り込み用)
;;
;; 例外 (0-31) とハード IRQ (0x20-0x2F) はすべて専用スタブを持つので、
;; ここに落ちるのはカーネル内の明示的な INT n だけ。何もせず復帰する。
;; ============================================================
global isr_stub_default
isr_stub_default:
        IRETD_USER

;; ============================================================
;; 動的 IRQ の共通スタブ (票 TASK_HAL_WIRING §1-1)
;;
;; 以前はここが IRQ_UNEXP (EOI + 診断だけ) だった。PCI の装置は BIOS が
;; IRQ を割り当て、複数の装置が 1 本を共有し得るので、「起動時に読んだ番号で
;; ハンドラを結ぶ口」が要る。C 側 irq_dispatch() が表の全登録者を 2 巡ぶん
;; 呼び、**EOI は irq_finish() の 1 経路だけ**で送る (誰も受けなくても送る —
;; 送らないと PIC の ISR ビットが立ったままになり、同順位以下が永久ブロック
;; される。実際 IRQ9 で起きた)。
;;
;; **V86 ゲストへは反射しない** (V86_REFLECT を置かない)。動的登録の IRQ は
;; ホスト (OS32) 所有の装置で、ゲストのものではない。V86 フレームからの復帰は
;; IRETD_USER が面倒を見る。
;;
;; cld: 割り込みの入口は DF を自動で消さない。V86 ゲストが STD の直後に
;; 割り込まれると、C 側から呼ばれる kstrcmp (lodsb) が逆向きに走る (往復 3 B3)。
;; ============================================================
extern irq_dispatch

%macro IRQ_COMMON 1
global irq_stub_common_%1
irq_stub_common_%1:
        pushad
        RESTORE_KSEG
        cld
        push    dword %1                ;; IRQ 番号 (ベクタではない)
        call    irq_dispatch
        add     esp, 4
        popad
        IRETD_USER
%endmacro

IRQ_COMMON 3                    ;; INT 0x23
IRQ_COMMON 5                    ;; INT 0x25
IRQ_COMMON 6                    ;; INT 0x26
IRQ_COMMON 8                    ;; INT 0x28
IRQ_COMMON 9                    ;; INT 0x29
IRQ_COMMON 10                   ;; INT 0x2A
IRQ_COMMON 14                   ;; INT 0x2E
IRQ_COMMON 15                   ;; INT 0x2F

;; ============================================================
;; IRQ0: タイマ割り込み (INT 0x20)
;; ============================================================
global irq_stub_0
irq_stub_0:
        pushad
        RESTORE_KSEG
        cld                     ;; 割り込みの入口は DF を消さない (往復 3 B3)

        ;; tick_count をインクリメント
        inc     dword [tick_count]

        ;; Cハンドラを呼び出し
        call    timer_handler

        ;; マスタPICにEOI送出 (PC-98: ポート 0x00)
        mov     al, OCW2_EOI
        out     PIC1_CMD, al

        V86_REFLECT 0           ;; IRQ0 (タイマ)。既定では INT 08h に落ちる

        ;; V86 セッションが長すぎたら畳む。暴走したゲストを無限に
        ;; 走らせないための保険。EOI も反射も済ませた後に判定する。
        test    dword [esp + 40], 0x00020000    ;; EFLAGS.VM
        jz      .no_timeout
        call    v86_tick_and_check_timeout
        test    eax, eax
        jz      .no_timeout
        call    v86_exit_to_kernel              ;; longjmp するので戻らない
.no_timeout:

        popad
        IRETD_USER

;; ============================================================
;; IRQ1: キーボード割り込み (INT 0x21)
;; ============================================================
global irq_stub_1
irq_stub_1:
        pushad
        RESTORE_KSEG
        cld                     ;; 同上 (C 呼び出しの前に DF=0)

        ;; Cハンドラを呼び出し。V86 セッション中はここでスキャンコードが
        ;; ゲスト用 FIFO に積まれ、OS32 のリングバッファには入らない。
        ;; 戻り値 (eax) = 反射してよいか。0041h から実データを読んだときだけ
        ;; 非 0 — 空 IRQ (RxRDY=0) やエラー (PE/FE) で反射すると、ゲストが
        ;; 空の仮想 FIFO から偽の打鍵を読む (POLICY_DEBUG §4-57)。
        ;; ebx に移すのは EOI の `out` が al を使うから。ebx は pushad が
        ;; 退避済みで、V86_REFLECT が呼ぶ C (v86_reflect_irq) も保存する。
        call    kbd_irq_handler
        mov     ebx, eax

        ;; マスタPICにEOI送出 (PC-98: ポート 0x00)
        mov     al, OCW2_EOI
        out     PIC1_CMD, al

        test    ebx, ebx
        jz      .no_reflect
        V86_REFLECT 1           ;; IRQ1 (キーボード)。既定では INT 09h に落ちる
.no_reflect:

        ;; 脱出ホットキー (CTRL+GRPH+DEL) が押されていたら畳む。
        ;; EOI と反射を済ませてから判定するのはタイマ側と同じ理由。
        test    dword [esp + 40], 0x00020000    ;; EFLAGS.VM
        jz      .no_exit
        call    v86_check_exit_request
        test    eax, eax
        jz      .no_exit
        call    v86_exit_to_kernel              ;; longjmp するので戻らない
.no_exit:

        ;; CPL=3 アプリの強制脱出 (CTRL+STOP、契約 T6 / 票 K2 作業 4)。
        ;; kbd_irq_handler が要求を立て、ここで畳む。**割り込まれた文脈が
        ;; CPL=3 (= アプリのコードそのもの) のときだけ**: カーネル内 (KAPI の
        ;; wrap 実行中など) で longjmp するとカーネル状態が中途半端になるので、
        ;; その場合は要求を残し ring3_syscall_dispatch の入口で畳む。
        ;; EOI と V86 反射を済ませてから判定するのは V86 の脱出と同じ理由。
        ;; フレーム: pushad(32B) の上に [esp+32]=EIP [esp+36]=CS [esp+40]=EFLAGS
        test    dword [esp + 40], 0x00020000    ;; EFLAGS.VM (V86 は対象外)
        jnz     .no_abort
        test    dword [esp + 36], 3             ;; CS.RPL == 3 (CPL=3 を割り込んだ)?
        jz      .no_abort
        call    ring3_abort_check               ;; 要求があれば longjmp (戻らない)
.no_abort:

        popad
        IRETD_USER

;; ============================================================
;; IRQ4: RS-232C 割り込み (INT 0x24)
;; ============================================================
global irq_stub_4
irq_stub_4:
        pushad
        RESTORE_KSEG
        cld

        ;; Cハンドラを呼び出し
        call    serial_irq_handler

        ;; マスタPICにEOI送出 (PC-98: ポート 0x00)
        mov     al, OCW2_EOI
        out     PIC1_CMD, al

        popad
        IRETD_USER

;; ============================================================
;; IRQ7: スプリアス対策 (INT 0x27)
;; PC-98ではIR7がスレーブカスケードだが、スプリアスは起こりうる
;; ============================================================
global irq_stub_7
irq_stub_7:
        push    eax
        ;; ISR読み出しでスプリアスか確認
        mov     al, 0x0B        ;; OCW3: ISR読み出し指定
        out     PIC1_CMD, al
        in      al, PIC1_CMD
        mov     ah, al          ;; ISR 値を退避
        ;; OCW3 を IRR 読み出しに戻す (pic_init が設定した既定モード)。
        ;; 戻さないと以後の PIC1_CMD リードが全部 ISR を返してしまう。
        mov     al, 0x0A        ;; OCW3: IRR読み出し指定
        out     PIC1_CMD, al
        test    ah, 0x80        ;; IR7がセットされているか
        jnz     .real            ;; 本物の割り込みなら処理

        ;; スプリアス → EOIを送らずに無視
        pop     eax
        IRETD_USER

.real:
        ;; 本物のIR7割り込み (スレーブカスケード等)
        mov     al, OCW2_EOI
        out     PIC1_CMD, al
        pop     eax
        IRETD_USER

;; ============================================================
;; IRQ11: FDD割り込み (INT 0x2B) — スレーブPIC IR11
;; ============================================================
global irq_stub_11
irq_stub_11:
        pushad
        RESTORE_KSEG
        cld

        ;; Cハンドラを呼び出し
        call    fdc_irq_handler

        ;; スレーブPICにEOI送出 (PC-98: ポート 0x08)
        mov     al, OCW2_EOI
        out     PIC2_CMD, al
        ;; マスタPICにもEOI送出 (カスケード)
        out     PIC1_CMD, al

        popad
        IRETD_USER

;; ============================================================
;; IRQ12: サウンドボード (INT 0x2C) — スレーブPIC IR4
;;
;; OS32 自身はサウンドボードの割り込みを使っていない。このスタブは
;; V86 ゲストのために存在する。
;;
;; Phase 0 の実測で、Ys が差し替えている割り込みは IRQ12 (ゲストの
;; INT 14h) ただ 1 本で、FM の演奏はこれだけに依存していることが
;; 確定している。前回の実装は IRQ0/1/2 しか注入しておらず、ゲストが
;; 使う唯一の IRQ を落としていた — それが「音源は検出できるのに
;; 曲が鳴らない」の正体だった。
;; (docs/tasks/v86v2/03_ys_profile.md §4)
;; ============================================================
;; ============================================================
;; IRQ2: VSYNC (INT 0x22)
;;
;; OS32 自身は VSYNC 割り込みを使わない。ここに居るのは
;; **ゲストに渡すためだけ**で、セッション中しか有効にしない
;; (v86_run が irq_enable/irq_disable する)。
;;
;; Ys はフィールドに入ると PIC の IRQ2 を開け (実測: master IMR が
;; 0x7D → 0x79)、VSYNC ハンドラでフレームカウンタを減らす。
;; ゲーム本体は 0160:0866 でそのカウンタが 0 になるのを待つので、
;; これが届かないと**画面が止まったまま二度と進まない**。
;; ============================================================
global irq_stub_2
irq_stub_2:
        pushad
        RESTORE_KSEG
        cld                     ;; V86_REFLECT も C (v86_reflect_irq) を呼ぶ

        ;; マスタPICにEOI送出 (PC-98: ポート 0x00)
        mov     al, OCW2_EOI
        out     PIC1_CMD, al

        V86_REFLECT 2           ;; IRQ2 (VSYNC)。既定では INT 0Ah に落ちる

        popad
        IRETD_USER

global irq_stub_12
irq_stub_12:
        pushad
        RESTORE_KSEG
        cld                     ;; 同上

        ;; スレーブPICにEOI送出 → マスタPICにもEOI送出 (カスケード)
        mov     al, OCW2_EOI
        out     PIC2_CMD, al
        out     PIC1_CMD, al

        V86_REFLECT 12          ;; IRQ12 (サウンド)。既定では INT 14h に落ちる

        popad
        IRETD_USER

;; ============================================================
;; IRQ13: マウス割り込み (INT 0x2D) — スレーブPIC IR5
;; ============================================================
global irq_stub_13
irq_stub_13:
        pushad
        RESTORE_KSEG
        cld

        ;; Cハンドラを呼び出し
        call    mouse_irq_handler

        ;; スレーブPICにEOI送出 (PC-98: ポート 0x08)
        mov     al, OCW2_EOI
        out     PIC2_CMD, al
        ;; マスタPICにもEOI送出 (カスケード)
        out     PIC1_CMD, al

        popad
        IRETD_USER

;; ============================================================
;; LAN (LGY-98 / NE2000) の IRQ 入口は **共通スタブに移した**
;; (票 TASK_HAL_WIRING §1-1)。drivers/lgy98.c が
;;   irq_register(irq, ne2k_irq_shared, NULL, IRQ_F_SHARED)
;; で結ぶので、専用の irq_stub_nic_3/5/6 も idt_register_irq() も要らない。
;; EOI は irq_finish() が 1 回だけ送る。
;; ============================================================
