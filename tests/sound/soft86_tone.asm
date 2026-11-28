; PC-98 2HD IPL loaded at 1fc0:0000. IRQ0 is reflected at 100 Hz.
bits 16
org 0
start:
    cli
    mov ax, cs
    mov ss, ax
    mov sp, 0400h
    xor ax, ax
    mov ds, ax
    mov word [8*4], timer
    mov [8*4+2], cs
    push cs
    pop ds
    in al, 02h
    mov [old_mask], al
    and al, 0feh
    out 02h, al
    sti
    cld
    mov si, tone
    mov cx, (tone_end-tone)/2
.write:
    lodsw
    call opna
    loop .write
    mov bx, 200
    call wait_ticks
    mov si, key_off
    lodsw
    call opna
    mov bx, 100
    call wait_ticks
    mov al, [old_mask]
    out 02h, al
    cli
    hlt

wait_ticks:
    mov cx, [ticks]
.wait:
    mov ax, [ticks]
    sub ax, cx
    cmp ax, bx
    jb .wait
    ret

timer:
    push ax
    inc word [cs:ticks]
    mov al, 20h
    out 00h, al
    pop ax
    iret

opna:
    mov dx, 188h
    out dx, al
    inc dx
    inc dx
    mov al, ah
    out dx, al
    ret

tone: db 30h,1, 40h,12, 50h,31, 80h,15, 0b0h,7, 0b4h,80h, 0a4h,22h, 0a0h,69h, 28h,10h
tone_end:
key_off: db 28h,0
ticks: dw 0
old_mask: db 0
; Keep the code, data and stack in the sector loaded by boot2.
times 1024-64-($-$$) db 0
times 64 db 0
times 77*2*8*1024-($-$$) db 0
