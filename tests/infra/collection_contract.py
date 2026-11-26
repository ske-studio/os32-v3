"""Exercise real runner, independent failures, and incomplete or invalid bundles."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.infra.ci import collect, run_bundle
from tests.support.media import Failure, main


def check(value, why):
    if not value:
        raise Failure(why)


def verify(config):
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
            (bodies / f"{name}.py").write_text(f'print("{name} diagnostic")\nraise SystemExit({rc})\n')
            plan.append((dict(id=name, subsystem="infra", gates=["main"], mutations=[],
                              environment="python", sources=[]), cpu, "test"))
        identity = dict(sha="fixture", run="local", attempt="1", mutate=False, gate="main")
        for args.bundle in ("host", "ppc"):
            run_bundle(source, [r for r in plan if r[1] == args.bundle], args, {}, identity)
        output = io.StringIO()
        with redirect_stdout(output):
            check(collect(plan, args, identity) == 1, "two failures must block collection")
        summary = json.loads((out / "summary.json").read_text())
        check([r["status"] for r in summary["rows"]] == ["FAIL", "FAIL", "PASS"],
              "remaining check did not run")
        check(all(f"{name} diagnostic" in output.getvalue() for name in ("first", "second")),
              "missing failure tails")
        path = out / "bundle-host.json"
        bundle = json.loads(path.read_text())
        bundle["records"][-1]["status"] = "REUSED"
        path.write_text(json.dumps(bundle))
        check(collect(plan, args, identity) == 2, "REUSED must be rejected")
        path.unlink()
        check(collect(plan, args, identity) == 1, "missing bundle must block collection")
        summary = json.loads((out / "summary.json").read_text())
        check(summary["rows"][-1]["status"] == "NOT_RUN", "missing result must be NOT_RUN")
        row, cpu, cfg = plan[0]
        row.update(environment="linux-x86-cross")
        from unittest.mock import patch
        with patch("shutil.which", return_value=None):
            args.bundle = "host"
            (out / "bundle-ppc.json").unlink()
            run_bundle(source, [plan[0]], args, {}, identity)
        check(collect([plan[0]], args, identity) == 2, "missing tools must be ERROR")

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


if __name__ == "__main__":
    main(lambda config: None, verify)
