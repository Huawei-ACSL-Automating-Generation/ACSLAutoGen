// src/SpecGenerator/specGenerators.h

#ifndef SPEC_GENERATOR_H
#define SPEC_GENERATOR_H

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>
#include "groups.h"
#include "Analyzer/Symbolic/expr.h"
#include "Analyzer/state.h"
#include "Utils/utils.h"

class ProgramState;

std::string emitFunctionContract(
    const ProgramState &pre,
    const ProgramState &post,
    std::string_view groupName = DEFAULT_FUNC_CONTRACT_PLUGINS,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
        std::nullopt);

struct LoopInfo {
    struct Pattern {
        not_null<std::unique_ptr<const Symbolic::SymbolicExpr>> initialValue_;
        int64_t step_;
        Pattern(not_null<std::unique_ptr<const Symbolic::SymbolicExpr>> initialValue, int64_t step)
            : initialValue_(std::move(initialValue)), step_(step) {}
        Pattern(const Pattern &other)
            : initialValue_(other.initialValue_->clone().into_underlying()), step_(other.step_) {}
        Pattern &operator=(const Pattern &other) {
            if (this == &other)
                return *this;
            initialValue_ = other.initialValue_->clone().into_underlying();
            step_         = other.step_;
            return *this;
        }
        Pattern(Pattern &&other)            = default;
        Pattern &operator=(Pattern &&other) = default;
        string dump() const {
            ostringstream oss;
            oss << "initialValue_: "
                << (initialValue_->regularForm() ? initialValue_->regularForm().value()
                                                 : initialValue_->dump())
                << "\n";
            oss << "step_: " << to_string(step_) << "\n";
            return oss.str();
        }
    };

    // SetLoopEntryPlugin
    struct LoopEntryInfo {
        not_null<std::unique_ptr<const ProgramState>> symbolicLoopEntry_;
    };
    optional<LoopEntryInfo> loopEntryInfo_;

    // SetIndexPlugin
    struct IndexInfo {
        not_null<std::unique_ptr<Symbolic::Address>> indexAddr_;
        not_null<std::unique_ptr<Symbolic::SymbolicExpr>> indexSymbolicValue_; // Varibale or Address
        clang::BinaryOperator::Opcode op_;
        not_null<std::unique_ptr<Symbolic::SymbolicExpr>> indexBound_; // exclusive bound
        not_null<std::unique_ptr<Symbolic::SymbolicExpr>> preciseLoopCount_;
        not_null<std::unique_ptr<Symbolic::SymbolicExpr>> maxLoopCount_;
        Pattern indexPattern_;
        bool isLocal_; // Useless, delete this.
    };
    optional<IndexInfo> indexInfo_;

    // SetPatternsPlugin
    // Address with pattern has constant step.
    // Address with nullopt means too complex.
    // Other addresses' values hold through loop.
    struct PatternInfo {
        Symbolic::AddressBoxMap<std::optional<const Pattern>> patternsMap_;
    };
    optional<PatternInfo> patternInfo_;

    // TODO(more info to be added)
};

std::pair<LoopInfo, bool> parseLoopInfo(
    const ProgramState &preState,
    const ProgramState &loopEntry,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body,
    std::string_view groupName = DEFAULT_LOOP_INFO_PLUGINS,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
        std::nullopt);

void parseComplexLoopInfo(
    const ProgramState &preState,
    const ProgramState &loopEntry,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body,
    LoopInfo &loopInfo,
    std::string_view groupName                                       = COMPLEX_LOOP_INFO_PLUGINS,
    optional<reference_wrapper<const vector<string>>> extraPluginIds = std::nullopt);

std::pair<std::string, unique_ptr<ProgramState>> emitLoopInvariant(
    const ProgramState &preState,
    const ProgramState &loopEntry,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body,
    const LoopInfo &loopInfo,
    std::string_view groupName = DEFAULT_LOOP_INVARIANT_PLUGINS,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
        std::nullopt);

std::string emitInlineContract(const ProgramState &state,
                               std::string_view groupName,
                               const std::vector<std::string> &extraPluginIds);

void substituteSymbols(not_null<std::unique_ptr<SymbolicExpr>> &expr, const Path &loopEntryPath);
not_null<std::unique_ptr<Address>> getSubstitutedAddr(const Address &addr,
                                                      const Path &loopEntryPath);

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
    virtual std::optional<std::string> generate(const ProgramState &pre,
                                                const ProgramState &post) const = 0;
};

class LoopInfoPlugin : public ACSLPlugin {
  public:
    Kind kind() const override { return Kind::LoopInfo; }

    /// @brief Parse the given loop and fill in loopInfo.
    /// @param loopEntry
    /// @param cond
    /// @param inc
    /// @param body
    /// @param loopInfo info to be filled in
    /// @return return false means this loop is too complex and will abort whole parsing!
    virtual bool parse(const ProgramState &preState,
                       const ProgramState &loopEntry,
                       const clang::Expr *cond,
                       const clang::Stmt *inc,
                       const clang::Stmt *body,
                       LoopInfo &loopInfo) const = 0;
};

struct PostInfo {
    Symbolic::AddressBoxMap<not_null<std::unique_ptr<SymbolicExpr>>> memoryMap_;
    vector<not_null<unique_ptr<SymbolicExpr>>> pathConds_;
};
class LoopInvariantPlugin : public ACSLPlugin {
  public:
    Kind kind() const override { return Kind::LoopInvariant; }

    virtual std::tuple<std::optional<std::string>, bool, std::vector<PostInfo>> generate(
        const ProgramState &preState,
        const ProgramState &loopEntry,
        const clang::Expr *cond,
        const clang::Stmt *inc,
        const clang::Stmt *body,
        const LoopInfo &loopInfo) const = 0;
};

class InlinePlugin : public ACSLPlugin {
  public:
    Kind kind() const override { return Kind::InlineAssertion; }
    virtual std::optional<std::string> generate(const ProgramState &state /* enough? */) = 0;
};

class ACSLPluginRegistry {
  public:
    static ACSLPluginRegistry &instance() {
        static ACSLPluginRegistry R;
        return R;
    }

    void registerPlugin(std::unique_ptr<ACSLPlugin> P) { plugins_.emplace(P->id(), std::move(P)); }

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
                       TransparentStringHash,
                       TransparentStringEqual>
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
    std::unordered_map<std::string, ACSLPluginGroup, TransparentStringHash, TransparentStringEqual>
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

#endif