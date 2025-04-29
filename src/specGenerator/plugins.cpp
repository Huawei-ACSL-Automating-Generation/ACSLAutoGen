// src/specGenerator/plugins.cpp

#include "specGenerator.h"
#include "stringTemplate.h"

class AssignPlugin : public FunctionContractPlugin
{
  public:
    AssignPlugin(const std::string &ID) : id_(ID) {}
    std::string id() const override { return id_; }
    std::string generate(const ProgramState &, const ProgramState &) override
    {
        // TODO
        return "//@ assigns ...;\n";
    }

  private:
    std::string id_;
};
REGISTER_ACSL_PLUGIN(AssignPlugin, "assign");

class ResultPlugin : public FunctionContractPlugin
{
  public:
    ResultPlugin(const std::string &ID) : id_(ID) {}
    std::string id() const override { return id_; }
    std::string generate(const ProgramState &, const ProgramState &) override
    {
        // TODO
        return "//@ ensures ...;\n";
    }

  private:
    std::string id_;
};
REGISTER_ACSL_PLUGIN(ResultPlugin, "result");
