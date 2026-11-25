#ifndef OS32_EXEC_SHLIB_H
#define OS32_EXEC_SHLIB_H
#include "exec_hooks.h"
#if OS32_HOOK_SHLIB
#include "shlib.h"
#define exec_sound_acquire shlib_sound_acquire
#define exec_sound_attach shlib_sound_attach
#define exec_sound_data_pages shlib_sound_data_pages
static inline int exec_shlib_addrspace_attach(struct addrspace *as) { return shlib_addrspace_attach(as); }
static inline void exec_shlib_addrspace_detach(struct addrspace *as) { shlib_addrspace_detach(as); }
static inline int exec_shlib_loaded(void) { return shlib_loaded(); }
static inline u32 exec_shlib_data_pages(void) { return shlib_data_pages(); }
static inline int exec_shlib_read_page(u32 va, u32 frame) { return shlib_read_page(va, frame); }
#else
#include "types.h"
struct addrspace;
static inline int exec_sound_acquire(int (*cancelled)(void)) { (void)cancelled; return -1; }
static inline int exec_sound_attach(struct addrspace *as) { (void)as; return -1; }
static inline u32 exec_sound_data_pages(void) { return 0; }
int exec_shlib_addrspace_attach(struct addrspace *as);
void exec_shlib_addrspace_detach(struct addrspace *as);
int exec_shlib_loaded(void);
u32 exec_shlib_data_pages(void);
int exec_shlib_read_page(u32 va, u32 frame);
#endif
#endif
