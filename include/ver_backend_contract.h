#ifndef VER_BACKEND_CONTRACT_H
#define VER_BACKEND_CONTRACT_H

#include "ver_info.h"

typedef struct {
    OS32_VerLine *line;
    const char *label;
    u32 used;
    int overflow;
} VerLineBuilder;

VerLineBuilder ver_line(OS32_VerInfo *out, u32 kind, const char *label);
void ver_text(VerLineBuilder *b, const char *text);
void ver_saved_text(VerLineBuilder *b, const char *text, u32 size);
void ver_number(VerLineBuilder *b, u32 value, int hex_digits);
void ver_end(VerLineBuilder *b);

struct ver_backend {
    const char *head;
    void (*collect)(VerSnapshot *s);
    void (*devices)(const VerSnapshot *s, OS32_VerInfo *out);
    void (*image)(const VerSnapshot *s, OS32_VerInfo *out);
    void (*debug)(const VerSnapshot *s, OS32_VerInfo *out);
};

#endif
