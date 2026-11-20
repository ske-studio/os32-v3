#include "fep_suspend_owner.h"

static int suspend_owner;

void fep_suspend_owner_note(int on, int rc, int owner)
{
    if (!on) suspend_owner = 0;
    else if (!rc) suspend_owner = owner;
}

int fep_suspend_owner_take(int owner)
{
    if (owner <= 0 || owner != suspend_owner) return 0;
    suspend_owner = 0;
    return 1;
}
