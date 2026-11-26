"""Build and verify the media used by the external shell observation."""
import re
import struct
import subprocess

from boot_media import Media
from run import ROOT


def make(out, *settings):
    result = subprocess.run(['make', '--no-print-directory', 'ARCH=ppc', 'PLATFORM=powermac',
                             'KB_OUT='+str(out), *settings], cwd=ROOT, text=True,
                            capture_output=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    (out.parent/(out.name+'.build.log')).write_text(result.stdout+result.stderr)
    return result


def fixed_layout(header):
    layout = {name: int(value, 16) for name, value in re.findall(
        r'#define LAYOUT_(\w+) (0x[0-9A-Fa-f]+)', header.read_text())}
    # Fixed band boundaries and budgets from the shell alignment design, section 4.
    expected = dict(KERNEL_LOAD_ADDR=0x100000, MEM_KERNEL_IMAGE_MAX=0x275000,
                    MEM_FIXED_PAGING_GUARD_BASE=0x3e0000, MEM_SHELL_LOAD_ADDR=0x400000,
                    MEM_SHELL_MAX_SIZE=0xa5000, MEM_SHELL_IMAGE_HEADROOM=0x10000,
                    MEM_SHELL_HEAP_BASE=0x4b5000, MEM_SHELL_HEAP_END=0x4f5000,
                    MEM_SHELL_EXEC_HEAP_SIZE=0x40000, MEM_SHELL_HEAP_SIZE=0x40000,
                    MEM_SHELL_GUARD=0x4f5000, MEM_PAGE_SIZE=0x1000,
                    MEM_SHELL_STACK_TOP=0x500000, MEM_SHELL_STACK_SIZE=0xa000)
    for name, value in expected.items():
        assert layout[name] == value, name
    return layout


def linked_image(path):
    elf = path.read_bytes()
    hdr = struct.unpack_from('>16sHHIIIIIHHHHHH', elf)
    assert hdr[0][:6] == b'\x7fELF\x01\x02' and hdr[2] == 20
    ph = [struct.unpack_from('>8I', elf, hdr[5]+i*hdr[9]) for i in range(hdr[10])]
    # With the lz4 root (PPC_ROOT_LZ4_DESIGN) the image has a second, high
    # PT_LOAD at the staging area; the resident budget is the low one at 1 MiB.
    load, = [p for p in ph if p[0] == 1 and p[2] == 0x100000]
    # boot-info's CRC case replaces ISO bytes after link, preserving the map.
    map_path = path.with_name('PPCKERN.map') if path.name == 'unverified.elf' else path.with_suffix('.map')
    symbols = {name: int(value, 16) for value, name in re.findall(
        r'^\s*(0x[0-9a-f]+)\s+(__\w+) =', map_path.read_text(), re.M)}
    _, _, va, pa, raw, memory, _, _ = load
    end = symbols['__kernel_resident_end']
    chain = symbols['__kernel_chain_end']
    assert va == pa == symbols['__kernel_image_start']
    assert va + raw == symbols['__kernel_file_end'] and va + memory == end
    layout = fixed_layout(path.parent/'memmap_layout.h')
    assert memory <= layout['MEM_KERNEL_IMAGE_MAX'], f'{path}: PT_LOAD size {memory:#x}'
    assert chain <= layout['MEM_FIXED_PAGING_GUARD_BASE'], f'{path}: chain end {chain:#x}'
    return elf, load, symbols, layout


def build(out, *settings):
    """Returns the ELF; make owns the manifest and the undefined/FP/PIT_HZ audits."""
    if not any('=' not in arg and not arg.startswith('-') for arg in settings):
        settings += ('kernel',)
    result = make(out, *settings)
    if result.returncode or 'kboot audit OK' not in result.stdout:
        raise RuntimeError('kboot build failed: '+str(out)+'\n'+(result.stdout+result.stderr)[-2000:])
    elf = out/'PPCKERN.ELF'
    _, _, symbols, _ = linked_image(elf)
    print(f'{elf}: resident_end={symbols["__kernel_resident_end"]:#x} '
          f'chain={symbols["__kernel_chain_end"]:#x}', flush=True)
    return elf


def build_media(out, *settings):
    elf = build(out, *settings, 'iso')
    media = Media(elf, out/'PPCKERN.map', out/'boot.iso')
    media.verify()
    return media
