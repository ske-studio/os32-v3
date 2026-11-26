"""Exercise the integration verdict with synthetic API replies."""
from pathlib import Path
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.support.media import Failure, main
from tools.ci import land_gate as gate


def verify(config):
    sha = "a" * 40
    repo = "owner/project"
    run = dict(id=19, run_attempt=2, head_sha=sha, event="push", workflow_id=3,
               head_branch="wt/int-example", head_repository={"full_name": repo},
               status="completed", conclusion="success")

    evidence = dict(sha=sha, run_id=19, attempt=2,
                    boots=[dict(machine=m, sha=sha) for m in ("pc98", "powermac")])

    boot = dict(machine="pc98", sha=sha, run_id=19, attempt=2, artifact_id=1,
                sha256="digest", result="success", reached="shell", log=__file__, media=__file__)
    with patch.object(gate, "medium_digest", return_value="digest"), patch.object(gate, "digest", return_value="digest"):
        gate.verify_boot("", repo, 3, run, [], dict(boot, selftest_fail=0))
        gate.verify_boot("", repo, 3, run, [], dict(boot, machine="powermac"))
        for extra in ({}, {"selftest_fail": 1}, {"selftest_fail": False}, {"selftest_fail": "0"}):
            try:
                gate.verify_boot("", repo, 3, run, [], dict(boot, **extra))
            except ValueError as exc:
                if "selftest_fail" in str(exc):
                    continue
                raise
            raise Failure("invalid PC-98 selftest count accepted")

    def api(url):
        return {f"repos/{repo}/actions/workflows/restructure-build.yml": {"id": 3},
                f"repos/{repo}/actions/runs/19": run}[url]

    def pages(url, field, query):
        if (url, field, query) != (f"repos/{repo}/actions/runs/19/artifacts", "artifacts", {}):
            raise Failure("unexpected evidence query")
        return []

    with patch.object(gate, "api", side_effect=api), patch.object(gate, "pages", side_effect=pages), \
         patch.object(gate, "verify_boot") as verify_boot:
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
        evidence["boots"].pop()
        if not gate.verify(repo, sha, evidence):
            raise Failure("missing PowerMac boot accepted")


if __name__ == "__main__":
    main(lambda config: None, verify)
