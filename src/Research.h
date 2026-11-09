#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <climits>
#include <string>
#include <vector>

namespace Research {
constexpr int choices=3;
inline constexpr const char* legacyIds[]={"sf_repair","sf_drain","sf_riposte"};
enum class Effect { Passive, Credits, Technology, ConvertInfantry, Invalid };
struct Option {
    std::string id, passive, icon;
    std::vector<std::string> types;
    std::wstring name, summary, detail;
    std::vector<std::string> countries, prerequisites;
    bool validChain=false;
    Effect effect=Effect::Passive;
    int credits=0;
    std::string technology, from, to;
};
inline std::vector<Option> options;
inline std::wstring title;
constexpr int price=100;
using Unlocks=std::map<int,std::set<std::string>>;
inline Unlocks unlocked;
inline std::map<int,int> points;
struct Producer { int owner,elapsed; };
inline std::map<unsigned long,Producer> producers;
inline int balance(int house) {
    auto it=points.find(house);
    return it==points.end()?0:it->second;
}
inline void award(int house,int amount) {
    if(house<0 || amount<=0) return;
    points[house]=static_cast<int>(std::min<long long>(INT_MAX,1LL*balance(house)+amount));
}
inline bool owns(int house,const std::string& id) {
    auto it=unlocked.find(house);
    return it!=unlocked.end() && it->second.contains(id);
}
inline std::map<int,std::vector<std::string>> savedOffers;
const std::vector<int>& offers(int house,const char* country);
void clearOfferCache();
inline bool expanded=false;
inline int pressed=-1;
void ensureReady();
bool available(int option);
bool executeEffect(int house,int option);
bool canApply(int house,int option);
bool meetsPrerequisites(int house,int option);
inline bool purchase(int house,int option,const char* country) {
    if(balance(house)<price || !available(option) || !meetsPrerequisites(house,option) || owns(house,options[option].id)) return false;
    const auto& eligible=offers(house,country);
    if(std::find(eligible.begin(),eligible.end(),option)==eligible.end()) return false;
    if(!executeEffect(house,option)) return false;
    unlocked[house].insert(options[option].id);
    points[house]-=price;
    savedOffers.erase(house); clearOfferCache();
    return true;
}
inline void reset() {
    unlocked.clear(); points.clear(); producers.clear(); savedOffers.clear(); clearOfferCache();
    options.clear(); expanded=false; pressed=-1;
}std::wstring describe(int option,wchar_t (&conditions)[96]);
}
