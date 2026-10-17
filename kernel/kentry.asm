;; ============================================================
;; kentry.asm — カーネルエントリポイント
;; kernel.binの先頭に配置され、BSSクリア後にkernel_mainにジャンプする
;; ============================================================

cpu 386
bits 32

extern kernel_main
extern __bss_start
extern __bss_end
;; LAYOUT_* は $(BUILD_OUT)/memmap_layout.h (LAYOUT_CPP で展開) から。

section .text

global kentry
kentry:
        ;; ============================================================
        ;; FPU (x87) 初期化
        ;; CR0.EM (bit2) = 0: FPU命令をソフトウェアエミュレーションしない
        ;; CR0.MP (bit1) = 1: WAIT/FWAIT でタスクスイッチ確認
        ;; CR0.NE (bit5) = 1: x87例外を内部(INT16)で処理 (外部IRQ13不使用)
        ;; ============================================================
        mov     eax, cr0
        and     eax, ~(1 << 2)    ;; CR0.EM = 0 (FPU命令を直接実行)
        or      eax, (1 << 1)     ;; CR0.MP = 1
        or      eax, (1 << 5)     ;; CR0.NE = 1
        mov     cr0, eax
        fninit                     ;; FPU を既知の状態にリセット
        ;; BSS領域ゼロクリア (ベアメタルではCRT0がないため手動で行う)
        mov     edi, __bss_start
        mov     ecx, __bss_end
        sub     ecx, edi
        shr     ecx, 2            ;; DWORD数に変換
        xor     eax, eax
        rep     stosd

        ;; ============================================================
        ;; カーネルスタックへ切り替える
        ;;
        ;; ここまではローダーが張った低位スタック (0x9FFFC) の上に居る。
        ;; ローダーはディスク BIOS を呼ぶためにリアルモードへ戻るので
        ;; 低位に居るしかないが、**カーネルが低位のリニアを掴んだままだと
        ;; V86 のゲストにコンベンショナルメモリを丸ごと渡せない**
        ;; (V86 のリニアは CPU が (seg<<4)+off で作るため低位 1MB 固定)。
        ;;
        ;; ここより上の BSS クリアは rep stosd だけで CALL を挟まないので、
        ;; 切り替え地点として安全。ページングはまだ有効でなく、
        ;; MEM_KSTACK_BASE からの領域は実 RAM なのでそのまま使える。
        ;;
        ;; **ローダーが積んだ引数を載せ替えること。**
        ;; kernel_main(u32 mem_kb, u32 boot_drive) は __cdecl で、
        ;; ローダーは far jmp の前に
        ;;   [esp+0] ダミーリターンアドレス / [esp+4] mem_kb /
        ;;   [esp+8] boot_drive
        ;; を積んでいる (boot/loader_hdd.asm / loader_fat*.asm の 3 本とも同じ)。
        ;; ESP を張り替えるだけだと引数が消え、kernel_main が拾う mem_kb が
        ;; ゴミになる。**症状はブート失敗ではなく `mem` の表示が
        ;; 3229946883 KB になるだけ**なので気づきにくい (実際これで一度踏んだ)。
        ;;
        ;; ESP は C/ld と同じ生成ヘッダの LAYOUT_MEM_KSTACK_TOP を使う。
        ;; EBP は 0 にしておく — 例外時のスタックトレースが
        ;; ここで止まれるようにするため。
        ;; ============================================================
        mov     eax, [esp + 4]          ;; mem_kb
        mov     edx, [esp + 8]          ;; boot_drive

        mov     esp, LAYOUT_MEM_KSTACK_TOP
        push    edx                     ;; 第2引数: boot_drive
        push    eax                     ;; 第1引数: mem_kb
        push    dword 0                 ;; ダミーリターンアドレス
        xor     ebp, ebp

        jmp     kernel_main
