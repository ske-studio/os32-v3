volatile int kbd_gui_mode;
int legacy_stop_filter(int sc, int br, int mods) { (void)sc; (void)br; (void)mods; __builtin_trap(); }
void legacy_cooked_push(u16 entry) { (void)entry; __builtin_trap(); }
void legacy_raw_push(int sc, int br, int mods) { (void)sc; (void)br; (void)mods; __builtin_trap(); }
