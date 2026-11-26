"""Exercise the U18 boot-record rules in ci_v2.md §0 and §4."""
import argparse
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import types
from unittest.mock import patch
import zipfile

TOOL = Path(__file__).resolve().parents[1] / 'ci/land_gate.py'


def exercise(source, root):
    gate = types.ModuleType('land_gate')
    gate.__file__ = str(TOOL)
    exec(compile(source, str(TOOL), 'exec'), gate.__dict__)
    old = dict(id=10, run_attempt=1, head_sha='a' * 40, head_branch='wt/base',
               head_repository={'full_name': 'repo'}, workflow_id=7, event='push',
               status='completed', conclusion='success')
    new = dict(old, id=20, run_attempt=2, head_sha='b' * 40, head_branch='wt/docs')
    media = root / 'accepted.iso'
    media.write_bytes(b'accepted')
    log = root / 'boot.log'
    log.write_text('shell')
    record = dict(machine='powermac', image='core', filename='boot.iso', sha=old['head_sha'],
                  run_id=10, attempt=1, artifact_id=101, sha256=gate.digest(media),
                  media=media, result='success', reached='shell', log=log)

    def case(label, allowed, paths=b'README.md\0', before=b'accepted', after=b'accepted', **fields):
        blobs = {}

        def artifact(run, ident, payload):
            out = io.BytesIO()
            with zipfile.ZipFile(out, 'w') as bundle:
                bundle.writestr('boot.iso', payload)
            blobs[f'api/artifacts/{ident}/zip'] = out.getvalue()
            return dict(id=ident, name='ppc-core-' + run['head_sha'], expired=False,
                        digest='sha256:' + hashlib.sha256(out.getvalue()).hexdigest())

        previous = artifact(old, 101, before)
        candidate = artifact(new, 202, after)

        def command(*args):
            if args[:2] == ('git', 'show'):
                revision, path = args[2].split(':', 1)
                assert revision == new['head_sha']
                return (TOOL.parents[2] / path).read_bytes()
            if args[:2] == ('git', 'diff'):
                assert args[2:] == ('--no-renames', '--name-only', '-z', old['head_sha'], new['head_sha'], '--')
                return paths
            if args[:2] == ('git', 'merge-base'):
                return b''
            return blobs[args[2]]

        boot = dict(record, **fields)
        with patch.object(gate, 'api', return_value=old), patch.object(gate, 'pages', return_value=[previous]), patch.object(gate, 'command', command):
            try:
                gate.verify_boot('api', 'repo', 7, new, [candidate], boot)
            except ValueError:
                assert not allowed, label
            else:
                assert allowed, label

    case('same medium after docs edit', True, paths=b'docs/images/demo_0532.png\0')
    case('listed markdown', True, paths=b'docs/CONSTRAINTS.md\0')
    case('test instructions', False, paths=b'docs/CONSTRAINTS.md\0tools/ops/gf_a4.md\0')
    case('unlisted markdown', False, paths=b'docs/new-file.md\0')
    case('packaged manual', False, paths=b'docs/manpages/hsync.1\0')
    case('generator input', False, paths=b'docs/design/hwdev/hwdev_imports.md\0')
    case('code change', False, paths=b'README.md\0sys/boot.c\0')
    case('candidate medium changed', False, after=b'changed')
    case('prior artifact differs', False, before=b'changed')
    case('wrong previous attempt', False, attempt=2)
    case('wrong previous artifact', False, artifact_id=202)
    case('fresh record', True, sha=new['head_sha'], run_id=20, attempt=2, artifact_id=202)
    case('fresh record wrong attempt', False, sha=new['head_sha'], run_id=20, artifact_id=202)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = TOOL.read_text()
    with tempfile.TemporaryDirectory() as temp:
        exercise(source, Path(temp))
        if args.mutate:
            for anchor, replacement in [
                    ('if previous != sha:', "if previous != sha: raise ValueError('fresh record required')\n    if previous != sha:"),
                    ("if row['所属種類'] == '文書'", 'if True'),
                    ('if not paths <= docs:', 'if False:'),
                    ('image_inputs.extend(', '[].extend('),
                    ("['docs/design/hwdev/hwdev_imports.md']", '[]'),
                    ("medium_digest(base, prior, prior_artifacts, boot, artifact_id) != boot['sha256']", 'False'),
                    ("medium_digest(base, run, artifacts, boot, artifact_id) != boot['sha256']", 'False'),
                    ("prior['run_attempt'] != attempt", 'False')]:
                assert source.count(anchor) == 1
                try:
                    exercise(source.replace(anchor, replacement), Path(temp))
                except AssertionError:
                    print('runtime RED', anchor)
                else:
                    sys.exit('survived: ' + anchor)
    print('PASS boot inheritance')
