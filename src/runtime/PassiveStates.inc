DeployPosture deploymentOf(const TechnoClass* unit) {
    switch(unit->WhatAmI()) {
        case AbstractType::Infantry: {
            auto infantry=static_cast<const InfantryClass*>(unit);
            if(infantry->IsDeployed()) return DeployPosture::Deployed;
            return infantry->Type->Deployer?DeployPosture::Undeployed:DeployPosture::None;
        }
        case AbstractType::Unit: {
            auto vehicle=static_cast<const UnitClass*>(unit);
            if(vehicle->Deployed) return DeployPosture::Deployed;
            return vehicle->Type->IsSimpleDeployer?DeployPosture::Undeployed:DeployPosture::None;
        }
        default: return DeployPosture::None;
    }
}
int stateOf(TechnoClass* unit) {
    int bits=(static_cast<int>(unit->CloakState)&3)<<2;
    if(unit->Owner && unit->Owner->HasFullPower()) bits|=1;
    if(needMotion && (unit->AbstractFlags & AbstractFlags::Foot)!=AbstractFlags::None) {
        auto foot=static_cast<FootClass*>(unit);
        if(foot->Locomotor && foot->Locomotor->Is_Really_Moving_Now()) bits|=16;
    }
    if(needUnitPower && !unit->Deactivated && !unit->IsUnderEMP() && unit->IsPowerOnline()) bits|=2;
    return bits;
}
bool meetsState(const Skill& s,int bits,int hp,int maximum,int ammo,int passengers) {
    return !s.requiredMask || (bits!=unknownState && (bits&s.requiredMask)==s.requiredBits);
}
struct Applied {
    Factors base=identity,last=identity;
    Factors state=identity;
    Factors percent=defaultStatPercent;
    std::array<bool,3> active{};
};
std::map<DWORD,Applied> applied;
void readPercentLimits() {
    constexpr const char* section="Starfall.PassiveLimits";
    constexpr std::array keys={"Speed","Firepower","Armor","Scale","ProjectileScale","WeaponRange","FireRate"};
    for(size_t k=0;k<keys.size();++k) {
        const auto defaults=defaultPercentLimits[k];
        int limit=number(read(section,keys[k],std::to_string(defaults.max).c_str()),true);
        if(limit<1) {
            std::ofstream("Starfall.log",std::ios::app)<<"Starfall: ["<<section<<"] invalid "<<keys[k]<<", using "<<defaults.max<<"%\n";
            limit=defaults.max;
        }
        const string minKey=string(keys[k])+".Min";
        const int defaultMin=std::min(defaults.min,limit);
        int minimum=number(read(section,minKey.c_str(),std::to_string(defaults.min).c_str()),true);
        if(minimum<1 || minimum>limit) {
            std::ofstream("Starfall.log",std::ios::app)<<"Starfall: ["<<section<<"] invalid "<<minKey<<", using "<<defaultMin<<"%\n";
            minimum=defaultMin;
        }
        percentLimits[k]={minimum,limit};
    }
    VisualScale::minimum=percentLimits[scaleIndex].min;
    // Loaded games can predate the size cap or use a different ruleset.
    for(auto& [id,size]:VisualScale::base) size=percentLimits[scaleIndex].clamp(size);
    std::erase_if(VisualScale::base,[](const auto& entry){return entry.second==100;});
    for(auto& [id,size]:projectileBase) size=percentLimits[projectileScaleIndex].clamp(size);
    std::erase_if(projectileBase,[](const auto& entry){return entry.second==100;});
    for(auto& [id,size]:rangeBase) size=percentLimits[weaponRangeIndex].clamp(size);
    std::erase_if(rangeBase,[](const auto& entry){return entry.second==100;});
    for(auto& [id,percent]:fireRateBase) percent=percentLimits[fireRateIndex].clamp(percent);
    std::erase_if(fireRateBase,[](const auto& entry){return entry.second==100;});
    for(auto& [id,record]:applied) for(size_t k=0;k<record.percent.size();++k)
        if(record.percent[k]!=100) record.percent[k]=percentLimits[k].clamp(record.percent[k]);
}
std::array<double*,3> statFields(TechnoClass* unit) {
    auto speed=(unit->AbstractFlags & AbstractFlags::Foot)!=AbstractFlags::None ?
        &static_cast<FootClass*>(unit)->SpeedMultiplier : nullptr;
    return {speed,&unit->FirepowerMultiplier,&unit->ArmorMultiplier};
}
void updateStat(Applied& record,size_t k,double& value) {
    // An absolute engine write (e.g. a crate) becomes the new native baseline.
    if(!record.active[k] || value!=record.last[k]) record.base[k]=value;
    double factor=record.state[k]*(record.percent[k]/100.0);
    if(factor!=1.0 || record.active[k]) value=record.base[k]*factor;
    record.last[k]=value;
    record.active[k]=factor!=1.0;
}
struct StateTimer { unsigned start; bool observed; };
using TimerKey=std::pair<DWORD,string>;
std::map<TimerKey,StateTimer> stateTimers;
std::map<DWORD,DWORD> hitSequences;
std::map<TimerKey,DWORD> observedHits;
struct AttachedAnim { DWORD id; int slot=-1; bool seen=false; };
std::map<TimerKey,AttachedAnim> attachedAnims;
std::set<TimerKey> activeAnimStates;

AnimClass* playSkillAnim(AnimTypeClass* type,TechnoClass* unit,const CoordStruct& at,HouseClass* house,bool loop=false) {
    auto anim=GameCreate<AnimClass>(type,unit?unit->Location:at);
    if(anim) {
        anim->IsInert=true;
        if(unit) anim->SetOwnerObject(unit);
        anim->Owner=unit?unit->Owner:house;
        anim->RemainingIterations=loop?0xFF:1;
    }
    return anim;
}
// Native animation objects can expire independently. Validate their IDs against
// the live array, and rebuild the slot index only when the array has shifted.
struct AnimFrame {
    std::unordered_map<DWORD,int> slots;
    vector<AnimClass*> retire;
    AnimClass* resolve(AttachedAnim& record) {
        auto& array=AnimClass::Array;
        if(record.slot>=0 && record.slot<array.Count) {
            auto anim=array.Items[record.slot];
            if(anim && anim->UniqueID==record.id) return anim;
        }
        if(slots.empty()) for(int i=0;i<array.Count;++i)
            if(auto anim=array.Items[i]) slots.emplace(anim->UniqueID,i);
        auto found=slots.find(record.id);
        if(found==slots.end()) return nullptr;
        record.slot=found->second;
        return array.Items[record.slot];
    }
    void attach(TechnoClass* unit,const Skill& s) {
        auto [it,fresh]=attachedAnims.try_emplace({unit->UniqueID,s.id},AttachedAnim{});
        auto& record=it->second;
        auto anim=fresh?nullptr:resolve(record);
        if(anim && (anim->OwnerObject!=unit || anim->Type!=s.anim || anim->TimeToDie)) {
            retire.push_back(anim); anim=nullptr;
        }
        if(!anim) {
            anim=playSkillAnim(s.anim,unit,unit->Location,unit->Owner,true);
            if(!anim) {attachedAnims.erase(it);return;}
            record.id=anim->UniqueID;
            record.slot=AnimClass::Array.Count-1;
        }
        anim->Owner=unit->Owner;
        VisualScale::attachedAnims[anim->UniqueID]=unit->UniqueID;
        record.seen=true;
    }
    void finish() {
        std::erase_if(attachedAnims,[&](auto& item) {
            if(item.second.seen) return false;
            auto record=item.second;
            VisualScale::attachedAnims.erase(record.id);
            if(auto anim=resolve(record)) retire.push_back(anim);
            return true;
        });
        for(auto anim:retire) anim->UnInit();
    }
};
// State rules are derived from the unit every frame. Never change a shared type's weapon array.
std::map<DWORD,WeaponRules> weaponRules;
struct VolleyWeapon { int slot; WeaponTypeClass* replacement; unsigned frame; };
std::map<DWORD,VolleyWeapon> volleyWeapons;
struct MountCopy { WeaponStruct original,visible; };
std::map<std::pair<DWORD,int>,MountCopy> mountCopies;
struct BuildingLookup { DWORD id; int slot; };
std::map<DWORD,BuildingLookup> buildingLookups;
const SlotRule* slotRule(DWORD id,int slot) {
    if(slot<0 || slot>1 || weaponRules.empty()) return nullptr;
    auto it=weaponRules.find(id);
    return it==weaponRules.end()?nullptr:&it->second[slot];
}
void applyMount(REGISTERS* R,DWORD id,int slot) {
    const auto rule=slotRule(id,slot);
    auto volley=volleyWeapons.find(id);
    auto replacement=volley!=volleyWeapons.end() && volley->second.slot==slot && volley->second.replacement?
        volley->second.replacement:rule?rule->replacement:nullptr;
    auto native=R->EAX<WeaponStruct*>();
    if(!replacement || !native) return;
    auto& copy=mountCopies[{id,slot}];
    // Ordinary buildings delegate to the Techno getter and reach both hooks.
    if(native!=&copy.visible) copy.original=*native;
    copy.visible=copy.original;
    copy.visible.WeaponType=replacement;
    R->EAX(&copy.visible);
}
void updateSlots(DWORD id,const WeaponRules& next) {
    bool active=false;
    for(int slot=0;slot<2;++slot) {
        active|=next[slot].disabled || next[slot].replacement;
        auto existing=mountCopies.find({id,slot});
        if(existing!=mountCopies.end())
            existing->second.visible.WeaponType=next[slot].replacement?next[slot].replacement:existing->second.original.WeaponType;
    }
    if(active) weaponRules[id]=next;
    else weaponRules.erase(id);
}
struct NearbySample {unsigned next; int owner; bool matches,seen;};
std::map<TimerKey,NearbySample> nearbySamples;
template<class Visitor>
bool visitNearbyCells(CellStruct center,int r,bool includeAir,Visitor&& visit) {
    auto list=[&](ObjectClass* object){
        for(;object;object=object->NextObject)
            if((object->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None && visit(static_cast<TechnoClass*>(object))) return true;
        return false;
    };
    for(int y=-r;y<=r;++y) for(int x=-r;x<=r;++x) {
        if(x*x+y*y>r*r) continue;
        CellStruct pos{static_cast<short>(center.X+x),static_cast<short>(center.Y+y)};
        auto cell=MapClass::Instance.TryGetCellAt(pos);
        if(cell && (list(cell->FirstObject) || list(cell->AltObject))) return true;
    }
    // Aircraft and jumpjets can be absent from the ground object chains.
    if(auto cell=MapClass::Instance.TryGetCellAt(center);cell && includeAir) {
        auto& tracker=AircraftTrackerClass::Instance;
        tracker.FillCurrentVector(cell,r);
        while(auto air=tracker.Get()) {
            auto pos=air->GetMapCoords(); int x=pos.X-center.X,y=pos.Y-center.Y;
            if(x*x+y*y<=r*r && visit(air)) return true;
        }
    }
    return false;
}
bool scanNearby(TechnoClass* unit,const NearbyRule& rule) {
    auto owner=unit->Owner;
    if(!owner) return false;
    auto center=unit->GetMapCoords(); const int r=rule.range;
    int count=0; std::set<DWORD> seen;
    // For an upper bound the first excess match decides immediately; for a
    // lower bound alone the last required match decides immediately.
    auto visit=[&](TechnoClass* candidate) {
        if(candidate==unit || !alive(candidate) || !candidate->IsOnMap || candidate->InLimbo || !candidate->Owner) return false;
        auto other=candidate->Owner;
        bool allied=owner->IsAlliedWith(other);
        const char* house=other==owner?"self":allied?"allies":other->IsNeutral()?"neutral":"enemies";
        if(!includes(rule.houses,"all") && !includes(rule.houses,house)) return false;
        if(!includes(rule.kinds,"all") && !includes(rule.kinds,kind(candidate))) return false;
        if(!Research::matchesTarget(rule.types,lower(candidate->GetTechnoType()->ID),kind(candidate),groundVehicle(candidate))) return false;
        if(other!=owner && !allied) {
            if(candidate->CloakState!=CloakState::Uncloaked) {
                const auto cell=MapClass::Instance.TryGetCellAt(candidate->GetMapCoords());
                if(!cell || owner->ArrayIndex<0 || owner->ArrayIndex>=24 || !cell->Sensors_InclHouse(owner->ArrayIndex)) return false;
            }
            if(candidate->Disguised && !candidate->IsClearlyVisibleTo(owner)) return false;
        }
        if(!seen.insert(candidate->UniqueID).second) return false;
        ++count;return count>rule.most || (rule.most==INT_MAX && count>=rule.least);
    };
    if(visitNearbyCells(center,r,!includes(rule.kinds,"building") || rule.kinds.size()>1,visit))
        return count<=rule.most;
    return count>=rule.least && count<=rule.most;
}
bool nearbyState(TechnoClass* unit,const Skill& s,unsigned frame) {
    const auto& rule=s.nearby;
    if(!rule.range) return true;
    int owner=unit->Owner?unit->Owner->ArrayIndex:-1;
    unsigned offset=unit->UniqueID%static_cast<unsigned>(rule.interval);
    NearbySample initial{frame+offset,owner,false,true};
    auto& sample=nearbySamples.try_emplace({unit->UniqueID,s.id},initial).first->second;
    if(sample.owner!=owner) sample=initial;
    sample.seen=true;
    if(static_cast<int>(frame-sample.next)>=0) {
        sample.matches=scanNearby(unit,rule);sample.next=frame+rule.interval;
    }
    return sample.matches;
}
double distanceCells(const CoordStruct& a,const CoordStruct& b);
std::optional<double> stateFormulaValue(const ConfigFormula& formula,TechnoClass* unit,AbstractClass* aim,
                                        UnitHealth::Values hp,const string& skillId,bool displayOnly=false) {
    auto type=unit->GetTechnoType();
    auto target=aim && (aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
    std::optional<double> distance;
    std::optional<HealthFormula::Values> values;
    if(aim && formula.expression.usesDistance())
        distance=distanceCells(unit->GetCenterCoords(),target?target->GetCenterCoords():aim->GetCoords());
    if(target && formula.expression.usesTargetValues()) {
        auto targetType=target->GetTechnoType();
        auto health=displayOnly?UnitHealth::snapshot(target):UnitHealth::read(target,targetType->Strength);
        values=HealthFormula::Values{health.current,health.maximum,targetType->Cost};
    }
    bool missing=false;
    HealthFormula::LiveState live{unit->Veterancy.IsElite()?2:unit->Veterancy.IsVeteran()?1:0};
    if(type->IsGattling && type->WeaponStages>0) {
        live.maxGattlingStage=type->WeaponStages;
        live.gattlingStage=1+std::clamp(unit->CurrentGattlingStage,0,type->WeaponStages-1);
    }
    if(unit->Owner) {live.powerOutput=unit->Owner->PowerOutput;live.powerDrain=unit->Owner->PowerDrain;}
    auto result=formula.expression.evaluate(hp.current,hp.maximum,0,distance,heldUnitStacks(unit,skillId),
        values,&missing,type->Cost,type->Ammo<0?-1:unit->Ammo,unit->Passengers.NumPassengers,std::nullopt,std::nullopt,live,unit->Owner?unit->Owner->ArrayIndex:-1,Research::counter);
    if(!result && !missing) formula.report();
    return result;
}
bool stateActive(TechnoClass* unit,const Skill& s,int bits,UnitHealth::Values hp,unsigned frame) {
    auto type=unit->GetTechnoType();
    if(!meetsState(s,bits,hp.current,hp.maximum,type->Ammo<0?-1:unit->Ammo,unit->Passengers.NumPassengers)) return false;
    if(!s.conditionFormula.expression.empty()) {
        if(stateFormulaValue(s.conditionFormula,unit,unit->Target,hp,s.id).value_or(0)==0) return false;
    }
    if(s.deployPosture!=DeployPosture::None && deploymentOf(unit)!=s.deployPosture) return false;
    if(!nearbyState(unit,s,frame)) return false;
    if(!s.stateDelayFrames) return true;
    auto& timer=stateTimers.try_emplace({unit->UniqueID,s.id},StateTimer{frame,true}).first->second;
    timer.observed=true;
    return frame-timer.start>=s.stateDelayFrames;
}
std::set<DWORD> mirageUnits,immuneUnits,mindImmuneUnits,empUnits,cloakableUnits;
bool empLocked(const AbstractClass* unit) {return unit && empUnits.contains(unit->UniqueID);}
// Derived from current skills each frame. Rendering never changes game state.
struct PassiveTint {
    ColorStruct color{};
    int strength=0;
    bool operator==(const PassiveTint&) const = default;
};
std::map<DWORD,PassiveTint> passiveTints;
// Only the simulation evaluates state conditions (nearby sampling and timers).
// Hover rendering formats the active templates for the one unit under the cursor.
std::map<DWORD,vector<const Skill*>> activeTooltips;
const PassiveTint* visibleTint(TechnoClass* unit) {
    if(passiveTints.empty()) return nullptr;
    auto it=passiveTints.find(unit->UniqueID);
    if(it==passiveTints.end() || !unit->IsOnMap || unit->InLimbo || unit->CloakState!=CloakState::Uncloaked ||
       unit->IsIronCurtained() || (unit->Disguised && (!HouseClass::CurrentPlayer || !unit->IsClearlyVisibleTo(HouseClass::CurrentPlayer))))
        return nullptr;
    return &it->second;
}
// The body blitters read the selected shade from this table after lighting.
// Blend each shade once; the original house palette remains available to other draws.
struct BodyPalette {
    std::unique_ptr<ConvertClass,GameDeleter> drawer;
    vector<WORD> sourceColors;
    int checkedFrame=-1;
};
using BodyPaletteKey=std::tuple<ConvertClass*,int,int>;
std::map<BodyPaletteKey,BodyPalette> bodyPalettes;

WORD mixBodyColor(WORD source,WORD target,int strength) {
    int result=0;
    for(const auto [shift,mask]:{std::pair{Drawing::RedShiftLeft,255>>Drawing::RedShiftRight},
            std::pair{Drawing::GreenShiftLeft,255>>Drawing::GreenShiftRight},
            std::pair{Drawing::BlueShiftLeft,255>>Drawing::BlueShiftRight}}) {
        const int from=(source>>shift)&mask, to=(target>>shift)&mask;
        result|=((from*(100-strength)+to*strength+50)/100)<<shift;
    }
    return static_cast<WORD>(result);
}

ConvertClass* bodyPalette(TechnoClass* unit,ConvertClass* source,int nativeColor) {
    if(nativeColor || passiveTints.empty()) return source;
    const auto tint=visibleTint(unit);
    if(!tint || !source || source->BytesPerPixel!=2 || !source->FullColorData || source->ShadeCount<=0) return source;
    const auto target=static_cast<WORD>(Drawing::RGB_To_Int(tint->color));
    const BodyPaletteKey key{source,target,tint->strength};
    if(bodyPalettes.size()>=32 && !bodyPalettes.contains(key)) {
        auto oldest=std::min_element(bodyPalettes.begin(),bodyPalettes.end(),[](const auto& a,const auto& b){
            return a.second.checkedFrame<b.second.checkedFrame;
        });
        bodyPalettes.erase(oldest);
    }
    auto& cache=bodyPalettes[key];
    const bool create=!cache.drawer || cache.drawer->ShadeCount!=source->ShadeCount;
    if(create) {
        static const BytePalette blank{};
        auto surface=DSurface::Primary;
        if(!surface || surface->GetBytesPerPixel()!=source->BytesPerPixel) return source;
        cache.drawer.reset(GameCreate<ConvertClass>(blank,blank,surface,source->ShadeCount,false));
        if(!cache.drawer || cache.drawer->BytesPerPixel!=2 || cache.drawer->ShadeCount!=source->ShadeCount || !cache.drawer->FullColorData) {
            cache.drawer.reset();
            return source;
        }
    }
    const int frame=Unsorted::CurrentFrame;
    if(create || cache.checkedFrame!=frame) {
        const size_t count=static_cast<size_t>(source->ShadeCount)*256;
        const auto first=static_cast<const WORD*>(source->FullColorData);
        auto dest=static_cast<WORD*>(cache.drawer->FullColorData);
        if(tint->strength==100) {
            if(create) std::fill_n(dest,count,target);
        } else if(create || !std::equal(first,first+count,cache.sourceColors.begin())) {
            cache.sourceColors.assign(first,first+count);
            std::transform(first,first+count,dest,[&](WORD value){return mixBodyColor(value,target,tint->strength);});
        }
        cache.checkedFrame=frame;
    }
    cache.drawer->CurrentZRemap=source->CurrentZRemap;
    return cache.drawer.get();
}
double distanceCells(const CoordStruct& a,const CoordStruct& b);
struct BuildingPowerState {
    HouseClass* owner;
    double multiplier;
    bool operator==(const BuildingPowerState&) const = default;
};
std::map<DWORD,BuildingPowerState> buildingPower;
void updateBuildingPower(BuildingClass* building,double multiplier) {
    auto id=building->UniqueID;
    auto old=buildingPower.find(id);
    BuildingPowerState next{building->Owner,multiplier};
    if(old!=buildingPower.end()) {
        if(old->second==next) return;
        if(auto owner=old->second.owner) owner->RecheckPower=true;
        buildingPower.erase(old);
    }
    if(next.owner && multiplier!=1) {
        buildingPower.emplace(id,next);
        next.owner->RecheckPower=true;
    }
}
int powerContribution(BuildingClass* building,int base,int accumulated) {
    if(base<=0 || buildingPower.empty()) return base;
    auto it=buildingPower.find(building->UniqueID);
    double factor=1;
    if(it!=buildingPower.end() && it->second.owner==building->Owner)
        factor=it->second.multiplier;
    const int room=INT_MAX-std::max(0,accumulated);
    return static_cast<int>(std::min<double>(room,std::round(base*factor)));
}
// A mounted unit reads effective state conditions and formula inputs from the
// actual holder. Copying passive ownership would run that holder's events twice.
size_t appendMountStates(TechnoClass* child,vector<string>& ids,TechnoClass*& parent) {
    auto ownCount=ids.size();
    auto link=UnitMount::links.find(child->UniqueID);
    if(link==UnitMount::links.end() || !alive(link->second.parent)) return ownCount;
    parent=link->second.parent;
    if((parent->InLimbo || !parent->IsOnMap) && !parent->Transporter) return ownCount;
    auto held=unitSkills(parent);extraSkills(parent,&held);
    for(const auto& id:held) {
        auto skill=skills.find(id);
        if(skill==skills.end() || (!skill->second.tint && skill->second.trigger!="whilestate") ||
           includes(ids,id) || includes(link->second.excluded,id) || suppressed(parent->UniqueID,id)) continue;
        ids.push_back(id);
    }
    return ownCount;
}
void tickStates() {
    ProjectileShield::active.clear();
    if(UnitMount::links.empty() && buildingPower.empty() && !anyStateSkills && !anyPassiveVisuals && attachedAnims.empty() && activeAnimStates.empty() && applied.empty() && stateTimers.empty() && mirageUnits.empty() && immuneUnits.empty() && mindImmuneUnits.empty() && empUnits.empty() && cloakableUnits.empty()) return;
    for(auto& entry:activeTooltips) entry.second.clear();
    AnimFrame animFrame;
    for(auto& [key,anim]:attachedAnims) anim.seen=false;
    std::set<TimerKey> nextAnimStates;
    std::set<DWORD> nextMirage,nextImmune,nextMindImmune,nextEmp,nextCloakable;
    vector<TargetClass> empCrashes;
    const unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    for(auto& [key,sample]:nearbySamples) sample.seen=false;
    for(auto& [key,timer]:stateTimers) if(!transportStateKeys.contains(key)) timer.observed=false;
    for(auto* unit:TechnoClass::Array) {
        if(!unit) continue;
        reconcileUnitStacks(unit);
        auto old=applied.find(unit->UniqueID);
        Factors desired=identity;
        double scaleFactor=1.0,projectileFactor=1.0,rangeFactor=1.0,fireRateFactor=1.0;
        double powerFactor=1;
        double experienceFactor=1;
        bool berserk=false,mirage=false,immune=false,mindImmune=false,emp=false,cloakable=false;
        PassiveTint tint{};
        std::string_view tintId;
        WeaponRules nextSlots{};
        const bool onMap=unit->IsOnMap && !unit->InLimbo;
        const bool passengerOnly=!onMap && (BetterGarrison::isOccupant(unit) ||
            (alive(unit->Transporter) && unit->Transporter->WhatAmI()==AbstractType::Unit));
        bool rider=UnitMount::mounted(unit);
        if(alive(unit) && (onMap || passengerOnly || rider)) {
            auto type=unit->GetTechnoType();
            const auto& native=unitSkills(unit);
            if(!rider && native.empty() && old==applied.end() && !weaponRules.contains(unit->UniqueID) && !VisualScale::state.contains(unit->UniqueID) && !projectileState.contains(unit->UniqueID) && !rangeState.contains(unit->UniqueID) && !fireRateState.contains(unit->UniqueID) && !experienceState.contains(unit->UniqueID) && !buildingPower.contains(unit->UniqueID) && !mirageUnits.contains(unit->UniqueID) && !empUnits.contains(unit->UniqueID) && !passiveTints.contains(unit->UniqueID) && !extraSkills(unit)) continue;
            auto ids=native;
            extraSkills(unit,&ids);
            int bits=stateOf(unit);
            auto hp=UnitHealth::read(unit,type->Strength);
            TechnoClass* parent=nullptr;
            const auto ownCount=appendMountStates(unit,ids,parent);
            for(size_t n=0;n<ids.size();++n) {
                const auto& id=ids[n];
                bool inherited=n>=ownCount;
                auto source=inherited?parent:unit;
                auto sourceHP=inherited?UnitHealth::read(source):hp;
                bool riding=passengerOnly || (inherited && source->Transporter);
                auto it=skills.find(id);
                if(it==skills.end()) continue;
                auto& s=it->second;
                if(suppressed(source->UniqueID,id)) continue;
                if(riding && (!s.passengerCombat || s.nearby.range || s.conditionFormula.expression.usesDistance())) continue;
                bool active=(s.trigger=="whilestate" || s.tint || (s.anim && !s.animAlways)) && stateActive(source,s,inherited?stateOf(source):bits,sourceHP,frame);
                if(onMap && active && s.tint && s.tintStrength>0 &&
                   (s.tintStrength>tint.strength || (s.tintStrength==tint.strength && id<tintId))) {
                    tint={*s.tint,s.tintStrength};
                    tintId=id;
                }
                if(!inherited && onMap && s.anim && (s.animAlways || active) && unit->CloakState==CloakState::Uncloaked) {
                    bool cooling=false;
                    if(s.animWhileReady) if(auto holder=counts.find(unit->UniqueID);holder!=counts.end()) {
                        auto progress=holder->second.find(id);
                        cooling=progress!=holder->second.end() && frame<progress->second.next;
                    }
                    if(!cooling) animFrame.attach(unit,s);
                }
                if(s.trigger!="whilestate" || !active) continue;
                if(onMap && !inherited && !s.tooltipText.empty()) activeTooltips[unit->UniqueID].push_back(&s);
                if(!inherited && onMap && s.triggerAnim) {
                    TimerKey key{unit->UniqueID,id};
                    if(!activeAnimStates.contains(key)) {
                        AbstractClass* aim=s.animTarget=="self"?unit:unit->Target;
                        if(aim) {
                            auto target=(aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
                            playSkillAnim(s.triggerAnim,target,aim->GetCoords(),unit->Owner);
                        }
                    }
                    nextAnimStates.insert(std::move(key));
                }
                auto multipliers=s.multipliers;
                for(const auto& [index,formula]:s.stateFormulas) {
                    if(riding && (!passengerCombatMultiplier(index) || formula.expression.usesDistance())) continue;
                    auto value=stateFormulaValue(formula,source,source->Target,sourceHP,id);
                    if(value && validStateMultiplier(index,*value)) multipliers[index]=*value;
                    else if(value) formula.report();
                }
                rangeFactor=std::clamp(rangeFactor*multipliers[3],0.0001,10000.0);
                fireRateFactor=std::clamp(fireRateFactor*multipliers[4],0.0001,10000.0);
                experienceFactor=std::min(10000.0,experienceFactor*multipliers[experienceMultiplierIndex]);
                desired[1]=std::clamp(desired[1]*multipliers[1],0.0001,10000.0);
                if(riding) continue;
                if(onMap && !inherited && s.shield.radius>0)
                    ProjectileShield::active.push_back({unit,s.shield});
                scaleFactor*=s.visualScale/100.0;
                projectileFactor*=s.projectileScale/100.0;
                if(!inherited) powerFactor=std::min(10000.0,powerFactor*multipliers[powerMultiplierIndex]);
                emp|=s.emp;
                berserk|=s.berserk;
                mirage|=!inherited && s.mirage;
                immune|=s.immune;
                mindImmune|=s.mindImmune;
                cloakable|=!inherited && s.cloakable;
                for(int slot=0;slot<2;++slot) {
                    nextSlots[slot].disabled|=s.slots[slot].disabled;
                    if(!inherited && s.slots[slot].replacement) nextSlots[slot].replacement=s.slots[slot].replacement;
                }
                if(!inherited) desired[0]=std::clamp(desired[0]*multipliers[0],0.0001,10000.0);
                desired[2]=std::clamp(desired[2]*multipliers[2],0.0001,10000.0);
            }
        }
        if(tint!=PassiveTint{}) {
            auto [it,added]=passiveTints.try_emplace(unit->UniqueID,tint);
            if(added || it->second!=tint) {it->second=tint;unit->MarkForRedraw();}
        } else if(passiveTints.erase(unit->UniqueID)) unit->MarkForRedraw();
        if(emp && (unit->WhatAmI()==AbstractType::Unit || unit->WhatAmI()==AbstractType::Aircraft)) {
            nextEmp.insert(unit->UniqueID);
            if(!empUnits.contains(unit->UniqueID)) {
                if(!rider && unit->IsInAir() && !ForceMove::locked(unit)) empCrashes.emplace_back(unit);
                else {
                    auto foot=static_cast<FootClass*>(unit);
                    if(!ForceMove::interruptTravel(foot)) ForceMove::stopOrders(foot);
                }
            }
        }
        if(alive(unit) && !emp && empUnits.contains(unit->UniqueID) &&
           (unit->WhatAmI()==AbstractType::Unit || unit->WhatAmI()==AbstractType::Aircraft)) {
            empUnits.erase(unit->UniqueID); // Release the mission hook before restoring Guard.
            auto foot=static_cast<FootClass*>(unit);
            if(foot->CurrentMission==Mission::Sleep && !ForceMove::motions.contains(unit->UniqueID))
                foot->ForceMission(Mission::Guard);
        }
        if(immune) nextImmune.insert(unit->UniqueID);
        if(mindImmune) nextMindImmune.insert(unit->UniqueID);
        if(cloakable) nextCloakable.insert(unit->UniqueID);
        if(mirage && unit->WhatAmI()==AbstractType::Unit) nextMirage.insert(unit->UniqueID);
        else if(mirageUnits.contains(unit->UniqueID) && unit->WhatAmI()==AbstractType::Unit) {
            auto type=unit->GetTechnoType();
            if(type && !(type->CanDisguise && !type->PermaDisguise && type->DisguiseWhenStill)
                && unit->Disguised && unit->Disguise && unit->Disguise->WhatAmI()==AbstractType::TerrainType)
                unit->ClearDisguise();
        }
        // Renew a short native effect. Longer effects from chaos weapons keep
        // their own timer and the engine handles expiration, including on load.
        if(berserk && unit->WhatAmI()!=AbstractType::Building && (!unit->Berzerk || unit->BerzerkDurationLeft<2)) {
            using EnterBerserk=void(__thiscall*)(TechnoClass*,int);
            reinterpret_cast<EnterBerserk>(0x70F890)(unit,2);
        }
        if(unit->WhatAmI()==AbstractType::Building)
            updateBuildingPower(static_cast<BuildingClass*>(unit),powerFactor);
        auto scaleId=unit->UniqueID;
        VisualScale::setStateFactor(unit,scaleFactor);
        if(projectileFactor!=1.0) projectileState[scaleId]=projectileFactor;
        else projectileState.erase(scaleId);
        if(rangeFactor!=1.0) rangeState[scaleId]=rangeFactor;
        else rangeState.erase(scaleId);
        if(fireRateFactor!=1.0) fireRateState[scaleId]=fireRateFactor;
        else fireRateState.erase(scaleId);
        if(experienceFactor!=1.0) experienceState[scaleId]=experienceFactor;
        else experienceState.erase(scaleId);
        updateSlots(unit->UniqueID,nextSlots);
        auto fields=statFields(unit);
        if(!fields[0]) desired[0]=1.0;
        if(old==applied.end() && desired==identity) continue;
        auto& record=applied[unit->UniqueID];
        record.state=desired;
        for(size_t k=0;k<fields.size();++k) if(fields[k]) updateStat(record,k,*fields[k]);
        if(desired==identity && record.percent==defaultStatPercent) applied.erase(unit->UniqueID);
    }
    std::erase_if(activeTooltips,[](const auto& entry){return entry.second.empty();});
    animFrame.finish();
    mirageUnits.swap(nextMirage);
    empUnits.swap(nextEmp);
    for(auto& handle:empCrashes)
        if(auto unit=handle.As_Foot();unit && alive(unit) && unit->IsInAir()) unit->Crash(nullptr);
    immuneUnits.swap(nextImmune);
    mindImmuneUnits.swap(nextMindImmune);
    cloakableUnits.swap(nextCloakable);
    activeAnimStates.swap(nextAnimStates);
    std::erase_if(nearbySamples,[](const auto& p){return !p.second.seen;});
    std::erase_if(stateTimers,[](const auto& item){return !item.second.observed;});
}
