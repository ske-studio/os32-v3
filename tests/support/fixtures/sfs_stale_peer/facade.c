#include "sys/dev/net/e82557/lan_module.c"
extern int lan_module_init(LanModuleApi *);
void model_load(void) {
    if (lan_module_init(&candidate)) __builtin_trap();
    candidate_valid = 1;
    tick_target = candidate.tick;
}
struct module_record *module_record_get(struct module_handle handle) {
    static struct module_record running = {.state = MODULE_RUNNING};
    return &running;
}
