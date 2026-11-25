/* OS32-owned x87 integration contract. No native FP types or packed C ABI.
 * Implementations belong to NOFPU_DESIGN waves B/C; nothing links in wave A. */
#ifndef OS32_X87_EMU_H
#define OS32_X87_EMU_H

#include "types.h"
#include "exec_arch.h"
#include <stdbool.h>

#define X87_EMU_INSN_BYTES 15
#define X87_EMU_MEMORY_BYTES 108
#define X87_EMU_REGISTERS 8
#define X87_EMU_EXT_BYTES 10
#define X87_EMU_ENV_BYTES 28
#define X87_EMU_ENV16_BYTES 14
#define X87_EMU_SAVE16_BYTES 94

enum x87_emu_gpr { X87_EAX, X87_ECX, X87_EDX, X87_EBX,
                   X87_ESP, X87_EBP, X87_ESI, X87_EDI };
enum x87_emu_segment { X87_ES, X87_CS, X87_SS, X87_DS, X87_FS, X87_GS,
                       X87_SEGMENTS };
/* A normalized view, NOT the assembler stack layout. gpr[ESP] and segment[SS]
 * come from the privilege-transition user ESP/SS, never PUSHAD's saved ESP.
 * C3/C4 translate saved registers/segments and write back only committed data. */
struct x87_emu_trap {
    u32 gpr[X87_EMU_REGISTERS];
    u32 eip, eflags;
    u16 segment[X87_SEGMENTS];
};
/* Canonical LE byte arrays; no SoftFloat struct padding enters a save image. */
struct x87_emu_ext80 { u8 bytes[X87_EMU_EXT_BYTES]; };
struct x87_emu_state {
    u16 cw, reserved_cw, sw, reserved_sw, tw, reserved_tw;
    u32 fip;
    u16 fcs, fop;
    u32 fdp;
    u16 fds, reserved_fds;
    /* Physical R0..R7; wire FNSAVE order is logical ST0..ST7 via SW.TOP. */
    struct x87_emu_ext80 reg[X87_EMU_REGISTERS];
};
enum x87_emu_result {
    X87_EMU_OK = 0, X87_EMU_UNSUPPORTED, X87_EMU_MEMORY_FAULT,
    X87_EMU_PENDING_MF, X87_EMU_NOT_USER, X87_EMU_REENTERED
};
/* Result OK includes deferred unmasked exceptions: commit only effects allowed
 * by that exception, set ES/B, advance EIP; next waiting op returns PENDING_MF.
 * Fault/unsupported/pending results leave memory, state, GPR and EIP unchanged. */
enum x87_emu_commit {
    X87_COMMIT_STATUS = 1u << 0, X87_COMMIT_ENV = 1u << 1,
    X87_COMMIT_REGS = 1u << 2, X87_COMMIT_MEMORY = 1u << 3,
    X87_COMMIT_GPR = 1u << 4, X87_COMMIT_EIP = 1u << 5
};
struct x87_emu_decoded {
    u32 offset;                 /* segment-relative EA, not a host pointer */
    u16 memory_bytes;           /* 0 for register/WAIT; at most 108 */
    u8 length, opcode, modrm, sib;
    u8 segment;                /* enum x87_emu_segment, including SS default */
    bool operand16, address16, memory, write, waiting;
    /* 0x9b is a separate one-byte WAIT; ESC is 0xd8..0xdf. */
};
struct addrspace;
struct x87_emu_access {
    const struct addrspace *as; /* borrowed live owner; IRQs held throughout */
    const struct x87_emu_trap *trap;
};
/* Fetch only requested bytes, checking CS limit and all owner/USER pages.
 * Decode uses this callback and the normalized GPRs; it does not mutate them. */
typedef enum x87_emu_result (*x87_emu_fetch_fn)(void *opaque, u32 offset,
                                              u8 *value);
struct x87_emu_effect {
    struct x87_emu_state state; /* speculative copy of the active state */
    u32 gpr[X87_EMU_REGISTERS];
    u32 next_eip, commit;       /* enum x87_emu_commit mask */
    u16 raised;                /* x87 IE/DE/ZE/OE/UE/PE/SF/C1, not SF flags */
    u8 output[X87_EMU_MEMORY_BYTES];
};
struct x87_emu_softfloat_scope {
    u8 rounding_mode, detect_tininess, exception_flags, ext80_precision;
};

enum x87_emu_result x87_emu_decode(const struct x87_emu_trap *trap,
    x87_emu_fetch_fn fetch, void *opaque, struct x87_emu_decoded *out);
/* Validate segment, overflow, every page, owner/lease and read/write access.
 * Write validates the ENTIRE range before any copy; rejects MMIO. No yield. */
enum x87_emu_result x87_emu_read(const struct x87_emu_access *access,
    const struct x87_emu_decoded *insn, u8 *buffer);
enum x87_emu_result x87_emu_write(const struct x87_emu_access *access,
    const struct x87_emu_decoded *insn, const u8 *buffer);
/* new_owner clears all bytes; FINIT/FNSAVE retain register payloads. */
void x87_emu_state_init(struct x87_emu_state *state, bool new_owner);
void x87_emu_state_encode(const struct x87_emu_state *state, u8 *image,
                         bool operand16, bool environment_only);
void x87_emu_state_decode(struct x87_emu_state *state, const u8 *image,
                         bool operand16, bool environment_only);
/* ESC handlers select and execute together, using only speculative effects.
 * Memory is fetched through access; stores remain staged until dispatch. */
enum x87_emu_result x87_emu_execute(const struct x87_emu_trap *trap,
    const struct x87_emu_access *access, struct x87_emu_decoded *insn,
    struct x87_emu_effect *effect);
void x87_emu_scope_begin(u16 cw, struct x87_emu_softfloat_scope *saved);
void x87_emu_scope_end(const struct x87_emu_softfloat_scope *saved,
                       struct x87_emu_effect *effect);
/* Apply instruction-specific suppression/completion (including DE, stack
 * faults, C1 and priority); do not treat all unmasked exceptions alike. */
enum x87_emu_result x87_emu_exception(const struct x87_emu_state *before,
    const struct x87_emu_decoded *insn, struct x87_emu_effect *effect);
/* Sole commit owner. Checks CPL3, !VM, live owner, !present, CR0.EM, no reentry.
 * After policy: memory -> state/GPR -> EIP. Only OK returns via IRET.
 * active is the one cooperative software context; no new AppSlot storage. */
enum x87_emu_result x87_emu_dispatch(struct x87_emu_trap *trap,
    const struct addrspace *as, struct x87_emu_state *active);

#endif
