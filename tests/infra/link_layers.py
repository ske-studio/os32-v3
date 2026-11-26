from pathlib import Path
import sys
import json
from functools import lru_cache

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.infra.include_layers import rule, target_wrong
from tests.support.link_symbols import data_declarations, declarations, references, symbols
from tests.support.media import Failure, main
from tests.support.product import build, commands, declared_sources, recipes
from tools.ownership import Inventory


def check_inputs(config, inventory, objects, links):
    selected = declared_sources(inventory, config)
    indirect = {out for out, args in commands(config) if "-r" in args}
    for output, inputs in links.items():
        for obj in inputs:
            if obj not in objects:
                if not Path(obj).is_file() or not (obj.endswith(".a") or obj in indirect):
                    raise Failure(f"{config} unknown direct object {obj} -> {output}")
                print(f"{config} {output} indirect input {obj}")
                continue
            source, _ = objects[obj]
            if source in inventory.paths and target_wrong(config, inventory.lookup(source)):
                raise Failure(f"{config} incompatible link input {source} -> {obj} -> {output}")
            if source not in selected:
                raise Failure(f"{config} unselected link input {source} -> {output}")
        if not Path(output).is_file():
            raise OSError(f"{config} missing final link {output}")


def verify(config):
    inventory = Inventory(Path.cwd())
    objects, links = recipes(config)
    check_inputs(config, inventory, objects, links)
    stage = Path("build/out/check-link")
    stage.mkdir(parents=True, exist_ok=True)
    import_files = {args[args.index("--elf") + 1]: args[args.index("--metadata") + 1]
                    for _, args in commands(config) if "--elf" in args and "--metadata" in args}
    imports = {out: {row["name"] for row in json.loads(Path(path).read_text())["imports"]}
               for out, path in import_files.items() if out in links}
    @lru_cache(None)
    def types(obj):
        return declarations(objects[obj][1], stage / "types.aux")

    @lru_cache(None)
    def data_types(obj):
        return data_declarations(objects[obj][1], config)

    @lru_cache(None)
    def assembly_declarations(provider, header):
        source, cmd = objects[provider]
        if not cmd or "-traditional-cpp" in cmd:
            # NASM has no C dialect. Use the kernel C flags of this configuration.
            source, cmd = next((src, flags) for src, flags in objects.values()
                               if src.startswith("sys/kern/") and src.endswith(".c"))
        probe = stage / "provider.c"
        probe.write_text(f'#include "{header}"\n')
        args = [str(probe) if arg == source else arg for arg in cmd]
        return declarations(args, stage / "provider.aux")

    symbol_table = lru_cache(None)(symbols)

    def contract(header):
        return header in contracts or (header in inventory.paths and
                inventory.lookup(header)["header_role"] == "fixed_contract")

    errors = set()
    contracts = inventory.tables["OWN_CONTRACT"]
    for output in links:
        for name, typ in symbol_table(output).items():
            if typ in ("U", "w", "v") and name not in imports.get(output, ()) and config not in inventory.tables["OWN_OPTIONAL"].get(name, ()):
                errors.add(f"{config} {output}: unresolved {name} ({typ})")
    for output, reader, symbol, provider in references(config, links, stage):
        if reader not in objects or provider not in objects:
            continue
        definition = symbol_table(provider).get(symbol, "U")
        if definition.upper() == "U" or symbol not in symbol_table(output):
            # ld also lists referring files for absolute script symbols.
            continue
        if definition in ("w", "v"):
            continue
        r, p = objects[reader][0], objects[provider][0]
        if (inventory.tables["OWN_IMPORT_PROVIDER"].get(p) == [import_files.get(output)]
                and symbol in imports.get(output, ())):
            continue
        def owner(src):
            if src in inventory.paths:
                return inventory.lookup(src)
            value = inventory.tables["OWN_GENERATED"].get(src)
            if not value:
                raise OSError(f"{config} unknown generated provider {src} -> {output}")
            return dict(kind=value[0], target=value[1], header_role=None)
        stub = inventory.tables["OWN_STUB"].get(p, [])
        if (definition in ("W", "V") and config not in inventory.tables["OWN_OPTIONAL"].get(symbol, ())
                and not (stub[:1] == [config] and symbol in stub[1:])):
            errors.add(f"{config} {output}: weak provider {p} for {symbol} lacks optional contract")
        violation = rule(config, owner(r), owner(p))
        if not violation:
            continue
        allowed = {signature for header, role, name, signature in types(reader)
                   if name == symbol and role == "C" and header in inventory.paths and
                   contract(header)}
        if allowed and any(name == symbol and role == "F" and signature in allowed
                           for _, role, name, signature in types(provider)):
            continue
        public_types = {ty for header, name, ty, _ in data_types(reader) if name == symbol and contract(header)}
        if public_types and any(name == symbol and ty in public_types and defined
                                for _, name, ty, defined in data_types(provider)):
            continue
        edge = f"{config} {output}: {r} -> {symbol} -> {p} ({violation})"
        if p.endswith((".S", ".s", ".asm")):
            headers = inventory.tables["OWN_ASM_CONTRACT"].get(p, ())
            if any(contract(header) and origin == header and name == symbol and role == "C"
                   and signature in allowed for header in headers
                   for origin, role, name, signature in assembly_declarations(provider, header)):
                continue
        errors.add(edge)
    if errors:
        raise Failure("\n".join(sorted(errors)))


if __name__ == "__main__":
    main(build, verify)
