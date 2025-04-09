// src/SpecGenerator/stringTemplate.h

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

// Parse a string containing placeholders in the form of ${name} as a template, supporting
// placeholder substitution and templates concatenation.
// The current implementation handles only simple placeholders: any text from ${ up to the first }
// is interpreted as the name, without special parsing.
class StringTemplate
{
  private:
    string rawText_;

    struct Placeholder
    {
        string name;
        size_t pos;
        size_t len;
    };
    vector<Placeholder> placeholders_;
    unordered_map<string,
        unordered_set<size_t>> nameToPh_; // Ph = placeholder
    unique_ptr<StringTemplate> next_;

    void initialize();

  public:
    StringTemplate() = delete;
    StringTemplate(const StringTemplate &);
    StringTemplate(const char *str);
    StringTemplate(const string &str) : rawText_(str) { initialize(); }

    size_t getPlaceholderNum() const;
    void withPrefix(const string &);
    size_t remap(const unordered_map<string, string> &);

    template <typename T, typename> void append(T &&templ);
    template <typename T, typename> friend StringTemplate operator+(T &&templA, T &&templB);

    string operator()(const unordered_map<string, string> &) const;
    string operator()() const;
    void operator()(ostream &, const unordered_map<string, string> &) const;
    void operator()(ostream &) const;
};

StringTemplate operator"" _ST(const char *);

// In terms of frequent concatenations between rValue
template <typename T,
    typename = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, StringTemplate>>>
void StringTemplate::append(T &&templ)
{
    if(next_)
        next_->append(std::forward<T>(templ));
    else
        next_ = make_unique<StringTemplate>(std::forward<T>(templ));
}

template <typename T,
    typename = std::enable_if_t<
        std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, StringTemplate>>>
StringTemplate operator+(T &&templA, T &&templB)
{
    StringTemplate temp = std::forward<T>(templA);
    temp.append(std::forward<T>(templB));
    return std::move(temp);
}

#endif