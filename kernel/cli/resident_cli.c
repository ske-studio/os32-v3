#include "resident_cli.h"

#define LINE_SIZE 256

static void output(const struct resident_cli_ops *ops, const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    ops->write(s, n);
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char *spaces(char *s)
{
    while (*s == ' ') s++;
    return s;
}

static void execute(const struct resident_cli_ops *ops, char *line)
{
    char *cmd = spaces(line), *arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = 0;
    arg = spaces(arg);
    if (!*cmd) return;
    if (equal(cmd, "help")) {
        output(ops, "help echo cat\n");
    } else if (equal(cmd, "echo")) {
        char *p = arg, *redirect = 0;
        while (*p) {
            if (*p == '>' || *p == '<' || *p == '|') {
                if (*p != '>' || redirect || p == arg || p[-1] != ' ' || p[1] != ' ') {
                    output(ops, "invalid redirect\n"); return;
                }
                redirect = p;
            }
            p++;
        }
        if (redirect) {
            char *path = spaces(redirect + 1), *end = redirect;
            if (path[0] != '/' || path[1] != 't' || path[2] != 'm' ||
                path[3] != 'p' || path[4] != '/' || !path[5]) {
                output(ops, "invalid redirect\n"); return;
            }
            for (p = path + 5; *p; p++)
                if (*p == '/' || *p == ' ') { output(ops, "invalid redirect\n"); return; }
            while (end > arg && end[-1] == ' ') end--;
            *end++ = '\n';
            if (ops->put(path, arg, (u32)(end - arg)) < 0) output(ops, "write failed\n");
        } else {
            output(ops, arg); output(ops, "\n");
        }
    } else if (equal(cmd, "cat")) {
        if (!*arg || ops->cat(arg) < 0) output(ops, "cat failed\n");
    } else {
        output(ops, "unknown command\n");
    }
}

void resident_cli_run(const struct resident_cli_ops *ops)
{
    char line[LINE_SIZE];
    u32 length = 0;
    int discard = 0;
    output(ops, "os32> ");
    for (;;) {
        int key = ops->getkey();
        if (key < 0) { ops->poll(); continue; }
        if (discard) {
            if (key == '\n' || key == '\r') {
                output(ops, "\nline too long\nos32> ");
                discard = 0;
                length = 0;
            }
            continue;
        }
        if (key == 3) {
            key = RESIDENT_CLI_KEY_CANCEL;
        }
        if (key == RESIDENT_CLI_KEY_CANCEL) {
            output(ops, "\n");
            length = 0;
            output(ops, "os32> ");
        } else if (key == '\n' || key == '\r') {
            output(ops, "\n");
            line[length] = 0;
            execute(ops, line);
            length = 0;
            output(ops, "os32> ");
        } else if (key == 8 || key == 127) {
            if (length) { length--; output(ops, "\b \b"); }
        } else if (key >= 32 && key <= 126) {
            if (length == LINE_SIZE - 1) discard = 1;
            else { line[length++] = (char)key; ops->write(&line[length - 1], 1); }
        }
    }
}
