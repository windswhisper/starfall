#include <HouseClass.h>
#include <MapClass.h>
#include <TargetClass.h>
#include <Unsorted.h>
#include "BuildingDocks.h"
#include "GroundDocks.h"

namespace GroundDocks {
using GroundGuards::DockState;

int berthCount(BuildingClass* post) {
    return std::min(post->Type->UnitRepair?1:post->Type->NumberOfDocks,post->RadioLinks.Capacity);
}

namespace {
CellClass* berth(BuildingClass* post,int index) {
    if(index<0 || index>=berthCount(post)) return nullptr;
    auto coord=post->GetCoords();
    if(!post->Type->UnitRepair && index<post->Type->DockingOffsets.Capacity)
        coord+=post->Type->DockingOffsets[index];
    auto& map=MapClass::Instance;
    const auto at=CellClass::Coord2Cell(coord);
    if(!map.IsWithinUsableArea(at,false)) return nullptr;
    auto cell=map.TryGetCellAt(at);
    if(!cell || std::abs(cell->GetCoordsWithBridge().Z-coord.Z)>Unsorted::LevelHeight/2) return nullptr;
    return cell;
}
bool freeFor(BuildingClass* post,CellClass* cell,TechnoTypeClass* type,FootClass* child) {
    if(!cell) return false;
    for(auto object=cell->GetContent();object;object=object->NextObject)
        if(object!=post && object!=child && object->IsAlive && !object->InLimbo) return false;
    auto building=cell->GetBuilding();
    if(building) return building==post;
    return (child && child->GetMapCoords()==cell->MapCoords) ||
        cell->IsClearToMove(type->SpeedType,false,false,-1,type->MovementZone,-1,cell->ContainsBridge());
}
int openBerth(BuildingClass* post,TechnoTypeClass* type,FootClass* child) {
    const int current=child?post->FindLinkIndex(child):-1;
    if(current>=0 && freeFor(post,berth(post,current),type,child)) return current;
    for(int i=0;i<berthCount(post);++i)
        if(!post->GetNthLink(i) && freeFor(post,berth(post,i),type,child)) return i;
    return -1;
}
bool reserveAt(BuildingClass* post,FootClass* unit,int index) {
    if(index<0 || !BuildingDocks::reserve(post,unit,berthCount(post))) return false;
    int actual=post->FindLinkIndex(unit);
    if(actual==index) return true;
    if(actual<0 || post->GetNthLink(index)) return false;
    // Radio allocates the first unreserved index. Its cell may be physically
    // occupied. Keep the reciprocal link and move it to the usable pad.
    std::swap(post->RadioLinks[actual],post->RadioLinks[index]);
    return true;
}
void wait(FootClass* unit) {
    if(unit->Target) unit->SetTarget(nullptr);
    if(unit->Destination) unit->SetDestination(nullptr,true);
    if(unit->CurrentMission!=Mission::Sleep || unit->QueuedMission!=Mission::None) {
        unit->Locomotor->Stop_Moving();
        unit->ForceMission(Mission::Sleep);
    }
}
bool recallPoint(BuildingClass* post,CellClass* station) {
    if(!station || station->GetBuilding()==post) return true;
    for(int i=0;i<berthCount(post);++i)
        if(berth(post,i)==station) return true;
    // A rally on a cell touching the foundation can also be issued with the mouse.
    const auto origin=post->GetMapCoords();
    const CellStruct end{0x7fff,0x7fff};
    for(auto p=post->GetFoundationData(false);p && *p!=end;++p)
        if(std::abs(origin.X+p->X-station->MapCoords.X)<=1 && std::abs(origin.Y+p->Y-station->MapCoords.Y)<=1) return true;
    return false;
}
}

FootClass* spawn(BuildingClass* post,TechnoTypeClass* type) {
    const int index=openBerth(post,type,nullptr);
    if(index<0) return nullptr;
    auto cell=berth(post,index);
    auto unit=static_cast<FootClass*>(type->CreateObject(post->Owner));
    if(!unit) return nullptr;
    unit->OnBridge=cell->ContainsBridge();
    bool placed=false;
    if(reserveAt(post,unit,index)) {
        // Normal placement rejects a building's own foundation. This narrowly
        // scoped exception is used only for a checked, reserved, empty berth.
        const bool inside=cell->GetBuilding()==post;
        if(inside) ++Unsorted::ScenarioInit;
        placed=unit->Unlimbo(cell->GetCoordsWithBridge(),post->PrimaryFacing.Current().GetDir());
        if(inside) --Unsorted::ScenarioInit;
    }
    if(!placed) {
        BuildingDocks::release(post,unit);
        unit->UnInit();
        return nullptr;
    }
    wait(unit);
    post->Owner->RecheckTechTree=true;
    return unit;
}

bool update(BuildingClass* post,FootClass* unit,GroundGuards::Slot& slot,CellClass* station,DWORD frame) {
    const bool recall=recallPoint(post,station);
    auto type=unit->GetTechnoType();
    const bool empty=type->Ammo>0 && unit->Ammo<=0;
    const bool servicing=slot.dock==DockState::Parked && slot.readyFrame;
    if(!recall && !empty && !servicing) {
        BuildingDocks::release(post,unit);
        slot.dock=DockState::Patrol;
        return false;
    }
    if(unit->Target) unit->SetTarget(nullptr);
    const int index=openBerth(post,type,unit);
    if(!reserveAt(post,unit,index)) {
        BuildingDocks::release(post,unit);
        slot.dock=DockState::Returning;
        slot.readyFrame=0;
        wait(unit);
        return true;
    }
    auto cell=berth(post,index);
    auto delta=unit->GetCoords()-cell->GetCoordsWithBridge();
    if(std::abs(delta.X)>12 || std::abs(delta.Y)>12 || std::abs(delta.Z)>Unsorted::LevelHeight/2) {
        slot.dock=DockState::Returning;
        slot.readyFrame=0;
        if(unit->Destination!=cell || (unit->CurrentMission!=Mission::Move && unit->QueuedMission!=Mission::Move)) {
            unit->SetDestination(cell,true);
            unit->QueueMission(Mission::Move,true);
        }
        return true;
    }
    wait(unit);
    slot.dock=DockState::Parked;
    if(type->Ammo>0 && unit->Ammo<type->Ammo) {
        if(!slot.readyFrame) slot.readyFrame=frame+std::max(0,post->Type->SpawnReloadRate);
        if(static_cast<int>(frame-slot.readyFrame)<0) return true;
        unit->Ammo=type->Ammo;
        unit->MarkForRedraw();
    }
    slot.readyFrame=0;
    return true;
}
}

// Only this parent's own vehicle may cross its foundation on the way to or
// out of the pad. Continue the native object loop so other blockers still count.
DEFINE_HOOK(0x73F6D6,ActSp_GroundDockAccess,6) {
    auto post=R->ESI<BuildingClass*>();
    auto unit=R->EBX<FootClass*>();
    auto it=GroundGuards::slots.find(post->UniqueID);
    if(it==GroundGuards::slots.end() || it->second.dock==GroundGuards::DockState::None ||
       it->second.child!=TargetClass(unit).m_ID || unit->Owner!=post->Owner) return 0;
    if((it->second.dock==GroundGuards::DockState::Returning && post->ContainsLink(unit)) ||
       (it->second.dock==GroundGuards::DockState::Patrol && unit->GetCell()->GetBuilding()==post)) return 0x73FA87;
    return 0;
}

// A finite magazine on a dock-managed vehicle refills at its own building.
// This is the native timer's ammo increment, after the timer has expired.
DEFINE_HOOK(0x6FB054,ActSp_GroundDockVehicleReload,5) {
    auto unit=R->ESI<TechnoClass*>();
    if(unit->WhatAmI()!=AbstractType::Unit || GroundGuards::slots.empty()) return 0;
    const int id=TargetClass(unit).m_ID;
    for(const auto& [post,slot]:GroundGuards::slots)
        if(slot.dock!=GroundGuards::DockState::None && slot.child==id && unit->Owner && slot.house==unit->Owner->ArrayIndex)
            return 0x6FB06E;
    return 0;
}
