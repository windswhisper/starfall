namespace ActiveSkills {
std::vector<int> held(TechnoClass* unit) {
    std::vector<int> result;
    if(specs.empty() || !alive(unit)) return result;
    auto ids=boundSkills(unit->GetTechnoType()->ID);extraSkills(unit,&ids);
    for(const auto& id:ids) {
        auto it=std::lower_bound(specs.begin(),specs.end(),id,[](const Spec& s,const string& key){return s.id<key;});
        if(it!=specs.end() && it->id==id && !suppressed(unit->UniqueID,id)) result.push_back(static_cast<int>(it-specs.begin()));
    }
    return result;
}
bool readyCaster(TechnoClass* unit,HouseClass* house) {
    return alive(unit) && unit->Owner==house && unit->IsOnMap && !unit->InLimbo && !unit->Transporter &&
           !ForceMove::locked(unit) && !empLocked(unit) && !unit->Deactivated && !unit->IsUnderEMP() && !unit->IsSinking && !unit->IsBeingWarpedOut();
}
int remaining(TechnoClass* unit,const Spec& spec) {
    auto owner=counts.find(unit->UniqueID);
    if(owner==counts.end()) return 0;
    auto it=owner->second.find(spec.id);
    if(it==owner->second.end()) return 0;
    unsigned now=Unsorted::CurrentFrame;
    return it->second.next>now?static_cast<int>(it->second.next-now):0;
}
bool playable(CellStruct p) {
    return p.X>=0 && p.Y>=0 && p.X<512 && p.Y<512 && MapClass::Instance.TryGetCellAt(p) && MapClass::Instance.IsWithinUsableArea(p,false);
}
bool aim(const Spec& skill,TechnoClass* caster,TechnoClass* target,CellStruct cursor,CellStruct& end) {
    if(!alive(caster) || !caster->Owner) return false;
    auto start=caster->GetMapCoords();
    if(skill.kind==Kind::None) {end=start;return true;}
    if(skill.kind==Kind::Unit) {
        if(!alive(target) || target->InLimbo || !target->IsOnMap || !target->Owner) return false;
        cursor=target->GetMapCoords();
        auto house=caster->Owner;
        bool allied=house->IsAlliedWith(target->Owner);
        string relation=house==target->Owner?"self":allied?"allies":target->Owner->IsNeutral()?"neutral":"enemies";
        if(!skill.houses.empty() && !includes(skill.houses,"all") && !includes(skill.houses,relation)) return false;
        if(house!=target->Owner && !allied) {
            if(target->Disguised && !target->IsClearlyVisibleTo(house)) return false;
            if(target->CloakState!=CloakState::Uncloaked) {
                auto cell=MapClass::Instance.TryGetCellAt(cursor);
                if(!cell || house->ArrayIndex<0 || house->ArrayIndex>=24 || !cell->Sensors_InclHouse(house->ArrayIndex)) return false;
            }
        }
    }
    if(!playable(cursor)) return false;
    int dx=cursor.X-start.X,dy=cursor.Y-start.Y;
    int squared=dx*dx+dy*dy;
    if(skill.kind!=Kind::Direction) {
        end=cursor;return !skill.range || squared<=skill.range*skill.range;
    }
    if(!squared) return false;
    double length=std::sqrt(static_cast<double>(squared));
    // Keep the endpoint inside the map at the edge, along the same ray.
    for(int d=skill.distance;d>0;--d) {
        end={static_cast<short>(start.X+std::lround(d*dx/length)),static_cast<short>(start.Y+std::lround(d*dy/length))};
        if(playable(end)) return true;
    }
    return false;
}
bool execute(int sender,int index,unsigned long casterId,unsigned long targetId,CellStruct cursor) {
    if(!ready) readSkills();
    if(index<0 || static_cast<size_t>(index)>=specs.size() || sender<0 || sender>=HouseClass::Array.Count) return false;
    auto house=HouseClass::Array.Items[sender];
    if(!house || house->Defeated || house->IsObserver() || !ScenarioClass::Instance) return false;
    Objects live;
    for(auto unit:TechnoClass::Array) if(alive(unit)) live.emplace(unit->UniqueID,unit);
    auto caster=findLive(live,casterId);
    const auto& spec=specs[index];
    if(!readyCaster(caster,house) || !ownsPassive(caster,spec.id) || remaining(caster,spec)>0) return false;
    auto target=spec.kind==Kind::Unit?findLive(live,targetId):nullptr;
    CellStruct end{};
    if(!aim(spec,caster,target,cursor,end)) return false;
    if(spec.interruptOrders) {
        if(auto foot=abstract_cast<FootClass*>(caster)) {
            foot->ClearMegaMissionData();
            foot->NavQueue.Count=0;
            foot->StopMoving();
            foot->SetDestination(nullptr,true);
        }
        caster->SetTarget(nullptr);
        caster->ForceMission(Mission::Stop);
    }
    auto& skill=skills.at(spec.id);
    auto e=eventSnapshot("oncast",caster,target,caster,target,0);
    e.hasImpact=true;e.impact=MapClass::Instance.GetCellAt(end)->GetCoordsWithBridge();
    e.castAim=cursor;
    auto prev=activeObjects;activeObjects=&live;
    bool success=runScript(skill.actions.front(),e,live,stackCount(casterId,spec.id));
    activeObjects=prev;
    if(success && findLive(live,casterId))
        counts[casterId][spec.id].next=static_cast<unsigned>(Unsorted::CurrentFrame)+skill.cooldown;
    return success;
}
}
