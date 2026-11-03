#ifndef KDEBUG_GF_COMMAND_TEST_H
#define KDEBUG_GF_COMMAND_TEST_H
#include "gf_command_wire.h"
#include "../hwdev/gf/gf2_test.h"
#include "recover.h"
/* Foreground synchronous loans, using the same owner/row policy as C9. */
int gf_command_test(struct gf2_request *, struct gf_foundation *,
                    int (*read_row)(void *, u32, u8 *), void *row_owner);
#endif
