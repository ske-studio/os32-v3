"""Read a split source with its same-translation-unit fragments expanded.

S 群 (TASK_REDUNDANCY) は大きい .c の関数を同じディレクトリの `<stem>_<group>.inc`
へ移し、元の場所で include する。ソースを文字で読む既存の試験はこれで元の 1 枚に戻して読む。
展開するのは `<stem>_*.inc` の include だけで、ほかの include はそのまま残す。
"""
import re


def paging_source(path):
    source = path.read_text(encoding='utf-8')
    if path.suffix != '.c':
        return source
    stem = re.escape(path.stem)
    return re.sub(r'^#include "(' + stem + r'_\w+\.inc)"\n',
                  lambda match: (path.parent / match[1]).read_text(encoding='utf-8'),
                  source, flags=re.M)


split_source = paging_source
