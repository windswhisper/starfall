#pragma once
#include "ForceMoveSpec.h"
#include "ProjectileShield.h"
#include "HealthFormula.h"
#include "Research.h"
#include <GeneralStructures.h>
#include <GeneralDefinitions.h>
#include <fstream>

class AnimTypeClass;
class BuildingTypeClass;
class ParticleSystemTypeClass;
class TechnoTypeClass;
class WarheadTypeClass;
class WeaponTypeClass;

namespace PassiveRules {
using std::string;
using std::vector;
using ScriptFields=std::map<string,string>;
constexpr std::array<std::string_view,7> percentActions={"changespeed","changefirepower","changearmor","changescale","changeprojectilescale","changeweaponrange","changefirerate"};
constexpr size_t scaleIndex=3, projectileScaleIndex=4, weaponRangeIndex=5, fireRateIndex=6;
struct SlotRule {
    bool disabled=false;
    WeaponTypeClass* replacement=nullptr;
};
using WeaponRules=std::array<SlotRule,2>;
// CollectCrate rolls the game's weighted reward table for overlay data 19.
constexpr Powerup randomCrate=static_cast<Powerup>(19);
struct NearbyRule {
    int range=0,least=0,most=INT_MAX,interval=10;
    vector<string> houses,kinds,types;
};
struct AuraRule {
    int range=0;
    bool includeSelf=false;
    vector<string> passives,houses,types;
};
struct ConfigFormula {
    HealthFormula expression;
    string label;
    mutable bool reported=false;

    bool compile(const string& section,const string& field,const string& value) {
        label="["+section+"] "+field+"="+value;
        return expression.compile(value) && std::all_of(expression.counterNames().begin(),expression.counterNames().end(),
            [](const string& key){return Research::knownResearchVariable(key);});
    }
    void report() const {
        if(reported) return;
        reported=true;
        std::ofstream("Starfall.log",std::ios::app)<<"Starfall: skipped invalid formula result: "<<label<<"\n";
    }
};
struct TooltipSegment {
    std::wstring literal;
    ConfigFormula number;
};
struct PassiveAction {
    string counter;
    ConfigFormula formula;
    ConfigFormula amountFormula;
    ConfigFormula signalFormula;
    string signal,signalTarget;
    vector<string> signalHouses;
    int signalRange=0;
    double signalValue=0;
    bool signalIncludeSelf=false;
    int percentDelta=0;
    size_t percentIndex=percentActions.size();
    string pickupType;
    vector<string> removePickupTypes;
    int pickupRange=0;
    std::optional<Research::Rarity> researchRarity;
    vector<string> researchOptions;
    Powerup crateType=randomCrate;
    string mountNode,mountSlot,mountAnchor;
    int removeMountCount=1;
    CoordStruct mountOffset{};
    bool transportFire=true;
    vector<string> excludeParentPassives;
    TechnoTypeClass* unitType=nullptr;
    vector<TechnoTypeClass*> randomUnitTypes;
    vector<int> randomWeights;
    int randomCount=1,randomWeightTotal=0;
    BuildingTypeClass* buildingType=nullptr;
    string owner;
    string action, target, passive, weapon, order;
    WarheadTypeClass* warhead=nullptr;
    AnimTypeClass* playAnim=nullptr;
    ParticleSystemTypeClass* particleSystem=nullptr;
    string sound;
    int shakeX=0,shakeY=4;
    bool shakeLocal=true;
    bool showText=true;
    string script,function,skillId;
    vector<string> passiveList;
    ForceMove::Spec forceMove;
    ForceMove::JumpSpec jump;
    ForceMove::EjectSpec eject;
    CellStruct teleportCell{-1,-1};
    bool destinationIsEventTarget=false;
    bool teleportInstant=false;
    bool landFirst=false;
    int searchRadius=3;
    bool targetIsEventTarget=false,stackOnReapply=false;
    int amount=0, maxHealthPercent=0, duration=-1;
};
enum class DeployPosture { None, Undeployed, Deployed };

constexpr std::array<std::string_view,7> stateMultiplierKeys={"modifier.speedmultiplier","modifier.firepowermultiplier",
    "modifier.armormultiplier","modifier.weaponrangemultiplier","modifier.fireratemultiplier",
    "modifier.powermultiplier","modifier.experiencemultiplier"};
constexpr size_t powerMultiplierIndex=5;
constexpr size_t experienceMultiplierIndex=6;
struct Skill {
    DeployPosture deployPosture=DeployPosture::None;
    int visualScale=100,projectileScale=100;
    std::array<double,stateMultiplierKeys.size()> multipliers={1,1,1,1,1,1,1};
    ConfigFormula conditionFormula;
    bool passengerCombat=false;
    int extraShotCount=0;
    bool multiShotPreferSameLayer=false;
    bool taunt=false;
    int tauntPriority=0;
    ConfigFormula multiShotFormula;
    bool hasMultiShot() const { return extraShotCount>0 || !multiShotFormula.expression.empty(); }
    vector<std::pair<size_t,ConfigFormula>> stateFormulas;
    vector<TooltipSegment> tooltipText;
    int tooltipDecimals=-1;
    NearbyRule nearby;
    AuraRule aura;
    ProjectileShield::Rules shield;
    AnimTypeClass* anim=nullptr;
    AnimTypeClass* triggerAnim=nullptr;
    AnimTypeClass* gainAnim=nullptr;
    bool animAlways=false;
    bool animWhileReady=false;
    std::optional<ColorStruct> tint;
    int tintStrength=50;
    string animTarget;
    vector<string> linkedPassives;
    vector<string> spawnPassives;
    vector<string> transportPassives;
    bool transportStack=false;
    bool berserk=false, mirage=false, immune=false, mindImmune=false, emp=false, cloakable=false;
    string id, trigger;
    ScriptFields scriptParams;
    string signal;
    vector<string> senderHouses,senderKinds;
    vector<PassiveAction> actions;
    vector<int> randomActionWeights;
    int randomActionCount=0,randomActionTotal=0;
    vector<string> houses, kinds;
    int count, chance, cooldown, minDamage, maxTriggers=0, stackMax=INT_MAX;
    int requiredMask=0,requiredBits=0;
    unsigned stateDelayFrames=0;
    int interval=0;
    WeaponRules slots{};
};

// Owned by the scenario's passive registry. Used while formatting a research card.
const Skill* findSkill(const string& id);
}
