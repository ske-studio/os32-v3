"""Read kernel boot fragments in call order, including same-TU boot helpers.

This is a source view for the existing wiring checks and compiled fragments,
not a C interpreter. Only the three straight-line E02a calls are expanded;
their conditionals remain intact. Never read ROOT when testing a copied tree.
"""
import re


def kernel_parts(root):
    source = (root / 'init/kernel.c').read_text()
    return ['init/' + name for name in
            re.findall(r'^#include "(kernel_(?:boot_cpu|boot_memory|gfx_config)\.inc)"$',
                       source, re.M)]


def kernel_source(root, overrides=None):
    overrides = overrides or {}

    def read(path):
        return overrides[path] if path in overrides else (root / path).read_text()

    source = read('init/kernel.c')
    source = re.sub(
        r'^#include "(kernel_(?:boot_cpu|boot_memory|gfx_config)\.inc)"$',
        lambda m: read('init/' + m[1]), source, flags=re.M)
    for name, parameters, arguments in (
        ('kernel_boot_cpu', 'u32 mem_kb, struct boot_context *boot', 'mem_kb, &boot'),
        ('kernel_boot_memory', 'u32 mem_kb, struct boot_context *boot', 'mem_kb, &boot'),
        ('kernel_gfx_config', 'void', ''),
    ):
        definition = (r'^static void ' + name + r'\(' + re.escape(parameters) +
                      r'\)\n\{\n(.*?)^}\n')
        matches = list(re.finditer(definition, source, re.M | re.S))
        if not matches:
            assert not re.search(r'\b' + name + r'\(', source), name
            continue  # The pre-extraction kernel is also tested.
        assert len(matches) == 1, name
        match = matches[0]
        body = match[1]
        # Bind the existing boot context pointer to the caller's local object.
        body = body.replace('boot->', 'boot.').replace(', boot);', ', &boot);')
        source = source[:match.start()] + source[match.end():]
        call = '    ' + name + '(' + arguments + ');\n'
        assert source.count(call) == 1, name + ': exactly one boot call'
        source = source.replace(call, body, 1)
    return source
