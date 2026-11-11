"""Provider groups absent on PowerMac, keyed by KAPI slot name."""

IME_SLOTS = frozenset({
    'ime_getchar', 'ime_trygetchar', 'ime_toggle', 'ime_is_active',
    'ime_set_mode', 'ime_get_mode', 'ime_getkey', 'ime_switch_dict',
    'ime_user_list', 'ime_user_delete', 'ime_user_export', 'ime_user_clear',
    'ime_trygetkey', 'ime_feed_key', 'ime_set_render', 'ime_dict_info',
    'ime_dict_suspend',
})

ABSENT_PROVIDER_SLOTS = IME_SLOTS
