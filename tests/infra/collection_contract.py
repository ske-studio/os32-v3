# 期待の出所: docs/decisions/ppc/PPC_SHELL_ALIGN_X86_DESIGN.md「製品の収集計画」。
"""Exercise real runner, independent failures, and incomplete or invalid bundles."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from unittest.mock import patch

from tests.infra.ci import clear_selected_evidence, check_prerequisites, collect, job_errors, run_bundle, selection
from tests.support.media import Failure, main


def check(value, why):
    if not value:
        raise Failure(why)


def verify_selection():
    import tomllib
    from tests.support.impact import impact, PPC
    from tools.ownership_images import image_dependencies
    from tools.ownership import Inventory
    root = Path.cwd()
    inventory = Inventory(root)
    rows = tomllib.loads((root / "tests/registry.toml").read_text())["check"]

    legacy_bodies = (
        "infra/ppc_media_build.py",
        "infra/x86_media_build.py",
        "infra/ppc_common_compile.py",
        "infra/build_identity.py",
        "infra/registry_fields.py",
        "system/ppc_boot.py",
        "infra/landing_evidence.py",
        "infra/src_owner.py",
        "infra/build_sources.py",
        "infra/include_layers.py",
        "infra/link_layers.py",
        "infra/hal_contracts.py",
        "infra/test_registration.py",
        "tools/docs_links.py",
        "tools/docs_navigation.py",
        "tools/docs_source_refs.py",
        "tools/docs_status.py",
        "tools/kapi_abi.py",
        "tools/kapi_sync.py",
        "infra/c_dialect.py",
        "infra/p2v.py",
        "infra/ring3_instructions.py",
    )
    for body in legacy_bodies:
        check("OS32_VERIFY_OUT" not in (root / "tests" / body).read_text(), "legacy verify environment")
    rows.append(dict(next(row for row in rows if row["id"] == "ppc_boot"),
                     id="net_fixture", environment="qemu-ppc-net", sources=[]))

    declared = image_dependencies(inventory, declared_only=True)
    inventory.declared_images = declared

    def plan(path):
        result = impact(rows, [path], inventory)
        return result, {tuple(entry) for entry in result["entries"]}

    result, entries = plan("userland/cmds/cal.c")
    check(result["pc98"] == "distribution" and result["powerpc"] == [] and result["userland"]
          and not result["sdk"], "userland products")
    check(entries == {(name, "pc98") for name in ("ring3_instructions", "c_dialect",
                                                  "x86_media_build", "build_identity")}, "userland checks")
    result, entries = plan("userland/shell/cmd_dir.c")
    check(result["pc98"] == "distribution" and set(result["powerpc"]) == {"sh", "link-sh", "powermac"}
          and ("ppc_boot", "powermac") in entries and ("ppc_boot", "core") not in entries,
          "shared userland media")
    check({cfg for name, cfg in entries if name == "net_fixture"} == set(result["powerpc"]),
          "net fixture image selection")
    result, entries = plan("sys/kern/av_focus.c")
    check(result["pc98"] == "distribution" and result["powerpc"] == list(PPC)
          and not result["userland"] and not result["sdk"], "kernel products")
    check({("link_layers", "pc98"), ("kapi_abi", "core"), ("ppc_boot", "sh")} <= entries
          and ("ring3_instructions", "pc98") not in entries, "kernel checks")
    for path in ("lib/lz4.c", "lib/utf8.c"):
        result, entries = plan(path)
        check(not result["full"] and result["userland"] and result["powerpc"] == list(PPC)
              and {("ring3_instructions", "pc98"), ("c_dialect", "pc98"),
                   ("link_layers", "pc98")} <= entries, f"shared program inputs: {path}")
    result, entries = plan("sys/platform/powermac/apm.c")
    check(result["pc98"] == "none" and result["powerpc"] == list(PPC)
          and ("ppc_boot", "core") in entries and all(c != "pc98" for _, c in entries), "PowerPC scope")
    result, entries = plan("sys/arch/x86/kernel/gdt.c")
    check(result["pc98"] == "distribution" and result["powerpc"] == []
          and ("include_layers", "pc98") in entries and ("ring3_instructions", "pc98") not in entries,
          "x86 scope")
    result, entries = plan("tools/ci/land_gate.py")
    check(result["pc98"] == "none" and result["powerpc"] == []
          and {("landing_evidence", "local"), ("collection_contract", "local")} <= entries, "gate scope")
    result, entries = plan("tests/infra/p2v.py")
    check(result["pc98"] == "none" and result["powerpc"] == []
          and {("p2v", "pc98"), ("p2v", "link-sh")} <= entries, "independent cross checks")
    result, entries = plan("docs/INDEX.md")
    check(result["pc98"] == "fd" and result["powerpc"] == ["core"]
          and entries == {(r["id"], "local") for r in rows if r["id"].startswith("docs_")}, "docs scope")
    jobs = {job: {"result": "success"} for job in ("plan", "artifacts", "powerpc", "checks-host")}
    jobs.update({job: {"result": "skipped"} for job in ("checks-x86", "checks-ppc")})
    check(not job_errors(jobs, result, [{"bundle": "host"}]), "planned skip rejected")
    jobs["checks-ppc"]["result"] = "success"
    check(not job_errors(jobs, result, [{"bundle": name} for name in
                        ("host", "ppc-net-1", "ppc-guest-1")]), "PPC matrix success rejected")
    jobs["checks-ppc"]["result"] = "skipped"
    jobs["powerpc"]["result"] = "skipped"
    check(job_errors(jobs, result, [{"bundle": "host"}]), "missing required boot medium accepted")
    for path in ("sdk/kapi.json", "sys/include/ide_contract.h", "sys/kern/exec/os32x_hdr.h",
                 "build/image.mk", ".github/workflows/restructure-build.yml", "tests/registry.toml",
                 "unknown/path", "sys/kern/kstring.h"):
        result, entries = plan(path)
        check(result["full"] and result["sdk"] and result["userland"] and not result["skipped"]
              and len(entries) == sum(len(c) for r in rows for c in r["arch"].values()), "full scope")

    spec_paths = {row["spec"]["path"] for row in rows}
    for path in sorted(inventory.paths):
        result, entries = plan(path)
        configs = {config for config, _ in declared.get(path, ())}
        selected = set(result["powerpc"])
        if result["pc98"] == "distribution":
            selected.add("pc98")
        check(configs <= selected, f"declared images: {path}: {sorted(configs - selected)}")
        if path in spec_paths:
            check(("test_registration", "local") in entries, f"spec input: {path}")

    docs_entries = {(row["id"], "local") for row in rows if row["id"].startswith("docs_")}
    for path in ("docs/design/tools/checks/tests_v2.md", "docs/design/tools/ci/ci_v2.md",
                 "docs/CONSTRAINTS.md"):
        result, entries = plan(path)
        check(result["pc98"] == "fd" and result["powerpc"] == ["core"]
              and entries == docs_entries | {("test_registration", "local")}, f"spec scope: {path}")
    _, entries = plan("docs/KAPI_SPEC.md")
    check({("kapi_sync", "pc98"), ("kapi_sync", "link-sh")} <= entries, "KAPI document input")



def verify_link(component):
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp).resolve()
        directory = output / "evidence" / "first" / "local"
        directory.mkdir(parents=True)
        marker = directory / "serial.log"
        marker.write_text("existing")
        linked = output / component
        with patch.object(Path, "is_symlink", lambda path: path == linked), patch("shutil.rmtree") as remove:
            try:
                clear_selected_evidence([({"id": "first"}, "host", "local")], SimpleNamespace(output=output))
            except ValueError:
                pass
            else:
                raise Failure(f"accepted linked component {component}")
            remove.assert_not_called()
        check(marker.read_text() == "existing", "existing evidence changed")


def verify(config):
    for component in ("evidence", "evidence/first", "evidence/first/local"):
        verify_link(component)
    verify_selection()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        source = root / "source"
        bodies = source / "tests/infra"
        bodies.mkdir(parents=True)
        out = root / "results"
        out.mkdir()
        args = SimpleNamespace(gate="main", output=out, mutate=False, bundle=None)
        plan = []
        for name, cpu, rc in (("first", "host", 1), ("second", "ppc", 1), ("last", "host", 0)):
            (bodies / f"{name}.py").write_text(f'import sys\nif sys.argv[-1] == "verify":\n    print("{name} diagnostic")\n    sys.exit({rc})\n')
            plan.append((dict(id=name, subsystem="infra", gates=["main"], mutations=[],
                              environment="python", sources=[]), cpu, "test"))
        rows = [dict(row, arch={cpu: [cfg]}, level="L4", environment=env)
                for (row, cpu, cfg), env in zip(plan, ("python", "qemu-ppc-net", "qemu-ppc"))]
        rows[-1]["arch"] = {"ppc": ["test"]}
        selector = SimpleNamespace(check=None, config=None, bundle=None)
        plan = list(selection(rows, selector))
        check([bundle for _, bundle, _ in plan] == ["host", "ppc-net-1", "ppc-guest-1"],
              "PPC runner separation")
        overflow = dict(rows[1], id="overflow", ppc_seconds={"test": 500})
        rows[1]["ppc_seconds"] = {"test": 410}
        selector.check = "overflow"
        check([bundle for _, bundle, _ in selection([*rows, overflow], selector)] == ["ppc-net-2"],
              "overweight PPC bundle or filter-dependent assignment")
        identity = dict(sha="fixture", run="local", attempt="1", mutate=False, gate="main")
        for args.bundle in ("host", "ppc-net-1", "ppc-guest-1"):
            run_bundle(source, [r for r in plan if r[1] == args.bundle], args, {}, identity)
        args.github_output = root / "github-output"
        output = io.StringIO()
        with redirect_stdout(output):
            check(collect(plan, args, identity) == 1, "two failures must block collection")
        summary = json.loads((out / "summary.json").read_text())
        check([r["status"] for r in summary["rows"]] == ["FAIL", "FAIL", "PASS"],
              "remaining check did not run")
        check(args.github_output.read_text().splitlines() == [
            "failure_logs<<END_LOGS", str(out / "first-test.log"), str(out / "second-test.log"),
            "END_LOGS"], "failure log selection")
        check(all(f"{name} diagnostic" in output.getvalue() for name in ("first", "second")),
              "missing failure tails")
        guest = out / "bundle-ppc-guest-1.json"
        wrong = guest.with_name("bundle-ppc-net-2.json")
        guest.rename(wrong)
        check(collect(plan, args, identity) == 2, "misplaced PPC bundle accepted")
        wrong.rename(guest)
        path = out / "bundle-host.json"
        bundle = json.loads(path.read_text())
        bundle["records"][-1]["status"] = "REUSED"
        path.write_text(json.dumps(bundle))
        check(collect(plan, args, identity) == 2, "REUSED must be rejected")
        path.unlink()
        check(collect(plan, args, identity) == 1, "missing bundle must block collection")
        summary = json.loads((out / "summary.json").read_text())
        check(summary["rows"][0]["status"] == "NOT_RUN", "missing result must be NOT_RUN")
        row, cpu, cfg = plan[0]
        row.update(environment="linux-x86-cross")
        args.bundle = "host"
        (out / "bundle-ppc-net-1.json").unlink()
        (out / "bundle-ppc-guest-1.json").unlink()
        row.update(environment="qemu-ppc-net")
        import socket
        def which(tool):
            return None if Path(tool).name in ("qemu-system-ppc", "powerpc-elf-gcc") else str(tool)
        # Model a complete cross installation on hosts without a PPC toolchain.
        def open_prerequisite(path, mode="r", **kwargs):
            if mode == "rb":
                return io.BytesIO(b"x")
            return open(path, mode, **kwargs)

        with patch.object(Path, "open", open_prerequisite):
            with patch("shutil.which", side_effect=which), patch("socket.socket") as udp:
                try:
                    check_prerequisites([plan[0]], args, identity)
                except OSError as exc:
                    check(all(name in str(exc) for name in ("qemu-system-ppc", "powerpc-elf-gcc")),
                          "missing prerequisites incomplete")
                else:
                    raise Failure("missing prerequisites accepted")
                udp.assert_called_once_with(socket.AF_INET, socket.SOCK_DGRAM)
            with patch("shutil.which", return_value="tool"), patch("socket.socket", side_effect=OSError):
                try:
                    check_prerequisites([plan[0]], args, identity)
                except OSError as exc:
                    check(str(exc) == "loopback UDP bind (AF_INET)", "loopback-only prerequisite")
                else:
                    raise Failure("missing loopback accepted")
                row.update(environment="qemu-ppc")
                check_prerequisites([plan[0]], args, identity)

        row.update(deferred=dict(ledger="fixture-approved", config=cfg, gates=["main"]))
        run_bundle(source, [plan[0]], args, {}, identity)
        check(collect([plan[0]], args, identity) == 0, "approved deferral was rejected")
        del row["deferred"]
        check(collect([plan[0]], args, identity) == 2, "unapproved deferral was accepted")
        row.update(gates=["push"])
        run_bundle(source, [plan[0]], args, {}, identity)
        check(collect([plan[0]], args, identity) == 0, "gate exemption was rejected")
        check(json.loads((out / "summary.json").read_text())["rows"][0]["status"] == "NOT_APPLICABLE",
              "gate exemption missing")

        args.mutate = True
        row.update(environment="python", gates=["main"])
        (source / "flag").write_text("good")
        (bodies / "first.py").write_text(
            "import os, sys\nfrom pathlib import Path\n"
            "if sys.argv[-1] == 'verify':\n"
            "    work = Path(os.environ['OS32_VERIFY_OUT'])\n"
            "    assert work.parent == Path.cwd() and not list(work.iterdir())\n"
            "    flag = Path('flag').read_text()\n"
            "    case = Path('evidence/7450/boot')\n"
            "    case.mkdir(parents=True)\n"
            "    (case / 'serial.log').write_text(flag * 300000)\n"
            "    print(flag + ' diagnostic')\n"
            "    sys.exit(int(flag != 'good'))\n")
        row["mutations"] = [dict(id=value, config=cfg, replace="flag", old="good", new=value,
                                 fail="expected diagnostic") for value in ("expected", "unexpected_failure")]
        run_bundle(source, [plan[0]], args, {}, identity)
        check(collect([plan[0]], args, identity) == 1, "undetected mutation accepted")
        item = json.loads((out / "summary.json").read_text())["rows"][0]
        log = (out / item["log"]).read_text()
        check("expected diagnostic" not in log and "unexpected_failure diagnostic" in log,
              "expected RED forwarded")
        dest = out / "evidence/first/test"
        check({p.name for p in dest.iterdir()} == {"unexpected_failure"}, "failure evidence selection")
        case = dest / "unexpected_failure/7450/boot"
        check((case / "serial.log").stat().st_size == 256 * 1024
              and json.loads((case / "manifest.json").read_text())["truncated_bytes"]["serial.log"] > 0
              and item["mutations"]["unexpected_failure"]["reason"] == "unexpected_failure diagnostic",
              "failure evidence or reason missing")
        import yaml
        workflow = yaml.safe_load(Path(".github/workflows/restructure-check-bundle.yml").read_text())
        uploads = [step["with"]["path"].splitlines() for job in workflow["jobs"].values()
                   for step in job["steps"] if step.get("uses", "").startswith("actions/upload-artifact@")]
        check(["${{ runner.temp }}/results/bundle-*.json", "${{ steps.run_checks.outputs.failure_logs }}",
               "${{ runner.temp }}/results/evidence/"] in uploads,
              "failure evidence upload missing")


if __name__ == "__main__":
    main(lambda config: None, verify)
