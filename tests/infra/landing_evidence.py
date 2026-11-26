"""Exercise the integration verdict with a candidate workflow and synthetic API replies."""
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

    def git(program, action, path):
        if (program, action) != ("git", "show") or not path.startswith(sha + ":"):
            raise ValueError("unexpected command")
        return (ROOT / path.split(":", 1)[1]).read_bytes()

    with patch.object(gate, "command", side_effect=git):
        expected = gate.expected_jobs(sha, "restructure-build.yml")
        jobs = [dict(name=name, run_id=19, run_attempt=2, status="completed", conclusion="success")
                for name in expected]
        evidence = dict(sha=sha, run_id=19, attempt=2,
                        boots=[dict(machine=m, sha=sha) for m in ("pc98", "powermac")])
        with patch.object(gate, "api", side_effect=lambda url: run if url.endswith("/runs/19") else {"id": 3}), \
             patch.object(gate, "pages", side_effect=lambda url, field, query: jobs if field == "jobs" else []), \
             patch.object(gate, "verify_boot"):
            if gate.verify(repo, sha, evidence):
                raise Failure("complete integration evidence rejected")
            for invalid in ("failure", "skipped"):
                collection = next(j for j in jobs if j["name"] == "collect")
                collection["conclusion"] = invalid
                if not gate.verify(repo, sha, evidence):
                    raise Failure("unsuccessful collection accepted")
                collection["conclusion"] = "success"
            collection["run_attempt"] = 1
            try:
                gate.verify(repo, sha, evidence)
            except ValueError:
                pass
            else:
                raise Failure("mixed attempts accepted")
            collection["run_attempt"] = 2
            evidence["boots"].pop()
            if not gate.verify(repo, sha, evidence):
                raise Failure("missing PowerMac boot accepted")


if __name__ == "__main__":
    main(lambda config: None, verify)
