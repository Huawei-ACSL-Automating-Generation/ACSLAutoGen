/**
 * @file specGenerator.h
 * @brief Declares ACSL specification generators, plugin interfaces, and loop information helpers.
 */

#ifndef __ACSLG_SRC_SPECGENERATOR_SPECGENERATOR_H__
#define __ACSLG_SRC_SPECGENERATOR_SPECGENERATOR_H__

#include <string>
#include <optional>
#include <unordered_set>
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

namespace acslg::spec_generator {
    namespace detail {
        inline analyzer::symbolic::ExprHandle importPostExprThroughCurrentFactory(
            const analyzer::symbolic::SymbolicExpr &expr) {
            return analyzer::symbolic::ExprFactoryScope::current().importExpr(expr);
        }
    } // namespace detail

    using PostMemoryMap =
        analyzer::symbolic::AddressBoxMap<analyzer::symbolic::ExprHandle>;

    /**
     * @brief Emit an ACSL function contract using registered plugins.
     * @param pre [in] Program state before function execution.
     * @param post [in] Program state after symbolic execution.
     * @param groupName [in] Plugin group identifier controlling which generators run.
     * @return Pair of ACSL text and set of SourcePoints used in the contract.
     */
    std::pair<std::string, std::unordered_set<analyzer::symbolic::SourcePoint>> emitFunctionContract(
        const analyzer::ProgramState &pre,
        const analyzer::ProgramState &post,
        std::string_view groupName = DEFAULT_FUNC_CONTRACT_PLUGINS);

    /**
     * @struct LoopInfo
     * @brief Captures parsed structural and symbolic information about a loop.
     *
     * LoopInfo acts as a scratchpad populated by loop info plugins and then consumed by invariant
     * generators to emit ACSL loop annotations.
     */
    struct LoopInfo {
        struct Pattern {
            utils::not_null<std::unique_ptr<const analyzer::symbolic::SymbolicExpr>> initialValue;
            int64_t step;
            /**
             * @brief Construct a pattern with initial symbolic value and fixed step.
             * @param init [in] Initial symbolic value for the pattern.
             * @param st [in] Step amount applied each iteration.
             */
            Pattern(utils::not_null<std::unique_ptr<const analyzer::symbolic::SymbolicExpr>> init,
                    int64_t st)
                : initialValue(std::move(init)), step(st) {}
            /// @brief Copy-construct with deep-cloned symbolic value.
            Pattern(const Pattern &other);
            Pattern &operator=(const Pattern &other);
            Pattern(Pattern &&other)            = default;
            Pattern &operator=(Pattern &&other) = default;
            /**
             * @brief Dump the pattern to a human-readable string.
             * @return String describing initial value and step.
             */
            std::string dump() const;
        };

        /**
         * @brief Construct loop metadata from the underlying loop statement.
         * @param ls [in] Loop statement (for/while/do-while) to analyze.
         */
        LoopInfo(const clang::Stmt *ls);

        const clang::Stmt *loopStmt;
        const clang::Stmt *initStmt;
        const clang::Expr *condExpr;
        const clang::Stmt *incStmt;
        const clang::Stmt *bodyStmt;

        // SetEntryAndCurrentPlugin
        struct EntryAndCurrentInfo {
            utils::not_null<std::unique_ptr<const analyzer::ProgramState>> symbolicLoopEntry;
            utils::not_null<std::unique_ptr<const analyzer::ProgramState>> symbolicLoopCurrent;
            std::vector<utils::not_null<std::unique_ptr<analyzer::Path>>> inactivePaths;
            analyzer::symbolic::SourcePoint loopEntryPoint;
        };
        std::optional<EntryAndCurrentInfo> entryAndCurrentInfo;

        // SetIndexPlugin
        struct IndexInfo {
            utils::not_null<const clang::Expr *> indexExpr;
            utils::not_null<std::unique_ptr<analyzer::symbolic::Address>>
                indexRealAddr; // index's sole address on pre-state
            utils::not_null<std::unique_ptr<analyzer::symbolic::Address>> indexSymbolicAddr;
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
                indexSymbolicValue; // Varibale or Address
            clang::BinaryOperator::Opcode op;
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
                indexBound; // exclusive bound
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>> preciseLoopCount;
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>
                maxLoopCount; // The absolute value of the difference between the starting index
                              // and the maximum/minimum possible index.
            Pattern indexPattern;
            bool isLocal; // Useless, delete this.
        };
        std::optional<IndexInfo> indexInfo;
        std::vector<const clang::Expr *>
            extraCondConjuncts; // extraCondConjuncts holds those conjunctive
                                // clauses extracted from the loop
        // condition that are **not** the simple index condition (e.g., i < n).

        // SetPatternsPlugin
        // Address with pattern has constant step.
        // Address with nullopt means too complex.
        // Other addresses' values hold through loop.
        struct PatternInfo {
            analyzer::symbolic::AddressBoxMap<std::optional<const Pattern>> normalExitPatternsMap;
            std::vector<analyzer::symbolic::AddressBoxMap<std::optional<const Pattern>>>
                interruptedPathPatternsMaps;
            analyzer::symbolic::AddressBoxMap<std::optional<const Pattern>> allPatternsMap;
        };
        std::optional<PatternInfo> patternInfo;

        // SetSharedStatePlugin
        std::optional<analyzer::symbolic::AddressBoxMap<
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>>>
            sharedMemoryMap;
        std::optional<analyzer::PathConditions> sharedPathConds;

        // TODO(more info to be added)
    };

    /**
     * @brief Parse loop information using the configured loop info plugin group.
     * @param preState [in] Program state before entering the loop.
     * @param loopEntry [in] Program state at loop entry.
     * @param loopStmt [in] Loop statement being analyzed.
     * @param groupName [in] Plugin group identifier.
     * @return Pair of populated LoopInfo and success flag (false when parsing aborts).
     */
    std::pair<LoopInfo, bool> parseLoopInfo(const analyzer::ProgramState &preState,
                                            const analyzer::ProgramState &loopEntry,
                                            const clang::Stmt *loopStmt,
                                            std::string_view groupName = DEFAULT_LOOP_INFO_PLUGINS);

    /**
     * @brief Parse additional loop information for complex loops (no early abort).
     * @param preState [in] Program state before the loop.
     * @param loopEntry [in] Program state at loop entry.
     * @param loopInfo [in,out] LoopInfo to be enriched by plugins.
     * @param groupName [in] Plugin group identifier to run.
     */
    void parseComplexLoopInfo(const analyzer::ProgramState &preState,
                              const analyzer::ProgramState &loopEntry,
                              LoopInfo &loopInfo,
                              std::string_view groupName = COMPLEX_LOOP_INFO_PLUGINS);

    struct EmitLoopInvResult {
        std::string acsl;
        std::unordered_set<analyzer::symbolic::SourcePoint> usedPoints;
        std::unique_ptr<analyzer::ProgramState> postState;
    };
    /**
     * @brief Emit ACSL loop invariant/assigns/variant clauses using registered plugins.
     * @param preState [in] State before entering the loop.
     * @param loopEntry [in] State representing loop entry.
     * @param loopInfo [in] Parsed loop metadata.
     * @param piGroupName [in] Path-insensitive plugin group to run.
     * @param psGroupName [in] Path-sensitive plugin group to run.
     * @return Generated ACSL text, used labels, and a synthesized post-state.
     */
    EmitLoopInvResult emitLoopInvariant(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const LoopInfo &loopInfo,
        std::string_view piGroupName = DEFAULT_PATH_INSENSITIVE_LOOP_INV_PLUGINS,
        std::string_view psGroupName = DEFAULT_PATH_SENSITIVE_LOOP_INV_PLUGINS);

    /**
     * @brief Emit inline ACSL annotations for the given program state.
     * @param state [in] Program state to describe.
     * @param groupName [in] Plugin group for inline contracts.
     * @return Generated ACSL string.
     */
    std::string emitInlineContract(const analyzer::ProgramState &state, std::string_view groupName);

    /*---------------------------------------*/
    /*-------Framework for ACSLPlugin--------*/
    /*---------------------------------------*/

    /**
     * @class ACSLPlugin
     * @brief Base interface for all ACSL generation plugins.
     *
     * Plugins are grouped by kind and invoked by registries to produce contracts, loop info, or
     * inline annotations.
     */
    class ACSLPlugin {
      public:
        virtual ~ACSLPlugin()               = default;
        virtual std::string_view id() const = 0;
        enum class Kind {
            K_FuncPlugin,
            K_LoopInfoPlugin,
            K_PILoopInvPlugin,
            K_PSLoopInvPlugin,
            K_InlinePlugin,
        };
        Kind getKind() const { return kind_; }
        static bool classof(const ACSLPlugin *) { return true; }

      protected:
        ACSLPlugin(Kind kind) : kind_(kind) {}

      private:
        Kind kind_;
    };

    /**
     * @class FunctionContractPlugin
     * @brief Plugin interface for generating function-level ACSL contracts.
     */
    class FunctionContractPlugin : public ACSLPlugin {
      public:
        static bool classof(const ACSLPlugin *plugin) {
            return plugin->getKind() == Kind::K_FuncPlugin;
        }
        using GenResultType = std::pair<std::optional<std::string>,
                                        std::unordered_set<analyzer::symbolic::SourcePoint>>;
        virtual GenResultType generate(const analyzer::ProgramState &pre,
                                       const analyzer::ProgramState &post) const = 0;

      protected:
        FunctionContractPlugin() : ACSLPlugin(Kind::K_FuncPlugin) {};
    };

    /**
     * @class LoopInfoPlugin
     * @brief Plugin interface for extracting structural and symbolic loop properties.
     */
    class LoopInfoPlugin : public ACSLPlugin {
      public:
        static bool classof(const ACSLPlugin *plugin) {
            return plugin->getKind() == Kind::K_LoopInfoPlugin;
        }
        /// @brief Parse the given loop and fill in loopInfo.
        /// @param preState
        /// @param loopEntry
        /// @param loopEntryPoint
        /// @param loopInfo info to be filled in
        /// @return return false means this loop is too complex and will abort whole parsing!
        virtual bool parse(const analyzer::ProgramState &preState,
                           const analyzer::ProgramState &loopEntry,
                           LoopInfo &loopInfo) const = 0;

      protected:
        LoopInfoPlugin() : ACSLPlugin(Kind::K_LoopInfoPlugin) {};
    };

    struct PostPIInfo {
        PostMemoryMap memoryMap;
        analyzer::PathConditions pathConds;

        PostPIInfo(PostMemoryMap mem, analyzer::PathConditions pcs)
            : memoryMap(std::move(mem)), pathConds(std::move(pcs)) {}

        PostPIInfo(const PostPIInfo &other) {
            for (const auto &kv : other.memoryMap) {
                const auto &addr  = kv.first;
                const auto &exprp = kv.second;
                memoryMap.emplace(addr, detail::importPostExprThroughCurrentFactory(*exprp));
            }

            pathConds.reserve(other.pathConds.size());
            for (const auto &expr : other.pathConds)
                pathConds.emplace(detail::importPostExprThroughCurrentFactory(*expr));
        }

        PostPIInfo()                       = default;
        PostPIInfo(PostPIInfo &&) noexcept = default;
    };
    /**
     * @class PathInsensitiveLoopInvPlugin
     * @brief Plugin interface for generating loop invariants without path distinction.
     */
    class PathInsensitiveLoopInvPlugin : public ACSLPlugin {
      public:
        static bool classof(const ACSLPlugin *plugin) {
            return plugin->getKind() == Kind::K_PILoopInvPlugin;
        }
        struct GenResultType {
            std::optional<std::string> acsl;
            std::unordered_set<analyzer::symbolic::SourcePoint> acslUsedPoints;
            PostPIInfo globalNormalPathPostInfo;
            std::vector<PostPIInfo> globalInterruptPathsPostInfo;
        };
        virtual GenResultType generate(const analyzer::ProgramState &preState,
                                       const analyzer::ProgramState &loopEntry,
                                       const LoopInfo &loopInfo) const = 0;

      protected:
        PathInsensitiveLoopInvPlugin() : ACSLPlugin(Kind::K_PILoopInvPlugin) {};
    };

    struct PostPSInfo {
        PostMemoryMap memoryMap;
        analyzer::PathConditions pathConds;
        analyzer::Path::PathState pathState;
        std::optional<analyzer::symbolic::ExprHandle> returnExpr;

        PostPSInfo(
            PostMemoryMap mem,
            analyzer::PathConditions pcs,
            analyzer::Path::PathState ps,
            std::optional<analyzer::symbolic::ExprHandle> re)
            : memoryMap(std::move(mem)), pathConds(std::move(pcs)), pathState(ps),
              returnExpr(std::move(re)) {}

        PostPSInfo(const PostPSInfo &other) : pathState(other.pathState), returnExpr(std::nullopt) {
            for (const auto &kv : other.memoryMap) {
                const auto &addr = kv.first;
                const auto &expr = kv.second;
                memoryMap.emplace(addr, detail::importPostExprThroughCurrentFactory(*expr));
            }

            pathConds.reserve(other.pathConds.size());
            for (const auto &expr : other.pathConds)
                pathConds.emplace(detail::importPostExprThroughCurrentFactory(*expr));

            if (other.returnExpr)
                returnExpr = detail::importPostExprThroughCurrentFactory(*other.returnExpr.value());
        }

        PostPSInfo()                       = default;
        PostPSInfo(PostPSInfo &&) noexcept = default;
    };
    /**
     * @class PathSensitiveLoopInvPlugin
     * @brief Plugin interface for generating loop invariants that differentiate execution paths.
     */
    class PathSensitiveLoopInvPlugin : public ACSLPlugin {
      public:
        static bool classof(const ACSLPlugin *plugin) {
            return plugin->getKind() == Kind::K_PSLoopInvPlugin;
        }
        struct GenResultType {
            std::optional<std::string> acsl;
            std::unordered_set<analyzer::symbolic::SourcePoint> acslUsedPoints;
            std::vector<PostPSInfo> normalPathPostInfos;
            std::vector<std::vector<PostPSInfo>> interruptPathsPostInfos;
        };

        // todo: may pass pass in some loop information to help the plugin determine whether it
        // can handle the request and its priority.
        virtual size_t propose() const                                                   = 0;
        virtual std::optional<GenResultType> tryGenerate(const analyzer::ProgramState &preState,
                                                         const analyzer::ProgramState &loopEntry,
                                                         const LoopInfo &loopInfo) const = 0;

      protected:
        PathSensitiveLoopInvPlugin() : ACSLPlugin(Kind::K_PSLoopInvPlugin) {};
    };

    /**
     * @class InlinePlugin
     * @brief Plugin interface for generating inline ACSL annotations at specific program points.
     */
    class InlinePlugin : public ACSLPlugin {
        static bool classof(const ACSLPlugin *plugin) {
            return plugin->getKind() == Kind::K_InlinePlugin;
        }

      public:
        virtual std::optional<std::string> generate(
            const analyzer::ProgramState &state /* enough? */) = 0;

      protected:
        InlinePlugin() : ACSLPlugin(Kind::K_InlinePlugin) {};
    };

    /**
     * @class ACSLPluginRegistry
     * @brief Singleton registry that stores plugin instances by ID.
     */
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

    /**
     * @struct ACSLPluginGroup
     * @brief Named collection of plugin IDs invoked together.
     */
    struct ACSLPluginGroup {
        std::string name;
        std::vector<std::string> pluginIds;
    };

    /**
     * @class ACSLPluginGroupRegistry
     * @brief Registry mapping group names to lists of plugin IDs.
     */
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
