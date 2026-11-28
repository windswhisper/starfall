// Each grant remembers its own caster, attack target and expiry. Object pointers
// are caches only; savegames use scenario IDs and rebind once after loading.
namespace Taunt {
using Key=std::pair<string,DWORD>;
struct Binding {
    DWORD target=0;
    GrantTime time{};
    TechnoClass* object=nullptr;
};
struct Control {
    TechnoClass* unit=nullptr;
    DWORD current=0;
    std::map<Key,Binding> bindings;
};
std::map<DWORD,Control> controls;
bool rebind=false;

DWORD targetOf(const AbstractClass* unit) {
    if(!unit || controls.empty()) return 0;
    auto it=controls.find(unit->UniqueID);
    return it==controls.end()?0:it->second.current;
}
bool present(TechnoClass* unit) {
    return alive(unit) && unit->Owner && unit->IsOnMap && !unit->InLimbo && !unit->Transporter &&
        !unit->IsSinking && !unit->IsCrashing && !unit->IsBeingWarpedOut();
}
bool fixedPosition(TechnoClass* unit) {
    return unit->WhatAmI()==AbstractType::Building || deploymentOf(unit)==DeployPosture::Deployed;
}
bool canAttack(TechnoClass* unit,TechnoClass* target,bool requireRange=false) {
    if(!present(target) || unit==target || unit->Owner==target->Owner || unit->Owner->IsAlliedWith(target->Owner) ||
       !target->GetTechnoType()->LegalTarget) return false;
    if(target->Disguised && !target->IsClearlyVisibleTo(unit->Owner)) return false;
    if(target->CloakState!=CloakState::Uncloaked) {
        auto cell=target->GetCell();
        int index=unit->Owner->ArrayIndex;
        if(!cell || index<0 || index>=24 || !cell->Sensors_InclHouse(index)) return false;
    }
    int slot=unit->SelectWeapon(target);
    auto mount=slot>=0 && slot<TechnoTypeClass::MaxWeapons?unit->GetWeapon(slot):nullptr;
    auto weapon=mount?mount->WeaponType:nullptr;
    if(!weapon || !weapon->Projectile || !weapon->Warhead ||
       !(target->IsInAir()?weapon->Projectile->AA:weapon->Projectile->AG) ||
       !MapClass::GetTotalDamage(10000,weapon->Warhead,target->GetTechnoType()->Armor,0)) return false;
    if(requireRange) {
        if(auto rule=slotRule(unit->UniqueID,slot);rule && rule->disabled) return false;
        if(!unit->IsCloseEnough(target,slot)) return false;
    }
    auto error=unit->GetFireError(target,slot,true);
    return (!requireRange || error!=FireError::RANGE) && error!=FireError::ILLEGAL && error!=FireError::CANT;
}
void clearOrders(TechnoClass* unit) {
    unit->QueuedMission=unit->SuspendedMission=Mission::None;
    if(auto foot=abstract_cast<FootClass*>(unit)) {
        foot->ClearMegaMissionData();
        foot->NavQueue.Count=0;
        foot->SetDestination(nullptr,true);
        foot->StopMoving();
    }
}
void update(Control& control) {
    auto unit=control.unit;
    if(!present(unit)) {control.current=0;control.bindings.clear();return;}
    unsigned frame=Unsorted::CurrentFrame;
    const bool immobilized=ForceMove::locked(unit) || empLocked(unit) || unit->Deactivated || unit->IsUnderEMP();
    TechnoClass* choice=nullptr;
    int best=-1;
    for(auto it=control.bindings.begin();it!=control.bindings.end();) {
        auto& [key,binding]=*it;
        auto skill=skills.find(key.first);
        if(!binding.time.remaining(frame) || skill==skills.end() || !skill->second.taunt ||
           !present(binding.object) || (!immobilized && !canAttack(unit,binding.object))) {
            it=control.bindings.erase(it);continue;
        }
        ++it;
        if(suppressed(unit->UniqueID,key.first) ||
           !stateActive(unit,skill->second,stateOf(unit),UnitHealth::read(unit),frame)) continue;
        int priority=skill->second.tauntPriority;
        if(priority>best || (priority==best && binding.target==control.current)) {best=priority;choice=binding.object;}
    }
    auto previous=control.current;
    control.current=choice?choice->UniqueID:0;
    if(immobilized) return;
    if(!choice) {
        if(previous) {clearOrders(unit);unit->SetTarget(nullptr);unit->ForceMission(Mission::Guard);}
        return;
    }
    if(previous!=control.current) clearOrders(unit);
    if(unit->Target!=choice) unit->SetTarget(choice);
    if(unit->CurrentMission!=Mission::Attack) unit->ForceMission(Mission::Attack);
}
bool grant(TechnoClass* unit,const string& id,TechnoClass* caster,TechnoClass* aim,int duration) {
    auto& control=controls[unit->UniqueID];
    control.unit=unit;
    auto& binding=control.bindings[{id,caster?caster->UniqueID:aim->UniqueID}];
    unsigned frame=Unsorted::CurrentFrame;
    int left=binding.time.remaining(frame);
    if(binding.target==aim->UniqueID && (left<0 || (duration>0 && left>=duration))) return false;
    binding={aim->UniqueID,{frame,duration},aim};
    return true;
}
void remove(TechnoClass* unit,const string& id) {
    auto it=controls.find(unit->UniqueID);
    if(it==controls.end()) return;
    std::erase_if(it->second.bindings,[&](const auto& pair){return pair.first.first==id;});
    update(it->second);
    if(it->second.bindings.empty()) controls.erase(it);
}
void forget(DWORD id) {
    controls.erase(id);
    for(auto& [uid,control]:controls)
        std::erase_if(control.bindings,[&](const auto& entry){return entry.second.target==id;});
}
void tick() {
    if(rebind) {
        std::map<DWORD,TechnoClass*> objects;
        for(auto unit:TechnoClass::Array) if(unit) objects.emplace(unit->UniqueID,unit);
        for(auto& [id,control]:controls) {
            control.unit=objects[id];
            for(auto& [key,binding]:control.bindings) binding.object=objects[binding.target];
        }
        rebind=false;
    }
    for(auto it=controls.begin();it!=controls.end();) {
        update(it->second);
        if(it->second.bindings.empty()) it=controls.erase(it);else ++it;
    }
}
bool blocksOrder(EventClass& event) {
    if(controls.empty()) return false;
    switch(event.Type) {
        // Each of these native synchronized events starts its payload with the ordered object.
        case EventType::MegaMission: case EventType::MegaMissionF: case EventType::Idle:
        case EventType::Scatter: case EventType::Deploy: case EventType::Detonate:
        case EventType::Sell: case EventType::Repair: case EventType::Archive: case EventType::Primary:
        case EventType::PlanConnect: case EventType::PlanNodeDelete:
        case EventType::PowerOn: case EventType::PowerOff:
            return targetOf(event.MegaMission.Whom.As_Techno())!=0;
        default:return false;
    }
}
bool blocksMission(AbstractClass* unit,Mission requested) {
    return targetOf(unit) && requested!=Mission::Attack &&
        !(requested==Mission::Sleep && (ForceMove::locked(unit) || empLocked(unit)));
}
}
