#ifndef INPUT_HAL_INPUT_CONTEXT_H
#define INPUT_HAL_INPUT_CONTEXT_H

#include "types.h"

enum {
    INPUT_CONTEXT_LINE_EDIT = 1, INPUT_CONTEXT_RUNNING,
    INPUT_CONTEXT_BYTE_INPUT, INPUT_CONTEXT_GUI
};
enum { INPUT_CONTEXT_ISIG = 1, INPUT_CONTEXT_ICANON = 2 };

typedef struct {
    u32 context, flags, command_gen;
} InputContext;

void input_context_init(InputContext *state);
int input_context_set(InputContext *state, u32 context, u32 flags);

#endif
