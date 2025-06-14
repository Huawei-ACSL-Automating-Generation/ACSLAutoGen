// src/SpecGenerator/loopInvariantPlugins.cpp

#include "macros.h"
#include "specGenerator.h"
#include "state.h"
#include "stringTemplate.h"

using namespace std;
using namespace clang;

class CheckAndDumpLoopInfoPlugin : public LoopInvariantPlugin {
  public:
    CheckAndDumpLoopInfoPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool> generate(const ProgramState &,
                                           const clang::Expr *,
                                           const clang::Stmt *,
                                           const clang::Stmt *,
                                           const LoopInfo &loopInfo) const override {
        if (loopInfo.symbolicLoopEntry_ == nullptr ||
            loopInfo.symbolicLoopEntry_->getPaths().size() != 1) {
            ERROR("SymbolicLoopEntry_ is in an invalid state");
        }

        if (!((loopInfo.index_ && loopInfo.indexBound_) ||
              (loopInfo.index_ == nullptr && loopInfo.indexBound_ == nullptr))) {
            ERROR("Index_ is in an invalid state");
        }
        for (const auto &[addr, pattern] : loopInfo.patternsMap_) {
            if (pattern && (*pattern).initialValue_ == nullptr)
                ERROR("PatternsMap_ is in an invalid state");
        }
        ostringstream oss;
        oss << "SymbolicLoopEntry: " << loopInfo.symbolicLoopEntry_->dump() << endl;
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
        return make_tuple(nullopt, true);
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(CheckAndDumpLoopInfoPlugin, "checkAndDumpLoopInfo");

class LinearInvariantPlugin : public LoopInvariantPlugin {
  public:
    LinearInvariantPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool> generate(const ProgramState &loopEntry,
                                           const clang::Expr *cond,
                                           const clang::Stmt *inc,
                                           const clang::Stmt *body,
                                           const LoopInfo &loopInfo) const override {
        auto &symbolicState = loopInfo.symbolicLoopEntry_;
        if (symbolicState->getPaths().empty())
            return make_tuple(nullopt, true);

        auto exprs = symbolicState->stepExpr(cond);
        int len    = exprs.size() / symbolicState->getPaths().size();
        std::vector<std::unique_ptr<SymbolicExpr>> loopCond;
        for (int i = 0; i < len; ++i)
            loopCond.push_back(std::move(exprs[i]));
        if (loopCond.empty())
            return make_tuple(nullopt, true);

        symbolicState->step(body);
        if (inc)
            symbolicState->step(inc);

        const auto &paths = symbolicState->getPaths();
        auto invariants   = buildLoopInvariant(std::move(loopCond), paths);

        std::ostringstream oss;
        for (const auto &inv : invariants)
            INFO(inv->dump());

        return make_tuple(oss.str(), true);
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(LinearInvariantPlugin, "StInGXPlugin");

class LoopAssignsPlugin : public LoopInvariantPlugin {
  public:
    LoopAssignsPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    tuple<optional<string>, bool> generate(const ProgramState &preState,
                                           const clang::Expr *cond,
                                           const clang::Stmt *inc,
                                           const clang::Stmt *body,
                                           const LoopInfo &loopInfo) const override {
        auto loopCurrent = loopInfo.symbolicLoopEntry_->clone();
        loopCurrent->step(cond);
        loopCurrent->step(body);
        loopCurrent->step(inc);

        auto &entryMS = loopInfo.symbolicLoopEntry_->getPaths()[0]->getMemoryState();

        string spec;
        vector<const Address *> assignedAddrs;

        auto isExisted = [&](const Address &addr) {
            // The time complexity can be reduced from O(n) to O(1), but it requires a complex
            // memoized recursive hash implementation.
            for (auto &it : assignedAddrs) {
                if (*it == addr)
                    return true;
            }
            return false;
        }; // isExisted end

        auto isLocal = [&](const Address &addr) {
            auto from = &addr.getFrom();
            while (auto addr = get_if<unique_ptr<Address>>(from)) {
                from = &(*addr)->getFrom();
            }
            if (auto var = get_if<const VarDecl *>(from)) {
                if (!loopInfo.symbolicLoopEntry_->getPaths()[0]->getVarAddr().contains(*var))
                    return true;
            } else {
                UNREACHABLE();
            }
            return false;
        }; // isLocal

        // For every path after one round symbolic execution.
        for (const auto &path : loopCurrent->getPaths()) {
            // and every Address in the path's memoryState.
            for (auto &[addr, value] : path->getMemoryState()) {
                if (isLocal(addr))
                    continue;
                if (auto it = entryMS.find(addr); it != entryMS.end()) {
                    if (*value == *it->second)
                        continue;
                } else {
                    if (value->getType() != SymbolicExpr::ExprType::Variable)
                        continue;

                    auto symbol = dynamic_cast<const Symbolic::Variable *>(value.get());

                    if (!symbol)
                        UNREACHABLE();

                    if (*symbol->getFrom() == addr) {
                        continue;
                    }
                }

                if (isExisted(addr))
                    continue;

                assignedAddrs.push_back(&addr);
            }
        }

        for (auto &addr : assignedAddrs) {
            auto addrStr = addr->regularForm(/*old = */ false);
            if (addrStr.empty())
                UNREACHABLE();
            if (addrStr[0] == '&')
                spec += addrStr.substr(1) + ", ";
            else
                spec += "*" + addrStr + ", ";
        }

        if (spec.empty())
            return make_tuple(R"(loop assigns \nothing;)", true);
        else
            return make_tuple("loop assigns " + spec.substr(0, spec.length() - 2) + ";", true);
    }

  private:
    string id_;
};
REGISTER_ACSL_PLUGIN(LoopAssignsPlugin, "loopAssigns");