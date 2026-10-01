"""Language rules on clang's physical cursors, types and preprocessing records."""
import ctypes
import functools
from . import cx, K, T, walk, relpath


@functools.lru_cache(maxsize=1)
def anonymous_api():
    # LLVM 18 exposes the C API but not the Python convenience method.
    lib = ctypes.CDLL(cx.conf.get_filename())
    fn = lib.clang_Cursor_isAnonymousRecordDecl
    fn.argtypes, fn.restype = [cx.Cursor], ctypes.c_bool
    return fn


def type_features(typ, seen=None):
    seen = set() if seen is None else seen
    typ = typ.get_canonical()
    key = typ.spelling
    if key in seen:
        return set()
    seen.add(key)
    result = set()
    if typ.is_restrict_qualified():
        result.add('restrict')
    if typ.kind == T.ATOMIC:
        result.add('_Atomic')
    if typ.kind == T.VARIABLEARRAY:
        result.add('VLA')
    if typ.kind == T.POINTER:
        result |= type_features(typ.get_pointee(),seen)
    if typ.kind in (T.CONSTANTARRAY,T.INCOMPLETEARRAY,T.VARIABLEARRAY):
        result |= type_features(typ.element_type,seen)
    if typ.kind in (T.FUNCTIONPROTO,T.FUNCTIONNOPROTO):
        result |= type_features(typ.get_result(),seen)
        if typ.kind == T.FUNCTIONPROTO:
            for arg in typ.argument_types():
                result |= type_features(arg,seen)
    return result


def runtime_sizeof(c):
    # In valid C sizeof(type) is nonconstant only for a variably sized type.
    if c.kind != K.CXX_UNARY_EXPR:
        return False
    evaluate = cx.conf.lib.clang_Cursor_Evaluate
    evaluate.argtypes, evaluate.restype = [cx.Cursor], ctypes.c_void_p
    def nonconstant(cursor):
        result = evaluate(cursor)
        if not result:
            return True
        dispose = cx.conf.lib.clang_EvalResult_dispose
        dispose.argtypes = [ctypes.c_void_p]
        dispose(result)
        return False
    if nonconstant(c):
        return True
    # sizeof(pointer-to-VLA) and _Alignof(VLA) are themselves constant.
    # Type operands expose their array dimensions as direct expression children;
    # ordinary sizeof(expr) instead has a PAREN_EXPR child.
    return any(x.kind.is_expression() and x.kind != K.PAREN_EXPR and nonconstant(x)
               for x in c.get_children())


def findings(tu, root, words, headers, public=False):
    from .type_occurrences import findings as type_occurrences
    hits = {hit for hit in type_occurrences(tu, root) if hit[2] in words}
    nodes = list(walk(tu.cursor))
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
    for c,_ in nodes:
        rel = relpath(c,root)
        if not rel:
            continue
        features = type_features(c.type)
        if runtime_sizeof(c):
            features.add('VLA')
        if c.kind == K.FUNCTION_DECL:
            features |= type_features(c.type.get_result())
        if c.kind == K.VAR_DECL and c.tls_kind.value:
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
        if c.kind in (K.STRUCT_DECL,K.UNION_DECL) and anonymous_api()(c) and c.semantic_parent and \
                c.semantic_parent.kind in (K.STRUCT_DECL,K.UNION_DECL):
            fields = tuple(x.spelling for x in c.get_children() if x.kind == K.FIELD_DECL)
            # Pre-existing HostDrv wire-layout members: preserve ABI, reject new shapes.
            inherited = rel == 'fs/hostdrvfs_proto.h' and fields in (
                ('ReplaceIfExists','AdvanceOnly'), ('ClusterCount','DeleteHandle'))
            if not inherited:
                features.add('anonymous record')
        if c.kind == K.FUNCTION_DECL and c.is_definition() and c.type.kind == T.FUNCTIONNOPROTO:
            features.add('old-style definition')
        for feature in features:
            if feature in words or feature in ('VLA','anonymous record','old-style definition'):
                hits.add((rel,c.extent.start.line,feature))
    return sorted(hits)
