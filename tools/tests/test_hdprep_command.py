"""Run the registered hdprep command against a PC-98 disk image in memory."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='hdprep-command-host', tier=1, needs=[], budget=2,
             covers=['platform/pc98/install_geometry.h', 'sdk/include/os32/*.h', 'tools/tests/hdprep_command_host.c', 'userland/shell/cmd_hdprep.c',
                     'platform/pc98/install_hdprep.c',
                     'platform/pc98/install_plan.c', 'platform/pc98/platform_install_layout.h',
                     'userland/shell/hdprep_plan.c', 'fs/pc98pt.c', 'fs/pc98pt.h',
                     'tools/tests/test_hdprep_command.py', 'include/platform_install_boot.h',
                     'userland/shell/shell.h', 'userland/system/inst_hdd.h',
                     'userland/system/inst_disk.h', 'userland/shell/hdprep_plan.h'],
             basis=['docs/design/userland/shell/hdprep_plan.md'])
MUTATIONS = [
    ('userland/shell/cmd_hdprep.c', 'if (!hp_ask_yes())', 'if (hp_ask_yes())'),
    ('userland/shell/cmd_hdprep.c', 'if (platform_hdprep_probe(g_api, &p) != 0)',
     'if (platform_hdprep_probe(g_api, &p) == 0)'),
    ('platform/pc98/install_hdprep.c', 'if (rc != 0 || !disk_memeq(hp_sect, hp_back, HDPREP_SECT)) {\n        api->kprintf',
     'if (rc != 0) {\n        api->kprintf'),
]


def run(ctx):
    with tempfile.TemporaryDirectory() as folder:
        tmp = Path(folder)
        cases = [None] + (MUTATIONS if ctx.mutate else [])
        for mutation in cases:
            sources = [p for p in CHECK['covers'] if p.endswith('.c')]
            if mutation:
                path, old, new = mutation
                code = (ctx.root / path).read_text()
                assert code.count(old) == 1
                mutant = tmp / Path(path).name
                mutant.write_text(code.replace(old, new))
                sources[sources.index(path)] = str(mutant)
            exe = tmp / 'hdprep'
            subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-D__cdecl=', '-D__OS32_USERLAND__', '-I.', '-Iinclude',
                            '-Iplatform/pc98', '-Iuserland/shell', '-Isdk/include/os32', *sources,
                            '-o', exe], cwd=ctx.root, check=True)
            rc = subprocess.run([exe], cwd=tmp).returncode
            if (rc == 0) != (mutation is None):
                return 1
            print('runtime RED ' + mutation[1] if mutation else 'hdprep command PASS')
    return 0
