;; ============================================================
;; loader_fat.asm — FAT12対応 第2ステージローダ
;;
;; boot_fat.asmによって0000:8000hにロードされる
;;
;; 処理:
;;   1. FAT12からKERNEL.BINを検索・メモリにロード
;;   2. A20ゲート有効化
;;   3. GDT設定 → プロテクトモード遷移 → pm32へジャンプ
;;
;; KERNEL.BINのメモリ配置:
;;   0:9000h〜 (リンクオフセット0x9000に合わせる)
;;   64KB境界(0xFFFF)を超えた分はセグメント切替で読込
;;
;; PC-98 2HD FAT12:
;;   セクタ5-10: ルートDir (192エントリ)
;;   セクタ1-2:  FAT
;;   セクタ11〜: データ (クラスタ2から)
;; ============================================================

cpu 386


DA_UA       EQU     090h
FAT_BUF     EQU     6000h       ;; ルートDir/FAT一時バッファ (0:6000)
ROOT_START  EQU     5
ROOT_SECTS  EQU     6
FAT_START   EQU     1
DATA_START  EQU     11
KERNEL_SEG0 EQU     0000h       ;; 最初のロードセグメント
KERNEL_OFF0 EQU     9000h       ;; 最初のロードオフセット
KERNEL_SEG1 EQU     1000h       ;; 64KB境界後のセグメント (物理0x10000)

section .text
        

        org 8000h

global loader_start
loader_start:
        ;; boot_fatから0:8000にジャンプされた直後
        ;; CS=0, IP=8000h だが DS は未初期化 (1FC0hのまま)
        ;; DS=0に設定してデータ参照を正しくする
        xor     ax, ax
        mov     ds, ax          ;; DS = 0
        mov     ss, ax
        mov     sp, 7C00h

        ;; テキストVRAM表示
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 160         ;; 2行目
        mov     si, msg_loader
        call    print16

        ;; ES = 0 に戻す
        xor     ax, ax
        mov     es, ax

        ;; ============================================================
        ;; ルートDirを0:6000にロード
        ;; ============================================================
        mov     cx, ROOT_SECTS
        mov     ax, ROOT_START
        mov     bp, FAT_BUF
.rd_loop:
        push    cx
        push    ax
        call    read_sect16
        pop     ax
        pop     cx
        inc     ax
        add     bp, 0400h
        loop    .rd_loop

        ;; ============================================================
        ;; KERNEL.BINを検索
        ;; ============================================================
        mov     di, FAT_BUF
        mov     cx, 192
.scan_kern:
        mov     al, es:[di]
        cmp     al, 0
        je      .no_kernel
        cmp     al, 0E5h
        je      .scan_knext

        push    cx
        push    di
        mov     si, kern_name
        mov     cx, 11
        repe    cmpsb
        pop     di
        pop     cx
        je      .found_kern

.scan_knext:
        add     di, 32
        loop    .scan_kern

.no_kernel:
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 320
        mov     si, msg_nokernel
        call    print16
.halt:
        hlt
        jmp     .halt

.found_kern:
        ;; 開始クラスタと全サイズ取得
        mov     ax, es:[di + 1Ah]
        mov     word [var_cluster], ax
        mov     ax, es:[di + 1Ch]
        mov     word [var_size_lo], ax
        mov     ax, es:[di + 1Eh]
        mov     word [var_size_hi], ax

        ;; カーネルサイズ表示 (デバッグ用)
        ;; ...

        ;; ============================================================
        ;; FATテーブルを0:6000にロード (2セクタ)
        ;; ============================================================
        xor     ax, ax
        mov     es, ax
        mov     ax, FAT_START
        mov     bp, FAT_BUF
        call    read_sect16
        mov     ax, FAT_START + 1
        mov     bp, FAT_BUF + 0400h
        call    read_sect16

        ;; ============================================================
        ;; KERNEL.BINをメモリにロード
        ;;
        ;; Phase 1: 0:9000h〜0:FFFFh (最大28KB)
        ;; Phase 2: 1000:0000h〜 (物理0x10000以降)
        ;; ============================================================
        mov     ax, word [var_cluster]
        mov     word [var_load_seg], KERNEL_SEG0
        mov     bp, KERNEL_OFF0         ;; 最初は0:9000

.load_kern:
        ;; クラスタ→LBA
        push    ax
        sub     ax, 2
        add     ax, DATA_START

        ;; ES:BP にロード
        push    es
        mov     bx, word [var_load_seg]
        mov     es, bx
        call    read_sect16
        pop     es

        pop     ax

        ;; 次の宛先計算
        add     bp, 0400h               ;; +1024

        ;; 64KB境界チェック (BP が 0 にラップアラウンドしたら)
        or      bp, bp
        jnz     .seg_ok

        ;; セグメント切替: 0:FFFFを超えた → 1000:0 に移行
        mov     word [var_load_seg], KERNEL_SEG1

.seg_ok:
        ;; FAT12で次のクラスタ
        call    fat12_next16

        cmp     ax, 0FF8h
        jb      .load_kern

        ;; ============================================================
        ;; カーネルロード完了 → プロテクトモード遷移
        ;; ============================================================

        ;; デバッグ: カーネルロード完了メッセージ
        push    es
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 320         ;; 3行目
        mov     si, msg_kern_ok
        call    print16
        mov     di, 480         ;; 4行目: PM遷移開始メッセージ
        mov     si, msg_pm
        call    print16
        pop     es
        ;; A20ゲート有効化 (PC-98: ポート0xF2)
        mov     al, 3
        out     0F2h, al

        ;; 割り込み禁止
        cli

        ;; NMI禁止
        xor     al, al
        out     50h, al

        ;; GDTロード
        xor     eax, eax
        mov     ax, cs
        shl     eax, 4
        add     eax, gdt
        mov     dword [gdtr_base], eax
        lgdt    [gdtr]

        ;; CR0 PEビット設定
        mov     eax, cr0
        or      al, 1
        mov     cr0, eax

        ;; far jmp でプロテクトモードに遷移 → pm_inline32
        db      066h            ;; 32ビットオペランドプレフィクス
        db      0EAh            ;; far jmp opcode
        dd      pm_inline32  ;; 32ビットオフセット (リンカがOFFSET=0x8000ベースで解決)
        dw      0008h           ;; コードセグメントセレクタ

;; ============================================================
;; pm_inline32 — インラインPM初期化 (USE32)
;; loader_fat内に配置、USE16→USE32切替
;; ============================================================


bits 32

pm_inline32:
        ;; セグメントレジスタ初期化
        mov     eax, 10h
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax

        ;; スタック設定
        mov     esp, 0009FFFCh

        ;; TVRAMにPM到達メッセージ
        mov     edi, 0A0000h + 640   ;; 5行目
        mov     esi, pm_ok_msg
        call    pm_print32

        ;; ============================================================
        ;; 物理メモリプロービング (1MB以上)
        ;; out 50h, al (NMIマスク) 済みのため安全に探索可能
        ;; ============================================================
        mov     esi, 00100000h  ;; 探索開始アドレス = 1MB (0x100000)
        mov     ecx, 1024       ;; メモリ容量初期値 mem_kb = 1024 (1MB)

.probe_loop:
        ;; 元の値を待避
        mov     eax, [esi]

        ;; テストパターン 1: 0xAA55AA55
        mov     dword [esi], 0AA55AA55h
        cmp     dword [esi], 0AA55AA55h
        jne     .probe_done     ;; 不一致ならループ終了 (メモリ未実装)

        ;; テストパターン 2: 0x55AA55AA
        mov     dword [esi], 055AA55AAh
        cmp     dword [esi], 055AA55AAh
        jne     .probe_done

        ;; 値を復元
        mov     [esi], eax

        ;; 512KB 加算
        add     ecx, 512        ;; mem_kb += 512
        add     esi, 00080000h  ;; PADDR += 512KB

        ;; 最大16MBまでプローブ
        cmp     esi, 01000000h
        jb      .probe_loop     ;; 16MB未満なら続行

.probe_done:
        ;; 不一致だった場合も元の値を念のため書き戻す必要はない（未実装領域のため）
        ;; ECX = 総メモリ容量 (mem_kb)

        ;; ============================================================
        ;; カーネル引数のPUSHとジャンプ
        ;; kernel_main(uint32_t mem_kb, uint32_t boot_drive)
        ;; cdecl規約に従い、右側の引数からスタックへ積む
        ;; ============================================================
        mov     eax, DA_UA
        push    eax             ;; 第2引数: boot_drive
        push    ecx             ;; 第1引数: mem_kb
        push    dword 0         ;; ダミーのリターンアドレス

        ;; カーネルにfar jmp (0x9000)
        ;; C関数から戻ることは想定していないが、スタック構造上必要
        db      0EAh
        dd      00009000h
        dw      0008h

pm_print32:
        mov     al, [esi]
        inc     esi
        or      al, al
        jz      .done
        mov     ah, 0
        mov     [edi], ax
        push    edi
        add     edi, 2000h
        mov     byte [edi], 0E1h
        pop     edi
        add     edi, 2
        jmp     pm_print32
.done:
        ret

pm_ok_msg:
        db      'PM OK! Jumping to kernel...', 0



bits 16

;; ============================================================
;; read_sect16 — 16ビットモードで1セクタ読み込み
;; 入力: AX = LBA, ES:BP = 行き先
;; ============================================================
read_sect16:
        push    ax
        mov     bx, ax

        and     al, 07h
        inc     al
        mov     dl, al

        mov     ax, bx
        mov     cl, 3
        shr     ax, cl
        mov     dh, al
        and     dh, 1

        shr     ax, 1
        mov     cl, al

        mov     ah, 76h
        mov     al, DA_UA
        mov     bx, 0400h
        mov     ch, 03h
        int     1Bh

        pop     ax
        jc      disk_err16
        ret

disk_err16:
        push    cs
        pop     ds
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 320
        mov     si, msg_diskerr
        call    print16
.halt:
        hlt
        jmp     .halt

;; ============================================================
;; fat12_next16 — FAT12次クラスタ取得
;; ============================================================
fat12_next16:
        push    bx
        push    dx
        push    es

        xor     bx, bx
        mov     es, bx          ;; ES = 0

        mov     bx, ax
        shr     bx, 1
        add     bx, ax
        add     bx, FAT_BUF
        mov     dx, es:[bx]

        test    ax, 1
        jnz     .odd
        and     dx, 0FFFh
        jmp     .done
.odd:
        mov     cl, 4
        shr     dx, cl
        and     dx, 0FFFh
.done:
        mov     ax, dx

        pop     es
        pop     dx
        pop     bx
        ret

;; ============================================================
;; print16 — 16ビット TVRAMに文字列表示
;; ============================================================
print16:
        lodsb
        or      al, al
        jz      .done
        mov     ah, 0
        mov     es:[di], ax
        push    di
        add     di, 2000h
        mov     byte es:[di], 0E1h
        pop     di
        add     di, 2
        jmp     print16
.done:
        ret

;; ============================================================
;; GDT
;; ============================================================
gdt:
        dq      0                       ;; ヌルディスクリプタ

        ;; セレクタ 0x08: コードセグメント
        dw      0FFFFh
        dw      0
        db      0
        db      09Ah
        db      0CFh
        db      0

        ;; セレクタ 0x10: データセグメント
        dw      0FFFFh
        dw      0
        db      0
        db      092h
        db      0CFh
        db      0
gdt_end:

gdtr:
        dw      gdt_end - gdt - 1
gdtr_base:
        dd      0

;; ============================================================
;; データ / 変数
;; ============================================================
kern_name:      db 'KERNEL  BIN'
msg_loader:     db 'Loading kernel from FAT12...', 0
msg_nokernel:   db 'KERNEL.BIN not found!', 0
msg_kern_ok:    db 'Kernel loaded OK', 0
msg_pm:         db 'Entering PM...', 0
msg_diskerr:    db 'Disk Error!', 0

var_cluster:    dw 0
var_size_lo:    dw 0
var_size_hi:    dw 0
var_load_seg:   dw 0

