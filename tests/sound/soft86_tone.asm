; PC-98 DOS COM. The V86 monitor reflects IRQ0 at 100 Hz.
bits 16
org 100h
start:
    mov ax, 3508h
    int 21h
    mov [old_vector], bx
    mov [old_vector+2], es
    mov dx, timer
    mov ax, 2508h
    int 21h
    in al, 02h
    mov [old_mask], al
    and al, 0feh
    out 02h, al
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
    lds dx, [old_vector]
    mov ax, 2508h
    int 21h
    mov ax, 4c00h
    int 21h

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
old_vector: dd 0
