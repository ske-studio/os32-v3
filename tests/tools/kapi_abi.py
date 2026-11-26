import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.kapi_layout import layouts
from tests.support.media import Failure, main


def public_definition(data):
    def arg_type(arg):
        return re.sub(r"\b[A-Za-z_]\w*(\s*(?:\[\d*\])?)$", r"\1", arg).strip()
    return {
        "slot": [(r["name"], r["ret"], tuple(map(arg_type, r["args"]))) for r in data["api"]],
        "field": [(r["name"], r["type"]) for r in data["data_fields"]],
        "func_capacity": data["func_capacity"], "crt_kapi_symbol": data["crt_kapi_symbol"],
        "kapi_abi": data["generations"]["kapi_abi"],
    }


def approves(message, item):
    # A similarly prefixed slot or a bare name without a reason does not
    # approve this change. Keep the match within this commit's trailer.
    token = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(item) + r"(?![A-Za-z0-9_])")
    for line in message.splitlines():
        if line.startswith("KAPI-Break:"):
            reason = line.removeprefix("KAPI-Break:").strip()
            if token.search(reason) and re.search(r"[^\W\d_]{2,}", token.sub("", reason)):
                return True
    return False


def compare(old, new, label, message):
    before, after = public_definition(old), public_definition(new)
    for kind, previous in before.items():
        current = after[kind]
        if current == previous:
            continue
        if kind in ("slot", "field"):
            changes = [(f"{kind} {n} {item[0]}", item, current[n] if n < len(current) else None)
                       for n, item in enumerate(previous) if n >= len(current) or current[n] != item]
        elif kind == "kapi_abi" and current > previous:
            changes = []
        else:
            changes = [(kind, previous, current)]
        for item, first, second in changes:
            approved = (new["version"] > old["version"] and after["kapi_abi"] > before["kapi_abi"]
                        and approves(message, item.split()[-1]))
            if not approved:
                raise Failure(f"{label} incompatible {item}: {first} -> {second}")
        if new["version"] <= old["version"]:
            raise Failure(f"{label} public {kind} changed without version increase")


def comparison_base():
    base = os.getenv("OS32_KAPI_BASE")
    if not base or not re.fullmatch(r"[0-9a-f]{40}", base):
        raise OSError("explicit OS32_KAPI_BASE SHA required")
    root = os.getenv("OS32_CHECK_REPOSITORY", ".")
    def git(*args):
        proc = subprocess.run(["git", "-C", root, *args], capture_output=True, text=True)
        if proc.returncode:
            raise OSError(f"{base} {proc.stderr}")
        return proc.stdout
    def definition(revision):
        try:
            return json.loads(git("show", f"{revision}:sdk/kapi.json"))
        except ValueError as exc:
            raise OSError(f"{revision}: invalid base public definition") from exc
    git("merge-base", "--is-ancestor", base, "HEAD")
    return base, git, definition


def verify(config):
    base, git, definition = comparison_base()
    def revision_layout(revision):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            for path in git("ls-tree", "-r", "--name-only", revision, "sdk/include/os32").splitlines():
                if path.endswith(".h"):
                    (folder / Path(path).name).write_text(git("show", f"{revision}:{path}"))
            return layouts(config, folder)

    def compare_revision(new, layout, label, message):
        compare(previous, new, label, message)
        for name, fields in old_layout.items():
            for field, old_value in fields.items():
                if name == "KernelAPI" and field == "kapi_reserved":
                    continue
                new_value = layout.get(name, {}).get(field)
                if new_value == old_value:
                    continue
                if (name == "KernelAPI" and field == "(sizeof)" and new_value is not None
                        and new_value > old_value and len(new["data_fields"]) > len(previous["data_fields"])):
                    continue
                item = f"{name}.{field}"
                approved = (new["version"] > previous["version"] and
                            new["generations"]["kapi_abi"] > previous["generations"]["kapi_abi"] and
                            approves(message, name if field.startswith("(") else item))
                if not approved:
                    raise Failure(f"{label} incompatible layout {item}: {old_value} -> {new_value}")
        if old_layout != layout and new["version"] <= previous["version"]:
            raise Failure(f"{label} public layout changed without version increase")

    previous, old_layout = definition(base), revision_layout(base)
    for commit in git("rev-list", "--reverse", "--first-parent", f"{base}..HEAD",
                      "--", "sdk/kapi.json", "sdk/include/os32").splitlines():
        current, new_layout = definition(commit), revision_layout(commit)
        compare_revision(current, new_layout, f"{base} {config} {commit}", git("show", "-s", "--format=%B", commit))
        previous, old_layout = current, new_layout
    compare_revision(json.loads(Path("sdk/kapi.json").read_text()), layouts(config, Path("sdk/include/os32")),
                     f"{base} {config} candidate", "")


if __name__ == "__main__":
    main(lambda config: None, verify)
