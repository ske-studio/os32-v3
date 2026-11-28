.global _start
_start:
#ifdef __PPC__
    bl probe
    li 0,1
    sc
#else
    call probe
    mov %eax,%ebx
    mov $1,%eax
    int $0x80
#endif

/* Pass the raw KAPI word to the host WM fixture using Linux write(1, p, 4). */
.global write_gui_raw
write_gui_raw:
#ifdef __PPC__
    mr 4,3
    li 3,1
    li 5,4
    li 0,4
    sc
    blr
#else
    push %ebx
    mov 8(%esp),%ecx
    mov $1,%ebx
    mov $4,%edx
    mov $4,%eax
    int $0x80
    pop %ebx
    ret
#endif
