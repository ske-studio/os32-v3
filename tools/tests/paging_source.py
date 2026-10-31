"""Read paging's same-translation-unit fragments for existing source fixtures.

Expand only paging includes, at their original positions and before host
substitutions or mutations. A copied tree always supplies its own fragments.
"""
import re


def paging_source(path):
    source = path.read_text(encoding='utf-8')
    if path.name == 'paging.c':
        source = re.sub(
            r'^#include "(paging_\w+\.inc)"\n',
            lambda match: (path.parent / match[1]).read_text(encoding='utf-8'),
            source, flags=re.M)
    return source
