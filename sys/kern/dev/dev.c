#include "../../hal/block/dev.h"
#include "dev_name.h"

int dev_name_present(const char *name)
{
    return dev_find(name) != 0;
}
