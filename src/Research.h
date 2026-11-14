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
enum class Rarity { Common, Rare, Epic, Insane, Invalid };
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
extern std::vector<Option> options;
enum class SoundCue { Button, Cards, Refresh, Select, Available };
extern std::array<std::string,5> soundFiles;
extern unsigned uiGeneration;
inline constexpr int rarityCount=static_cast<int>(Rarity::Invalid);
inline constexpr std::array<int,rarityCount> defaultRarityWeights={5,4,1,0};
extern std::array<int,rarityCount> rarityWeights;
extern std::wstring title;
extern bool enabled;
bool isEnabled();
extern bool allowCardRefresh;
extern std::map<int,unsigned> refreshedSlots;
bool slotUsed(int house,int slot);
bool canRefresh(int house,const char* country,int slot);
bool refresh(int house,const char* country,int slot,int expected);
constexpr int price=100;
using Unlocks=std::map<int,std::set<std::string>>;
extern Unlocks unlocked;
extern std::map<int,std::vector<std::string>> unlockOrder;
extern std::map<int,int> points;
struct GlobalIncome {
    int initialPoints=0, periodicPoints=0;
    unsigned intervalFrames=0,lastFrame=0;
    bool initialized=false;
};
extern GlobalIncome globalIncome;
struct Producer { int owner,elapsed; };
extern std::map<unsigned long,Producer> producers;
enum class BuildingRewardSource : unsigned { None, Engineer, Spy };
struct BuildingReward { int owner=-1, authorized=-1; bool claimed=false; BuildingRewardSource source=BuildingRewardSource::None; };
extern std::map<unsigned long,BuildingReward> buildingRewards;
extern unsigned long rewardPanel;
std::vector<int> buildingOptions(unsigned long id,int house);
bool claimBuilding(unsigned long id,int house,int option);
int balance(int house);
void award(int house,int amount);
bool owns(int house,const std::string& id);
extern std::map<int,std::vector<std::string>> savedOffers;
extern std::map<int,std::set<std::string>> seenOffers;
const std::vector<int>& offers(int house,const char* country);
void clearOfferCache();
// Resource files may change between scenarios/configuration reloads.
void clearPresentationCaches(bool stopAudio=false);
extern bool expanded;
extern int pressed,pressedOption;
extern int badgePageIndex;
// Configuration is loaded after passive registration; state storage lives in Research.cpp.
void readConfiguration(const char* configPath);
void ensureReady();
bool available(int option);
bool executeEffect(int house,int option);
bool canApply(int house,int option);
bool meetsPrerequisites(int house,int option);
bool purchase(int house,int option,const char* country);
void reset();
std::wstring describe(int option,wchar_t (&conditions)[96]);
}
