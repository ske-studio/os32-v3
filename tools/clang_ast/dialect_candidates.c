// Select cursors for the Python dialect rules inside libclang's native walk.
// Written types (TypeLocs) are inspected separately by type_occurrences.cpp.
#include <clang-c/Index.h>

enum {
    RESTRICT = 1, ATOMIC = 2, VLA = 4, ANONYMOUS = 8, TLS = 16, OLD_STYLE = 32
};
typedef void (*Report)(CXCursor, unsigned);

struct Scan {
    Report report;
    int public_macros;
};

static unsigned type_features(CXType original)
{
    CXType type = clang_getCanonicalType(original);
    unsigned features = clang_isRestrictQualifiedType(type) ? RESTRICT : 0;
    switch (type.kind) {
    case CXType_Atomic:
        return features | ATOMIC;
    case CXType_VariableArray:
        features |= VLA;
        // fall through
    case CXType_ConstantArray:
    case CXType_IncompleteArray:
        return features | type_features(clang_getArrayElementType(type));
    case CXType_Pointer:
        return features | type_features(clang_getPointeeType(type));
    case CXType_FunctionProto:
        for (int i = 0, count = clang_getNumArgTypes(type); i < count; ++i)
            features |= type_features(clang_getArgType(type, i));
        // fall through
    case CXType_FunctionNoProto:
        return features | type_features(clang_getResultType(type));
    default:
        return features;
    }
}

static enum CXChildVisitResult visit(CXCursor cursor, CXCursor parent,
                                     CXClientData data)
{
    (void)parent;
    struct Scan *scan = data;
    enum CXCursorKind kind = clang_getCursorKind(cursor);
    CXType type = clang_getCursorType(cursor);
    unsigned features = type_features(type);
    if (kind == CXCursor_FunctionDecl) {
        features |= type_features(clang_getResultType(type));
        if (type.kind == CXType_FunctionNoProto && clang_isCursorDefinition(cursor))
            features |= OLD_STYLE;
    }
    if (kind == CXCursor_VarDecl && clang_getCursorTLSKind(cursor) != CXTLS_None)
        features |= TLS;
    if ((kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl) &&
        clang_Cursor_isAnonymousRecordDecl(cursor))
        features |= ANONYMOUS;

    if (features || kind == CXCursor_InclusionDirective || kind == CXCursor_StaticAssert ||
        (kind == CXCursor_MacroDefinition && scan->public_macros))
        scan->report(cursor, features);
    return CXChildVisit_Recurse;
}

void dialect_candidates(CXTranslationUnit tu, int public_macros, Report report)
{
    struct Scan scan = { report, public_macros };
    clang_visitChildren(clang_getTranslationUnitCursor(tu), visit, &scan);
}
