/* Run the resident shell up to its command dispatcher. */
#include "userland/shell/shell.h"

extern int legacy_cooked_take(void);
static void *landing[5];
static int accepted, reads;
const char *cmd_names[] = {0};

static int get_key(void)
{
    int key = legacy_cooked_take();
    if (key < 0 || ++reads > 2) __builtin_longjmp(landing, 1);
    return key;
}

int execute_command(const char *cmd)
{
    accepted = reads == 2 && cmd[0] == 'a' && cmd[1] == 0;
    __builtin_longjmp(landing, 1);
}

static void print(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
static void put_char(char ch, u8 attr) { (void)ch; (void)attr; }
static int open_file(const char *path, int mode) { (void)path; (void)mode; return -1; }
static const char *cwd(void) { return "/"; }
static int serial_status(u32 *mode, u32 *baud, u32 *fifo)
{ (void)mode; (void)baud; (void)fifo; return OS32_ERR_NOSYS; }
static KernelAPI api = {.ime_getkey = get_key, .shell_putchar = put_char, .kprintf = print,
                        .sys_getcwd = cwd, .sys_open = open_file, .serial_get_status = serial_status};
KernelAPI *g_api = &api;
const char *env_get(const char *name) { (void)name; return "/"; }
const char *shell_get_path(void) { __builtin_trap(); }
void script_source_profile(const char *path) { (void)path; __builtin_trap(); }
void sh_refuse(const char *what, int limit) { (void)what; (void)limit; __builtin_trap(); }
int sh_refused_take(void) { __builtin_trap(); }
void sh_status_set(int status) { (void)status; __builtin_trap(); }
int os32_ls(const char *path, DirCallback cb, void *ctx)
{ (void)path; (void)cb; (void)ctx; __builtin_trap(); }

int legacy_shell_dispatch(void)
{
    if (!__builtin_setjmp(landing)) shell_run(0);
    return accepted && legacy_cooked_take() == -1;
}
