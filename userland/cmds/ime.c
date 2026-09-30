/* ======================================================================== */
/*  IME.C — FEP辞書・状態管理用シェルコマンド                                 */
/*                                                                          */
/*  FEPのON/OFF、入力モード切り替え、辞書切り替え、ユーザー学習辞書の管理を行う。  */
/* ======================================================================== */

#include "os32api.h"
#include <stdio.h>
#include <string.h>

static void print_usage(void)
{
    printf("Usage:\n");
    printf("  ime                      Show current FEP status\n");
    printf("  ime on | off             Enable/Disable FEP\n");
    printf("  ime mode hira|kata       Switch input mode\n");
    printf("  ime dict s|m|l           Switch dictionary variant\n");
    printf("  ime user list [yomi]     List user dictionary entries\n");
    printf("  ime user delete <yomi> [kanji]  Delete user dictionary entry\n");
    printf("  ime user export [path]   Export user dictionary to CSV\n");
    printf("  ime user clear           Clear all user dictionary entries\n");
}

int main(int argc, char **argv, KernelAPI *api)
{
    int active;
    int mode;
    const char *mode_str;
    int variant;
    int rc;
    const char *prefix;
    IME_UserEntry entries[64];
    int count;
    int i;
    const char *yomi;
    const char *kanji;
    const char *path;
    char confirm[16];

    if (argc < 2) {
        /* FEPの現在の状態を表示 */
        active = api->ime_is_active();
        if (active) {
            mode = api->ime_get_mode();
            mode_str = "Unknown";
            if (mode == IME_MODE_HIRAGANA) {
                mode_str = "Hiragana";
            } else if (mode == IME_MODE_KATAKANA) {
                mode_str = "Katakana";
            }
            printf("FEP: ON (Mode: %s)\n", mode_str);
        } else {
            printf("FEP: OFF\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        api->ime_set_mode(IME_MODE_HIRAGANA);
        printf("FEP enabled.\n");
        return 0;
    }

    if (strcmp(argv[1], "off") == 0) {
        api->ime_set_mode(IME_MODE_OFF);
        printf("FEP disabled.\n");
        return 0;
    }

    if (strcmp(argv[1], "mode") == 0) {
        if (argc < 3) {
            printf("Error: Missing mode argument (hira|kata).\n");
            return 1;
        }
        if (strcmp(argv[2], "hira") == 0) {
            api->ime_set_mode(IME_MODE_HIRAGANA);
            printf("Mode switched to Hiragana.\n");
        } else if (strcmp(argv[2], "kata") == 0) {
            api->ime_set_mode(IME_MODE_KATAKANA);
            printf("Mode switched to Katakana.\n");
        } else {
            printf("Error: Invalid mode '%s'. Use 'hira' or 'kata'.\n", argv[2]);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "dict") == 0) {
        if (argc < 3) {
            printf("Error: Missing dictionary size (s|m|l).\n");
            return 1;
        }
        if (strcmp(argv[2], "s") == 0) {
            variant = 0;
        } else if (strcmp(argv[2], "m") == 0) {
            variant = 1;
        } else if (strcmp(argv[2], "l") == 0) {
            variant = 2;
        } else {
            printf("Error: Invalid dictionary variant '%s'. Use 's', 'm', or 'l'.\n", argv[2]);
            return 1;
        }
        rc = api->ime_switch_dict(variant);
        if (rc == 0) {
            printf("Dictionary switched successfully.\n");
        } else {
            printf("Error: Failed to switch dictionary (rc=%d).\n", rc);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "user") == 0) {
        if (argc < 3) {
            printf("Error: Missing user dictionary command.\n");
            return 1;
        }

        if (strcmp(argv[2], "list") == 0) {
            prefix = "";
            if (argc >= 4) {
                prefix = argv[3];
            }

            count = api->ime_user_list(prefix, entries, 64);
            if (count < 0) {
                printf("Error: Failed to list user entries (rc=%d).\n", count);
                return 1;
            }

            for (i = 0; i < count; i++) {
                printf("%s,%s,%d\n", entries[i].yomi, entries[i].kanji, entries[i].freq);
            }
            return 0;
        }

        if (strcmp(argv[2], "delete") == 0) {
            kanji = "";
            if (argc < 4) {
                printf("Error: Missing yomi to delete.\n");
                return 1;
            }
            yomi = argv[3];
            if (argc >= 5) {
                kanji = argv[4];
            }

            rc = api->ime_user_delete(yomi, kanji);
            if (rc == 0) {
                printf("Entry deleted successfully.\n");
            } else {
                printf("Error: Failed to delete entry (rc=%d).\n", rc);
                return 1;
            }
            return 0;
        }

        if (strcmp(argv[2], "export") == 0) {
            path = "/tmp/userdict.csv";
            if (argc >= 4) {
                path = argv[3];
            }

            rc = api->ime_user_export(path);
            if (rc == 0) {
                printf("User dictionary exported to %s.\n", path);
            } else {
                printf("Error: Failed to export user dictionary (rc=%d).\n", rc);
                return 1;
            }
            return 0;
        }

        if (strcmp(argv[2], "clear") == 0) {
            printf("Are you sure to clear user dict? (y/n): ");
            fflush(stdout);

            if (fgets(confirm, sizeof(confirm), stdin)) {
                if (confirm[0] == 'y' || confirm[0] == 'Y') {
                    rc = api->ime_user_clear();
                    if (rc == 0) {
                        printf("User dictionary cleared.\n");
                    } else {
                        printf("Error: Failed to clear user dictionary (rc=%d).\n", rc);
                        return 1;
                    }
                } else {
                    printf("Cancelled.\n");
                }
            }
            return 0;
        }

        printf("Error: Unknown user command '%s'.\n", argv[2]);
        return 1;
    }

    print_usage();
    return 1;
}
