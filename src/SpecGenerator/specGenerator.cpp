// src/SpecGenerator/specGenerators.cpp

#include <ranges>
#include <llvm/ADT/TypeSwitch.h>

#include "specGenerator.h"
#include "utilityTemplates.h"
#include "macros.h"
#include "state.h"

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    namespace {
        // auxiliary function
        template <typename T>
        std::vector<const T *> getPlugins(
            std::string_view groupName,
            std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
            const ACSLPluginGroup *group = ACSLPluginGroupRegistry::instance().getGroup(groupName);
            if (!group) {
                auto names = ACSLPluginGroupRegistry::instance().allGroupNames();
                std::string allName;
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
                ERROR("Unknown ACSL group: " + std::string{groupName} +
                      ". All registered groups: " + allName);
            }

            std::vector<std::string> ids = group->pluginIds;
            if (extraPluginIds)
                ids.insert(ids.end(), (*extraPluginIds).get().begin(),
                           (*extraPluginIds).get().end());

            std::vector<const T *> plugins;
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

    std::string LoopInfo::Pattern::dump() const {
        std::ostringstream oss;
        oss << "initialValue_: "
            << (initialValue_->regularForm() ? initialValue_->regularForm().value()
                                             : initialValue_->dump())
            << "\n";
        oss << "step_: " << std::to_string(step_) << "\n";
        return oss.str();
    }

    LoopInfo::LoopInfo(const clang::Stmt *loopStmt)
        : loopStmt_(loopStmt), initStmt_{nullptr}, condExpr_{nullptr}, incStmt_(nullptr),
          bodyStmt_(nullptr) {
        if (const auto *forStmt = dyn_cast<clang::ForStmt>(loopStmt)) {
            initStmt_ = forStmt->getInit();
            condExpr_ = forStmt->getCond();
            incStmt_  = forStmt->getInc();
            bodyStmt_ = forStmt->getBody();
        } else if (const auto *whileStmt = dyn_cast<clang::WhileStmt>(loopStmt)) {
            condExpr_ = whileStmt->getCond();
            bodyStmt_ = whileStmt->getBody();
        } else if (const auto *doWhileStmt = dyn_cast<clang::DoStmt>(loopStmt)) {
            condExpr_ = doWhileStmt->getCond();
            bodyStmt_ = doWhileStmt->getBody();
        } else {
            ERROR("LoopStmt should be a clang::Stmt of loop.");
        }
    }

    std::string emitFunctionContract(
        const analyzer::ProgramState &pre,
        const analyzer::ProgramState &post,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins     = getPlugins<FunctionContractPlugin>(groupName, extraPluginIds);
        std::string spec = ACSL_HEAD.to_string();

        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                continue;
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is generating...");
            if (auto s = plugin->generate(pre, post); s)
                spec += *s;
        }
        spec += ACSL_END.to_string();
        return spec;
    }

    std::pair<LoopInfo, bool> parseLoopInfo(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const clang::Stmt *loopStmt,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

        LoopInfo loopInfo{loopStmt};
        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is parsing...");
            if (!plugin->parse(preState, loopEntry, loopInfo))
                return std::pair{std::move(loopInfo), false};
        }
        return std::pair{std::move(loopInfo), true};
    }

    void parseComplexLoopInfo(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        LoopInfo &loopInfo,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins = getPlugins<LoopInfoPlugin>(groupName, extraPluginIds);

        for (auto &plugin : plugins) {
            if (plugin == nullptr)
                UNREACHABLE();
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is parsing...");
            if (!plugin->parse(preState, loopEntry, loopInfo))
                ERROR("Plugin: {" + std::string{plugin->id()} +
                      "} parsing complex loop's information failed.");
        }
    }

    std::pair<std::string, std::unique_ptr<analyzer::ProgramState>> emitLoopInvariant(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const LoopInfo &loopInfo,
        std::string_view groupName,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds) {
        auto plugins = getPlugins<LoopInvariantPlugin>(groupName, extraPluginIds);
        std::vector<std::unique_ptr<analyzer::Path>> invariants;
        std::string spec = ACSL_HEAD.to_string();

        auto loopEntryPoint = symb::SourcePoint::fromStmtBefore(
            loopInfo.loopStmt_, loopEntry.getContext().getSourceManager(),
            loopEntry.getContext().getLangOptions());
        auto postState   = preState.clone();
        auto &postPaths  = postState->getPaths();
        auto pathNum     = postPaths.size();
        auto resultInfos = std::vector<std::vector<PostInfo>>{pathNum};

        // Update the resultInfos with a plugin's postInfo.
        auto updateResultInfos = [&](std::vector<PostInfo> &infos) {
            if (infos.empty())
                return;
            if (preState.getPaths().size() != loopEntry.getPaths().size()) {
                ERROR("Branch is unsupported here.");
            }

            auto &entryPaths = loopEntry.getPaths();

            for (auto i : std::views::iota(size_t{0}, pathNum)) {
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
            DEBUG("Plugin {" + std::string{plugin->id()} + "} is generating...");
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
        for (auto i : std::views::iota(size_t{0}, pathNum)) {
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
                if (root == std::nullopt)
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

        return std::pair{spec, std::move(postState)};
    }

    void substituteSymbols(utils::not_null<std::unique_ptr<symb::SymbolicExpr>> &expr,
                           const analyzer::Path &loopEntryPath,
                           const symb::SourcePoint &fromPoint) {
        auto &mem = loopEntryPath.getMemoryState(); // Memory snapshot at loop entry
        llvm::TypeSwitch<symb::SymbolicExpr *, void>(expr.get().get())
            .Case<symb::LiteralExpr>([&](auto *) {
                // Nothing to substitute
            })
            .Case<symb::Variable>([&](auto *var) {
                if (var->getFromPoint() && var->getFromPoint().value() != fromPoint)
                    return;

                std::visit(
                    [&](auto &&arg) -> void {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            // No origin info; not substitutable at the moment.
                            TODO();
                        } else if constexpr (std::is_same_v<T, utils::not_null<std::unique_ptr<
                                                                   const symb::Address>>>) {
                            auto realFromAddr = getSubstitutedAddr(*arg, loopEntryPath, fromPoint);
                            // Origin is an address-like handle; try reading from loop-entry memory.
                            if (auto value = mem.read(*realFromAddr)) {
                                // Replace current variable with the cloned value read from memory.
                                expr = value.value()->clone();
                            } else {
                                // Address originates from an address present on this path at loop
                                // entry but hasn't been accessed -> construct a Variable with
                                // corrext fromAddr and fromPoint.
                                expr = std::make_unique<symb::Variable>(
                                    var->getVarType(), std::move(realFromAddr).into_underlying(),
                                    loopEntryPath.getStartPoint());
                            }
                        }
                    },
                    var->getFromAddr());
            })
            .Case<symb::SymbolAddress>([&](auto *symbolAddr) {
                if (symbolAddr->getFromPoint() && symbolAddr->getFromPoint().value() != fromPoint)
                    return;
                expr = getSubstitutedAddr(*symbolAddr, loopEntryPath, fromPoint).into_underlying();
            })
            .Case<symb::BinaryOpExpr>([&](auto *bin) {
                substituteSymbols(bin->getLeft(), loopEntryPath, fromPoint);
                substituteSymbols(bin->getRight(), loopEntryPath, fromPoint);
            })
            .Case<symb::UnaryOpExpr>(
                [&](auto *un) { substituteSymbols(un->getSub(), loopEntryPath, fromPoint); })
            .Case<symb::Structure>([&](auto *st) {
                for (auto &field : st->fieldsValues()) {
                    substituteSymbols(field, loopEntryPath, fromPoint);
                }
            })
            .Case<symb::UnknownExpr>([&](auto *) {
                // Nothing to substitute
            })
            .Default([&](auto *) {
                // `VariableAddress` and `FieldAddress` should not appear in expressions.
                UNREACHABLE();
            });
    };

    utils::not_null<std::unique_ptr<symb::Address>> getSubstitutedAddr(
        const symb::Address &addr,
        const analyzer::Path &loopEntryPath,
        const symb::SourcePoint &fromPoint) {
        // If it's not a symbolic address, simply return a clone.
        if (llvm::isa<symb::VariableAddress>(addr))
            return addr.addressClone();

        auto &mem = loopEntryPath.getMemoryState();

        if (auto fieldAddr = llvm::dyn_cast<const symb::FieldAddress>(&addr)) {
            return std::visit(
                [&](auto &&arg) -> utils::not_null<std::unique_ptr<symb::Address>> {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        // No origin info — unresolved substitution.
                        TODO();
                    } else if constexpr (std::is_same_v<T, std::pair<utils::not_null<std::unique_ptr<
                                                                         const symb::Address>>,
                                                                     const size_t>>) {
                        // Substitute the base address.
                        auto &[baseAddr, index] = arg;
                        auto trueBaseAddr = getSubstitutedAddr(*baseAddr, loopEntryPath, fromPoint);
                        return std::make_unique<symb::FieldAddress>(
                            fieldAddr->getDefinition(),
                            std::pair<utils::not_null<std::unique_ptr<const symb::Address>>,
                                      const size_t>{std::move(trueBaseAddr).into_underlying(),
                                                    index});
                    }
                },
                fieldAddr->getFrom());
        }

        auto symbolAddr = llvm::dyn_cast<const symb::SymbolAddress>(&addr);
        assert(symbolAddr != nullptr && "If the addr is not a `VariableAddress` and not a "
                                        "`FieldAddress`, then it must be a `SymbolAddress`.");

        if (symbolAddr->getFromPoint() != fromPoint)
            return addr.addressClone();
        // Try to resolve the "from" origin of the SymbolAddress via loop-entry memory.
        return std::visit(
            [&](auto &&arg) -> utils::not_null<std::unique_ptr<symb::Address>> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    // No origin info — unresolved substitution.
                    TODO();
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const symb::Address>>>) {
                    auto realFromAddr = getSubstitutedAddr(*arg, loopEntryPath, fromPoint);

                    // Clone and substitute the offset of the original SymbolAddress.
                    auto offset = symbolAddr->getOffset()->clone();
                    substituteSymbols(offset, loopEntryPath,
                                      fromPoint); // substitute std::any vars/addresses in offset
                    offset = offset->simplifiedExpr();

                    std::optional<utils::not_null<std::unique_ptr<symb::SymbolicExpr>>> length{};
                    // If original was a range, also substitute and std::set the length.
                    if (symbolAddr->isRange()) {
                        length = symbolAddr->getLength()->clone();
                        substituteSymbols(length.value(), loopEntryPath, fromPoint);
                        length = length.value()->simplifiedExpr();
                    }

                    // Origin is an address; attempt to read the value at that origin.
                    if (auto value = mem.read(*realFromAddr)) {
                        // The origin resolves to a value; it must be convertible to an "offseted
                        // address".
                        auto realAddr = value.value()->tryEvalAsSymbolAddr();
                        if (realAddr == std::nullopt)
                            ERROR("This expr should be a address");

                        // Apply substituted offset to the concrete address.
                        realAddr.value()->addOffset(std::move(offset));

                        if (length) {
                            realAddr.value()->setLength(std::move(length).value());
                        }
                        // Return the underlying concrete address (std::unique_ptr<Address>).
                        return std::move(realAddr).value().into_underlying();
                    } else {
                        // The origin hasn't been accessed at loop entry -> construct a
                        // SymbolAddress with corrext fromAddr and fromPoint.
                        if (length == std::nullopt) {
                            return std::make_unique<symb::SymbolAddress>(
                                std::move(realFromAddr).into_underlying(),
                                loopEntryPath.getStartPoint(), std::move(offset).into_underlying(),
                                std::nullopt);
                        }
                        return std::make_unique<symb::SymbolAddress>(
                            std::move(realFromAddr).into_underlying(),
                            loopEntryPath.getStartPoint(), std::move(offset).into_underlying(),
                            std::move(length).value().into_underlying());
                    }
                }
            },
            symbolAddr->getFromAddr());
    };
} // namespace acslg::spec_generator