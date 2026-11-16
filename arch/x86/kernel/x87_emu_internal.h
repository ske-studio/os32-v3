/* OS32 private connections for the NP21/W ESC port. */
#ifndef OS32_X87_EMU_INTERNAL_H
#define OS32_X87_EMU_INTERNAL_H
#include "x87_emu.h"
#include "softfloat/softfloat.h"
#define X87_IE 0x0001
#define X87_DE 0x0002
#define X87_ZE 0x0004
#define X87_OE 0x0008
#define X87_UE 0x0010
#define X87_PE 0x0020
#define X87_SF 0x0040
#define X87_ESB 0x8080
#define X87_C0 0x0100
#define X87_C1 0x0200
#define X87_C2 0x0400
#define X87_TOP 0x3800
#define X87_C3 0x4000
#define X87_CC (X87_C0 | X87_C2 | X87_C3)
#define X87_ALL_COMMIT 0x3f
static inline unsigned x87_top(const struct x87_emu_state *s) { return (s->sw >> 11) & 7; }
static inline unsigned x87_st(const struct x87_emu_state *s, unsigned n) { return (x87_top(s) + n) & 7; }
static inline unsigned x87_tag(const struct x87_emu_state *s, unsigned n) { return (s->tw >> (n * 2)) & 3; }
static inline void x87_set_tag(struct x87_emu_state *s, unsigned n, unsigned tag) { s->tw = (s->tw & ~(3u << (n * 2))) | (tag << (n * 2)); }
static inline void x87_set_top(struct x87_emu_state *s, unsigned n) { s->sw = (s->sw & ~X87_TOP) | ((n & 7) << 11); }
unsigned long long x87_get_le(const u8 *p, unsigned n);
void x87_put_le(u8 *p, unsigned long long v, unsigned n);
sw_extFloat80_t x87_unpack(const struct x87_emu_ext80 *r);
void x87_pack(struct x87_emu_ext80 *r, sw_extFloat80_t v);
void x87_put(struct x87_emu_state *s, unsigned n, sw_extFloat80_t v);
sw_extFloat80_t x87_get(struct x87_emu_effect *e, unsigned n);
sw_extFloat80_t x87_indefinite(void);
void x87_push(struct x87_emu_effect *e, sw_extFloat80_t v);
void x87_pop(struct x87_emu_effect *e);
void x87_constant(struct x87_emu_effect *e, unsigned n);
void x87_compare_values(struct x87_emu_effect *e, sw_extFloat80_t a, sw_extFloat80_t b, bool ordered);
void x87_binary(struct x87_emu_effect *e, unsigned operation, sw_extFloat80_t a, sw_extFloat80_t b);
void x87_unary(struct x87_emu_effect *e, bool square_root);
void x87_fxam(struct x87_emu_effect *e);
sw_extFloat80_t x87_load32(struct x87_emu_effect *e, const u8 *p, bool integer);
void x87_store32(struct x87_emu_effect *e, bool integer);
enum x87_emu_result x87_emu_fetch(void *opaque, u32 offset, u8 *value);
#endif
