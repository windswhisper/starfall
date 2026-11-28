#pragma once
#include "HealthFormula.h"
#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <climits>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Research {
inline bool isTargetCategory(std::string_view value) {
    return value=="@infantry" || value=="@vehicle" || value=="@groundvehicle" || value=="@aircraft" || value=="@building";
}
inline bool matchesTarget(const std::vector<std::string>& targets,std::string_view type,std::string_view kind,bool groundVehicle=false) {
    for(const auto& target:targets) {
        if(target=="*" || target==type) return true;
        if(target=="@groundvehicle" && kind=="vehicle" && groundVehicle) return true;
        if(isTargetCategory(target) && std::string_view(target).substr(1)==kind) return true;
    }
    return false;
}
constexpr int choices=3;
inline constexpr const char* legacyIds[]={"sf_repair","sf_drain","sf_riposte"};
enum class Effect { Passive, Credits, Technology, ConvertInfantry, ConvertVehicle, TypeCost, CategoryCost, ReplaceProduction, RandomResearch, FreeResearch, Invalid };
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
struct ProjectReward {
    std::optional<Rarity> rarity;
    int count=1;
    std::vector<std::string> projects;
};
struct EffectSpec {
    Effect effect=Effect::Invalid;
    std::string passive,technology,from,to;
    std::vector<std::string> types;
    int credits=0;
    CostChange cost;
    ProjectReward reward;
};
struct StageEffect : EffectSpec {std::string id;};
struct Stage {
    std::string id;
    HealthFormula condition;
    std::vector<StageEffect> effects;
};
struct DetailFormula {
    size_t start,end;
    HealthFormula expression;
};
struct Option {
    std::string id, passive, icon;
    std::vector<std::string> types;
    std::wstring name, summary, detail;
    std::vector<std::string> countries, prerequisites, excludes, replaces;
    bool validChain=false;
    Effect effect=Effect::Passive;
    int credits=0;
    std::string technology, from, to;
    std::vector<EffectSpec> effects;
    bool effectList=false;
    Rarity rarity=Rarity::Common;
    CostChange cost;
    ProjectReward reward;
    std::wstring targetLabel;
    bool grantOnly=false;
    std::vector<std::string> unlockWithTech,techProviders;
    std::vector<DetailFormula> detailFormulas;
    int detailDecimals=-1;
    bool stagesOnly=false;
    std::vector<Stage> stages;
    bool repeatable=false;
};
struct BondTier {
    std::string id;
    int required=0;
    std::wstring summary;
    std::vector<StageEffect> effects;
};
struct Bond {
    std::string id;
    std::wstring name;
    std::vector<std::string> members;
    std::vector<BondTier> tiers;
};
struct BondTierState {bool active=false;std::set<std::string> grants;};
using StageKey=std::tuple<int,std::string,std::string>;
using StageProgress=std::map<StageKey,std::set<std::string>>;
extern StageProgress stageProgress;
extern std::vector<Bond> bonds;
extern std::map<std::string,int> bondIndex;
extern std::map<std::string,std::vector<int>> memberBonds;
using BondProgress=std::map<StageKey,BondTierState>;
extern BondProgress bondProgress;
extern unsigned bondRevision;
std::wstring bondRewardText(const StageEffect& effect);
int bondCount(int house,const Bond& bond,int source=-1);
bool knownResearchVariable(const std::string& key);
template<class Visitor>
void forBondEffects(int house,Visitor visit,int source=-1) {
    for(auto it=bondProgress.lower_bound({house,"",""});it!=bondProgress.end() && std::get<0>(it->first)==house;++it) {
        if(!it->second.active || it->second.grants.empty()) continue;
        auto index=bondIndex.find(std::get<1>(it->first));
        if(index==bondIndex.end()) continue;
        const auto& bond=bonds[index->second];
        for(const auto& tier:bond.tiers) if(tier.id==std::get<2>(it->first)) {
            if(source>=0 && bondCount(house,bond,source)<tier.required) break;
            for(const auto& e:tier.effects) if(it->second.grants.contains(e.id)) visit(e);
            break;
        }
    }
}
// Stage completion and successful persistent effects are tracked separately.
template<class Visitor>
void forStageEffects(int house,const Option& project,Visitor visit) {
    for(const auto& stage:project.stages) {
        auto it=stageProgress.find({house,project.id,stage.id});
        if(it!=stageProgress.end()) for(const auto& effect:stage.effects)
            if(it->second.contains(effect.id)) visit(effect);
    }
}
template<class Visitor>
void forBaseEffects(const Option& project,Visitor visit) {
    if(project.effectList) {for(const auto& effect:project.effects) visit(effect);}
    else if(!project.stagesOnly) visit(project);
}
template<class Visitor>
void forActiveEffects(int house,const Option& project,Visitor visit) {
    forBaseEffects(project,visit);
    forStageEffects(house,project,visit);
}
extern std::vector<Option> options;
enum class SoundCue { Button, Cards, Refresh, Select, Available };
extern std::array<std::string,5> soundFiles;
extern std::string freeRewardSound;
extern unsigned uiGeneration;
inline constexpr int rarityCount=static_cast<int>(Rarity::Invalid);
inline constexpr std::array<int,rarityCount> defaultRarityWeights={5,4,1,0};
extern std::array<int,rarityCount> rarityWeights;
const wchar_t* rarityName(Rarity rarity);
extern std::wstring title;
extern bool enabled;
bool isEnabled();
extern bool allowCardRefresh;
extern bool allowAIResearch;
extern bool showAvailableAnimation;
extern bool sharedRaritySequence;
extern bool mixedRarityPool;
extern bool announceSelection;
extern std::map<int,unsigned> refreshedSlots;
bool slotUsed(int house,int slot);
bool canRefresh(int house,const char* country,int slot);
bool refresh(int house,const char* country,int slot,int expected);
constexpr int price=100;
using Unlocks=std::map<int,std::set<std::string>>;
extern Unlocks unlocked, retired;
// Successful ordinary purchases only; free building rewards do not advance rounds.
extern std::map<int,unsigned> completedRounds;
// Loaded match seed; unset for a fresh scenario. Reading offers never mutates it.
extern std::optional<unsigned> raritySequenceSeed;
std::vector<int> completedProjects(int house);
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
using RewardId=std::uint64_t;
inline constexpr RewardId ticketFlag=RewardId{1}<<32;
struct FreeResearchTicket { int house; std::vector<std::string> projects; std::optional<Rarity> rarity; };
extern std::map<unsigned long,FreeResearchTicket> freeTickets;
extern unsigned long lastTicket;
extern RewardId rewardPanel;
std::vector<RewardId> pendingFreeRewards(int house);
std::vector<int> freeOptions(RewardId id,int house);
bool claimFree(RewardId id,int house,int option);
bool grantFree(int house,const char* country,const std::vector<std::string>& projects,std::optional<Rarity> rarity);
bool canOfferFree(int house,const char* country,const std::vector<std::string>& projects,std::optional<Rarity> rarity,int source);
const std::vector<int>& ticketOptions(unsigned long id,int house,const char* country);
std::vector<int> buildingOptions(unsigned long id,int house);
std::vector<unsigned long> pendingBuildingRewards(int house);
bool claimBuilding(unsigned long id,int house,int option);
int balance(int house);
void award(int house,int amount);
bool owns(int house,const std::string& id);
// Stored only for projects acquired at least twice, including retired projects.
extern std::map<std::pair<int,std::string>,int> repeatCounts;
int acquisitionCount(int house,const std::string& id);
bool ownershipAllows(int house,const Option& option);
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
// Fully qualified INI counter name -> owning research project.
extern std::map<std::string,std::string> counterProjects;
using CounterValues=std::map<int,std::map<std::string,int>>;
extern CounterValues counterValues;
int counter(int house,const std::string& key);
bool changeCounter(int house,const std::string& key,int delta);
void readConfiguration(const char* configPath,void (*loadPassives)()=nullptr);
void ensureReady();
bool available(int option);
bool executeEffect(int house,int option);
bool canApply(int house,int option);
bool meetsPrerequisites(int house,int option);
bool meetsCountryOrTech(int house,const Option& option,const std::string& country,int source=-1);
bool purchase(int house,int option,const char* country);
int purchaseForAI(int house,const char* country,unsigned frame);
// Free, uniform draw over eligible projects; nullopt includes every rarity.
// Returns the acquired option index, or -1. Does not advance ordinary rounds.
int grantNamed(int house,const char* country,const std::string& id);
int grantRandom(int house,const char* country,std::optional<Rarity> rarity=std::nullopt);
// Called only after ownership and purchase/building bookkeeping have committed.
void grantProjectRewards(int house,int option,const char* country);
bool canGrantRandom(int house,int source,const char* country,std::optional<Rarity> rarity);
void reset();
std::wstring describe(int option,int house);
}
