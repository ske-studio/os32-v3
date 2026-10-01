"""Read expanded assembly templates through libclang, including older CI APIs."""
import ast as literal
import ctypes
import functools
from . import cx, K, ParseError, walk


@functools.lru_cache(maxsize=1)
def api():
    lib = ctypes.CDLL(cx.conf.get_filename())
    def bind(name, args, result):
        fn = getattr(lib, name)
        fn.argtypes, fn.restype = args, result
        return fn
    try:
        template = bind('clang_Cursor_getGCCAssemblyTemplate', [cx.Cursor], cx._CXString)
    except AttributeError:
        template = None
    return (lib, template,
            bind('clang_getCursorPrintingPolicy', [cx.Cursor], ctypes.c_void_p),
            bind('clang_getCursorPrettyPrinted', [cx.Cursor, ctypes.c_void_p], cx._CXString),
            bind('clang_PrintingPolicy_dispose', [ctypes.c_void_p], None))


def string(value):
    return str(cx.conf.lib.clang_getCString(value))


def printed_templates(function):
    """LLVM 18 fallback: clang expands macros when printing a function's AST.

    The secondary buffer is used ONLY by clang's lexer, not for type checking.
    Its missing surrounding declarations do not affect the template tokens.
    The original build TU must already have passed the normal parse gate.
    """
    _, _, policy_get, pretty, dispose = api()
    policy = policy_get(function)
    try:
        text = string(pretty(function, policy))
    finally:
        dispose(policy)
    if not text:
        raise ParseError('clang could not print the asm enclosing function')
    name = '/tmp/os32-asm-lexer.c'
    buffer = cx.Index.create().parse(name, args=['-std=gnu11'], unsaved_files=[(name,text)])
    file = buffer.get_file(name)
    start = cx.SourceLocation.from_offset(buffer,file,0)
    end = cx.SourceLocation.from_offset(buffer,file,len(text.encode()))
    tokens = list(buffer.get_tokens(extent=cx.SourceRange.from_locations(start,end)))
    result = []
    for i,t in enumerate(tokens):
        if t.spelling not in ('asm','__asm','__asm__'):
            continue
        # A local register variable's asm label is not an ASM_STMT.
        if i and tokens[i-1].kind == cx.TokenKind.IDENTIFIER:
            continue
        j = i+1
        while j < len(tokens) and tokens[j].spelling in ('volatile','goto','inline'):
            j += 1
        if j == len(tokens) or tokens[j].spelling != '(':
            raise ParseError('clang printed an invalid asm template')
        value = []
        for token in tokens[j+1:]:
            if token.spelling in (':',')'):
                break
            if token.kind != cx.TokenKind.LITERAL or not token.spelling.startswith('"'):
                raise ParseError('clang printed an unresolved asm template')
            value.append(literal.literal_eval(token.spelling))
        result.append(''.join(value))
    cursors = [c for c,_ in walk(function) if c.kind == K.ASM_STMT]
    if len(result) != len(cursors):
        raise ParseError('clang printed asm count differs from original AST')
    return dict(zip((c.hash for c in cursors),result))


def templates(tu, force_printed=False):
    _, native, _, _, _ = api()
    result = {}
    def visit(c, function=None):
        if c.kind == K.FUNCTION_DECL and c.is_definition():
            function = c
        if c.kind == K.ASM_STMT:
            if native is not None and not force_printed:
                result[c.hash] = string(native(c))
            elif c.hash not in result:
                if function is None:
                    raise ParseError('asm has no enclosing function')
                result.update(printed_templates(function))
        for child in c.get_children():
            visit(child,function)
    visit(tu.cursor)
    return result
