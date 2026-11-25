/* libc-free i386 fixture. x87 instructions are the independent reference;
 * QEMU execution is a reference under emulation, not physical hardware. */
#include "lib/softfloat/softfloat.h"
#include "lib/softfloat_bits.h"
#include "lib/sqlite3/sqlite3.h"
#include "sys/kern/kstring.h"
typedef unsigned long long U64;
typedef union { double d; U64 u; } Bits;
extern double __adddf3(double, double), __subdf3(double, double);
extern double __muldf3(double, double), __divdf3(double, double), __negdf2(double);
extern int __eqdf2(double,double), __nedf2(double,double), __ltdf2(double,double);
extern int __ledf2(double,double), __gtdf2(double,double), __gedf2(double,double);
extern int __cmpdf2(double,double), __unorddf2(double,double);
extern int __fixdfsi(double);
extern long long __fixdfdi(double);
extern unsigned __fixunsdfsi(double);
extern U64 __fixunsdfdi(double);
extern double __floatsidf(int), __floatdidf(long long);
extern double __floatunsidf(unsigned), __floatundidf(U64);
extern double __extendsfdf2(float);
extern float __truncdfsf2(double);
static void exit_test(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) exit_test(1); } while (0)
extern U64 hardware(U64, U64, int);
extern unsigned relation(U64, U64);
extern long long hardware_fix(U64);
extern U64 hardware_float(long long), hardware_extend(unsigned);
extern unsigned hardware_narrow(U64);
static int isnan_bits(U64 x) { return (x & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL; }
static int fixed_time(sqlite3_vfs *v, sqlite3_int64 *out)
{
    (void)v; *out=210866760000000LL + 1700000000000LL; return SQLITE_OK;
}
int sqlite3_os_init(void)
{
    static sqlite3_vfs vfs={.iVersion=2, .szOsFile=sizeof(sqlite3_file),
        .mxPathname=256, .zName="nofpu-host", .xCurrentTimeInt64=fixed_time};
    return sqlite3_vfs_register(&vfs,1);
}
int sqlite3_os_end(void) { return SQLITE_OK; }
int sqlite3DbIsNamed(void *db, int i, const char *name)
{ (void)db; (void)name; return i==0; }
void *malloc(unsigned long n) { (void)n; exit_test(2); return 0; }
void *realloc(void *p, unsigned long n) { (void)p; (void)n; exit_test(2); return 0; }
void free(void *p) { (void)p; exit_test(2); }
/* Mutation-only libc seams: omitted datetime must reach a runtime verdict,
 * rather than fail to link its reduced current_timestamp implementation. */
void *gmtime(const long *t) { (void)t; exit_test(2); return 0; }
unsigned long strftime(char *p, unsigned long n, const char *f, const void *t)
{ (void)p; (void)n; (void)f; (void)t; exit_test(2); return 0; }
static void sql_test(void)
{
    static unsigned char heap[512*1024] __attribute__((aligned(8)));
    sqlite3 *db=0; sqlite3_stmt *stmt=0;
    CHECK(sqlite3_config(SQLITE_CONFIG_HEAP,heap,sizeof(heap),32)==SQLITE_OK);
    CHECK(sqlite3_initialize()==SQLITE_OK);
    CHECK(sqlite3_open(":memory:",&db)==SQLITE_OK);
    CHECK(sqlite3_prepare_v2(db,
        "SELECT date('now'), time('now'), strftime('%Y-%m-%d %H:%M:%S','now'), "
        "julianday('1970-01-01'), 1.5+2.75, 3.0/2.0, -2.75, 1.5<2.0",
        -1,&stmt,0)==SQLITE_OK);
    CHECK(sqlite3_step(stmt)==SQLITE_ROW);
    CHECK(!kstrcmp((const char *)sqlite3_column_text(stmt,0),"2023-11-14"));
    CHECK(!kstrcmp((const char *)sqlite3_column_text(stmt,1),"22:13:20"));
    CHECK(!kstrcmp((const char *)sqlite3_column_text(stmt,2),"2023-11-14 22:13:20"));
    Bits b;
    b.d=sqlite3_column_double(stmt,3); CHECK(b.u==0x41429ec5c0000000ULL);
    b.d=sqlite3_column_double(stmt,4); CHECK(b.u==0x4011000000000000ULL);
    b.d=sqlite3_column_double(stmt,5); CHECK(b.u==0x3ff8000000000000ULL);
    b.d=sqlite3_column_double(stmt,6); CHECK(softfp_f64_to_i32_trunc(b.u)==-2);
    CHECK(sqlite3_column_int(stmt,7)==1);
    CHECK(sqlite3_finalize(stmt)==SQLITE_OK);
    CHECK(sqlite3_close(db)==SQLITE_OK);
    CHECK(sqlite3_shutdown()==SQLITE_OK);
}
void _start(void)
{
    static const U64 values[] = {
        0, 0x8000000000000000ULL, 0x3ff8000000000000ULL,
        0xc006000000000000ULL, 1, 0x0010000000000000ULL,
        0x7fefffffffffffffULL, 0x7ff0000000000000ULL,
        0xfff0000000000000ULL, 0x7ff8000000000123ULL,
        0x7ff0000000000001ULL
    };
    __asm__ volatile("fninit");
    for (unsigned i=0; i<sizeof(values)/sizeof(*values); i++) {
        for (unsigned j=0; j<sizeof(values)/sizeof(*values); j++) {
            Bits a={.u=values[i]}, b={.u=values[j]}, r;
            for (int op=0; op<4; op++) {
                switch(op) {
                case 0: r.d=__adddf3(a.d,b.d); break;
                case 1: r.d=__subdf3(a.d,b.d); break;
                case 2: r.d=__muldf3(a.d,b.d); break;
                default: r.d=__divdf3(a.d,b.d); break;
                }
                U64 hw=hardware(a.u,b.u,op);
                CHECK(isnan_bits(hw) == isnan_bits(r.u));
                CHECK(r.u==hw);
            }
            unsigned rel=relation(a.u,b.u);
            int unordered=rel==0x4500, eq=rel==0x4000;
            int cmp=eq?0:rel==0x100?-1:1;
            CHECK((__eqdf2(a.d,b.d)==0)==eq);
            CHECK((__nedf2(a.d,b.d)==0)==eq);
            CHECK(__unorddf2(a.d,b.d)==unordered);
            CHECK(__ltdf2(a.d,b.d)==cmp && __ledf2(a.d,b.d)==cmp);
            CHECK(__cmpdf2(a.d,b.d)==cmp);
            CHECK(__gtdf2(a.d,b.d)==(unordered?-1:cmp));
            CHECK(__gedf2(a.d,b.d)==(unordered?-1:cmp));
        }
    }
    Bits a={.u=0xc006000000000000ULL}, r;
    r.d=__negdf2(a.d); CHECK(r.u==0x4006000000000000ULL);
    CHECK(__fixdfsi(a.d)==hardware_fix(a.u) && __fixdfdi(a.d)==hardware_fix(a.u));
    a.u=0x4006000000000000ULL;
    CHECK(__fixunsdfsi(a.d)==(unsigned)hardware_fix(a.u));
    CHECK(__fixunsdfdi(a.d)==(U64)hardware_fix(a.u));
    r.d=__floatsidf(-2147483647-1); CHECK(r.u==hardware_float(-2147483647-1));
    r.d=__floatdidf(-4294967296LL); CHECK(r.u==hardware_float(-4294967296LL));
    r.d=__floatunsidf(4294967295U); CHECK(r.u==hardware_float(4294967295LL));
    r.d=__floatundidf(18446744073709551615ULL); CHECK(r.u==0x43f0000000000000ULL);
    union {float f; unsigned u;} f={.u=0x3fc00000};
    r.d=__extendsfdf2(f.f); CHECK(r.u==hardware_extend(f.u));
    f.f=__truncdfsf2(r.d); CHECK(f.u==hardware_narrow(r.u));
    /* DB truncation restores all four globals even for invalid inputs. */
    softfloat_roundingMode=softfloat_round_max;
    softfloat_detectTininess=softfloat_tininess_beforeRounding;
    softfloat_exceptionFlags=7; extF80_roundingPrecision=32;
    CHECK(softfp_f64_to_i32_trunc(0xc006000000000000ULL)==-2);
    CHECK(softfp_f64_to_i32_trunc(0x3ff8000000000000ULL)==1);
    CHECK(softfp_f64_to_i32_trunc(0x41e0000000000000ULL)==(-2147483647-1));
    CHECK(softfp_f64_to_i32_trunc(0x7ff0000000000000ULL)==(-2147483647-1));
    CHECK(softfp_f64_to_i32_trunc(0x7ff8000000000001ULL)==(-2147483647-1));
    CHECK(softfloat_roundingMode==softfloat_round_max);
    CHECK(softfloat_detectTininess==softfloat_tininess_beforeRounding);
    CHECK(softfloat_exceptionFlags==7 && extF80_roundingPrecision==32);
    softfloat_roundingMode=softfloat_round_near_even;
    sql_test();
    exit_test(0);
}
