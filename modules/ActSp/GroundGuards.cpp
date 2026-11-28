#include <BuildingClass.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <TargetClass.h>
#include <Unsorted.h>
#include "ActSp.h"
#include "GroundGuards.h"
#include "GroundDocks.h"
#include "BuildingDocks.h"
#include "GuardPoints.h"
#include "BetterGarrison.h"
#include "HighPlatforms.h"

namespace GroundGuards {
std::map<DWORD,Slot> slots;

namespace {
FootClass* findChild(int id) {
    if(!id) return nullptr;
    TargetClass target;
    target.m_ID=id;
    target.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    auto unit=target.As_Foot();
    if(unit && unit->IsAlive && unit->Health>0 &&
       (unit->WhatAmI()==AbstractType::Unit || unit->WhatAmI()==AbstractType::Infantry)) return unit;
    return nullptr;
}
int distanceSquared(CellStruct a,CellStruct b) {
    const int dx=a.X-b.X, dy=a.Y-b.Y;
    return dx*dx+dy*dy;
}
// Only cells touching the actual foundation are exits, including custom foundations.
CellClass* exitCell(BuildingClass* post,const TechnoTypeClass* type,CellStruct station) {
    auto origin=post->GetMapCoords();
    auto& map=MapClass::Instance;
    CellClass* best=nullptr;
    int nearest=INT_MAX;
    constexpr CellStruct end{0x7fff,0x7fff};
    for(auto offset=post->GetFoundationData(false);offset && *offset!=end;++offset) {
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy) continue;
            CellStruct at{short(origin.X+offset->X+dx),short(origin.Y+offset->Y+dy)};
            const int distance=distanceSquared(at,station);
            if(distance>=nearest || !map.IsWithinUsableArea(at,false)) continue;
            auto cell=map.TryGetCellAt(at);
            if(!cell || cell->GetBuilding() || cell->GetContent() ||
               !cell->IsClearToMove(type->SpeedType,false,false,-1,type->MovementZone,-1,cell->ContainsBridge())) continue;
            best=cell;
            nearest=distance;
        }
    }
    return best;
}
FootClass* spawn(BuildingClass* post,TechnoTypeClass* type,CellStruct station) {
    auto exit=exitCell(post,type,station);
    if(!exit) return nullptr;
    auto unit=static_cast<FootClass*>(type->CreateObject(post->Owner));
    if(!unit) return nullptr;
    unit->OnBridge=exit->ContainsBridge();
    if(!unit->Unlimbo(exit->GetCoordsWithBridge(),DirType::North)) {
        unit->UnInit();
        return nullptr;
    }
    post->Owner->RecheckTechTree=true;
    return unit;
}
void direct(FootClass* unit,CellClass* station,int radius) {
    const auto here=unit->GetMapCoords();
    if(distanceSquared(here,station->MapCoords)<=radius*radius) {
        if(auto threat=GuardPoints::findTarget(unit,station,radius)) {
            if(unit->Target!=threat || (unit->CurrentMission!=Mission::Attack && unit->QueuedMission!=Mission::Attack)) {
                unit->SetTarget(threat);
                unit->QueueMission(Mission::Attack,true);
            }
            return;
        }
    }
    if(unit->Target) unit->SetTarget(nullptr);
    if(here==station->MapCoords) {
        if(unit->Destination) unit->SetDestination(nullptr,true);
        if(unit->CurrentMission!=Mission::Guard && unit->QueuedMission!=Mission::Guard)
            unit->QueueMission(Mission::Guard,true);
    } else if(unit->Destination!=station || (unit->CurrentMission!=Mission::Move && unit->QueuedMission!=Mission::Move)) {
        unit->SetDestination(station,true);
        unit->QueueMission(Mission::Move,true);
    }
}
void update(BuildingClass* post,const Config& config,DWORD frame) {
    if(!post->IsAlive || post->Health<=0 || !post->Owner || post->Owner->Defeated) return;
    auto point=GuardPoints::station(post->UniqueID,config.initialPoint);
    const bool docks=config.useBuildingDocks && GroundDocks::berthCount(post)>0;
    auto& map=MapClass::Instance;
    auto station=point.X>=0 && point.Y>=0 && map.IsWithinUsableArea(point,false)?map.TryGetCellAt(point):nullptr;
    if(!station && !docks) return;
    auto& slot=slots[post->UniqueID];
    const int house=post->Owner->ArrayIndex;
    const bool captured=slot.house>=0 && slot.house!=house;
    auto child=findChild(slot.child);
    if(captured || (slot.child && (!child || child->Owner!=post->Owner))) {
        if(child && slot.dock!=DockState::None) {
            BuildingDocks::release(post,child);
            if(child->IsOnMap && !child->InLimbo && slot.dock==DockState::Parked) {
                if(auto exit=exitCell(post,child->GetTechnoType(),child->GetMapCoords())) {
                    child->SetDestination(exit,true);
                    child->QueueMission(Mission::Move,true);
                }
            }
        }
        slot={0,frame+config.delay,house};
        child=nullptr;
    }
    slot.house=house;
    if(post->InLimbo || !post->IsOnMap || post->Deactivated || post->IsUnderEMP() ||
       post->IsBeingWarpedOut() || !post->IsPowerOnline() ||
       post->CurrentMission==Mission::Construction || post->CurrentMission==Mission::Selling) return;
    if(!child) {
        if(static_cast<int>(frame-slot.readyFrame)<0) return;
        child=docks?GroundDocks::spawn(post,config.type):spawn(post,config.type,point);
        if(!child) return;
        slot.child=TargetClass(child).m_ID;
        slot.readyFrame=0;
        slot.dock=docks?DockState::Parked:DockState::None;
    }
    if(!child->IsOnMap || child->InLimbo || child->Transporter || child->Absorbed || child->Deactivated ||
       child->IsImmobilized || child->IsUnderEMP() || child->IsBeingWarpedOut() || child->IsSinking ||
       child->IsAttackedByLocomotor) return;
    if(docks && GroundDocks::update(post,child,slot,station,frame)) return;
    direct(child,station,config.radius);
}
}

void forget(DWORD postId) {
    const auto it=slots.find(postId);
    if(it==slots.end()) return;
    if(it->second.dock!=DockState::None) {
        auto unit=findChild(it->second.child);
        if(unit && !unit->InLimbo && unit->IsOnMap && unit->CurrentMission==Mission::Sleep)
            unit->QueueMission(Mission::Guard,true);
    }
    slots.erase(it);
}
}

void ActSp::tick() {
    HighPlatforms::tick();
    BetterGarrison::tick();
    DWORD frame=Unsorted::CurrentFrame;
    if(frame%8) return;
    for(auto post:BuildingClass::Array) {
        if(!post) continue;
        if(auto config=GroundGuards::config(post)) GroundGuards::update(post,*config,frame);
    }
}
