/* Initial x86 cdecl frame and atomic privilege/stack transition. */
#include "exec_arch.h"
#include "abi.h"
#include "cpu.h"
#include "gdt.h"
#include "tss.h"

extern struct tss_entry kernel_tss;
struct x86_entry { u32 sp, ip, user, saved_sp; };
STATIC_ASSERT(sizeof(struct arch_entry_context) == sizeof(struct x86_entry), entry_size);
STATIC_ASSERT(__alignof__(struct arch_entry_context) == __alignof__(struct x86_entry), entry_align);

int arch_entry_prepare(struct arch_entry_context *out, u32 top, u32 entry,
                       int user, const struct exec_entry_args *args,
                       int (*store)(void *, u32, const void *, u32), void *owner)
{
    u32 frame[5];
    struct x86_entry ctx;
    if (top < 16) return -1;
    ctx.sp = (top - 16) & ~(u32)(ABI_STACK_ALIGN - 1);
    if (user && ctx.sp < 4) return -1;
    if (user) ctx.sp -= 4;
    frame[0] = 0; /* iret supplies no return address. */
    frame[1] = args->argc;
    frame[2] = args->argv;
    frame[3] = args->api;
    frame[4] = args->aux;
    if (store(owner, ctx.sp, user ? frame : frame + 1, user ? 20 : 16))
        return -1;
    ctx.ip = entry;
    ctx.user = user;
    ctx.saved_sp = 0;
    __builtin_memcpy(out->opaque, &ctx, sizeof(ctx));
    return 0;
}

/* ARCH-ASM-OK: CLI through iret must stay atomic with ESP0 and CR3. */
#define arch_enter_user(root_phys, user_sp, entry)                      \
do {                                                                    \
    __asm__ volatile(                                                   \
        "cli\n\t"                                                       \
        "movl %%esp, %[e0]\n\t"    /* TSS.ESP0 = 現在のカーネル ESP */  \
        "movl %[pd], %[saved_pd]\n\t"                                  \
        "movl %[pd], %%cr3\n\t"    /* アプリ PD へ切替 */               \
        "movl %[uds], %%eax\n\t"                                        \
        "movw %%ax, %%ds\n\t"                                           \
        "movw %%ax, %%es\n\t"                                           \
        "movw %%ax, %%fs\n\t"                                           \
        "movw %%ax, %%gs\n\t"                                           \
        "pushl %[uds]\n\t"         /* SS = USER_DS */                   \
        "pushl %[uesp]\n\t"        /* ESP = ユーザスタック */           \
        "pushl %[uflags]\n\t"      /* EFLAGS: IF=1, IOPL=0 */           \
        "pushl %[ucs]\n\t"         /* CS = USER_CS */                   \
        "pushl %[eip]\n\t"         /* EIP = エントリポイント */         \
        "iret\n\t"                                                      \
        : [e0] "=m"(kernel_tss.esp0),                                   \
          [saved_pd] "=m"(kernel_tss.cr3)                               \
        : [pd]  "r"(root_phys),                                         \
          [uesp]"r"(user_sp),                                           \
          [eip] "r"(entry),                                             \
          [uds] "i"(USER_DS),                                           \
          [ucs] "i"(USER_CS),                                           \
          [uflags] "i"(EFLAGS_IF | EFLAGS_RESERVED1)                    \
        : "eax", "memory"                                               \
    );                                                                  \
} while (0)

/* Tests replace only the final privileged/ASM transition observation points. */
#ifndef ARCH_ENTRY_USER
#define ARCH_ENTRY_USER(pd, sp, ip) arch_enter_user(pd, sp, ip)
#endif
#ifndef ARCH_ENTRY_CALL
#define ARCH_ENTRY_CALL(saved, sp, ip) \
    __asm__ volatile( \
        "movl %%esp, (%0)\n\t" \
        "movl %1, %%esp\n\t" \
        "call *%2\n\t" \
        "movl (%0), %%esp" \
        : : "r"(saved), "r"(sp), "r"(ip) \
        : "eax", "ecx", "edx", "cc", "memory")
#endif

void arch_entry_enter(struct arch_entry_context *entry, u32 address_space)
{
    struct x86_entry ctx;
    __builtin_memcpy(&ctx, entry->opaque, sizeof(ctx));
    if (ctx.user) {
        /* ARCH-ASM-OK: CLI -> ESP0 -> CR3 -> segments -> iret is indivisible. */
        ARCH_ENTRY_USER(address_space, ctx.sp, ctx.ip);
        __builtin_unreachable();
    }
    /* saved_sp belongs to the slot, never the switched-away local stack.
     * All three register operands survive a cdecl call (volatile regs clobbered). */
    ARCH_ENTRY_CALL(entry->opaque + 12, ctx.sp, ctx.ip);
}
