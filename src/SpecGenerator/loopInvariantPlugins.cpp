// src/SpecGenerator/loopInvariantPlugins.cpp

#include "macros.h"
#include "specGenerator.h"
#include "state.h"
#include "stringTemplate.h"

using namespace std;
using namespace clang;

class DumpLoopInfoPlugin : public LoopInvariantPlugin {
  public:
    DumpLoopInfoPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    optional<string> generate(const ProgramState &preState,
                              const clang::Expr *,
                              const clang::Stmt *,
                              const clang::Stmt *,
                              const LoopInfo &loopInfo) const override {
        ostringstream oss;
        oss << "index's address: "
            << (loopInfo.index_ ? loopInfo.index_->regularForm(false) : "NULL") << endl;
        oss << "index's bound: "
            << (loopInfo.indexBound_ ? loopInfo.indexBound_->regularForm(false) : "NULL") << endl;
        oss << "patterns: " << endl;

        for (auto &[addr, pattern] : loopInfo.patternsMap_) {
            oss << "address: " << addr.regularForm(false) << "\t";
            oss << "pattern: ";
            if (pattern) {
                oss << "{ initial value="
                    << ((*pattern).initialValue_ ? (*pattern).initialValue_->regularForm(false)
                                                 : "NULL")
                    << ", step=" << (*pattern).step_ << " }" << endl;
            } else {
                oss << "Value has changed in loop, but pattern is too complex to preprocess."
                    << endl;
            }
        }
        INFO(oss.str());
        return nullopt;
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(DumpLoopInfoPlugin, "dumpLoopInfo");

class LinearInvariantPlugin : public LoopInvariantPlugin {
  public:
    LinearInvariantPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    optional<string> generate(const ProgramState &preState,
                              const clang::Expr *cond,
                              const clang::Stmt *inc,
                              const clang::Stmt *body,
                              const LoopInfo &loopInfo) const override {
        auto &symbolicState = loopInfo.symbolicLoopEntry_;
        if (symbolicState->getPaths().empty())
            return std::nullopt;

        auto exprs = symbolicState->stepExpr(cond);
        int len    = exprs.size() / symbolicState->getPaths().size();
        std::vector<std::unique_ptr<SymbolicExpr>> loopCond;
        for (int i = 0; i < len; ++i)
            loopCond.push_back(std::move(exprs[i]));
        if (loopCond.empty())
            return std::nullopt;

        symbolicState->step(body);
        if (inc)
            symbolicState->step(inc);

        const auto &paths = symbolicState->getPaths();
        auto invariants   = buildLoopInvariant(loopCond, paths);

        std::ostringstream oss;
        for (const auto &inv : invariants)
            INFO(inv->dump());

        return oss.str();
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(LinearInvariantPlugin, "StInGXPlugin");