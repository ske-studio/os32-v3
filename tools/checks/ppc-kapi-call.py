"""Register tools/ppc/test_kapi_call.py with its design expectations."""
CHECK = dict(env=['ppc-cross'],
    name='ppc-kapi-call',
    tier=1,
    budget=3,
    needs=[],
    covers=['tools/ppc/test_kapi_call.py',
     'sys/arch/ppc/kapi_call.c',
     'sys/kapi/kapi_print.c',
     'sys/include/kapi_print.h',
     'sys/include/redir_access.h',
     'sys/include/kprintf.h',
     'sys/kern/mm/kmalloc.h',
     'sys/arch/ppc/ppc_frame.h',
     'sys/include/kapi_call.h',
     'sys/include/exec_arch.h',
     'sdk/gen_kapi.py',
     'sdk/kapi.json',
     'tools/checks/ppc_registered.py',
     'tools/tests/host32.py'],
    basis=['PPC_SHELL_EXEC_DESIGN§4'],
    env_limit='ホストの模型・コンパイル検査であり実機の MMIO・DMA・割込みのタイミング障害は保証しない。',
    full_only=False,
)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_kapi_call.py', ['gcc', 'qemu-i386'])
