#ifndef MACHCFG_CONTRACT_H
#define MACHCFG_CONTRACT_H

#include "types.h"

#define MACHCFG_FILE_MAX (1024 - 1)
#define MACHCFG_LINE_MAX 95

struct machcfg { u8 perf_request, nap7450; };
struct machcfg_seed { struct machcfg defaults; u8 accepts_nap7450; };

struct machcfg_seed platform_machcfg_seed(void);
void machcfg_begin(struct machcfg_seed seed);
void machcfg_finish(const struct machcfg *values);
const struct machcfg *machcfg_get(void);

#define MACHCFG_FILE_NOFS 0
#define MACHCFG_FILE_NONE 1
#define MACHCFG_FILE_BIG  2
#define MACHCFG_FILE_OK   3
#define MACHCFG_KEY_PERF  0x01
#define MACHCFG_KEY_NAP   0x02

void machcfg_note_file(u8 result, u8 keys);
void machcfg_report(const char *machine, u32 cpu_id, int seed_from_cpu);

#endif
