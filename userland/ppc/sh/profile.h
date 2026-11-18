#ifndef PPC_SH_PROFILE_H
#define PPC_SH_PROFILE_H

#define SHELL_AS_APP 1
#define SH_LAUNCH_SYNC 1
#define SH_REGISTER_COMMANDS(X) \
    X(base) X(file) X(dir) X(env) X(script) X(sys)
#define SH_BASE_MINIMAL 1
#define SH_SYS_MINIMAL 1 /* sys の組は reboot だけ */
#define SH_DEFAULT_PATH "/bin"
#define SH_DEFAULT_HOME "/tmp"
#define SH_DEFAULT_SHELL "/bin/sh.bin"

#endif
