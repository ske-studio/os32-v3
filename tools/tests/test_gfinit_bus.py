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
    return ['gcc', *FLAGS, '-O2', '-ffunction-sections', '-Wl,--gc-sections', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'tools/tests/recover_hostshim'), '-I'+str(ROOT/'include'), '-I'+str(ROOT/'sdk/include/os32'), str(ROOT/'tools/tests/gfinit_bus_host.c'), '-o', str(exe)]


MUTATIONS = [
    ('cr1f_second_visit', 'k < sizeof(order) / sizeof(order[0])', 'k + 1 < sizeof(order) / sizeof(order[0])'),
    ('cr1f_restore', 'value = !k || v->value == RECOVER_CR1F_UNLOCKED ?', 'value = true ?'),
    ('cleanup_order', '{RECOVER_CR1F, RECOVER_CR11, RECOVER_CR44, RECOVER_CR1F}', '{RECOVER_CR1F, RECOVER_CR11, RECOVER_CR1F, RECOVER_CR44}'),
    ('cfg_present', 'if (!present(b)) return -1;', 'if (false) return -1;'),
    ('legacy_reject', 'if (pa < GF_LEGACY_HI && pa + a->width > GF_LEGACY_LO)', 'if (false)'),
    ('bar_type', '(bars[i] & PCI_BAR_TYPE_MASK)', 'false'),
    ('bus_mem', '!(command & RECOVER_COMMAND_MEM)', 'false'),
    ('bus_bar', 'bars[i] != b->bars[i]', 'false'),
    ('bus_tuple', 'a->offset != resolved.offset', 'false'),
    ('bus_command_width', 'if (a->width == 2)', 'if (a->width == 1)'),
]
SHARED_MUTATIONS = [
    ('kdebug/recover.c', 'resource_overlap_left', '(unsigned long long)base < (unsigned long long)exclude[i].base + exclude[i].len', 'false'),
    ('kdebug/recover.c', 'resource_overlap_right', '(unsigned long long)exclude[i].base < (unsigned long long)base + length', 'false'),
    ('kdebug/recover_plan/access.inc', 'cr1f_verify', 'value == RECOVER_CR1F_UNLOCK ?\n        RECOVER_CR1F_UNLOCKED : 0', 'value == RECOVER_CR1F_UNLOCK ?\n        RECOVER_CR1F_UNLOCK : 0'),
    ('kdebug/recover_plan/access.inc', 'cr1f_saved', 'RECOVER_CR1F_UNLOCK : RECOVER_CR1F_LOCK) : value;', 'RECOVER_CR1F_UNLOCK : 0) : value;'),
    ('kdebug/recover_plan/access.inc', 'local_select', 'v->key == target && *value == t->index && t->access == REC_RW) found = true;',
     'v->key == target && *value == t->index && t->access == REC_RW) found = false;'),
]
RESOURCE_MUTATIONS = [
    ('kdebug/gfinit_resources/bounds.inc', 'resource_multifunction', 'return pci_is_multifunction(hdr0) ? PCI_FN_COUNT : 1;',
     '(void)hdr0; return PCI_FN_COUNT;'),
    ('kdebug/gfinit_resources/bounds.inc', 'resource_log', 'reason, dev, fn, reg, raw);', '"unknown", dev, fn, reg, raw); (void)reason;'),
    ('kdebug/gfinit_resources/scan.inc', 'resource_unknown', 'if (raw && !(raw & PCI_BAR_SPACE_IO))', 'if (raw && !(raw & PCI_BAR_SPACE_IO) && false)'),
    ('kdebug/gfinit_resources/scan.inc', 'resource_function', 'if ((id & 0xffffU) == PCI_VENDOR_NONE) continue;', 'if (fn || (id & 0xffffU) == PCI_VENDOR_NONE) continue;'),
    ('kdebug/gfinit_resources/windows.inc', 'resource_high', 'if (type && (pci_cfg_read32', 'if (false && (pci_cfg_read32'),
    ('kdebug/gfinit_resources/windows.inc', 'resource_window_len', 'last + PCI_BRIDGE_WINDOW_GRANULE - base', 'last - base'),
    # Reject the last valid slot instead of writing beyond a full array.
    ('kdebug/gfinit_resources/windows.inc', 'resource_count', '*used == RECOVER_RANGE_MAX', '*used >= RECOVER_RANGE_MAX - 1U'),
    ('kdebug/gfinit_resources/bounds.inc', 'resource_cap_align_only', 'cap = b->base[i] - base;', 'cap = cap;'),
    ('kdebug/gfinit_resources/bounds.inc', 'resource_next_first', 'if (b->base[i] > base && b->base[i] - base < cap)\n            cap = b->base[i] - base;',
     'if (b->base[i] > base) { cap = b->base[i] - base; break; }'),
    ('kdebug/gfinit_resources.c', 'resource_csr_length', '#define GF_RES_FXP_CSR_LEN 0x1000U', '#define GF_RES_FXP_CSR_LEN 0x2000U'),
    ('kdebug/gfinit_resources.c', 'resource_flash_bar', '{((PCI_DEVICE_82557 << 16) | PCI_VENDOR_INTEL), GF_RES_BAR2, GF_RES_FXP_FLASH_LEN', '{((PCI_DEVICE_82557 << 16) | PCI_VENDOR_INTEL), 3U, GF_RES_FXP_FLASH_LEN'),
    ('kdebug/gfinit_resources/bars.inc', 'resource_upper_append', 'u32 cap = resource_cap(b, raw & PCI_BAR_MEM_ADDR_MASK);',
     'if (!spec) return 0;\n    u32 cap = resource_cap(b, raw & PCI_BAR_MEM_ADDR_MASK);'),
    ('kdebug/gfinit_resources/bars.inc', 'resource_spec_bound', 'if (exact && len > cap)', 'if (false && len > cap)'),
    ('kdebug/gfinit_resources/bars.inc', 'resource_alignment', '(base & (len - 1U))', 'false'),
    ('kdebug/gfinit_resources/bounds.inc', 'resource_write', 'b->count = 0;', 'b->count = 0; pci_cfg_write32(0, 8, 0, PCI_CFG_BAR0, 0);'),
    ('kdebug/gfinit_resources/bounds.inc', 'resource_alias', 'if (b->base[i] == base)', 'if (false)'),
    ('kdebug/gfinit_resources/bounds.inc', 'resource_bounds_count', 'if (b->count >= RECOVER_RANGE_MAX)', 'if (false)'),
    ('kdebug/gfinit_resources/bars.inc', 'resource_exclude_count', 'if (*used >= RECOVER_RANGE_MAX)', 'if (false)'),
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
            list(SHARED_MUTATIONS) +
            [('kdebug/kdebug_window.c', *item) for item in WINDOW_MUTATIONS] +
            RESOURCE_MUTATIONS,
            ('kdebug/gfinit_bus.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
