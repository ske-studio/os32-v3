/* Build-selected private default renderer. */
#ifndef OS32_IME_DEFAULT_RENDER_H
#define OS32_IME_DEFAULT_RENDER_H
#include "sys/kern/ime/ime_render.h"
extern const IME_Render g_ime_render_tvram;
#define IME_DEFAULT_RENDER &g_ime_render_tvram
#endif
