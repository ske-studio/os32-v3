"""S5 actual accessor, shared window and production plan; fake hardware only."""
import pathlib
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']


def command(exe):
    return ['gcc', *FLAGS, '-O2', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'tools/tests/recover_hostshim'), '-I'+str(ROOT/'include'), '-I'+str(ROOT/'sdk/include/os32'), str(ROOT/'tools/tests/gfinit_bus_host.c'), '-o', str(exe)]


MUTATIONS = [
    ('cr1f_restore', 'value = !k || v->value == RECOVER_CR1F_UNLOCKED ?', 'value = true ?'),
    ('cleanup_order', '{GF_CR_LOCK, GF_CR_PROTECT, RECOVER_CR44, GF_CR_LOCK}', '{GF_CR_LOCK, GF_CR_PROTECT, GF_CR_LOCK, RECOVER_CR44}'),
    ('cfg_present', 'if (!present(b)) return -1;', 'if (false) return -1;'),
    ('legacy_reject', 'if (pa < GF_LEGACY_HI && pa + a->width > GF_LEGACY_LO)', 'if (false)'),
    ('bar_type', '(bars[i] & GF_BAR_TYPE)', 'false'),
    ('bus_mem', '!(command & RECOVER_COMMAND_MEM)', 'false'),
    ('bus_bar', 'bars[i] != b->bars[i]', 'false'),
    ('bus_tuple', 'a->offset != resolved.offset', 'false'),
    ('bus_command_width', 'if (a->width == 2)', 'if (a->width == 1)'),
]
SHARED_MUTATIONS = [
    ('cr1f_verify', 'value == RECOVER_CR1F_UNLOCK ?\n        RECOVER_CR1F_UNLOCKED : 0', 'value == RECOVER_CR1F_UNLOCK ?\n        RECOVER_CR1F_UNLOCK : 0'),
    ('cr1f_saved', 'RECOVER_CR1F_UNLOCK : RECOVER_CR1F_LOCK) : value;', 'RECOVER_CR1F_UNLOCK : 0) : value;'),
    ('local_select', 'v->key == target && *value == t->index && t->access == REC_RW) found = true;',
     'v->key == target && *value == t->index && t->access == REC_RW) found = false;'),
]
RESOURCE_MUTATIONS = [
    ('resource_unknown', 'if (raw && !(raw & PCI_BAR_SPACE_IO)) return -1;', 'if (raw && !(raw & PCI_BAR_SPACE_IO) && false) return -1;'),
    ('resource_function', 'fn < GF_RES_FUNCTIONS', 'fn < 1U'),
    ('resource_high', 'if (type && (pci_cfg_read32', 'if (false && (pci_cfg_read32'),
    ('resource_window_len', 'last + GF_RES_WINDOW_GRANULE - base', 'last - base'),
    ('resource_count', '*used == RECOVER_RANGE_MAX', 'false'),
]
WINDOW_MUTATIONS = [
    ('window_busy', 'if (window_busy)', 'if (false)'),
    ('window_poison', 'window_failed = true;', 'window_failed = false;'),
]


def mutation(item):
    file, name, old, new = item
    source = (ROOT/file).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gfinit-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {file: source.replace(old, new, 1) if old else source},
                                       [command(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return f'{name}: compile failed {exc.stderr!r}', 1
        result = host32.run([str(exe)], cwd=tree, timeout=60, capture_output=True)
        return f'{name}: {"GREEN" if result.returncode == 0 else "RED"} rc={result.returncode}', int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='gfinit-') as td:
        exe = pathlib.Path(td)/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,
            [('kdebug/gfinit_bus.c', *item) for item in MUTATIONS] +
            [('kdebug/recover.c', *item) for item in SHARED_MUTATIONS] +
            [('kdebug/kdebug_window.c', *item) for item in WINDOW_MUTATIONS] +
            [('kdebug/gfinit_resources.c', *item) for item in RESOURCE_MUTATIONS],
            ('kdebug/gfinit_bus.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
