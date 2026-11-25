"""Check caller discovery in an isolated tracked tree."""
import argparse
import subprocess
import tempfile
from pathlib import Path
from types import ModuleType

import tool_callers


def exercise(module, root):
    targets = ['tools/unit.py', 'tools/idle.py']
    hits = module.callers(root, targets)
    assert hits[targets[0]] == dict.fromkeys(
        ['Makefile', 'build/nested/test.mk', '.github/workflows/job.yml',
         '.claude/skills/example/SKILL.md', 'tools/checks/sample.py',
         'tools/consumer.py', 'docs/current.md'], 2), hits
    assert hits[targets[1]] == {}, hits


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        subprocess.run(['git', 'init', '-q', temp], check=True)
        files = dict.fromkeys(['Makefile', 'build/nested/test.mk', '.github/workflows/job.yml',
                               '.claude/skills/example/SKILL.md', 'docs/current.md',
                               'docs/archive/old.md'], 'python3 -B tools/unit.py')
        files.update({'Makefile': 'check: tools/unit.py', 'tools/unit.py': '', 'tools/checks/sample.py': "argv = ['python3', 'tools/unit.py']",
                      'tools/consumer.py': 'from unit import main',
                      'docs/archive/idle.md': 'python3 tools/idle.py',
                      'docs/list.md': '| tools/idle.py |',
                      'tools/note.py': '# python3 tools/idle.py'})
        files['tools/idle.py'] = 'python3 tools/idle.py'
        for path, content in files.items():
            dest = root / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text('\n' + content + '\n')
        subprocess.run(['git', 'add', '.'], cwd=root, check=True)
        exercise(tool_callers, root)
        if args.mutate:
            code = Path(tool_callers.__file__).read_text()
            for anchor, replacement in [("':!docs/archive'", "'docs/archive'"),
                                        ('caller == target or ', ''),
                                        ('if make_input or pattern.search(line):', 'if pattern.search(line):'),
                                        ("line.lstrip().startswith('#')", 'False'),
                                        (' + imported + ', " + '(?!)' + ")]:
                assert code.count(anchor) == 1
                mutant = ModuleType('mutant')
                mutant.__file__ = tool_callers.__file__
                exec(compile(code.replace(anchor, replacement), mutant.__file__, 'exec'), mutant.__dict__)
                try:
                    exercise(mutant, root)
                except AssertionError:
                    print('RED', anchor)
                else:
                    raise AssertionError(anchor)
    print('OK: caller evidence and zero-caller candidates')


if __name__ == '__main__':
    main()
