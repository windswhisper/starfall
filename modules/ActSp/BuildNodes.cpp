#include "BuildNodes.h"
#include "../../src/ForceMove.h"
#include "../../src/UnitMount.h"
#include <AircraftTypeClass.h>
#include <BuildingClass.h>
#include <CCINIClass.h>
#include <DisplayClass.h>
#include <HouseClass.h>
#include <LocomotionClass.h>
#include <set>

namespace BuildNodes {
namespace {
std::set<TechnoTypeClass*> enabled;
std::set<TechnoTypeClass*> requiresBuilding;
bool loaded=false;
bool checkingOrder=false;

void load() {
    auto ini=CCINIClass::INI_Rules;
    if(loaded || !ini || !TechnoTypeClass::Array.Count) return;
    for(auto type:TechnoTypeClass::Array) {
        if(type->WhatAmI()==AbstractType::BuildingType) {
            char allowed[1024]{};
            if(ini->ReadString(type->ID,"Adjacent.Allowed","",allowed)>0)
                requiresBuilding.insert(type);
        } else if(ini->ReadBool(type->ID,"BuildNode.Enabled",false)) {
            enabled.insert(type);
        }
    }
    loaded=true;
}

bool available(FootClass* node,TechnoTypeClass* type) {
    if(node->InLimbo || !node->IsOnMap || !node->IsAlive || node->Health<=0 || node->Transporter ||
       node->Absorbed || node->TubeIndex>=0 || node->IsSinking || node->IsCrashing || node->IsFallingDown ||
       node->IsBeingWarpedOut() || UnitMount::mounted(node) || ForceMove::screenLift(node)>0) return false;
    if(type->Locomotor==LocomotionClass::CLSIDs::Jumpjet || type->Locomotor==LocomotionClass::CLSIDs::Hover)
        return true;
    if(type->WhatAmI()==AbstractType::AircraftType)
        return static_cast<AircraftTypeClass*>(type)->Rotors;
    return !node->IsInAir();
}

bool inRange(BuildingTypeClass* building,int house,CellStruct origin,int width,int height,int adjacent) {
    load();
    if(enabled.empty() || requiresBuilding.contains(building) || adjacent<0) return false;
    for(auto node:FootClass::Array) {
        if(!node || !node->Owner || node->Owner->ArrayIndex!=house) continue;
        auto type=node->GetTechnoType();
        if(!enabled.contains(type)) continue;
        const auto at=node->GetMapCoords();
        const int x=at.X-origin.X,y=at.Y-origin.Y;
        // Native Adjacent counts empty cells between the node and the foundation.
        const int gap=std::max({-x,x-width+1,-y,y-height+1})-1;
        if(gap>=0 && gap<=adjacent && available(node,type)) return true;
    }
    return false;
}
}

void reset() {
    enabled.clear();
    requiresBuilding.clear();
    loaded=false;
}

// Seed the native scan's per-call result after Phobos has chosen its radius.
// An explicit prohibition still exits the scan immediately with false.
DEFINE_HOOK(0x4A8F44,ActSp_BuildNodeScan,5) {
    bool hasNode=inRange(R->ESI<BuildingTypeClass*>(),R->Stack<int>(0x38),*R->EDI<CellStruct*>(),
        R->EDX<int>(),R->Stack<int>(0x34),R->EAX<int>());
    R->Stack<BYTE>(0x3C,hasNode);
    return 0;
}
DEFINE_HOOK(0x4A8F61,ActSp_KeepBuildNodeResult,5) {return 0x4A8F66;}

// Every peer must evaluate a paid placement for the issuing house, including
// remote orders. Ordinary local cursor checks retain the native player filter.
DEFINE_HOOK(0x4A8EC3,ActSp_BuildNodeOrderHouse,6) {return checkingOrder?0x4A8ED6:0;}
DEFINE_HOOK(0x4FB1E4,ActSp_BuildNodePlace,6) {
    if(!R->EDI<void*>()) return 0;
    auto house=R->EBP<HouseClass*>();
    auto product=abstract_cast<BuildingClass*>(R->ESI<TechnoClass*>());
    if(!product || !house->IsControlledByHuman()) return 0;
    auto type=product->Type;
    if(type->PlaceAnywhere || type->PowersUpBuilding[0]) return 0;
    load();
    if(enabled.empty()) return 0;
    auto origin=R->Stack<CellStruct>(0x4C);
    checkingOrder=true;
    bool valid=DisplayClass::Instance.PassesProximityCheck(type,house->ArrayIndex,type->GetFoundationData(false),&origin);
    checkingOrder=false;
    return valid?0:0x4FB35F;
}
}
