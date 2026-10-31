"""E01a/E01b: run the real exec_launch through image I/O, argv and entry storage.

Compile the complete exec.c translation unit, including its image and argv helpers.
Only external allocation, VFS and CPU boundaries are supplied by the fixture.
"""
from exec_source import exec_source
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]

MUTATIONS = (
    ('open-error-code', 'image', 'if (fd < 0) return EXEC_ERR_NOT_FOUND;',
     'if (fd < 0) return EXEC_ERR_INVALID;'),
    ('seek-close-missing', 'image', '        vfs_close(fd);', ''),
    ('read-close-missing', 'image', '    }\n    vfs_close(fd);', '    }'),
    ('last-chunk-missing', 'image', 'while (pos < hdr->text_size)',
     'while (pos + 17 < hdr->text_size)'),
    ('read-error-ignored', 'image', 'if (vfs_read_fd(fd, P2V(pa), n) != (int)n) break;',
     '(void)vfs_read_fd(fd, P2V(pa), n);'),
    ('partial-image-accepted', 'image', 'if (pos != hdr->text_size) return EXEC_ERR_INVALID;',
     '(void)pos;'),
    ('shell-bss-missing', 'image', 'if (!ctx->cpl3) kmemset(', 'if (0) kmemset('),
    ('caller-error-ignored', 'source',
     'if (image_rc) return exec_launch_abort(launcher_id, id, image_rc);',
     'if (0 && image_rc) return exec_launch_abort(launcher_id, id, image_rc);'),
    ('argv-null-missing', 'argv', 'launch_argv[argc] = 0;', 'launch_argv[argc] = 1;'),
    ('token-null-missing', 'argv', '{ u8 ch = 0; copy_failed |= app_store',
     '{ u8 ch = 1; copy_failed |= app_store'),
    ('argv-limit-shortened', 'argv', 'argc < OS32_MAX_ARGS - 1', 'argc < OS32_MAX_ARGS - 2'),
    ('limit-stops-copy', 'argv', '        /* 新しい引数を開始 */',
     '        if (argc == OS32_MAX_ARGS - 1) break;\n        /* 新しい引数を開始 */'),
    ('double-quote-escape', 'argv', "quote == '\"' &&", "quote == '\\'' &&"),
    ('unquoted-escape', 'argv', "} else if (launch_read_byte(launcher_pd, s, &copy_failed) == '\\\\' &&",
     "} else if (0 && launch_read_byte(launcher_pd, s, &copy_failed) == '\\\\' &&"),
    ('copy-error-ignored', 'argv', 'if (copy_failed || app_store',
     'if ((0 && copy_failed) || app_store'),
    ('argv-null-not-stored', 'argv', '(argc + 1) * sizeof(u32)', 'argc * sizeof(u32)'),
    ('argv-store-error-ignored', 'argv',
     'app_store(ctx, *argv_addr, launch_argv, (argc + 1) * sizeof(u32)))',
     '(app_store(ctx, *argv_addr, launch_argv, (argc + 1) * sizeof(u32)), 0))'),
    ('argv-address-shifted', 'argv', '*argv_addr = stack_top;', '*argv_addr = stack_top + 4;'),
    ('argc-result-shifted', 'argv', 'return argc;', 'return argc + 1;'),
    ('argv-error-code', 'argv', 'return EXEC_ERR_INVALID;', 'return EXEC_ERR_NOMEM;'),
    ('argv-caller-error-ignored', 'source',
     'if (argc < 0) return exec_launch_abort(launcher_id, id, argc);',
     'if (0 && argc < 0) return exec_launch_abort(launcher_id, id, argc);'),
    ('argv-stack-handoff', 'source', '        stack_top = argv_addr;', '        stack_top += 0;'),
    ('argv-launcher-pd', 'source', 'exec_build_argv(ctx, launcher_pd,',
     'exec_build_argv(ctx, paging_kernel_pd_phys(),'),
)


def run(source=None, image=None, argv=None):
    if source is None:
        source = exec_source(ROOT)
    if image is None:
        image = (ROOT / 'exec/exec_image.inc').read_text()
    if argv is None:
        argv = (ROOT / 'exec/exec_argv.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='exec-image-') as directory:
        tmp = Path(directory)
        # Keep the entire TU and its local helper together, including mutants.
        (tmp / 'exec.c').write_text(source)
        (tmp / 'exec_image.inc').write_text(image)
        (tmp / 'exec_argv.inc').write_text(argv)
        exe = tmp / 'test'
        result = subprocess.run([
            'gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Werror',
            # host_arch's stack-switch macro only assigns the saved ESP slot.
            '-Wno-unused-function', '-Wno-unused-but-set-variable',
            '-D__KERNEL_BUILD__', '-ffunction-sections', '-fdata-sections',
            '-Wl,--gc-sections', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            '-I' + str(tmp), '-I' + str(ROOT / 'tools/tests/host_arch'),
            '-I' + str(ROOT / 'exec'), *include_flags(ROOT),
            str(ROOT / 'tools/tests/exec_image_host.c'), '-o', str(exe),
        ], capture_output=True, text=True)
        if result.returncode:
            return 'compile', result.stderr
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=30)
        status = 'pass' if result.returncode == 0 else 'runtime'
        if result.returncode and (result.returncode != 1 or 'FAIL:' not in result.stdout):
            status = 'unexpected'
        return status, result.stdout + result.stderr


if __name__ == '__main__':
    status, log = run()
    print(log, end='')
    if status != 'pass':
        raise SystemExit(status)
    if '--mutate' in sys.argv:
        originals = {'source': exec_source(ROOT),
                     'image': (ROOT / 'exec/exec_image.inc').read_text(),
                     'argv': (ROOT / 'exec/exec_argv.inc').read_text()}
        for name, target, old, new in MUTATIONS:
            assert originals[target].count(old) == 1, (name, originals[target].count(old))
            changed = dict(originals)
            changed[target] = changed[target].replace(old, new)
            status, log = run(**changed)
            print(name + ': ' + status)
            if status != 'runtime':
                raise SystemExit(log or 'mutation must fail at runtime')
