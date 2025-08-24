// src/SpecGenerator/stringTemplate.cpp

#include "stringTemplate.h"

using namespace std;

StringTemplate::StringTemplate(const char *str) {
    if (str == nullptr)
        rawText_ = "";
    else
        rawText_ = str;
    initialize();
};

StringTemplate::StringTemplate(const StringTemplate &templ)
    : rawText_(templ.rawText_), placeholders_(templ.placeholders_), nameToPh_(templ.nameToPh_) {
    if (templ.next_)
        next_ = make_unique<StringTemplate>(*templ.next_);
}

void StringTemplate::initialize() {
    Placeholder curPh; // Ph = placeholder
    bool afterDollor = false;
    bool inBraces    = false;
    for (size_t i = 0; i < rawText_.size(); ++i) {
        char ch = rawText_[i];

        if (inBraces) {
            ++curPh.len;
            if (ch == '}') {
                nameToPh_[curPh.name].insert(placeholders_.size());
                placeholders_.push_back(curPh);
                inBraces = false;
            } else {
                curPh.name += ch;
            }
            continue;
        }

        if (afterDollor && ch == '{') {
            inBraces   = true;
            curPh.name = "";
            curPh.pos  = i - 1;
            curPh.len  = 2;
        }

        afterDollor = (ch == '$');
    }
}

size_t StringTemplate::getPlaceholderNum() const {
    return placeholders_.size() + (next_ ? next_->placeholders_.size() : 0);
}

size_t StringTemplate::remap(const NameMap &nameMap) {
    size_t count = 0;
    unordered_map<string, unordered_set<size_t>> temp;
    for (auto &nameMap_it : nameMap) {
        if (auto nameToPh_it = nameToPh_.find(nameMap_it.first); nameToPh_it != nameToPh_.end()) {
            auto &phSet = nameToPh_it->second;
            for (auto &id : phSet) {
                placeholders_[id].name = nameMap_it.second;
                ++count;
            }
            temp[nameMap_it.second] = std::move(phSet);
            nameToPh_.erase(nameToPh_it);
        }
    }
    for (auto &kv : temp) {
        nameToPh_[kv.first].merge(std::move(kv.second));
    }

    if (next_)
        count += next_->remap(nameMap);
    return count;
}

void StringTemplate::withPrefix(const string &prefix) {
    for (auto &ph : placeholders_) {
        ph.name = prefix + ph.name;
    }
    unordered_map<string, unordered_set<size_t>> temp;
    for (auto &kv : nameToPh_) {
        temp[prefix + kv.first] = std::move(kv.second);
    }
    nameToPh_ = std::move(temp);

    if (next_)
        next_->withPrefix(prefix);
}

string StringTemplate::to_string(const NameMap &phMap) const {
    string result;
    size_t curPos = 0;
    for (auto &ph : placeholders_) {
        result += rawText_.substr(curPos, ph.pos - curPos);
        if (auto kv = phMap.find(ph.name); kv != phMap.end())
            result += kv->second;
        else
            result += ph.name;
        curPos = ph.pos + ph.len;
    }
    if (curPos < rawText_.length())
        result += rawText_.substr(curPos);

    if (next_)
        result += next_->to_string(phMap);
    return result;
}

string StringTemplate::to_string() const {
    NameMap emptyMap;
    return this->to_string(emptyMap);
}

StringTemplate operator""_st(const char *str, size_t) { return StringTemplate(str); }