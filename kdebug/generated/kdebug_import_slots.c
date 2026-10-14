#include "kdebug_imports.h"
unsigned long kdebug_import_slots[KDEBUG_IMPORT_COUNT]
__attribute__((section(".data.kdebug_import_slots"), aligned(4))) = {0};
