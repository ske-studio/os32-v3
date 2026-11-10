/* Software x86-shaped tables; native u32 words, never LE wire data. */
#ifndef OS32_PPC_PD_H
#define OS32_PPC_PD_H
#include "types.h"

struct ppc_translation { u32 pa, attr; };
/* Returns one native word from a physical address. No allocation or writes. */
typedef u32 (*ppc_pd_read_fn)(u32 pa, void *context);

/* DR=0, Ks=0/Kp=1. 1=permitted translation, 0=absent/protected/unsupported.
 * pa includes the page offset; attr is the HPT low-word WIMG|PP encoding.
 * Failure leaves *out unchanged. Supervisor stores ignore software RW (WP=0).
 * The default reader dereferences physical addresses with translation off. */
int ppc_pd_lookup(u32 root, u32 va, int store, int user,
                  struct ppc_translation *out);
int ppc_pd_lookup_read(u32 root, u32 va, int store, int user,
                      struct ppc_translation *out,
                      ppc_pd_read_fn read_word, void *context);
#endif
