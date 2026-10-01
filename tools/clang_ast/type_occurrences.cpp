// libclang cursors omit TypeLocs, including folded array-size expressions.
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>
#include <iterator>

class TypeVisitor : public clang::RecursiveASTVisitor<TypeVisitor> {
    clang::SourceManager &sources;
public:
    explicit TypeVisitor(clang::SourceManager &sources) : sources(sources) {}

    bool TraverseTypeLoc(clang::TypeLoc loc) {
        if (loc.isNull())
            return true;
        clang::QualType type = loc.getType();
        auto position = sources.getExpansionLoc(loc.getBeginLoc());
        if (position.isInvalid())
            return true;
        auto file = sources.getFilename(position);
        auto line = sources.getLineNumber(sources.getFileID(position),
                                          sources.getFileOffset(position));
        auto report = [&](const char *feature) {
            llvm::outs() << file << '\t' << line << '\t' << feature << '\n';
        };
        if (type.isRestrictQualified())
            report("restrict");
        // Array parameter bracket qualifiers belong to ArrayType, not QualType.
        if (const auto *array = llvm::dyn_cast<clang::ArrayType>(type.getTypePtr())) {
            if (array->getIndexTypeQualifiers().hasRestrict())
                report("restrict");
        }
        if (type->isAtomicType())
            report("_Atomic");
        return clang::RecursiveASTVisitor<TypeVisitor>::TraverseTypeLoc(loc);
    }
};

class TypeConsumer : public clang::ASTConsumer {
public:
    void HandleTranslationUnit(clang::ASTContext &context) override {
        TypeVisitor visitor(context.getSourceManager());
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }
};

class TypeAction : public clang::ASTFrontendAction {
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &, llvm::StringRef) override {
        return std::make_unique<TypeConsumer>();
    }
};

int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    std::string code(std::istreambuf_iterator<char>(std::cin), {});
    std::vector<std::string> args(argv + 2, argv + argc);
    return clang::tooling::runToolOnCodeWithArgs(
        std::make_unique<TypeAction>(), code, args, argv[1]) ? 0 : 1;
}
