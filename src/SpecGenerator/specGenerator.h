// src/SpecGenerator/specGenerators.h

#ifndef SPEC_GENERATOR_H
#define SPEC_GENERATOR_H

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>

class ProgramState;

std::string emitFunctionContract(const ProgramState &pre,
    const ProgramState &post,
    const std::string &groupName,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
        std::nullopt);

struct LoopPattern
{};

std::optional<LoopPattern> getLoopPattern(const clang::Stmt *init,
    const clang::Expr *cond,
    const clang::Stmt *inc,
    const clang::Stmt *body);

std::string emitLoopInvariantContract(const ProgramState &concretePre,
    const ProgramState &symbolicPre,
    const ProgramState &symbolicPost,
    const LoopPattern &pattern,
    const std::string &groupName,
    std::optional<std::reference_wrapper<const std::vector<std::string>>> extraPluginIds =
        std::nullopt);

std::string emitInlineContract(const ProgramState &state,
    const std::string &groupName,
    const std::vector<std::string> &extraPluginIds);

/*---------------------------------------*/
/*-------Framework for ACSLPlugin--------*/
/*---------------------------------------*/

class ACSLPlugin
{
  public:
    virtual ~ACSLPlugin()          = default;
    virtual std::string id() const = 0;
    enum class Kind
    {
        FunctionContract,
        LoopInvariant,
        InlineAssertion,
        LoopPattern
    };
    virtual Kind kind() const = 0;
};

class FunctionContractPlugin : public ACSLPlugin
{
  public:
    Kind kind() const override { return Kind::FunctionContract; }
    virtual std::optional<std::string>
    generate(const ProgramState &pre, const ProgramState &post) = 0;
};

class LoopPatternPlugin : public ACSLPlugin
{
  public:
    Kind kind() const override { return Kind::LoopPattern; }
    virtual std::optional<LoopPattern>
    parse(clang::Stmt *init, clang::Expr *cond, clang::Stmt *inc, clang::Stmt *body) = 0;
};

class LoopInvariantPlugin : public ACSLPlugin
{
  public:
    Kind kind() const override { return Kind::LoopInvariant; }
    virtual std::optional<std::string> generate(/* TODO */) = 0;
};

class InlinePlugin : public ACSLPlugin
{
  public:
    Kind kind() const override { return Kind::InlineAssertion; }
    virtual std::optional<std::string> generate(const ProgramState &state /* enough? */) = 0;
};

class ACSLPluginRegistry
{
  public:
    static ACSLPluginRegistry &instance()
    {
        static ACSLPluginRegistry R;
        return R;
    }

    void registerPlugin(std::unique_ptr<ACSLPlugin> P) { plugins_.emplace(P->id(), std::move(P)); }

    ACSLPlugin *get(const std::string &id) const
    {
        auto it = plugins_.find(id);
        return it == plugins_.end() ? nullptr : it->second.get();
    }

    std::vector<std::string> idsByKind(ACSLPlugin::Kind k) const
    {
        std::vector<std::string> v;
        for (auto const &p : plugins_)
            if (p.second->kind() == k)
                v.push_back(p.first);
        return v;
    }

  private:
    std::unordered_map<std::string, std::unique_ptr<ACSLPlugin>> plugins_;
};

#define REGISTER_ACSL_PLUGIN(PluginType, PluginID)                                                 \
    namespace                                                                                      \
    {                                                                                              \
        struct PluginType##Reg                                                                     \
        {                                                                                          \
            PluginType##Reg()                                                                      \
            {                                                                                      \
                ACSLPluginRegistry::instance().registerPlugin(                                     \
                    std::make_unique<PluginType>(PluginID));                                       \
                INFO(#PluginType "is registered with id: " #PluginID);                             \
            }                                                                                      \
        } _##PluginType##Reg;                                                                      \
    }

struct ACSLPluginGroup
{
    std::string name;
    std::vector<std::string> pluginIds;
};

class ACSLPluginGroupRegistry
{
  public:
    static ACSLPluginGroupRegistry &instance()
    {
        static ACSLPluginGroupRegistry R;
        return R;
    }

    void registerGroup(ACSLPluginGroup G) { groups_.emplace(G.name, std::move(G)); }

    const ACSLPluginGroup *getGroup(const std::string &name) const
    {
        auto it = groups_.find(name);
        return it == groups_.end() ? nullptr : &it->second;
    }

    std::vector<std::string> allGroupNames() const
    {
        std::vector<std::string> v;
        for (auto &kv : groups_)
            v.push_back(kv.first);
        return v;
    }

  private:
    std::unordered_map<std::string, ACSLPluginGroup> groups_;
};

#define REGISTER_ACSL_GROUP(GroupName, ...)                                                        \
    namespace                                                                                      \
    {                                                                                              \
        struct GroupName##Reg                                                                      \
        {                                                                                          \
            GroupName##Reg()                                                                       \
            {                                                                                      \
                ACSLPluginGroup G;                                                                 \
                G.name      = #GroupName;                                                          \
                G.pluginIds = {__VA_ARGS__};                                                       \
                ACSLPluginGroupRegistry::instance().registerGroup(G);                              \
                INFO(#GroupName " is registered.");                                                \
            }                                                                                      \
        } _##GroupName##Reg;                                                                       \
    }

#endif