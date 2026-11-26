#ifndef OS32_EXEC_CALLER_CONTRACT_H
#define OS32_EXEC_CALLER_CONTRACT_H

/* いまの呼び出しが CPL=3 のアプリ由来か (= ring3_guard_active(ring3_in_syscall,
 * ring3_wm_depth))。ポインタを**あとで使うために控える** KAPI が、控える時点で
 * 由来を記録するのに使う (redir_access_capture の RedirAccess)。 */
int ring3_call_from_user(void);

#endif
