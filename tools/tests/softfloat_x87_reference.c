/* Independent native x87 reference; integer bit boundaries only. */
typedef unsigned long long U64;
U64 hardware(U64 a, U64 b, int op)
{
    U64 result;
    switch (op) {
    case 0: __asm__ volatile("fldl %1; faddl %2; fstpl %0" : "=m"(result) : "m"(a),"m"(b) : "st"); break;
    case 1: __asm__ volatile("fldl %1; fsubl %2; fstpl %0" : "=m"(result) : "m"(a),"m"(b) : "st"); break;
    case 2: __asm__ volatile("fldl %1; fmull %2; fstpl %0" : "=m"(result) : "m"(a),"m"(b) : "st"); break;
    default: __asm__ volatile("fldl %1; fdivl %2; fstpl %0" : "=m"(result) : "m"(a),"m"(b) : "st"); break;
    }
    return result;
}
unsigned relation(U64 a, U64 b)
{
    unsigned short sw;
    __asm__ volatile("fldl %2; fldl %1; fucompp; fnstsw %%ax" : "=a"(sw) : "m"(a),"m"(b) : "st","st(1)");
    return sw & 0x4500;
}
long long hardware_fix(U64 bits)
{
    unsigned short cw, trunc;
    long long value;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    trunc=(cw & ~0xc00u)|0xc00u;
    __asm__ volatile("fldcw %1; fldl %2; fistpll %0; fldcw %3"
        : "=m"(value) : "m"(trunc),"m"(bits),"m"(cw) : "st");
    return value;
}
U64 hardware_float(long long value)
{
    U64 bits;
    __asm__ volatile("fildll %1; fstpl %0" : "=m"(bits) : "m"(value) : "st");
    return bits;
}
unsigned hardware_narrow(U64 bits)
{
    unsigned result;
    __asm__ volatile("fldl %1; fstps %0" : "=m"(result) : "m"(bits) : "st");
    return result;
}
U64 hardware_extend(unsigned bits)
{
    U64 result;
    __asm__ volatile("flds %1; fstpl %0" : "=m"(result) : "m"(bits) : "st");
    return result;
}
