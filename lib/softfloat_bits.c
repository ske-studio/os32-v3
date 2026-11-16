/* Integer-only DB conversion. The resident copy owns independent SF modes. */
#include "softfloat_bits.h"
#include "softfloat/softfloat.h"
i32 softfp_f64_to_i32_trunc(unsigned long long bits)
{
    uint_fast8_t rounding = softfloat_roundingMode;
    uint_fast8_t tininess = softfloat_detectTininess;
    uint_fast8_t flags = softfloat_exceptionFlags;
    uint_fast8_t precision = extF80_roundingPrecision;
    sw_float64_t value;
    i32 result;
    value.v = bits;
    softfloat_roundingMode = softfloat_round_near_even;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
    extF80_roundingPrecision = 80;
    result = f64_to_i32(value, softfloat_round_minMag, false);
    softfloat_roundingMode = rounding;
    softfloat_detectTininess = tininess;
    softfloat_exceptionFlags = flags;
    extF80_roundingPrecision = precision;
    return result;
}
