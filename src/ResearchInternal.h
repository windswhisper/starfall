#pragma once
#include "Research.h"
#include <functional>

class TechnoClass;
class TechnoTypeClass;
class HouseClass;

// Internal research interfaces. Runtime bridges are implemented beside the passive
// registry and conversion engine; research never accesses their private containers.
namespace Research::Detail {
extern std::array<std::wstring,rarityCount> rarityNameOverrides;
extern std::array<std::optional<std::array<int,3>>,rarityCount> rarityColors;
using ResearchWarning=std::function<void(const std::string&,const char*,const std::string&)>;
using EffectWarning=std::function<void(const char*,const std::string&)>;
inline EffectSpec singleEffect(const Option& o) {
    return {o.effect,o.passive,o.technology,o.from,o.to,o.types,o.credits,o.cost,o.reward};
}
bool validateEffect(const EffectSpec& effect,const EffectWarning& warn={});
bool hasProductionLicense(const HouseClass* house,TechnoTypeClass* type);
void installProductionCheck();
void clearEffectCaches();
bool applyResearchEffect(HouseClass* h,const EffectSpec& effect,int source);
bool hasReplacement(const HouseClass* owner,const EffectSpec& spec);
bool persistentEffect(Effect effect);
bool activateStageEffect(HouseClass* owner,const StageEffect& effect);
void checkBonds(int house,int acquired=-1);
void indexStages();
void checkStages(int house,const std::string* changedCounter=nullptr);
void recordAcquisition(int house,int option);
unsigned matchSeed();
int randomIndex(int count);
const std::vector<int>& buildingOfferRows(unsigned long id,int house,const std::vector<std::string>& configured);
void notifyFreeResearch(int house);
void showResearchSelection(HouseClass* house,int option);
void announceReward(int house,int option);

namespace Runtime {
bool hasPassive(const std::string& id);
bool eligibleHouse(HouseClass* house);
bool conversionSupportsTypes(const TechnoTypeClass* from,const TechnoTypeClass* to);
bool canTransformUnit(TechnoClass* unit,TechnoTypeClass* to);
bool transformUnit(TechnoClass* unit,TechnoTypeClass* to);
}
}
