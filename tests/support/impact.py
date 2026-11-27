"""Select registered configurations and their distribution media for one Git diff."""
from fnmatch import fnmatchcase
from pathlib import Path
from subprocess import CalledProcessError
from tools.ownership import Inventory, OwnershipError, git

PPC = ("core", "link", "sh", "link-sh", "shell", "neg1")
GATE_CHECKS = {"collection_contract", "landing_evidence", "registry_fields", "test_registration"}


def media_inputs(inventory):
    import yaml
    from tools.deploy_manifests import CORE_MANIFEST_RELPATHS

    inputs = ["docs/design/hwdev/hwdev_imports.md"]
    for path in CORE_MANIFEST_RELPATHS:
        data = (git(inventory.root, "show", f"{inventory.revision}:{path}") if inventory.revision
                else (inventory.root / path).read_bytes())
        entries = yaml.safe_load(data)["filesystem"]["files"]
        inputs.extend(entry["host"] for entry in entries if not entry.get("host_only"))
    return inputs


def scope(path, inventory, direct, product_inputs):
    if inventory is None:
        return "full"
    try:
        owner = inventory.lookup(path)
    except OwnershipError:
        return "full"
    name = Path(path).name
    if path.startswith(("tests/", "tools/ci/")):
        # A shared helper can have indirect consumers outside registry.sources.
        if path == "tests/registry.toml" or path.startswith("tests/support/"):
            return "full"
        return "checks" if direct or path == "tests/infra/ci.py" else "full"
    if (owner["public"] or owner["header_role"] == "fixed_contract" or path in inventory.tables["OWN_CONTRACT"]
            or name.endswith("_contract.h") or name.startswith("platform_") and name.endswith(".h")
            or path.startswith(("build/", ".github/", "sdk/"))
            or name in ("Makefile", "Cargo.toml", "Cargo.lock") or name.endswith(".mk")
            or path in ("tools/ownership.py", "sys/include/memmap.h",
                        "sys/kapi/kapi_generated.c", "sys/include/kapi_call_generated.h",
                        "sys/kern/exec/exec_kapi_init.inc")
            or name in ("callconv.h", "os32x_hdr.c", "os32x_hdr.h", "syscall_code.h")
            or "abi" in name or "generation" in name or owner["kind"] == "ビルドの定義"):
        return "full"
    if owner["kind"] == "文書":
        if any(fnmatchcase(path, pattern) for pattern in product_inputs):
            return "full"
        return "docs"
    if path.startswith("userland/") and set(inventory.tables["OWN_COMPILE"].get(path, ())) & set(PPC):
        if "pc98" in inventory.tables["OWN_COMPILE"][path]:
            return "shared-userland"
        return "ppc-userland"
    if path.startswith("userland/ppc/"):
        return "ppc-userland"
    if path.startswith("userland/"):
        if path.startswith(("userland/shell/", "userland/lib/", "userland/gshell/", "userland/rust/")):
            if path not in inventory.tables["OWN_COMPILE"]:
                return "shared-userland"
        return "userland"
    if owner["target"] in ("x86", "pc98") or path.startswith(("sys/platform/pc98/", "sys/arch/x86/")):
        return "x86"
    if owner["target"] in ("ppc", "powermac") or path.startswith(("sys/platform/powermac/", "sys/arch/ppc/")):
        return "ppc"
    if path.startswith(("sys/", "lib/")) and owner["kind"] in ("共通実装", "デバイス固有実装"):
        return "kernel"
    return "full"


def impact(rows, paths, inventory, force=False):
    scopes = {}
    direct_pairs = set()
    if inventory is not None and not hasattr(inventory, "media_inputs"):
        inventory.media_inputs = media_inputs(inventory)
    product_inputs = inventory.media_inputs if inventory is not None else []
    for path in sorted(paths):
        direct = {r["id"] for r in rows if path == f'tests/{r["subsystem"]}/{r["id"]}.py'
                  or any(path == source or path.startswith(source + "/") for source in r["sources"])}
        if any(row["spec"]["path"] == path for row in rows):
            direct.add("test_registration")
        scopes[path] = scope(path, inventory, direct, product_inputs)
        for name in direct:
            bundles = {"x86", "ppc", "host"}
            if scopes[path] in ("x86", "userland"):
                bundles.discard("ppc")
            if scopes[path] in ("ppc", "ppc-userland"):
                bundles.discard("x86")
            direct_pairs.update((name, bundle) for bundle in bundles)
    product_scopes = {"x86", "ppc", "kernel", "userland", "shared-userland", "ppc-userland"}
    declared_configs = set()
    shared_programs = set()
    if not force and "full" not in scopes.values() and product_scopes.intersection(scopes.values()):
        from tools.ownership_images import image_dependencies
        if not hasattr(inventory, "declared_images"):
            try:
                inventory.declared_images = image_dependencies(inventory, declared_only=True)
            except (OSError, CalledProcessError):
                inventory.declared_images = {}  # Unknown product inputs require the full plan.
        for path, kind in scopes.items():
            if kind not in product_scopes:
                continue
            outputs = inventory.declared_images.get(path, ())
            declared_configs.update(config for config, _ in outputs)
            if not outputs or Path(path).suffix not in (".c", ".asm", ".s", ".S"):
                scopes[path] = "full"
            elif any(config == "pc98" and output.startswith("userland/") and output.endswith((".elf", ".bin"))
                     for config, output in outputs):
                shared_programs.add(path)
    kinds = set(scopes.values())
    if shared_programs:
        kinds.add("userland")
    ppc_configs = declared_configs.intersection(PPC)
    for path, kind in scopes.items():
        if kind in ("ppc-userland", "shared-userland"):
            ppc_configs.update(set(inventory.tables["OWN_COMPILE"].get(path, PPC)).intersection(PPC))
    if "shared-userland" in kinds:
        kinds.add("userland")
    full = force or not paths or "full" in kinds
    cpu_bundles = kinds.intersection(("x86", "ppc"))
    if "kernel" in kinds:
        cpu_bundles.update(("x86", "ppc"))
    userland_checks = {"ring3_instructions", "c_dialect", "x86_media_build", "build_identity"}
    entries = []
    skipped = {}
    images = set(PPC) if full or "ppc" in cpu_bundles else set(ppc_configs)
    needs_distribution = full or "pc98" in declared_configs or "x86" in cpu_bundles or "userland" in kinds
    pc98 = "distribution" if needs_distribution else "none"
    for row in rows:
        name = row["id"]
        for bundle, configs in row["arch"].items():
            selected = full or (name, bundle) in direct_pairs
            if bundle in cpu_bundles and name != "ring3_instructions":
                selected = True
            if "userland" in kinds and bundle == "x86" and name in userland_checks:
                selected = True
            if bundle == "host":
                selected |= bool(cpu_bundles) and name == "src_owner"
                selected |= "checks" in kinds and name in GATE_CHECKS
                selected |= "docs" in kinds and name.startswith("docs_")
            qemu = row["environment"].startswith("qemu-ppc")
            for config in configs:
                matches_config = bundle == "ppc" and config in ppc_configs and (qemu or name in (
                    "c_dialect", "ppc_media_build", "build_identity"))
                if selected or matches_config:
                    entries.append([name, config])
                    if qemu or name == "ppc_media_build":
                        images.add(config)
                    elif name == "x86_media_build":
                        pc98 = "distribution"
                else:
                    skipped[f"{name}/{config}"] = "outside changed ownership and registered inputs"
    if "docs" in kinds:
        images.add("core")
        if pc98 == "none":
            pc98 = "fd"
    products = {"pc98": pc98 != "none", "pc98-distribution": pc98 == "distribution",
                "sdk": full, "userland": full or "userland" in kinds}
    products.update({f"ppc-{image}": image in images for image in PPC})
    omitted = {product: "not required by " + ", ".join(sorted(kinds)) for product, active in
               products.items() if not active}
    return dict(omitted_products=omitted, paths=scopes, full=full, entries=entries, skipped=skipped, pc98=pc98,
                powerpc=[image for image in PPC if image in images], sdk=full,
                userland=full or "userland" in kinds)


def comparison(root, rows, base, sha, force):
    paths = git(root, "diff", "--no-renames", "--name-only", "-z", base, sha, "--")
    try:
        inventory = Inventory(root, sha)
    except OwnershipError:
        inventory = None  # The full run includes the ownership diagnostic.
    return impact(rows, paths.decode().split("\0")[:-1], inventory, force)
