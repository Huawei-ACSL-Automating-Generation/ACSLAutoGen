#ifndef STRINGTEMPLATE_H
#define STRINGTEMPLATE_H

#include <string>
#include <vector>
#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <type_traits>
#include <utility>
#include <memory>

using namespace std;

class StringTemplate {
private:
    string rawText_;

    struct Placeholder {
        string name;
        size_t pos;
        size_t len;
    };
    vector<Placeholder> placeholders_;
    unordered_map<string,
        unordered_set<size_t>> name2PH_; // PH = placeholder
    unique_ptr<StringTemplate> next_;
        
    void initialize();

public:
    StringTemplate() = delete;
    // Has same behavior with nullptr and empty string.
    StringTemplate(const char* str) {
        if(str == nullptr)
            rawText_ = "";
        initialize();
    };
    StringTemplate(const string& str) : rawText_(str) { initialize(); }

    size_t getPlaceholderNum() const { return placeholders_.size(); }
    void withPrefix(const string&);
    size_t remap(const unordered_map<string, string>&);

    template <typename T,
        typename = std::enable_if_t<
            std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, StringTemplate>
        >
    >
    void append(T&&);
    
    template <typename T,
        typename = std::enable_if_t<
            std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, StringTemplate>
        >
    >
    friend StringTemplate operator+  (T&& templA, T&& templB){
        auto& temp = templA;
        temp.append(forward<T>(templB));
        return temp;
    }
    string operator() (const unordered_map<string, string>&) const;
    void operator() (ostream&, const unordered_map<string, string>&) const;
};

StringTemplate operator"" _ST(const char*);

#endif