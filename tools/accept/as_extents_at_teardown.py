#!/usr/bin/env python3
"""PM teardown snapshot (read-only; no network without --live).

prepare OUT: save offsets, hashes and the nm entry BP for this build. PM keeps
this JSON with the deployment evidence, verifies the running image using ver
and the deployed image hash, and arms that entry BP. Launch CUI commands from
shell/rshell, GUI commands from gshell's Run dialog. At the trap:
  capture OUT --deployment DEPLOY.json --deployed-image COPIED_VMKERNEL --cmd CMD --id ID --live
COPIED_VMKERNEL must be read back from the deployed boot path, not build/out.
The deployment JSON must be the prepare output saved for that deployment.
Capture neither installs/deletes BPs nor resumes the guest. PM releases the
trap after saving the JSON. Extents are the values immediately before teardown,
not a lifetime maximum. EAX is AppSlot*, never an AS at entry+0x24.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from np21w_mcp import np21w_client as emu
WORD_BYTES = struct.calcsize('<I')
LAYOUT_SOURCE = r'''
#include <stddef.h>
#include "appslot.h"
#include "appmem_types.h"
#define EMIT(name, value) __asm__ volatile(".globl ext_" #name "\n.set ext_" #name ", %c0" : : "i"(value))
void ext_layout(void) {
    EMIT(pointer_size, sizeof(void *));
    EMIT(word_size, sizeof(u32));
    EMIT(slot_size, sizeof(AppSlot));
    EMIT(slot_as, offsetof(AppSlot, as));
    EMIT(slot_state, offsetof(AppSlot, state));
    EMIT(slot_cpl3, offsetof(AppSlot, cpl3));
    EMIT(as_size, sizeof(struct addrspace));
    EMIT(as_pd, offsetof(struct addrspace, pd_phys));
    EMIT(as_appmem, offsetof(struct addrspace, appmem));
    EMIT(as_poisoned, offsetof(struct addrspace, appmem_poisoned));
    EMIT(table_e, offsetof(struct appmem_table, e));
    EMIT(extent_size, sizeof(struct appmem_extent));
    EMIT(extent_base, offsetof(struct appmem_extent, base));
    EMIT(extent_end, offsetof(struct appmem_extent, end));
    EMIT(extent_kind, offsetof(struct appmem_extent, kind));
    EMIT(extent_flags, offsetof(struct appmem_extent, flags));
    EMIT(extent_count, APPMEM_EXTENT_MAX);
    EMIT(kind_free, 0);
    EMIT(kind_libc, APPMEM_LIBC_INITIAL);
    EMIT(kind_exec, APPMEM_EXEC_INITIAL);
    EMIT(kind_anon, APPMEM_ANON);
    EMIT(kind_arena, APPMEM_EXEC_ARENA);
    EMIT(kind_large, APPMEM_EXEC_LARGE);
    EMIT(app_min, APP_ID_MIN);
    EMIT(app_max, APP_ID_MAX);
    EMIT(state_free, APP_STATE_FREE);
}
'''


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def nm_symbols(raw, wanted=None):
    result = {}
    for line in raw.splitlines():
        row = line.split()
        if len(row) == 3 and (wanted is None or row[2] in wanted):
            require(row[2] not in result or result[row[2]] == int(row[0], 16),
                    'ambiguous nm symbol: ' + row[2])
            result[row[2]] = int(row[0], 16)
    return result


def generate_offsets(cross):
    """Same ILP32 cross GCC and headers as tools/h3_layout.c; no host ABI."""
    with tempfile.TemporaryDirectory(prefix='ext-layout-') as directory:
        obj = Path(directory) / 'layout.o'
        subprocess.run([str(cross / 'i386-elf-gcc'), '-std=gnu11', '-ffreestanding',
                        '-I.', '-Iinclude', '-Iexec', '-Ikernel', '-x', 'c', '-c',
                        '-', '-o', str(obj)], input=LAYOUT_SOURCE, text=True,
                       cwd=ROOT, check=True, capture_output=True)
        raw = subprocess.check_output([str(cross / 'i386-elf-nm'), '--defined-only',
                                       str(obj)], text=True)
    offsets = {k[4:]: v for k, v in nm_symbols(raw).items() if k.startswith('ext_')}
    require(offsets['pointer_size'] == offsets['word_size'] == WORD_BYTES,
            'ILP32 compiler required')
    return offsets


def entry_code(disasm, bp, offsets):
    rows = []
    for line in disasm.splitlines():
        match = re.match(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)(.*)$', line)
        if match:
            rows.append((int(match[1], 16), bytes.fromhex(match[2]), match[3].strip()))
    require(rows and rows[0][0] == bp, 'disassembly does not start at nm entry')
    # GCC uses an internal regparm convention for this static function. Prove
    # the actual build starts with EAX's null check, then reads cpl3 and as from
    # that SAME EAX before the first call. Fail closed if code generation changes.
    require(re.fullmatch(r'test\s+%eax,%eax', rows[0][2]) is not None,
            'entry EAX convention changed')
    before_call = []
    for _, _, instruction in rows:
        if instruction.startswith(('call', 'jmp')):
            break
        before_call.append(instruction)
    as_load = r'mov\s+0x%x\(%%eax\),%%eax' % offsets['slot_as']
    hits = [i for i, op in enumerate(before_call) if re.fullmatch(as_load, op)]
    require(len(hits) == 1, 'entry does not load AppSlot.as from EAX')
    prefix = before_call[:hits[0]]
    cpl3_load = r'mov\s+0x%x\(%%eax\),%%[a-z]+' % offsets['slot_cpl3']
    require(any(re.fullmatch(cpl3_load, op) for op in prefix), 'missing AppSlot.cpl3 load')
    require(not any(re.match(r'(mov|lea|pop|xor|add|sub|and|or)\s+.*(?:,|\s)%eax$', op)
                    for op in prefix), 'EAX overwritten before AppSlot.as load')
    raw = bytearray()
    for address, code, _ in rows:
        require(address == bp + len(raw), 'noncontiguous function bytes')
        raw.extend(code)
    return raw.hex()


def make_layout(elf, kernel_map, image):
    cross = Path(os.environ['CROSS_DIR']) / 'bin'
    offsets = generate_offsets(cross)
    required = ('exec_teardown_app', 'g_slot', '__bss_end', 'exec_as_leftover_pages')
    sym = nm_symbols(subprocess.check_output(
        [str(cross / 'i386-elf-nm'), '--defined-only', str(elf)], text=True), required)
    require(all(name in sym for name in required), 'kernel symbols missing')
    map_text = Path(kernel_map).read_text()
    for name in ('__bss_end', 'exec_as_leftover_pages'):
        values = re.findall(r'(0x[0-9a-fA-F]+)\s+' + name + r'\b', map_text)
        require(values and all(int(v, 16) == sym[name] for v in values), 'map/ELF mismatch: ' + name)
    disasm = subprocess.check_output([str(cross / 'i386-elf-objdump'), '-d',
                                     '--disassemble=exec_teardown_app', str(elf)], text=True)
    return dict(format=1, elf_sha256=digest(elf), map_sha256=digest(kernel_map),
                image_sha256=digest(image), offsets=offsets, bp=sym['exec_teardown_app'],
                slots=sym['g_slot'], eax_role='AppSlot*',
                code_hex=entry_code(disasm, sym['exec_teardown_app'], offsets),
                compiler=subprocess.check_output([str(cross / 'i386-elf-gcc'),
                                                  '--version'], text=True).splitlines()[0])


def validate_deployment(layout, deployment, deployed_image_hash):
    for key in ('format', 'elf_sha256', 'map_sha256', 'image_sha256', 'offsets',
                'bp', 'slots', 'eax_role', 'code_hex', 'compiler'):
        require(deployment.get(key) == layout[key], 'deployment mismatch: ' + key)
    require(layout['eax_role'] == 'AppSlot*', 'EAX must be AppSlot* at entry')
    require(deployed_image_hash == layout['image_sha256'], 'deployed image hash mismatch')


def word(raw, offset):
    require(0 <= offset <= len(raw) - WORD_BYTES, 'field outside object')
    return struct.unpack_from('<I', raw, offset)[0]


def parse_extents(raw, offsets):
    require(len(raw) == offsets['extent_count'] * offsets['extent_size'], 'short extent table')
    kinds = {offsets['kind_libc']: 'LIBC_INITIAL', offsets['kind_exec']: 'EXEC_INITIAL',
             offsets['kind_anon']: 'ANON', offsets['kind_arena']: 'EXEC_ARENA',
             offsets['kind_large']: 'EXEC_LARGE'}
    counts = dict.fromkeys(kinds.values(), 0)
    for i in range(offsets['extent_count']):
        row = raw[i * offsets['extent_size']:(i + 1) * offsets['extent_size']]
        kind = word(row, offsets['extent_kind'])
        if kind == offsets['kind_free']:
            continue
        require(kind in kinds, 'unknown extent kind')
        require(word(row, offsets['extent_base']) < word(row, offsets['extent_end']), 'invalid extent range')
        counts[kinds[kind]] += 1
    total = sum(counts.values())
    return dict(total=total, free=offsets['extent_count'] - total, kinds=counts,
                arenas=counts['ANON'] + counts['EXEC_ARENA'])


def snapshot(layout, deployment, deployed_image_hash, regs, read_mem, cmd, app_id):
    """One injected address->bytes reader traverses the real entry EAX chain."""
    validate_deployment(layout, deployment, deployed_image_hash)
    require(regs['eip'] == layout['bp'], 'not stopped at exec_teardown_app ENTRY')
    o = layout['offsets']
    require(o['app_min'] <= app_id <= o['app_max'], 'USER app ID required')
    slot_ptr = regs['eax']
    require(slot_ptr == layout['slots'] + app_id * o['slot_size'], 'EAX is not the requested AppSlot')

    def read(address, size):
        require(0 < address <= 0xffffffff and size <= 0x100000000 - address, 'invalid read address')
        raw = read_mem(address, size)
        require(len(raw) == size, 'short memory read')
        return raw

    code = bytes.fromhex(layout['code_hex'])
    require(read(layout['bp'], len(code)) == code, 'running ELF code mismatch')
    slot = read(slot_ptr, o['slot_size'])
    require(word(slot, o['slot_cpl3']) == 1 and word(slot, o['slot_state']) != o['state_free'], 'not a live USER slot')
    as_ptr = word(slot, o['slot_as'])
    require(as_ptr != 0, 'AppSlot has no AS')
    address_space = read(as_ptr, o['as_size'])
    require(word(address_space, o['as_pd']) != 0, 'AS has no PD')
    table_start = o['as_appmem'] + o['table_e']
    table_end = table_start + o['extent_count'] * o['extent_size']
    result = parse_extents(address_space[table_start:table_end], o)
    result.update(cmd=cmd, id=app_id, poisoned=word(address_space, o['as_poisoned']),
                  slot=slot_ptr, addrspace=as_ptr, bp=regs['eip'],
                  elf_sha256=layout['elf_sha256'], map_sha256=layout['map_sha256'],
                  image_sha256=layout['image_sha256'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'capture'))
    parser.add_argument('out', type=Path)
    parser.add_argument('--elf', type=Path, default=ROOT / 'build/out/kernel.elf')
    parser.add_argument('--map', type=Path, default=ROOT / 'build/out/kernel.map')
    parser.add_argument('--image', type=Path, default=ROOT / 'build/out/vmkernel.lz4')
    parser.add_argument('--deployment', type=Path)
    parser.add_argument('--deployed-image', type=Path)
    parser.add_argument('--cmd')
    parser.add_argument('--id', type=int)
    parser.add_argument('--live', action='store_true')
    args = parser.parse_args()
    try:
        layout = make_layout(args.elf, args.map, args.image)
        result = layout
        if args.action == 'capture':
            if not (args.live and args.deployment and args.deployed_image and args.cmd and args.id is not None):
                parser.error('capture needs --live, --deployment, --deployed-image, --cmd and --id')
            deployment = json.loads(args.deployment.read_text())
            image_hash = digest(args.deployed_image)
            validate_deployment(layout, deployment, image_hash)
            state = json.loads(emu.get('/api/instance', timeout=20))
            require(state.get('ok') is True and state.get('trap_pause') is True and
                    state.get('user_pause') is False, 'entry breakpoint trap required')
            raw_regs = json.loads(emu.get('/api/regs', timeout=20))
            regs = {key: int(raw_regs[key], 16) for key in ('eax', 'eip')}
            def read_mem(address, size):
                reply = json.loads(emu.get('/api/mem?addr=0x%x&len=%d&space=phys' % (address, size), timeout=20))
                require(reply.get('ok') is True, 'memory read rejected')
                return bytes.fromhex(reply['hex'])
            result = snapshot(layout, deployment, image_hash, regs, read_mem, args.cmd, args.id)
        elif args.live:
            parser.error('--live is only for capture')
        with args.out.open('x') as output:
            output.write(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
        print(json.dumps(result, sort_keys=True, ensure_ascii=False))
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError, emu.EmuError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
