#include "HealthFormula.h"
#include "UnitHealth.h"
#include "Research.h"
#include "ResearchInternal.h"
#include "ConfigText.h"
#include "ResearchAudio.h"
#include "VisualScale.h"
#include "SonicRipple.h"
#include "LuaTimers.h"
#include "ActiveSkills.h"
#include "../modules/ActSp/CommandBarOrders.h"
#include "BuildingSpawn.h"
#include "BuildingTransform.h"
#include "CarrierConversion.h"
#include "BuildingResearch.h"
#include "ForceMove.h"
#include "PassiveRules.h"
#include "../modules/ActSp/ActSp.h"
#include "ActionSequence.h"
#include "../modules/ArtSp/Tracers.h"
#include "../modules/ArtSp/TipsLabel.h"
#include "../modules/ArtSp/ContinuousBeam.h"
#include "../modules/ActSp/BetterGarrison.h"
#include "../modules/ActSp/GuardPoints.h"
#include "../modules/ActSp/HighPlatforms.h"
#include "../modules/ActSp/EntityWalls.h"
#include "../modules/ActSp/Piercing.h"
#include "../modules/ActSp/ProjectileScript.h"
#include "../modules/ActSp/FireOrigin.h"
#include <filesystem>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <deque>
#include <functional>
#include <random>
#include <Unsorted.h>
#include <charconv>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <TechnoClass.h>
#include <AnimClass.h>
#include <ParticleSystemClass.h>
#include <EBolt.h>
#include <LaserDrawClass.h>
#include <VocClass.h>
#include <GScreenClass.h>
#include <unordered_map>
#include <unordered_set>
#include <HouseClass.h>
#include <FactoryClass.h>
#include <SuperClass.h>
#include <EventClass.h>
#include "UnitMount.h"
#include <ScenarioClass.h>
#include <SessionClass.h>
#include <RulesClass.h>
#include <BuildingClass.h>
#include <BulletClass.h>
#include <BombClass.h>
#include <BombListClass.h>
#include <RocketLocomotionClass.h>
#include <AircraftTrackerClass.h>
#include <AircraftClass.h>
#include <SpawnManagerClass.h>
#include <BulletTypeClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <MapClass.h>
#include <TacticalClass.h>
#include <ToolTipManager.h>
#include <StringTable.h>
#include <LocomotionClass.h>
#include <Powerups.h>
#include <FileSystem.h>
#include <OverlayTypeClass.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <DriveLocomotionClass.h>
#include <JumpjetLocomotionClass.h>
#include <TActionClass.h>
#include <TargetClass.h>
#include <TagClass.h>
#include <TriggerClass.h>
#include <TeamClass.h>
#include <CCINIClass.h>

using std::string;
using std::vector;

namespace {
using ConfigText::trim;
using ConfigText::lower;
using ConfigText::split;
using ConfigText::number;
using ConfigText::includes;
using ConfigText::displayText;
using ConfigText::configLog;
using namespace PassiveRules;
using UnitHealth::alive;
using Research::Detail::Runtime::eligibleHouse;

const char* registry = "Starfall.PassiveTypes";
LuaTimers::Store scriptTimers;
std::map<string,ScriptRuntime::Module> scriptModules;
std::map<string,ScriptFields> scriptDefinitions;
std::map<string,vector<string>> scriptBindings;
using Factors=std::array<double,3>; // speed, firepower, armor
constexpr Factors identity={1.0,1.0,1.0};
constexpr Factors defaultStatPercent={100,100,100};
std::map<DWORD,int> projectileBase;
std::map<DWORD,double> projectileState;
std::map<DWORD,double> rangeBase;
std::map<DWORD,double> rangeState;
std::map<DWORD,double> fireRateBase;
std::map<DWORD,double> fireRateState;
std::map<DWORD,double> experienceState;
// A kill can dispatch another kill during experience distribution. Key the original
// score by the engine's call frame so each invocation restores its own points.
std::map<DWORD,int> unscaledKillScores;

double unitFactor(DWORD id,const std::map<DWORD,double>& bases,const std::map<DWORD,double>& states) {
    const auto base=bases.find(id);
    const auto state=states.find(id);
    return (base==bases.end()?1.0:base->second/100.0)*(state==states.end()?1.0:state->second);
}

int adjustedRearmDelay(const TechnoClass* unit,int frames) {
    if(frames<=0 || (fireRateBase.empty() && fireRateState.empty())) return frames;
    const double rate=unitFactor(unit->UniqueID,fireRateBase,fireRateState);
    if(rate==1.0) return frames;
    return static_cast<int>(std::lround(std::clamp(frames/rate,1.0,static_cast<double>(INT_MAX))));
}

int adjustedWeaponRange(const TechnoClass* unit,int range) {
    if(range<=0 || (rangeBase.empty() && rangeState.empty())) return range;
    const double factor=unitFactor(unit->UniqueID,rangeBase,rangeState);
    if(factor==1.0) return range;
    // Guard scans double this result using signed int arithmetic.
    return static_cast<int>(std::lround(std::clamp(range*factor,1.0,static_cast<double>(INT_MAX/2))));
}

// The virtual getter supplies targeting and pursuit distances. Chain the
// installed getter so replacement weapons and other extensions resolve first.
using RangeGetter=int(__thiscall*)(const TechnoClass*,int);
template<class T> RangeGetter originalRangeGetter=nullptr;
template<class T> int __fastcall queryWeaponRange(const TechnoClass* unit,void*,int slot) {
    return adjustedWeaponRange(unit,originalRangeGetter<T>(unit,slot));
}
template<class T> void installRangeGetter() {
    if(originalRangeGetter<T>) return;
    auto slot=reinterpret_cast<RangeGetter*>(T::AbsVTable+0x168);
    DWORD protection;
    if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection)) {
        OutputDebugStringA("Starfall: could not install weapon range query\n");return;
    }
    originalRangeGetter<T> = *slot;
    *slot=reinterpret_cast<RangeGetter>(&queryWeaponRange<T>);
    VirtualProtect(slot,sizeof(*slot),protection,&protection);
}
struct PercentRange {
    int min,max;
    template<class T> T clamp(T value) const {
        return std::clamp<T>(value,min,max);
    }
};
// Same order as percentActions: speed, firepower, armor, scale, projectile scale, range, fire rate.
constexpr std::array defaultPercentLimits={
    PercentRange{1,300}, PercentRange{1,1000}, PercentRange{1,1000}, PercentRange{25,300},
    PercentRange{1,300}, PercentRange{1,300}, PercentRange{1,300}
};
static_assert(defaultPercentLimits.size()==percentActions.size());
auto percentLimits=defaultPercentLimits;
constexpr int unknownState=-1;
struct PickupType {
    Skill reward{};
    string image;
    int lifetime=-1;
};
struct GroundPickup {
    string type;
    unsigned born;
};
using PickupCell=std::pair<short,short>;
std::map<string,PickupType> pickupTypes;
std::map<PickupCell,GroundPickup> groundPickups;
std::map<string,std::unique_ptr<SHPReference>> pickupImages;
std::map<string, Skill> skills;
std::map<string, Skill> weaponScripts;
std::map<WeaponTypeClass*,const Skill*> weaponFireScripts;
std::map<string, vector<string>> bindings;
std::map<string,string> mapPassiveBindings;
// Native map overrides are discarded from INI_Rules after loading the scenario.
std::map<string,bool> mapPassengersBySize;
bool ready = false;
bool anyAuras=false;
constexpr unsigned auraInterval=15;
using AuraKey=std::pair<DWORD,string>;
struct AuraSource {
    TechnoClass* unit=nullptr;
    int owner=-1;
    vector<string> grants;
    std::set<DWORD> targets;
};
std::map<AuraKey,AuraSource> auraSources;
// Count sources independently; expose each passive ID to the unit only once.
std::map<DWORD,std::map<string,unsigned>> auraGrants;
std::map<DWORD,std::set<string>> auraLosses;
bool discoverAuras=true;
bool anySpawnPassives=false;
bool anyLinkedPassives=false;
std::map<DWORD,std::set<string>> linkedGrants;
bool anyTransportPassives=false;
struct TransportGrant {
    unsigned sources=0, layers=0;
    bool operator==(const TransportGrant&) const = default;
};
std::map<DWORD,std::map<string,TransportGrant>> transportGrants;
std::set<std::pair<DWORD,string>> transportStateKeys;
struct SpawnGrant {
    DWORD parent=0;
    vector<string> ids;
    bool operator==(const SpawnGrant&) const = default;
};
std::map<DWORD,SpawnGrant> spawnGrants;
bool anyGainSkills=false;
std::map<DWORD,vector<string>> gainSeen;
// Re-check bindings when their inputs change;
// timed grants and explicit removals update ownership directly.
struct GainInputs {
    TechnoTypeClass* type=nullptr;
    HouseClass* owner=nullptr;
    size_t unlocked=0;
    bool onMap=false;
    bool operator==(const GainInputs&) const = default;
};
std::unordered_map<DWORD,GainInputs> gainInputs;
bool anyIntervals=false;
bool anyMultiShot=false;
bool anyBeforeDealDamage=false;
bool anyBeforeTakeDamage=false;
struct IncomingDamage { TechnoClass* unit; int* amount; bool nonpositive; };
std::map<DWORD,IncomingDamage> incomingDamage;
bool anyStateSkills=false, observePromotions=false, observeDeployments=false;
bool needUnitPower=false,needMotion=false;

namespace UnitOrders {void readDefinitions(); bool registered(const string&); bool isAutoEnter(const string&);}
bool anyPassiveVisuals=false;
#include "runtime/PassiveRegistration.inc"

struct Progress { int count=0; unsigned next=0; };
using Counts = std::map<DWORD,std::map<string,Progress>>;
Counts counts;
// Successful capped activations survive temporary passive removal and ownership changes.
std::map<DWORD,std::map<string,unsigned>> triggerTotals;
// Only nonzero values are stored; layers belong to one unit and one passive ID.
std::map<DWORD,std::map<string,int>> passiveStacks;
int manualStackCount(DWORD unit,const string& skill) {
    auto owner=passiveStacks.find(unit);
    if(owner!=passiveStacks.end()) if(auto value=owner->second.find(skill);value!=owner->second.end()) return value->second;
    return 0;
}
int64_t suppliedStacks(DWORD unit,const string& skill,int research) {
    int64_t layers=research;
    if(auto carrier=transportGrants.find(unit);carrier!=transportGrants.end())
        if(auto grant=carrier->second.find(skill);grant!=carrier->second.end()) layers+=grant->second.layers;
    return layers;
}
// Ownership supplies the first layer. Stackable sources count their first grant once.
// Saves only store manual additions, so existing units need no per-unit migration.
int stackCount(DWORD unit,const string& skill,int research=0) {
    auto supplied=suppliedStacks(unit,skill,research);
    auto def=skills.find(skill);
    int maximum=def==skills.end()?INT_MAX:def->second.stackMax;
    return static_cast<int>(std::min<int64_t>(maximum,manualStackCount(unit,skill)+std::max<int64_t>(1,supplied)));
}
void eraseStacks(DWORD unit,const string& skill) {
    auto found=passiveStacks.find(unit);
    if(found==passiveStacks.end()) return;
    found->second.erase(skill);
    if(found->second.empty()) passiveStacks.erase(found);
}
bool changeStacks(DWORD unit,const string& id,int amount,int maximum,int research=0) {
    int base=manualStackCount(unit,id);
    int before=stackCount(unit,id,research);
    int upper=amount>0?std::max(base,std::max(0,maximum-(before-base))):maximum;
    int after=static_cast<int>(std::clamp<int64_t>(int64_t(base)+amount,0,upper));
    if(after==base) return false;
    if(after) passiveStacks[unit][id]=after;
    else eraseStacks(unit,id);
    return true;
}
using PassengerPassives=std::map<string,int>;
struct SignalChain { DWORD id=0,remaining=1024; };
DWORD nextSignalChain=1;
struct LandingOrder {
    DWORD unit,house,started;
    CellStruct cell;
    string from,to,skill;
    int nextDraw;
};
struct Event {
    string trigger, kind, relation;
    DWORD holder, attacker, victim;
    int damage, hp, maxHp, state=unknownState;
    CoordStruct impact{};
    bool hasImpact=false;
    std::optional<CoordStruct> blastCenter;
    int ammo=-1, passengers=-1, owner=-1;
    int attackerOwner=-1, victimOwner=-1;
    CoordStruct origin{};
    CellStruct castAim{}; // Transient: active commands run synchronously from the game event queue.
    vector<string> snapshot; // skill IDs for a dying or replaced deployer
    double distance=-1; // Horizontal distance in cells, -1 when no target was available.
    std::map<string,int> stackSnapshot;
    int heldStacks(const string& id) const {
        auto it=stackSnapshot.find(id);
        return it==stackSnapshot.end()?1:std::max(1,it->second);
    }
    int targetHp=0,targetMaxHp=0;
    std::optional<int> selfCost;
    std::optional<int> targetCost; // Absent for ground targets and older event snapshots.
    // The action chain and the carrier's queued event draw from the same remaining stacks.
    DWORD consumedTarget=0;
    string consumedKind;
    int consumedOwner=-1;
    CoordStruct consumedPosition{};
    std::shared_ptr<PassengerPassives> passengerPassives;
    DWORD sequenceCast=0; // Only live script callbacks use this, never queued passive events.
    string deathType;
    DWORD deathTeam=0;
    string signal,senderKind,senderRelation;
    int senderHp=0,senderMaxHp=0;
    std::optional<int> senderCost;
    CoordStruct signalSenderPosition{};
    double signalValue=0;
    unsigned signalDepth=0;
    std::shared_ptr<SignalChain> signalChain;
    std::optional<LandingOrder> landing;
};
vector<Event> pending;
vector<std::pair<DWORD,CoordStruct>> impactCenters;
void leaveImpactCenter(DWORD frame) {
    if(!impactCenters.empty() && impactCenters.back().first==frame) impactCenters.pop_back();
}
bool applyingDamage=false;
const int* ownerDamageOverride=nullptr;
std::map<DWORD,std::set<string>> removedPassives;
struct GrantTime {
    unsigned start;
    int duration;
    int remaining(unsigned frame) const {
        if(duration<0) return -1;
        unsigned elapsed=frame-start;
        return elapsed>=static_cast<unsigned>(duration)?0:duration-static_cast<int>(elapsed);
    }
};
struct UnitGrants {
    TechnoClass* unit=nullptr; // Rebound by object ID after loading a game.
    std::map<string,GrantTime> skills;
};
std::map<DWORD,UnitGrants> warheadGrants;
bool rebindGrants=false;
// Native damage and extensions use the registered warhead's identity. Keep the
// radius scoped to this synchronous impact, including re-entrant detonations.
struct SpreadScope {
    DWORD frame;
    WarheadTypeClass* wh;
    float original,effective;
};
vector<SpreadScope> spreadScopes;
struct ExplosionBatch { int first,scale; };
std::map<DWORD,ExplosionBatch> explosionBatches;

// Capture only the impact animation creation stage, after area damage and before
// debris. Phobos can append several AnimList or SplashList objects to the array.
void finishExplosions(DWORD frame) {
    auto batch=explosionBatches.find(frame);
    if(batch==explosionBatches.end()) return;
    for(int i=batch->second.first;i<AnimClass::Array.Count;++i) {
        if(auto anim=AnimClass::Array.Items[i])
            VisualScale::explosions.try_emplace(anim->UniqueID,batch->second.scale);
    }
    explosionBatches.erase(batch);
}
void enterSpread(DWORD frame,WarheadTypeClass* wh,int percent) {
    if(spreadScopes.empty() && (percent==100 || !wh || !(wh->CellSpread>0))) return;
    if(!spreadScopes.empty()) {
        const auto& outer=spreadScopes.back();
        if(outer.wh) outer.wh->CellSpread=outer.original;
    }
    float original=wh?wh->CellSpread:0;
    float effective=original;
    // The native cell-offset table ends at ten cells. Preserve larger mod-defined radii.
    if(percent!=100 && original>0)
        effective=static_cast<float>(std::min(original*(percent/100.0),std::max(10.0,double(original))));
    spreadScopes.push_back({frame,wh,original,effective});
    if(wh) wh->CellSpread=effective;
}
void leaveSpread(DWORD frame) {
    if(spreadScopes.empty() || spreadScopes.back().frame!=frame) return;
    auto top=spreadScopes.back();
    if(top.wh) top.wh->CellSpread=top.original;
    spreadScopes.pop_back();
    if(!spreadScopes.empty()) {
        const auto& outer=spreadScopes.back();
        if(outer.wh) outer.wh->CellSpread=outer.effective;
    }
}
// A bullet, its native area pass and the per-victim damage callbacks can all
// report the same hit. Nested detonations get their own recipient set.
struct ReapplyImpact {
    DWORD frame;
    WarheadTypeClass* wh;
    std::set<DWORD> recipients;
};
vector<ReapplyImpact> reapplyImpacts;
void beginReapplyImpact(DWORD frame,WarheadTypeClass* wh) {
    auto rule=grantRules.find(wh);
    if(!reapplyImpacts.empty() || (rule!=grantRules.end() && rule->second.stackOnReapply))
        reapplyImpacts.push_back({frame,wh,{}});
}
void endReapplyImpact(DWORD frame) {
    if(!reapplyImpacts.empty() && reapplyImpacts.back().frame==frame) reapplyImpacts.pop_back();
}
struct GrantImpact { WarheadTypeClass* wh; CoordStruct at; HouseClass* source; };
std::map<DWORD,GrantImpact> bulletGrantImpacts;
// Ownerless scripted projectiles still need a house for passive-grant filters.
std::map<DWORD,int> passiveBulletOwners;
std::set<DWORD> passiveBullets;
std::set<DWORD> passiveDetonations;
bool directSuppressed(DWORD id,const string& skill) {
    auto it=removedPassives.find(id);
    if(it==removedPassives.end() || !it->second.contains(skill)) return false;
    auto grants=warheadGrants.find(id);
    if(grants==warheadGrants.end()) return true;
    auto entry=grants->second.skills.find(skill);
    return entry==grants->second.skills.end() || !entry->second.remaining(Unsorted::CurrentFrame);
}
bool linkedPassive(DWORD unit,const string& id) {
    auto it=linkedGrants.find(unit);
    return it!=linkedGrants.end() && it->second.contains(id);
}
bool suppressed(DWORD unit,const string& id) {
    return directSuppressed(unit,id) && !linkedPassive(unit,id);
}
bool groundVehicle(TechnoClass* unit) {
    if(!unit || unit->WhatAmI()!=AbstractType::Unit) return false;
    const auto type=unit->GetTechnoType();
    // Type-based: a ground vehicle retains eligibility while temporarily airborne.
    return !type->Naval && !type->JumpJet && !type->BalloonHover && !type->ConsideredAircraft &&
        type->SpeedType!=SpeedType::Winged && type->MovementZone!=MovementZone::Fly;
}
const char* kind(TechnoClass* t) {
    switch(t->WhatAmI()) {
        case AbstractType::Infantry: return "infantry";
        case AbstractType::Unit: return "vehicle";
        case AbstractType::Aircraft: return "aircraft";
        case AbstractType::Building: return "building";
        default: return "other";
    }
}
#include "runtime/UnitMount.inc"

const vector<string>& boundSkills(const char* typeID) {
    if (!ready) readSkills();
    auto [it, fresh] = bindings.try_emplace(typeID);
    if(fresh) {
        it->second=split(read(typeID,"Starfall.Passives"));
        const auto type=lower(typeID);
        if(auto map=mapPassiveBindings.find(type);map!=mapPassiveBindings.end())
            for(const auto& id:split(map->second)) if(!includes(it->second,id)) it->second.push_back(id);
        if(auto found=scriptBindings.find(type);found!=scriptBindings.end())
            for(const auto& id:found->second) if(skills.contains(id) && !includes(it->second,id)) it->second.push_back(id);
        for(const auto& id:it->second) if(!skills.contains(id))
            configLog("["+string(typeID)+"] Starfall.Passives="+id+" - 引用的被动未注册或已停用");
    }
    return it->second;
}
std::map<string,vector<string>> mountedBindings;
const vector<string>& unitSkills(TechnoClass* unit) {
    const auto type=unit->GetTechnoType()->ID;
    if(!UnitMount::mounted(unit)) return boundSkills(type);
    auto [it,added]=mountedBindings.try_emplace(type);
    if(added) it->second=split(read(type,"Starfall.Passives"));
    return it->second;
}
// Research layers follow the current owner and type, including units built after purchase.
const std::map<string,int>& researchLayers(TechnoClass* unit) {
    static const std::map<string,int> empty;
    if(!unit || !unit->Owner || Research::unlocked.empty() || UnitMount::mounted(unit)) return empty;
    static std::map<std::pair<int,TechnoTypeClass*>,std::map<string,int>> cache;
    static unsigned revision=0,generation=0;
    if(revision!=Research::bondRevision || generation!=Research::uiGeneration) {
        cache.clear();revision=Research::bondRevision;generation=Research::uiGeneration;
    }
    int house=unit->Owner->ArrayIndex;
    auto type=unit->GetTechnoType();
    auto [it,added]=cache.try_emplace({house,type});
    auto& layers=it->second;
    if(!added) return layers;
    auto typeId=lower(type->ID);
    for(const auto& project:Research::options) {
        int count=Research::acquisitionCount(house,project.id);
        if(!count || (!project.repeatable && count==1)) continue;
        std::set<string> matched;
        Research::forBaseEffects(project,[&](const auto& e){
            if(e.effect==Research::Effect::Passive && Research::matchesTarget(e.types,typeId,kind(unit),groundVehicle(unit)))
                matched.insert(e.passive);
        });
        for(const auto& id:matched) layers[id]+=std::min(count,INT_MAX-layers[id]);
    }
    return layers;
}
int researchLayers(TechnoClass* unit,const string& id) {
    const auto& layers=researchLayers(unit);
    auto it=layers.find(id);
    return it==layers.end() || directSuppressed(unit->UniqueID,id)?0:it->second;
}
int heldUnitStacks(TechnoClass* unit,const string& id) {
    return stackCount(unit->UniqueID,id,researchLayers(unit,id));
}
bool ownsPassive(TechnoClass* unit,const string& id);
int unitStacks(TechnoClass* unit,const string& id) {
    return unit && ownsPassive(unit,id)?heldUnitStacks(unit,id):0;
}
bool changeUnitStacks(TechnoClass* unit,const string& id,int amount,int maximum) {
    return changeStacks(unit->UniqueID,id,amount,maximum,researchLayers(unit,id));
}
bool researchSkills(TechnoClass* unit,vector<string>* active=nullptr) {
    if(!unit->Owner || UnitMount::mounted(unit)) return false;
    auto unlocked=Research::unlocked.find(unit->Owner->ArrayIndex);
    if(unlocked==Research::unlocked.end()) return false;
    const auto type=lower(unit->GetTechnoType()->ID);
    bool found=false;
    for(const auto& o:Research::options) {
        if(!unlocked->second.contains(o.id)) continue;
        auto add=[&](Research::Effect effect,const vector<string>& types,const string& passive) {
            if(effect!=Research::Effect::Passive || !Research::matchesTarget(types,type,kind(unit),groundVehicle(unit))) return;
            found=true;if(active && !includes(*active,passive)) active->push_back(passive);
        };
        Research::forActiveEffects(unit->Owner->ArrayIndex,o,[&](const auto& e){add(e.effect,e.types,e.passive);});
        if(found && !active) break;
    }
    Research::forBondEffects(unit->Owner->ArrayIndex,[&](const auto& e){
        if(e.effect==Research::Effect::Passive && Research::matchesTarget(e.types,type,kind(unit),groundVehicle(unit))) {
            found=true;if(active && !includes(*active,e.passive)) active->push_back(e.passive);
        }
    });
    return found;
}
bool directExtraSkills(TechnoClass* unit,vector<string>* active) {
    bool found=researchSkills(unit,active);
    if(auto grants=transportGrants.find(unit->UniqueID);grants!=transportGrants.end()) {
        found=true;
        if(active) for(const auto& [id,grant]:grants->second) if(!includes(*active,id)) active->push_back(id);
    }
    if(auto aura=auraGrants.find(unit->UniqueID);aura!=auraGrants.end()) {
        found=true;
        if(active) for(const auto& grant:aura->second) if(!includes(*active,grant.first)) active->push_back(grant.first);
    }
    if(!spawnGrants.empty() && unit->SpawnOwner) {
        auto grant=spawnGrants.find(unit->UniqueID);
        auto parent=unit->SpawnOwner;
        if(grant!=spawnGrants.end() && grant->second.parent==parent->UniqueID &&
           alive(parent) && parent->IsOnMap && !parent->InLimbo && parent->Owner==unit->Owner) {
            found=true;
            if(active) for(const auto& id:grant->second.ids) if(!includes(*active,id)) active->push_back(id);
        }
    }
    auto it=warheadGrants.find(unit->UniqueID);
    if(it!=warheadGrants.end()) for(const auto& [id,time]:it->second.skills) {
        if(!time.remaining(Unsorted::CurrentFrame)) continue;
        found=true;
        if(!active) break;
        if(!includes(*active,id)) active->push_back(id);
    }
    return found;
}
bool extraSkills(TechnoClass* unit,vector<string>* active=nullptr) {
    bool direct=directExtraSkills(unit,active);
    if(UnitMount::mounted(unit)) return direct;
    auto links=linkedGrants.find(unit->UniqueID);
    if(links==linkedGrants.end()) return direct;
    if(active) for(const auto& id:links->second) if(!includes(*active,id)) active->push_back(id);
    return true;
}
bool ownsDirectPassive(TechnoClass* unit,const string& id) {
    if(!skills.contains(id) || directSuppressed(unit->UniqueID,id)) return false;
    auto ids=unitSkills(unit);
    directExtraSkills(unit,&ids);
    return includes(ids,id);
}
void reconcileUnitStacks(TechnoClass* unit) {
    if(!unit) return;
    auto group=passiveStacks.find(unit->UniqueID);
    if(group==passiveStacks.end()) return;
    auto ids=unitSkills(unit);extraSkills(unit,&ids);
    for(auto it=group->second.begin();it!=group->second.end();) {
        auto skill=skills.find(it->first);
        if(skill==skills.end() || skill->second.stackMax==1 || suppressed(unit->UniqueID,it->first) || !includes(ids,it->first)) it=group->second.erase(it);
        else {it->second=std::min(it->second,skill->second.stackMax-1);++it;}
    }
    if(group->second.empty()) passiveStacks.erase(group);
}
void reconcileAllStacks() {
    if(passiveStacks.empty()) return;
    for(auto unit:TechnoClass::Array) if(unit && passiveStacks.contains(unit->UniqueID)) reconcileUnitStacks(unit);
}

#include "runtime/PassiveStates.inc"
#include "runtime/Taunt.inc"
#include "../modules/ActSp/UnitOrders.inc"
#include "runtime/PassiveTooltips.inc"

TechnoClass* Mount::acquire(FootClass* child) {
    int range=0;
    for(int i=0;i<2;++i) {
        auto slot=child->GetWeapon(i);
        if(slot && slot->WeaponType) range=std::max(range,child->GetWeaponRange(i));
    }
    if(range<=0) return nullptr;
    auto origin=child->Transporter?child->Transporter:child;
    TechnoClass* best=nullptr;
    int distance=INT_MAX;
    auto visit=[&](TechnoClass* unit) {
        if(!alive(unit) || !unit->Owner || unit->Owner==child->Owner || child->Owner->IsAlliedWith(unit->Owner) ||
           unit->Owner->IsNeutral() || !unit->GetTechnoType()->LegalTarget || !usable(child,unit)) return false;
        auto d=origin->DistanceFrom(unit);
        if(!best || d<distance || (d==distance && unit->UniqueID<best->UniqueID)) {best=unit;distance=d;}
        return false;
    };
    if(range<=32*256) visitNearbyCells(origin->GetMapCoords(),(range+255)/256+2,true,visit);
    else for(auto unit:TechnoClass::Array) visit(unit);
    return best;
}

struct Transition {int rank; bool deployed;};
std::map<DWORD,Transition> previousState;
std::set<DWORD> reportedDeaths;
std::map<DWORD,Event> shots;
double distanceCells(const CoordStruct& a,const CoordStruct& b) {
    return std::hypot(static_cast<double>(a.X)-b.X,static_cast<double>(a.Y)-b.Y)/256.0;
}
Event eventSnapshot(const char* name,TechnoClass* holder,TechnoClass* other,TechnoClass* attacker,TechnoClass* victim,int damage) {
    if(alive(holder)) reconcileUnitStacks(holder);
    const char* rel="none";
    if(other && other->Owner && holder->Owner)
        rel=other->Owner==holder->Owner?"self":holder->Owner->IsAlliedWith(other->Owner)?"allies":other->Owner->IsNeutral()?"neutral":"enemies";
    auto type=holder->GetTechnoType();
    auto hp=UnitHealth::read(holder,type->Strength);
    Event e{name,other?kind(other):"none",rel,holder->UniqueID,attacker?attacker->UniqueID:0,
            victim?victim->UniqueID:0,damage,hp.current,hp.maximum,stateOf(holder)};
    if(e.trigger=="ondeath") {
        e.deathType=type->ID;
        if(holder->WhatAmI()!=AbstractType::Building) {
            auto team=static_cast<FootClass*>(holder)->Team;
            if(team && team->Owner==holder->Owner) {
                if(team->UniqueID==static_cast<DWORD>(-1)) team->Create_ID();
                e.deathTeam=team->Fetch_ID();
            }
        }
    }
    e.selfCost=type->Cost;
    e.senderKind=kind(holder);
    e.origin=holder->GetCenterCoords();
    if(auto stacks=passiveStacks.find(holder->UniqueID);stacks!=passiveStacks.end())
        for(const auto& [id,extra]:stacks->second) e.stackSnapshot[id]=heldUnitStacks(holder,id);
    if(auto grants=transportGrants.find(holder->UniqueID);grants!=transportGrants.end())
        for(const auto& [id,grant]:grants->second) e.stackSnapshot[id]=heldUnitStacks(holder,id);
    for(const auto& [id,layers]:researchLayers(holder))
        if(!directSuppressed(holder->UniqueID,id)) e.stackSnapshot[id]=stackCount(holder->UniqueID,id,layers);
    if(other) {
        e.distance=distanceCells(e.origin,other->GetCenterCoords());
        auto targetType=other->GetTechnoType();
        e.targetCost=targetType->Cost;
        auto target=UnitHealth::read(other,targetType->Strength);
        if(target.maximum>0) {e.targetMaxHp=target.maximum;e.targetHp=std::clamp(target.current,0,target.maximum);}
    }
    if(!impactCenters.empty()) e.blastCenter=impactCenters.back().second;
    if(victim) {e.hasImpact=true;e.impact=victim->GetCoords();}
    e.ammo=type->Ammo<0?-1:holder->Ammo;
    e.passengers=holder->Passengers.NumPassengers;
    e.owner=holder->Owner?holder->Owner->ArrayIndex:-1;
    if(attacker && attacker->Owner) e.attackerOwner=attacker->Owner->ArrayIndex;
    if(victim && victim->Owner) e.victimOwner=victim->Owner->ArrayIndex;
    return e;
}
bool meetsEventFormula(const Skill& skill,const Event& event) {
    if(skill.conditionFormula.expression.empty()) return true;
    auto distance=event.distance>=0?std::optional<double>{event.distance}:std::nullopt;

    HealthFormula::Values target{event.targetHp,event.targetMaxHp,event.targetCost};
    std::optional<HealthFormula::Values> sender;
    if(event.trigger=="onsignal") sender=HealthFormula::Values{event.senderHp,event.senderMaxHp,event.senderCost};
    bool missing=false;
    auto value=skill.conditionFormula.expression.evaluate(event.hp,event.maxHp,event.damage,distance,
        event.heldStacks(skill.id),target,&missing,event.selfCost,event.ammo,
        event.passengers>=0?std::optional<int>{event.passengers}:std::nullopt,sender,
        event.trigger=="onsignal"?std::optional<double>{event.signalValue}:std::nullopt,std::nullopt,event.owner,Research::counter);
    if(!value) {if(!missing) skill.conditionFormula.report();return false;}
    return *value!=0;
}
bool interested(TechnoClass* unit,const char* trigger,vector<string>* snapshot=nullptr) {
    if(!unit) return false;
    auto ids=unitSkills(unit);
    extraSkills(unit,&ids);
    bool any=false;
    for(const auto& id:ids) {
        auto it=skills.find(id);
        if(it==skills.end() || it->second.trigger!=trigger || suppressed(unit->UniqueID,id)) continue;
        any=true;
        if(!snapshot) break;
        snapshot->push_back(id);
    }
    return any;
}
void forgetGain(DWORD unit,const string& id) {
    auto it=gainSeen.find(unit);
    if(it==gainSeen.end()) return;
    std::erase(it->second,id);
    if(it->second.empty()) gainSeen.erase(it);
}
void observeGains(TechnoClass* unit) {
    if(!anyGainSkills || !alive(unit)) return;
    vector<string> current;
    auto held=unitSkills(unit);
    extraSkills(unit,&held);
    for(const auto& id:held) {
        auto it=skills.find(id);
        if(it!=skills.end() && !suppressed(unit->UniqueID,id) &&
           (it->second.trigger=="onpassivegained" || it->second.gainAnim)) current.push_back(id);
    }
    auto old=gainSeen.find(unit->UniqueID);
    if(current.empty()) {if(old!=gainSeen.end()) gainSeen.erase(old);return;}
    if(!unit->IsOnMap || unit->InLimbo) {
        // Remember continuing ownership while transported. Defer first acquisition until placement.
        if(old!=gainSeen.end()) {
            std::erase_if(old->second,[&](const string& id){return !includes(current,id);});
            if(old->second.empty()) gainSeen.erase(old);
        }
        return;
    }
    vector<string> added;
    for(const auto& id:current) if(old==gainSeen.end() || !includes(old->second,id)) added.push_back(id);
    if(!added.empty()) {
        vector<string> events;
        for(const auto& id:added) {
            const auto& skill=skills.at(id);
            if(skill.gainAnim) playSkillAnim(skill.gainAnim,unit,unit->Location,unit->Owner);
            if(skill.trigger=="onpassivegained") events.push_back(id);
        }
        if(!events.empty()) {
            auto e=eventSnapshot("onpassivegained",unit,nullptr,unit,unit,0);
            e.snapshot=std::move(events);pending.push_back(std::move(e));
        }
    }
    if(old==gainSeen.end() || old->second!=current) gainSeen[unit->UniqueID]=std::move(current);
}
void tickGains() {
    if(!anyGainSkills) return;
    for(auto unit:TechnoClass::Array) {
        if(!alive(unit)) continue;
        const int house=unit->Owner?unit->Owner->ArrayIndex:-1;
        auto owned=Research::unlocked.find(house);
        auto retired=Research::retired.find(house);
        const size_t replaced=retired==Research::retired.end()?0:retired->second.size();
        GainInputs now{unit->GetTechnoType(),unit->Owner,replaced+(owned==Research::unlocked.end()?0:owned->second.size()),
            unit->IsOnMap && !unit->InLimbo};
        auto [it,fresh]=gainInputs.try_emplace(unit->UniqueID,now);
        if(!fresh && it->second==now) continue;
        it->second=now;
        observeGains(unit);
    }
}
void capture(const char* event,TechnoClass* holder,TechnoClass* other,TechnoClass* attacker,TechnoClass* victim,int damage) {
    if(applyingDamage || !passiveDetonations.empty() || !alive(holder)) return;
    vector<string> ids;
    if(!interested(holder,event,&ids)) return;
    auto e=eventSnapshot(event,holder,other,attacker,victim,damage);
    e.snapshot=std::move(ids);
    pending.push_back(std::move(e));
}
// Keep a separate clock for every instance/skill. The first tick is due one
// complete interval after acquisition. Leaving the map or losing the skill resets it.
struct PeriodicClock { unsigned start; bool seen; };
std::map<TimerKey,PeriodicClock> periodic;
bool expiryScheduled=false;
unsigned nextExpiry=0;
#include "runtime/PassiveGrants.inc"

void tickIntervals() {
    if(!anyIntervals && periodic.empty()) return;
    const unsigned frame=Unsorted::CurrentFrame;
    for(auto& [key,timer]:periodic) timer.seen=false;
    vector<string> ids;
    for(auto* unit:TechnoClass::Array) {
        if(!alive(unit) || !unit->IsOnMap || unit->InLimbo) continue;
        ids.clear();
        if(!interested(unit,"oninterval",&ids)) continue;
        for(const auto& id:ids) {
            auto& timer=periodic.try_emplace({unit->UniqueID,id},PeriodicClock{frame,true}).first->second;
            timer.seen=true;
            if(frame-timer.start<static_cast<unsigned>(skills.at(id).interval)) continue;
            timer.start=frame;
            auto aim=unit->Target;
            auto other=aim && (aim->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None
                ? static_cast<TechnoClass*>(aim) : nullptr;
            auto e=eventSnapshot("oninterval",unit,other,unit,other,0);
            if(aim) {e.impact=aim->GetCoords(); e.hasImpact=true;if(!other) e.distance=distanceCells(e.origin,e.impact);}
            e.snapshot.push_back(id);
            pending.push_back(std::move(e));
        }
    }
    std::erase_if(periodic,[](const auto& item){return !item.second.seen;});
}
void observeTransitions() {
    shots.clear(); // failed firing attempts have no event
    if(!observePromotions && !observeDeployments) return;
    for(auto* u:TechnoClass::Array) {
        if(!alive(u)) continue;
        bool promo=observePromotions && (interested(u,"onpromote") || interested(u,"onpromoteveteran") || interested(u,"onpromoteelite")),deploy=observeDeployments && interested(u,"ondeploy");
        if(!promo && !deploy) { previousState.erase(u->UniqueID); continue; }
        bool deployed=deploy && deploymentOf(u)==DeployPosture::Deployed;
        int rank=u->Veterancy.IsElite()?2:u->Veterancy.IsVeteran()?1:0;
        Transition now{rank,deployed};
        auto [it,fresh]=previousState.try_emplace(u->UniqueID,now);
        if(!fresh) {
            if(promo && rank>it->second.rank) {
                capture("onpromote",u,nullptr,u,u,0);
                capture(rank==2?"onpromoteelite":"onpromoteveteran",u,nullptr,u,u,0);
            }
            if(deployed && !it->second.deployed) capture("ondeploy",u,nullptr,u,u,0);
            it->second=now;
        }
    }
}
#include "runtime/RewardText.inc"
#include "runtime/PassiveActions.inc"
#include "LuaAnim.inc"
#include "LuaSkillActions.inc"
#include "OrdnanceEffects.inc"
#include "WarheadScripts.inc"

bool playEventAnim(const Skill& s,const Event& e,TechnoClass* holder,const Objects& objects) {
    auto owner=holder?holder->Owner:e.owner>=0 && e.owner<HouseClass::Array.Count?HouseClass::Array.Items[e.owner]:nullptr;
    if(s.animTarget=="self" || e.trigger=="ondeploy" || e.trigger=="onpromote" || e.trigger=="onpromoteveteran" || e.trigger=="onpromoteelite" || e.trigger=="onpassivegained")
        return playSkillAnim(s.triggerAnim,holder,e.origin,owner)!=nullptr;
    bool attacker=e.trigger=="ontakedamage" || e.trigger=="ondeath";
    if(auto unit=findLive(objects,attacker?e.attacker:e.victim))
        return playSkillAnim(s.triggerAnim,unit,unit->Location,owner)!=nullptr;
    return !attacker && e.hasImpact && playSkillAnim(s.triggerAnim,nullptr,e.impact,owner);
}
#include "runtime/LandingTransform.inc"
bool applyResults(const Skill& s,Event& e,TechnoClass*& holder,const Objects& objects,int firstDraw=0) {
    bool success=false;
    Event actionEvent=e;
    int draws=s.randomActionCount?s.randomActionCount:static_cast<int>(s.actions.size());
    for(int draw=firstDraw;draw<draws;++draw) {
        size_t index=static_cast<size_t>(draw);
        if(s.randomActionCount) {
            int pick=ScenarioClass::Instance->Random.RandomRanged(1,s.randomActionTotal);index=0;
            while(index<s.randomActionWeights.size() && (pick-=s.randomActionWeights[index])>0) ++index;
            if(index>=s.actions.size()) continue;
        }
        const auto& action=s.actions[index];
        // Keep the last live position if this action destroys the holder.
        if(holder && s.actions.size()>1) actionEvent.origin=holder->GetCenterCoords();

        if(action.landFirst) {
            auto unit=findLive(objects,resultTarget(action,actionEvent));
            if(unit && unit->WhatAmI()==AbstractType::Unit && unit->GetTechnoType()->Locomotor==LocomotionClass::CLSIDs::Jumpjet &&
                action.unitType->Locomotor==LocomotionClass::CLSIDs::Drive) {
                if(landingSites.contains(unit->UniqueID)) break;
                auto converter=publicPhobosConverter();
                if(canTransformUnit(unit,action.unitType,converter,true)) {
                    if(!landingSiteFree(static_cast<UnitClass*>(unit),action.unitType,unit->GetMapCoords())) break;
                    if(!transformUnit(unit,action.unitType,converter,true)) break;
                    success=true;
                    continue;
                }
                return queueLanding(static_cast<UnitClass*>(unit),action.unitType,s,actionEvent,draw+1) || success;
            }
        }
        bool changed=apply(action,actionEvent,holder,objects,e.heldStacks(s.id));
        if(changed) success=true;
        if(e.trigger!="ondeath") holder=findLive(objects,e.holder);
        if(!changed && (action.landFirst || (!s.randomActionCount && stopsSequenceOnFailure(action.action)))) break;
    }
    return success;
}
void dispatch() {
    if(pending.empty()) return;
    Objects objects;
    for(auto* t : TechnoClass::Array) if(alive(t)) objects.emplace(t->UniqueID,t);
    activeObjects=&objects;
    auto events=std::move(pending);
    pending.clear();
    landingSites.clear();
    for(const auto& e:events) if(e.landing) landingSites.emplace(e.landing->unit,e.landing->cell);
    for(auto& e:events) {
        auto holder=findLive(objects,e.holder);
        if(e.landing) {
            auto order=*e.landing;
            auto status=advanceLanding(order,objects);
            if(status==LandingStatus::Waiting) {pending.push_back(std::move(e));continue;}
            landingSites.erase(order.unit);
            if(status==LandingStatus::Failed) continue;
            e.landing.reset();
            if(e.trigger=="ondeath") holder=nullptr;
            const Skill* skill=nullptr;
            if(e.trigger=="onpickup" && e.snapshot.size()==1) {
                if(auto it=pickupTypes.find(e.snapshot.front());it!=pickupTypes.end()) skill=&it->second.reward;
            } else if(e.trigger=="onweaponfire") {
                if(auto it=weaponScripts.find(order.skill);it!=weaponScripts.end()) skill=&it->second;
            } else if(auto it=skills.find(order.skill);it!=skills.end()) skill=&it->second;
            if(skill && skill->id==order.skill) applyResults(*skill,e,holder,objects,order.nextDraw);
            continue;
        }
        bool death=e.trigger=="ondeath";
        if(death) holder=nullptr;
        // A parasite can kill its victim and be removed while exiting it, before
        // queued events run. Preserve already-earned kill location/owner effects.
        const bool detached=death || (!holder && e.trigger=="onkill");
        if(!holder && !detached) continue;
        if(e.trigger=="onweaponfire") {
            if(e.snapshot.size()==1) {
                auto script=weaponScripts.find(e.snapshot.front());
                if(script!=weaponScripts.end()) applyResults(script->second,e,holder,objects);
            }
            continue;
        }
        if(e.trigger=="onpickup") {
            if(e.snapshot.size()==1) {
                auto type=pickupTypes.find(e.snapshot.front());
                if(type!=pickupTypes.end()) applyResults(type->second.reward,e,holder,objects);
            }
            continue;
        }
        auto active=e.snapshot;
        if(active.empty()) {
            if(!holder) continue;
            active=unitSkills(holder);extraSkills(holder,&active);
        }
        for(const auto& id:active) {
            auto it=skills.find(id);
            if(it==skills.end()) continue;
            const Skill& s=it->second;
            if(e.trigger=="onsignal") {
                vector<string> held;interested(holder,"onsignal",&held);
                if(!includes(held,id) || s.signal!=e.signal ||
                   (!includes(s.senderHouses,"all") && !includes(s.senderHouses,e.senderRelation)) ||
                   (!includes(s.senderKinds,"all") && !includes(s.senderKinds,e.senderKind))) continue;
            }
            if(suppressed(e.holder,id) || s.trigger!=e.trigger || e.damage<s.minDamage || !meetsState(s,e.state,e.hp,e.maxHp,e.ammo,e.passengers) || !meetsEventFormula(s,e) ||
               (!includes(s.houses,"all") && !includes(s.houses,e.relation)) ||
               (!includes(s.kinds,"all") && !includes(s.kinds,e.kind))) continue;
            if(s.maxTriggers>0) {
                auto unit=triggerTotals.find(e.holder);
                if(unit!=triggerTotals.end()) {
                    auto total=unit->second.find(id);
                    if(total!=unit->second.end() && total->second>=static_cast<unsigned>(s.maxTriggers)) continue;
                }
            }
            auto& p=counts[e.holder][id];
            unsigned frame=Unsorted::CurrentFrame;
            if(frame<p.next || ++p.count<s.count) continue;
            p.count=0;
            if(s.chance==0 || (s.chance<100 && ScenarioClass::Instance->Random.RandomRanged(1,100)>s.chance)) continue;
            bool played=s.triggerAnim && playEventAnim(s,e,holder,objects);
            bool success=s.actions.empty() && played;
            if(applyResults(s,e,holder,objects)) success=true;
            if(success && s.maxTriggers>0) ++triggerTotals[e.holder][id];
            if(detached) continue;
            if(!holder) break;
            if(success && !suppressed(e.holder,id)) counts[e.holder][id].next=frame+s.cooldown;
        }
        if(detached) counts.erase(e.holder);
    }
    activeObjects=nullptr;
}
// Damage interception must finish synchronously, before the engine subtracts HP.
int adjustPendingDamage(const char* trigger,TechnoClass* holder,TechnoClass* other,
                        TechnoClass* attacker,TechnoClass* victim,int damage) {
    vector<string> ids;
    if(!interested(holder,trigger,&ids)) return damage;
    auto e=eventSnapshot(trigger,holder,other,attacker,victim,damage);
    for(const auto& id:ids) {
        auto it=skills.find(id);
        if(it==skills.end() || suppressed(e.holder,id)) continue;
        const auto& skill=it->second;
        e.damage=damage;
        if(damage<skill.minDamage || !meetsState(skill,e.state,e.hp,e.maxHp,e.ammo,e.passengers) || !meetsEventFormula(skill,e) ||
           (!includes(skill.houses,"all") && !includes(skill.houses,e.relation)) ||
           (!includes(skill.kinds,"all") && !includes(skill.kinds,e.kind))) continue;
        if(skill.maxTriggers>0) {
            auto unit=triggerTotals.find(e.holder);
            if(unit!=triggerTotals.end()) {
                auto total=unit->second.find(id);
                if(total!=unit->second.end() && total->second>=static_cast<unsigned>(skill.maxTriggers)) continue;
            }
        }
        auto& progress=counts[e.holder][id];
        unsigned frame=Unsorted::CurrentFrame;
        if(frame<progress.next || ++progress.count<skill.count) continue;
        progress.count=0;
        if(skill.chance==0 || (skill.chance<100 && ScenarioClass::Instance->Random.RandomRanged(1,100)>skill.chance)) continue;
        bool success=false;

        int stacks=e.heldStacks(id);
        for(const auto& action:skill.actions) {
            if(action.action=="runscript") {
                Objects live;
                for(auto unit:TechnoClass::Array) if(alive(unit)) live.emplace(unit->UniqueID,unit);
                e.damage=damage;
                success|=runScript(action,e,live,stacks,&damage);
                if(!damage) break;
                continue;
            }
            auto adjusted=pendingDamageAmount(action,e,damage,stacks);
            if(!adjusted) continue;
            damage=*adjusted;success=true;
            if(!damage) break;
        }
        if(!success) continue;
        if(skill.maxTriggers>0) ++triggerTotals[e.holder][id];
        progress.next=frame+skill.cooldown;
        auto animTarget=skill.animTarget=="target"?other:holder;
        if(skill.triggerAnim && animTarget) playSkillAnim(skill.triggerAnim,animTarget,animTarget->GetCenterCoords(),holder->Owner);
        if(!damage) break;
    }
    return damage;
}

bool recheckLoadedStages=false;
void reset() {
    UnitOrders::orders.clear();UnitOrders::definitions.clear();CommandBarOrders::entries.clear();
    recheckLoadedStages=false;
    bombSnapshots.clear();
    bombKeys.clear();
    rebindBombKeys=false;
    bombImpacts.clear();
    warheadScriptImpacts.clear();
    runningWarheadScripts.clear();
    pickupTypes.clear();
    groundPickups.clear();
    SonicRipple::clear();
    ArtSp::Tracers::clearTrails();
    ArtSp::Tracers::clearRules();
    ArtSp::TipsLabel::clear();
    ArtSp::ContinuousBeam::reset();
    splitTargets.clear();
    Taunt::controls.clear();Taunt::rebind=false;
    RewardText::labels.clear();
    pickupImages.clear();
    linkedGrants.clear();
    anyLinkedPassives=false;
    passiveBulletOwners.clear();
    LuaAnim::entries.clear();
    hitSequences.clear();
    observedHits.clear();
    soundIndices.clear();
    lastMissingSound.clear();
    LuaNearby::reset();
    ActionSequence::reset();
    ForceMove::settling.clear();
    ForceMove::clearMotions();
    impactCenters.clear();
    anyMultiShot=false;
    UnitMount::links.clear();Mount::rebind=false;mountedBindings.clear();
    passiveTints.clear();
    activeTooltips.clear();
    bodyPalettes.clear();
    rulesSkillNames.clear();
    ++ActiveSkills::generation;
    ActiveSkills::specs.clear();
    mapPassiveBindings.clear();
    mapPassengersBySize.clear();
    scriptTimers={};
    ScriptRuntime::clearPool();
    scriptModules.clear();
    ProjectileScript::reset();
    scriptDefinitions.clear();
    scriptBindings.clear();
    transportGrants.clear();
    transportStateKeys.clear();
    anyTransportPassives=false;
    auraSources.clear();
    auraGrants.clear();
    auraLosses.clear();
    anyAuras=false;
    discoverAuras=true;
    buildingPower.clear();
    anyBeforeDealDamage=false;
    anyBeforeTakeDamage=false;
    incomingDamage.clear();
    passiveStacks.clear();
    triggerTotals.clear();
    spawnGrants.clear();
    anySpawnPassives=false;
    anyGainSkills=false;
    gainInputs.clear();
    gainSeen.clear();
    UnitHealth::pools.clear();
    fireRateBase.clear();
    fireRateState.clear();
    experienceState.clear();
    unscaledKillScores.clear();
    rangeBase.clear();
    rangeState.clear();
    projectileBase.clear();
    projectileState.clear();
    VisualScale::projectiles.clear();
    VisualScale::explosions.clear();
    VisualScale::attachedAnims.clear();
    explosionBatches.clear();
    spreadScopes.clear();
    grantPresets.clear();
    grantRules.clear();
    warheadHouses.clear();
    warheadGrants.clear();
    bulletGrantImpacts.clear();
    reapplyImpacts.clear();
    rebindGrants=false;
    expiryScheduled=false;
    cloakableUnits.clear();
    mirageUnits.clear();
    immuneUnits.clear();
    mindImmuneUnits.clear();
    empUnits.clear();
    VisualScale::base.clear();
    VisualScale::state.clear();
    VisualScale::clearSprites();
    attachedAnims.clear();
    activeAnimStates.clear();
    anyPassiveVisuals=false;
    needMotion=false;
    nearbySamples.clear();
    weaponRules.clear();
    ProjectileShield::active.clear();
    ProjectileShield::configured=false;
    volleyWeapons.clear();
    mountCopies.clear();
    buildingLookups.clear();
    anyIntervals=false;
    periodic.clear();
    shots.clear();
    previousState.clear();
    reportedDeaths.clear();
    removedPassives.clear();
    passiveBullets.clear();
    passiveDetonations.clear();
    stateTimers.clear();
    applied.clear();
    anyStateSkills=false;
    observePromotions=false;
    observeDeployments=false;
    needUnitPower=false;
    BuildingResearch::clearTypeCaches();
    Research::reset();
    counts.clear();
    pending.clear();
    bindings.clear();
    weaponFireScripts.clear();
    weaponScripts.clear();
    skills.clear();
    ready=false;
}
} // namespace

void ForceMove::playJumpEffect(const JumpEffect& effect,const CoordStruct& at,HouseClass* owner) {
    if(!effect.anim.empty())
        if(auto type=AnimTypeClass::Find(effect.anim.c_str())) playSkillAnim(type,nullptr,at,owner);
    if(!effect.sound.empty()) playActionSound(effect.sound,at);
}

// The stock unit name and Phobos's EnemyUIName converge here. A null ESI
// continues through the game's terrain tooltip path.
DEFINE_HOOK(0x4AE678,SF_UnitHoverText,8) {
    auto unit=R->ESI<TechnoClass*>();
    if(!unit) {
        R->EAX(R->ESP()+0x10); // replay the terrain branch's LEA
        return 0x4AE680;
    }
    R->EAX(unitHoverText(unit,R->EAX<const wchar_t*>()));
    return 0x4AE69D;
}

// Capture the list chosen by YR itself (solo/skirmish or multiplayer). The native
// parser advances the physical slot even for an unrecognized command and for x.
// A later INI without this section leaves the native layout intact.
DEFINE_HOOK(0x67467B,SF_BeginCommandBarList,5) {
    if(R->EAX()) CommandBarOrders::listedSlots.clear();
    return 0;
}
DEFINE_HOOK(0x6746DC,SF_CommandBarListItem,7) {
    const char* name=R->EAX<const char*>();
    const int native=reinterpret_cast<int(__fastcall*)(const char*)>(0x6CFCC0)(name);
    if(native==*reinterpret_cast<const int*>(0x8427CC) && name && _stricmp(name,"x")!=0)
        CommandBarOrders::listedSlots.try_emplace(ConfigText::lower(name),R->ESI<int>());
    R->EAX(native);
    return 0x6746E3;
}

bool CommandBarOrders::eligible(TechnoClass* unit,HouseClass* owner,unsigned enterKinds) {
    if(!owner || owner->Defeated || owner->IsObserver() || !ActiveSkills::readyCaster(unit,owner) ||
       UnitMount::mounted(unit) || unit->IsImmobilized || unit->IsCrashing) return false;
    if(enterKinds) return UnitOrders::enterSource(unit,enterKinds);
    if(unit->IsArmed()) return true;
    auto secondary=unit->GetWeapon(1);
    return secondary && secondary->WeaponType;
}
bool CommandBarOrders::execute(int sender,unsigned index,int object,CellStruct center) {
    if(!ready) readSkills();
    if(index>=entries.size() || sender<0 || sender>=HouseClass::Array.Count || !ScenarioClass::Instance) return false;
    TargetClass handle;
    handle.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    handle.m_ID=object;
    auto unit=handle.As_Techno();
    const auto& entry=entries[index];
    if(!eligible(unit,HouseClass::Array.Items[sender],entry.enterKinds)) return false;
    return UnitOrders::start(unit,entry.id,center,entry.radius);
}
#include "ActiveCast.inc"
#include "../modules/ActSp/ProjectileScript.inc"

const PassiveRules::Skill* PassiveRules::findSkill(const string& id) {
    const auto it=skills.find(id);
    return it==skills.end()?nullptr:&it->second;
}
bool PassiveRules::hasPickupType(const string& id) { return pickupTypes.contains(id); }
const vector<string>* PassiveRules::scriptFunctions(const string& module) {
    const auto it=scriptModules.find(module);
    return it==scriptModules.end()?nullptr:&it->second.functions;
}

bool Research::Detail::Runtime::hasPassive(const string& id) { return skills.contains(id); }
// Use the persisted extension stream shared with Lua random draws. Native RNG
// activity during scenario loading must not reroll a pending research reward.
int Research::Detail::randomIndex(int count) { return scriptTimers.range(0,count-1,Game::Seed); }
bool Research::Detail::Runtime::conversionSupportsTypes(const TechnoTypeClass* from,const TechnoTypeClass* to) {
    if(!IsEqualGUID(from->Locomotor,to->Locomotor))
        return groundFlightPair(from,to) && publicPhobosConverter();
    return publicPhobosConverter() || nativeConversionTypesSafe(from,to);
}
bool Research::Detail::Runtime::canTransformUnit(TechnoClass* unit,TechnoTypeClass* to) { return ::canTransformUnit(unit,to); }
bool Research::Detail::Runtime::transformUnit(TechnoClass* unit,TechnoTypeClass* to) { return ::transformUnit(unit,to); }

void Research::ensureReady() { if(!ready) readSkills(); }
// Scale only this building's contribution, after engine/extension power getters
// have included damage, upgrades and power-plant enhancers.
DEFINE_HOOK(0x508CF9,SF_BuildingPowerOutput,6) {
    auto building=R->EDI<BuildingClass*>();
    R->EAX(powerContribution(building,R->EAX<int>(),R->ESI<HouseClass*>()->PowerOutput));
    return 0;
}
DEFINE_HOOK(0x508D0E,SF_BuildingPowerDrain,6) {
    auto building=R->EDI<BuildingClass*>();
    R->EAX(powerContribution(building,R->EAX<int>(),R->ESI<HouseClass*>()->PowerDrain));
    return 0;
}
// The blackout check queries output a second time, outside the accumulation.
DEFINE_HOOK(0x508D29,SF_BuildingPowerOutageCheck,5) {
    if(buildingPower.empty()) return 0;
    auto building=R->EDI<BuildingClass*>();
    R->EAX(powerContribution(building,building->GetPowerOutput(),0));
    return 0x508D2E;
}
void ActionSequence::executeSteps(DWORD id,const Cast& cast,const vector<Skill>& steps) {
    auto event=sequenceEvent(id,cast);
    Objects live;
    for(auto u:TechnoClass::Array) if(alive(u)) live.emplace(u->UniqueID,u);
    auto previous=activeObjects;activeObjects=&live;
    for(const auto& step:steps) apply(step.actions.front(),event,nullptr,live,0);
    activeObjects=previous;
}
// Shared synchronized command entry for unit orders and point superweapons.
DEFINE_HOOK(0x4C6CB0,SF_ActionSequenceCommand,6) {
    auto event=R->ECX<EventClass*>();
    if(Taunt::blocksOrder(*event)) return 0x4C6CEF;
    if(!UnitOrders::orders.empty() && (event->Type==EventType::Deploy || event->Type==EventType::Scatter)) {
        auto unit=event->MegaMission.Whom.As_Techno();
        if(unit && unit->Owner && unit->Owner->ArrayIndex==event->HouseIndex) UnitOrders::cancel(unit);
    }
    if(event->Type==EventType::SpecialPlace &&
       ActionSequence::consumeCommand(event->HouseIndex,event->SpecialPlace.ID,event->SpecialPlace.Location)) return 0x4C6CEF;
    return 0;
}
// Custom type keys are in the scenario INI while it is being read. The rules INI
// does not retain these entries after the engine applies its own type overrides.
DEFINE_HOOK(0x68AD2F, SF_MapPassiveBindings, 5) {
    auto ini=R->EDI<CCINIClass*>();
    ActionSequence::mapFields.clear();ActionSequence::collect(ini,ActionSequence::mapFields);
    if(ready) ArtSp::TipsLabel::readRules();
    mapPassiveBindings.clear();mapPassengersBySize.clear();bindings.clear();
    ProjectileScript::mapSpeeds.clear();ProjectileScript::weaponSpeeds.clear();
    if(ini) for(auto section=ini->Sections.First();section && section->IsValid();section=section->Next()) {
        if(!section->Name || strlen(section->Name)>127) continue;
        char list[2048]{};
        ini->ReadString(section->Name,"Starfall.Passives","",list,sizeof(list));
        if(list[0]) mapPassiveBindings.emplace(lower(section->Name),list);
        int speed=ini->ReadInteger(section->Name,"Speed",INT_MIN);
        if(speed!=INT_MIN) ProjectileScript::mapSpeeds.emplace(lower(section->Name),speed);
        char capacityMode[32]{};
        ini->ReadString(section->Name,"Passengers.BySize","",capacityMode,sizeof(capacityMode));
        if(capacityMode[0]) mapPassengersBySize.emplace(lower(section->Name),ini->ReadBool(section->Name,"Passengers.BySize",true));
    }
    return 0;
}
DEFINE_HOOK(0x683549, SF_ScenarioStart, 9) {
    reset();
    installAresMindGuards();
    ActSp::reset();
    return 0;
}
// New units (including aircraft born off-grid), and units lifted from a cell
// between synchronous Lua calls, stay eligible even while no cell lists them.
DEFINE_HOOK(0x6F3259,SF_NearbyUnitCreated,7) {
    LuaNearby::moved(R->ESI<TechnoClass*>());
    return 0;
}
DEFINE_HOOK(0x47EA90,SF_NearbyCellRemoval,8) {
    if(LuaNearby::initialized) {
        auto obj=R->Stack<ObjectClass*>(4);
        if(obj && (obj->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None)
            LuaNearby::moved(static_cast<TechnoClass*>(obj));
    }
    return 0;
}
// UnInit defers object destruction. Release the slot after native Limbo has
// finished removing the child's footprint, including destruction outside Starfall.
DEFINE_HOOK(0x5F6625,SF_MountUninit,7) {
    auto object=R->ESI<ObjectClass*>();
    if(!UnitMount::links.empty() && (object->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None)
        Mount::forget(object->UniqueID);
    return 0;
}
DEFINE_HOOK(0x6F4500, SF_UnitDestroyed, 5) {
    ArtSp::ContinuousBeam::forget(R->ECX<TechnoClass*>());
    splitTargets.erase(R->ECX<TechnoClass*>()->UniqueID);
    ProjectileShield::forget(R->ECX<TechnoClass*>());
    auto id=R->ECX<TechnoClass*>()->UniqueID;
    Taunt::forget(id);
    Mount::forget(id);
    LuaNearby::forget(id);
    auto power=buildingPower.find(id);
    if(power!=buildingPower.end()) {
        if(auto owner=power->second.owner) owner->RecheckPower=true;
        buildingPower.erase(power);
    }
    stopAuraSources(id);
    if(auraGrants.erase(id)) {
        for(auto& entry:auraSources) entry.second.targets.erase(id);
    }
    auraLosses.erase(id);
    linkedGrants.erase(id);
    transportGrants.erase(id);
    std::erase_if(transportStateKeys,[&](const auto& key){return key.first==id;});
    spawnGrants.erase(id);
    UnitHealth::pools.erase(id);gainSeen.erase(id);gainInputs.erase(id);
    hitSequences.erase(id);std::erase_if(observedHits,[&](const auto& entry){return entry.first.first==id;});
    warheadGrants.erase(id);passiveStacks.erase(id);cloakableUnits.erase(id);mirageUnits.erase(id);immuneUnits.erase(id);mindImmuneUnits.erase(id);empUnits.erase(id);projectileBase.erase(id);projectileState.erase(id);rangeBase.erase(id);rangeState.erase(id);fireRateBase.erase(id);fireRateState.erase(id);
    experienceState.erase(id);
    activeTooltips.erase(id);passiveTints.erase(id);VisualScale::base.erase(id);VisualScale::state.erase(id);
    weaponRules.erase(id); volleyWeapons.erase(id); mountCopies.erase({id,0});mountCopies.erase({id,1});
    previousState.erase(id); reportedDeaths.erase(id); removedPassives.erase(id); counts.erase(id); triggerTotals.erase(id); applied.erase(id);
    BuildingResearch::forget(id);
    ForceMove::forget(id);ForceMove::settling.erase(id);
    LuaAnim::forget(id);scriptTimers.forget(id);
    if(activeObjects) activeObjects->erase(id);
    UnitOrders::forget(id);
    ActSp::forget(id);
    ArtSp::Tracers::forget(id);
    return 0;
}
// QueueMission and ForceMission ignore new orders while the motion owns the unit.
DEFINE_HOOK(0x5B35E0,SF_ForceMoveQueue,5) {
    if(Taunt::blocksMission(R->ECX<AbstractClass*>(),R->Stack<Mission>(4))) {R->EAX(0);return 0x5B3642;}
    if((ForceMove::locked(R->ECX<AbstractClass*>()) || empLocked(R->ECX<AbstractClass*>()))) {R->EAX(0);return 0x5B3642;}
    return 0;
}
DEFINE_HOOK(0x5B2FD0,SF_ForceMoveMission,6) {
    if(Taunt::blocksMission(R->ECX<AbstractClass*>(),R->Stack<Mission>(4))) return 0x5B3030;
    if((ForceMove::locked(R->ECX<AbstractClass*>()) || empLocked(R->ECX<AbstractClass*>())) && R->Stack<Mission>(4)!=Mission::Sleep) return 0x5B3030;
    return 0;
}
// After the prologues shared with Phobos sync logging, before the destination is read.
DEFINE_HOOK(0x74197A,SF_ForceMoveVehicleDestination,7) {
    auto vehicle=R->EBP<TechnoClass*>();
    if((Taunt::targetOf(vehicle)?Taunt::fixedPosition(vehicle):UnitOrders::fixedPosition(vehicle))) R->Stack<AbstractClass*>(0x8C,nullptr);
    if(UnitMount::mounted(R->EBP<AbstractClass*>()) || ForceMove::locked(R->EBP<AbstractClass*>()) || empLocked(R->EBP<AbstractClass*>())) R->Stack<AbstractClass*>(0x8C,nullptr);
    return 0;
}
DEFINE_HOOK(0x51AA45,SF_ForceMoveInfantryDestination,7) {
    auto infantry=R->ECX<TechnoClass*>();
    if((Taunt::targetOf(infantry)?Taunt::fixedPosition(infantry):UnitOrders::fixedPosition(infantry))) R->Stack<AbstractClass*>(0x38,nullptr);
    if(UnitMount::mounted(R->ECX<AbstractClass*>()) || ForceMove::locked(R->ECX<AbstractClass*>()) || empLocked(R->ECX<AbstractClass*>())) R->Stack<AbstractClass*>(0x38,nullptr);
    return 0;
}
DEFINE_HOOK(0x700C40,SF_ForceMoveControl,9) {
    if(UnitMount::mounted(R->ECX<AbstractClass*>()) || ForceMove::locked(R->ECX<AbstractClass*>()) || empLocked(R->ECX<AbstractClass*>())) {R->EAX(0);return 0x700C53;}
    return 0;
}

// After target and controllability validation in native synchronized orders.
DEFINE_HOOK(0x4C7225,SF_ApplyObjectOrder,5) {
    auto source=R->EDI<TechnoClass*>();
    auto event=R->ESI<EventClass*>();
    if(!UnitMount::links.empty()) {
        if(UnitMount::mounted(source)) return 0x4C8109;
        Mount::order(source,event->MegaMission.Mission==static_cast<BYTE>(Mission::Attack)?event->MegaMission.Target.As_Techno():nullptr);
    }
    UnitOrders::cancel(source);
    BetterGarrison::applyOrder(source,event);
    return 0;
}
// The separate synchronized stop event has passed its native ownership checks.
DEFINE_HOOK(0x4C757D,SF_ApplyStopOrder,6) {
    auto source=R->ESI<TechnoClass*>();
    if(!UnitMount::links.empty()) {
        if(UnitMount::mounted(source)) return 0x4C8109;
        Mount::order(source,nullptr);
    }
    UnitOrders::cancel(source);
    ActSp::stopOrder(source);
    ArtSp::ContinuousBeam::stop(source);
    splitTargets.erase(source->UniqueID);
    return 0;
}
#include "runtime/CombatHooks.inc"

// PlacePowerupCrate has chosen a cell but has not allocated a crate slot yet.
DEFINE_HOOK(0x56BF6E,SF_RedirectCrateFromPickup,9) {
    if(!groundPickups.empty() && !redirectCrateFromPickup(*R->EAX<CellStruct*>())) return 0x56C00A;
    return 0;
}
// Both random refreshes and targeted crates pass this final overlay check.
// Its normal failure path lets the native random generator try another cell.
DEFINE_HOOK(0x4A192D,SF_CheckCrateCellForPickup,6) {
    if(!groundPickups.empty()) {
        auto at=R->EAX<CellClass*>()->MapCoords;
        if(groundPickups.contains({at.X,at.Y})) return 0x4A1A8D;
    }
    return 0;
}
// The tactical draw surface has been selected; render before the native object layers.
DEFINE_HOOK(0x6D465F,SF_DrawGroundPickups,5) {
    drawGroundPickups();
    EntityWalls::drawPreview();
    return 0;
}
// Share the tactical flying-string draw pass used by Ares and Phobos.
DEFINE_HOOK(0x6D4684,SF_DrawRewardText,6) {
    ArtSp::ContinuousBeam::draw(DSurface::Temp,{0,0,DSurface::ViewBounds.Width,DSurface::ViewBounds.Height},Unsorted::CurrentFrame);
    ArtSp::Tracers::draw(DSurface::Temp,{0,0,DSurface::ViewBounds.Width,DSurface::ViewBounds.Height},Unsorted::CurrentFrame);
    ArtSp::TipsLabel::draw();
    RewardText::draw();
    return 0;
}

DEFINE_HOOK(0x55B4E1, SF_Frame, 5) {
    LuaAnim::tick();
    LuaNearby::expire();
    SonicRipple::expire(Unsorted::CurrentFrame);
    ArtSp::Tracers::expire(Unsorted::CurrentFrame);
    ArtSp::ContinuousBeam::tick();
    RewardText::expire(Unsorted::CurrentFrame);
    ForceMove::tick();
    Mount::tick();
    incomingDamage.clear();
    clearCompletedVolleys();
    UnitHealth::syncAll();
    if(recheckLoadedStages) {
        recheckLoadedStages=false;
        Research::ensureReady();
        for(const auto& [house,projects]:Research::unlocked) {
            Research::Detail::checkBonds(house);Research::Detail::checkStages(house);
        }
    }
    BuildingResearch::tickBuildings();
    refreshSpawnGrants();
    expireWarheadGrants();
    refreshAuras();
    refreshTransportGrants();
    refreshAllLinkedPassives();
    reconcileAllStacks();
    tickGains();
    observeTransitions();
    tickStates();
    Taunt::tick();
    tickIntervals();
    collectPickups();
    dispatch();
    tickScriptTimers();
    ActSp::tick();
    UnitOrders::tick();
    return 0;
}

DEFINE_HOOK(0x6DD8B0, SF_MapAction, 6) {
    auto action=R->ECX<TActionClass*>();
    auto kind=static_cast<int>(action->ActionKind);
    if(kind!=9600 && kind!=9601 && kind!=9602) return 0;
    int target=action->Param4;
    auto house=R->Stack<HouseClass*>(4);
    if(kind==9601) {
        Research::ensureReady();
        auto preset=grantPresets.find(action->Param3);
        if(preset!=grantPresets.end()) grantFromTrigger(preset->second,target,house,R->Stack<ObjectClass*>(8),R->Stack<TriggerClass*>(12));
    } else if(kind==9602) {
        if(target!=0) house=HouseClass::Index_IsMP(target)?HouseClass::FindByPlayerAt(target):nullptr;
        Research::ensureReady();
        auto preset=ActionSequence::mapScripts.find(action->Param3);
        if(house && preset!=ActionSequence::mapScripts.end() && ActionSequence::lastCast!=UINT32_MAX) {
            auto object=R->Stack<ObjectClass*>(8);DWORD objectId=0;
            if(object && (object->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None)
                objectId=object->UniqueID;
            auto at=R->Stack<CellStruct*>(16);
            CellStruct point{-1,-1};
            if(at && at->X>=0 && at->Y>=0 && at->X<512 && at->Y<512 && MapClass::Instance.IsWithinUsableArea(*at,false)) point=*at;
            auto id=++ActionSequence::lastCast;
            auto name=preset->second+":"+std::to_string(objectId);
            auto& cast=ActionSequence::casts.emplace(id,ActionSequence::Cast{name,house->ArrayIndex,point}).first->second;
            auto event=sequenceEvent(id,cast);
            Objects live;for(auto unit:TechnoClass::Array) if(alive(unit)) live.emplace(unit->UniqueID,unit);
            auto previous=activeObjects;activeObjects=&live;
            if(auto step=ActionSequence::stepFor(cast,preset->second)) apply(step->actions.front(),event,findLive(live,objectId),live,0);
            activeObjects=previous;ActionSequence::retire(id);
        } else configLog("Map action 9602: unknown script preset or invalid house: "+std::to_string(action->Param3));
    } else {
        if(target!=0) house=HouseClass::Index_IsMP(target)?HouseClass::FindByPlayerAt(target):nullptr;
        if(eligibleHouse(house)) Research::award(house->ArrayIndex,action->Param3);
    }
    R->AL(1); return 0x6DD910;
}

// An empty team can be retired before the next frame dispatches OnDeath.
// Keep its live script and targets until the queued callbacks get their turn.
DEFINE_HOOK(0x6E9149,SF_TeamWaitForDeathCopy,6) {
    auto team=R->ESI<TeamClass*>();
    if(!team->FirstUnit && team->UniqueID!=static_cast<DWORD>(-1) &&
       std::any_of(pending.begin(),pending.end(),[team](const Event& e){return e.deathTeam==team->UniqueID;}))
        return 0x6E9F4A;
    return 0;
}
// After Phobos' TeamClass_AI hook: EAX is the action and ECX points to the script node.
DEFINE_HOOK(0x6E944B, SF_TeamGrantPassives, 6) {
    if(R->EAX<int>()!=9601) return 0;
    auto team=R->ESI<TeamClass*>();
    auto node=R->ECX<ScriptActionNode*>();
    Research::ensureReady();
    auto preset=grantPresets.find(node->Argument);
    if(preset!=grantPresets.end())
        for(auto unit=team->FirstUnit;unit;unit=unit->NextTeamMember) grantFromPreset(unit,preset->second);
    return 0x6E94BA; // native "step completed" epilogue, including for unknown presets
}

#include "runtime/Savegame.inc"

SYRINGE_HANDSHAKE(info) {
    if(!info || info->cbSize<sizeof(SyringeHandshakeInfo)) return E_INVALIDARG;
    if(info->exeCRC!=0x54CC0A13) return E_FAIL;
    if(info->Message && info->cchMessage>0)
        _snprintf_s(info->Message,info->cchMessage,_TRUNCATE,"Starfall Lab 0.40");
    return S_OK;
}
