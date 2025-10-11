// src/SpecGenerator/specGenerators.cpp

#include <ranges>
#include "specGenerator.h"
#include "utilityTemplates.h"
#include "macros.h"
#include "state.h"

using namespace std;
using namespace clang;

namespace {
    // auxiliary function
    template <typename T>
    vector<const T *> getPlugins(std::string_view groupName,
                                 optional<reference_wrapper<const vector<string>>> extraPluginIds) {
        const ACSLPluginGroup *group = ACSLPluginGroupRegistry::instance().getGroup(groupName);
        if (!group) {
            auto names = ACSLPluginGroupRegistry::instance().allGroupNames();
            string allName;
            allName += "[";
            for (auto &name : names) {
                allName += name;
                allName += ", ";
            }
            if (allName.size() > 1) {
                // No flag!
                allName.pop_back();
                allName.pop_back();
            }
            allName += "]";
            ERROR("Unknown ACSL group: " + string{groupName} +
                  ". All registered groups: " + allName);
        }

        vector<string> ids = group->pluginIds;
        if (extraPluginIds)
            ids.insert(ids.end(), (*extraPluginIds).get().begin(), (*extraPluginIds).get().end());

        vector<const T *> plugins;
        for (auto &pid : ids) {
            auto *pl = ACSLPluginRegistry::instance().get(pid);
            if (!pl)
                ERROR("Plugin with id " + pid + " does not exist!");
            auto *fcp = dynamic_cast<const T *>(pl);
            if (fcp) {
                plugins.push_back(fcp);
            }
        }
        return plugins;
    }
} // namespace

LoopInfo::Pattern &LoopInfo::Pattern::operator=(const Pattern &other) {
    if (this == &other)
        return *this;
    initialValue_ = other.initialValue_->clone().into_underlying();
    step_         = other.step_;
    return *this;
}

string LoopInfo::Pattern::dump() const {
    ostringstream oss;
    oss << "initialValue_: "
        << (initialValue_->regularForm() ? initialValue_->regularForm().value()
                                         : initialValue_->dump())
        << "\n";
    oss << "step_: " << to_string(step_) << "\n";
    return oss.str();
}

LoopInfo::LoopInfo(const clang::Stmt *loopStmt)
    : loopStmt_(loopStmt), initStmt_{nullptr}, condExpr_{nullptr}, incStmt_(nullptr),
      bodyStmt_(nullptr) {
    if (const auto *forStmt = dyn_cast<ForStmt>(loopStmt)) {
        initStmt_ = forStmt->getInit();
        condExpr_ = forStmt->getCond();
        incStmt_  = forStmt->getInc();
        bodyStmt_ = forStmt->getBody();
    } else if (const auto *whileStmt = dyn_cast<WhileStmt>(loopStmt)) {
        condExpr_ = whileStmt->getCond();
        bodyStmt_ = whileStmt->getBody();
    } else if (const auto *doWhileStmt = dyn_cast<DoStmt>(loopStmt)) {
        condExpr_ = doWhileStmt->getCond();
        bodyStmt_ = doWhileStmt->getBody();
    } else {
        ERROR("LoopStmt should be a Stmt of loop.");
    }
}

string emitFunctionContract(const ProgramState &pre,
                            const ProgramState &post,
                            std::string_view groupName,
                            optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<FunctionContractPlugin>(groupName, extraPluginIds);
    string spec  = ACSL_HEAD.to_string();

    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            continue;
        DEBUG("Plugin {" + string{plugin->id()} + "} is generating...");
        if (auto s = plugin->generate(pre, post); s)
            spec += *s;
    }
    spec += ACSL_END.to_string();
    return spec;
}

pair<LoopInfo, bool> parseLoopInfo(
    const ProgramState &preState,
    const ProgramState &loopEntry,
    const clang::Stmt *loopStmt,
    std::string_view groupName,
    optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

    LoopInfo loopInfo{loopStmt};
    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            UNREACHABLE();
        DEBUG("Plugin {" + string{plugin->id()} + "} is parsing...");
        if (!plugin->parse(preState, loopEntry, loopInfo))
            return pair{std::move(loopInfo), false};
    }
    return pair{std::move(loopInfo), true};
}

void parseComplexLoopInfo(const ProgramState &preState,
                          const ProgramState &loopEntry,
                          LoopInfo &loopInfo,
                          std::string_view groupName,
                          optional<reference_wrapper<const vector<string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            UNREACHABLE();
        DEBUG("Plugin {" + string{plugin->id()} + "} is parsing...");
        if (!plugin->parse(preState, loopEntry, loopInfo))
            ERROR("Plugin: {" + string{plugin->id()} +
                  "} parsing complex loop's information failed.");
    }
}

std::pair<std::string, unique_ptr<ProgramState>> emitLoopInvariant(
    const ProgramState &preState,
    const ProgramState &loopEntry,
    const LoopInfo &loopInfo,
    std::string_view groupName,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
    auto plugins = getPlugins<LoopInvariantPlugin>(groupName, extraPluginIds);
    vector<unique_ptr<Path>> invariants;
    string spec = ACSL_HEAD.to_string();

    auto loopEntryPoint =
        SourcePoint::fromStmtBefore(loopInfo.loopStmt_, loopEntry.getContext().getSourceManager(),
                                    loopEntry.getContext().getLangOptions());
    auto postState   = preState.clone();
    auto &postPaths  = postState->getPaths();
    auto pathNum     = postPaths.size();
    auto resultInfos = vector<vector<PostInfo>>{pathNum};

    // Update the resultInfos with a plugin's postInfo.
    auto updateResultInfos = [&](vector<PostInfo> &infos) {
        if (infos.empty())
            return;
        if (preState.getPaths().size() != loopEntry.getPaths().size()) {
            ERROR("Branch is unsupported here.");
        }

        auto &entryPaths = loopEntry.getPaths();

        for (auto i : views::iota(size_t{0}, pathNum)) {
            auto &entryPath         = *entryPaths.at(i);
            auto &postBranchesInfos = resultInfos.at(i);

            if (infos.size() != 1)
                TODO();
            if (postBranchesInfos.empty())
                postBranchesInfos.emplace_back();
            auto &info           = infos.at(0);
            auto &postBranchInfo = postBranchesInfos.at(0);

            for (auto &[addr, value] : info.memoryMap_) {
                auto subedAddr = getSubstitutedAddr(addr, entryPath, loopEntryPoint);
                substituteSymbols(value, entryPath, loopEntryPoint);
                if (auto it = postBranchInfo.memoryMap_.find(*subedAddr);
                    it != postBranchInfo.memoryMap_.end() && !it->second->isUnknown()) {
                    auto regForm = value->simplifiedExpr()->regularForm();
                    WARN("Another plugin has already updated this address. The new value: {" +
                         (regForm ? regForm.value() : value->dump()) + "} is discarded.");
                    continue;
                }
                postBranchInfo.memoryMap_.insert_or_assign(*subedAddr, std::move(value));
            }

            for (auto &cond : info.pathConds_) {
                substituteSymbols(cond, entryPath, loopEntryPoint);
                postBranchInfo.pathConds_.push_back(std::move(cond));
            }
        }
    }; // updatePostState

    for (auto &plugin : plugins) {
        if (plugin == nullptr)
            UNREACHABLE();
        DEBUG("Plugin {" + string{plugin->id()} + "} is generating...");
        auto [s, continueFlag, postInfos] = plugin->generate(preState, loopEntry, loopInfo);

        if (s) {
            spec += "    " /*4 spaces*/ + *s + "\n";
        }

        updateResultInfos(postInfos);

        if (!continueFlag)
            break;
    }
    spec += ACSL_END.to_string();

    // Build post-state from result infos.
    for (auto i : views::iota(size_t{0}, pathNum)) {
        auto &prePath           = preState.getPaths().at(i);
        auto &postPath          = postPaths.at(i);
        auto &postBranchesInfos = resultInfos.at(i);

        if (postBranchesInfos.empty()) {
            WARN("A path has no post-info, there might be some errors.");
            continue;
        }

        if (postBranchesInfos.size() != 1)
            TODO();

        auto &postBranchInfo = postBranchesInfos.at(0);
        for (auto &[addr, value] : postBranchInfo.memoryMap_) {
            auto root = addr.get().getFromRoot();
            if (root == nullopt)
                TODO();
            if (!postPath->getVarAddr().contains(root.value()))
                continue;
            postPath->updateMemory(addr, std::move(value));
        }
        for (auto &&[addr, value] : prePath->getMemoryState().flat()) {
            if (postPath->getMemoryState().contains(addr))
                continue;
            postPath->updateMemory(addr, value->clone());
        }

        for (auto &pathCond : prePath->getPathConditions())
            postPath->insertPathCondition(pathCond->clone());
        for (auto &pathCond : postBranchInfo.pathConds_)
            postPath->insertPathCondition(std::move(pathCond));
    }

    return pair{spec, std::move(postState)};
}

void substituteSymbols(not_null<unique_ptr<SymbolicExpr>> &expr,
                       const Path &loopEntryPath,
                       const SourcePoint &fromPoint) {
    auto &mem = loopEntryPath.getMemoryState(); // Memory snapshot at loop entry
    switch (expr->getType()) {
        using enum SymbolicExpr::ExprType;
        case Literal: return; // Literals have no symbolic origin; nothing to substitute.
        case Variable: {
            // Try to resolve where this variable comes from and substitute with the value at that
            // address.
            auto &var = dynamic_cast<const Symbolic::Variable &>(*expr.get().get());
            if (var.getFromPoint() && var.getFromPoint().value() != fromPoint)
                return;
            std::visit(
                [&](auto &&arg) -> void {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        // No origin info; not substitutable at the moment.
                        TODO();
                    } else if constexpr (std::is_same_v<
                                             T, not_null<std::unique_ptr<const Symbolic::Address>>>) {
                        auto realFromAddr = getSubstitutedAddr(*arg, loopEntryPath, fromPoint);
                        // Origin is an address-like handle; try reading from loop-entry memory.
                        if (auto value = mem.read(*realFromAddr)) {
                            // Replace current variable with the cloned value read from memory.
                            expr = value.value()->clone();
                        } else {
                            // Address originates from an address present on this path at loop entry
                            // but hasn't been accessed -> construct a Variable with corrext
                            // fromAddr and fromPoint.
                            expr = make_unique<Symbolic::Variable>(
                                var.getVarType(), std::move(realFromAddr).into_underlying(),
                                loopEntryPath.getStartPoint());
                        }
                    }
                },
                var.getFromAddr());
            return;
        }
        case Address: {
            // For a SymbolAddress node, try to read its "from" origin and substitute the node by
            // the value.
            auto symbolAddr = dynamic_cast<const Symbolic::SymbolAddress *>(expr.get().get());
            if (symbolAddr == nullptr)
                UNREACHABLE();
            if (symbolAddr->getFromPoint() && symbolAddr->getFromPoint().value() != fromPoint)
                return;
            expr = getSubstitutedAddr(*symbolAddr, loopEntryPath, fromPoint).into_underlying();
            return;
        }
        case BinaryOp: {
            // Recursively substitute in both children (non-const downcast is intentional).
            auto &bin = dynamic_cast<Symbolic::BinaryOpExpr &>(*expr.get().get());
            substituteSymbols(bin.getLeft(), loopEntryPath, fromPoint);
            substituteSymbols(bin.getRight(), loopEntryPath, fromPoint);
            return;
        }
        case UnaryOp: {
            // Recursively substitute in sub-expression (non-const downcast is intentional).
            auto &un = dynamic_cast<Symbolic::UnaryOpExpr &>(*expr.get().get());
            substituteSymbols(un.getSub(), loopEntryPath, fromPoint);
            return;
        }
        case Structure: {
            auto &st = dynamic_cast<Symbolic::Structure &>(*expr.get().get());
            for (auto &field : st.fieldsValues()) {
                // Substitute all fields of structure.
                substituteSymbols(field, loopEntryPath, fromPoint);
            }
            return;
        }
        case Unknown: return; // Unknown nodes are left untouched.
        default: UNREACHABLE();
    }
    UNREACHABLE();
};

not_null<unique_ptr<Address>> getSubstitutedAddr(const Address &addr,
                                                 const Path &loopEntryPath,
                                                 const SourcePoint &fromPoint) {
    // If it's not a symbolic address, simply return a clone.
    if (addr.getAddressType() == Address::AddressType::VariableAddr)
        return addr.addressClone();

    auto &mem = loopEntryPath.getMemoryState();

    if (addr.getAddressType() == Address::AddressType::FieldAddr) {
        auto &fieldAddr = dynamic_cast<const FieldAddress &>(addr);
        return std::visit(
            [&](auto &&arg) -> not_null<unique_ptr<Address>> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    // No origin info — unresolved substitution.
                    TODO();
                } else if constexpr (std::is_same_v<
                                         T, std::pair<not_null<std::unique_ptr<const Address>>,
                                                      const size_t>>) {
                    // Substitute the base address.
                    auto &[baseAddr, index] = arg;
                    auto trueBaseAddr = getSubstitutedAddr(*baseAddr, loopEntryPath, fromPoint);
                    return make_unique<FieldAddress>(
                        fieldAddr.getDefinition(),
                        pair<not_null<std::unique_ptr<const Address>>, const size_t>{
                            std::move(trueBaseAddr).into_underlying(), index});
                }
            },
            fieldAddr.getFrom());
    }

    auto &symbolAddr = dynamic_cast<const SymbolAddress &>(addr);

    if (symbolAddr.getFromPoint() != fromPoint)
        return addr.addressClone();
    // Try to resolve the "from" origin of the SymbolAddress via loop-entry memory.
    return std::visit(
        [&](auto &&arg) -> not_null<unique_ptr<Address>> {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                // No origin info — unresolved substitution.
                TODO();
            } else if constexpr (std::is_same_v<T, not_null<std::unique_ptr<const Address>>>) {
                auto realFromAddr = getSubstitutedAddr(*arg, loopEntryPath, fromPoint);

                // Clone and substitute the offset of the original SymbolAddress.
                auto offset = symbolAddr.getOffset()->clone();
                substituteSymbols(offset, loopEntryPath,
                                  fromPoint); // substitute any vars/addresses in offset
                offset = offset->simplifiedExpr();

                optional<not_null<unique_ptr<SymbolicExpr>>> length{};
                // If original was a range, also substitute and set the length.
                if (symbolAddr.isRange()) {
                    length = symbolAddr.getLength()->clone();
                    substituteSymbols(length.value(), loopEntryPath, fromPoint);
                    length = length.value()->simplifiedExpr();
                }

                // Origin is an address; attempt to read the value at that origin.
                if (auto value = mem.read(*realFromAddr)) {
                    // The origin resolves to a value; it must be convertible to an "offseted
                    // address".
                    auto realAddr = value.value()->tryEvalAsSymbolAddr();
                    if (realAddr == nullopt)
                        ERROR("This expr should be a address");

                    // Apply substituted offset to the concrete address.
                    realAddr.value()->addOffset(std::move(offset));

                    if (length) {
                        realAddr.value()->setLength(std::move(length).value());
                    }
                    // Return the underlying concrete address (unique_ptr<Address>).
                    return std::move(realAddr).value().into_underlying();
                } else {
                    // The origin hasn't been accessed at loop entry -> construct a SymbolAddress
                    // with corrext fromAddr and fromPoint.
                    return make_unique<SymbolAddress>(std::move(realFromAddr).into_underlying(),
                                                      loopEntryPath.getStartPoint(),
                                                      std::move(offset).into_underlying(),
                                                      std::move(length).value().into_underlying());
                }
            }
        },
        symbolAddr.getFromAddr());
};
