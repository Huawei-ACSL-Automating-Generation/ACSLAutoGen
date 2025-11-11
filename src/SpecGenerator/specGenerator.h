// src/SpecGenerator/specGenerators.h

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

namespace aclsg::analyzer {
    class programState;
}

namespace acslg::spec_generator {
    std::pair<std::string, std::unordered_set<analyzer::symbolic::SourcePoint>> emitFunctionContract(
        const analyzer::ProgramState &pre,
        const analyzer::ProgramState &post,
        std::string_view groupName = DEFAULT_FUNC_CONTRACT_PLUGINS);

    struct LoopInfo {
        struct Pattern {
            utils::not_null<std::unique_ptr<const analyzer::symbolic::SymbolicExpr>> initialValue;
            int64_t step;
            Pattern(utils::not_null<std::unique_ptr<const analyzer::symbolic::SymbolicExpr>> init,
                    int64_t st)
                : initialValue(std::move(init)), step(st) {}
            Pattern(const Pattern &other)
                : initialValue(other.initialValue->clone().into_underlying()), step(other.step) {}
            Pattern &operator=(const Pattern &other);
            Pattern(Pattern &&other)            = default;
            Pattern &operator=(Pattern &&other) = default;
            std::string dump() const;
        };

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

        // TODO(more info to be added)
    };

    std::pair<LoopInfo, bool> parseLoopInfo(const analyzer::ProgramState &preState,
                                            const analyzer::ProgramState &loopEntry,
                                            const clang::Stmt *loopStmt,
                                            std::string_view groupName = DEFAULT_LOOP_INFO_PLUGINS);

    void parseComplexLoopInfo(const analyzer::ProgramState &preState,
                              const analyzer::ProgramState &loopEntry,
                              LoopInfo &loopInfo,
                              std::string_view groupName = COMPLEX_LOOP_INFO_PLUGINS);

    struct EmitLoopInvResult {
        std::string acsl;
        std::unordered_set<analyzer::symbolic::SourcePoint> usedPoints;
        std::unique_ptr<analyzer::ProgramState> postState;
    };
    EmitLoopInvResult emitLoopInvariant(
        const analyzer::ProgramState &preState,
        const analyzer::ProgramState &loopEntry,
        const LoopInfo &loopInfo,
        std::string_view piGroupName = DEFAULT_PATH_INSENSITIVE_LOOP_INV_PLUGINS,
        std::string_view psGroupName = DEFAULT_PATH_SENSITIVE_LOOP_INV_PLUGINS);

    std::string emitInlineContract(const analyzer::ProgramState &state, std::string_view groupName);

    /*---------------------------------------*/
    /*-------Framework for ACSLPlugin--------*/
    /*---------------------------------------*/

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
        analyzer::symbolic::AddressBoxMap<
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>>
            memoryMap;
        std::vector<utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> pathConds;

        PostPIInfo(
            analyzer::symbolic::AddressBoxMap<
                utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> mem,
            std::vector<utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> pcs)
            : memoryMap(std::move(mem)), pathConds(std::move(pcs)) {}

        PostPIInfo(const PostPIInfo &other) {
            for (const auto &kv : other.memoryMap) {
                const auto &addr  = kv.first;
                const auto &exprp = kv.second;
                auto cloned       = exprp->clone();
                memoryMap.emplace(addr, utils::not_null{std::move(cloned)});
            }

            pathConds.reserve(other.pathConds.size());
            for (const auto &exprp : other.pathConds) {
                auto cloned = exprp->clone();
                pathConds.push_back(utils::not_null{std::move(cloned)});
            }
        }

        PostPIInfo()                       = default;
        PostPIInfo(PostPIInfo &&) noexcept = default;
    };
    class PathInsensitiveLoopInvPlugin : public ACSLPlugin {
      public:
        static bool classof(const ACSLPlugin *plugin) {
            return plugin->getKind() == Kind::K_PILoopInvPlugin;
        }
        struct GenResultType {
            std::optional<std::string> acsl;
            std::unordered_set<analyzer::symbolic::SourcePoint> acslUsedPoints;
            PostPIInfo globalPostInfo;
        };
        virtual GenResultType generate(const analyzer::ProgramState &preState,
                                       const analyzer::ProgramState &loopEntry,
                                       const LoopInfo &loopInfo) const = 0;

      protected:
        PathInsensitiveLoopInvPlugin() : ACSLPlugin(Kind::K_PILoopInvPlugin) {};
    };

    struct PostPSInfo {
        analyzer::symbolic::AddressBoxMap<
            utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>>
            memoryMap;
        std::vector<utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> pathConds;
        analyzer::Path::PathState pathState;
        std::optional<utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> returnExpr;

        PostPSInfo(
            analyzer::symbolic::AddressBoxMap<
                utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> mem,
            std::vector<utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> pcs,
            analyzer::Path::PathState ps,
            std::optional<utils::not_null<std::unique_ptr<analyzer::symbolic::SymbolicExpr>>> re)
            : memoryMap(std::move(mem)), pathConds(std::move(pcs)), pathState(ps),
              returnExpr(std::move(re)) {}

        PostPSInfo(const PostPSInfo &other) : pathState(other.pathState), returnExpr(std::nullopt) {
            for (const auto &kv : other.memoryMap) {
                const auto &addr = kv.first;
                const auto &expr = kv.second;
                auto cloned      = expr->clone();
                memoryMap.emplace(addr, utils::not_null{std::move(cloned)});
            }

            pathConds.reserve(other.pathConds.size());
            for (const auto &expr : other.pathConds) {
                auto cloned = expr->clone();
                pathConds.push_back(utils::not_null{std::move(cloned)});
            }

            if (other.returnExpr)
                returnExpr = other.returnExpr.value()->clone();
        }

        PostPSInfo()                       = default;
        PostPSInfo(PostPSInfo &&) noexcept = default;
    };
    class PathSensitiveLoopInvPlugin : public ACSLPlugin {
      public:
        static bool classof(const ACSLPlugin *plugin) {
            return plugin->getKind() == Kind::K_PSLoopInvPlugin;
        }
        struct GenResultType {
            std::optional<std::string> acsl;
            std::unordered_set<analyzer::symbolic::SourcePoint> acslUsedPoints;
            std::vector<PostPSInfo> perPathPostInfos;
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
