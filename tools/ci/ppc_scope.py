#!/usr/bin/env python3
"""Select PPC checks from the main push delta or the entire feature branch delta."""
import fnmatch
import os
import re
from pathlib import Path
import subprocess

# KB_C, KB_INC and generators in build/targets/ppc-powermac.mk / ram-root.mk; the owner sets in build/select.mk.
COMMON = ('kernel/', 'lib/', 'include/', 'net/', 'fs/', 'mm/', 'exec/',
          'gfx/', 'drivers/', 'init/', 'sdk/', 'kapi/', 'userland/shell/',
          'assets/fonts/', 'platform/none/', 'userland/lib/')
PPC = ('arch/ppc/', 'platform/powermac/', 'build/ppc', 'build/targets/ppc-', 'build/images/',
       'build/select.mk', 'build/sources/', 'build/arch/ppc', 'build/platform/powermac', 'userland/ppc/',
       'tools/ppc/', 'tools/checks/', 'tools/tests/', '.github/actions/ppc-setup/')
EXACT = ('Makefile', 'tools/ci/build_cross_ppc.sh', 'tools/ci/ppc_scope.py',
         '.github/workflows/check.yml')
X86 = ('arch/x86/', 'platform/pc98/')


DOC_PATTERNS = ('docs/**', '**.md', '.claude/**')


def decide(paths, ref, arch='auto', before_paths=None):
    if arch == 'x86':
        return 'skip', 'arch=x86 が選ばれたので PPC は回さない'
    if arch in ('ppc', 'both'):
        return 'ppc', 'arch=' + arch
    if arch != 'auto':
        raise ValueError('unknown arch: ' + arch)
    if ref == 'refs/heads/main':
        if before_paths is None:
            return 'ppc', 'main: before unavailable'
        if all(any(fnmatch.fnmatchcase(p, pattern) for pattern in DOC_PATTERNS) for p in before_paths):
            return 'skip', 'main: documentation only'
        return 'ppc', 'main: code changed'
    if ref.removeprefix('refs/heads/').startswith('wt/int-'):
        return 'ppc', 'integration branch: ' + ref
    for path in paths:
        if path.startswith('docs/') or path.endswith('.md'):
            continue
        if (path.startswith(COMMON + PPC) or path in EXACT
                or (path.startswith('build/') and path.endswith('.mk'))):
            return 'ppc', path
        if path.startswith(X86):
            continue
        # Anything not known to be x86-only or documentation may affect PPC.
        return 'ppc', 'conservative: ' + path
    return 'skip', 'x86-only, documentation-only, or empty branch delta'


def changed_paths():
    base = subprocess.check_output(
        ['git', 'merge-base', 'origin/main', 'HEAD'], text=True).strip()
    return subprocess.check_output(
        ['git', 'diff', '--name-only', '--no-renames', '-z', base, 'HEAD']
    ).decode().rstrip('\0').split('\0') if base else []


def push_paths(before):
    if not re.fullmatch('[0-9a-f]{40}', before) or before == '0' * 40:
        return None
    if subprocess.run(['git', 'merge-base', '--is-ancestor', before, 'HEAD']).returncode:
        return None
    try:
        names = subprocess.check_output(['git', 'diff', '--name-only', '--no-renames', '-z', before, 'HEAD'])
    except subprocess.CalledProcessError:
        return None
    return os.fsdecode(names).split('\0')[:-1]


def main():
    arch = os.environ.get('PPC_ARCH', 'auto') or 'auto'
    ref = os.environ.get('GITHUB_HEAD_REF') or os.environ.get('GITHUB_REF', '')
    main_push = (ref == 'refs/heads/main' and os.environ.get('GITHUB_EVENT_NAME') == 'push'
                 and 'PUSH_BEFORE' in os.environ)
    before_paths = push_paths(os.environ['PUSH_BEFORE']) if main_push and os.environ.get('PUSH_FORCED') != 'true' else None
    paths = changed_paths() if arch == 'auto' and not main_push else []
    scope_ref = 'main' if ref == 'refs/heads/main' and not main_push else ref
    scope, reason = decide([p for p in paths if p], scope_ref, arch, before_paths)
    print(scope + ': ' + reason.replace('\n', '\\n'))
    with Path(os.environ['GITHUB_OUTPUT']).open('a') as out:
        out.write('can_reuse=' + ('true' if before_paths is not None and scope == 'ppc' else 'false') + '\n')
        out.write('scope=' + scope + '\n')


if __name__ == '__main__':
    main()
