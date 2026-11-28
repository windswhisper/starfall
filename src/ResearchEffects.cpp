#include "ResearchInternal.h"
#include "ConfigText.h"
#include <Unsorted.h>
#include <HouseClass.h>
#include <BuildingTypeClass.h>
#include <FactoryClass.h>
#include <TechnoClass.h>
#include <cmath>

using std::string;
using std::vector;
using namespace ConfigText;
using namespace Research::Detail;

namespace Research::Detail {
// Purchased project IDs are already synchronized and saved per house. Resolve
// their grants once per house, including technologies inside combined projects.
std::map<int,std::set<TechnoTypeClass*>> productionLicenses;
struct ProductionReplacements {
    std::map<TechnoTypeClass*,TechnoTypeClass*> from,to;
};
std::map<int,ProductionReplacements> productionReplacements;
std::set<TechnoTypeClass*> replacementTargets;
bool replacementTargetsReady=false;
bool requiresReplacement(TechnoTypeClass* type) {
    if(!replacementTargetsReady) {
        replacementTargetsReady=true;
        auto collect=[](const auto& effect) {
            if(effect.effect==Research::Effect::ReplaceProduction)
                if(auto to=TechnoTypeClass::Find(effect.to.c_str())) replacementTargets.insert(to);
        };
        for(const auto& bond:Research::bonds) for(const auto& tier:bond.tiers) for(const auto& e:tier.effects)
            if(e.effect==Research::Effect::ReplaceProduction) {
                auto to=TechnoTypeClass::Find(e.to.c_str());
                if(to && to->TechLevel<0) replacementTargets.insert(to);
            }
        for(const auto& option:Research::options) {
            if(option.effectList) {for(const auto& effect:option.effects) collect(effect);}
            else collect(option);
            for(const auto& stage:option.stages) for(const auto& effect:stage.effects)
                if(effect.effect==Research::Effect::ReplaceProduction) {
                    auto to=TechnoTypeClass::Find(effect.to.c_str());
                    if(to && to->TechLevel<0) replacementTargets.insert(to);
                }
        }
    }
    return replacementTargets.contains(type);
}
const ProductionReplacements& replacements(const HouseClass* house) {
    static const ProductionReplacements empty;
    if(!house) return empty;
    auto owned=Research::unlocked.find(house->ArrayIndex);
    if(owned==Research::unlocked.end() || owned->second.empty()) return empty;
    auto [entry,fresh]=productionReplacements.try_emplace(house->ArrayIndex);
    if(fresh) {
        auto collect=[&](const auto& effect) {
            if(effect.effect!=Research::Effect::ReplaceProduction) return;
            auto from=TechnoTypeClass::Find(effect.from.c_str()),to=TechnoTypeClass::Find(effect.to.c_str());
            if(from && to) {entry->second.from.emplace(from,to);entry->second.to.emplace(to,from);}
        };
        for(const auto& option:Research::options) if(owned->second.contains(option.id)) {
            Research::forActiveEffects(house->ArrayIndex,option,collect);
        }
        Research::forBondEffects(house->ArrayIndex,collect);
    }
    return entry->second;
}
bool hasReplacement(const HouseClass* owner,const EffectSpec& spec) {
    const auto& mappings=replacements(owner).from;
    auto it=mappings.find(TechnoTypeClass::Find(spec.from.c_str()));
    return it!=mappings.end() && it->second==TechnoTypeClass::Find(spec.to.c_str());
}
bool hasProductionLicense(const HouseClass* house,TechnoTypeClass* type) {
    if(!house || !type || Research::unlocked.empty()) return false;
    const int player=house->ArrayIndex;
    auto owned=Research::unlocked.find(player);
    if(owned==Research::unlocked.end() || owned->second.empty()) return false;
    auto [entry,fresh]=productionLicenses.try_emplace(player);
    if(fresh) {
        auto collect=[&](const auto& effect) {
            if(effect.effect==Research::Effect::Technology)
                if(auto* item=TechnoTypeClass::Find(effect.technology.c_str())) entry->second.insert(item);
        };
        for(const auto& option:Research::options) if(owned->second.contains(option.id)) {
            Research::forActiveEffects(house->ArrayIndex,option,collect);
        }
        Research::forBondEffects(house->ArrayIndex,collect);
    }
    return entry->second.contains(type);
}
bool useLicensedProduction(const HouseClass* house,TechnoTypeClass* type) {
    return hasProductionLicense(house,type) && type->FindFactory(true,false,false,house);
}
// Ares replaces the entire CanBuild routine. Forward to the installed checker
// with its build-limit-only argument so its factory and limit checks still run.
using ProductionCheck=CanBuildResult(__thiscall*)(const HouseClass*,TechnoTypeClass*,bool,bool);
ProductionCheck previousProductionCheck=nullptr;
CanBuildResult __fastcall checkLicensedProduction(const HouseClass* house,void*,TechnoTypeClass* type,bool limitsOnly,bool queued) {
    const auto& replace=replacements(house);
    if(replace.from.contains(type)) return CanBuildResult::Unbuildable;
    if(auto it=replace.to.find(type);it!=replace.to.end()) {
        // Check the original's country, prerequisites and factory eligibility,
        // then the replacement's own limits. Never mutate shared type rules.
        auto result=previousProductionCheck(house,it->second,useLicensedProduction(house,it->second),queued);
        if(result!=CanBuildResult::Buildable) return result;
        return previousProductionCheck(house,type,true,queued);
    }
    // Native AI eligibility can bypass TechLevel=-1. Keep replacement-only
    // types hidden for every unlicensed house, including AI production.
    if(requiresReplacement(type) && !hasProductionLicense(house,type)) return CanBuildResult::Unbuildable;
    return previousProductionCheck(house,type,limitsOnly || useLicensedProduction(house,type),queued);
}
bool nativeProductionPrerequisite=false;
void installProductionCheck() {
    if(previousProductionCheck) return;
    auto entry=reinterpret_cast<BYTE*>(0x4F7870);
    if(entry[0]!=0xE9) return; // The original game takes the native hook below.
    DWORD old;
    if(!VirtualProtect(entry,5,PAGE_EXECUTE_READWRITE,&old)) {
        OutputDebugStringA("Starfall: cannot chain production availability check\n");return;
    }
    int displacement;
    memcpy(&displacement,entry+1,sizeof(displacement));
    auto next=reinterpret_cast<uintptr_t>(entry+5);
    previousProductionCheck=reinterpret_cast<ProductionCheck>(next+displacement);
    displacement=static_cast<int>(reinterpret_cast<uintptr_t>(&checkLicensedProduction)-next);
    memcpy(entry+1,&displacement,sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(),entry,5);
    VirtualProtect(entry,5,old,&old);
}
} // namespace
// Vanilla has loaded buildLimitOnly into AL before pushing EDI.
DEFINE_HOOK(0x4F787C,SF_NativeProductionLicense,7) {
    auto house=R->EBP<HouseClass*>();auto type=R->Stack<TechnoTypeClass*>(0x4C);
    if(!nativeProductionPrerequisite) {
        const auto& replace=replacements(house);
        // EDI has not been pushed yet: enter each epilogue after its pop edi.
        if(replace.from.contains(type)) return 0x4F80BE;
        if(auto it=replace.to.find(type);it!=replace.to.end()) {
            nativeProductionPrerequisite=true;
            auto result=house->CanBuild(it->second,useLicensedProduction(house,it->second),R->Stack<bool>(0x54));
            nativeProductionPrerequisite=false;
            if(result==CanBuildResult::Unbuildable) return 0x4F80BE;
            if(result==CanBuildResult::TemporarilyUnbuildable) return 0x4F833C;
            R->AL(1);
        } else if(requiresReplacement(type) && !hasProductionLicense(house,type)) return 0x4F80BE;
    }
    if(!R->AL() && useLicensedProduction(house,type)) R->AL(1);
    return 0;
}
namespace Research::Detail {
bool matchesConversion(TechnoClass* unit,HouseClass* h,TechnoTypeClass* from,TechnoTypeClass* to) {
    return unit && unit->Owner==h && (unit->IsAlive && unit->Health>0) && unit->GetTechnoType()==from && Runtime::canTransformUnit(unit,to);
}
bool validateEffect(const Research::EffectSpec& o,const EffectWarning& warn) {
    using Research::Effect;
    auto fail=[&](const char* key,const char* reason) {if(warn) warn(key,reason);return false;};
    switch(o.effect) {
        case Effect::Passive: {
            bool valid=true;
            if(!Runtime::hasPassive(o.passive)) valid=fail("Passive","被动 ID 未注册、未填写或该被动已停用");
            if(o.types.empty()) return fail("TargetTypes","至少填写一个受益类型或类别");
            for(const auto& type:o.types) if(type.starts_with('@') && !Research::isTargetCategory(type))
                return fail("TargetTypes","未知的受益类别");
            return valid;
        }
        case Effect::TypeCost: {
            bool valid=true;
            if(!o.cost.delta) valid=fail("CostModifier","价格修正须为非零整数或百分比");
            if(o.types.empty()) return fail("TargetTypes","须填写有效的生产类型 ID");
            for(const auto& id:o.types) if(!TechnoTypeClass::Find(id.c_str())) {
                if(warn) warn("TargetTypes","未注册的生产类型 ["+id+"]");
                return false;
            }
            return valid;
        }
        case Effect::CategoryCost: {
            bool valid=true;
            if(!o.cost.delta || !o.cost.percent) valid=fail("CostModifier","类别价格修正须为非零百分比");
            if(o.cost.category==Research::CostCategory::Invalid) valid=fail("Category","未知的生产类别");
            return valid;
        }
        case Effect::Credits: return o.credits>0 || fail("Credits","须填写正整数资金");
        case Effect::FreeResearch:
            if(o.reward.count!=1) return fail("Reward.Count","一次发放一份免费科研");
            [[fallthrough]];
        case Effect::RandomResearch:
            if(o.reward.count<1 || o.reward.count>256) return fail("Reward.Count","须填写 1 到 256 的整数");
            return !o.reward.rarity || (*o.reward.rarity>=Rarity::Common && *o.reward.rarity<Rarity::Invalid) ||
                fail("Reward.Rarity","应为 Any、Common、Rare、Epic 或 Insane");
        case Effect::Technology: return (!o.technology.empty() && TechnoTypeClass::Find(o.technology.c_str())) ||
            fail("Technology","须填写已注册的单位或建筑类型 ID");
        case Effect::ReplaceProduction: {
            auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
            if(!from) return fail("FromType","须填写已注册的生产类型 ID");
            if(!to || to==from || to->WhatAmI()!=from->WhatAmI())
                return fail("ToType","须填写不同且同类别的生产类型 ID");
            if(from->WhatAmI()==AbstractType::BuildingType &&
               static_cast<BuildingTypeClass*>(from)->BuildCat!=static_cast<BuildingTypeClass*>(to)->BuildCat)
                return fail("ToType","替换建筑须使用相同的建造类别");
            return true;
        }
        case Effect::ConvertInfantry:
        case Effect::ConvertVehicle: {
            auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
            auto kind=o.effect==Effect::ConvertInfantry?AbstractType::InfantryType:AbstractType::UnitType;
            bool valid=true;
            if(!from || from->WhatAmI()!=kind) valid=fail("FromType","原类型不存在或类别不符");
            if(!to || to->WhatAmI()!=kind) valid=fail("ToType","目标类型不存在或类别不符");
            if(!valid) return false;
            if(from==to) return fail("ToType","两个类型相同");
            if(!Runtime::conversionSupportsTypes(from,to))
                return fail("ToType","当前转换接口不支持这组类型");
            return true;
        }
        default: return fail("Effect","未知或缺少科研效果");
    }
}
bool researchEffectCanApply(HouseClass* h,const Research::EffectSpec& o,int source) {
    using Research::Effect;
    if(o.effect==Effect::FreeResearch) return Research::canOfferFree(h->ArrayIndex,h->Type->ID,o.reward.projects,o.reward.rarity,source);
    if(o.effect==Effect::RandomResearch) return Research::canGrantRandom(h->ArrayIndex,source,h->Type->ID,o.reward.rarity);
    if(o.effect==Effect::Passive || o.effect==Effect::TypeCost || o.effect==Effect::CategoryCost) return true;
    if(source>=0 && Research::options[source].repeatable && Research::owns(h->ArrayIndex,Research::options[source].id) &&
        (o.effect==Effect::Technology || o.effect==Effect::ReplaceProduction)) return true;
    if(source>=0 && !Research::options[source].replaces.empty() &&
       (o.effect==Effect::Technology || o.effect==Effect::ReplaceProduction)) {
        const auto& removed=Research::options[source].replaces;
        auto conflicts=[&](const auto& active) {
            if(active.effect!=o.effect) return false;
            if(o.effect==Effect::Technology) return active.technology==o.technology;
            return active.from==o.from || active.from==o.to || active.to==o.from || active.to==o.to;
        };
        for(const auto& project:Research::options) {
            if(!Research::owns(h->ArrayIndex,project.id) || includes(removed,project.id)) continue;
            bool conflict=false;
            Research::forActiveEffects(h->ArrayIndex,project,[&](const auto& effect){if(conflicts(effect)) conflict=true;});
            if(conflict) return false;
        }
        bool bondConflict=false;
        Research::forBondEffects(h->ArrayIndex,[&](const auto& effect){if(conflicts(effect)) bondConflict=true;},source);
        return !bondConflict;
    }
    if(o.effect==Effect::ReplaceProduction) {
        auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
        const auto& replace=replacements(h);
        return from && to && !replace.from.contains(from) && !replace.to.contains(from) &&
            !replace.from.contains(to) && !replace.to.contains(to);
    }
    if(o.effect==Effect::Technology) {
        auto type=TechnoTypeClass::Find(o.technology.c_str());
        return type && !hasProductionLicense(h,type);
    }
    if(o.effect==Effect::ConvertInfantry || o.effect==Effect::ConvertVehicle) {
        auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
        for(auto unit:TechnoClass::Array) if(matchesConversion(unit,h,from,to)) return true;
        return false;
    }
    return o.effect==Effect::Credits;
}
bool applyResearchEffect(HouseClass* h,const Research::EffectSpec& o,int source) {
    using Research::Effect;
    if(!researchEffectCanApply(h,o,source)) return false;
    if(source>=0 && Research::owns(h->ArrayIndex,Research::options[source].id) &&
        (o.effect==Effect::Technology || o.effect==Effect::ReplaceProduction)) return true;
    if(o.effect==Effect::RandomResearch || o.effect==Effect::FreeResearch) return true; // Dispatched after ownership commits.
    if(o.effect==Effect::Passive || o.effect==Effect::TypeCost || o.effect==Effect::CategoryCost) return true;
    if(o.effect==Effect::Credits) {h->GiveMoney(o.credits);return true;}
    if(o.effect==Effect::Technology) {
        h->RecheckTechTree=true;return true;
    }
    if(o.effect==Effect::ReplaceProduction) {
        auto from=TechnoTypeClass::Find(o.from.c_str());
        for(auto factory:FactoryClass::Array) if(factory->Owner==h) {
            while(factory->RemoveOneFromQueue(from)) {}
            if(factory->Object && factory->Object->GetTechnoType()==from) factory->AbandonProduction();
        }
        h->RecheckTechTree=true;return true;
    }
    auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
    vector<TechnoClass*> targets;
    for(auto unit:TechnoClass::Array) if(matchesConversion(unit,h,from,to)) targets.push_back(unit);
    int changed=0;
    for(auto unit:targets) if(matchesConversion(unit,h,from,to) && Runtime::transformUnit(unit,to)) ++changed;
    return changed>0;
}
}
bool Research::available(int option) {
    if(!isEnabled() || option<0 || option>=static_cast<int>(options.size())) return false;
    const auto& o=options[option];
    if(o.rarity==Rarity::Invalid) return false;
    if(o.stagesOnly) return !o.stages.empty();
    if(!o.effectList) return validateEffect(singleEffect(o));
    std::set<string> replacementTypes;
    for(const auto& effect:o.effects) if(effect.effect==Effect::ReplaceProduction) {
        if(!replacementTypes.insert(effect.from).second || !replacementTypes.insert(effect.to).second) return false;
    }
    return !o.effects.empty() && std::all_of(o.effects.begin(),o.effects.end(),[](const auto& effect){ return validateEffect(effect); });
}
bool Research::canApply(int house,int option) {
    if(!isEnabled() || option<0 || option>=static_cast<int>(options.size()) || house<0 || house>=HouseClass::Array.Count) return false;
    auto h=HouseClass::Array.Items[house];if(!Runtime::eligibleHouse(h)) return false;
    const auto& o=options[option];
    if(o.stagesOnly) return !o.stages.empty();
    if(!o.effectList) return researchEffectCanApply(h,singleEffect(o),option);
    // Ownership enables every permanent replacement in the project. Reject the
    // whole bundle if one would conflict instead of recording a skipped mapping.
    for(const auto& effect:o.effects) if(effect.effect==Effect::ReplaceProduction && !researchEffectCanApply(h,effect,option)) return false;
    return std::any_of(o.effects.begin(),o.effects.end(),[h,option](const auto& effect){return researchEffectCanApply(h,effect,option);});
}
bool Research::executeEffect(int house,int option) {
    if(!canApply(house,option)) return false;
    auto h=HouseClass::Array.Items[house];const auto& o=options[option];
    if(!o.replaces.empty()) h->RecheckTechTree=true;
    if(o.stagesOnly) return true;
    if(!o.effectList) return applyResearchEffect(h,singleEffect(o),option);
    bool applied=false;
    for(const auto& effect:o.effects) if(applyResearchEffect(h,effect,option)) applied=true;
    return applied;
}
namespace Research::Detail {
void announceReward(int house,int option) {
    if(house>=0 && house<HouseClass::Array.Count) showResearchSelection(HouseClass::Array.Items[house],option);
}
struct CostAdjustment {
    double percent=0, credits=0;
    bool active=false;
};
using PriceKey=std::pair<int,TechnoTypeClass*>;
std::map<PriceKey,CostAdjustment> priceCache;
Research::CostCategory sidebarCategory(TechnoTypeClass* type) {
    using C=Research::CostCategory;
    switch(type->WhatAmI()) {
        case AbstractType::BuildingType:
            return static_cast<BuildingTypeClass*>(type)->BuildCat==BuildCat::Combat?C::Defense:C::Building;
        case AbstractType::InfantryType: return C::Infantry;
        case AbstractType::UnitType:
        case AbstractType::AircraftType: return C::Vehicle;
        default: return C::Invalid;
    }
}
CostAdjustment costAdjustment(HouseClass* house,TechnoTypeClass* type) {
    const auto owned=Research::unlocked.find(house->ArrayIndex);
    if(owned==Research::unlocked.end() || owned->second.empty()) return {};
    Research::ensureReady();
    auto [it,added]=priceCache.try_emplace({house->ArrayIndex,type});
    auto& sum=it->second;
    if(!added) return sum;
    const auto id=lower(type->ID);
    const auto category=sidebarCategory(type);
    auto collect=[&](const auto& e,int copies) {
        using Research::Effect;
        const bool byType=e.effect==Effect::TypeCost && includes(e.types,id);
        const bool byCategory=e.effect==Effect::CategoryCost && e.cost.category==category && e.cost.percent;
        if(!byType && !byCategory) return;
        if(!e.cost.delta) return;
        (e.cost.percent?sum.percent:sum.credits)+=static_cast<double>(e.cost.delta)*copies;
        sum.active=true;
    };
    for(const auto& project:Research::options) if(owned->second.contains(project.id)) {
        int copies=Research::acquisitionCount(house->ArrayIndex,project.id);
        Research::forBaseEffects(project,[&](const auto& e){collect(e,copies);});
        Research::forStageEffects(house->ArrayIndex,project,[&](const auto& e){collect(e,1);});
    }
    Research::forBondEffects(house->ArrayIndex,[&](const auto& e){collect(e,1);});
    return sum;
}
}
// GetActualCost has already applied country, difficulty and factory plant modifiers.
DEFINE_HOOK(0x711F46,SF_ResearchProductionPrice,5) {
    const int price=R->EAX<int>();
    if(price<0 || Research::unlocked.empty()) return 0;
    const auto mod=costAdjustment(R->EDI<HouseClass*>(),R->ESI<TechnoTypeClass*>());
    if(mod.active) {
        double adjusted=price*(100.0+mod.percent)/100.0+mod.credits;
        R->EAX(static_cast<int>(std::round(std::clamp(adjusted,0.0,static_cast<double>(INT_MAX)))));
    }
    return 0;
}
// The vanilla cancellation code re-quotes the product. Its Value holds the
// original order price, including for orders started before buying a project.
DEFINE_HOOK(0x4CA03D,SF_ResearchProductionCancel,6) {
    auto factory=R->ESI<FactoryClass*>();
    if(!Research::unlocked.empty() && factory->Owner && factory->Object &&
       costAdjustment(factory->Owner,factory->Object->GetTechnoType()).active)
        R->EAX(factory->Object->Value);
    return 0;
}

void Research::Detail::clearEffectCaches() {
    priceCache.clear();
    productionLicenses.clear();
    productionReplacements.clear();
    replacementTargets.clear();replacementTargetsReady=false;
}
unsigned Research::Detail::matchSeed() { return static_cast<unsigned>(Game::Seed); }
