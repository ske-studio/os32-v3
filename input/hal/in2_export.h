#ifndef INPUT_HAL_IN2_EXPORT_H
#define INPUT_HAL_IN2_EXPORT_H

#include "in2_int.h"
#include "os32_in2.h"

void in2_export(const InputEvent *event, u16 seq, IN2_Event *out);

#endif
