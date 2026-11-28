// Reservations are local to one dispatch pass and can be reconstructed from the saved events.
std::map<DWORD,CellStruct> landingSites;
bool landingUnitReady(UnitClass* unit) {
    auto loco=unit->Locomotor?locomotion_cast<JumpjetLocomotionClass*>(unit->Locomotor):nullptr;
    return unit->Owner && loco && !loco->Is_Piggybacking() && unit->IsOnMap && !unit->InLimbo && !unit->Transporter &&
        !unit->IsCrashing && !unit->IsSinking && !unit->IsFallingDown && !unit->IsImmobilized &&
        !unit->IsBeingWarpedOut() && !unit->IsAttackedByLocomotor && !ForceMove::locked(unit) &&
        !unit->Deploying && !unit->Undeploying && !unit->Deployed && !UnitMount::mounted(unit);
}
bool landingSiteFree(UnitClass* unit,TechnoTypeClass* to,CellStruct site) {
    auto& map=MapClass::Instance;
    auto cell=map.TryGetCellAt(site);
    if(!cell || !map.IsWithinUsableArea(site,false) || cell->ContainsBridge() || cell->GetBuilding() ||
        cell->GetTerrain(false) || (cell->Jumpjet && cell->Jumpjet!=unit) ||
        !cell->IsClearToMove(to->SpeedType,true,true,-1,to->MovementZone,-1,false)) return false;
    for(auto obj=cell->GetContent();obj;obj=obj->NextObject)
        if(obj!=unit && obj->IsAlive && (obj->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None && !obj->IsInAir()) return false;
    for(const auto& [id,pos]:landingSites) if(id!=unit->UniqueID && pos==site) return false;
    return true;
}
bool queueLanding(UnitClass* unit,TechnoTypeClass* to,const Skill& skill,Event& e,int nextDraw) {
    if(!publicPhobosConverter() || !landingUnitReady(unit) || !groundFlightPair(unit->Type,to,true) ||
        landingSites.contains(unit->UniqueID)) return false;
    const auto center=unit->GetMapCoords();
    for(int radius=0;radius<=3;++radius) for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) {
        if(std::max(std::abs(x),std::abs(y))!=radius) continue;
        CellStruct site{short(center.X+x),short(center.Y+y)};
        if(!landingSiteFree(unit,to,site)) continue;
        e.landing=LandingOrder{unit->UniqueID,static_cast<DWORD>(unit->Owner->ArrayIndex),
            static_cast<DWORD>(Unsorted::CurrentFrame),site,unit->Type->ID,to->ID,skill.id,nextDraw};
        landingSites.emplace(unit->UniqueID,site);
        unit->SetTarget(nullptr);
        unit->SetDestination(MapClass::Instance.GetCellAt(site),true);
        unit->ForceMission(Mission::Move);
        pending.push_back(std::move(e));
        return true;
    }
    return false;
}
enum class LandingStatus { Waiting, Converted, Failed };
LandingStatus advanceLanding(const LandingOrder& order,const Objects& objects) {
    auto target=findLive(objects,order.unit);
    auto unit=target && target->WhatAmI()==AbstractType::Unit?static_cast<UnitClass*>(target):nullptr;
    auto to=UnitTypeClass::Find(order.to.c_str());
    if(!unit || !to || !unit->Owner || unit->Owner->ArrayIndex!=order.house || unit->Type->ID!=order.from ||
        !landingUnitReady(unit) || !groundFlightPair(unit->Type,to,true)) return LandingStatus::Failed;
    auto loco=locomotion_cast<JumpjetLocomotionClass*>(unit->Locomotor);
    auto cell=MapClass::Instance.GetCellAt(order.cell);
    if(static_cast<DWORD>(Unsorted::CurrentFrame)-order.started>=450 || !landingSiteFree(unit,to,order.cell) ||
        (unit->Destination && unit->Destination!=cell)) {
        if(unit->Destination==cell) {
            unit->SetDestination(nullptr,true);
            loco->Stop_Moving();
            unit->ForceMission(Mission::Guard);
        }
        if(loco->State==JumpjetLocomotionClass::Descending && unit->GetHeight()>0)
            loco->State=JumpjetLocomotionClass::Ascending;
        return LandingStatus::Failed;
    }
    if(unit->GetMapCoords()!=order.cell) return LandingStatus::Waiting;
    if(loco->State==JumpjetLocomotionClass::Grounded && unit->GetHeight()==0) {
        if(unit->Destination || loco->Is_Moving() || loco->Is_Moving_Now()) return LandingStatus::Waiting;
        return transformUnit(unit,to,publicPhobosConverter(),true)?LandingStatus::Converted:LandingStatus::Failed;
    }
    // BalloonHover normally stays aloft after reaching a destination. Request the
    // native descent on this instance only, once it is centred above its landing cell.
    const auto offset=unit->Location-cell->GetCoords();
    if(unit->Type->BalloonHover && std::abs(offset.X)<32 && std::abs(offset.Y)<32 &&
        loco->State==JumpjetLocomotionClass::Hovering) {
        unit->SetDestination(nullptr,true);
        loco->Stop_Moving();
        loco->State=JumpjetLocomotionClass::Descending;
    }
    return LandingStatus::Waiting;
}
