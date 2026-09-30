;; ============================================================
;; boot_fat.asm — PC-98 FAT12 IPLブートセクタ
;;
;; PC-98 2HD 1024B/sector FAT12フロッピーからLOADER.BINを読み込み、
;; 0000:8000h にロードしてジャンプする。
;;
;; NP21/Wでは 1FC0:0000 にロードされる (物理 0x1FC00)
;; BPBはmkfat12.pyによって上書きされる場合がある
;;
;; FAT12レイアウト (PC-98 2HD):
;;   セクタ0:   ブートセクタ (本コード)
;;   セクタ1-2: FAT1 (2セクタ)
;;   セクタ3-4: FAT2
;;   セクタ5-10: ルートディレクトリ (192エントリ)
;;   セクタ11〜: データ領域 (クラスタ2から)
;;
;; LBA→CHS (spt=8, heads=2):
;;   cylinder = LBA / 16
;;   head     = (LBA / 8) & 1
;;   sector   = (LBA % 8) + 1
;; ============================================================

cpu 8086


;; ============================================================
;; ジオメトリ — `-DFD144` で 1.44MB 版になる (既定は PC-98 2HD 1232KB)
;;
;; 値の正典は tools/mkfat12.py の GEOMETRIES。**片方だけ直さないこと。**
;;
;; 1.44MB は DA/UA 0x30 台の「1.44MB 対応両用インタフェース」から起動する
;; (PC9800Bible 表 2-34、undocumented/memsys.md の 0000:0584h DISK_BOOT)。
;; NP21/W は rpm=1 のとき IPL を **512 バイトだけ** 1FE0:0000 へ読む
;; (src/bios/bios1b.c boot_fd1)。だから 144 版は 512B に収めねばならない。
;; ============================================================
%ifdef FD144
SECT_SZ     EQU     0200h       ;; セクタ長 512B
SECT_N      EQU     02h         ;; INT 1Bh のセクタ長コード (2 = 512B)
SPT         EQU     18          ;; セクタ/トラック
DA_UA       EQU     030h        ;; DA/UA: 1.44MB 対応両用 I/F ユニット0
ROOT_START  EQU     19          ;; ルートDir開始LBA (1 + 2*9)
ROOT_SECTS  EQU     12          ;; ルートDirセクタ数 (192 エントリ)
FAT_SECTS   EQU     9           ;; FAT1 本あたりのセクタ数
DATA_START  EQU     31          ;; データ領域開始LBA
%else
SECT_SZ     EQU     0400h       ;; セクタ長 1024B
SECT_N      EQU     03h         ;; INT 1Bh のセクタ長コード (3 = 1024B)
SPT         EQU     8           ;; セクタ/トラック
DA_UA       EQU     090h        ;; DA/UA: 1MB FDD ユニット0
ROOT_START  EQU     5           ;; ルートDir開始LBA
ROOT_SECTS  EQU     6           ;; ルートDirセクタ数
FAT_SECTS   EQU     2           ;; FAT1 本あたりのセクタ数
DATA_START  EQU     11          ;; データ領域開始LBA
%endif

ROOT_ENTS   EQU     192         ;; ルートDirエントリ数 (両ジオメトリ共通)
LOAD_DEST   EQU     8000h       ;; LOADER.BIN ロード先
FAT_BUF     EQU     6000h       ;; FAT/RootDir一時バッファ
FAT_START   EQU     1           ;; FAT開始LBA

section .text
        org 0x0

global start
start:
        jmp     short boot_main
        nop

;; ============================================================
;; BPB (BIOS Parameter Block) — 0x03〜0x23
;; mkfat12.py が正しい値で上書きする
;; ============================================================
bpb_oem:        db 'OS32IPL '      ;; 0x03: OEM名
bpb_bps:        dw SECT_SZ         ;; 0x0B: bytes/sector
bpb_spc:        db 01h             ;; 0x0D: sectors/cluster = 1
bpb_resv:       dw 0001h           ;; 0x0E: reserved = 1
bpb_nfats:      db 02h             ;; 0x10: FAT数 = 2
bpb_rootcnt:    dw ROOT_ENTS       ;; 0x11: root entries
%ifdef FD144
bpb_totsect:    dw 2880            ;; 0x13: total sectors
bpb_media:      db 0F0h            ;; 0x15: media = 1.44MB
%else
bpb_totsect:    dw 1232            ;; 0x13: total sectors
bpb_media:      db 0FEh            ;; 0x15: media = PC-98 2HD
%endif
bpb_fatsz:      dw FAT_SECTS       ;; 0x16: FAT size
bpb_spt:        dw SPT             ;; 0x18: sectors/track
bpb_heads:      dw 0002h           ;; 0x1A: heads = 2
bpb_hidden:     dd 0               ;; 0x1C: hidden sectors
bpb_totsect32:  dd 0               ;; 0x20: total sectors 32bit
;; (ここまで0x24 = 36バイト)

;; ============================================================
;; ブートコード本体
;; ============================================================
boot_main:
        ;; セグメント初期化
        cli
        push    cs
        pop     ds              ;; DS = CS (BPBアクセス用)
        xor     ax, ax
        mov     es, ax          ;; ES = 0 (データ読込先)
        mov     ss, ax
        mov     sp, 7C00h
        sti

        ;; 起動メッセージ表示
        call    show_boot_msg

        ;; ============================================================
        ;; ルートディレクトリを0:6000にロード
        ;; 192 エントリ = 6144B。1.44MB でも 12 セクタ×512B = 6144B で
        ;; 終端は 0x7800、SP=0x7C00 に当たらない (224 エントリだと当たる)。
        ;; ============================================================
        mov     cx, ROOT_SECTS
        mov     ax, ROOT_START
        mov     bp, FAT_BUF    ;; ES:BP = 0:6000
.rd_loop:
        push    cx
        push    ax
        call    read_sector
        pop     ax
        pop     cx
        inc     ax
        add     bp, SECT_SZ
        loop    .rd_loop

        ;; ============================================================
        ;; ルートDirから "LOADER  BIN" を検索
        ;; ============================================================
        mov     di, FAT_BUF     ;; ES:DI = 0:6000
        mov     cx, ROOT_ENTS
.scan_dir:
        ;; エントリ先頭バイト確認
        mov     al, es:[di]
        cmp     al, 0           ;; 終端?
        je      .no_loader
        cmp     al, 0E5h        ;; 削除済み?
        je      .scan_next

        ;; 11バイト比較
        push    cx
        push    di
        mov     si, loader_name
        mov     cx, 11
        repe    cmpsb
        pop     di
        pop     cx
        je      .found_loader

.scan_next:
        add     di, 32          ;; 次のエントリ
        loop    .scan_dir

.no_loader:
        ;; LOADER.BIN見つからない → エラー
        mov     si, msg_noldr
        call    show_error
        jmp     disk_error.halt

.found_loader:
        ;; 開始クラスタ取得 (DirEntry 0x1A)
        mov     ax, es:[di + 1Ah]
        mov     word [var_cluster], ax

        ;; ============================================================
        ;; FATテーブルを0:6000にロード (FAT_SECTS セクタ)
        ;; ルートDirバッファを上書き (もう不要)
        ;; 1.44MB は FAT が 9 セクタ = 4608B ある。**2 セクタ決め打ちだと
        ;; 後ろのクラスタを引いたときに黙って化ける。**
        ;; ============================================================
        mov     cx, FAT_SECTS
        mov     ax, FAT_START
        mov     bp, FAT_BUF    ;; 0:6000
.fat_loop:
        push    cx
        push    ax
        call    read_sector
        pop     ax
        pop     cx
        inc     ax
        add     bp, SECT_SZ
        loop    .fat_loop

        ;; ============================================================
        ;; FATチェーンに沿ってLOADER.BINを0:8000にロード
        ;; ============================================================
        mov     bp, LOAD_DEST   ;; ES:BP = 0:8000
        mov     ax, word [var_cluster]

.load_chain:
        ;; クラスタ→LBA: LBA = DATA_START + (cluster - 2)
        push    ax
        sub     ax, 2
        add     ax, DATA_START
        call    read_sector
        pop     ax

        ;; 次の宛先
        add     bp, SECT_SZ

        ;; FAT12で次のクラスタを取得
        call    fat12_next      ;; AX = next cluster

        ;; EOC (>= 0xFF8) ?
        cmp     ax, 0FF8h
        jb      .load_chain

        ;; ============================================================
        ;; LOADER.BINにジャンプ
        ;; ============================================================
        db      0EAh            ;; far jmp opcode
        dw      LOAD_DEST       ;; = 0x8000
        dw      0000h           ;; segment = 0x0000

;; ============================================================
;; read_sector — 1セクタ(SECT_SZ)をES:BPに読み込む
;; 入力: AX = LBA, ES:BP = 行き先
;; 破壊: AX, BX, CX, DX
;;
;; **SPT で実際に割る。** 以前は spt=8 を前提にシフトで済ませていたが、
;; 1.44MB の spt=18 は 2 の冪ではない。2HD でも div は正しく、こちらは
;; 余裕が 600 バイト以上あるので**両ジオメトリで同じ道を通す**
;; (分岐を残すと片方だけ直して食い違う)。
;;   sector   = LBA % SPT + 1
;;   track    = LBA / SPT ;  head = track & 1 ;  cylinder = track >> 1
;; LBA は最大 2879 なので 16 ビットの div で足りる。
;; ============================================================
read_sector:
        push    ax

        xor     dx, dx
        mov     cx, SPT
        div     cx              ;; AX = track, DX = LBA % SPT (< 18 なので DH = 0)
        inc     dl              ;; DL = sector (1-based)

        mov     dh, al
        and     dh, 1           ;; DH = head (track の bit0)
        shr     ax, 1           ;; AX = cylinder
        mov     cl, al          ;; CL = cylinder

        ;; INT 1Bh: FDD読み込み
        mov     ah, 76h         ;; SEEK+RETRY+READ
        mov     al, DA_UA
        mov     bx, SECT_SZ     ;; 転送バイト数
        mov     ch, SECT_N      ;; セクタ長コード
        int     1Bh

        pop     ax
        jc      disk_error
        ret

;; ============================================================
;; fat12_next — FAT12チェーンの次のクラスタを取得
;; 入力: AX = 現在のクラスタ番号
;; 出力: AX = 次のクラスタ番号
;; FATデータは 0:6000h にロード済み
;; ============================================================
fat12_next:
        push    bx
        push    dx

        ;; = cluster + cluster/2 (1.5バイト/エントリ)
        mov     bx, ax
        shr     bx, 1
        add     bx, ax          ;; BX = byte in FAT

        ;; 16ビットロード from ES:[FAT_BUF + BX]
        add     bx, FAT_BUF
        mov     dx, es:[bx]

        ;; 奇数/偶数で上位/下位4bit選択
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

        pop     dx
        pop     bx
        ret

;; ============================================================
;; disk_error — ディスクエラー表示
;; ============================================================
disk_error:
        mov     si, msg_derr
        call    show_error
.halt:
        hlt
        jmp     .halt

;; ============================================================
;; show_boot_msg — 起動メッセージをTVRAMに表示
;; ============================================================
show_boot_msg:
        push    es
        mov     ax, 0A000h
        mov     es, ax
        xor     di, di          ;; 0行目先頭
        mov     si, msg_boot
        call    print_str
        pop     es
        ret

;; ============================================================
;; show_error — エラーメッセージをTVRAMの2行目に表示
;; ============================================================
show_error:
        push    es
        mov     ax, 0A000h
        mov     es, ax
        mov     di, 160         ;; 2行目 (80文字×2バイト)
        call    print_str
        pop     es
        ret

;; ============================================================
;; print_str — テキストVRAMに文字列書き込み
;; 入力: ES = 0xA000, DI = TVRAMオフセット, DS:SI = 文字列
;; ============================================================
print_str:
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
        jmp     print_str
.done:
        ret

;; ============================================================
;; データ領域
;; ============================================================
loader_name:    db 'LOADER  BIN'   ;; 8.3形式ファイル名
msg_boot:       db 'PC-98 OS32 FAT12 Boot', 0
msg_noldr:      db 'No LOADER.BIN!', 0
msg_derr:       db 'Disk Error!', 0

var_cluster:    dw 0               ;; 一時変数: クラスタ番号

;; ブートセクタはちょうど 1 セクタ。**144 は 512B に収める**
;; (NP21/W は rpm=1 のとき 512 バイトしか読まない)。
;; 溢れたら nasm がここで止まる — 黙って切り詰めさせない。
        times (SECT_SZ - 2) - ($ - $$) db 0
        db      055h, 0AAh

