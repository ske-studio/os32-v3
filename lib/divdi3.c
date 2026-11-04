/* OS32 original integer division, derived from binary long division.
 * No libgcc/compiler-rt source or implementation is used.
 * The compiler ABI uses 64-bit long long and 32-bit unsigned int on i386.
 * Division by zero and signed MIN / -1 retain C's undefined contract.
 */
#include "types.h"

STATIC_ASSERT(sizeof(unsigned int) == 4 && sizeof(long long) == 8,
              division_helpers_require_32_64_bit_integers);

static unsigned long long divide_unsigned(unsigned long long value,
                                           unsigned long long divisor,
                                           unsigned long long *remainder)
{
    unsigned long long quotient = 0;
    unsigned long long rest = 0;
    unsigned int bits = 64;

    if ((divisor >> 32) == 0) {
        unsigned int den = (unsigned int)divisor;
        unsigned int high = (unsigned int)(value >> 32);
        quotient = (unsigned long long)(high / den) << 32;
        rest = high % den;
        bits = 32;
    }
    /* Each step brings down one dividend bit. For a wide divisor the
     * partial dividend has at most 64 bits; for a word divisor rest is
     * below 2^32 before the shift. Neither path can overflow rest. */
    for (unsigned int bit = bits; bit-- > 0;) {
        rest = (rest << 1) | ((value >> bit) & 1u);
        if (rest >= divisor) {
            rest -= divisor;
            quotient |= 1ULL << bit;
        }
    }
    if (remainder) *remainder = rest;
    return quotient;
}

unsigned long long __udivmoddi4(unsigned long long value,
                               unsigned long long divisor,
                               unsigned long long *remainder)
{
    return divide_unsigned(value, divisor, remainder);
}

unsigned long long __udivdi3(unsigned long long value, unsigned long long divisor)
{
    return divide_unsigned(value, divisor, 0);
}

unsigned long long __umoddi3(unsigned long long value, unsigned long long divisor)
{
    unsigned long long remainder;
    divide_unsigned(value, divisor, &remainder);
    return remainder;
}

long long __divmoddi4(long long value, long long divisor, long long *remainder)
{
    /* Negate in unsigned arithmetic so the magnitude of MIN is defined. */
    unsigned long long numerator = value < 0 ? 0ULL - (unsigned long long)value
                                            : (unsigned long long)value;
    unsigned long long denominator = divisor < 0 ? 0ULL - (unsigned long long)divisor
                                                : (unsigned long long)divisor;
    unsigned long long rest;
    unsigned long long quotient = divide_unsigned(numerator, denominator, &rest);
    if ((value < 0) != (divisor < 0)) quotient = 0ULL - quotient;
    if (value < 0) rest = 0ULL - rest;
    if (remainder) *remainder = (long long)rest;
    return (long long)quotient;
}

long long __divdi3(long long value, long long divisor)
{
    return __divmoddi4(value, divisor, 0);
}

long long __moddi3(long long value, long long divisor)
{
    long long remainder;
    __divmoddi4(value, divisor, &remainder);
    return remainder;
}
