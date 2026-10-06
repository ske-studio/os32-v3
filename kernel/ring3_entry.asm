;; ============================================================
;; ring3_entry.asm — リング3 (CPL=3) システムコール入口 (v2 M1/M2)
;;
;; CPL=3 プログラムが KAPI トランポリンのスタブ (mov eax,slot; int 0x80; ret)
;; を呼ぶとここに入る。IDT ゲートは DPL=3 (kernel/idt.c)。CPU は特権遷移で
;; TSS.SS0:ESP0 のカーネルスタックに切り替え SS/ESP/EFLAGS/CS/EIP を積む
;; (エラーコードなし)。
;;
;; int80_stub: pushad でフレームを作り、C の ring3_syscall_dispatch(frame*) へ。
;;   フレーム: [0..7]=pushad(EDI..EAX), [8]=EIP [9]=CS [10]=EFLAGS
;;             [11]=userESP [12]=userSS。eax(=[7]) が slot。
;; ディスパッチャが本物の wrap を呼び戻り値を frame[7](=eax) に書く。
;; sys_exit / 範囲外 slot は longjmp するのでここへ戻らない。それ以外は
;; popad で eax=戻り値を復元し、CPL=3 へ iretd で戻る。
;;
;; 【重要】CPL=3 への iretd 復帰時はユーザデータセグメント (USER_DS) を
;; 復元する (isr_stub.asm の IRETD_USER と同じ理由: RESTORE_KSEG で
;; DS/ES/FS/GS が KERNEL_DS になっており、iretd はこれを戻さないため)。
;; ============================================================

cpu 386

extern ring3_syscall_dispatch

;; ユーザデータセグメント (CONTRACTS C1, kernel/gdt.h の USER_DS)
USER_DS     equ 0x2B
KERNEL_DS   equ 0x10

section .text

global int80_stub
int80_stub:
        ;; ゲートは割込みゲートなので IF はクリア済み (セグメント復元まで保つ)。
        pushad                          ;; フレーム先頭 = esp

        ;; C ハンドラはカーネルデータセグメント前提。CPL=3 由来では
        ;; DS/ES/FS/GS が USER_DS なので復元する (RESTORE_KSEG 相当)。
        mov     ax, KERNEL_DS
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax

        ;; 割込みゲートで IF=0 になっているので、ここで戻す。KAPI 本体
        ;; (wrap_*) は CPL=0 の call 経路と同じく IF=1 で走る前提で書かれて
        ;; いる: kbd_getchar / sys_halt / gui_call(OP_WAIT) は hlt で IRQ を
        ;; 待つため、IF=0 のままだと CPL=3 からの呼び出しで永久停止する
        ;; (2026-09-06、gdi_test の kbd_getchar と less の sys_halt で実測)。
        ;; iretd が EFLAGS をフレームから復元するので戻りの cli は不要。
        test    dword [esp + 40], 0x00020000   ;; saved EFLAGS.VM
        jnz     .dispatch
        sti
.dispatch:

        mov     eax, esp                ;; frame ptr (pushad 先頭)
        push    eax
        call    ring3_syscall_dispatch  ;; sys_exit/範囲外は戻らない (longjmp)
        add     esp, 4

        ;; 出口は IF=0 で通す。USER_DS を DS/ES/FS/GS に載せてから iretd まで
        ;; の間に IRQ が入ると、IRQ スタブが RESTORE_KSEG で DS=KERNEL_DS に
        ;; し、フレームの CS が CPL=0 (ここ) なので IRETD_USER が USER_DS を
        ;; 戻さず、そのまま CPL=3 へ iretd → 最初のメモリ参照で #GP になる
        ;; (2026-09-06 gdi_test の gfx_pixel で実測)。iretd がフレームの
        ;; EFLAGS (IF=1) を復元するので、ユーザ側の IF は変わらない。
        cli

        ;; 以下は ring3_resume と共有する出口 (票 K5b の P4)。
        ;; esp -> [pushad 8 語][EIP][CS][EFLAGS][userESP][userSS]

global ring3_iret_to_user
ring3_iret_to_user:
        popad                           ;; eax = 戻り値 (dispatcher が frame[7] に格納)

        ;; --- IRETD_USER: CPL=3 へ戻るならユーザセグメントを復元 ---
        ;; popad 後 esp -> [EIP][CS][EFLAGS][userESP][userSS]
        test    dword [esp + 8], 0x00020000   ;; EFLAGS.VM? (トランポリンは常に非V86)
        jnz     .do_iret
        test    dword [esp + 4], 3            ;; CS.RPL == 3 (CPL=3 へ戻る)?
        jz      .do_iret
        push    eax
        mov     ax, USER_DS
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        pop     eax
.do_iret:
        iretd

;; ============================================================
;; void __cdecl ring3_resume(const u32 *frame, u32 pd_phys, void *tss)
;;
;; 票 docs/archive/gui_v13/TASK_K5_multiapp.md の D2 (b) / P4。
;; exec_park が AppSlot へ写した 13 語 (pushad + iret 分) をカーネルスタックへ
;; 積み直し、CR3 をそのアプリの PD に載せて CPL=3 の続きへ戻る。**戻らない**。
;;
;; cli → TSS.ESP0 → CR3 → 積む → popad; iretd を割り込み禁止で一続きに行う
;; (exec.c の iret ブロックと同じ作法)。cli の後 iretd までに IRQ は 1 つも
;; 入らないので、TSS.ESP0 を書き替えてから CPL=3 に降りるまでの窓は無い。
;; iretd が保存済み EFLAGS (IF=1) を復元するのでアプリ側の IF は変わらない。
;;
;; frame はカーネル .bss (PDE 0 = 全 PD 共有) にあるので、CR3 を載せ替えた
;; 後でも読める。DS はカーネルデータのまま (C から呼ばれる)。
;; ============================================================
global ring3_resume
ring3_resume:
        cli
        mov     esi, [esp + 4]          ;; frame (13 語)
        mov     eax, [esp + 8]          ;; pd_phys
        mov     edx, [esp + 12]         ;; &kernel_tss
        mov     ecx, esp
        ;; TSS.ESP0 = 現在のカーネル ESP。offset 4 は kernel/tss.c の
        ;; STATIC_ASSERT(tss_esp0_at_offset_4) が固定している
        ;; (v86_entry.asm と同じ書き方)。
        mov     [edx + 4], ecx
        mov     cr3, eax                ;; アプリ PD へ切替
        sub     esp, 13 * 4             ;; フレーム置き場
        mov     edi, esp
        mov     ecx, 13
        cld
        rep     movsd                   ;; [esi] -> [edi] を 13 語
        jmp     ring3_iret_to_user      ;; popad; iretd (戻らない)

;; ============================================================
;; u32 __cdecl kapi_invoke(void *wrapfn, const void *args_src, u32 nbytes)
;;
;; args_src から nbytes バイトをカレント (カーネル) スタックへコピーし、
;; wrapfn を cdecl 呼び出しして戻り値 (eax) を返す。nbytes は 4 の倍数。
;; cdecl なので呼び出し側 (この関数) がコピーした引数を掃除する。
;; ============================================================
global kapi_invoke
kapi_invoke:
        push    ebp
        mov     ebp, esp
        push    esi
        push    edi
        push    ebx
        mov     ebx, [ebp + 8]          ;; wrapfn
        mov     esi, [ebp + 12]         ;; args_src
        mov     ecx, [ebp + 16]         ;; nbytes
        sub     esp, ecx                ;; 引数用の領域
        mov     edi, esp
        cld
        rep     movsb                   ;; [esi]->[edi] を ecx バイト
        call    ebx                     ;; wrapfn(...); eax = 戻り値
        lea     esp, [ebp - 12]         ;; コピー引数を破棄し保存レジスタ位置へ
        pop     ebx
        pop     edi
        pop     esi
        pop     ebp
        ret
