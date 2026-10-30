# hwdev の契約

kdebug・hwdev が読むヘッダには enum を足さず `#define` で書く。`tools/gen_hwdev_contract.py` が列挙子を契約に数えるため。
