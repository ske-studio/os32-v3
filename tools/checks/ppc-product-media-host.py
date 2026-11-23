CHECK = dict(name='ppc-product-media-host', tier=1, needs=[], budget=10,
    covers=['tools/ppc/product_media_host.py', 'tools/ppc/product_media.py',
            'tools/ppc/root_lz4_layout.py', 'tools/ppc/root_pair.py', 'tools/mkmod.py',
            'tools/module_schema.py', 'include/memmap.h', 'include/callconv.h', 'include/types.h'],
    basis=['docs/decisions/ppc/PPC_ROOT_MOUNT_ALIGN_X86_DESIGN.md §6'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/product_media_host.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
