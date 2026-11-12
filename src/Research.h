#pragma once
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <climits>
#include <string>
#include <string_view>
#include <vector>

namespace Research {
inline bool isTargetCategory(std::string_view value) {
    return value=="@infantry" || value=="@vehicle" || value=="@aircraft" || value=="@building";
}
inline bool matchesTarget(const std::vector<std::string>& targets,std::string_view type,std::string_view kind) {
    for(const auto& target:targets) {
        if(target=="*" || target==type) return true;
        if(isTargetCategory(target) && std::string_view(target).substr(1)==kind) return true;
    }
    return false;
}
constexpr int choices=3;
inline constexpr const char* legacyIds[]={"sf_repair","sf_drain","sf_riposte"};
enum class Effect { Passive, Credits, Technology, ConvertInfantry, ConvertVehicle, TypeCost, CategoryCost, Invalid };
enum class Rarity { Common, Rare, Epic, Invalid };
enum class CostCategory { Building, Defense, Infantry, Vehicle, Invalid };
inline constexpr const wchar_t* categoryName(CostCategory category) {
    switch(category) {
        case CostCategory::Building: return L"建筑";
        case CostCategory::Defense: return L"防御";
        case CostCategory::Infantry: return L"步兵";
        case CostCategory::Vehicle: return L"载具";
        default: return L"";
    }
}
struct CostChange {
    CostCategory category=CostCategory::Invalid;
    int delta=0;
    bool percent=false;
};
struct EffectSpec {
    Effect effect=Effect::Invalid;
    std::string passive,technology,from,to;
    std::vector<std::string> types;
    int credits=0;
    CostChange cost;
};
struct Option {
    std::string id, passive, icon;
    std::vector<std::string> types;
    std::wstring name, summary, detail;
    std::vector<std::string> countries, prerequisites;
    bool validChain=false;
    Effect effect=Effect::Passive;
    int credits=0;
    std::string technology, from, to;
    std::vector<EffectSpec> effects;
    bool effectList=false;
    Rarity rarity=Rarity::Common;
    CostChange cost;
};
inline std::vector<Option> options;
enum class SoundCue { Button, Cards, Refresh, Select, Available };
inline std::array<std::string,5> soundFiles;
inline unsigned uiGeneration=0;
inline std::array<int,3> rarityWeights={5,4,1};
inline std::wstring title;
inline bool enabled=true;
bool isEnabled();
inline bool allowCardRefresh=true;
inline std::map<int,unsigned> refreshedSlots;
bool slotUsed(int house,int slot);
bool canRefresh(int house,const char* country,int slot);
bool refresh(int house,const char* country,int slot,int expected);
constexpr int price=100;
using Unlocks=std::map<int,std::set<std::string>>;
inline Unlocks unlocked;
inline std::map<int,std::vector<std::string>> unlockOrder;
inline std::map<int,int> points;
struct GlobalIncome {
    int initialPoints=0, periodicPoints=0;
    unsigned intervalFrames=0,lastFrame=0;
    bool initialized=false;
};
inline GlobalIncome globalIncome;
struct Producer { int owner,elapsed; };
inline std::map<unsigned long,Producer> producers;
struct BuildingReward { int owner=-1, authorized=-1; bool claimed=false; };
inline std::map<unsigned long,BuildingReward> buildingRewards;
inline unsigned long rewardPanel=0;
std::vector<int> buildingOptions(unsigned long id,int house);
bool claimBuilding(unsigned long id,int house,int option);
inline int balance(int house) {
    auto it=points.find(house);
    return it==points.end()?0:it->second;
}
inline void award(int house,int amount) {
    if(house<0 || amount<=0 || !isEnabled()) return;
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
inline int pressed=-1,pressedOption=-1;
inline int badgePageIndex=0;
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
    unlockOrder[house].push_back(options[option].id);
    points[house]-=price;
    savedOffers.erase(house); refreshedSlots.erase(house); clearOfferCache();
    return true;
}
inline void reset() {
    ++uiGeneration;soundFiles={};enabled=true;
    globalIncome={};
    unlocked.clear(); unlockOrder.clear(); points.clear(); producers.clear(); buildingRewards.clear(); rewardPanel=0; savedOffers.clear(); clearOfferCache();
    refreshedSlots.clear(); options.clear(); expanded=false; pressed=pressedOption=-1; badgePageIndex=0;
}
std::wstring describe(int option,wchar_t (&conditions)[96]);
}
