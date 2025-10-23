// src/SpecGenerator/specGenerators.h

#ifndef __ACSLG_SRC_SPECGENERATOR_SPECGENERATOR_H__
#define __ACSLG_SRC_SPECGENERATOR_SPECGENERATOR_H__

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>
#include "groups.h"
#include "Analyzer/Symbolic/expr.h"
#include "Analyzer/state.h"
#include "Utils/utils.h"

namespace aclsg::analyzer {
    class programState;
}

namespace acslg::spec_generator {

    std::string emitFunctionContract(
        const analyzer::ProgramState &pre,
        const analyzer::ProgramState &post,
        std::string_view groupName = DEFAULT_FUNC_CONTRACT_PLUGINS,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
            std::nullopt);

    struct LoopInfo {
        struct Pattern {
            utils::not_null<std::unique_ptr<const analyzer::symbolic::SymbolicExpr>> initialValue_;
            int64_t step_;
            Pattern(utils::not_null<std::unique_ptr<const analyzer::symbolic::SymbolicExpr>>
                        initialValue,
                    int64_t step)
                : initialValue_(std::move(initialValue)), step_(step) {}
            Pattern(const Pattern &other)
                : initialValue_(other.initialValue_->clone().into_underlying()),
                  step_(other.step_) {}
            Pattern &operator=(const Pattern &other);
            Pattern(Pattern &&other)            = default;
            Pattern &operator=(Pattern &&other) = default;
            std::string dump() const;
        };

        LoopInfo(const clang::Stmt *loopStmt);

        const clang::Stmt *loopStmt_;
        const clang::Stmt *initStmt_;
        const clang::Expr *condExpr_;
        const clang::Stmt *incStmt_;
        const clang::Stmt *bodyStmt_;

        // SetLoopEntryPlugin
        struct LoopEntryInfo {
            utils::not_null<std::unique_ptr<const analyzer::ProgramState>> symbolicLoopEntry_;
        };
        std::optional<LoopEntryInfo> loopEntryInfo_;

        // SetIndexPlugin
        struct IndexInfo {
            utils::not_null<const clang::Expr *> indexExpr_;
            utils::not_null<std::unique_ptr<analyzer::symbolic::Address>>
                indexRealAddr_; // index's sole address on pre-state
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
                indexSymbolicValue_; // Varibale or Address
            clang::BinaryOperator::Opcode op_;
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
                indexBound_; // exclusive bound
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>> preciseLoopCount_;
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
                maxLoopCount_; // The absolute value of the difference between the starting index
                               // and the maximum/minimum possible index.
            Pattern indexPattern_;
            bool isLocal_; // Useless, delete this.
        };
        std::optional<IndexInfo> indexInfo_;

        // SetIndexPlugin
        std::vector<const clang::Expr *>
            extraCondConjuncts_; // extraCondConjuncts holds those conjunctive
                                 // clauses extracted from the loop
        // condition that are **not** the simple index condition (e.g., i < n).

        // SetPatternsPlugin
        // Address with pattern has constant step.
        // Address with nullopt means too complex.
        // Other addresses' values hold through loop.
        struct PatternInfo {
            analyzer::symbolic::AddressBoxMap<std::optional<const Pattern>> patternsMap_;
        };
        std::optional<PatternInfo> patternInfo_;

        // TODO(more info to be added)
    };

    std::pair<LoopInfo, bool> parseLoopInfo(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const clang::Stmt *loopStmt,
        std::string_view groupName = DEFAULT_LOOP_INFO_PLUGINS,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
            std::nullopt);

    void parseComplexLoopInfo(const analyzer::ProgramState &preState,
                              const analyzer::ProgramState &loopEntry,
                              LoopInfo &loopInfo,
                              std::string_view groupName = COMPLEX_LOOP_INFO_PLUGINS,
                              std::optional<std::reference_wrapper<const std::vector<std::string>>>
                                  extraPluginIds = std::nullopt);

    std::pair<std::string, std::unique_ptr<analyzer::ProgramState>> emitLoopInvariant(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const LoopInfo &loopInfo,
        std::string_view groupName = DEFAULT_LOOP_INVARIANT_PLUGINS,
        std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
            std::nullopt);

    std::string emitInlineContract(const analyzer::ProgramState &state,
                                   std::string_view groupName,
                                   const std::vector<std::string> &extraPluginIds);

    /*---------------------------------------*/
    /*-------Framework for ACSLPlugin--------*/
    /*---------------------------------------*/

    class ACSLPlugin {
      public:
        virtual ~ACSLPlugin()               = default;
        virtual std::string_view id() const = 0;
        enum class Kind {
            FunctionContract,
            LoopInvariant,
            InlineAssertion,
            LoopInfo
        };
        virtual Kind kind() const = 0;
    };

    class FunctionContractPlugin : public ACSLPlugin {
      public:
        Kind kind() const override { return Kind::FunctionContract; }
        virtual std::optional<std::string> generate(const analyzer::ProgramState &pre,
                                                    const analyzer::ProgramState &post) const = 0;
    };

    class LoopInfoPlugin : public ACSLPlugin {
      public:
        Kind kind() const override { return Kind::LoopInfo; }

        /// @brief Parse the given loop and fill in loopInfo.
        /// @param preState
        /// @param loopEntry
        /// @param loopEntryPoint
        /// @param loopInfo info to be filled in
        /// @return return false means this loop is too complex and will abort whole parsing!
        virtual bool parse(const analyzer::ProgramState &preState,
                           const analyzer::ProgramState &loopEntry,
                           LoopInfo &loopInfo) const = 0;
    };

    struct PostInfo {
        analyzer::symbolic::AddressBoxMap<
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>>
            memoryMap_;
        std::vector<utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> pathConds_;
    };
    class LoopInvariantPlugin : public ACSLPlugin {
      public:
        Kind kind() const override { return Kind::LoopInvariant; }

        virtual std::tuple<std::optional<std::string>, bool, std::vector<PostInfo>> generate(
            const analyzer::ProgramState &preState,
            const analyzer::ProgramState &loopEntry,
            const LoopInfo &loopInfo) const = 0;
    };

    class InlinePlugin : public ACSLPlugin {
      public:
        Kind kind() const override { return Kind::InlineAssertion; }
        virtual std::optional<std::string> generate(
            const analyzer::ProgramState &state /* enough? */) = 0;
    };

    class ACSLPluginRegistry {
      public:
        static ACSLPluginRegistry &instance() {
            static ACSLPluginRegistry R;
            return R;
        }

        void registerPlugin(std::unique_ptr<ACSLPlugin> P) {
            plugins_.emplace(P->id(), std::move(P));
        }

        ACSLPlugin *get(std::string_view id) const {
            auto it = plugins_.find(id);
            return it == plugins_.end() ? nullptr : it->second.get();
        }

        std::vector<std::string> idsByKind(ACSLPlugin::Kind k) const {
            std::vector<std::string> v;
            for (auto const &p : plugins_)
                if (p.second->kind() == k)
                    v.push_back(p.first);
            return v;
        }

      private:
        std::unordered_map<std::string,
                           std::unique_ptr<ACSLPlugin>,
                           utils::TransparentStringHash,
                           utils::TransparentStringEqual>
            plugins_;
    };

#define REGISTER_ACSL_PLUGIN(PluginType, PluginID)                                                 \
    namespace {                                                                                    \
        struct PluginType##Reg {                                                                   \
            PluginType##Reg() {                                                                    \
                ACSLPluginRegistry::instance().registerPlugin(                                     \
                    std::make_unique<PluginType>(PluginID));                                       \
                INFO(#PluginType " is registered with id: " #PluginID);                            \
            }                                                                                      \
        } _##PluginType##Reg;                                                                      \
    }

    struct ACSLPluginGroup {
        std::string name;
        std::vector<std::string> pluginIds;
    };

    class ACSLPluginGroupRegistry {
      public:
        static ACSLPluginGroupRegistry &instance() {
            static ACSLPluginGroupRegistry R;
            return R;
        }

        void registerGroup(ACSLPluginGroup G) { groups_.emplace(G.name, std::move(G)); }

        const ACSLPluginGroup *getGroup(std::string_view name) const {
            auto it = groups_.find(name);
            return it == groups_.end() ? nullptr : &it->second;
        }

        std::vector<std::string_view> allGroupNames() const {
            std::vector<std::string_view> v;
            for (auto &[name, _] : groups_)
                v.push_back(name);
            return v;
        }

      private:
        std::unordered_map<std::string,
                           ACSLPluginGroup,
                           utils::TransparentStringHash,
                           utils::TransparentStringEqual>
            groups_;
    };

#define REGISTER_ACSL_GROUP(GroupName, ...)                                                        \
    namespace {                                                                                    \
        struct GroupName##Reg {                                                                    \
            GroupName##Reg() {                                                                     \
                ACSLPluginGroup G;                                                                 \
                G.name      = #GroupName;                                                          \
                G.pluginIds = {__VA_ARGS__};                                                       \
                ACSLPluginGroupRegistry::instance().registerGroup(G);                              \
                for (auto &id : G.pluginIds) {                                                     \
                    if (ACSLPluginRegistry::instance().get(id) == nullptr) {                       \
                        ERROR("Plugin with id " + id + " does not exist.");                        \
                    }                                                                              \
                }                                                                                  \
                INFO(#GroupName " is registered.");                                                \
            }                                                                                      \
        } _##GroupName##Reg;                                                                       \
    }
} // namespace acslg::spec_generator

#endif