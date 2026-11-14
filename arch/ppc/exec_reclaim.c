#include "exec_reclaim.h"
#include "platform_host.h"

void exec_pcm_reclaim(int owner) { (void)owner; }
void exec_host_owner_exit(int owner) { platform_host_owner_exit(owner); }
