;; V86 ゲストのディスク読み出しテスト (Phase 3-3b)
;;
;;   INT 1Bh の READ DATA でシリンダ0/ヘッド0/セクタ1 を読み、
;;   結果を 0x8C00:0008 に記録して HLT する。
;;   カーネル側は読めた中身を loop_dev で直接読んだものと突き合わせる。
;;
;; 配置: CS = 0x8A00 (linear 0x8A000), offset 0
;; 転送先: 0x8D00:0000 (linear 0x8D000)
;; 結果:   0x8C00:0008

cpu 386
bits 16
org 0

DATA_SEG    equ 0x8C00
BUF_SEG     equ 0x8D00
SECLEN      equ 256      ; PC-98 2HD のブートトラックは 256B x 16

start:
        ;; 結果スロットを初期化
        mov     ax, DATA_SEG
        mov     ds, ax
        mov     word [8], 0

        ;; 転送先 ES:BP
        mov     ax, BUF_SEG
        mov     es, ax
        xor     bp, bp

        ;; CHS 指定
        ;;   CL = シリンダ, CH = セクタ長コード, DH = ヘッド, DL = セクタ
        mov     cl, 0                   ; シリンダ 0
        mov     ch, 1                   ; N=1 → 256 バイト/セクタ
        mov     dh, 0                   ; ヘッド 0
        mov     dl, 1                   ; セクタ 1 (1 起算)
        mov     bx, SECLEN              ; 転送バイト数

        ;; AH = コマンド, AL = DA/UA (最後に設定する)
        mov     ax, 0x0690              ; AH=06 READ DATA, AL=90 2HD FDD#1
        int     0x1B
        jc      .fail

        mov     word [8], 0xD15C        ; 成功マーカー
.fail:
        hlt
