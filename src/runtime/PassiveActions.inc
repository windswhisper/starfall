using Objects = std::map<DWORD, TechnoClass*>;
#include "../LuaNearby.inc"
TechnoClass* findLive(const Objects& objects,DWORD id) {
    auto it=objects.find(id);
    // Destructors invalidate entries during secondary damage.
    return it!=objects.end() && alive(it->second) ? it->second : nullptr;
}
Objects* activeObjects=nullptr;
template<class Visit>
void visitDirectOccupants(TechnoClass* container,Visit visit) {
    if(!container) return;
    auto passenger=container->Passengers.FirstPassenger;
    for(int left=container->Passengers.NumPassengers;left>0 && passenger;--left) {
        visit(passenger);
        passenger=static_cast<FootClass*>(passenger->NextObject);
    }
    if(auto building=abstract_cast<BuildingClass*>(container)) {
        for(auto resident:building->Occupants) visit(resident);
        if(building->Type->Bunker && building->TankBunkerState==TankBunkerState::Bunkered) {
            auto tank=building->BunkerLinkedItem;
            if(tank && tank->BunkerLinkedItem==building) visit(tank);
        }
    }
}
struct EffectScope {
    bool previous=applyingDamage;
    EffectScope(){ applyingDamage=true; }
    ~EffectScope(){ applyingDamage=previous; }
};
BulletVelocity launchVelocity(WeaponTypeClass* weapon,int speedValue,const CoordStruct& origin,AbstractClass* target,CoordStruct destination) {
    if(auto wall=HighPlatforms::arcingWallCell(target,weapon->Projectile,origin)) destination=wall->GetCoords();
    double dx=double(destination.X)-origin.X,dy=double(destination.Y)-origin.Y,dz=double(destination.Z)-origin.Z;
    double length=std::sqrt(dx*dx+dy*dy+dz*dz);
    double speed=std::max(1,speedValue);
    BulletVelocity velocity{};
    if(length>0) velocity={dx*speed/length,dy*speed/length,dz*speed/length};
    if(weapon->Projectile->Arcing) {
        double horizontal=std::hypot(dx,dy);
        if(horizontal>0) {
            double gravity=std::max(1,RulesClass::Instance->Gravity);
            double flight=std::sqrt(horizontal/gravity)*(weapon->Lobber?2.0:1.0);
            velocity={dx/flight,dy/flight,dz/flight+0.5*gravity*flight};
        }
    }
    return velocity;
}
void drawExtraWeaponVisual(TechnoClass*,AbstractClass*,int,WeaponTypeClass*,const CoordStruct&,HouseClass* = nullptr);

bool weaponEffect(const PassiveAction& s,TechnoClass* firer,AbstractClass* target,const CoordStruct& destination,const CoordStruct& muzzle,HouseClass* owner,std::optional<CoordStruct> explicitOrigin=std::nullopt) {
    auto weapon=s.action=="fireweapon"?WeaponTypeClass::Find(s.weapon.c_str()):nullptr;
    if(s.action=="fireweapon" && !weapon) return false;
    if(explicitOrigin && !FireOrigin::supports(weapon)) return false;
    auto warhead=weapon?weapon->Warhead:s.warhead;
    auto projectile=weapon?weapon->Projectile:BulletTypeClass::Find("Invisible");
    if(!warhead || !projectile || !target) return false;
    EffectScope scope;
    auto bullet=projectile->CreateBullet(target,firer,weapon?weapon->Damage:s.amount,warhead,
        weapon?weapon->Speed:100,weapon?weapon->Bright:warhead->Bright);
    if(!bullet) return false;
    if(!firer && owner) passiveBulletOwners[bullet->UniqueID]=owner->ArrayIndex;
    bullet->SetWeaponType(weapon);
    const auto origin=explicitOrigin?*explicitOrigin:FireOrigin::resolve(weapon,target,muzzle);
    const bool customOrigin=explicitOrigin.has_value() || FireOrigin::enabled(weapon);
    if(!weapon || (target==firer && !customOrigin) ||
       ((!firer || target==firer || explicitOrigin) && origin==destination)) {
        const auto id=bullet->UniqueID;
        bullet->SetLocation(destination);
        bullet->Detonate(destination);
        bullet->UnInit();
        passiveBulletOwners.erase(id);
        return true;
    }

    passiveBullets.insert(bullet->UniqueID);
    if(!bullet->MoveTo(origin,launchVelocity(weapon,bullet->Speed,origin,target,destination))) {
        const auto id=bullet->UniqueID;bullet->UnInit();passiveBulletOwners.erase(id);return false;
    }
    if(customOrigin && (firer || explicitOrigin) && (weapon->IsLaser || weapon->IsElectricBolt || weapon->IsRadBeam))
        drawExtraWeaponVisual(firer,target,0,weapon,origin,owner);
    if(firer) if(auto tracer=ArtSp::Tracers::select(firer->UniqueID,weapon))
        ArtSp::Tracers::launch(*tracer,origin,destination,Unsorted::CurrentFrame);
    return true;
}
bool particleEffect(const PassiveAction& s,TechnoClass* holder,AbstractClass* aim,const CoordStruct& origin,
                    const CoordStruct& destination,HouseClass* owner) {
    auto behavior=s.particleSystem->BehavesLike;
    bool directed=behavior==BehavesLike::Fire || behavior==BehavesLike::Railgun;
    if(directed && origin==destination) return false;
    if(directed && !aim) aim=MapClass::Instance.TryGetCellAt(destination);
    return GameCreate<ParticleSystemClass>(s.particleSystem,directed?origin:destination,
        directed?aim:nullptr,directed?holder:nullptr,directed?destination:CoordStruct::Empty,owner)!=nullptr;
}
struct MultiShotSettings {
    int count=0;
    bool preferSameLayer=false;
};
MultiShotSettings multiShotSettings(TechnoClass* unit,AbstractClass* primary) {
    auto ids=unitSkills(unit);extraSkills(unit,&ids);
    MultiShotSettings settings;
    TechnoClass* parent=nullptr;
    const auto ownCount=appendMountStates(unit,ids,parent);
    for(size_t n=0;n<ids.size();++n) {
        const auto& id=ids[n];
        auto source=n<ownCount?unit:parent;
        auto it=skills.find(id);
        if(it==skills.end() || !it->second.hasMultiShot() || suppressed(source->UniqueID,id)) continue;
        const auto& s=it->second;
        if(source->Transporter && (s.nearby.range || s.conditionFormula.expression.usesDistance() || s.multiShotFormula.expression.usesDistance())) continue;
        auto hp=UnitHealth::read(source);
        if(!stateActive(source,s,stateOf(source),hp,Unsorted::CurrentFrame)) continue;
        double extra=s.extraShotCount;
        if(!s.multiShotFormula.expression.empty())
            extra=stateFormulaValue(s.multiShotFormula,source,n<ownCount?primary:source->Target,hp,id).value_or(0);
        settings.count+=static_cast<int>(std::clamp(extra,0.0,static_cast<double>(INT_MAX-settings.count)));
        if(extra>=1 && s.multiShotPreferSameLayer) settings.preferSameLayer=true;
    }
    return settings;
}
void drawDetachedBolt(TechnoClass* firer,AbstractClass* target,WeaponTypeClass* weapon,const CoordStruct& origin) {
    using CreateBolt=EBolt*(__thiscall*)(TechnoClass*,AbstractClass*,WeaponTypeClass*,const CoordStruct&);
    reinterpret_cast<CreateBolt>(0x6FD460)(firer,target,weapon,origin);
}
void drawExtraWeaponVisual(TechnoClass* firer,AbstractClass* target,int slot,WeaponTypeClass* weapon,const CoordStruct& origin,HouseClass* owner) {
    if(firer) owner=firer->Owner;
    const auto end=(target->AbstractFlags&AbstractFlags::Object)!=AbstractFlags::None?
        static_cast<ObjectClass*>(target)->GetTargetCoords():target->GetCenterCoords();
    if(weapon->IsLaser) {
        if(firer) {
            if(auto laser=firer->CreateLaser(reinterpret_cast<ObjectClass*>(target),slot,weapon,origin))
                laser->Target=ProjectileShield::beamEnd(firer,weapon,laser->Source,laser->Target);
        } else {
            const bool houseColor=weapon->IsHouseColor && owner;
            const auto color=houseColor?owner->LaserColor:weapon->LaserInnerColor;
            const auto clipped=ProjectileShield::beamEnd(nullptr,weapon,origin,end,owner);
            if(auto laser=GameCreate<LaserDrawClass>(origin,clipped,color,weapon->LaserOuterColor,
                                                      weapon->LaserOuterSpread,weapon->LaserDuration))
                laser->IsHouseColor=houseColor;
        }
    } else if(weapon->IsElectricBolt) {
        if(firer) drawDetachedBolt(firer,target,weapon,origin);
        else if(auto bolt=GameCreate<EBolt>()) {
            bolt->AlternateColor=weapon->IsAlternateColor;
            bolt->Fire(origin,ProjectileShield::beamEnd(nullptr,weapon,origin,end,owner),0);
        }
    } else if(weapon->IsSonic) {
        SonicRipple::launch(origin,end,Unsorted::CurrentFrame);
    } else if(weapon->IsRadBeam) {
        const bool temporal=weapon->Warhead && weapon->Warhead->Temporal;
        auto beam=RadBeam::Allocate(temporal?RadBeamType::Temporal:RadBeamType::RadBeam);
        if(!beam) return;
        beam->SetCoordsSource(origin);
        beam->SetCoordsTarget(ProjectileShield::beamEnd(firer,weapon,origin,end,owner));
        const auto rules=RulesClass::Instance;
        const auto ini=CCINIClass::INI_Rules;
        const auto color=temporal?rules->ChronoBeamColor:rules->RadColor;
        beam->Color=owner && ini->ReadBool(weapon->ID,"Beam.IsHouseColor",false)?owner->LaserColor:
            ini->ReadColor(weapon->ID,"Beam.Color",color);
        beam->Period=ini->ReadInteger(weapon->ID,"Beam.Duration",15);
        beam->Amplitude=ini->ReadDouble(weapon->ID,"Beam.Amplitude",40.0);
    }
}
struct SplitTargets {
    DWORD primary;
    int slot;
    string weapon;
    vector<DWORD> branches;
};
std::map<DWORD,SplitTargets> splitTargets;
void multiShot(unsigned shot,TechnoClass* unit,BulletClass* original,AbstractClass* primary,int slot,const CoordStruct& origin,
               const ArtSp::Tracers::Style* tracer) {
    if(!alive(unit) || !unit->Owner || !original || !primary || slot<0 || slot>=TechnoTypeClass::MaxWeapons) return;
    auto last=splitTargets.extract(unit->UniqueID);
    auto weapon=original->WeaponType;
    if(!weapon || !weapon->Projectile || !original->WH || !original->Type || weapon->Spawner || weapon->LimboLaunch || weapon->IsMagBeam) return;
    const auto settings=multiShotSettings(unit,primary);
    if(!settings.count) return;
    auto owner=unit->Owner;
    auto originUnit=unit->Transporter?unit->Transporter:unit;
    auto mount=unit->GetWeapon(slot);
    if(!mount || mount->WeaponType!=weapon) return;
    std::set<DWORD> seen;
    const bool primaryAirborne=settings.preferSameLayer && primary->WhatAmI()!=AbstractType::Cell && primary->IsInAir();
    DWORD aim=primary->UniqueID;
    if(primary->WhatAmI()==AbstractType::Cell) {
        const auto cell=static_cast<CellClass*>(primary)->MapCoords;
        aim=0x80000000u | (static_cast<DWORD>(static_cast<WORD>(cell.X))<<16) | static_cast<WORD>(cell.Y);
    }
    std::unordered_map<DWORD,size_t> previousSlot;
    if(!last.empty() && last.mapped().primary==aim && last.mapped().slot==slot && last.mapped().weapon==weapon->ID) {
        const auto& branches=last.mapped().branches;
        for(size_t i=0;i<branches.size();++i) if(branches[i]) previousSlot.emplace(branches[i],i);
    }
    struct Candidate { bool otherLayer; size_t oldSlot; int distance; TechnoClass* unit; };
    vector<Candidate> targets;
    auto visit=[&](TechnoClass* other) {
        if(!alive(other) || other==primary || other==original->Target || other==unit || !other->Owner ||
           !other->IsOnMap || other->InLimbo || other->Transporter || other->IsSinking ||
           other->Owner==owner || owner->IsAlliedWith(other->Owner) || other->Owner->IsNeutral() ||
           !seen.insert(other->UniqueID).second) return false;
        if(other->Disguised && !other->IsClearlyVisibleTo(owner)) return false;
        if(other->CloakState!=CloakState::Uncloaked) {
            auto cell=other->GetCell();
            if(!cell || owner->ArrayIndex<0 || owner->ArrayIndex>=24 || !cell->Sensors_InclHouse(owner->ArrayIndex)) return false;
        }
        const bool airborne=other->IsInAir();
        if(!(airborne?original->Type->AA:original->Type->AG) || !other->GetTechnoType()->LegalTarget ||
           !unit->IsCloseEnough(other,slot) || !MapClass::GetTotalDamage(10000,original->WH,other->GetTechnoType()->Armor,0)) return false;
        auto error=unit->GetFireError(other,slot,false);
        if(error!=FireError::OK && error!=FireError::FACING && error!=FireError::ROTATING) return false;
        auto prev=previousSlot.find(other->UniqueID);
        targets.push_back({settings.preferSameLayer && airborne!=primaryAirborne,prev==previousSlot.end()?SIZE_MAX:prev->second,originUnit->DistanceFrom(other),other});
        return false;
    };
    int range=unit->GetWeaponRange(slot);
    if(range>0 && range<=32*256 && !original->Type->AA && originUnit==unit && !unit->IsInAir())
        visitNearbyCells(unit->GetMapCoords(),(range+255)/256+2,false,visit);
    else for(auto other:TechnoClass::Array) visit(other);
    auto count=std::min<size_t>(targets.size(),settings.count);
    std::partial_sort(targets.begin(),targets.begin()+count,targets.end(),[](const auto& a,const auto& b){
        if(a.otherLayer!=b.otherLayer) return a.otherLayer<b.otherLayer;
        if(a.oldSlot!=b.oldSlot) return a.oldSlot<b.oldSlot;
        if(a.distance!=b.distance) return a.distance<b.distance;
        return a.unit->UniqueID<b.unit->UniqueID;
    });
    if(!count) return;
    // Keep a surviving branch's index even when fewer enemies remain than available shots.
    size_t slots=count;
    for(size_t i=0;i<count;++i) if(targets[i].oldSlot<settings.count)
        slots=std::max(slots,targets[i].oldSlot+1);
    vector<TechnoClass*> assigned(slots);
    for(size_t i=0;i<count;++i)
        if(targets[i].oldSlot<slots) assigned[targets[i].oldSlot]=targets[i].unit;
    auto vacancy=assigned.begin();
    for(size_t i=0;i<count;++i) if(targets[i].oldSlot>=slots) {
        vacancy=std::find(vacancy,assigned.end(),nullptr);
        *vacancy++=targets[i].unit;
    }
    SplitTargets next{aim,slot,weapon->ID,vector<DWORD>(slots)};
    for(size_t i=0;i<slots;++i) {
        auto target=assigned[i];
        if(!target) continue;
        auto extra=original->Type->CreateBullet(target,unit,original->Health,original->WH,original->Speed,original->Bright);
        if(!extra) continue;
        extra->SetWeaponType(weapon);
        extra->DamageMultiplier=original->DamageMultiplier;
        extra->Range=original->Range;
        extra->InheritedColor=original->InheritedColor;
        const auto branch=FireOrigin::resolve(weapon,target,origin);
        if(!extra->MoveTo(branch,launchVelocity(weapon,extra->Speed,branch,target,target->GetCenterCoords()))) {
            extra->UnInit();
            continue;
        }
        next.branches[i]=target->UniqueID;
        if(weapon->AttachedParticleSystem && (weapon->IsRailgun || weapon->UseFireParticles || weapon->UseSparkParticles)) {
            auto system=GameCreate<ParticleSystemClass>(weapon->AttachedParticleSystem,branch,target,unit,target->GetTargetCoords(),unit->Owner);
            if(system) system->Target=target;
        }
        if(!ArtSp::ContinuousBeam::collectExtra(shot,unit,primary,weapon,target))
            drawExtraWeaponVisual(unit,target,slot,weapon,branch);
        if(tracer) ArtSp::Tracers::launch(*tracer,branch,target->GetTargetCoords(),Unsorted::CurrentFrame);
    }
    if(std::any_of(next.branches.begin(),next.branches.end(),[](DWORD id){return id!=0;}))
        splitTargets.insert_or_assign(unit->UniqueID,std::move(next));
}
HouseClass* actionOwner(const string& source,const Event& e,TechnoClass* holder,const Objects& objects) {
    if(e.trigger=="onmaptrigger" && source=="self" && e.owner>=0 && e.owner<HouseClass::Array.Count)
        return HouseClass::Array.Items[e.owner];
    if(source=="neutral") return HouseClass::FindNeutral();
    if(source.starts_with("player")) return HouseClass::FindByPlayerAt(HouseClass::PlayerAtA+source.back()-'1');
    DWORD id=source=="attacker"?e.attacker:source=="victim"?e.victim:e.holder;
    auto live=id==e.holder?holder:findLive(objects,id);
    if(live) return live->Owner;
    int saved=source=="attacker"?e.attackerOwner:source=="victim"?e.victimOwner:e.owner;
    return saved>=0 && saved<HouseClass::Array.Count ? HouseClass::Array.Items[saved] : nullptr;
}
// Newer Phobos builds expose a supported conversion entry that also refreshes
// their own per-instance data.
using PhobosConvertFn=HRESULT(__stdcall*)(FootClass*,TechnoTypeClass*);
PhobosConvertFn publicPhobosConverter() {
    auto module=GetModuleHandleW(L"Phobos.dll");
    if(!module) return nullptr;
    for(auto name:{"ConvertToType_Phobos","_ConvertToType_Phobos@8","ConvertToType_Phobos@8"})
        if(auto entry=GetProcAddress(module,name)) return reinterpret_cast<PhobosConvertFn>(entry);
    static bool reported=false;
    if(!reported) {
        std::ofstream("Starfall.log",std::ios::app)<<"Starfall: ConvertToType_Phobos unavailable, using built-in unit conversion\n";
        reported=true;
    }
    return nullptr;
}
using AresPermaMindFn=bool(__thiscall*)(void*,HouseClass*,AbstractClass*);
std::array<AresPermaMindFn,2> aresMindOriginal{};
bool __fastcall aresMindGuard0(void* self,void*,HouseClass* house,AbstractClass* target) {
    if(target && (target->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
       (UnitMount::mounted(target) || mindImmuneUnits.contains(static_cast<TechnoClass*>(target)->UniqueID))) return true;
    return aresMindOriginal[0](self,house,target);
}
bool __fastcall aresMindGuard1(void* self,void*,HouseClass* house,AbstractClass* target) {
    if(target && (target->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
       (UnitMount::mounted(target) || mindImmuneUnits.contains(static_cast<TechnoClass*>(target)->UniqueID))) return true;
    return aresMindOriginal[1](self,house,target);
}
void installAresMindGuards() {
    static bool installed=false,reported=false;
    if(installed) return;
    auto failure=[&] {
        if(!reported) {std::ofstream("Starfall.log",std::ios::app)<<"Starfall: unsupported Ares permanent mind-control guard\n";reported=true;}
    };
    auto base=reinterpret_cast<BYTE*>(GetModuleHandleW(L"Ares.dll"));
    if(!base) return;
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return;
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const BYTE entry[]={0x80,0x79,0x08,0x00,0x74,0x23};
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=0x61daa114 ||
       nt->OptionalHeader.SizeOfImage!=0xdb000 || memcmp(base+0x53980,entry,sizeof(entry))) {
        failure();
        return;
    }
    BYTE* calls[]={base+0x5450A,base+0x727E3};
    void* guards[]={reinterpret_cast<void*>(&aresMindGuard0),reinterpret_cast<void*>(&aresMindGuard1)};
    BYTE original[2][5]{};
    for(int i=0;i<2;++i) {
        if(calls[i][0]!=0xE8) {failure();return;}
        memcpy(original[i],calls[i],5);
        int32_t displacement;memcpy(&displacement,calls[i]+1,4);
        auto destination=reinterpret_cast<uintptr_t>(calls[i]+5)+displacement;
        HMODULE owner=nullptr;
        if(destination!=reinterpret_cast<uintptr_t>(base+0x53980) &&
           (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCWSTR>(destination),&owner) || owner!=GetModuleHandleW(L"Phobos.dll"))) {failure();return;}
        aresMindOriginal[i]=reinterpret_cast<AresPermaMindFn>(destination);
    }
    for(int i=0;i<2;++i) {
        DWORD old=0;
        if(!VirtualProtect(calls[i],5,PAGE_EXECUTE_READWRITE,&old)) {
            if(i) {DWORD protect=0;if(VirtualProtect(calls[0],5,PAGE_EXECUTE_READWRITE,&protect)) {
                memcpy(calls[0],original[0],5);FlushInstructionCache(GetCurrentProcess(),calls[0],5);
                VirtualProtect(calls[0],5,protect,&protect);
            }}
            failure();return;
        }
        auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(guards[i])-reinterpret_cast<uintptr_t>(calls[i]+5));
        memcpy(calls[i]+1,&relative,4);FlushInstructionCache(GetCurrentProcess(),calls[i],5);
        VirtualProtect(calls[i],5,old,&old);
    }
    installed=true;
}
bool nativeConversionTypesSafe(const TechnoTypeClass* from,const TechnoTypeClass* to) {
    if(from->Enslaves || to->Enslaves) return false;
    for(int i=0;i<TechnoTypeClass::MaxWeapons;++i) for(auto* weapon:{to->Weapon[i].WeaponType,to->EliteWeapon[i].WeaponType})
        if(weapon && weapon->Warhead && (weapon->Warhead->MindControl || weapon->Warhead->Parasite || weapon->Warhead->Temporal)) return false;
    return true;
}
bool groundFlightPair(const TechnoTypeClass* from,const TechnoTypeClass* to,bool landFirst=false) {
    using Id=LocomotionClass::CLSIDs;
    if(from->WhatAmI()!=AbstractType::UnitType || to->WhatAmI()!=AbstractType::UnitType) return false;
    const auto* flight=from->Locomotor==Id::Jumpjet?from:to;
    const auto* ground=flight==from?to:from;
    return flight->Locomotor==Id::Jumpjet && (!flight->BalloonHover || (landFirst && flight==from)) && ground->Locomotor==Id::Drive &&
        !ground->Naval && !flight->Naval;
}
bool canSwitchGroundFlight(UnitClass* unit,const TechnoTypeClass* to) {
    if(!unit->Locomotor || unit->GetHeight()!=0 || unit->OnBridge || unit->Transporter || unit->Absorbed ||
        unit->IsFallingDown || unit->IsSinking || unit->IsCrashing || unit->IsBeingWarpedOut() ||
        unit->IsAttackedByLocomotor || unit->IsImmobilized || unit->TubeIndex>=0 || unit->ParasiteEatingMe ||
        unit->Deploying || unit->Deployed || unit->Undeploying || unit->Unloading ||
        UnitMount::mounted(unit) || ForceMove::locked(unit) || unit->Destination ||
        unit->Locomotor->Is_Moving() || unit->Locomotor->Is_Moving_Now()) return false;
    const bool fromJumpjet=unit->Type->Locomotor==LocomotionClass::CLSIDs::Jumpjet;
    if(fromJumpjet) {
        auto loco=locomotion_cast<JumpjetLocomotionClass*>(unit->Locomotor);
        if(!loco || loco->Is_Piggybacking() || loco->State!=JumpjetLocomotionClass::Grounded) return false;
    } else {
        auto loco=locomotion_cast<DriveLocomotionClass*>(unit->Locomotor);
        if(!loco || loco->Is_Piggybacking()) return false;
    }
    const auto ground=fromJumpjet?to:unit->Type;
    auto cell=unit->GetCell();
    return cell && !cell->GetBuilding() && !cell->GetTerrain(false) && (!cell->Jumpjet || cell->Jumpjet==unit) &&
        cell->IsClearToMove(ground->SpeedType,true,true,-1,ground->MovementZone,-1,false);
}
bool canTransformUnit(TechnoClass* target,TechnoTypeClass* to,PhobosConvertFn converter,bool landFirst=false) {
    if(!target || !to) return false;
    if(target->WhatAmI()==AbstractType::Building)
        return to->WhatAmI()==AbstractType::BuildingType && BuildingTransform::eligible(static_cast<BuildingClass*>(target),static_cast<BuildingTypeClass*>(to));
    auto from=target->GetTechnoType();
    if(!from || !target->IsOnMap || target->InLimbo || from==to || from->WhatAmI()!=to->WhatAmI()
        || !target->Owner || !alive(target) || from->Strength<=0 || to->Strength<=0) return false;
    const bool switchLoco=from->Locomotor!=to->Locomotor;
    if(switchLoco && (!converter || !groundFlightPair(from,to,landFirst) ||
        !canSwitchGroundFlight(static_cast<UnitClass*>(target),to))) return false;
    if(target->WhatAmI()!=AbstractType::Infantry && target->WhatAmI()!=AbstractType::Unit && target->WhatAmI()!=AbstractType::Aircraft) return false;
    if(!converter) {
        auto foot=static_cast<FootClass*>(target);
        if(!nativeConversionTypesSafe(from,to) || !CarrierConversion::supported(target) || target->SlaveManager || target->CaptureManager ||
            target->Airstrike || target->TemporalTargetingMe || target->IsMindControlled() || target->MindControlledByHouse ||
            foot->ParasiteEatingMe || foot->ParasiteImUsing || foot->IsDeploying || target->Absorbed ||
            (target->Passengers.NumPassengers && (from->Gunner!=to->Gunner || from->OpenTopped!=to->OpenTopped))) return false;
    }
    if(target->Passengers.NumPassengers>0) {
        bool bySize=true;
        if(GetModuleHandleW(L"Ares.dll") || GetModuleHandleW(L"Phobos.dll")) {
            if(auto found=mapPassengersBySize.find(lower(to->ID));found!=mapPassengersBySize.end()) bySize=found->second;
            else if(auto ini=CCINIClass::INI_Rules) bySize=ini->ReadBool(to->ID,"Passengers.BySize",true);
        }
        const int used=bySize?target->Passengers.GetTotalSize():target->Passengers.NumPassengers;
        if(used>to->Passengers) return false;
        for(auto* p=target->Passengers.FirstPassenger;p;p=static_cast<FootClass*>(p->NextObject))
            if(p->GetTechnoType()->Size>to->SizeLimit) return false;
    }
    if(!switchLoco && from->SpeedType!=to->SpeedType && !target->IsInAir()) {
        auto cell=target->GetCell();
        if(!cell || to->SpeedType<SpeedType::Foot || to->SpeedType>SpeedType::FloatBeach) return false;
        auto land=target->OnBridge?LandType::Clear:cell->LandType;
        if(land<LandType::Clear || static_cast<size_t>(land)>=std::size(GroundType::Array)
            || GroundType::Array[static_cast<int>(land)].Cost[static_cast<int>(to->SpeedType)]<=0) return false;
    }
    return true;
}
bool transformUnit(TechnoClass* target,TechnoTypeClass* to,PhobosConvertFn converter,bool landFirst=false) {
    if(!canTransformUnit(target,to,converter,landFirst)) return false;
    if(target->WhatAmI()==AbstractType::Building) {
        // Keep roof occupants out of the foundation check until placement or rollback finishes.
        vector<DWORD> lifted;
        for(const auto& [id,link]:UnitMount::links) {
            if(link.parent==target && alive(link.child) && link.child->IsOnMap && !link.child->InLimbo &&
               link.child->Mark(MarkType::Up)) {
                lifted.push_back(id);
            }
        }
        auto replacement=BuildingTransform::convert(static_cast<BuildingClass*>(target),static_cast<BuildingTypeClass*>(to),
            [](BuildingClass* previous,BuildingClass* current) {
                const auto id=current->UniqueID;
                BuildingResearch::forget(previous->UniqueID);
                BuildingResearch::trackBuilding(current);
                for(auto& [child,link]:UnitMount::links) {
                    if(link.parent==previous) link.parent=current;
                    if(link.ordered==previous) link.ordered=current;
                }
                if(activeObjects) {activeObjects->erase(previous->UniqueID);(*activeObjects)[id]=current;}
                if(auto it=warheadGrants.find(id);it!=warheadGrants.end()) it->second.unit=current;
                for(auto& [key,source]:auraSources) if(source.unit==previous) source.unit=current;
                AnimFrame animations;
                for(auto& [key,record]:attachedAnims) if(key.first==id)
                    if(auto anim=animations.resolve(record)) anim->SetOwnerObject(current);
                // Native type-specific weapon mounts are rebuilt on the next query.
                ProjectileShield::forget(previous);
                weaponRules.erase(id);volleyWeapons.erase(id);mountCopies.erase({id,0});mountCopies.erase({id,1});
                buildingLookups.clear();
                LuaNearby::forget(previous->UniqueID);LuaNearby::moved(current);
            });
        for(auto id:lifted) {
            auto found=UnitMount::links.find(id);
            if(found!=UnitMount::links.end() && alive(found->second.child))
                found->second.child->Mark(MarkType::Down);
        }
        return replacement!=nullptr;
    }
    if(converter) {
        // A grounded jumpjet can still own the cell's separate flight reservation.
        auto landingCell=target->GetTechnoType()->Locomotor==LocomotionClass::CLSIDs::Jumpjet &&
            to->Locomotor==LocomotionClass::CLSIDs::Drive ? target->GetCell() : nullptr;
        const auto result=converter(static_cast<FootClass*>(target),to);
        if(result==S_OK && landingCell && landingCell->Jumpjet==target) landingCell->Jumpjet=nullptr;
        if(result!=E_NOTIMPL && result!=E_NOINTERFACE) return SUCCEEDED(result);
        if(!canTransformUnit(target,to,nullptr)) return false;
    }

    auto from=target->GetTechnoType();
    auto owner=target->Owner;
    if(!CarrierConversion::reserveSlots(target,to)) return false;
    auto hp=UnitHealth::read(target,from->Strength);
    const int maxHp=static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(to->Strength)+hp.maximum-from->Strength,1,INT_MAX));
    const int current=static_cast<int>(std::clamp<int64_t>((static_cast<int64_t>(hp.current)*maxHp+hp.maximum/2)/hp.maximum,1,maxHp));
    auto temporal=target->TemporalImUsing;
    if(temporal && temporal->Target) temporal->LetGo();
    owner->RegisterLoss(target,false);
    owner->RemoveTracking(target);
    switch(target->WhatAmI()) {
        case AbstractType::Infantry: static_cast<InfantryClass*>(target)->Type=static_cast<InfantryTypeClass*>(to);break;
        case AbstractType::Unit: static_cast<UnitClass*>(target)->Type=static_cast<UnitTypeClass*>(to);break;
        case AbstractType::Aircraft: static_cast<AircraftClass*>(target)->Type=static_cast<AircraftTypeClass*>(to);break;
        default: return false; // Callers validate supported types before changing bookkeeping.
    }
    if(maxHp==to->Strength) {
        UnitHealth::pools.erase(target->UniqueID);
        target->Health=target->EstimatedHealth=current;
    } else {
        auto& pool=UnitHealth::pools[target->UniqueID];
        pool.base=to->Strength;pool.maximum=maxHp;pool.set(target,current);
    }
    if(to->Ammo<0) target->Ammo=-1;
    else target->Ammo=target->Ammo<0?to->Ammo:std::min(target->Ammo,to->Ammo);
    if(target->WhatAmI()==AbstractType::Aircraft) target->SecondaryFacing.SetROT(to->ROT);
    else target->PrimaryFacing.SetROT(to->ROT);
    owner->AddTracking(target);
    owner->RegisterGain(target,false);
    owner->RecheckTechTree=true;
    CarrierConversion::apply(target,to);
    target->MarkForRedraw();
    return true;
}
bool canTransformUnit(TechnoClass* target,TechnoTypeClass* to) {
    return canTransformUnit(target,to,target && target->WhatAmI()!=AbstractType::Building?publicPhobosConverter():nullptr);
}
bool transformUnit(TechnoClass* target,TechnoTypeClass* to) {
    return transformUnit(target,to,target && target->WhatAmI()!=AbstractType::Building?publicPhobosConverter():nullptr);
}
bool chronoTeleport(TechnoClass* target,CellStruct center,int radius,bool instant=false) {
    auto& map=MapClass::Instance;
    if((target->AbstractFlags&AbstractFlags::Foot)==AbstractFlags::None || !target->IsOnMap || target->InLimbo ||
       target->Transporter || target->Absorbed || target->IsInAir() || target->IsSinking || target->IsCrashing ||
       target->IsImmobilized || target->IsBeingWarpedOut() || !map.IsWithinUsableArea(center,false)) return false;
    auto foot=static_cast<FootClass*>(target);
    if(!foot->Locomotor || foot->TubeIndex>=0 || foot->IsAttackedByLocomotor || foot->IsDeploying) return false;
    auto type=foot->GetTechnoType();
    auto speed=type->SpeedType==SpeedType::Winged?SpeedType::Wheel:type->SpeedType;
    if(speed<SpeedType::Foot || speed>SpeedType::FloatBeach || center==foot->GetMapCoords()) return false;
    bool usesBridge=GroundType::Array[static_cast<int>(LandType::Clear)].Cost[static_cast<int>(speed)]>0;
    // ChronoWarpTo records its landing cell immediately, before it becomes occupied.
    // Reading the native field also covers simultaneous orders and restored games.
    std::set<std::pair<short,short>> reserved;
    for(auto unit:FootClass::Array) if(unit && unit->IsImmobilized && alive(unit)) {
        auto p=CellClass::Coord2Cell(unit->ChronoDestCoords);
        reserved.emplace(p.X,p.Y);
    }
    auto available=[&](short x,short y)->CellClass* {
        CellStruct p{x,y};
        if(x<0 || y<0 || x>=512 || y>=512 || !map.IsWithinUsableArea(p,false) || reserved.contains({x,y})) return nullptr;
        auto cell=map.TryGetCellAt(p);
        if(!cell || cell->GetBuilding() || cell->GetContent() || cell->Jumpjet) return nullptr;
        bool bridge=cell->ContainsBridge();
        return (!bridge || usesBridge) && cell->IsClearToMove(speed,false,false,-1,type->MovementZone,-1,bridge)?cell:nullptr;
    };
    auto best=available(center.X,center.Y);
    int bestDistance=best?0:radius*radius+1;
    for(int r=1;r<=radius && r*r<bestDistance;++r) {
        for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx) {
            int distance=dx*dx+dy*dy;
            if(std::max(std::abs(dx),std::abs(dy))!=r || distance>=bestDistance) continue;
            if(auto cell=available(static_cast<short>(center.X+dx),static_cast<short>(center.Y+dy))) {
                best=cell;bestDistance=distance;
            }
        }
    }
    if(instant) {
        if(!best || foot->ParasiteEatingMe || !ForceMove::interruptTravel(foot)) return false;
        const auto from=foot->Location;
        foot->MarkForRedraw();foot->Mark(MarkType::Up);
        foot->OnBridge=best->ContainsBridge();foot->SetLocation(best->GetCoordsWithBridge());
        foot->Mark(MarkType::Down);ForceMove::syncCellRegistration(foot);
        foot->SightTimer.Start(0);foot->ForceMission(Mission::Guard);foot->MarkForRedraw();
        if(auto anim=RulesClass::Instance->WarpOut) playSkillAnim(anim,nullptr,from,foot->Owner);
        if(auto anim=RulesClass::Instance->WarpIn) playSkillAnim(anim,nullptr,foot->Location,foot->Owner);
        return true;
    }
    // The engine adjusts floor coordinates to bridge height inside ChronoWarpTo.
    return best && foot->ChronoWarpTo(best->GetCoords());
}
void joinDeathTeam(FootClass* unit,DWORD id) {
    if(!id) return;
    TeamClass* team=nullptr;
    for(auto candidate:TeamClass::Array) {
        if(candidate->UniqueID==id) {team=candidate;break;}
    }
    if(!team || team->Owner!=unit->Owner || !team->Type || !team->Type->TaskForce) return;
    if(team->AddMember(unit,false)) return;
    // The task force can already be full after the first split child. Temporarily
    // reserve a vacancy on this instance; native recruitment selects and counts
    // the actual slot, including types treated as equivalent by Phobos.
    auto force=team->Type->TaskForce;
    int reserved[6]{};
    for(int i=0;i<force->CountEntries && i<6;++i) {
        if(force->Entries[i].Amount>0 && team->CountObjects[i]>=force->Entries[i].Amount) {
            reserved[i]=team->CountObjects[i]-force->Entries[i].Amount+1;
            team->CountObjects[i]-=reserved[i];
        }
    }
    team->AddMember(unit,false);
    for(int i=0;i<6;++i) team->CountObjects[i]+=reserved[i];
}
FootClass* spawnUnit(TechnoTypeClass* type,HouseClass* owner,const CoordStruct& location) {
    auto& map=MapClass::Instance;
    const auto center=CellClass::Coord2Cell(location);
    if(!map.IsWithinUsableArea(center,false)) return nullptr;
    auto speed=type->WhatAmI()==AbstractType::AircraftType ? SpeedType::Wheel : type->SpeedType;
    if(speed<SpeedType::Foot || speed>SpeedType::FloatBeach) return nullptr;
    bool canUseBridge=GroundType::Array[static_cast<int>(LandType::Clear)].Cost[static_cast<int>(speed)]>0;
    for(int r=0;r<=3;++r) {
        for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx) {
            if(std::max(std::abs(dx),std::abs(dy))!=r) continue;
            CellStruct pos{static_cast<short>(center.X+dx),static_cast<short>(center.Y+dy)};
            if(!map.IsWithinUsableArea(pos,false)) continue;
            auto cell=map.TryGetCellAt(pos);
            if(!cell || cell->GetBuilding() || cell->GetContent()) continue;
            bool bridge=cell->ContainsBridge();
            if((bridge && !canUseBridge) || !cell->IsClearToMove(speed,false,false,-1,MovementZone::Normal,-1,bridge)) continue;
            auto unit=static_cast<FootClass*>(type->CreateObject(owner));
            if(!unit) return nullptr;
            unit->OnBridge=bridge;
            if(!unit->Unlimbo(cell->GetCoordsWithBridge(),DirType::North)) {
                unit->UnInit();
                continue;
            }
            unit->QueueMission(Mission::Guard,false);
            owner->RecheckTechTree=true;
            return unit;
        }
    }
    return nullptr;
}
// Validate the whole selected subtree before removing anything. Parents are
// recorded before their loaded children so reversing the list empties them inside out.
bool collectCargoTransports(TechnoClass* carrier,FootClass* root,vector<FootClass*>& transports) {
    if(!alive(root) || root==carrier || !root->GetTechnoType() || root->ParasiteEatingMe) return false;
    if(!root->Passengers.NumPassengers && !root->Passengers.FirstPassenger) return true;
    std::set<TechnoClass*> seen{carrier,root};
    transports.push_back(root);
    for(size_t i=0;i<transports.size();++i) {
        auto* parent=transports[i];
        int remaining=parent->Passengers.NumPassengers;
        for(ObjectClass* object=parent->Passengers.FirstPassenger;object;object=object->NextObject) {
            if(remaining<=0 || (object->AbstractFlags & AbstractFlags::Foot)==AbstractFlags::None) return false;
            --remaining;
            auto child=static_cast<FootClass*>(object);
            if(!seen.insert(child).second || !alive(child) || !child->GetTechnoType() || child->ParasiteEatingMe ||
               (child->Transporter && child->Transporter!=parent)) return false;
            if(child->Passengers.NumPassengers || child->Passengers.FirstPassenger) transports.push_back(child);
        }
        if(remaining) return false;
    }
    return true;
}
void disposePassenger(TechnoClass* carrier,FootClass* passenger) {
    if(carrier->GetTechnoType()->OpenTopped) carrier->ExitedOpenTopped(passenger);
    passenger->Transporter=nullptr;
    passenger->RegisterDestruction(nullptr);
    passenger->UnInit();
}
bool consumePassenger(TechnoClass* carrier,Event& actionEvent) {
    if(!carrier || carrier->Passengers.NumPassengers<=0) return false;
    auto passenger=carrier->Passengers.GetFirstPassenger();
    vector<FootClass*> transports;
    if(!collectCargoTransports(carrier,passenger,transports)) return false;
    auto hp=UnitHealth::read(passenger);
    auto passengerId=passenger->UniqueID;
    auto passengerCost=passenger->GetTechnoType()->Cost;
    actionEvent.consumedKind=kind(passenger);
    actionEvent.consumedOwner=passenger->Owner?passenger->Owner->ArrayIndex:-1;
    actionEvent.consumedPosition=carrier->GetCenterCoords();
    auto held=unitSkills(passenger);
    directExtraSkills(passenger,&held);
    auto remaining=std::make_shared<PassengerPassives>();
    for(const auto& id:held) if(skills.contains(id) && !directSuppressed(passengerId,id))
        remaining->emplace(id,heldUnitStacks(passenger,id));
    vector<string> ids;
    std::optional<Event> consumed;
    if(interested(carrier,"onpassengerconsumed",&ids)) {
        consumed=eventSnapshot("onpassengerconsumed",carrier,passenger,carrier,passenger,0);
        consumed->snapshot=std::move(ids);
        consumed->distance=0.0;consumed->hasImpact=true;consumed->impact=carrier->GetCoords();
    }
    for(auto it=transports.rbegin();it!=transports.rend();++it) {
        auto parent=*it;
        while(auto child=parent->RemoveFirstPassenger()) disposePassenger(parent,child);
    }
    FootClass* removed=nullptr;
    if((carrier->AbstractFlags&AbstractFlags::Foot)!=AbstractFlags::None)
        removed=static_cast<FootClass*>(carrier)->RemoveFirstPassenger();
    else removed=carrier->Passengers.RemoveFirstPassenger();
    if(removed!=passenger) return false;
    disposePassenger(carrier,passenger);
    // Release layers supplied by the departing passenger before checking the carrier's stack cap.
    if(transportGrants.contains(carrier->UniqueID)) refreshTransportGrants();
    if(carrier->Owner) carrier->Owner->RecheckPower=true;
    carrier->MarkForRedraw();
    actionEvent.targetHp=std::clamp(hp.current,0,hp.maximum);
    actionEvent.targetMaxHp=hp.maximum;
    actionEvent.targetCost=passengerCost;
    actionEvent.consumedTarget=passengerId;
    actionEvent.passengerPassives=remaining;
    if(consumed) {
        consumed->consumedTarget=passengerId;
        consumed->passengerPassives=std::move(remaining);
        consumed->passengers=carrier->Passengers.NumPassengers;
        pending.push_back(std::move(*consumed));
    }
    return true;
}
bool consumeTarget(TechnoClass* target) {
    // Remove exactly the selected map object. Native transport/garrison cleanup
    // could otherwise destroy or unload additional units as a side effect.
    if(!alive(target) || !target->IsOnMap || target->InLimbo || target->Transporter ||
       target->Passengers.NumPassengers || target->Passengers.FirstPassenger || target->BunkerLinkedItem)
        return false;
    if(auto building=abstract_cast<BuildingClass*>(target);building && building->Occupants.Count) return false;
    if(auto foot=abstract_cast<FootClass*>(target);foot && foot->ParasiteEatingMe) return false;
    auto owner=target->Owner;
    if(!target->Limbo()) return false;
    target->UnInit();
    if(owner) {
        owner->RecheckTechTree=true;
        owner->RecheckPower=true;
    }
    return true;
}
bool ownsPassive(TechnoClass* unit,const string& id) {
    if(!skills.contains(id) || suppressed(unit->UniqueID,id)) return false;
    auto ids=unitSkills(unit);
    extraSkills(unit,&ids);
    return includes(ids,id);
}
bool removeHeldPassive(TechnoClass* unit,const string& id) {
    if(!ownsDirectPassive(unit,id)) return false;
    auto uid=unit->UniqueID;
    if(auto granted=warheadGrants.find(uid);granted!=warheadGrants.end()) {
        granted->second.skills.erase(id);
        if(granted->second.skills.empty()) warheadGrants.erase(granted);
    }
    removedPassives[uid].insert(id);
    Taunt::remove(unit,id);
    eraseStacks(uid,id);
    refreshLinkedPassives(unit);
    if(!linkedPassive(uid,id)) stopAuraSources(uid,&id);
    clearGrantProgress(unit,id);
    return true;
}
bool stealPassives(TechnoClass* holder,TechnoClass* donor,const PassiveAction& action,PassengerPassives* passenger=nullptr) {
    if(!alive(holder) || (!donor && !passenger) || holder==donor) return false;
    reconcileUnitStacks(donor);
    reconcileUnitStacks(holder);
    bool changed=false;
    for(const auto& id:action.passiveList) {
        const int maximum=skills.at(id).stackMax;
        const bool alreadyHeld=ownsPassive(holder,id);
        const int room=maximum-(alreadyHeld?heldUnitStacks(holder,id):0);
        if(room<=0) continue;
        int available=0;
        bool canTakeBase=false;
        if(passenger) {
            auto saved=passenger->find(id);
            if(saved==passenger->end()) continue;
            // Older snapshots used zero for a held passive with no extra layers.
            available=std::max(1,saved->second);
        } else {
            if(!ownsDirectPassive(donor,id)) continue;
            canTakeBase=suppliedStacks(donor->UniqueID,id,researchLayers(donor,id))==0;
            available=manualStackCount(donor->UniqueID,id)+(canTakeBase?1:0);
        }
        const int moved=std::min({available,action.amount,room});
        if(!moved) continue;
        if(!passenger) {
            if(canTakeBase && moved==available) removeHeldPassive(donor,id);
            else if(!changeUnitStacks(donor,id,-moved,maximum)) continue;
        }
        grantToUnit(holder,PassiveGrant{{id},-1},false);
        const int addition=alreadyHeld?moved:moved-1;
        if(addition) changeUnitStacks(holder,id,addition,maximum);
        if(passenger) {
            if(moved==available) passenger->erase(id);
            else passenger->at(id)=available-moved;
        }
        changed=true;
    }
    // Acquisition formulas see all transferred stacks, including on the first grant.
    if(changed) {
        refreshLinkedPassives(donor);
        refreshLinkedPassives(holder);
    }
    if(changed && anyGainSkills) observeGains(holder);
    return changed;
}
bool runScript(const PassiveAction&,Event&,const Objects&,int,int* pendingDamage=nullptr);
// Audio availability is local. A muted client or a missing sample must not
// alter whether the synchronized action list counts as successfully fired.
std::map<string,int> soundIndices;
string lastMissingSound;
void playActionSound(const string& name,const CoordStruct& at) {
    if(name.size()>4 && !_strnicmp(name.c_str(),"wav:",4)) {
        // WAV playback is local presentation; gameplay success is unchanged.
        auto cell=MapClass::Instance.TryGetCellAt(at);
        if(cell && !cell->IsShrouded() && !cell->IsFogged() && TacticalClass::Instance) {
            auto [point,visible]=TacticalClass::Instance->CoordsToClient(at);
            const auto& view=DSurface::ViewBounds;
            if(visible && point.X>=view.X && point.Y>=view.Y && point.X<view.X+view.Width && point.Y<view.Y+view.Height)
                ResearchAudio::playPath(name.substr(4));
        }
        return;
    }
    if(!VocClass::Array.Count) return;
    auto it=soundIndices.find(name);
    if(it==soundIndices.end()) {
        int index=VocClass::FindIndex(name.c_str());
        if(index<0) {
            if(name!=lastMissingSound) configLog("PlaySound: unknown sound "+name);
            lastMissingSound=name;
            return;
        }
        it=soundIndices.emplace(name,index).first;
    }
    VocClass::PlayAt(it->second,at);
}
bool shakeVisibleAt(const CoordStruct& pos) {
    auto cell=MapClass::Instance.TryGetCellAt(pos);
    if(!cell || cell->IsShrouded() || cell->IsFogged() || !TacticalClass::Instance) return false;
    auto [pt,visible]=TacticalClass::Instance->CoordsToClient(pos);
    const auto& view=DSurface::ViewBounds;
    return visible && pt.X>=view.X && pt.Y>=view.Y && pt.X<view.X+view.Width && pt.Y<view.Y+view.Height;
}
#include "../GroundPickups.inc"
DWORD eventTarget(const Event& e) {
    if(e.kind=="none") return 0;
    if(e.trigger=="ontakedamage" || e.trigger=="onbeforetakedamage" || e.trigger=="ondeath") return e.attacker;
    return e.victim;
}
DWORD resultTarget(const PassiveAction& s,const Event& e) {
    return s.targetIsEventTarget && e.consumedTarget ? e.consumedTarget :
        s.target=="sender" || s.target=="attacker"?e.attacker:s.target=="victim"?e.victim:e.holder;
}
bool applyOne(const PassiveAction& s, Event& e, TechnoClass* holder, const Objects& objects,int stacks,std::optional<DWORD> selected,std::optional<CellStruct> point,TechnoClass* tauntTarget,std::optional<CoordStruct> fireOrigin) {
    if(s.action=="runscript") return runScript(s,e,objects,stacks);
    if(s.action=="removepickup" && s.pickupRange==-1) return removePickups(s,CellStruct::Empty);
    DWORD targetId=resultTarget(s,e);
    if(selected) targetId=*selected;
    auto target=targetId==e.holder ? holder : findLive(objects,targetId);
    if(selected && !target) return false;
    if(s.action=="cancelunitorder") return UnitOrders::cancel(target,s.order);
    if(s.target=="self.occupants" && (positionalAction(s.action) || (s.action=="sendsignal" && s.signalRange>0)) &&
        (!target->IsOnMap || target->InLimbo)) return false;
    auto eventDistance=e.distance>=0?std::optional<double>{e.distance}:std::nullopt;
    HealthFormula::Values targetValues{e.targetHp,e.targetMaxHp,e.targetCost};
    std::optional<HealthFormula::Values> senderValues;
    if(e.trigger=="onsignal") senderValues=HealthFormula::Values{e.senderHp,e.senderMaxHp,e.senderCost};
    auto signalValue=e.trigger=="onsignal"?std::optional<double>{e.signalValue}:std::nullopt;
    if(s.action=="sendsignal") {
        unsigned depth=e.signalChain?e.signalDepth+1:1;
        if(depth>16) {configLog("signal chain depth limit reached");return false;}
        auto chain=e.signalChain?e.signalChain:std::make_shared<SignalChain>(SignalChain{nextSignalChain++,1024});
        if(!chain->remaining) {configLog("signal chain delivery limit reached");return false;}
        double value=s.signalValue;
        if(!s.signalFormula.expression.empty()) {
            bool missing=false;
            auto result=s.signalFormula.expression.evaluate(e.hp,e.maxHp,e.damage,eventDistance,stacks,targetValues,&missing,e.selfCost,e.ammo,
                e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt,senderValues,signalValue,std::nullopt,e.owner,Research::counter);
            if(!result || !std::isfinite(*result)) {if(!missing) s.signalFormula.report();return false;}
            value=*result;
        }
        auto senderHouse=holder?holder->Owner:(e.owner>=0 && e.owner<HouseClass::Array.Count?HouseClass::Array.Items[e.owner]:nullptr);
        if(!senderHouse || (!target && !(s.target=="self" && !holder && s.signalRange!=0))) return false;
        CoordStruct center=target?target->GetCenterCoords():e.origin;
        auto senderHealth=holder?UnitHealth::read(holder):UnitHealth::Values{e.hp,e.maxHp};
        auto senderCost=holder?std::optional<int>{holder->GetTechnoType()->Cost}:e.selfCost;
        DWORD payloadId=0;
        if(s.signalTarget=="self") payloadId=e.holder;
        else if(s.signalTarget=="sender") payloadId=e.attacker;
        else if(s.signalTarget=="attacker") payloadId=e.attacker;
        else if(s.signalTarget=="victim") payloadId=e.victim;
        else if(s.signalTarget=="target") payloadId=e.consumedTarget?e.consumedTarget:
            (e.trigger=="ontakedamage" || e.trigger=="onbeforetakedamage" || e.trigger=="ondeath")?e.attacker:e.victim;
        auto payload=findLive(objects,payloadId);
        vector<TechnoClass*> receivers;
        for(auto unit:TechnoClass::Array) {
            if(!alive(unit) || !unit->IsOnMap || unit->InLimbo || !unit->Owner) continue;
            if(s.signalRange==0 && unit!=target) continue;
            if(s.signalRange!=0 && unit->UniqueID==e.holder && !s.signalIncludeSelf) continue;
            if(s.signalRange>0 && distanceCells(center,unit->GetCenterCoords())>s.signalRange) continue;
            const char* house=unit->Owner==senderHouse?"self":senderHouse->IsAlliedWith(unit->Owner)?"allies":unit->Owner->IsNeutral()?"neutral":"enemies";
            if(!includes(s.signalHouses,"all") && !includes(s.signalHouses,house)) continue;
            if(interested(unit,"onsignal")) receivers.push_back(unit);
        }
        std::sort(receivers.begin(),receivers.end(),[](auto a,auto b){return a->UniqueID<b->UniqueID;});
        bool sent=false;
        for(auto unit:receivers) {
            vector<string> ids;interested(unit,"onsignal",&ids);
            std::erase_if(ids,[&](const auto& id){return skills.at(id).signal!=s.signal;});
            if(ids.empty()) continue;
            if(!chain->remaining) {configLog("signal chain delivery limit reached");break;}
            auto message=eventSnapshot("onsignal",unit,payload,holder,payload,0);
            message.attacker=e.holder;message.victim=payloadId;
            message.attackerOwner=senderHouse->ArrayIndex;
            message.signal=s.signal;message.signalValue=value;message.signalDepth=depth;message.signalChain=chain;
            message.senderHp=senderHealth.current;message.senderMaxHp=senderHealth.maximum;message.senderCost=senderCost;
            message.signalSenderPosition=holder?holder->GetCenterCoords():e.origin;
            message.senderKind=holder?kind(holder):e.senderKind.empty()?"other":e.senderKind;
            message.senderRelation=unit->Owner==senderHouse?"self":unit->Owner->IsAlliedWith(senderHouse)?"allies":senderHouse->IsNeutral()?"neutral":"enemies";
            if(!payload && payloadId) {
                if(payloadId==e.holder) {
                    message.targetHp=e.hp;message.targetMaxHp=e.maxHp;message.targetCost=e.selfCost;message.kind=message.senderKind;
                    message.relation=message.senderRelation;message.victimOwner=e.owner;message.distance=distanceCells(unit->GetCenterCoords(),e.origin);
                }
                else if(payloadId==e.victim || payloadId==e.attacker || payloadId==e.consumedTarget) {
                    const bool consumed=payloadId==e.consumedTarget && !e.consumedKind.empty();
                    const bool originalSender=e.trigger=="onsignal" && payloadId==e.attacker && !consumed;
                    message.targetHp=originalSender?e.senderHp:e.targetHp;message.targetMaxHp=originalSender?e.senderMaxHp:e.targetMaxHp;
                    message.targetCost=originalSender?e.senderCost:e.targetCost;message.kind=consumed?e.consumedKind:originalSender?e.senderKind:e.kind;
                    int owner=consumed?e.consumedOwner:payloadId==e.victim?e.victimOwner:e.attackerOwner;
                    message.victimOwner=owner;
                    auto other=owner>=0 && owner<HouseClass::Array.Count?HouseClass::Array.Items[owner]:nullptr;
                    message.relation=!other?"none":other==unit->Owner?"self":unit->Owner->IsAlliedWith(other)?"allies":other->IsNeutral()?"neutral":"enemies";
                    if(consumed) message.distance=distanceCells(unit->GetCenterCoords(),e.consumedPosition);
                    else if(originalSender) message.distance=distanceCells(unit->GetCenterCoords(),e.signalSenderPosition);
                    else if(e.hasImpact && payloadId==e.victim) message.distance=distanceCells(unit->GetCenterCoords(),e.impact);
                }
            }
            message.snapshot=std::move(ids);--chain->remaining;pending.push_back(std::move(message));sent=true;
        }
        return sent;
    }
    if(s.action=="stealpassive") {
        if(!Mount::permits(target,holder) || UnitMount::mounted(holder)) return false;
        if(e.consumedTarget && targetId==e.consumedTarget)
            return stealPassives(holder,nullptr,s,e.passengerPassives.get());
        return stealPassives(holder,target,s);
    }
    if(positionalAction(s.action)) {
        if(e.trigger=="onsignal" && s.target=="victim" && !target) return false;
        auto origin=holder?holder->GetCenterCoords():e.origin;
        CoordStruct destination;
        AbstractClass* aim=target;
        if(point) {
            auto cell=MapClass::Instance.TryGetCellAt(*point);
            if(!cell || !MapClass::Instance.IsWithinUsableArea(*point,false)) return false;
            destination=cell->GetCoordsWithBridge();aim=cell;
        } else if(s.target=="victimlocation" || (!target && s.target=="victim")) {
            if(!e.hasImpact) return false;
            destination=e.impact;
            aim=nullptr;
        } else if(!holder && s.target=="self") {
            destination=origin;
        } else if(target) {
            destination=target->GetCenterCoords();
        } else return false;
        auto& map=MapClass::Instance;
        if(s.action=="playanim") {
            auto unit=(!point && s.target!="victimlocation")?target:nullptr;
            return playSkillAnim(s.playAnim,unit,destination,actionOwner("self",e,holder,objects))!=nullptr;
        }
        if(s.action=="playsound") {
            playActionSound(s.sound,destination);
            return true;
        }
        if(s.action=="shakescreen") {
            // Local presentation must never alter synchronized success, timers or cooldowns.
            if(!CCINIClass::INI_RA2MD.ReadBool("Phobos","HideShakeEffects",false) &&
               (!s.shakeLocal || shakeVisibleAt(destination))) {
                auto& screen=GScreenClass::Instance;
                if(s.shakeX) screen.ScreenShakeX=s.shakeX;
                if(s.shakeY) screen.ScreenShakeY=s.shakeY;
            }
            return true;
        }
        if(s.action=="spawnparticlesystem")
            return particleEffect(s,holder,aim,origin,destination,actionOwner("self",e,holder,objects));
        if(s.action=="spawnunit") {
            auto owner=actionOwner(s.owner,e,holder,objects);
            if(!owner || owner->Defeated || owner->IsObserver()) return false;
            if(s.randomUnitTypes.empty()) return spawnUnit(s.unitType,owner,destination);
            bool success=false;
            for(int n=0;n<s.randomCount;++n) {
                int pick=ScenarioClass::Instance->Random.RandomRanged(1,s.randomWeightTotal);
                size_t index=0;while(index<s.randomWeights.size() && (pick-=s.randomWeights[index])>0) ++index;
                if(index<s.randomUnitTypes.size() && spawnUnit(s.randomUnitTypes[index],owner,destination)) success=true;
            }
            return success;
        }
        if(s.action=="spawnbuilding")
            return BuildingSpawn::create(s.buildingType,actionOwner(s.owner,e,holder,objects),destination,s.searchRadius);
        if(s.action=="removepickup") return removePickups(s,CellClass::Coord2Cell(destination));
        if(s.action=="spawnpickup") return placePickup(s.pickupType,CellClass::Coord2Cell(destination));
        if(s.action=="spawncrate") {
            auto cell=CellClass::Coord2Cell(destination);
            return map.IsWithinUsableArea(cell,false) && map.PlacePowerupCrate(cell,s.crateType);
        }
        if(!aim) aim=map.TryGetCellAt(destination);
        return weaponEffect(s,holder,aim,destination,origin,actionOwner("self",e,holder,objects),fireOrigin);
    }
    auto amountValue=[&]() -> int {
        if(!s.amountFormula.expression.empty()) {
            bool missing=false;
            auto value=s.amountFormula.expression.evaluate(e.hp,e.maxHp,e.damage,eventDistance,stacks,targetValues,&missing,e.selfCost,e.ammo,e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt,senderValues,signalValue,std::nullopt,e.owner,Research::counter);
            if(!value || *value<(s.action=="changeresearchcounter"?INT_MIN:0) || *value>INT_MAX) {if(!missing) s.amountFormula.report();return 0;}
            return static_cast<int>(*value);
        }
        return s.amount;
    };
    if(s.action=="changeresearchcounter") return Research::changeCounter(e.owner,s.counter,amountValue());
    if(s.action=="givecredits" || s.action=="giveresearchpoints" || s.action=="grantrandomresearch" || s.action=="grantfreeresearch" || s.action=="grantresearch") {
        auto house=target?target->Owner:nullptr;
        if(e.trigger=="onmaptrigger" && s.target=="self" && e.owner>=0 && e.owner<HouseClass::Array.Count)
            house=HouseClass::Array.Items[e.owner];
        if(!house && !holder && s.target=="self" && e.owner>=0 && e.owner<HouseClass::Array.Count)
            house=HouseClass::Array.Items[e.owner];
        if(s.action=="grantresearch")
            return Research::Detail::Runtime::eligibleHouse(house) && house->Type &&
                Research::grantNamed(house->ArrayIndex,house->Type->ID,s.researchOptions.front())>=0;
        if(s.action=="grantfreeresearch")
            return Research::Detail::Runtime::eligibleHouse(house) && house->Type && Research::grantFree(house->ArrayIndex,house->Type->ID,s.researchOptions,s.researchRarity);
        if(s.action=="grantrandomresearch") {
            if(!house || !house->Type) return false;
            const int option=Research::grantRandom(house->ArrayIndex,house->Type->ID,s.researchRarity);
            if(option>=0) Research::Detail::showResearchSelection(house,option);
            return option>=0;
        }
        int amount=amountValue();
        if(!house || amount<=0) return false;
        const bool research=s.action=="giveresearchpoints";
        if(research) {
            int before=Research::balance(house->ArrayIndex);
            Research::award(house->ArrayIndex,amount);
            amount=Research::balance(house->ArrayIndex)-before;
            if(amount<=0) return false;
        } else house->GiveMoney(amount);
        if(s.showText) RewardText::add(house,target,e,research,amount);
        return true;
    }
    if (!target) return false;
    if(s.action=="attachunit") return Mount::create(target,s);
    if(s.action=="removeattachedunit") return Mount::removeAttached(target,s);
    if(UnitMount::mounted(target) && includes({"chronoteleport","changeowner","transform"},s.action)) return false;
    if(!Mount::permits(target,holder) && includes({"grantpassive","resetstatetimer","changepassivestack","removepassive"},s.action)) return false;
    if(s.action=="chronoteleport" || s.action=="jumpto" || s.action=="ejectpassengers" || s.action=="ejectunit") {
        auto dest=s.teleportCell;
        if(s.destinationIsEventTarget) {
            const auto id=e.consumedTarget?e.consumedTarget:eventTarget(e);
            const auto other=findLive(objects,id);
            if(!other || !other->IsOnMap || other->InLimbo) return false;
            dest=other->GetMapCoords();
        } else if(dest.X<0) {
            if(!e.hasImpact || s.action=="chronoteleport") return false;
            dest=CellClass::Coord2Cell(e.impact);
        }
        if(s.action=="chronoteleport") return !ForceMove::locked(target) && chronoTeleport(target,dest,s.searchRadius,s.teleportInstant);
        if(s.action=="jumpto") return ForceMove::jump(target,dest,s.jump);
        if(s.action=="ejectunit") return ForceMove::ejectUnit(target,dest,s.eject);
        return ForceMove::eject(target,dest,s.eject);
    }
    if(s.action=="forcemove") {
        if(s.forceMove.impact && !e.blastCenter && !e.hasImpact) return false;
        auto center=s.forceMove.impact?e.blastCenter.value_or(e.impact):holder?holder->Location:e.origin;
        return ForceMove::start(target,s.forceMove,center);
    }
    if(s.action=="applyironcurtain") {
        if(!target->IsOnMap || target->InLimbo) return false;
        int remaining=target->IsIronCurtained()?target->IronCurtainTimer.GetTimeLeft():0;
        if(remaining>=s.duration || (remaining>0 && target->ForceShielded)) return false;
        auto source=actionOwner("self",e,holder,objects);
        int hp=target->Health;
        EffectScope scope;
        target->IronCurtain(s.duration,source,false);
        // Native organic handling may remove the target during this call.
        auto survivor=findLive(objects,targetId);
        return !survivor || survivor->Health<hp || survivor->IronCurtainTimer.GetTimeLeft()>remaining;
    }
    if(s.action=="promote") {
        if(!target->GetTechnoType()->Trainable || target->Veterancy.IsElite()) return false;
        if(observePromotions && !previousState.contains(targetId))
            previousState[targetId]={target->Veterancy.IsVeteran()?1:0,deploymentOf(target)==DeployPosture::Deployed};
        if(s.amount==2 || target->Veterancy.IsVeteran()) target->Veterancy.SetElite();
        else target->Veterancy.SetVeteran();
        target->MarkForRedraw();return true;
    }
    if(s.action=="consumetarget") return consumeTarget(target);
    if(s.action=="consumepassenger") return consumePassenger(target,e);
    if(s.percentIndex<percentActions.size()) {
        const auto k=s.percentIndex;
        auto id=target->UniqueID;
        double delta=s.percentDelta;
        if(!s.formula.expression.empty()) {
            bool missing=false;
            auto value=s.formula.expression.evaluate(e.hp,e.maxHp,e.damage,eventDistance,stacks,targetValues,&missing,e.selfCost,e.ammo,e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt,senderValues,signalValue,std::nullopt,e.owner,Research::counter);
            if(!value) {if(!missing) s.formula.report();return false;}
            delta=*value*100.0;
            if(!std::isfinite(delta)) {s.formula.report();return false;}
            if(delta==0) return false;
        }
        auto changeStored=[&](auto& values) {
            auto it=values.find(id);
            double current=it==values.end()?100:static_cast<double>(it->second);
            double next=percentLimits[k].clamp(current+delta);
            if(next==current) return false;
            if(next==100) values.erase(id);
            else values[id]=static_cast<typename std::decay_t<decltype(values)>::mapped_type>(next);
            return true;
        };
        if(k==projectileScaleIndex) return changeStored(projectileBase);
        if(k==weaponRangeIndex) return changeStored(rangeBase);
        if(k==fireRateIndex) return changeStored(fireRateBase);
        if(k==scaleIndex) {
            if(target->WhatAmI()==AbstractType::Building || !changeStored(VisualScale::base)) return false;
            target->MarkForRedraw();return true;
        }
        auto field=statFields(target)[k];
        if(!field) return false;
        auto it=applied.find(id);
        double current=it==applied.end()?100:it->second.percent[k];
        double next=percentLimits[k].clamp(current+delta);
        if(next==current) return false;
        auto& record=applied[id];
        record.percent[k]=next;
        updateStat(record,k,*field);
        if(record.state==identity && record.percent==defaultStatPercent) applied.erase(id);
        return true;
    }
    if(s.action=="changemaxhealth") {
        int amount=s.amount;
        if(s.maxHealthPercent) {
            auto maximum=UnitHealth::read(target).maximum;
            amount=static_cast<int>(std::clamp<int64_t>((int64_t(maximum)*s.maxHealthPercent+50)/100,1,INT_MAX));
        }
        return UnitHealth::change(target,amount);
    }
    if(s.action=="transform") return transformUnit(target,s.unitType);
    if(s.action=="changeowner") {
        auto owner=actionOwner(s.owner,e,holder,objects);
        if(!owner || owner->Defeated || owner->IsObserver() || owner==target->Owner || target->IsMindControlled()
            || target->MindControlledByHouse) return false;
        return target->SetOwningHouse(owner,false);
    }
    if(s.action=="changeammo") {
        int max=target->GetTechnoType()->Ammo;
        if(max<=0 || target->Ammo<0) return false;
        int next=static_cast<int>(std::clamp<long long>(1LL*target->Ammo+s.amount,0,max));
        if(next==target->Ammo) return false;
        target->Ammo=next;target->MarkForRedraw();return true;
    }
    if(s.action=="selfdestruct") {
        int damage=UnitHealth::read(holder).current;
        auto id=holder->UniqueID;
        EffectScope scope;
        holder->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
        return !findLive(objects,id);
    }
    if(s.action=="grantpassive") return grantToUnit(target,PassiveGrant{{s.passive},s.duration,s.stackOnReapply},true,holder,tauntTarget);
    if(s.action=="resetstatetimer") {
        auto skill=skills.find(s.passive);
        if(skill==skills.end() || skill->second.trigger!="whilestate" || !skill->second.stateDelayFrames || !ownsPassive(target,s.passive)) return false;
        auto timer=stateTimers.find({target->UniqueID,s.passive});
        if(timer==stateTimers.end()) return false;
        timer->second.start=Unsorted::CurrentFrame;
        timer->second.observed=true;
        return true;
    }
    if(s.action=="changepassivestack") {
        reconcileUnitStacks(target);
        if(!ownsPassive(target,s.passive)) return false;
        const auto uid=target->UniqueID;
        const int research=researchLayers(target,s.passive);
        if(s.amount<0 && s.amount<=-stackCount(uid,s.passive,research)) {
            const auto passenger=transportGrants.find(uid);
            const bool fromPassenger=passenger!=transportGrants.end() && passenger->second.contains(s.passive);
            if(!research && !fromPassenger && !linkedPassive(uid,s.passive)) {
                // Single-grant research, including stages and bonds, also provides a base layer.
                vector<string> granted;
                researchSkills(target,&granted);
                if(!includes(granted,s.passive)) return removeHeldPassive(target,s.passive);
            }
        }
        return changeStacks(uid,s.passive,s.amount,skills.at(s.passive).stackMax,research);
    }
    if(s.action=="removepassive") {
        bool changed=false;
        for(const auto& id:s.passiveList) if(removeHeldPassive(target,id)) changed=true;
        return changed;
    }
    int amount=amountValue();
    if (!amount) return false;
    if(s.action=="heal") {
        auto hp=UnitHealth::read(target);
        int gain=std::min(amount,hp.maximum-hp.current);
        if(gain<=0) return false;
        UnitHealth::heal(target,hp.current+gain);
        target->MarkForRedraw();
    } else {
        int before=UnitHealth::read(target).current;
        DWORD id=target->UniqueID;
        auto source=actionOwner("self",e,holder,objects);
        if(!s.warhead || !warheadAllows(s.warhead,source,target->Owner)) return false;
        EffectScope scope;
        // Vanilla includes the owner in AffectsAllies. Keep the override local to this
        // damage operand so secondary damage and other units still use their own rules.
        auto previous=ownerDamageOverride;
        ownerDamageOverride=!s.warhead->AffectsAllies && source==target->Owner?&amount:nullptr;
        target->ReceiveDamage(&amount,0,s.warhead,holder,false,false,source);
        ownerDamageOverride=previous;
        auto after=findLive(objects,id);
        return !after || UnitHealth::read(after).current<before;
    }
    return true;
}
bool apply(const PassiveAction& s,Event& e,TechnoClass* holder,const Objects& objects,int stacks,
           std::optional<DWORD> selected=std::nullopt,std::optional<CellStruct> point=std::nullopt,
           TechnoClass* tauntTarget=nullptr,std::optional<CoordStruct> fireOrigin=std::nullopt) {
    if(s.target!="self.occupants") return applyOne(s,e,holder,objects,stacks,selected,point,tauntTarget,fireOrigin);
    // A result can destroy a unit and invalidate the native passenger chain mid-action.
    vector<DWORD> members;
    visitDirectOccupants(holder,[&](TechnoClass* member) {
        if(member && findLive(objects,member->UniqueID)==member) members.push_back(member->UniqueID);
    });
    bool changed=false;
    for(auto id:members) {
        holder=findLive(objects,e.holder);
        if(!holder) break;
        if(applyOne(s,e,holder,objects,stacks,id,point,tauntTarget,fireOrigin)) changed=true;
    }
    return changed;
}
// Both INI and Lua intercept the same pending hit after armor but before HP changes.
std::optional<int> pendingDamageAmount(const PassiveAction& action,const Event& e,int damage,int stacks) {
    if(action.action=="blockdamage") return 0;
    if(action.amountFormula.expression.empty()) return action.amount;
    auto distance=e.distance>=0?std::optional<double>{e.distance}:std::nullopt;
    HealthFormula::Values target{e.targetHp,e.targetMaxHp,e.targetCost};
    bool missing=false;
    auto value=action.amountFormula.expression.evaluate(e.hp,e.maxHp,damage,distance,stacks,target,&missing,
        e.selfCost,e.ammo,e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt,std::nullopt,std::nullopt,std::nullopt,e.owner,Research::counter);
    if(!value || *value<0 || *value>INT_MAX) {
        if(!missing) action.amountFormula.report();
        return std::nullopt;
    }
    return static_cast<int>(*value);
}
