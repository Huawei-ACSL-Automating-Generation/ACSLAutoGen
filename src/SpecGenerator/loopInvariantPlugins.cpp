// src/SpecGenerator/loopInvariantPlugins.cpp

#include "specGenerator.h"
#include "stringTemplate.h"
#include "macros.h"
#include "state.h"

using namespace std;
using namespace clang;

class DumpLoopInfoPlugin : public LoopInvariantPlugin
{
  public:
    DumpLoopInfoPlugin(const string &ID) : id_(ID) {}
    string_view id() const override { return id_; }
    optional<string> generate(const ProgramState &preState, const LoopInfo &loopInfo) const override
    {
        ostringstream oss;
        oss << "index's address: "
            << (loopInfo.index_ ? loopInfo.index_->regularForm(false) : "NULL") << endl;
        oss << "index's bound: "
            << (loopInfo.indexBound_ ? loopInfo.indexBound_->regularForm(false) : "NULL") << endl;
        oss << "patterns: " << endl;
        for (auto &[addr, pattern] : loopInfo.patternsMap_)
        {
            oss << "address: " << addr.regularForm(false) << "\t";
            oss << "pattern: ";
            if (pattern)
            {
                oss << "{ initial value="
                    << ((*pattern).initialValue_ ? (*pattern).initialValue_->regularForm(false)
                                                 : "NULL")
                    << ", step=" << (*pattern).step_ << " }" << endl;
            }
            else
            {
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