#include "BuildingResearch.h"
#include "PassiveRules.h"
#include "ConfigText.h"
#include "ResearchInternal.h"
#include "UnitHealth.h"
#include <BuildingClass.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <SessionClass.h>
#include <TargetClass.h>
#include <Unsorted.h>

using std::string;
using std::vector;
using UnitHealth::alive;
using Research::Detail::Runtime::eligibleHouse;

bool Research::Detail::Runtime::eligibleHouse(HouseClass* h) {
    return h && h->ArrayIndex>=0 && !h->Defeated && !h->IsObserver() && !h->IsNeutral();
}

namespace BuildingResearch {
using ConfigText::includes;
using ConfigText::number;
using ConfigText::split;
using PassiveRules::invalidField;
using PassiveRules::read;

namespace {
struct Income { int amount,interval; };
std::map<BuildingTypeClass*,Income> incomeTypes;
struct ProducerSite { BuildingClass* building; Income income; };
std::map<DWORD,ProducerSite> sites;
bool indexed=false;
std::map<BuildingTypeClass*,RewardRule> rewardTypes;
}

void clearTypeCaches() {
    incomeTypes.clear();
    sites.clear();
    indexed=false;
    rewardTypes.clear();
}
const RewardRule& rewardRule(BuildingTypeClass* type) {
    auto [it,fresh]=rewardTypes.try_emplace(type);
    if(fresh) {
        it->second.options=split(read(type->ID,"Starfall.ResearchOptions"));
        auto flag=[&](const char* key) {
            auto value=read(type->ID,key,"no");
            if(includes({"yes","true","1"},value)) return true;
            if(!includes({"no","false","0"},value)) invalidField(type->ID,key);
            return false;
        };
        it->second.engineer=flag("Starfall.ResearchOptions.AllowEngineerCapture");
        it->second.spy=flag("Starfall.ResearchOptions.AllowSpyInfiltration");
    }
    return it->second;
}
const vector<string>& rewardOptions(BuildingTypeClass* type) {
    return rewardRule(type).options;
}
void tickGlobalResearch() {
    auto& state=Research::globalIncome;
    unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    if(state.initialized && (!state.periodicPoints || !state.intervalFrames || frame-state.lastFrame<state.intervalFrames)) return;
    std::set<int> players;
    auto add=[&](HouseClass* house) {if(eligibleHouse(house)) players.insert(house->ArrayIndex);};
    for(int slot=0;slot<8;++slot) add(HouseClass::FindByPlayerAt(HouseClass::PlayerAtA+slot));
    if(SessionClass::IsCampaign()) add(HouseClass::CurrentPlayer);
    if(players.empty()) return;
    if(!state.initialized) {
        for(int house:players) Research::award(house,state.initialPoints);
        state.initialized=true;state.lastFrame=frame;
    } else if(state.periodicPoints>0 && state.intervalFrames>0 && frame-state.lastFrame>=state.intervalFrames) {
        for(int house:players) Research::award(house,state.periodicPoints);
        state.lastFrame=frame;
    }
}
void trackBuilding(BuildingClass* building) {
    if(incomeTypes.empty() || !building) return;
    auto rule=incomeTypes.find(building->Type);
    if(rule!=incomeTypes.end()) {
        sites.insert_or_assign(building->UniqueID,ProducerSite{building,rule->second});
    } else if(sites.erase(building->UniqueID)) {
        // A converted building keeps its ID, but may stop producing points.
        Research::producers.erase(building->UniqueID);
    }
}
void forget(unsigned long id) {
    sites.erase(id);
    Research::producers.erase(id);
    Research::buildingRewards.erase(id);
}
void ownerChanged(BuildingClass* building) {
    if(!building->Owner) return;
    const auto id=building->UniqueID;
    const int owner=building->Owner->ArrayIndex;
    auto timer=Research::producers.find(id);
    if(timer!=Research::producers.end() && timer->second.owner!=owner) Research::producers.erase(timer);
    auto reward=Research::buildingRewards.find(id);
    if(reward!=Research::buildingRewards.end() && reward->second.owner!=owner) {
        if(reward->second.claimed) reward->second.owner=owner;
        else Research::buildingRewards.erase(reward);
    }
}
static void indexProducers() {
    for(auto type:BuildingTypeClass::Array) {
        int amount=number(read(type->ID,"Starfall.ResearchPoints","0"));
        if(amount<=0) continue;
        int interval=number(read(type->ID,"Starfall.ResearchInterval","150"));
        if(interval>0) incomeTypes.emplace(type,Income{amount,interval});
    }
    if(!incomeTypes.empty()) for(auto building:BuildingClass::Array) trackBuilding(building);
    std::erase_if(Research::producers,[](const auto& entry){return !sites.contains(entry.first);});
    indexed=true;
}
void tickBuildings() {
    if(!Research::isEnabled()) return;
    if(!indexed) indexProducers();
    tickGlobalResearch();
    for(const auto& [id,site]:sites) {
        auto b=site.building;
        const auto& income=site.income;
        if(!eligibleHouse(b->Owner) || !alive(b) || !b->IsOnMap || b->InLimbo ||
            b->CurrentMission==Mission::Selling || b->CurrentMission==Mission::Construction) {
            Research::producers.erase(id); continue;
        }
        int owner=b->Owner->ArrayIndex;
        auto [entry,added]=Research::producers.try_emplace(id,Research::Producer{owner,0});
        auto& timer=entry->second;
        if(timer.owner!=owner) timer={owner,0};
        if(b->IsUnderEMP() || !b->IsPowerOnline() || b->BeingWarpedOut) continue;
        if(++timer.elapsed>=income.interval) {
            Research::award(owner,income.amount); timer.elapsed=0;
        }
    }
    const unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    if(Research::allowAIResearch && frame%15==0) {
        for(auto* house:HouseClass::Array) {
            if(!house || !house->Type || !eligibleHouse(house) || house->IsControlledByHuman()) continue;
            int option=-1;
            for(const auto& [id,ticket]:Research::freeTickets) {
                if(ticket.house!=house->ArrayIndex) continue;
                auto rows=Research::ticketOptions(id,ticket.house,house->Type->ID);
                for(int i:rows) if(Research::claimFree(Research::ticketFlag|id,house->ArrayIndex,i)) {option=i;break;}
                if(option>=0) break;
            }
            if(option<0) option=Research::purchaseForAI(house->ArrayIndex,house->Type->ID,frame);
            if(option>=0) Research::Detail::showResearchSelection(house,option);
        }
    }
}
void authorizeBuildingReward(InfantryClass* infantry,BuildingClass* building,Research::BuildingRewardSource source) {
    if(source==Research::BuildingRewardSource::None) return;
    if(Research::isEnabled() && infantry && eligibleHouse(infantry->Owner) && building && building->Owner && building->Type && alive(building) && !rewardOptions(building->Type).empty()) {
        const auto& rule=rewardRule(building->Type);
        if(source==Research::BuildingRewardSource::Engineer && (!rule.engineer || !infantry->Type || !infantry->Type->Engineer || building->Owner!=infantry->Owner)) return;
        if(source==Research::BuildingRewardSource::Spy && !rule.spy) return;
        auto [it,added]=Research::buildingRewards.try_emplace(building->UniqueID,Research::BuildingReward{building->Owner->ArrayIndex,-1,false});
        auto& reward=it->second;
        if(reward.claimed) return;
        const int house=infantry->Owner->ArrayIndex;
        const bool newlyGranted=reward.owner!=building->Owner->ArrayIndex || reward.authorized!=house;
        reward.owner=building->Owner->ArrayIndex;reward.authorized=house;reward.source=source;
        if(newlyGranted && !Research::buildingOptions(building->UniqueID,house).empty())
            Research::Detail::notifyFreeResearch(house);
    }
}
} // namespace BuildingResearch

static BuildingClass* rewardBuilding(unsigned long id) {
    TargetClass target;
    target.m_ID=id;
    target.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    return target.As_Building();
}

std::vector<int> Research::buildingOptions(unsigned long id,int house) {
    std::vector<int> result;
    if(!isEnabled()) return result;
    auto state=buildingRewards.find(id);
    if(state==buildingRewards.end() || state->second.claimed || state->second.authorized!=house) return result;
    auto building=rewardBuilding(id);
    if(!building || !building->Type || !alive(building) || !building->IsOnMap || building->InLimbo || !building->Owner || building->Owner->ArrayIndex!=state->second.owner) return result;
    const auto& rule=BuildingResearch::rewardRule(building->Type);
    if((state->second.source==BuildingRewardSource::Engineer && !rule.engineer) ||
       (state->second.source==BuildingRewardSource::Spy && !rule.spy) ||
       state->second.source==BuildingRewardSource::None) return result;
    return Detail::buildingOfferRows(id,house,rule.options);
}
std::vector<unsigned long> Research::pendingBuildingRewards(int house) {
    std::vector<unsigned long> result;
    for(const auto& [id,reward]:buildingRewards)
        if(reward.authorized==house && !reward.claimed && !buildingOptions(id,house).empty()) result.push_back(id);
    return result;
}
bool Research::claimBuilding(unsigned long id,int house,int option) {
    return claimFree(id,house,option);
}
std::vector<Research::RewardId> Research::pendingFreeRewards(int house) {
    const auto buildings=pendingBuildingRewards(house);
    std::vector<RewardId> result(buildings.begin(),buildings.end());
    if(house<0 || house>=HouseClass::Array.Count) return result;
    const auto h=HouseClass::Array.Items[house];
    if(h && h->Type) for(const auto& [id,ticket]:freeTickets)
        if(ticket.house==house && !ticketOptions(id,house,h->Type->ID).empty()) result.push_back(ticketFlag|id);
    return result;
}
std::vector<int> Research::freeOptions(RewardId id,int house) {
    if(!(id&ticketFlag)) return buildingOptions(static_cast<unsigned long>(id),house);
    if(house<0 || house>=HouseClass::Array.Count) return {};
    const auto h=HouseClass::Array.Items[house];
    return h && h->Type?ticketOptions(static_cast<unsigned long>(id),house,h->Type->ID):vector<int>{};
}
bool Research::claimFree(RewardId id,int house,int option) {
    const auto board=freeOptions(id,house);
    if(std::find(board.begin(),board.end(),option)==board.end() || !executeEffect(house,option)) return false;
    const auto serial=static_cast<unsigned long>(id);
    if(id&ticketFlag) freeTickets.erase(serial);
    else {
        if(auto building=rewardBuilding(serial); building && building->Owner) {
            buildingRewards[serial]={building->Owner->ArrayIndex,-1,true,BuildingRewardSource::None};
        }
        refreshedSlots.erase(house);
    }
    Detail::recordAcquisition(house,option);
    const auto country=HouseClass::Array.Items[house]->Type->ID;
    savedOffers.erase(house);seenOffers.erase(house);clearOfferCache();
    if(id&ticketFlag) offers(house,country);
    grantProjectRewards(house,option,country);
    return true;
}

DEFINE_HOOK(0x519F84, SF_EngineerResearchBuilding, 6) {
    BuildingResearch::authorizeBuildingReward(R->ESI<InfantryClass*>(),R->EDI<BuildingClass*>(),Research::BuildingRewardSource::Engineer);
    return 0;
}
DEFINE_HOOK(0x51A002, SF_SpyResearchBuilding, 6) {
    BuildingResearch::authorizeBuildingReward(R->ESI<InfantryClass*>(),R->EDI<BuildingClass*>(),Research::BuildingRewardSource::Spy);
    return 0;
}
