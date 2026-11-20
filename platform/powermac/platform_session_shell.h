#ifndef POWERMAC_PLATFORM_SESSION_SHELL_H
#define POWERMAC_PLATFORM_SESSION_SHELL_H

int pm_session_shell_caller(void);
const char *pm_shell_select(const char *def, int *switched);
#define SESSION_SHELL_CALLER() pm_session_shell_caller()

#endif
