import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.documents import documents, report
from tests.support.tracked import paths
from tests.support.media import main


def verify(config):
    policy = Path("docs/POLICY_DEV.md").read_text().split("<!-- status-vocab:begin -->")[1].split("<!-- status-vocab:end -->")[0]
    vocabulary = {line.split("|")[1].strip() for line in policy.splitlines() if line.startswith("| ")} - {"語"}
    exceptions = json.loads(Path("tools/docs_status_exceptions.json").read_text())
    active = {"計画", "設計中", "実装中", "受入待ち", "受入完了・実機確認待ち"}
    errors = []
    for name in documents(paths(Path.cwd())):
        if not name.startswith("docs/tasks/"):
            continue
        p = Path(name)
        lines = p.read_text().splitlines()[:20]
        required = any(p.match(g) for g in ("TASK_*.md", "HANDOVER_*.md", "CHECKLIST_*.md", "*PLAN*.md"))
        status = [(n, line) for n, line in enumerate(lines[:12], 1) if
                  re.match(r"^>\s*状態[:：]", line) or
                  (required and line.startswith(">") and re.search(r" / 状態[:：]", line))]
        if not status and not required:
            continue
        if len(status) != 1:
            errors.append(f"{name} expected one status line, found {len(status)}")
            continue
        number, line = status[0]
        match = re.search(r"状態[:：]\s*\*\*([^*（(]+)", line)
        word = match[1].strip() if match else None
        if word not in vocabulary:
            errors.append(f"{name}:{number} invalid status {word}")
        if word in active and p.match("TASK_*.md") and name not in exceptions["no_criteria"]:
            if not any(re.match(r"^>\s*完了の基準[:：]\s*\S", line) for line in lines):
                errors.append(f"{name} missing completion criteria")
        if word in {"受入完了", "完了記録", "撤回"} and not exceptions["done_in_place"].get(name):
            errors.append(f"{name} terminal status in current tasks: {word}")
    report(errors)


if __name__ == "__main__":
    main(lambda config: None, verify)
