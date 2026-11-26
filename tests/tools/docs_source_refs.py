from pathlib import Path
import posixpath
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.documents import documents, report
from tests.support.tracked import paths
from tests.support.media import main


REFERENCE = re.compile(r"(?<![\w/])([.\w/-]+\.(?:c|h|S|s|inc|asm|rs|py|sh|mk|ld))(?::\d+|#L\d+)")


# A versioned external source immediately introducing a code span identifies
# that span even if the upstream repository has a path shared with OS32.
EXTERNAL_SOURCE = re.compile(r"\b[A-Za-z][\w.+-]*[ \t]+(?:releng/|v|release[-/ ])\d+(?:[.\w/-]*)[ \t]+`$")


def verify(config):
    tracked = paths(Path.cwd())
    errors = []
    for name in documents(tracked):
        for line, body in enumerate(Path(name).read_text().splitlines(), 1):
            for match in REFERENCE.finditer(body):
                if EXTERNAL_SOURCE.search(body[:match.start()]):
                    continue
                target = match[1]
                relative = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
                if target in tracked or relative in tracked:
                    errors.append(f"{name}:{line} source line reference {match[0]}")
    report(errors)


if __name__ == "__main__":
    main(lambda config: None, verify)
