// tests/testUtils/testHelper.h

#ifndef TEST_HELPER_H
#define TEST_HELPER_H

#include "ASTExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "Analyzer/function.h"
#include "Analyzer/state.h"
#include "Analyzer/analysis.h"

// These codes performs minimal safety checks, so please ensure the validity of the input.

inline auto doPluginOnFirstFunc(const std::string &code, const std::string &pid) {
    using namespace std;
    using namespace clang;

    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    preState->init();
    DEBUG(preState->dump());
    auto postState = preState->clone();
    for (clang::Stmt *stmt : func->getBody()->children()) {
        postState->step(stmt);
        DEBUG(postState->dump());
    }

    auto *pl = ACSLPluginRegistry::instance().get(pid);
    if (!pl)
        ERROR("Plugin with id " + pid + " does not exist!");
    auto *fcp = dynamic_cast<const FunctionContractPlugin *>(pl);

    auto spec = fcp->generate(*preState, *postState);
    if (spec)
        DEBUG(*spec);
    return spec;
}

inline void doAll(const std::string_view code) {
    using namespace std;
    using namespace clang;

    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    ACSLAnalyzer analyzer(context.value());
    analyzer.analyzeFunctions();
    for (auto &str : context.value().getInsertedStrings()) {
        DEBUG(str);
    }
}

inline auto execOnFirstFunc(const std::string &code) {
    using namespace std;
    using namespace clang;

    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    preState->init();
    DEBUG(preState->dump());
    auto postState = preState->clone();
    for (clang::Stmt *stmt : func->getBody()->children()) {
        postState->step(stmt);
        DEBUG(postState->dump());
    }

    return postState;
}

inline not_null<std::unique_ptr<SymbolicExpr>> getReturnExprOfFirstPath(const ProgramState &state) {
    using namespace std;
    using namespace clang;

    if (state.getPaths().empty())
        ERROR("Empty paths_!");
    auto &firstPath  = state.getPaths()[0];
    auto &returnExpr = firstPath->getReturnExpr();
    if (returnExpr == nullopt)
        ERROR("There is no returnExpr!");
    return returnExpr.value()->clone();
}

inline not_null<std::unique_ptr<ProgramState>> getPostStateOfFirstLoop(const std::string_view code) {
    using namespace std;
    using namespace clang;

    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func = e.findFirstDecl<clang::FunctionDecl>();
    auto symbolicState =
        make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    symbolicState->init();
    DEBUG(symbolicState->dump());
    for (clang::Stmt *stmt : func->getBody()->children()) {
        symbolicState->step(stmt);
        if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) || isa<clang::DoStmt>(stmt)) {
            DEBUG(symbolicState->dump());
            break;
        }
    }
    return symbolicState;
}

inline auto doPluginsOnFirstLoop(std::string_view code, const std::vector<std::string> pids) {
    using namespace std;
    using namespace clang;

    static ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    clang::Stmt *loopStmt;
    preState->init();
    DEBUG(preState->dump());
    for (clang::Stmt *stmt : func->getBody()->children()) {
        if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) || isa<clang::DoStmt>(stmt)) {
            loopStmt = stmt;
            break;
        }
        preState->step(stmt);
        DEBUG(preState->dump());
    }

    auto loopEntry = preState->clone();
    if (auto forLoop = dyn_cast<clang::ForStmt>(loopStmt); forLoop && forLoop->getInit()) {
        loopEntry->step(forLoop->getInit());
        DEBUG(loopEntry->dump());
    }

    LoopInfo loopInfo{loopStmt};

    if (auto *pl = ACSLPluginRegistry::instance().get("setLoopEntry")) {
        auto *setLoopEntryPlugin = dynamic_cast<const LoopInfoPlugin *>(pl);

        if (!setLoopEntryPlugin->parse(*preState, *loopEntry, loopInfo))
            ERROR("Set loop entry fail.");
    } else {
        ERROR("Set loop entry fail.");
    }

    bool result;
    for (auto &pid : pids) {
        auto *pl = ACSLPluginRegistry::instance().get(pid);
        if (!pl)
            ERROR("Plugin with id " + pid + " does not exist!");
        auto *fcp = dynamic_cast<const LoopInfoPlugin *>(pl);
        result    = fcp->parse(*preState, *loopEntry, loopInfo);
    }
    return pair{std::move(loopInfo), result};
}

inline std::tuple<std::optional<std::string>, bool, vector<PostInfo>> doPluginOnFirstLoop(
    const string &code,
    const string &pid) {
    using namespace std;
    using namespace clang;

    ASTExtractor e;
    static optional<ACSLContext> context{};

    e.init(code);
    context.emplace(e.getASTContext());
    auto func     = e.findFirstDecl<clang::FunctionDecl>();
    auto preState = make_unique<ProgramState>(make_unique<ACSLFunction>(func), context.value());
    clang::Stmt *loopStmt;
    preState->init();
    DEBUG(preState->dump());
    for (clang::Stmt *stmt : func->getBody()->children()) {
        if (isa<clang::WhileStmt>(stmt) || isa<clang::ForStmt>(stmt) || isa<clang::DoStmt>(stmt)) {
            loopStmt = stmt;
            break;
        }
        preState->step(stmt);
        DEBUG(preState->dump());
    }

    auto loopEntry = preState->clone();
    if (auto forLoop = dyn_cast<clang::ForStmt>(loopStmt); forLoop && forLoop->getInit()) {
        loopEntry->step(forLoop->getInit());
        DEBUG(loopEntry->dump());
    }

    auto [loopInfo, ok] = parseLoopInfo(*preState, *loopEntry, loopStmt);
    if (!ok) {
        // TODO(complex loop)
        UNIMPLEMENT("Loop is too complex!");
    }

    auto *pl = ACSLPluginRegistry::instance().get(pid);
    if (!pl)
        ERROR("Plugin with id " + pid + " does not exist!");
    auto *fcp = dynamic_cast<const LoopInvariantPlugin *>(pl);

    return fcp->generate(*preState, *loopEntry, loopInfo);
}

consteval unsigned digitCount(unsigned x) {
    unsigned n = 1;
    while (x >= 10) {
        x /= 10;
        ++n;
    }
    return n;
}

namespace acslg::details {
    consteval auto makeCodeArr() {
        struct Buf {
            char data[2048];
            unsigned n = 0;
        } b{};
        auto app = [&](const char *s) {
            while (*s)
                b.data[b.n++] = *s++;
        };
        auto appUInt = [&](unsigned v) {
            char tmp[16];
            int k = 0;
            do {
                tmp[k++] = char('0' + (v % 10));
                v /= 10;
            } while (v);
            while (k--)
                b.data[b.n++] = tmp[k];
        };

        app("void func() {}");
        for (unsigned i = 1; i <= 20; ++i) {
            app("int g");
            appUInt(i);
            app(";");
        }
        for (unsigned i = 1; i <= 20; ++i) {
            app("int f");
            appUInt(i);
            app("(int x){return 0;}");
        }

        std::array<char, 2048> arr{};
        for (unsigned i = 0; i < b.n; ++i)
            arr[i] = b.data[i];
        arr[b.n] = '\0';
        return std::pair{arr, b.n};
    }
} // namespace acslg::details

class FixtureWithCode : public ::testing::Test {
  protected:
    static constexpr auto codeArr = acslg::details::makeCodeArr();
    static constexpr std::string_view code{codeArr.first.data(), codeArr.second};

    FixtureWithCode()
        : e(code),
          defaultPoint(SourcePoint::fromFuncDeclBefore(e.findFirstDecl<clang::FunctionDecl>(),
                                                       e.getSourceManager(),
                                                       e.getLangOptions())) {
        using namespace std;
        using namespace clang;

        for (auto d : e.getASTContext().getTranslationUnitDecl()->decls()) {
            if (auto vd = llvm::dyn_cast<clang::VarDecl>(d))
                varDecls.push_back(vd);
        }
        for (auto d : e.getASTContext().getTranslationUnitDecl()->decls()) {
            if (auto fd = llvm::dyn_cast<clang::FunctionDecl>(d)) {
                if (fd->getNameAsString().find("func") == string::npos)
                    funcDecls.push_back(fd);
            }
        }
    }

    not_null<const clang::VarDecl *> getVarDecl(unsigned int id) {
        if (!varIdCountMap.contains(id)) {
            assert(var_count < varDecls.size() && "Need more varDecl? Change the for loop above!");
            varIdCountMap[id] = var_count++;
        }
        return varDecls.at(varIdCountMap.at(id));
    }

    not_null<const clang::FunctionDecl *> getFuncDecl(unsigned int id) {
        if (!funcIdCountMap.contains(id)) {
            assert(func_count < funcDecls.size() &&
                   "Need more FunctionDecl? Change the for loop above!");
            funcIdCountMap[id] = func_count++;
        }
        return funcDecls.at(funcIdCountMap.at(id));
    }

    VariableAddress makeVariableAddr(unsigned int id) { return VariableAddress{getVarDecl(id)}; }

    Symbolic::SymbolAddress makeRangeAddr(unsigned int id,
                                          std::unique_ptr<const SymbolicExpr> offset,
                                          std::unique_ptr<const SymbolicExpr> len,
                                          std::optional<SourcePoint> fromPoint = nullopt) {
        using namespace std;
        using namespace clang;

        auto baseAddr = makeVariableAddr(id);
        if (len != nullptr)
            return Symbolic::SymbolAddress{baseAddr.addressClone().into_underlying(),
                                           fromPoint.value_or(defaultPoint), std::move(offset),
                                           std::move(len)};
        return Symbolic::SymbolAddress{baseAddr.addressClone().into_underlying(),
                                       fromPoint.value_or(defaultPoint), std::move(offset),
                                       nullopt};
    }

    std::unique_ptr<Symbolic::Variable> makeVariable(
        unsigned int id,
        std::optional<SourcePoint> fromPoint = nullopt) {
        using namespace std;
        using namespace clang;

        return std::make_unique<Symbolic::Variable>(
            SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, id},
            make_unique<VariableAddress>(getVarDecl(id)), fromPoint.value_or(defaultPoint));
    }

    Symbolic::SymbolAddress makeSimpleSymbolAddr(unsigned int id,
                                                 std::optional<SourcePoint> fromPoint = nullopt) {
        auto baseAddr = makeVariableAddr(id);
        return Symbolic::SymbolAddress{baseAddr.addressClone().into_underlying(),
                                       fromPoint.value_or(defaultPoint), nullopt, nullopt};
    }

    Symbolic::SymbolAddress makePointAddr(unsigned int id, std::uint64_t off) {
        return makeRangeAddr(id, std::make_unique<LiteralExpr>(static_cast<std::uint64_t>(off)),
                             nullptr);
    }

    void ExpectReadEqAt(MemoryModel &mm,
                        unsigned id,
                        std::uint64_t off,
                        const Symbolic::SymbolicExpr &expected) {
        auto addr = makePointAddr(id, off);
        auto got  = mm.read(addr);
        ASSERT_NE(got, nullopt) << "read returned null at off=" << off;
        EXPECT_EQ(*got.value(), expected) << "mismatch at off=" << off;
    }

    void ExpectReadNullAt(MemoryModel &mm, unsigned id, std::uint64_t off) {
        auto addr = makePointAddr(id, off);
        EXPECT_EQ(mm.read(addr), nullopt) << "expected null at off=" << off;
    }

    ASTExtractor e;
    SourcePoint defaultPoint;

  private:
    std::vector<const clang::VarDecl *> varDecls;
    size_t var_count{0};
    std::unordered_map<unsigned int, size_t> varIdCountMap{};

    std::vector<const clang::FunctionDecl *> funcDecls;
    size_t func_count{0};
    std::unordered_map<unsigned int, size_t> funcIdCountMap{};
};

#endif