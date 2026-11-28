void scheduleExpiry(unsigned frame,int remaining) {
    if(remaining<=0) return;
    unsigned end=frame+static_cast<unsigned>(remaining);
    if(!expiryScheduled || static_cast<int>(end-nextExpiry)<0) nextExpiry=end;
    expiryScheduled=true;
}
void clearGrantProgress(TechnoClass* unit,const string& id) {
    // A native or researched copy keeps its existing count and clocks.
    if(!suppressed(unit->UniqueID,id)) {
        auto ids=unitSkills(unit);
        extraSkills(unit,&ids);
        if(includes(ids,id)) return;
    }
    forgetGain(unit->UniqueID,id);
    eraseStacks(unit->UniqueID,id);
    TimerKey key{unit->UniqueID,id};
    if(auto found=counts.find(key.first);found!=counts.end()) found->second.erase(id);
    periodic.erase(key);stateTimers.erase(key);nearbySamples.erase(key);activeAnimStates.erase(key);
}
// Compute reachability from independent sources. Multiple paths confer one ability.
void refreshLinkedPassives(TechnoClass* unit) {
    if(UnitMount::mounted(unit) || !alive(unit) || (!anyLinkedPassives && !linkedGrants.contains(unit->UniqueID))) return;
    const auto uid=unit->UniqueID;
    auto reachable=unitSkills(unit);
    directExtraSkills(unit,&reachable);
    std::erase_if(reachable,[&](const string& id){return directSuppressed(uid,id);});
    std::set<string> linked;
    for(size_t i=0;i<reachable.size();++i) {
        auto it=skills.find(reachable[i]);
        if(it==skills.end() || it->second.linkedPassives.empty()) continue;
        const auto& parent=it->second;
        if(unit->InLimbo || !unit->IsOnMap) {
            if(parent.nearby.range || parent.conditionFormula.expression.usesDistance()) continue;
            if(parent.stateDelayFrames) transportStateKeys.emplace(uid,parent.id);
        }
        if(!stateActive(unit,parent,stateOf(unit),UnitHealth::read(unit),Unsorted::CurrentFrame)) continue;
        for(const auto& id:parent.linkedPassives)
            if(linked.insert(id).second && !includes(reachable,id)) reachable.push_back(id);
    }
    auto old=linkedGrants.find(uid);
    if(old==linkedGrants.end() ? linked.empty() : old->second==linked) return;
    std::set<string> previous;
    if(old!=linkedGrants.end()) previous.swap(old->second);
    if(linked.empty()) linkedGrants.erase(uid);
    else linkedGrants[uid]=std::move(linked);
    gainInputs.erase(uid);
    if(anyAuras) discoverAuras=true;
    for(const auto& id:previous) if(!linkedPassive(uid,id)) clearGrantProgress(unit,id);
}
void refreshAllLinkedPassives() {
    if(anyLinkedPassives || !linkedGrants.empty())
        for(auto unit:TechnoClass::Array) refreshLinkedPassives(unit);
}
void refreshTransportGrants() {
    for(const auto& key:transportStateKeys) {
        if(auto timer=stateTimers.find(key);timer!=stateTimers.end()) timer->second.observed=false;
    }
    transportStateKeys.clear();
    if(!anyTransportPassives && transportGrants.empty()) return;
    decltype(transportGrants) next;
    for(auto carrier:UnitClass::Array) {
        if(!alive(carrier) || carrier->Passengers.NumPassengers<=0) continue;
        int remaining=carrier->Passengers.NumPassengers;
        for(ObjectClass* object=carrier->Passengers.FirstPassenger;object && remaining-->0;object=object->NextObject) {
            auto passenger=static_cast<FootClass*>(object);
            if(!alive(passenger) || passenger->Transporter!=carrier || !Mount::permits(carrier,passenger)) continue;
            auto ids=unitSkills(passenger);extraSkills(passenger,&ids);
            std::map<string,bool> provided;
            for(const auto& id:ids) {
                auto skill=skills.find(id);
                if(skill==skills.end() || skill->second.transportPassives.empty() || suppressed(passenger->UniqueID,id)) continue;
                transportStateKeys.emplace(passenger->UniqueID,id);
                if(!stateActive(passenger,skill->second,stateOf(passenger),UnitHealth::read(passenger),Unsorted::CurrentFrame)) continue;
                for(const auto& grant:skill->second.transportPassives) provided[grant]=provided[grant] || skill->second.transportStack;
            }
            for(const auto& [id,stacks]:provided) {
                auto& grant=next[carrier->UniqueID][id];
                ++grant.sources;if(stacks) ++grant.layers;
            }
        }
    }
    if(next==transportGrants) return;
    transportGrants.swap(next);
    for(auto carrier:UnitClass::Array) {
        if(!carrier) continue;
        auto old=next.find(carrier->UniqueID),current=transportGrants.find(carrier->UniqueID);
        if(old==next.end() && current==transportGrants.end()) continue;
        if(old!=next.end() && current!=transportGrants.end() && old->second==current->second) continue;
        gainInputs.erase(carrier->UniqueID);
        if(old!=next.end()) for(const auto& [id,grant]:old->second) clearGrantProgress(carrier,id);
    }
}
std::set<DWORD> auraTargets(TechnoClass* emitter,const AuraRule& rule) {
    std::set<DWORD> result;
    auto origin=emitter->GetCenterCoords();
    auto owner=emitter->Owner;
    auto visit=[&](TechnoClass* target) {
        if(!Mount::permits(target,emitter) || !alive(target) || !target->IsOnMap || target->InLimbo || !target->Owner ||
           (rule.range>0 && target==emitter && !rule.includeSelf)) return false;
        if(rule.range>0) {
            auto at=target->GetCenterCoords();
            int64_t dx=int64_t(at.X)-origin.X,dy=int64_t(at.Y)-origin.Y;
            int64_t radius=rule.range*256;
            if(dx*dx+dy*dy>radius*radius) return false;
        }
        const char* relation=target->Owner==owner?"self":owner->IsAlliedWith(target->Owner)?"allies":
            target->Owner->IsNeutral()?"neutral":"enemies";
        if((includes(rule.houses,"all") || includes(rule.houses,relation)) &&
            Research::matchesTarget(rule.types,lower(target->GetTechnoType()->ID),kind(target),groundVehicle(target)))
            result.insert(target->UniqueID);
        return false;
    };
    if(rule.range==-1) {
        for(auto target:TechnoClass::Array) visit(target);
    } else {
        // Cell rounding can shift the diagonal separation by up to sqrt(2) cells.
        visitNearbyCells(CellClass::Coord2Cell(origin),rule.range+2,true,visit);
        if(rule.includeSelf) visit(emitter);
    }
    return result;
}
void removeAuraTarget(DWORD target,const vector<string>& grants) {
    auto unit=auraGrants.find(target);
    if(unit==auraGrants.end()) return;
    for(const auto& id:grants) {
        auto it=unit->second.find(id);
        if(it!=unit->second.end() && --it->second==0) {
            unit->second.erase(it);auraLosses[target].insert(id);gainInputs.erase(target);
        }
    }
    if(unit->second.empty()) auraGrants.erase(unit);
}
void updateAuraCoverage(AuraSource& source,const vector<string>& grants,std::set<DWORD> targets) {
    const bool same=source.grants==grants;
    if(same && source.targets==targets) return;
    for(auto target:source.targets) if(!same || !targets.contains(target)) removeAuraTarget(target,source.grants);
    for(auto target:targets) if(!same || !source.targets.contains(target)) {
        auto& refs=auraGrants[target];
        for(const auto& id:grants) if(refs[id]++==0) gainInputs.erase(target);
    }
    source.grants=grants;
    source.targets=std::move(targets);
}
void flushAuraLosses() {
    if(auraLosses.empty()) return;
    for(auto unit:TechnoClass::Array) if(alive(unit)) {
        auto it=auraLosses.find(unit->UniqueID);
        if(it!=auraLosses.end()) for(const auto& id:it->second) clearGrantProgress(unit,id);
    }
    auraLosses.clear();
}
void stopAuraSources(DWORD emitter,const string* passive=nullptr) {
    for(auto it=auraSources.lower_bound({emitter,{}});it!=auraSources.end() && it->first.first==emitter;) {
        if(passive && it->first.second!=*passive) {++it;continue;}
        for(auto target:it->second.targets) removeAuraTarget(target,it->second.grants);
        it=auraSources.erase(it);
    }
}
void refreshAuras() {
    if(!anyAuras && auraSources.empty()) {flushAuraLosses();return;}
    const auto frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    const bool pulse=frame%auraInterval==0;
    if(pulse || discoverAuras) {
        discoverAuras=false;
        for(auto unit:TechnoClass::Array) {
            if(!alive(unit) || !unit->IsOnMap || unit->InLimbo || !unit->Owner) continue;
            auto ids=unitSkills(unit);extraSkills(unit,&ids);
            for(const auto& id:ids) {
                auto s=skills.find(id);
                if(s!=skills.end() && s->second.aura.range && !suppressed(unit->UniqueID,id))
                    auraSources[{unit->UniqueID,id}].unit=unit;
            }
        }
    }
    std::map<std::pair<int,string>,std::set<DWORD>> globalTargets;
    for(auto it=auraSources.begin();it!=auraSources.end();) {
        auto& source=it->second;
        auto unit=source.unit;
        const auto& id=it->first.second;
        auto skill=skills.find(id);
        bool held=false;
        if(alive(unit) && unit->Owner && unit->IsOnMap && !unit->InLimbo &&
           skill!=skills.end() && skill->second.aura.range && !suppressed(unit->UniqueID,id)) {
            auto ids=unitSkills(unit);extraSkills(unit,&ids);
            held=includes(ids,id);
        }
        if(!held) {
            updateAuraCoverage(source,{},{});it=auraSources.erase(it);continue;
        }
        const auto& rule=skill->second.aura;
        if(!stateActive(unit,skill->second,stateOf(unit),UnitHealth::read(unit),frame)) {
            if(!source.grants.empty()) updateAuraCoverage(source,{},{});
        } else if(pulse || source.owner!=unit->Owner->ArrayIndex || source.grants!=rule.passives) {
            if(rule.range==-1 && UnitMount::links.empty()) {
                // Identical global auras owned by the same house share one world scan per refresh.
                auto [pool,fresh]=globalTargets.try_emplace({unit->Owner->ArrayIndex,id});
                if(fresh) pool->second=auraTargets(unit,rule);
                auto targets=pool->second;
                if(!rule.includeSelf) targets.erase(unit->UniqueID);
                updateAuraCoverage(source,rule.passives,std::move(targets));
            } else updateAuraCoverage(source,rule.passives,auraTargets(unit,rule));
            source.owner=unit->Owner->ArrayIndex;
        }
        ++it;
    }
    // Resolve all source changes before clearing progress or observing gains:
    // crossing two overlapping auras must not re-trigger OnPassiveGained.
    flushAuraLosses();
}
void refreshSpawnGrants() {
    if(!anySpawnPassives && spawnGrants.empty()) return;
    decltype(spawnGrants) next;
    for(auto manager:SpawnManagerClass::Array) {
        auto parent=manager->Owner;
        if(!alive(parent) || !parent->IsOnMap || parent->InLimbo) continue;
        auto ids=unitSkills(parent);
        extraSkills(parent,&ids);
        vector<string> grants;
        for(const auto& id:ids) {
            auto it=skills.find(id);
            if(it==skills.end() || it->second.spawnPassives.empty() || suppressed(parent->UniqueID,id)) continue;
            if(!stateActive(parent,it->second,stateOf(parent),UnitHealth::read(parent),Unsorted::CurrentFrame)) continue;
            for(const auto& skill:it->second.spawnPassives) if(!includes(grants,skill)) grants.push_back(skill);
        }
        if(grants.empty()) continue;
        for(auto node:manager->SpawnedNodes) {
            auto child=node?node->Unit:nullptr;
            if(alive(child) && Mount::permits(child,parent) && child->SpawnOwner==parent && child->Owner==parent->Owner)
                next.emplace(child->UniqueID,SpawnGrant{parent->UniqueID,grants});
        }
    }
    if(next==spawnGrants) return;
    spawnGrants.swap(next);
    for(auto child:AircraftClass::Array) {
        auto old=next.find(child->UniqueID),current=spawnGrants.find(child->UniqueID);
        if(old==next.end() && current==spawnGrants.end()) continue;
        if(old!=next.end() && current!=spawnGrants.end() && old->second==current->second) continue;
        if(old!=next.end()) for(const auto& id:old->second.ids) clearGrantProgress(child,id);
        if(anyGainSkills) observeGains(child);
    }
}
void expireWarheadGrants() {
    unsigned frame=Unsorted::CurrentFrame;
    if(!rebindGrants && (!expiryScheduled || static_cast<int>(frame-nextExpiry)<0)) return;
    if(rebindGrants) {
        for(auto u:TechnoClass::Array) if(u) {
            auto it=warheadGrants.find(u->UniqueID);
            if(it!=warheadGrants.end()) it->second.unit=u;
        }
        rebindGrants=false;
    }
    expiryScheduled=false;
    for(auto group=warheadGrants.begin();group!=warheadGrants.end();) {
        auto& [unit,entries]=group->second;
        if(unit) for(auto it=entries.begin();it!=entries.end();) {
            int left=it->second.remaining(frame);
            if(left==0 || !skills.contains(it->first)) {
                auto expired=it++;
                string id=expired->first;
                entries.erase(expired);clearGrantProgress(unit,id);
            } else {scheduleExpiry(frame,left);++it;}
        }
        if(!unit || entries.empty()) group=warheadGrants.erase(group); else ++group;
    }
}
bool grantToUnit(TechnoClass* target,const PassiveGrant& rule,bool notifyGains=true,TechnoClass* source=nullptr,TechnoClass* tauntTarget=nullptr) {
    if(!Mount::permits(target,source)) return false;
    bool changed=false;
    unsigned frame=Unsorted::CurrentFrame;
    vector<string> held;
    if(rule.stackOnReapply) {
        held=unitSkills(target);
        extraSkills(target,&held); // Includes active temporary, research, aura and spawn grants.
    }
    auto& group=warheadGrants[target->UniqueID];
    group.unit=target;
    for(const auto& id:rule.ids) {
        auto definition=skills.find(id);
        if(definition!=skills.end() && definition->second.taunt) {
            auto aim=tauntTarget?tauntTarget:source;
            if(!Taunt::present(target) || !Taunt::canAttack(target,aim)) continue;
            changed|=Taunt::grant(target,id,source,aim,rule.duration);
        }
        if(rule.stackOnReapply && includes(held,id) && !suppressed(target->UniqueID,id)) {
            auto skill=skills.find(id);
            if(skill!=skills.end() && changeUnitStacks(target,id,1,skill->second.stackMax)) changed=true;
        }
        if(!group.skills.contains(id)) clearGrantProgress(target,id);
        auto [entry,fresh]=group.skills.try_emplace(id,GrantTime{frame,rule.duration});
        if(!fresh) {
            int left=entry->second.remaining(frame);
            if(left<0 || (rule.duration>0 && left>=rule.duration)) continue;
            if(!left) clearGrantProgress(target,id);
            entry->second={frame,rule.duration};
        }
        auto type=skills.find(id);
        if(type!=skills.end() && type->second.aura.range)
            auraSources[{target->UniqueID,id}].unit=target;
        changed=true;
        scheduleExpiry(frame,rule.duration);
    }
    if(group.skills.empty()) warheadGrants.erase(target->UniqueID);
    if(changed) refreshLinkedPassives(target);
    if(auto it=Taunt::controls.find(target->UniqueID);changed && it!=Taunt::controls.end()) Taunt::update(it->second);
    if(changed && notifyGains && anyGainSkills) observeGains(target);
    return changed;
}
void grantFromPreset(TechnoClass* unit,const GrantPreset& preset) {
    if(!alive(unit) || !unit->IsOnMap || unit->InLimbo || unit->IsSinking || unit->IsBeingWarpedOut()) return;
    if(!preset.targetTypes.empty() && std::find(preset.targetTypes.begin(),preset.targetTypes.end(),
        unit->GetTechnoType())==preset.targetTypes.end()) return;
    grantToUnit(unit,preset);
}
void grantFromTrigger(const GrantPreset& preset,int target,HouseClass* house,ObjectClass* object,TriggerClass* trigger) {
    if(target==-1) {
        if(object && (object->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None)
            grantFromPreset(static_cast<TechnoClass*>(object),preset);
        return;
    }
    if(target==-2) {
        if(!trigger) return;
        for(auto unit:TechnoClass::Array) if(unit && unit->AttachedTag) {
            for(auto linked=unit->AttachedTag->FirstTrigger;linked;linked=linked->NextTrigger) {
                if(linked->Type!=trigger->Type) continue;
                grantFromPreset(unit,preset);
                break;
            }
        }
        return;
    }
    if(target!=0) house=HouseClass::Index_IsMP(target)?HouseClass::FindByPlayerAt(target):nullptr;
    if(house) for(auto unit:TechnoClass::Array) if(unit && unit->Owner==house) grantFromPreset(unit,preset);
}
void tryGrantUnit(TechnoClass* unit,WarheadTypeClass* wh,const WarheadGrantRule& rule,HouseClass* source,TechnoClass* firer=nullptr) {
    if(!Mount::permits(unit,firer)) return;
    if(!alive(unit) || !unit->Owner || !unit->IsOnMap || unit->InLimbo || unit->IsSinking ||
       unit->IsIronCurtained() || unit->IsBeingWarpedOut()) return;
    if(!Research::matchesTarget(rule.types,lower(unit->GetTechnoType()->ID),kind(unit),groundVehicle(unit))) return;
    auto owner=unit->Owner;
    bool self=owner==source,ally=source && source->IsAlliedWith(owner);
    if(!warheadAllows(wh,source,owner)) return;
    const char* house=self?"self":ally?"allies":owner->IsNeutral()?"neutral":source?"enemies":"none";
    if(!includes(rule.houses,"all") && !includes(rule.houses,house)) return;
    if(!MapClass::GetTotalDamage(10000,wh,unit->GetTechnoType()->Armor,0)) return;
    if(rule.stackOnReapply && !reapplyImpacts.empty()) {
        auto& impact=reapplyImpacts.back();
        if(impact.wh==wh && !impact.recipients.insert(unit->UniqueID).second) return;
    }
    grantToUnit(unit,rule,true,firer);
}
void deliverWarheadGrants(WarheadTypeClass* wh,const CoordStruct& at,HouseClass* source,AbstractClass* aim,TechnoClass* firer=nullptr) {
    auto config=grantRules.find(wh);
    if(config==grantRules.end()) return;
    const auto& rule=config->second;
    auto& map=MapClass::Instance;
    auto center=CellClass::Coord2Cell(at);
    if(!map.CoordinatesLegal(center)) return;
    std::set<DWORD> visited;
    auto deliver=[&](TechnoClass* unit) {
        if(unit && visited.insert(unit->UniqueID).second) tryGrantUnit(unit,wh,rule,source,firer);
    };
    if(!(wh->CellSpread>0)) {
        if(aim && (aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None) {
            auto unit=static_cast<TechnoClass*>(aim);
            auto cell=map.TryGetCellAt(center);
            if(unit->GetCoords().DistanceFromSquared(at)<=64.0*64.0 ||
               (cell && unit->WhatAmI()==AbstractType::Building && cell->GetBuilding()==unit)) deliver(unit);
        }
        return;
    }
    const double radius=static_cast<double>(wh->CellSpread)*Unsorted::LeptonsPerCell;
    const int cells=static_cast<int>(std::min(512.0,std::ceil(static_cast<double>(wh->CellSpread))+1));
    for(int y=std::max(0,center.Y-cells);y<=std::min(511,center.Y+cells);++y)
        for(int x=std::max(0,center.X-cells);x<=std::min(511,center.X+cells);++x) {
            CellStruct pos{static_cast<short>(x),static_cast<short>(y)};
            auto cell=map.TryGetCellAt(pos);
            if(!cell) continue;
            // A large building can be hit on any occupied foundation cell.
            if(auto b=cell->GetBuilding(); b && !b->Type->InvisibleInGame &&
               cell->GetCenterCoords().DistanceFromSquared(at)<=radius*radius) deliver(b);
            for(auto head:{cell->FirstObject,cell->AltObject}) for(auto obj=head;obj;obj=obj->NextObject) {
                if((obj->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
                   obj->WhatAmI()!=AbstractType::Building && obj->GetCoords().DistanceFromSquared(at)<=radius*radius)
                    deliver(static_cast<TechnoClass*>(obj));
            }
            if(auto jj=cell->Jumpjet; jj && jj->Location.DistanceFromSquared(at)<=radius*radius) deliver(jj);
        }
    // Avoid borrowing the engine's shared air-target iterator during a detonation.
    for(auto air:AircraftClass::Array) if(air && air->Location.DistanceFromSquared(at)<=radius*radius) deliver(air);
}
