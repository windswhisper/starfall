// Included inside ForceMove. The engine owns each unit throughout the flight.
std::map<DWORD,EjectOrder> ejectOrders;
std::map<DWORD,CellStruct> descending;
namespace {
FootClass* ejectObject(DWORD id) {
    TargetClass ref;
    ref.m_ID=id;ref.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    auto foot=ref.As_Foot();
    return foot && foot->IsAlive && foot->Health>0?foot:nullptr;
}
bool ejectCarrier(TechnoClass* carrier) {
    return carrier && carrier->WhatAmI()==AbstractType::Unit && carrier->Owner && eligible(carrier) &&
           !locked(carrier) && !carrier->Deactivated && !carrier->IsUnderEMP();
}
bool ejectable(FootClass* p,FootClass* carrier) {
    return p && p->WhatAmI()==AbstractType::Infantry && p->Transporter==carrier && p->InLimbo &&
           p->IsAlive && p->Health>0 && !locked(p);
}
std::optional<CellStruct> ejectSlot(FootClass* rider,CoordStruct origin,CellStruct target,const EjectSpec& spec) {
    JumpSpec arc{spec.range,spec.openHeight};
    return findJumpSlot(rider,target,arc,{},nullptr,false,origin,
                        std::min(spec.range,4),spec.openHeight*256);
}
void playEjectionCrawl(FootClass* foot) {
    auto infantry=abstract_cast<InfantryClass*>(foot);
    if(!infantry) return;
    const auto sequences=infantry->Type->Sequence;
    if(infantry->SequenceAnim!=Sequence::Crawl && sequences && sequences->GetSequence(Sequence::Crawl).CountFrames>0)
        infantry->PlayAnim(Sequence::Crawl,true,false);
}
Landing ejectionLanding(CellClass* cell,const CoordStruct& from,const EjectSpec& spec) {
    int height=spec.openHeight*256;
    return {from.Z,cell->GetCoordsWithBridge().Z+height,cell->Level,HighPlatforms::landingDeck(cell->MapCoords),
            cell->ContainsBridge(),false,{},height,spec.horizontalSpeed};
}
bool launchPassenger(FootClass* carrier,FootClass* rider,CellStruct slot,const EjectSpec& spec) {
    auto cell=jumpCell(slot);
    auto to=cell->GetCoordsWithBridge(),from=carrier->Location;
    const bool open=carrier->GetTechnoType()->OpenTopped;
    if(!carrier->Passengers.IndexOf(rider)) return false;
    // The native specific-passenger remover updates the list and gunner in place.
    // Its return register does not contain the removed passenger.
    carrier->RemovePassenger(rider);
    rider->NextObject=nullptr; // Cargo and map cells share this intrusive link.
    if(open) {
        carrier->ExitedOpenTopped(rider);
        LogicClass::Instance.RemoveObject(rider);
    }
    rider->Transporter=nullptr;
    auto& m=motions[rider->UniqueID];
    m.foot=rider;m.x=from.X;m.y=from.Y;m.dx=to.X-from.X;m.dy=to.Y-from.Y;
    m.height=spec.openHeight*256;m.duration=ejectFlightFrames(m.dx,m.dy,spec.horizontalSpeed);m.tilt=false;
    m.lastFrame=Unsorted::CurrentFrame;
    m.landing=ejectionLanding(cell,from,spec);
    // Set ghost occupation before unlimbo so the infantry cannot clear the carrier's cell bits.
    rider->OnBridge=carrier->OnBridge;
    ++Unsorted::ScenarioInit;
    const bool placed=rider->Unlimbo(from,DirType::North);
    --Unsorted::ScenarioInit;
    if(!placed) {
        motions.erase(rider->UniqueID);
        carrier->EnterAsPassenger(rider);rider->Transporter=carrier;
        if(open) carrier->EnteredOpenTopped(rider);
        return false;
    }
    m.x=rider->Location.X;m.y=rider->Location.Y;m.dx=to.X-m.x;m.dy=to.Y-m.y;
    m.duration=ejectFlightFrames(m.dx,m.dy,spec.horizontalSpeed);
    rider->ShouldEnterOccupiable=false;rider->ShouldGarrisonStructure=false;
    rider->QueueUpToEnter=nullptr;rider->FrozenStill=false;
    stopOrders(rider);
    playEjectionCrawl(rider);
    rider->LastMapCoords=rider->GetMapCoords();
    syncAirTracker(m);
    carrier->Owner->RecheckPower=true;carrier->MarkForRedraw();
    return true;
}
bool nextPassenger(FootClass* carrier,EjectOrder& order) {
    while(!order.passengers.empty()) {
        const auto id=order.passengers.back();order.passengers.pop_back();
        auto rider=carrier->Passengers.FirstPassenger;
        int remaining=carrier->Passengers.NumPassengers;
        while(rider && remaining-->0 && rider->UniqueID!=id) rider=static_cast<FootClass*>(rider->NextObject);
        if(!rider || rider->UniqueID!=id) continue;
        if(!ejectable(rider,carrier)) continue;
        auto slot=ejectSlot(rider,carrier->Location,order.target,order.spec);
        if(slot && launchPassenger(carrier,rider,*slot,order.spec)) return true;
    }
    return false;
}
}
bool ejectUnit(TechnoClass* unit,CellStruct target,const EjectSpec& spec) {
    if(!spec.valid() || !eligible(unit) || !unit->Owner || locked(unit) || unit->IsFallingDown ||
       unit->GetTechnoType()->JumpJet || unit->GetTechnoType()->SpeedType==SpeedType::Winged) return false;
    auto foot=static_cast<FootClass*>(unit);
    auto from=foot->Location;
    auto slot=ejectSlot(foot,from,target,spec);
    if(!slot) return false;
    auto cell=jumpCell(*slot);
    auto to=cell->GetCoordsWithBridge();
    Spec move;
    move.dx=to.X-from.X;move.dy=to.Y-from.Y;move.height=spec.openHeight*256;
    move.duration=ejectFlightFrames(move.dx,move.dy,spec.horizontalSpeed);
    move.tilt=false;move.affectsIronCurtain=true;
    if(!start(unit,move,from)) return false;
    motions.at(foot->UniqueID).landing=ejectionLanding(cell,from,spec);
    playEjectionCrawl(foot);
    return true;
}
bool canEject(TechnoClass* unit,CellStruct target,const EjectSpec& spec) {
    if(!ejectCarrier(unit) || !spec.valid() || ejectOrders.contains(unit->UniqueID)) return false;
    auto carrier=static_cast<FootClass*>(unit);
    int left=carrier->Passengers.NumPassengers;
    for(auto obj=carrier->Passengers.FirstPassenger;obj && left-->0;obj=static_cast<FootClass*>(obj->NextObject))
        if(ejectable(obj,carrier) && ejectSlot(obj,carrier->Location,target,spec)) return true;
    return false;
}
bool eject(TechnoClass* unit,CellStruct target,const EjectSpec& spec) {
    if(!ejectCarrier(unit) || !spec.valid() || ejectOrders.contains(unit->UniqueID)) return false;
    auto carrier=static_cast<FootClass*>(unit);
    EjectOrder order{target,spec,static_cast<DWORD>(Unsorted::CurrentFrame)+spec.interval,{}};
    int left=carrier->Passengers.NumPassengers;
    auto rider=carrier->Passengers.FirstPassenger;
    for(;rider && left>0;--left,rider=static_cast<FootClass*>(rider->NextObject))
        if(ejectable(rider,carrier)) order.passengers.push_back(rider->UniqueID);
    if(left || rider) return false;
    std::reverse(order.passengers.begin(),order.passengers.end());
    if(!nextPassenger(carrier,order)) return false;
    if(!order.passengers.empty()) ejectOrders.emplace(unit->UniqueID,std::move(order));
    return true;
}
void tickEjections(DWORD frame) {
    std::erase_if(descending,[](const auto& item) {
        auto p=ejectObject(item.first);
        // HasParachute can stay set after touchdown. Falling is the native descent state.
        return !p || p->InLimbo || !p->IsFallingDown;
    });
    for(auto it=ejectOrders.begin();it!=ejectOrders.end();) {
        if(frame<it->second.nextFrame) {++it;continue;}
        auto carrier=ejectObject(it->first);
        auto node=ejectOrders.extract(it++);
        auto& order=node.mapped();
        if(ejectCarrier(carrier) && nextPassenger(carrier,order) && !order.passengers.empty()) {
            order.nextFrame=frame+order.spec.interval;
            ejectOrders.insert(std::move(node));
        }
    }
}
void finishEjection(DWORD id,const Motion& motion) {
    auto foot=motion.foot;
    const auto& plan=*motion.landing;
    const auto goal=jumpEnd(motion);
    auto valid=[&](CellClass* cell) {
        return cell && !booked(foot,cell->MapCoords) && jumpOccupants(foot,cell,false)==0 && parachuteCell(foot,cell);
    };
    auto dest=jumpCell(goal);
    bool canOpen=valid(dest) && dest->Level==plan.level && dest->ContainsBridge()==plan.bridge &&
        HighPlatforms::landingDeck(goal)==plan.deck;
    if(!canOpen && !foot->InLimbo) {
        auto from=foot->Location;
        from.Z=motion.jumpZ();
        // Replan from the rider's altitude, not its ground-level cell registration.
        const JumpSpec divert{4,1};
        auto slot=findJumpSlot(foot,goal,divert,{},&plan,false,from,divert.range,plan.openHeight);
        if(slot && *slot!=goal) {
            dest=jumpCell(*slot);
            auto to=dest->GetCoordsWithBridge();
            auto& next=motions.at(id);
            next.x=from.X;next.y=from.Y;next.dx=to.X-from.X;next.dy=to.Y-from.Y;
            next.height=256;next.elapsed=0;next.blocked=false;
            next.duration=ejectFlightFrames(next.dx,next.dy,plan.ejectSpeed);
            next.landing=Landing{from.Z,to.Z+plan.openHeight,dest->Level,plan.deck,plan.bridge,false,{},plan.openHeight,plan.ejectSpeed};
            return;
        }
    }
    // Remove ghost occupation only after Limbo. While the engine places the
    // parachutist it must see the real infantry, without the synthetic lift.
    auto open=[&](CellClass* cell) {
        if(!foot->InLimbo && !foot->Limbo()) return false;
        auto node=motions.extract(id);
        removeTracker(node.mapped());
        foot->SetLastFlightMapCoords(CellStruct::Empty);
        auto pos=cell->GetCoordsWithBridge();
        pos.Z+=plan.openHeight;
        foot->OnBridge=cell->ContainsBridge();
        ++Unsorted::ScenarioInit;
        bool opened=foot->SpawnParachuted(pos);
        --Unsorted::ScenarioInit;
        if(opened) {
            foot->LastMapCoords=cell->MapCoords;
            descending[id]=cell->MapCoords;
            return true;
        }
        // Native placement sets the falling flag even on an early rejection.
        foot->IsFallingDown=false;
        motions.insert(std::move(node));
        return false;
    };
    if(canOpen && open(dest)) return;
    // An entire deck may have vanished, or native placement may have rejected
    // the chosen cell. Try other clear surfaces in stable, outward ring order.
    for(int radius=1;radius<512;++radius) {
        for(int dy=-radius;dy<=radius;++dy) {
            int y=goal.Y+dy;
            if(y<0 || y>=512) continue;
            int step=std::abs(dy)==radius?1:2*radius;
            for(int dx=-radius;dx<=radius;dx+=step) {
                auto cell=jumpCell({short(goal.X+dx),short(y)});
                if(valid(cell) && open(cell)) return;
            }
        }
    }
    auto& pending=motions.at(id);
    pending.elapsed=pending.duration-1;
    pending.lastFrame=Unsorted::CurrentFrame;
    pending.blocked=true;
}
