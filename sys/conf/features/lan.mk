# feature/lan owns this selection.
C_FEATURES += sys/dev/net/e82557/lan_module.c
INCLUDE_FEATURES += sys/dev/net/e82557
MODULE_FEATURES += lan
MODULE_MK_lan = build/lan-module.mk
