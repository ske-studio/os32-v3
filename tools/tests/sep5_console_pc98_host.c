/* C1: the PC-98 backend's geometry, default colour and route guard (product text). */
#include "types.h"
#include "pc98.h"
#include "os32_kapi_shared.h"
#include "console_backend.h"
#include "pc98_route.h"
#ifndef ATTR_WHITE
#define ATTR_WHITE TATTR_WHITE
#endif
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#ifndef NO_GUARD
static int route_safe = 1;
int pc98_console_safe(void) { return route_safe; }
#endif
#include "console_pc98_slice.inc"
void _start(void)
{
    const struct con_geometry *g = con_backend_geometry();
    if (g->cols != 80 || g->rows != 25 || g != con_backend_geometry()) finish(2);
    if (con_backend_color(CON_COLOR_DEFAULT) != 0xE1) finish(3);
#ifdef NO_GUARD
    if (!con_backend_safe()) finish(5);      /* guard not linked: always writable */
#else
    if (!con_backend_safe()) finish(4);
    route_safe = 0;
    if (con_backend_safe()) finish(4);
#endif
    finish(0);
}
