#include "hwdev_contract.h"
u32 hwdev_import_slots[HWDEV_IMPORT_COUNT]
__attribute__((section(".data.hwdev_import_slots"), aligned(4))) = {0};
