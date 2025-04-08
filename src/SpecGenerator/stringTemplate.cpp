#include "stringTemplate.h"

using namespace std;

// Initialize the instance with rawText_, nested $ will be ignored.
void StringTemplate::initialize(){
    Placeholder curPH; // PH = placeholder
    bool afterDollor = false;
    bool inBraces = false;
    for(int i = 0; i < rawText_.size(); ++i){
        char ch = rawText_[i];
        
        if(inBraces){
            ++curPH.len;
            if(ch == '}'){
                placeholders_.push_back(curPH);
                inBraces = false;
            } else {
                curPH.name += ch;
            }
            continue;
        }

        if(afterDollor && ch == '{'){
            inBraces = true;
            curPH.name = "";
            curPH.pos = i;
            curPH.len = 2;
        }
        
        afterDollor = (ch == '$');
    }
}

// Replace placeholders named ${key} with ${value}.
// With mp has both <A, B> and <B, C>, the placeholders named A will have name B eventually.
// /return the total number of replaced placeholders.
size_t StringTemplate::remap(const unordered_map<string, string>& mp){
    size_t count = 0;
    unordered_map<string, unordered_set<size_t>> temp;
    for(auto& kv : mp){
        if(!name2PH_.count(kv.first))
            continue;

        for(auto& id : name2PH_[kv.first]){
            placeholders_[id].name = kv.second;
            ++count;
        }
        temp[kv.second] = std::move(name2PH_[kv.first]);
        name2PH_.erase(kv.first);
    }
    for(auto& kv : temp){
        name2PH_[kv.first].merge(std::move(kv.second));
    }

    if(next_) count += next_->remap(mp);
    return count;
}

// prefix all placeholders' name
void StringTemplate::withPrefix(const string& prefix){
    for(auto& ph : placeholders_){
        ph.name = prefix + ph.name;
    }
    unordered_map<string, unordered_set<size_t>> temp;
    for(auto& kv : name2PH_){
        temp[prefix + kv.first] = std::move(kv.second);
    }
    name2PH_ = std::move(temp);

    if(next_) next_->withPrefix(prefix);
}

// In terms of frequent concatenations.
template <typename T, typename>
void StringTemplate::append(T&& templ){
    if(next_)
        next_->append(forward<T>(templ));
    else
        next_ = make_unique(forward<T>(templ));
}

// /return the string with all placeholders replaced by mp
string StringTemplate::operator() (const unordered_map<string, string>& mp) const{
    string result;
    size_t curPos = 0;
    for(auto& ph : placeholders_){
        result += rawText_.substr(curPos, ph.pos-curPos);
        if(mp.count(ph.name))
            result += mp.at(ph.name);
        curPos = ph.pos + ph.len;
    }

    if(next_) result += (*next_)(mp);
    return result;
}

// Output string with placeholders replaced by mp to os.
void StringTemplate::operator() (ostream& os, const unordered_map<string, string>& mp) const{
    size_t curPos = 0;
    for(auto& ph : placeholders_){
        os << rawText_.substr(curPos, ph.pos-curPos);
        if(mp.count(ph.name))
            os << mp.at(ph.name);
        curPos = ph.pos + ph.len;
    }

    if(next_) (*next_)(os, mp);
}

// String literal to StringTemplate.
StringTemplate operator"" _ST(const char* str){
    return StringTemplate(str);
}