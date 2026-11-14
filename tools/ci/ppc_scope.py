#!/usr/bin/env python3
"""Select PPC checks from the entire branch delta, never push before/after."""
import os
from pathlib import Path
import subprocess

# KB_C, KB_INC and generators in build/ppc-kboot.mk / ram-root.mk.
COMMON = ('kernel/', 'lib/', 'include/', 'net/', 'fs/', 'mm/', 'exec/',
          'gfx/', 'drivers/', 'init/', 'sdk/', 'kapi/', 'userland/shell/',
          'assets/fonts/', 'platform/none/', 'userland/lib/')
PPC = ('arch/ppc/', 'platform/powermac/', 'build/ppc', 'userland/ppc/',
       'tools/ppc/', 'tools/checks/', 'tools/tests/', '.github/actions/ppc-setup/')
EXACT = ('Makefile', 'tools/ci/build_cross_ppc.sh', 'tools/ci/ppc_scope.py',
         '.github/workflows/check.yml')
X86 = ('arch/x86/', 'platform/pc98/')


def decide(paths, ref, arch='auto'):
    if arch == 'x86':
        return 'skip', 'arch=x86 が選ばれたので PPC は回さない'
    if arch in ('ppc', 'both'):
        return 'ppc', 'arch=' + arch
    if arch != 'auto':
        raise ValueError('unknown arch: ' + arch)
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


def main():
    arch = os.environ.get('PPC_ARCH', 'auto') or 'auto'
    ref = os.environ.get('GITHUB_HEAD_REF') or os.environ.get('GITHUB_REF', '')
    paths = changed_paths() if arch == 'auto' else []
    scope, reason = decide([p for p in paths if p], ref, arch)
    print(scope + ': ' + reason.replace('\n', '\\n'))
    with Path(os.environ['GITHUB_OUTPUT']).open('a') as out:
        out.write('scope=' + scope + '\n')


if __name__ == '__main__':
    main()
