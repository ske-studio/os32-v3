"""PowerMac two-LOAD verification record and pair."""
CHECK = dict(name='ppc-root-pair-host', tier=2, budget=30, needs=[], env=['ppc-cross'],
             covers=['tools/checks/ppc_registered.py', 'tools/ppc/test_root_pair.py', 'tools/ppc/root_pair.py',
                     'build/ppc-root-pair.mk', 'tools/tests/ppc_root_image_host.c',
                     'platform/powermac/root_image.c', 'platform/powermac/root_boot.h',
                     'mm/pgalloc.h', 'lib/crc32.[ch]', 'lib/lz4*', 'tools/ppc/pair_fixture/*', 'tools/ppc/root_lz4_layout.py',
                     'tools/mkmod.py', 'tools/module_schema.py', 'sdk/target_profile.py',
                     'platform/powermac/module_pair.c', 'include/*.h', 'lib/crc32_core.inc'],
             basis=['docs/decisions/ppc/PPC_RAM_ROOT_W6_PLAN.md',
                    'docs/design/platform/powermac/root_pair.md'],
             env_limit='Uses powerpc-elf-gcc and qemu-ppc user mode.')


def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_root_pair.py', ['qemu-ppc', 'xorriso'])
