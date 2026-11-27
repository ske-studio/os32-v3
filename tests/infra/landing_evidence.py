"""Exercise the integration verdict with synthetic API replies."""
import hashlib
import io
import json
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import Failure, main
from tools.ci import land_gate as gate


def selection_artifact(run):
    previous = "b" * 40
    rows = '[[check]]\nid = "example"\ngates = ["main"]\nmutations = []'
    expected = dict(pc98="none", powerpc=["core"], entries=[["example", "core"]],
                    skipped={"other/local": "outside changed ownership and registered inputs"})
    plan = dict(base=previous, selection=expected.copy(), entries=[dict(id="example", config="core")],
                identity=dict(sha=run["head_sha"], kapi_base=previous, run="19", attempt="2",
                              gate="main", mutate=True))

    summary = dict(rc=0, errors=[], selection=expected, identity=plan["identity"],
                   rows=[dict(id="example", config="core", status="PASS", reason="", mutations={})])

    def read_plan():
        commands = {("git", "rev-parse", "origin/main"): previous.encode(),
                    ("git", "merge-base", "--is-ancestor", previous, run["head_sha"]): b"",
                    ("git", "show", run["head_sha"] + ":tests/registry.toml"): rows.encode()}
        artifacts = []
        for ident, name, filename, payload in (
                (42, "plan-2", "plan.json", plan),
                (43, f"verified-{run['head_sha']}-a2", "summary.json", summary)):
            contents = io.BytesIO()
            with zipfile.ZipFile(contents, "w") as archive:
                archive.writestr(filename, json.dumps(payload))
            data = contents.getvalue()
            artifacts.append(dict(name=name, expired=False, id=ident,
                                  digest="sha256:" + hashlib.sha256(data).hexdigest()))
            commands["gh", "api", f"/artifacts/{ident}/zip"] = data
        with patch.object(gate, "command", side_effect=lambda *args: commands[args]), \
             patch.object(gate, "comparison", return_value=expected):
            return gate.planned_boots("", run, artifacts)

    if read_plan() != ({"powermac"}, "none"):
        raise Failure("wrong boot evidence for a PowerMac-only plan")
    plan["entries"][0]["bundle"] = "ppc-guest-1"
    expected["entries"].append(["example", "link"])
    plan["entries"].append(dict(id="example", config="link", bundle="ppc-net-1"))
    summary["rows"].append(dict(summary["rows"][0], config="link"))
    if read_plan() != ({"powermac"}, "none"):
        raise Failure("split PowerPC plan rejected")
    with patch.dict(summary, rows=summary["rows"][:1]):
        try:
            read_plan()
        except ValueError:
            pass
        else:
            raise Failure("missing second PPC bundle accepted")
    for field, changed in (("base", run["head_sha"]), ("entries", []),
                           ("selection", dict(expected, skipped={})),
                           ("identity", dict(plan["identity"], attempt="1"))):
        with patch.dict(plan, {field: changed}):
            try:
                read_plan()
            except ValueError:
                continue
            raise Failure(f"invalid selection {field} accepted")
    for field, changed in (("rc", 1), ("rows", []), ("selection", {}),
                           ("rows", [dict(summary["rows"][0], status="FAIL"), summary["rows"][1]]),
                           ("rows", [dict(summary["rows"][0], mutations={"extra": {"result": "RED"}}), summary["rows"][1]]),
                           ("identity", dict(summary["identity"], run="18")),
                           ("identity", dict(summary["identity"], attempt="1"))):
        with patch.dict(summary, {field: changed}):
            try:
                read_plan()
            except ValueError:
                continue
            raise Failure(f"invalid summary {field} accepted")


def verify(config):
    sha = "a" * 40
    repo = "owner/project"
    run = dict(id=19, run_attempt=2, head_sha=sha, event="push", workflow_id=3,
               head_branch="wt/int-example", head_repository={"full_name": repo},
               status="completed", conclusion="success")

    selection_artifact(run)
    evidence = dict(sha=sha, run_id=19, attempt=2,
                    boots=[dict(machine=m, sha=sha) for m in ("pc98", "powermac")])

    boot = dict(machine="pc98", sha=sha, run_id=19, attempt=2, artifact_id=1,
                sha256="digest", result="success", reached="shell", log=__file__, media=__file__)
    with patch.object(gate, "medium_digest", return_value="digest"), patch.object(gate, "digest", return_value="digest"):
        gate.verify_boot("", run, [], "distribution", dict(boot, selftest_fail=0))
        gate.verify_boot("", run, [], "distribution", dict(boot, machine="powermac"))
        fd = dict(boot, boot_kind="fd", filename="os32_boot.d88", ver_commit=sha[:7],
                  ver_image_crc="12ab3456", ver_loader="FD")
        gate.verify_boot("", run, [], "fd", fd)
        for plan, record, message in (
                ("distribution", dict(fd, selftest_fail=0), "FD accepted for distribution"),
                ("fd", dict(boot, selftest_fail=0), "HDD accepted for FD plan"),
                ("fd", dict(fd, ver_commit="b" * 7), "wrong FD Commit accepted"),
                ("fd", dict(fd, ver_commit=""), "missing FD Commit accepted"),
                ("fd", dict(fd, ver_image_crc=""), "missing FD Image CRC accepted"),
                ("fd", dict(fd, ver_loader="HDD"), "wrong FD loader accepted")):
            try:
                gate.verify_boot("", run, [], plan, record)
            except ValueError:
                continue
            raise Failure(message)
        for extra in ({}, {"selftest_fail": 1}, {"selftest_fail": False}, {"selftest_fail": "0"}):
            try:
                gate.verify_boot("", run, [], "distribution", dict(boot, **extra))
            except ValueError as exc:
                if "selftest_fail" in str(exc):
                    continue
                raise
            raise Failure("invalid PC-98 selftest count accepted")

    with tempfile.TemporaryDirectory() as tmp:
        fake = Path(tmp) / "gh"
        fake.write_text("""#!/bin/bash
case "$1 $2" in
    'auth status') exit 0 ;;
    'run list') echo '[{"databaseId":19,"headSha":"%s","headBranch":"wt/int-example","createdAt":"now"}]' ;;
    'api --paginate') echo "$NAMES" ;;
    'api repos/'*) echo '{"run_attempt":2,"status":"completed"}' ;;
    *) exit 2 ;;
esac
""" % sha)
        fake.chmod(0o755)
        product = f"os32-wt-int-example-{sha[:7]}-a2"
        for names, options, rc, state in (
            ([product], [], 1, "BUILT"),
            ([product], ["--allow-built"], 0, "BUILT"),
            ([product, f"verified-{sha}-a1"], [], 1, "BUILT"),
            ([product, f"verified-{sha}-a2"], [], 0, "VERIFIED"),
        ):
            result = subprocess.run(["bash", ROOT / "tools/ci_fetch.sh", "--dry-run", *options],
                env=dict(os.environ, PATH=tmp + os.pathsep + os.environ["PATH"], NAMES="\n".join(names)),
                capture_output=True, text=True)
            if result.returncode != rc or state not in result.stdout + result.stderr:
                raise Failure(f"incorrect fetch policy for {state}: {result.stdout}{result.stderr}")

    def api(url):
        return {f"repos/{repo}/actions/workflows/restructure-build.yml": {"id": 3},
                f"repos/{repo}/actions/runs/19": run}[url]

    artifacts = [dict(name=f"verified-{sha}-a2", expired=False)]

    def pages(url, field, query):
        if (url, field, query) != (f"repos/{repo}/actions/runs/19/artifacts", "artifacts", {}):
            raise Failure("unexpected evidence query")
        return artifacts

    with patch.object(gate, "api", side_effect=api), patch.object(gate, "pages", side_effect=pages), \
         patch.object(gate, "verify_boot") as verify_boot:
        try:
            gate.verify(repo, sha, evidence)
        except ValueError as exc:
            if "selection plan" not in str(exc):
                raise
        else:
            raise Failure("missing selection plan accepted")
        with patch.object(gate, "planned_boots", return_value=({"pc98", "powermac"}, "distribution")):
            verify_verdict(gate, repo, sha, evidence, run, artifacts, verify_boot)


def verify_verdict(gate, repo, sha, evidence, run, artifacts, verify_boot):
    if gate.verify(repo, sha, evidence):
        raise Failure("complete integration evidence rejected")
    if [call.args[-1] for call in verify_boot.call_args_list] != evidence["boots"]:
        raise Failure("boot evidence unchecked")
    for field, value in (("run_attempt", 3), ("id", 20), ("head_sha", "b" * 40),
                         ("status", "in_progress"), ("conclusion", "failure")):
        with patch.dict(run, {field: value}):
            try:
                gate.verify(repo, sha, evidence)
            except ValueError:
                continue
            raise Failure(f"invalid {field} accepted")
    marker = artifacts.pop()
    try:
        gate.verify(repo, sha, evidence)
    except ValueError as exc:
        if "VERIFIED required" not in str(exc):
            raise
    else:
        raise Failure("BUILT accepted for landing")
    artifacts.append(marker)
    evidence["boots"].pop()
    if not gate.verify(repo, sha, evidence):
        raise Failure("missing PowerMac boot accepted")


if __name__ == "__main__":
    main(lambda config: None, verify)
