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
