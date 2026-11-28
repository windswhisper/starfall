// Included inside ForceMove so jumps share the same movement and occupation lifecycle.
namespace {
CellClass* jumpCell(CellStruct at) {
    auto& map=MapClass::Instance;
    return at.X>=0 && at.Y>=0 && at.X<512 && at.Y<512 && map.IsWithinUsableArea(at,false)?map.TryGetCellAt(at):nullptr;
}
int ejectFlightFrames(int dx,int dy,double speed) {
    const double cells=std::hypot(double(dx),double(dy))/256.0;
    return std::max(1,static_cast<int>(std::ceil(cells*15.0/speed)));
}
int cellDistance(CellStruct a,CellStruct b) {
    int x=a.X-b.X,y=a.Y-b.Y;
    return x*x+y*y;
}
CellStruct jumpEnd(const Motion& m) {
    return CellClass::Coord2Cell({m.x+m.dx,m.y+m.dy,0});
}
bool booked(FootClass* foot,CellStruct at) {
    for(const auto& [id,m]:motions)
        if(id!=foot->UniqueID && m.landing && jumpEnd(m)==at) return true;
    for(const auto& [id,cell]:descending) if(id!=foot->UniqueID && cell==at) return true;
    return false;
}
bool jumpGround(FootClass* foot,CellClass* cell) {
    const bool bridge=cell->ContainsBridge();
    auto type=foot->GetTechnoType();
    auto land=bridge?LandType::Clear:cell->LandType;
    auto speed=type->SpeedType;
    return speed>=SpeedType::Foot && speed<=SpeedType::FloatBeach && speed!=SpeedType::Winged &&
        land>=LandType::Clear && static_cast<size_t>(land)<std::size(GroundType::Array) &&
        GroundType::Array[static_cast<int>(land)].Cost[static_cast<int>(speed)]>0 &&
        !HighPlatforms::landingRim(cell->MapCoords);
}
// 0 = empty, 1 = every occupant can be crushed, -1 = occupied or impassable.
int jumpOccupants(FootClass* foot,CellClass* cell,bool allowCrush) {
    bool bridge=cell->ContainsBridge();
    auto type=foot->GetTechnoType();
    if(!jumpGround(foot,cell) || cell->GetBuilding() || cell->GetTerrain(bridge) ||
       !cell->IsClearToMove(type->SpeedType,true,true,-1,type->MovementZone,-1,bridge)) return -1;
    bool victims=false,transient=false;
    for(auto obj=bridge?cell->AltObject:cell->FirstObject;obj;obj=obj->NextObject) {
        if(obj==foot || obj->InLimbo || !obj->IsAlive || obj->Health<=0 || !obj->IsOnMap) continue;
        auto other=abstract_cast<TechnoClass*>(obj);
        if(!other || other->Transporter || other->OnBridge!=bridge) continue;
        if(unreserved(obj) || obj->IsInAir()) {transient=true;continue;}
        if(!allowCrush || (obj->WhatAmI()!=AbstractType::Unit && obj->WhatAmI()!=AbstractType::Infantry) ||
           !other->Owner || other->Owner->IsNeutral() || foot->Owner->IsAlliedWith(other->Owner) ||
           !(type->Crusher || foot->HasAbility(Ability::Crusher)) || other->IsIronCurtained() ||
           other->Uncrushable || !obj->IsCrushable(foot)) return -1;
        victims=true;
    }
    return victims?(transient?-1:1):0;
}
bool clearJumpArc(CoordStruct from,CoordStruct to,int height) {
    const int steps=(std::max(std::abs(to.X-from.X),std::abs(to.Y-from.Y))+31)/32;
    for(int i=1;i<steps;++i) {
        double t=double(i)/steps;
        CoordStruct p{from.X+(to.X-from.X)*i/steps,from.Y+(to.Y-from.Y)*i/steps,0};
        auto cell=jumpCell(CellClass::Coord2Cell(p));
        if(!cell) return false;
        int floor=MapClass::Instance.GetCellFloorHeight(p);
        if(auto building=cell->GetBuilding())
            floor+=std::max(1,building->Type->Height)*Unsorted::LevelHeight;
        else if(cell->OverlayTypeIndex>=0 && cell->OverlayTypeIndex<OverlayTypeClass::Array.Count &&
                OverlayTypeClass::Array.Items[cell->OverlayTypeIndex]->Wall) floor+=Unsorted::LevelHeight;
        if((1-t)*from.Z+t*to.Z+4*t*(1-t)*height<floor) return false;
    }
    return true;
}
// SpawnParachuted checks actual occupation and the movement zone too. In
// particular, a vehicle can reserve a cell before it appears in its object list.
bool parachuteCell(FootClass* rider,CellClass* cell) {
    auto type=rider->GetTechnoType();
    bool bridge=cell->ContainsBridge();
    if(bridge && !(cell->Flags & CellFlags::Unknown_200)) return false;
    int zone=MapClass::Instance.GetMovementZoneType(cell->MapCoords,type->MovementZone,bridge);
    return cell->IsClearToMove(type->SpeedType,false,false,zone,type->MovementZone,-1,true);
}
// Flood only the clicked landing surface. A narrow strip of water, cliff or
// platform rim must not silently redirect the order to the opposite side.
std::optional<CellStruct> findJumpSlot(FootClass* foot,CellStruct click,const JumpSpec& spec,
    const std::vector<CellStruct>& claimed,const Landing* recovery,bool crushClick=true,std::optional<CoordStruct> launch={},int radius=2,int openHeight=0) {
    auto base=jumpCell(click);
    if(!base || (!recovery && !jumpGround(foot,base))) return {};
    const int level=recovery?recovery->level:base->Level;
    const bool bridge=recovery?recovery->bridge:base->ContainsBridge();
    const DWORD deck=recovery?recovery->deck:HighPlatforms::landingDeck(click);
    const auto from=launch.value_or(foot->Location);
    const auto origin=CellClass::Coord2Cell(from);
    if(!recovery && cellDistance(origin,click)>spec.range*spec.range) return {};
    std::vector<CellStruct> cells{click};
    for(size_t n=0;n<cells.size();++n) {
        const auto p=cells[n];
        for(const auto delta:{CellStruct{-1,0},CellStruct{1,0},CellStruct{0,-1},CellStruct{0,1}}) {
            CellStruct at{short(p.X+delta.X),short(p.Y+delta.Y)};
            if(cellDistance(at,click)>radius*radius || std::find(cells.begin(),cells.end(),at)!=cells.end()) continue;
            auto cell=jumpCell(at);
            if(cell && cell->Level==level && cell->ContainsBridge()==bridge &&
               HighPlatforms::landingDeck(at)==deck && jumpGround(foot,cell)) cells.push_back(at);
        }
    }
    std::sort(cells.begin(),cells.end(),[&](CellStruct a,CellStruct b){
        auto score=[&](CellStruct p){return std::tuple(cellDistance(p,click),cellDistance(p,origin),p.Y,p.X);};
        return score(a)<score(b);
    });
    for(auto at:cells) {
        auto cell=jumpCell(at);
        if(cell->Level!=level || cell->ContainsBridge()!=bridge || HighPlatforms::landingDeck(at)!=deck ||
           (!recovery && (at==origin || cellDistance(origin,at)>spec.range*spec.range)) ||
           booked(foot,at) || std::find(claimed.begin(),claimed.end(),at)!=claimed.end()) continue;
        if(jumpOccupants(foot,cell,crushClick && at==click && (!recovery || recovery->crush))<0) continue;
        if(openHeight && !parachuteCell(foot,cell)) continue;
        auto to=cell->GetCoordsWithBridge();
        to.Z+=openHeight;
        if(clearJumpArc(from,to,spec.height*256)) return at;
    }
    return {};
}
}
std::optional<CellStruct> jumpSlot(TechnoClass* unit,CellStruct click,const JumpSpec& spec,const std::vector<CellStruct>& claimed) {
    if(!unit || unit->WhatAmI()!=AbstractType::Unit || !unit->Owner || locked(unit) || !eligible(unit) ||
       unit->GetTechnoType()->JumpJet || unit->GetTechnoType()->SpeedType==SpeedType::Winged ||
       spec.range<1 || spec.range>128 || spec.height<1 || spec.height>128 || spec.duration<1 || spec.duration>3600) return {};
    return findJumpSlot(static_cast<FootClass*>(unit),click,spec,claimed,nullptr);
}
bool jump(TechnoClass* unit,CellStruct click,const JumpSpec& spec) {
    auto slot=jumpSlot(unit,click,spec);
    if(!slot) return false;
    auto cell=jumpCell(*slot);
    auto to=cell->GetCoordsWithBridge(),from=unit->Location;
    Spec move;
    move.dx=to.X-from.X;move.dy=to.Y-from.Y;move.duration=spec.duration;
    move.height=spec.height*256;move.tilt=false;move.affectsIronCurtain=true;
    if(!start(unit,move,from)) return false;
    motions.at(unit->UniqueID).landing=Landing{from.Z,to.Z,cell->Level,HighPlatforms::landingDeck(*slot),cell->ContainsBridge(),*slot==click,spec.land};
    playJumpEffect(spec.takeoff,from,unit->Owner);
    return true;
}
// Called after all movers advanced. Recompute eligibility before touching any
// victim. A late obstruction redirects the flight; ordinary traffic owns its cell.
bool landJump(FootClass* foot,const Motion& motion) {
    auto cell=foot->GetCell();
    auto goal=jumpEnd(motion);
    const auto planned=*motion.landing;
    bool matches=cell->MapCoords==goal && cell->Level==planned.level && cell->ContainsBridge()==planned.bridge && foot->OnBridge==planned.bridge &&
                 HighPlatforms::landingDeck(goal)==planned.deck;
    int occupants=matches?jumpOccupants(foot,cell,planned.crush):-1;
    if(occupants==1) {
        TargetClass handle(foot);
        foot->TryCrushCell(goal,false);
        foot=handle.As_Foot();
        if(!foot || !foot->IsAlive || foot->Health<=0 || foot->InLimbo) return false;
        occupants=jumpOccupants(foot,cell,false);
    }
    if(occupants==0) return true;
    auto it=motions.find(foot->UniqueID);
    if(it==motions.end()) return false;
    auto& next=it->second;
    auto fallback=planned;fallback.crush=false;
    JumpSpec shortHop{2,1,6};
    auto slot=findJumpSlot(foot,goal,shortHop,{},&fallback);
    if(!slot) {
        // Try the launch site when the landing surface disappears or fills up.
        const auto launch=CellClass::Coord2Cell({motion.x,motion.y,0});
        shortHop.range=static_cast<int>(std::ceil(std::sqrt(double(cellDistance(launch,foot->GetMapCoords())))))+2;
        slot=findJumpSlot(foot,launch,shortHop,{},nullptr,false);
    }
    if(!slot) { // Hold briefly and retry; never solidify inside another unit.
        next.elapsed=next.duration-1;
        next.landing->crush=false;
        return false;
    }
    auto dest=jumpCell(*slot);
    auto to=dest->GetCoordsWithBridge(),from=foot->Location;
    next.x=from.X;next.y=from.Y;next.dx=to.X-from.X;next.dy=to.Y-from.Y;
    next.elapsed=0;next.height=256;next.blocked=false;
    next.duration=shortHop.duration;
    next.landing=Landing{from.Z,to.Z,dest->Level,HighPlatforms::landingDeck(*slot),dest->ContainsBridge(),false,planned.effect};
    return false;
}
