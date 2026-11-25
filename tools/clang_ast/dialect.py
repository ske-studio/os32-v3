"""Language rules on clang's physical cursors, types and preprocessing records."""
import ctypes
import functools
from . import cx, K, ParseError, relpath
from .type_occurrences import visitor_binary


# Match the feature bits in dialect_candidates.c.
_RESTRICT, _ATOMIC, _VLA, _ANONYMOUS, _TLS, _OLD_STYLE = (1, 2, 4, 8, 16, 32)
_TYPE_WORDS = ((_RESTRICT, 'restrict'), (_ATOMIC, '_Atomic'), (_VLA, 'VLA'))


# CXCursor contains pointers into its translation unit. Keep the unit alive
# exactly as clang.cindex.Cursor.get_children does.
_Report = ctypes.CFUNCTYPE(None, cx.Cursor, ctypes.c_uint)


@functools.lru_cache(maxsize=1)
def candidate_visitor():
    try:
        library = ctypes.CDLL(str(visitor_binary('dialect_candidates.c')))
        visit = library.dialect_candidates
    except (OSError, AttributeError) as error:
        raise ParseError('Clang cursor visitor unavailable: ' + str(error)) from error
    visit.argtypes = [cx.TranslationUnit, ctypes.c_int, _Report]
    visit.restype = None
    return visit


def candidates(tu, public):
    nodes = []

    @_Report
    def collect(cursor, mask):
        cursor._tu = tu
        nodes.append((cursor, mask))

    candidate_visitor()(tu, public, collect)
    return nodes


def findings(tu, root, words, headers, public=False):
    from .type_occurrences import findings as type_occurrences
    hits = {hit for hit in type_occurrences(tu, root) if hit[2] in words}
    nodes = candidates(tu, public)
    for d in tu.diagnostics:
        folding_assert = d.option == '-Wgnu-folding-constant' and any(
            c.kind == K.STATIC_ASSERT and str(c.location.file) == str(d.location.file) and
            c.extent.start.offset <= d.location.offset <= c.extent.end.offset for c,_ in nodes)
        if (d.option == '-Wdeprecated-non-prototype' or folding_assert) and d.location.file:
            import os
            import pathlib
            try:
                rel = pathlib.Path(os.path.abspath(d.location.file.name)).relative_to(root).as_posix()
                hits.add((rel,d.location.line,'nonconstant static assert' if folding_assert else 'old-style definition'))
            except ValueError:
                pass
    for c, mask in nodes:
        rel = relpath(c,root)
        if not rel:
            continue
        features = {word for bit, word in _TYPE_WORDS if mask & bit}
        if mask & _TLS:
            # Use tokens only to distinguish the two spellings in diagnostics.
            features.add('__thread' if any(t.spelling == '__thread' for t in c.get_tokens())
                         else '_Thread_local')
        if c.kind == K.INCLUSION_DIRECTIVE:
            basename = c.spelling.rsplit('/',1)[-1]
            if basename in headers:
                hits.add((rel,c.location.line,basename))
        if public and c.kind == K.MACRO_DEFINITION:
            # Public replacement lists are part of the SDK even before expansion.
            for t in c.get_tokens():
                if t.spelling in words:
                    hits.add((rel,c.location.line,t.spelling))
        if public:
            features &= words
        if mask & _ANONYMOUS and c.semantic_parent and \
                c.semantic_parent.kind in (K.STRUCT_DECL,K.UNION_DECL):
            fields = tuple(x.spelling for x in c.get_children() if x.kind == K.FIELD_DECL)
            # Pre-existing HostDrv wire-layout members: preserve ABI, reject new shapes.
            inherited = rel == 'sys/dev/hostdrv/hostdrvfs_proto.h' and fields in (
                ('ReplaceIfExists','AdvanceOnly'), ('ClusterCount','DeleteHandle'))
            if not inherited:
                features.add('anonymous record')
        if mask & _OLD_STYLE:
            features.add('old-style definition')
        for feature in features:
            if feature in words or feature in ('VLA','anonymous record','old-style definition'):
                hits.add((rel,c.extent.start.line,feature))
    return sorted(hits)
