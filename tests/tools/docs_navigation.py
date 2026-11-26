from pathlib import Path
import sys
import re

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.documents import graph, report
from tests.support.media import main


def verify(config):
    _, anchors, edges = graph()
    errors = []
    pending = ["docs/INDEX.md"]
    reached = set()
    while pending:
        src = pending.pop()
        if src in reached:
            continue
        reached.add(src)
        pending.extend(e.target for e in edges.get(src, ()) if e.target in anchors)
    def front(name):
        return "\n".join(Path(name).read_text().splitlines()[:12])
    indexes = {name for name in anchors if Path(name).stem in ("INDEX", "README", "DEVELOPMENT") or
               re.search(r"^> 索引[:：]", front(name), re.M)}
    tickets = {name for name in anchors if name.startswith("docs/tasks/") and
               not re.search(r"^(?:> )?状態[:：]\s*\*\*現行", front(name), re.M)}
    for name in anchors:
        if name.startswith("docs/") and name not in reached:
            errors.append(f"unreachable {name}")
        for edge in edges[name]:
            history = edge.target.startswith("docs/archive/")
            basis = (name.startswith(("docs/design/", "docs/decisions/")) or
                     (Path(name).parent == Path("docs") and not
                      re.search(r"^# .*?(リリースノート|変更履歴|事例集)", front(name), re.M)))
            if history or (basis and name not in indexes and not edge.navigation and edge.target in tickets):
                errors.append(f"{name}:{edge.line} forbidden reference {edge.target}")
    report(errors)


if __name__ == "__main__":
    main(lambda config: None, verify)
