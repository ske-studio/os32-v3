/* Internal target contract; included only by tools/abi/type_contract.c. */
#ifndef TYPE_CONTRACT_H
#define TYPE_CONTRACT_H
#include "types.h"
#include "arch_type_contract.h"

enum type_contract_enum { TYPE_CONTRACT_ZERO, TYPE_CONTRACT_MINUS = -1 };
struct type_contract_u16 { char c; u16 n; };
struct type_contract_u32 { char c; u32 n; };
struct type_contract_u64 { char c; unsigned long long n; };
STATIC_ASSERT(sizeof(int) == 4, type_int_width);
STATIC_ASSERT(sizeof(long) == 4, type_long_width);
STATIC_ASSERT(sizeof(void *) == 4, type_pointer_width);
STATIC_ASSERT(__CHAR_BIT__ == 8 && (char)-1 < 0, type_signed_char_8);
STATIC_ASSERT(sizeof(short) == 2, type_short_width);
STATIC_ASSERT(sizeof(enum type_contract_enum) == 4, type_enum_width);
STATIC_ASSERT(sizeof(unsigned long long) == 8, type_u64_width);
STATIC_ASSERT(sizeof(u8) == 1 && (u8)-1 > 0, type_u8);
STATIC_ASSERT(sizeof(i8) == 1 && (i8)-1 < 0, type_i8);
STATIC_ASSERT(sizeof(u16) == 2 && (u16)-1 > 0, type_u16);
STATIC_ASSERT(sizeof(i16) == 2 && (i16)-1 < 0, type_i16);
STATIC_ASSERT(sizeof(u32) == 4 && (u32)-1 > 0, type_u32);
STATIC_ASSERT(sizeof(i32) == 4 && (i32)-1 < 0, type_i32);
STATIC_ASSERT(sizeof(uptr) == 4 && (uptr)-1 > 0, type_uptr);
STATIC_ASSERT(__builtin_offsetof(struct type_contract_u16, n) == 2, type_u16_member_align);
STATIC_ASSERT(__builtin_offsetof(struct type_contract_u32, n) == 4, type_u32_member_align);
STATIC_ASSERT(__builtin_offsetof(struct type_contract_u64, n) == ARCH_U64_MEMBER_ALIGN,
              type_u64_member_align);
STATIC_ASSERT(__BYTE_ORDER__ == ARCH_TYPE_BYTE_ORDER, type_byte_order);
#endif
