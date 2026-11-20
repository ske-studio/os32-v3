"""PPC module pack and loader wire arithmetic."""
import subprocess

CHECK = dict(name='ppc-module-rela-host', tier=1, budget=3, needs=[],
             covers=['build/ppc-module.ld', 'tools/tests/test_module_rela_ppc.py', 'tools/tests/host32.py',
                     'tools/tests/cc_cache.py', 'tools/ppc/module_rela.py',
                     'tools/tests/module_fixup_ppc_host.c', 'kernel/module/module_fixup_ppc.inc',
                     'kernel/module/module_image.h', 'kernel/module/module_schema.h',
                     'include/endian_le.h', 'include/types.h', 'include/callconv.h'],
             basis=['docs/design/kernel/module/module_image.md',
                    'docs/tasks/ppc/TASK_PPC_ALIGN_X86.md B08・C04'])


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_module_rela_ppc.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
