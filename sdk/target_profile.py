"""Target capabilities; PowerPC currently supports object inspection only."""
PROFILES = {
    'x86-pc98': dict(elf_identity=(1, 1, 3), module_codec=True,
                     layout=True, link_guard=True, ar='i386-elf-ar'),
    'ppc-powermac': dict(elf_identity=(1, 2, 20), module_codec=False,
                         layout=False, link_guard=False, ar='powerpc-elf-ar'),
}
MESSAGES = dict(module_codec='module', layout='kernel layout', link_guard='guarded-link')


def require(target, capability):
    if target not in PROFILES:
        raise ValueError('unknown target ' + target)
    profile = PROFILES[target]
    if not profile[capability]:
        raise ValueError('unsupported ' + MESSAGES[capability] + ' target ' + target)
    return profile


def check_elf_identity(path, profile):
    with open(path, 'rb') as stream:
        data = stream.read(20)
    if len(data) < 20 or data[:4] != b'\x7fELF' or data[5] not in (1, 2):
        raise ValueError('invalid ELF identity: ' + str(path))
    machine = int.from_bytes(data[18:20], 'little' if data[5] == 1 else 'big')
    if (data[4], data[5], machine) != profile['elf_identity']:
        raise ValueError('ELF target mismatch: ' + str(path))
