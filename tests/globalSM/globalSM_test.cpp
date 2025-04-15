// tests/globalSM/globalSM_test.cpp

#include <gtest/gtest.h>
#include <unordered_map>
#include <string>
#include <sstream>
#include "globalSM.h"
#include "ASTExtractor.h"

using namespace std;

TEST(GlobalSMTest, testing)
{
    utils::ASTExtractor e("");
    GlobalSM::getInstance().initialize(e.getASTContext());
    auto &SM = GlobalSM::getSM();
    auto fileID = SM.getMainFileID();
    auto fileEntry = SM.getFileEntryForID(fileID);
    auto filepath = fileEntry->tryGetRealPathName();
    auto filename = filepath.substr(filepath.find_last_of("/") + 1);
    EXPECT_STREQ(filename.str().c_str(), "input.cc");
}