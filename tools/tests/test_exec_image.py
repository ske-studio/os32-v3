"""E01a: run the real exec_launch through image I/O, stopping at argv storage.

Admission/allocation/CPU edges are stubs. The launch body is not rewritten;
after extraction its real included helper runs in the same fixture as well.
"""
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
)


def function(source, name):
    start = source.index('static int ' + name + '(')
    end = source.index('\n}', start) + 2
    return source[start:end]


def run(source=None, image=None):
    if source is None:
        source = (ROOT / 'exec/exec.c').read_text()
    helper = ROOT / 'exec/exec_image.inc'
    if image is None:
        image = helper.read_text() if helper.exists() else ''
    code = image + '\n' + function(source, 'exec_launch')
    with tempfile.TemporaryDirectory(prefix='exec-image-') as directory:
        tmp = Path(directory)
        (tmp / 'exec_image_source.inc').write_text(code)
        exe = tmp / 'test'
        result = subprocess.run([
            'gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Werror',
            '-Wno-unused-function', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            '-I' + str(tmp), *include_flags(ROOT),
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
        originals = {'source': (ROOT / 'exec/exec.c').read_text(),
                     'image': (ROOT / 'exec/exec_image.inc').read_text()}
        for name, target, old, new in MUTATIONS:
            assert originals[target].count(old) == 1, (name, originals[target].count(old))
            changed = dict(originals)
            changed[target] = changed[target].replace(old, new)
            status, log = run(**changed)
            print(name + ': ' + status)
            if status != 'runtime':
                raise SystemExit(log or 'mutation must fail at runtime')
