/* Keep the existing printf entry point; the platform owns the conversion. */
#include "kprintf.h"
#include "console_attr.h"

u8 kprintf_attr_to_pc98(u8 attr)
{
    return con_backend_printf_attr(attr);
}
