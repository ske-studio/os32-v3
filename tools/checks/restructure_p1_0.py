CHECK = dict(name='restructure-p1-0', tier=1, needs=[], budget=20,
             covers=['tools/restructure/p1_verify.py', 'tools/restructure/test_p1_verify.py',
                     'tools/restructure/hal_inventory.py', 'sys/hal/input/input_service.c',
                     'tools/move_files.py', 'tools/restructure/layout.py', 'tools/restructure/tool_callers.py',
                     'docs/tasks/restructure/data/layout_splits.tsv', 'docs/tasks/restructure/data/layout.tsv', 'tools/ci/replay/move_map.json',
                     'docs/tasks/restructure/data/hal_map.tsv', 'docs/tasks/restructure/data/layout_review.tsv',
                     'sys/dev/char/kdebug/probe.c', 'build/programs.mk', 'build/sdk.mk', 'userland/diag/ata_deny.c', 'userland/diag/learn_probe.c',
                     'build/os32.ld', 'sys/kapi/kapi_generated.c', 'sdk/link/*.ld',
                     'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs',
                     'tools/g16/scan_ui_chars.py', 'tools/gen_kcg_font.py', 'tools/gen_unicode.py',
                     'tools/np21w_restart.py', 'tools/pack_name_tables.txt', 'tools/pack_notes.txt',
                     'tools/split_c_functions.py', 'tools/gfinit_romfile_host.c',
                     'hwdev/gf/display/*.c', 'sys/dev/video/geforce/diag/gfinit_decode.c', 'sys/dev/video/geforce/diag/gfinit_mode.c',
                     'sys/dev/video/geforce/diag/gfinit_pll.c', 'sys/platform/pc98/kdebug/recover.c', 'sys/dev/video/geforce/diag/recover_gf.c',
                     'docs/tasks/restructure/data/scope.tsv'],
             basis=['docs/tasks/restructure/P1.md P1-0 担当の表', 'docs/design/build/hal_layout.md ファイル単位の判断'])


def run(ctx):
    import subprocess
    status = subprocess.call(['python3', '-B', 'tools/restructure/p1_verify.py'], cwd=ctx.root)
    if status:
        return status
    args = ['python3', '-B', 'tools/restructure/test_p1_verify.py']
    return subprocess.call(args + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
