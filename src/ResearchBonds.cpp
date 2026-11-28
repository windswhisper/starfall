#include "ResearchInternal.h"
#include "ConfigText.h"
#include <HouseClass.h>
#include <deque>

namespace Research {
std::vector<Bond> bonds;
std::map<std::string,int> bondIndex;
std::map<std::string,std::vector<int>> memberBonds;
BondProgress bondProgress;
unsigned bondRevision=0;

int bondCount(int house,const Bond& bond,int source) {
    int count=0;
    for(const auto& id:bond.members) {
        if(source>=0 && options[source].id==id) ++count;
        else if(owns(house,id) && (source<0 || !ConfigText::includes(options[source].replaces,id))) ++count;
    }
    return count;
}
bool knownResearchVariable(const std::string& key) {
    return key.starts_with('#')?bondIndex.contains(key.substr(1)):counterProjects.contains(key);
}

void Detail::checkBonds(int house,int acquired) {
    if(!enabled || bonds.empty()) return;
    auto owner=HouseClass::Array.GetItemOrDefault(house);
    if(!Runtime::eligibleHouse(owner)) return;
    std::set<int> affected;
    auto add=[&](const std::string& id) {
        auto it=memberBonds.find(id);
        if(it!=memberBonds.end()) affected.insert(it->second.begin(),it->second.end());
    };
    if(acquired<0) for(int i=0;i<static_cast<int>(bonds.size());++i) affected.insert(i);
    else {
        add(options[acquired].id);
        for(const auto& id:options[acquired].replaces) add(id);
    }
    struct Activation {int house,bond,tier;bool first;};
    static std::deque<Activation> pending;
    static bool draining=false;
    bool withdrawn=false;
    for(int index:affected) {
        const auto& bond=bonds[index];
        const int count=bondCount(house,bond);
        for(int j=0;j<static_cast<int>(bond.tiers.size());++j) {
            const auto& tier=bond.tiers[j];
            StageKey key{house,bond.id,tier.id};
            if(count<tier.required) {
                auto it=bondProgress.find(key);
                if(it!=bondProgress.end() && it->second.active) {
                    it->second.active=false;it->second.grants.clear();withdrawn=true;
                }
                continue;
            }
            auto [it,first]=bondProgress.try_emplace(key);
            if(it->second.active) continue;
            it->second.active=true;
            pending.push_back({house,index,j,first});
        }
    }
    if(withdrawn) {owner->RecheckTechTree=true;clearOfferCache();}
    if(draining) return;
    draining=true;
    while(!pending.empty()) {
        auto task=pending.front();pending.pop_front();
        const auto& bond=bonds[task.bond];
        const auto& tier=bond.tiers[task.tier];
        auto& state=bondProgress.at({task.house,bond.id,tier.id});
        auto recipient=HouseClass::Array.GetItemOrDefault(task.house);
        for(const auto& effect:tier.effects) {
            if(persistentEffect(effect.effect)) {
                if(!state.active || state.grants.contains(effect.id)) continue;
            } else if(!task.first) continue;
            if(activateStageEffect(recipient,effect)) {
                state.grants.insert(effect.id);
                clearOfferCache();
            }
        }
    }
    draining=false;
}
}
