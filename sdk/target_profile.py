"""Target capabilities and SDK selections."""
# The SDK selection is one set: C runtime objects, link scripts, the Rust target,
# the linker emulation and the call ABI are chosen together (paths are relative
# to the SDK root: sdk/ in the tree, $(OS32_SDK) in a staged SDK).
SDK_FIELDS = ('call_abi', 'elf_emulation', 'crt_user', 'crt_resident',
              'app_link', 'app_sys_link', 'shlib_link', 'rust')
USER_SDK_FIELDS = ('call_abi', 'elf_emulation', 'crt_user', 'app_link')
_X86_SDK = dict(
    call_abi='i386-cdecl', elf_emulation='elf_i386',
    crt_user=('crt/crt0.o', 'crt/crt0_c.o', 'crt/syscalls.o', 'crt/help.o'),
    crt_resident=('crt/crt0.o', 'crt/crt0_c.o', 'crt/resident/syscalls.o',
                  'crt/help.o', 'crt/resident/nano_adapter.o'),
    app_link='link/app.ld', app_sys_link='link/app_sys.ld',
    shlib_link='link/shlib.ld', rust='rust/i686-os32-none.json')
PROFILES = {
    'x86-pc98': dict(elf_identity=(1, 1, 3), module_codec=True,
                     layout=True, link_guard=True, ar='i386-elf-ar',
                     sdk=True, sdk_user=True, **_X86_SDK),
    'ppc-powermac': dict(elf_identity=(1, 2, 20), module_codec=False,
                         layout=False, link_guard=False, ar='powerpc-elf-ar',
                         sdk=False, **dict.fromkeys(SDK_FIELDS)),
}
PROFILES['ppc-powermac'].update(
    sdk_user=True, call_abi='powerpc-sysv', elf_emulation='elf32ppc',
    crt_user=('crt/ppc/crt0.o', 'crt/ppc/crt0_c.o'),
    app_link='link/ppc/app.ld')
MESSAGES = dict(module_codec='module', layout='kernel layout', link_guard='guarded-link',
                sdk='sdk', sdk_user='user sdk')


class Unsupported(ValueError):
    """The target is unknown or lacks the capability (a refusal, not a defect)."""


def require(target, capability):
    if target not in PROFILES:
        raise Unsupported('unknown target ' + target)
    profile = PROFILES[target]
    if not profile[capability]:
        raise Unsupported('unsupported ' + MESSAGES[capability] + ' target ' + target)
    return profile


def sdk_values(target):
    """The SDK selection of a target. Call this before reading inputs, removing
    outputs or starting a tool: an unsupported target is refused here."""
    profile = require(target, 'sdk')
    values = {field: profile[field] for field in SDK_FIELDS}
    if not all(values.values()):
        raise ValueError('incomplete sdk target ' + target)
    return values


def sdk_user_values(target):
    """Select only the fields needed to build a USER C application."""
    profile = require(target, 'sdk_user')
    values = {field: profile[field] for field in USER_SDK_FIELDS}
    if not all(values.values()):
        raise ValueError('incomplete user sdk target ' + target)
    return values


def check_elf_identity(path, profile):
    with open(path, 'rb') as stream:
        data = stream.read(20)
    if len(data) < 20 or data[:4] != b'\x7fELF' or data[5] not in (1, 2):
        raise ValueError('invalid ELF identity: ' + str(path))
    machine = int.from_bytes(data[18:20], 'little' if data[5] == 1 else 'big')
    if (data[4], data[5], machine) != profile['elf_identity']:
        raise ValueError('ELF target mismatch: ' + str(path))
