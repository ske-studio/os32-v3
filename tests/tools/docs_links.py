from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.documents import graph, report
from tests.support.media import main


def verify(config):
    tracked, anchors, edges = graph()
    dirs = {str(parent) for name in tracked for parent in Path(name).parents}
    errors = []
    for links in edges.values():
        for edge in links:
            if edge.target not in tracked and edge.target not in dirs:
                errors.append(f"{edge.source}:{edge.line} missing target {edge.target}")
            elif edge.fragment and edge.target in anchors and edge.fragment not in anchors[edge.target]:
                errors.append(f"{edge.source}:{edge.line} missing anchor {edge.target}#{edge.fragment}")
    report(errors)


if __name__ == "__main__":
    main(lambda config: None, verify)
