"""Stage the product D1 providers for full-dispatch host fixtures."""
import subprocess
import sys
from functools import lru_cache


@lru_cache(maxsize=None)
def ensure_call_metadata(root):
    subprocess.run([sys.executable, "-B", "sdk/gen_kapi.py"], cwd=root,
                   check=True, stdout=subprocess.DEVNULL)


def syscall_providers(root, read=None):
    """Product x86 call/entry bodies; only ASM invocation is supplied by host."""
    ensure_call_metadata(root)
    if read is None:
        read = lambda path: (root / path).read_text()
    dispatch = read('exec/exec_syscall.inc')
    special = dispatch[dispatch.index('/* Keep the dispatcher'):dispatch.index('void exec_syscall_dispatch(')]
    return ('#if __SIZEOF_LONG__ != 4\n'
            '/* LP64 host substitutes for x86 providers; no product coverage. */\n' +
            ''.join('#define arch_kapi_' + n + ' host_arch_kapi_' + n + '\n'
                    for n in ('source', 'read_block', 'invoke', 'legacy_ls', 'legacy_print')) +
            '#define ring3_syscall_dispatch host_ring3_syscall_dispatch\n'
            '#endif\n#include "kapi_call.h"\n' +
            read('arch/x86/kernel/kapi_call.c') + '\n' +
            read('arch/x86/kernel/syscall_entry.c') + '\n' + special)
