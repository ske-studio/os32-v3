#ifndef MACHCFG_CONTRACT_H
#define MACHCFG_CONTRACT_H

#include "types.h"

#define MACHCFG_FILE_MAX (1024 - 1)
#define MACHCFG_LINE_MAX 95

struct machcfg { u8 perf_request, nap7450; };
struct machcfg_seed { struct machcfg defaults; u8 accepts_nap7450; };

void machcfg_begin(struct machcfg_seed seed);
void machcfg_finish(const struct machcfg *values);
const struct machcfg *machcfg_get(void);

#endif
