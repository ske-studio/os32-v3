#include "os32api.h"
#include <string.h>

int main(int argc, char **argv, KernelAPI *api)
{
    IME_UserEntry rows[4];
    int count = api->ime_user_list("ねこ", rows, 4);

    (void)argc;
    (void)argv;
    for (int i = 0; i < count; ++i) {
        if (!strcmp(rows[i].yomi, "ねこ") && !strcmp(rows[i].kanji, "猫") && rows[i].freq == 1)
            return 73;
    }
    return 1;
}
