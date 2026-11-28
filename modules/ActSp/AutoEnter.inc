// Reservations are rebuilt from synchronized orders, including after a load.
using EntryReservations=std::map<DWORD,int>;

bool transportUsesSize(UnitClass* transport) {
    auto [it,inserted]=passengerSizeRules.try_emplace(transport->Type,true);
    if(inserted && CCINIClass::INI_Rules && GetModuleHandleW(L"Phobos.dll"))
        it->second=CCINIClass::INI_Rules->ReadBool(transport->Type->ID,"Passengers.BySize",true);
    return it->second;
}
int entrySpace(TechnoClass* unit,TechnoClass* host) {
    auto transport=abstract_cast<UnitClass*>(host);
    return transport && transportUsesSize(transport)?static_cast<int>(unit->GetTechnoType()->Size):1;
}
bool enterTarget(const Order& o,const Definition& def,TechnoClass* host,bool approaching,int reserved) {
    auto unit=o.unit;
    if(!Taunt::present(host) || host==unit || host->IsInAir() || host->CurrentMission==Mission::Selling ||
       host->CurrentMission==Mission::Construction) return false;
    auto cell=host->GetMapCoords();
    int dx=cell.X-o.center.X,dy=cell.Y-o.center.Y;
    if(dx*dx+dy*dy>o.radius*o.radius) return false;
    const bool friendly=unit->Owner==host->Owner || unit->Owner->IsAlliedWith(host->Owner);
    if(auto building=abstract_cast<BuildingClass*>(host)) {
        if(auto soldier=abstract_cast<InfantryClass*>(unit)) {
            return (def.enterKinds&CommandBarOrders::Garrison) && (friendly || building->Owner->IsNeutral()) &&
                soldier->Type->Occupier && building->Type->CanBeOccupied &&
                reserved<building->Type->MaxNumberOccupants-building->Occupants.Count &&
                building->CanBeOccupiedBy(soldier);
        }
        return (def.enterKinds&CommandBarOrders::TankBunker) && friendly && building->Type->Bunker && !reserved &&
            !building->BunkerLinkedItem && (building->ContainsLink(unit) || !building->HasAnyLink()) &&
            (approaching || building->TankBunkerState==TankBunkerState::Idle);
    }
    auto transport=abstract_cast<UnitClass*>(host);
    if(!(def.enterKinds&CommandBarOrders::Transport) || !abstract_cast<InfantryClass*>(unit) ||
       !friendly || !transport || transport->Type->Passengers<=0 ||
       unit->GetTechnoType()->Size>transport->Type->SizeLimit) return false;
    int aboard=transportUsesSize(transport)?transport->Passengers.GetTotalSize():transport->Passengers.NumPassengers;
    if(entrySpace(unit,host)>transport->Type->Passengers-aboard-reserved) return false;
    if(transport->ContainsLink(unit)) return true;
    // A busy docking link is temporary. The native enter mission queues approaching passengers.
    if(approaching && transport->HasAnyLink()) return true;
    return unit->SendCommand(RadioCommand::QueryCanEnter,transport)==RadioCommand::AnswerPositive;
}

// A fresh order has no target and no scheduled approach check. A targetless
// order with a deadline already spent its assignment, including in old saves.
bool updateEnter(Order& o,const Definition& def,unsigned frame,EntryReservations& reserved) {
    if(!o.target && o.nextScan) return false;
    if(frame<o.nextScan) return true;
    o.nextScan=frame+def.interval;
    auto foot=static_cast<FootClass*>(o.unit);
    auto pos=foot->GetMapCoords();
    if(auto assigned=o.target) {
        if(pos!=o.lastCell) {o.lastCell=pos;o.lastMoved=frame;}
        int& allocated=reserved.at(assigned->UniqueID);
        int own=entrySpace(foot,assigned);
        if(enterTarget(o,def,assigned,true,allocated-own) && frame-o.lastMoved<180) return true;
        allocated-=own;
        return false;
    }
    if(!enterSource(foot,def.enterKinds)) return false;
    TechnoClass* best=nullptr;
    int bestDistance=INT_MAX;
    visitNearbyCells(o.center,o.radius,false,[&](TechnoClass* candidate) {
        if(candidate==best) return false;
        auto at=candidate->GetMapCoords();
        int dx=at.X-pos.X,dy=at.Y-pos.Y,distance=dx*dx+dy*dy;
        if(distance>bestDistance || (distance==bestDistance && best && candidate->UniqueID>=best->UniqueID)) return false;
        auto it=reserved.find(candidate->UniqueID);
        if(enterTarget(o,def,candidate,false,it==reserved.end()?0:it->second) && foot->IsInSameZoneAs(candidate)) {
            best=candidate;bestDistance=distance;
        }
        return false;
    });
    if(!best) return false;
    o.target=best;o.lastMoved=frame;o.lastCell=pos;
    reserved[best->UniqueID]+=entrySpace(foot,best);
    foot->SetDestination(best,true);
    auto building=abstract_cast<BuildingClass*>(best);
    if(building && building->Type->Bunker && foot->WhatAmI()==AbstractType::Unit)
        foot->SendCommand(RadioCommand::RequestLink,building);
    foot->QueueMission(building && foot->WhatAmI()==AbstractType::Infantry?Mission::Capture:Mission::Enter,true);
    return true;
}
