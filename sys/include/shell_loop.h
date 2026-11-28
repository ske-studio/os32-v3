#ifndef SHELL_LOOP_H
#define SHELL_LOOP_H

struct shell_loop_ops {
    int (*gui_default)(void);
    int (*run)(const char *cmdline, int line_edit);
    int (*prepare)(int gui); /* 0: ready, 1: missing KCG, 2: missing GUI dependency */
    void (*loading)(const char *path, int gui);
    void (*result)(const char *path, int rc, int gui, int streak);
    void (*failure)(int gui, int rc, int streak);
    void (*delay_clear)(void);
};

void shell_loop_run(const struct shell_loop_ops *ops);

#endif
