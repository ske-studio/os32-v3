/* OS32-owned GCC soft ABI adapters; no native FP operations or casts.
 * Compile callers AND this file with -msoft-float -mno-fp-ret-in-387. */
#include "softfloat/softfloat.h"
typedef union { double value; unsigned long long bits; } F64Bits;
typedef union { float value; unsigned int bits; } F32Bits;
static sw_float64_t unpack(double value)
{
    F64Bits b; sw_float64_t f;
    b.value = value; f.v = b.bits; return f;
}
static double pack(sw_float64_t f)
{
    F64Bits b; b.bits = f.v; return b.value;
}
static int nan64(sw_float64_t f)
{
    return (f.v & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL;
}
double __adddf3(double a, double b) { return pack(f64_add(unpack(a), unpack(b))); }
double __subdf3(double a, double b) { return pack(f64_sub(unpack(a), unpack(b))); }
double __muldf3(double a, double b) { return pack(f64_mul(unpack(a), unpack(b))); }
double __divdf3(double a, double b) { return pack(f64_div(unpack(a), unpack(b))); }
double __negdf2(double a)
{
    sw_float64_t f = unpack(a); f.v ^= 0x8000000000000000ULL; return pack(f);
}
static int compare(double a, double b, int unordered)
{
    sw_float64_t x = unpack(a), y = unpack(b);
    if (nan64(x) || nan64(y)) return unordered;
    if (f64_eq(x, y)) return 0;
    return f64_lt(x, y) ? -1 : 1;
}
int __eqdf2(double a, double b) { return compare(a, b, 1); }
int __nedf2(double a, double b) { return compare(a, b, 1); }
int __ltdf2(double a, double b) { return compare(a, b, 1); }
int __ledf2(double a, double b) { return compare(a, b, 1); }
int __gtdf2(double a, double b) { return compare(a, b, -1); }
int __gedf2(double a, double b) { return compare(a, b, -1); }
int __cmpdf2(double a, double b) { return compare(a, b, 1); }
int __unorddf2(double a, double b) { return nan64(unpack(a)) || nan64(unpack(b)); }
int __fixdfsi(double a) { return f64_to_i32(unpack(a), softfloat_round_minMag, false); }
long long __fixdfdi(double a) { return f64_to_i64(unpack(a), softfloat_round_minMag, false); }
unsigned int __fixunsdfsi(double a) { return f64_to_ui32(unpack(a), softfloat_round_minMag, false); }
unsigned long long __fixunsdfdi(double a) { return f64_to_ui64(unpack(a), softfloat_round_minMag, false); }
double __floatsidf(int a) { return pack(i32_to_f64(a)); }
double __floatdidf(long long a) { return pack(i64_to_f64(a)); }
double __floatunsidf(unsigned int a) { return pack(ui32_to_f64(a)); }
double __floatundidf(unsigned long long a) { return pack(ui64_to_f64(a)); }
double __extendsfdf2(float a)
{
    F32Bits b; sw_float32_t f; b.value = a; f.v = b.bits;
    return pack(f32_to_f64(f));
}
float __truncdfsf2(double a)
{
    F32Bits b; b.bits = f64_to_f32(unpack(a)).v; return b.value;
}
