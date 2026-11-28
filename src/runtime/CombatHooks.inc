// Select the body's conversion table after the native or Phobos status tint.
DEFINE_HOOK(0x7063E4,SF_PassiveTintShape,7) {
    R->EBX(bodyPalette(R->ESI<TechnoClass*>(),R->EBX<ConvertClass*>(),R->Stack<int>(0x80)));
    return 0;
}
// Ground vehicles composite their indexed hull, turret and barrel before this draw.
DEFINE_HOOK(0x73B2C3,SF_PassiveTintGroundVoxel,6) {
    if(passiveTints.empty() || R->Stack<int>(0x6C)) return 0;
    auto unit=R->ESI<UnitClass*>();
    if(!visibleTint(unit)) return 0;
    auto source=unit->GetDrawer();
    auto palette=bodyPalette(unit,source,0);
    if(palette!=source) R->EAX(palette->SelectPlainBlitter(R->EDI<BlitterFlags>()));
    return 0;
}
// Aircraft and building voxels have separate fresh and cached blit paths.
DEFINE_HOOK(0x70719C,SF_PassiveTintFreshVoxel,5) {
    R->ESI(bodyPalette(R->EBP<TechnoClass*>(),R->ESI<ConvertClass*>(),R->Stack<int>(0x160)));
    return 0;
}
DEFINE_HOOK(0x70750B,SF_PassiveTintVoxel,8) {
    R->EAX(bodyPalette(R->EDI<TechnoClass*>(),R->EAX<ConvertClass*>(),R->Stack<int>(0x78)));
    return 0;
}
// Building art can put most of its visible pixels in an active animation.
DEFINE_HOOK(0x4235D3,SF_PassiveTintBuildingAnim,6) {
    if(passiveTints.empty()) return 0;
    auto anim=R->ESI<AnimClass*>();
    if(anim->IsBuildingAnim && anim->OwnerObject && anim->OwnerObject->WhatAmI()==AbstractType::Building) {
        auto source=R->Stack<ConvertClass*>(0x30);
        R->Stack<ConvertClass*>(0x30,bodyPalette(static_cast<BuildingClass*>(anim->OwnerObject),source,R->EBP<int>()));
    }
    return 0;
}
#include "../OrdnanceHooks.inc"
DEFINE_HOOK(0x466507,SF_ProjectileLaunchScale,6) {
    auto bullet=R->ECX<BulletClass*>();
    if(auto owner=bullet->Owner) {
        if(owner->Owner && (ActionSequence::warheadScripts.contains(bullet->WH) || ProjectileShield::configured || Piercing::enabled(bullet->Type) ||
           (!bullet->Type->Inviso && EntityWalls::hasProjectileBlockers())))
            passiveBulletOwners[bullet->UniqueID]=owner->Owner->ArrayIndex;
        auto fixed=projectileBase.find(owner->UniqueID);
        auto state=projectileState.find(owner->UniqueID);
        double value=(fixed==projectileBase.end()?100:fixed->second)*(state==projectileState.end()?1.0:state->second);
        int percent=static_cast<int>(std::lround(std::clamp(value,1.0,static_cast<double>(INT_MAX))));
        if(percent!=100) {VisualScale::projectiles[bullet->UniqueID]=percent;return 0;}
    }
    VisualScale::projectiles.erase(bullet->UniqueID);
    return 0;
}
bool Piercing::allowed(WarheadTypeClass* wh,HouseClass* house,TechnoClass* u) {
    if(!wh || u->IsIronCurtained() || u->GetTechnoType()->Immune || immuneUnits.contains(u->UniqueID)) return false;
    bool allowed=warheadHouses.contains(wh)?warheadAllows(wh,house,u->Owner):
        (!house || !house->IsAlliedWith(u->Owner) || wh->AffectsAllies);
    return allowed && MapClass::GetTotalDamage(10000,wh,u->GetTechnoType()->Armor,0)!=0;
}
HouseClass* EntityWalls::firingHouse(BulletClass* bullet) {
    auto it=passiveBulletOwners.find(bullet->UniqueID);
    if(it!=passiveBulletOwners.end()) return HouseClass::Array.GetItemOrDefault(it->second);
    return bullet->Owner?bullet->Owner->Owner:nullptr;
}
DEFINE_HOOK(0x469BD4,SF_ExplosionBegin,8) {
    auto bullet=R->ESI<BulletClass*>();
    auto found=VisualScale::projectiles.find(bullet->UniqueID);
    if(found!=VisualScale::projectiles.end())
        explosionBatches[R->EBP()]={AnimClass::Array.Count,found->second};
    return 0;
}
DEFINE_HOOK(0x469D06,SF_ExplosionEnd,6) {
    finishExplosions(R->EBP());
    return 0;
}
// Bullet impacts can happen several frames after FireWeapon returns. Guard both
// exits of BulletClass::Detonate and keep an enclosing passive impact suppressed.
DEFINE_HOOK(0x4690BC,SF_ImpactEnter,5) {
    auto bullet=R->ECX<BulletClass*>();
    auto scale=VisualScale::projectiles.find(bullet->UniqueID);
    impactCenters.emplace_back(R->EBP(),*R->Base<CoordStruct*>(8));
    beginReapplyImpact(R->EBP(),bullet->WH);
    enterSpread(R->EBP(),bullet->WH,scale==VisualScale::projectiles.end()?100:scale->second);
    if(passiveBullets.contains(bullet->UniqueID)) passiveDetonations.insert(R->EBP());
    return 0;
}
DEFINE_HOOK(0x469211,SF_BulletGrantPassives,6) {
    auto bullet=R->ESI<BulletClass*>();
    bool scripted=ActionSequence::warheadScripts.contains(bullet->WH);
    bool grants=grantRules.contains(bullet->WH);
    if(!scripted && !grants) return 0;
    auto at=R->Base<CoordStruct*>(8);
    auto house=bullet->Owner?bullet->Owner->Owner:nullptr;
    if(!house) if(auto owner=passiveBulletOwners.find(bullet->UniqueID);owner!=passiveBulletOwners.end()) {
        auto index=owner->second;
        if(index>=0 && index<HouseClass::Array.Count) house=HouseClass::Array.Items[index];
    }
    if(scripted) warheadScriptImpacts.emplace(R->EBP(),WarheadScriptImpact{bullet->WH,*at,house,bullet->Health});
    if(!grants) return 0;
    bulletGrantImpacts[R->EBP()]={bullet->WH,*at,house};
    deliverWarheadGrants(bullet->WH,*at,house,bullet->Target,bullet->Owner);
    return 0;
}
// Suspend the outer radius before Phobos and the native area logic inspect it.
DEFINE_HOOK(0x489280,SF_AreaSpreadEnter,6) {
    // Scripted projectiles and casterless queued effects retain their firing house.
    // Forward it to native area damage as well as passive grants, so ally and
    // owner filtering still works after the original killer has been removed.
    if(R->Stack<DWORD>(0)==0x469A88 && !R->Stack<TechnoClass*>(4)) {
        if(auto bullet=R->ESI<BulletClass*>())
            if(auto owner=passiveBulletOwners.find(bullet->UniqueID);owner!=passiveBulletOwners.end() &&
               owner->second>=0 && owner->second<HouseClass::Array.Count)
                R->Stack<HouseClass*>(0x10,HouseClass::Array.Items[owner->second]);
    }
    if(!bombImpacts.empty() && !bombImpacts.back().areaFrame) {
        auto& impact=bombImpacts.back();impact.areaFrame=R->ESP()-sizeof(DWORD);
        R->EDX(ordnanceDamage(R->EDX<int>(),impact.snapshot.firepower));
        enterSpread(impact.areaFrame,R->Stack<WarheadTypeClass*>(8),impact.snapshot.scale);
        return 0;
    }
    if(R->Stack<DWORD>(0)==0x6632CC) {
        enterSpread(R->ESP()-sizeof(DWORD),R->Stack<WarheadTypeClass*>(8),ordnanceScale(R->Stack<TechnoClass*>(4)));
        return 0;
    }
    if(!spreadScopes.empty() && (R->Stack<DWORD>(0)!=0x469A88 || R->EBP()!=spreadScopes.back().frame))
        enterSpread(R->ESP()-sizeof(DWORD),nullptr,100);
    return 0;
}
DEFINE_HOOK(0x48928C,SF_AreaGrantPassives,5) {
    auto wh=R->Base<WarheadTypeClass*>(0xC);
    // The original bullet-to-area call is part of the same detonation. Other
    // area calls (including nested hits at identical coordinates) are separate.
    bool bulletArea=!reapplyImpacts.empty() && reapplyImpacts.back().wh==wh &&
        reapplyImpacts.back().frame==R->Base<DWORD>(0) && R->Base<DWORD>(4)==0x469A88;
    impactCenters.emplace_back(R->EBP(),*R->ECX<CoordStruct*>());
    if(!bulletArea) beginReapplyImpact(R->EBP(),wh);
    bool scripted=ActionSequence::warheadScripts.contains(wh);
    bool grants=grantRules.contains(wh) && wh->CellSpread>0;
    if(!scripted && !grants) return 0;
    auto firer=R->Base<TechnoClass*>(8);
    auto house=firer?firer->Owner:R->Base<HouseClass*>(0x14);
    auto at=R->ECX<CoordStruct*>();
    if(scripted) {
        auto frame=R->EBP();
        if(!house && !bombImpacts.empty() && bombImpacts.back().areaFrame==frame) {
            const auto& bomb=bombImpacts.back();
            house=bomb.bomb->OwnerHouse;
            auto index=bomb.snapshot.owner;
            if(!house && index>=0 && index<HouseClass::Array.Count) house=HouseClass::Array.Items[index];
        }
        if(R->Base<DWORD>(4)==0x469A88) {
            auto outer=warheadScriptImpacts.find(R->Base<DWORD>(0));
            if(outer!=warheadScriptImpacts.end() && outer->second.wh==wh) {
                frame=outer->first;
                if(!house) house=outer->second.house;
            }
        }
        warheadScriptImpacts.insert_or_assign(frame,WarheadScriptImpact{wh,*at,house,R->EDX<int>()});
    }
    if(!grants) return 0;
    auto parent=bulletGrantImpacts.find(R->Base<DWORD>(0));
    if(!house && bulletArea && parent!=bulletGrantImpacts.end() && parent->second.wh==wh) house=parent->second.source;
    if((grantRules.at(wh).stackOnReapply && !bulletArea) || parent==bulletGrantImpacts.end() || parent->second.wh!=wh ||
       parent->second.at!=*at || parent->second.source!=house) deliverWarheadGrants(wh,*at,house,nullptr,firer);
    return 0;
}
DEFINE_HOOK_AGAIN(0x489B3B,SF_AreaSpreadLeave,5)
DEFINE_HOOK_AGAIN(0x48A4DA,SF_AreaSpreadLeave,5)
DEFINE_HOOK(0x48A4AE,SF_AreaSpreadLeave,5) {
    auto frame=R->EBP();
    leaveImpactCenter(frame);endReapplyImpact(frame);leaveSpread(frame);
    finishWarheadScript(frame);
    return 0;
}
DEFINE_HOOK_AGAIN(0x46A2FB,SF_ImpactLeave,5)
DEFINE_HOOK(0x46A290,SF_ImpactLeave,5) {
    auto frame=R->EBP();
    leaveImpactCenter(frame);endReapplyImpact(frame);finishExplosions(frame);leaveSpread(frame);
    bulletGrantImpacts.erase(frame);passiveDetonations.erase(frame);
    finishWarheadScript(frame);
    return 0;
}
DEFINE_HOOK(0x4665E9,SF_BulletDestroyed,10) {auto id=R->ESI<BulletClass*>()->UniqueID;Piercing::forget(id);ProjectileScript::forget(id);VisualScale::projectiles.erase(id);passiveBullets.erase(id);passiveBulletOwners.erase(id);return 0;}
void queueDeath(TechnoClass* victim,TechnoClass* killer) {
    vector<string> deathSkills;
    if(victim && interested(victim,"ondeath",&deathSkills) && reportedDeaths.insert(victim->UniqueID).second) {
        auto e=eventSnapshot("ondeath",victim,killer,killer,victim,0);
        e.snapshot=std::move(deathSkills);pending.push_back(std::move(e));
    }
    if(victim) stopAuraSources(victim->UniqueID);
}
DEFINE_HOOK(0x702E4E, SF_Kill, 6) {
    auto killer=R->EDI<TechnoClass*>(), victim=R->ESI<TechnoClass*>();
    if(victim && victim->Health<=0) queueDeath(victim,killer);
    if(victim) capture("onkill",killer,victim,killer,victim,0);
    return 0;
}
// The engine has ruled out allied kills. Supply the rank-adjusted victim value
// to the shared entry of vanilla and Ares experience distribution.
DEFINE_HOOK(0x702E79,SF_KillExperience,6) {
    if(experienceState.empty() || R->EBP<int>()<=0) return 0;
    const auto killer=R->EDI<TechnoClass*>();
    const auto factor=experienceState.find(killer->UniqueID);
    if(factor==experienceState.end()) return 0;
    const auto& rank=R->ESI<TechnoClass*>()->Veterancy;
    const int weight=rank.IsElite()?3:(rank.IsVeteran()?2:1);
    const auto points=static_cast<long long>(R->EBP<int>())*weight;
    unscaledKillScores[R->ESP()]=static_cast<int>(points);
    R->EBP(static_cast<int>(std::lround(std::min(static_cast<double>(INT_MAX),points*factor->second))));
    return 0x702E9D;
}
DEFINE_HOOK(0x702FF5,SF_RestoreKillScore,7) {
    auto original=unscaledKillScores.find(R->ESP());
    if(original!=unscaledKillScores.end()) {
        R->EBP(original->second);
        unscaledKillScores.erase(original);
    }
    return 0;
}
// Only full rearm passes through these exits. Burst cadence is calculated
// separately. The normal path already includes house, veteran and bunker rules.
DEFINE_HOOK(0x6FD1F1,SF_FullRearmFireRate,5) {
    R->EBP(adjustedRearmDelay(R->ESI<TechnoClass*>(),R->EBP<int>()));
    return 0;
}
DEFINE_HOOK(0x6FD1FA,SF_FixedRearmFireRate,6) {
    R->EAX(adjustedRearmDelay(R->ESI<TechnoClass*>(),R->EDI<WeaponTypeClass*>()->ROF));
    return 0x6FD200;
}
// InRange performs a separate calculation. EDI now holds the maximum in
// leptons, including native bonuses. MinimumRange is checked independently.
DEFINE_HOOK(0x6F72E3,SF_WeaponRangeCheck,6) {
    R->EDI(adjustedWeaponRange(R->ESI<TechnoClass*>(),R->EDI<int>()));
    return 0;
}
// Aircraft attack missions also compare distance directly. Use the range of
// the resolved mount (including alternate aircraft weapons selected by Phobos).
DEFINE_HOOK(0x4180FF,SF_AircraftAttackRange,6) {
    auto plane=R->ESI<AircraftClass*>();
    if(!rangeBase.contains(plane->UniqueID) && !rangeState.contains(plane->UniqueID)) return 0;
    auto mount=R->EAX<WeaponStruct*>();
    if(!mount || !mount->WeaponType || !plane->Target) return 0;
    int slot=R->EBX<int>();
    if(slot<0 || slot>=TechnoTypeClass::MaxWeapons || plane->GetWeapon(slot)!=mount) {
        slot=plane->SelectWeapon(plane->Target);
        if(slot<0 || slot>=TechnoTypeClass::MaxWeapons || plane->GetWeapon(slot)!=mount) return 0;
    }
    R->EDI(mount->WeaponType);
    return plane->DistanceFrom(plane->Target)<plane->GetWeaponRange(slot)?0x418117:0x418131;
}
DEFINE_HOOK(0x418BAE,SF_AircraftAttackDelayRange,5) {
    R->EAX(adjustedWeaponRange(R->ESI<AircraftClass*>(),R->EAX<int>()));
    return 0;
}
// Standard units, infantry, aircraft, and the base path used by buildings.
// EAX already contains the mount chosen by the engine (including elite FLH).
DEFINE_HOOK(0x70E190,SF_WeaponMount,5) {
    if(!weaponRules.empty() || !volleyWeapons.empty()) applyMount(R,R->ESI<TechnoClass*>()->UniqueID,R->EDI<int>());
    return 0;
}
// Occupied/upgraded buildings also have paths that bypass the base getter.
// Preserve the building identity: ESI may hold an occupant at the epilogue.
DEFINE_HOOK(0x4526F0,SF_BuildingMountStart,6) {
    if(!weaponRules.empty() || !volleyWeapons.empty()) {
        auto id=R->ECX<BuildingClass*>()->UniqueID;
        int slot=R->Stack<int>(4);
        auto volley=volleyWeapons.find(id);
        if((slotRule(id,slot) && slotRule(id,slot)->replacement) ||
           (volley!=volleyWeapons.end() && volley->second.slot==slot && volley->second.replacement))
            buildingLookups[R->ESP()]={id,slot};
    }
    return 0;
}
DEFINE_HOOK_AGAIN(0x45278B,SF_BuildingMountEnd,7)
DEFINE_HOOK_AGAIN(0x4527AD,SF_BuildingMountEnd,7)
DEFINE_HOOK(0x4527BC,SF_BuildingMountEnd,7) {
    if(!buildingLookups.empty()) {
        auto it=buildingLookups.find(R->ESP()+16);
        if(it!=buildingLookups.end()) {
            applyMount(R,it->second.id,it->second.slot);buildingLookups.erase(it);
        }
    }
    return 0;
}
// For a conventional primary/secondary loadout, select the remaining weapon.
// Native fire checks still enforce range, ammunition and targeting restrictions.
DEFINE_HOOK(0x6F3330,SF_SelectEnabledSlot,5) {
    if(weaponRules.empty()) return 0;
    auto unit=R->ECX<TechnoClass*>();
    auto it=weaponRules.find(unit->UniqueID);
    if(it==weaponRules.end() || it->second[0].disabled==it->second[1].disabled) return 0;
    auto type=unit->GetTechnoType();
    if(type->Gunner || type->IsGattling || type->WeaponCount>2 || type->HasMultipleTurrets()) return 0;
    int other=it->second[0].disabled?1:0;
    auto mount=unit->GetWeapon(other);
    if(!mount || !mount->WeaponType) return 0;
    R->EAX(other);return 0x6F3813;
}
DEFINE_HOOK(0x6FC0B0,SF_DisabledSlotCheck,8) {
    if((ForceMove::locked(R->ECX<TechnoClass*>()) || empLocked(R->ECX<TechnoClass*>()))) {R->EAX(static_cast<int>(FireError::BUSY));return 0x6FC876;}
    if(!weaponRules.empty()) {
        auto rule=slotRule(R->ECX<TechnoClass*>()->UniqueID,R->Stack<int>(8));
        if(rule && rule->disabled) { R->EAX(5); return 0x6FC876; }
    }
    return 0;
}
std::optional<Event> firingEvent(TechnoClass* unit,AbstractClass* aim) {
    vector<string> ids;
    if(applyingDamage || !passiveDetonations.empty() || !alive(unit) || !interested(unit,"onfire",&ids)) return {};
    auto victim=aim && (aim->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
    auto e=eventSnapshot("onfire",unit,victim,unit,victim,0);
    e.snapshot=std::move(ids);
    if(aim) {
        e.impact=aim->GetCoords();e.hasImpact=true;
        if(!victim) e.distance=distanceCells(e.origin,e.impact);
    }
    return e;
}
// Missile spawners assign the destination through SpawnManager, outside the
// ordinary projectile commit. This point is reached once per launched missile.
DEFINE_HOOK(0x6B75BC,SF_SpawnMissileFired,6) {
    auto manager=R->ESI<SpawnManagerClass*>();
    UnitOrders::shot(manager->Owner,manager->Target,true);
    if(auto e=firingEvent(manager->Owner,manager->Target)) pending.push_back(std::move(*e));
    return 0;
}
DEFINE_HOOK(0x6FDD7D,SF_StartFiring,5) {
    auto u=R->ESI<TechnoClass*>(); auto key=R->EBP();shots.erase(key);
    int slot=R->Base<int>(12);
    if(ForceMove::locked(u) || empLocked(u)) return 0x6FDE03;
    if(auto it=UnitOrders::orders.find(u->UniqueID);it!=UnitOrders::orders.end() && !Taunt::targetOf(u) &&
       (!it->second.target || it->second.target!=R->Base<AbstractClass*>(8) || !UnitOrders::fixedPosition(u))) return 0x6FDE03;
    if(auto lock=Taunt::targetOf(u)) {
        auto aim=R->Base<AbstractClass*>(8);
        if(!aim || aim->UniqueID!=lock) return 0x6FDE03;
    }
    if(auto rule=slotRule(u->UniqueID,slot);rule && rule->disabled) return 0x6FDE03;
    if(slot>=0 && slot<2 && !applyingDamage && passiveDetonations.empty() && alive(u) &&
       (volleyWeapons.contains(u->UniqueID) || interested(u,"onbeforefire"))) {
        auto aim=R->Base<AbstractClass*>(8);
        if(u->CurrentBurstIndex==0) {
            volleyWeapons.erase(u->UniqueID);
            // Fire fetched the mount before this hook; discard any previous volley's
            // cached replacement before deciding whether this volley qualifies.
            auto native=u->GetWeapon(slot);
            if(!native || !native->WeaponType) return 0x6FDE03;
            R->EBX(native->WeaponType);R->Stack(0x40,native->WeaponType);
            vector<string> ids;
            if(aim && interested(u,"onbeforefire",&ids)) {
                auto other=(aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
                auto event=eventSnapshot("onbeforefire",u,other,u,other,0);
                for(const auto& id:ids) {
                    const auto& s=skills.at(id);
                    if(!meetsState(s,event.state,event.hp,event.maxHp,event.ammo,event.passengers) || !meetsEventFormula(s,event) ||
                       (!includes(s.houses,"all") && !includes(s.houses,event.relation)) ||
                       (!includes(s.kinds,"all") && !includes(s.kinds,event.kind))) continue;
                    auto candidate=WeaponTypeClass::Find(s.actions[0].weapon.c_str());
                    volleyWeapons[u->UniqueID]={slot,nullptr,static_cast<unsigned>(Unsorted::CurrentFrame)};
                    if(!candidate || !candidate->Projectile || !candidate->Warhead || candidate->NeverUse) break;
                    volleyWeapons[u->UniqueID].replacement=candidate;
                    bool air=other && other->IsInAir();
                    if((air && !candidate->Projectile->AA) || (!air && !candidate->Projectile->AG) ||
                       u->GetFireError(aim,slot,false)!=FireError::OK || !u->IsCloseEnough(aim,slot))
                        volleyWeapons[u->UniqueID].replacement=nullptr;
                    break;
                }
            }
        }
        auto choice=volleyWeapons.find(u->UniqueID);
        if(choice!=volleyWeapons.end() && choice->second.slot==slot && choice->second.replacement) {
            auto weapon=choice->second.replacement;
            R->EBX(weapon);R->Stack(0x40,weapon);
        }
    }
    auto weapon=R->EBX<WeaponTypeClass*>();
    const bool missileOrder=weapon && weapon->Spawner && u->SpawnManager && u->SpawnManager->SpawnType && u->SpawnManager->SpawnType->MissileSpawn;
    if(!missileOrder) {
        if(auto e=firingEvent(u,R->Base<AbstractClass*>(8))) shots.emplace(key,std::move(*e));
    }
    return 0;
}
void commitFiringEvents(DWORD key,TechnoClass* unit) {
    if(auto it=shots.find(key);it!=shots.end()) {
        pending.push_back(std::move(it->second));
        shots.erase(it);
    }
    if(observeDeployments && alive(unit) && unit->WhatAmI()==AbstractType::Unit &&
        unit->CurrentMission==Mission::Unload && unit->GetTechnoType()->DeployFire &&
        !static_cast<UnitClass*>(unit)->Type->IsSimpleDeployer) capture("ondeploy",unit,nullptr,unit,unit,0);
}
// Delayed-fire checks and the native null/limbo guards have succeeded. Queue Lua
// outside the engine's firing stack, since the callback may remove either unit.
DEFINE_HOOK(0x6FDE0E,SF_ScriptedVolley,6) {
    if(weaponFireScripts.empty()) return 0;
    auto weapon=R->EBX<WeaponTypeClass*>();
    auto script=weaponFireScripts.find(weapon);
    if(script==weaponFireScripts.end()) return 0;
    auto unit=R->ESI<TechnoClass*>();
    auto aim=R->EDI<AbstractClass*>();
    int slot=R->Base<int>(12);
    if(!alive(unit) || unit->RearmTimer.GetTimeLeft()>0 ||
       unit->GetFireError(aim,slot,false)!=FireError::OK) return 0x6FDE03;
    auto victim=(aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
    auto e=eventSnapshot("onweaponfire",unit,victim,unit,victim,0);
    e.impact=aim->GetCoords();e.hasImpact=true;
    e.snapshot.push_back(script->second->id);
    pending.push_back(std::move(e));
    // Query the full-volley ROF through the virtual getter, including modifiers.
    unit->CurrentBurstIndex=std::max(1,weapon->Burst);
    const int delay=std::max(1,unit->GetROF(slot));
    unit->CurrentBurstIndex=0;
    unit->ChargeTurretDelay=delay;
    unit->RearmTimer.Start(delay);
    unit->LastFireBulletFrame=Unsorted::CurrentFrame;
    unit->DecreaseAmmo();
    UnitOrders::shot(unit,aim);
    commitFiringEvents(R->EBP(),unit);
    if(weapon->DecloakToFire && unit->CloakState!=CloakState::Uncloaked) unit->Uncloak(false);
    if(weapon->FireOnce) unit->SetTarget(nullptr);
    return 0x6FDE03;
}
// TechnoClass::Fire reaches this branch only after the original bullet's MoveTo succeeds.
DEFINE_HOOK(0x6FF031,SF_MultiShotLaunch,10) {
    auto unit=R->ESI<TechnoClass*>();
    auto bullet=R->EBX<BulletClass*>();
    auto origin=R->Stack<CoordStruct>(0x44);
    auto tracer=ArtSp::Tracers::onShot(unit->UniqueID,bullet,origin,Unsorted::CurrentFrame);
    ArtSp::ContinuousBeam::takeExtras(R->EBP());
    if(anyMultiShot && !applyingDamage && passiveDetonations.empty()) {
        const int slot=R->Base<int>(12);
        const auto muzzle=FireOrigin::enabled(bullet->WeaponType)?unit->GetFLH(slot,CoordStruct::Empty):origin;
        multiShot(R->EBP(),unit,bullet,R->Base<AbstractClass*>(8),slot,muzzle,tracer);
    }
    return 0;
}
// Weapon visual dispatch, after the muzzle flash and before shot bookkeeping.
// EDI is the visual target (potentially replaced by a blocking cell).
DEFINE_HOOK(0x6FF4D2,SF_ContinuousBeam,8) {
    auto extras=ArtSp::ContinuousBeam::takeExtras(R->EBP());
    auto firer=R->ESI<TechnoClass*>();
    auto weapon=R->EBX<WeaponTypeClass*>();
    int slot=R->Base<int>(12);
    auto origin=R->Stack<CoordStruct>(0x44);
    const bool relative=FireOrigin::enabled(weapon);
    const auto muzzle=relative?firer->GetFLH(slot,CoordStruct::Empty):origin;
    if(ArtSp::ContinuousBeam::onShot(firer,R->EDI<AbstractClass*>(),weapon,slot,muzzle,extras)) return 0x6FF656;
    for(auto target:extras)
        drawExtraWeaponVisual(firer,target,slot,weapon,FireOrigin::resolve(weapon,target,muzzle));
    if(!R->AL() && weapon->IsElectricBolt && relative) {
        // A native attached bolt updates its muzzle from the firer on every draw.
        drawDetachedBolt(firer,R->EDI<AbstractClass*>(),weapon,origin);
        return 0x6FF656;
    }
    return R->AL()?0x6FF4DA:0x6FF57D;
}
DEFINE_HOOK(0x6FF660,SF_ShotCommitted,6) {
    auto firer=R->ESI<TechnoClass*>();
    if(UnitOrders::controlled(firer)) {
        auto mount=firer->GetWeapon(R->Base<int>(12));
        if(mount && mount->WeaponType && !(mount->WeaponType->Spawner && firer->SpawnManager &&
           firer->SpawnManager->SpawnType && firer->SpawnManager->SpawnType->MissileSpawn))
            UnitOrders::shot(firer,R->Base<AbstractClass*>(8));
    }
    commitFiringEvents(R->EBP(),R->ESI<TechnoClass*>());
    return 0;
}
DEFINE_HOOK(0x739956,SF_DeployedInto,6) {
    auto old=R->EBP<UnitClass*>(); auto building=R->EBX<BuildingClass*>();
    vector<string> ids;
    if(old && building && interested(old,"ondeploy",&ids)) {
        auto e=eventSnapshot("ondeploy",old,nullptr,building,building,0);
        e.holder=building->UniqueID;e.snapshot=std::move(ids);pending.push_back(std::move(e));
    }return 0;
}
// Only successful factory placement emits OnBuilt. Generic Unlimbo/Place also
// run for preplaced, captured, transformed and spawned buildings, so never use
// those as construction notifications. Both points follow native/Phobos Unlimbo.
void buildingProduced(TechnoClass* unit) {
    if(alive(unit) && unit->IsOnMap && !unit->InLimbo && unit->WhatAmI()==AbstractType::Building &&
       BuildingSpawn::supported(static_cast<BuildingClass*>(unit)->Type)) {
        EntityWalls::built(static_cast<BuildingClass*>(unit));
        capture("onbuilt",unit,nullptr,unit,unit,0);
    }
}
DEFINE_HOOK(0x4FB248,SF_BuildingProducedHuman,6) {
    buildingProduced(R->ESI<TechnoClass*>());return 0;
}
DEFINE_HOOK(0x4452F0,SF_BuildingProducedAI,6) {
    buildingProduced(R->EDI<TechnoClass*>());return 0;
}
// Unlimbo has accepted the upgrade and stored its type in the host's slot;
// the temporary upgrade object is still alive at this point.
DEFINE_HOOK(0x4409E9,SF_BuildingUpgradeInstalled,6) {
    auto upgrade=R->ESI<BuildingClass*>();
    auto host=R->EDI<BuildingClass*>();
    if(!upgrade || !upgrade->Type || !alive(host)) return 0;
    vector<string> ids;
    auto held=boundSkills(upgrade->Type->ID);
    refreshLinkedPassives(upgrade);
    if(auto links=linkedGrants.find(upgrade->UniqueID);links!=linkedGrants.end())
        for(const auto& id:links->second) if(!includes(held,id)) held.push_back(id);
    for(const auto& id:held) {
        auto it=skills.find(id);
        if(it!=skills.end() && it->second.trigger=="onbuildingupgrade") ids.push_back(id);
    }
    if(!ids.empty()) {
        auto e=eventSnapshot("onbuildingupgrade",host,host,host,host,0);
        e.snapshot=std::move(ids);
        for(const auto& id:e.snapshot) e.stackSnapshot[id]=1;
        pending.push_back(std::move(e));
    }
    return 0;
}
DEFINE_HOOK(0x701CC2,SF_DamageWarheadOwner,6) {
    return ownerDamageOverride==R->EBX<int*>()?0x701CD7:0;
}
// Preserve the original sign before Phobos and native damage adjustments.
// The entry stack address distinguishes nested damage calls.
DEFINE_HOOK(0x701900,SF_DamageInput,6) {
    if(anyBeforeDealDamage || anyBeforeTakeDamage) {
        auto amount=R->Stack<int*>(4);
        incomingDamage[R->ESP()]={R->ECX<TechnoClass*>(),amount,!amount || *amount<=0};
    }
    return 0;
}
// These two points straddle ObjectClass::ReceiveDamage. Armor and overkill
// are accounted for by measuring the actual reduction in hit points.
// ObjectClass::ReceiveDamage in gamemd 1.001. Its damage/armor math operates in
// real hit points. Publish the native-scale Health only at the two write sites.
DEFINE_HOOK(0x5F53E5,SF_DamageMaxHealth,6) {
    auto obj=R->ESI<ObjectClass*>();
    if(auto p=UnitHealth::find(obj->UniqueID)) {
        p->sync(static_cast<TechnoClass*>(obj),R->EAX<ObjectTypeClass*>()->Strength);
        R->Stack(0x14,p->current);
        if(R->EDI<int*>()==&obj->Health) {
            // Some forced kills pass &Health itself. Keep the damage operand separate.
            R->Stack(0x18,p->current);
            R->EDI(R->lea_Stack<int*>(0x18));
        }
        R->EBP(p->maximum);
        return 0x5F53EB;
    }
    return 0;
}
DEFINE_HOOK(0x5F546A,SF_NativeHealMaxHealth,5) {
    auto obj=R->ESI<ObjectClass*>();
    if(auto p=UnitHealth::find(obj->UniqueID)) {
        int old=p->current;
        int next=static_cast<int>(std::min<int64_t>(p->maximum,int64_t(old)-R->ECX<int>()));
        p->set(static_cast<TechnoClass*>(obj),next);
        return next!=old?0x5F5480:0x5F548C;
    }
    return 0;
}
// This positive-damage branch runs after native armor and immunity checks,
// but before the engine clamps damage to health or decides lethal state.
DEFINE_HOOK(0x5F5498,SF_BeforeTakeDamage,12) {
    auto object=R->ESI<ObjectClass*>();
    auto amount=R->EDI<int*>();
    const bool enabled=anyBeforeDealDamage || anyBeforeTakeDamage;
    bool nonpositiveInput=false;
    if(enabled && R->Stack<DWORD>(0x24)==0x701DFD) {
        auto found=incomingDamage.find(R->ESP()+0x108);
        if(found!=incomingDamage.end()) {
            nonpositiveInput=found->second.unit==object && found->second.amount==amount && found->second.nonpositive;
            incomingDamage.erase(found);
        }
    }
    if(enabled && !nonpositiveInput && !(R->EBX<DWORD>()&0xff) && object && amount && *amount>0 &&
       (object->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None) {
        auto source=R->Stack<ObjectClass*>(0x34);
        auto attacker=source && (source->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None ?
            static_cast<TechnoClass*>(source) : nullptr;
        auto victim=static_cast<TechnoClass*>(object);
        if(alive(victim)) {
            int damage=*amount;
            if(anyBeforeDealDamage && alive(attacker))
                damage=adjustPendingDamage("onbeforedealdamage",attacker,victim,attacker,victim,damage);
            if(anyBeforeTakeDamage && damage>0)
                damage=adjustPendingDamage("onbeforetakedamage",victim,attacker,attacker,victim,damage);
            *amount=damage;
            // The native lethal clamp compares ECX before subtracting the damage operand.
            R->ECX(damage);
        }
        if(!*amount) return 0x5F545C;
    }
    R->EDX(R->Stack<int>(0x14));
    R->Stack(0x10,1);
    return 0x5F54A4;
}
DEFINE_HOOK(0x5F5505,SF_NativeDamageMaxHealth,6) {
    auto obj=R->ESI<ObjectClass*>();
    if(auto p=UnitHealth::find(obj->UniqueID)) {
        int damage=*R->EDI<int*>();
        // Subsequent native notifications compare the old and new display-scale fields.
        R->Stack(0x14,p->native);
        p->set(static_cast<TechnoClass*>(obj),std::max(0,R->EDX<int>()-damage));
        R->EAX(p->native);
        R->EDX(damage);
        return 0x5F550B;
    }
    return 0;
}
namespace { std::map<DWORD,int> healthBefore; }
DEFINE_HOOK(0x701DCC, SF_BeforeDamage, 7) {
    healthBefore[R->ESP()]=UnitHealth::current(R->ESI<TechnoClass*>());
    return 0;
}
DEFINE_HOOK(0x701DFF, SF_AfterDamage, 7) {
    auto i=healthBefore.find(R->ESP());
    if(i==healthBefore.end()) return 0;
    auto victim=R->ESI<TechnoClass*>(), attacker=R->Stack<TechnoClass*>(0xD4);
    int damage=i->second-std::max(UnitHealth::current(victim),0);
    healthBefore.erase(i);
    // Some engine attacks deliver damage directly without detonating a bullet.
    if(damage!=0 && !grantRules.empty()) {
        auto wh=R->Stack<WarheadTypeClass*>(0xD0);
        if(auto found=grantRules.find(wh);found!=grantRules.end())
            tryGrantUnit(victim,wh,found->second,attacker?attacker->Owner:R->Stack<HouseClass*>(0xE0),attacker);
    }
    if(R->EDI<int>()==static_cast<int>(DamageState::NowDead)) queueDeath(victim,attacker);
    if(damage>0) {
        capture("ondealdamage",attacker,victim,attacker,victim,damage);
        capture("ontakedamage",victim,attacker,attacker,victim,damage);
    }
    return 0;
}
// Each site reads ObjectTypeClass::Immune into CL. Leave the native type
// check intact for other objects; forced damage already bypasses this read.
DEFINE_HOOK_AGAIN(0x417F59,SF_ImmuneFlag,6) // aircraft attack cursor: target in EDI
DEFINE_HOOK_AGAIN(0x44751D,SF_ImmuneFlag,6) // building attack cursor: target in EBP
DEFINE_HOOK_AGAIN(0x51F16F,SF_ImmuneFlag,6) // infantry attack cursor: target in ESI
DEFINE_HOOK_AGAIN(0x740490,SF_ImmuneFlag,6) // vehicle attack cursor: target in EDI
DEFINE_HOOK_AGAIN(0x4423AD,SF_ImmuneFlag,6) // building-specific damage check: victim in ESI
DEFINE_HOOK(0x5F53CD,SF_ImmuneFlag,6) { // common damage check: victim in ESI
    if(immuneUnits.empty() && UnitMount::links.empty()) return 0;
    const auto site=R->Origin();
    ObjectClass* object=R->ESI<ObjectClass*>();
    if(site==0x417F59 || site==0x740490) object=R->EDI<ObjectClass*>();
    else if(site==0x44751D) object=R->EBP<ObjectClass*>();
    if(!immuneUnits.contains(object->UniqueID)) {
        auto it=UnitMount::links.find(object->UniqueID);
        if(it==UnitMount::links.end() || !alive(it->second.parent)) return 0;
        auto parent=it->second.parent;
        if(!immuneUnits.contains(parent->UniqueID) && !parent->IsIronCurtained() && !parent->GetTechnoType()->Immune) return 0;
    }
    R->CL(1);
    return site+6;
}
// Stop ordinary mind-control projectiles before Phobos replaces the capture operation.
DEFINE_HOOK(0x46921F,SF_MindImmuneBullet,6) {
    auto bullet=R->ESI<BulletClass*>();
    auto target=bullet?bullet->Target:nullptr;
    if(target && (target->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
       (UnitMount::mounted(target) || mindImmuneUnits.contains(static_cast<TechnoClass*>(target)->UniqueID))) return 0x469AA4;
    return 0;
}
// The engine and Phobos both expose capture eligibility through this entry.
DEFINE_HOOK(0x471C90,SF_MindImmuneCanCapture,6) {
    auto target=R->Stack<TechnoClass*>(4);
    if(target && (UnitMount::mounted(target) || mindImmuneUnits.contains(target->UniqueID))) {R->AL(0);return 0x471D39;}
    return 0;
}
DEFINE_HOOK(0x471D40,SF_MindImmuneCapture,7) {
    auto target=R->Stack<TechnoClass*>(4);
    if(target && (UnitMount::mounted(target) || mindImmuneUnits.contains(target->UniqueID))) {R->AL(0);return 0x471D5A;}
    return 0;
}
// Native Psychic Dominator selection; its damage is handled independently.
DEFINE_HOOK(0x53B270,SF_MindImmuneDominator,6) {
    auto target=R->ESI<TechnoClass*>();
    return target && (UnitMount::mounted(target) || mindImmuneUnits.contains(target->UniqueID))?0x53B364:0;
}
// Extend the base capability query, including FootClass's qualified base call.
// The caller still applies CloakStop, detection, firing and native cloak timers.
// Never write the object's Cloakable flag: crates, type conversion and other
// extensions retain ownership of their own capability, including in saves.
DEFINE_HOOK(0x70C5A0, SF_CloakableFlag, 6) {
    auto unit=R->ECX<TechnoClass*>();
    if(unit && cloakableUnits.contains(unit->UniqueID)) {R->AL(1);return 0x70C5AB;}
    return 0;
}
// Run the native mirage update for affected vehicles without changing their shared type.
DEFINE_HOOK(0x736486, SF_MirageUpdate, 6) {
    auto unit=R->ESI<UnitClass*>();
    return unit && mirageUnits.contains(unit->UniqueID)?0x73649A:0;
}
// The native update still checks movement, nearby detectors, firing and its own timers.
DEFINE_HOOK(0x7468FB, SF_MirageWhenStill, 6) {
    auto unit=R->ESI<UnitClass*>();
    return unit && mirageUnits.contains(unit->UniqueID)?0x74690B:0;
}
void clearCompletedVolleys() {
    if(volleyWeapons.empty()) return;
    unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    for(auto unit:TechnoClass::Array) if(unit) {
        auto it=volleyWeapons.find(unit->UniqueID);
        if(it!=volleyWeapons.end() && (!alive(unit) || (unit->CurrentBurstIndex==0 && it->second.frame!=frame))) volleyWeapons.erase(it);
    }
}
