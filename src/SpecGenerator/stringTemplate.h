// src/SpecGenerator/stringTemplate.h

#ifndef STRING_TEMPLATE_H
#define STRING_TEMPLATE_H

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

using NameMap = unordered_map<string, string>;

/// Parse a string containing placeholders in the form of ${name} as a template, supporting
/// placeholder substitution and templates concatenation.
/// The current implementation handles only simple placeholders: any text from ${ up to the first
/// } is interpreted as the name, without special parsing.
class StringTemplate {
  private:
    string rawText_; ///< The raw input text, will never be modified.

    struct Placeholder {
        string name; ///< Placeholder's name, ${name} will be 'name'.
        size_t pos;  ///< Placeholder's start position in rawText_.
        size_t len;  ///< Placeholder's length, ${name} will be 6.
    };
    vector<Placeholder> placeholders_;
    unordered_map<string,
                  unordered_set<size_t>>
        nameToPh_; ///< Map from name to index in placeholders_. ph = placeholder
    unique_ptr<StringTemplate>
        next_; ///< Pointer to next StringTemplate, for templates concatenations.

    /// @brief Initialize the remaining members with rawText_ inputed, called only during
    /// construction. Note: nested ${} are ignored.
    void initialize();

  public:
    StringTemplate() = delete;

    /// Move constructor.
    StringTemplate(StringTemplate &&) = default;

    /// Copy constructor.
    StringTemplate(const StringTemplate &);

    /// @brief Constructor with raw C-string.
    /// @param str if str is nullptr, treat it as empty string.
    StringTemplate(const char *str);

    /// @brief Constructor with std::string
    StringTemplate(const string &str) : rawText_(str) { initialize(); }
    ~StringTemplate() = default;

    /// @return The number of placeholders.
    size_t getPlaceholderNum() const;

    /// @brief Prefix all placeholders' name.
    /// @param prefix
    void withPrefix(const string &prefix);

    /// @brief Modify all placeholders matching nameMap.
    /// @param nameMap Map from old name to new name. Non-matching keys are ignored.
    /// @return Number of modified placeholders. Note: Key-value like {"name": "name"} will also
    /// cause placeholder named 'name' treated as modified. With nameMap has both <A, B> and <B, C>,
    /// the placeholders named A will have name B eventually.
    size_t remap(const NameMap &nameMap);

    /// @brief Append another StringTemplate to *this.
    /// @tparam Use template for supporting rValue reference.
    /// @param templ
    template <typename T> void append(T &&templ) {
        if (next_)
            next_->append(std::forward<T>(templ));
        else
            next_ = make_unique<StringTemplate>(StringTemplate(std::forward<T>(templ)));
    }

    /// @brief Output the rawText_ with placeholders replaced, "Hello, ${name}!" will be "Hello,
    /// Alice!" with NameMap containing {"name": "Alice"}. Unmapped placeholder will hold its name.
    /// e.g. "Hello, ${name}!" will be "Hello, name!".
    /// @param nameMap Map from placeholders' name to string you want.
    /// @return String with placeholders replaced.
    string to_string(const NameMap &nameMap) const;

    /// @brief Output the rawText_ with placeholders replaced by its name. Override for a simpler
    /// interface.
    /// @return String with placeholders replaced by empty string.
    string to_string() const;

  private:
    /// @brief Operator +.
    /// @tparam Use template for supporting rValue reference.
    /// @param LHS
    /// @param RHS
    /// @return Return the emptyTemplate.append(LHS).append(RHS).
    template <typename T1, typename T2> friend StringTemplate operator+(T1 &&LHS, T2 &&RHS) {
        StringTemplate temp = StringTemplate(std::forward<T1>(LHS));
        temp.append(StringTemplate(std::forward<T2>(RHS)));
        // copy elision
        return temp;
    }

    /// @brief Operator +=, functionally equivalent to append.
    /// @tparam Use template for supporting rValue reference.
    /// @param RHS
    /// @return Return the LHS.append(RHS).
    template <typename T> friend StringTemplate &operator+=(StringTemplate &LHS, T &&RHS) {
        LHS.append(StringTemplate(std::forward<T>(RHS)));
        // copy elision
        return LHS;
    }
};

/// @brief Literal operator for construct template from C-string literal easily.
StringTemplate operator"" _st(const char *, size_t);

#endif