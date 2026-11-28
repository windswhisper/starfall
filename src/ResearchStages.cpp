#include "ResearchInternal.h"
#include <HouseClass.h>
#include <deque>

namespace Research {
StageProgress stageProgress;
}

namespace Research::Detail {
namespace {
using StageRef=std::pair<int,int>;
std::vector<StageRef> allStages;
std::map<std::string,std::vector<StageRef>> counterStages;
struct PendingStage {int house;StageRef ref;};
std::deque<PendingStage> pending;
bool draining=false;

void activate(int house,const Option& project,const Stage& stage) {
    auto owner=HouseClass::Array.GetItemOrDefault(house);
    if(!Runtime::eligibleHouse(owner) || !owns(house,project.id)) return;
    StageKey key{house,project.id,stage.id};
    if(!stageProgress.try_emplace(key).second) return;
    for(const auto& effect:stage.effects) {
        // A random acquisition can retire the parent mid-list. Remaining immediate rewards
        // still execute; persistent grants require the parent to remain owned.
        if(persistentEffect(effect.effect) && !owns(house,project.id)) continue;
        if(activateStageEffect(owner,effect)) {
            stageProgress.at(key).insert(effect.id);
            clearOfferCache();
        }
    }
}
}

bool persistentEffect(Effect effect) {
    return effect==Effect::Technology || effect==Effect::Passive || effect==Effect::TypeCost ||
        effect==Effect::CategoryCost || effect==Effect::ReplaceProduction;
}
bool activateStageEffect(HouseClass* owner,const StageEffect& effect) {
    const int house=owner->ArrayIndex;
    if(effect.effect==Effect::RandomResearch) {
        for(int n=0;n<effect.reward.count;++n) {
            int granted=grantRandom(house,owner->Type->ID,effect.reward.rarity);
            if(granted<0) break;
            announceReward(house,granted);
        }
        return false;
    }
    if(effect.effect==Effect::FreeResearch) {
        grantFree(house,owner->Type->ID,effect.reward.projects,effect.reward.rarity);
        return false;
    }
    const bool applied=applyResearchEffect(owner,effect,-1);
    return persistentEffect(effect.effect) && (applied || effect.effect==Effect::Technology ||
        (effect.effect==Effect::ReplaceProduction && hasReplacement(owner,effect)));
}
void indexStages() {
    allStages.clear();counterStages.clear();
    for(int i=0;i<static_cast<int>(options.size());++i) {
        const auto& stages=options[i].stages;
        for(int j=0;j<static_cast<int>(stages.size());++j) {
            allStages.emplace_back(i,j);
            const auto& names=stages[j].condition.counterNames();
            for(const auto& name:std::set<std::string>(names.begin(),names.end()))
                counterStages[name].emplace_back(i,j);
        }
    }
}

void checkStages(int house,const std::string* changedCounter) {
    if(!enabled || allStages.empty()) return;
    const auto* candidates=&allStages;
    if(changedCounter) {
        auto it=counterStages.find(*changedCounter);
        if(it==counterStages.end()) return;
        candidates=&it->second;
    }
    for(auto [i,j]:*candidates) {
        const auto& project=options[i];
        const auto& stage=project.stages[j];
        if(!owns(house,project.id) || stageProgress.contains({house,project.id,stage.id})) continue;
        auto value=stage.condition.evaluateResearch(house,counter);
        if(value && *value!=0) pending.push_back({house,{i,j}});
    }
    if(draining) return;
    draining=true;
    while(!pending.empty()) {
        auto next=pending.front();pending.pop_front();
        const auto& project=options[next.ref.first];
        activate(next.house,project,project.stages[next.ref.second]);
    }
    draining=false;
}
}
