namespace InfantryGuardCheck {
constexpr CellStruct site{53,81}, station{55,81}, luaStation{56,81}, enemySite{58,81};
DWORD childId=0, enemyId=0;
bool shot=false, done=false;

InfantryClass* guard(const BuildingClass* post) {
    if(!post) return nullptr;
    const auto slot=GroundGuards::slots.find(post->UniqueID);
    if(slot==GroundGuards::slots.end() || !slot->second.child) return nullptr;
    TargetClass reference;
    reference.m_RTTI=static_cast<BYTE>(AbstractType::Abstract);
    reference.m_ID=slot->second.child;
    auto unit=reference.As_Infantry();
    return unit && unit->IsAlive && unit->Health>0 && unit->IsOnMap && !unit->InLimbo?unit:nullptr;
}

void tick(int frame,bool load,HouseClass* house) {
    static int lost=-1;
    if(done || frame<20) return;
    if(frame==20 && !load) placeBuilding("ASINFPOST",site,house);
    auto post=outpost(site);
    auto unit=guard(post);
    if(frame==50 && !load) {
        check(post && !post->SpawnManager && !unit,"infantry post awaits a ground rally");
        rally(post,station);
    }
    if(frame==119 && !load) {
        check(unit && unit->Owner==house && !_stricmp(unit->Type->ID,"E1"),"ground post creates native infantry");
        std::ofstream("infantry-guard-identity.txt")<<unit->UniqueID;
    }
    if(frame==160) {
        std::ifstream("infantry-guard-identity.txt")>>childId;
        check(unit && unit->UniqueID==childId && unit->GetMapCoords()==station && unit->GetHeight()==0,
            "same infantry guard reached its rally after save");
        check(grantToUnit(post,PassiveGrant{{"orderdrone"},-1}),"granted Lua infantry guard order");
        tickGains();
        dispatch();
    }
    if(frame==285) {
        check(unit && unit->UniqueID==childId && unit->GetMapCoords()==luaStation,
            "Lua retasked the same infantry guard");
        HouseClass* foe=nullptr;
        for(auto candidate:HouseClass::Array) if(candidate && !candidate->IsNeutral() && !candidate->Defeated &&
            !house->IsAlliedWith(candidate)) {
            foe=candidate;
            break;
        }
        check(foe!=nullptr,"infantry guard opponent");
        enemyId=placeBuilding("GAPOWR",enemySite,foe)->UniqueID;
    }
    if(lost>=0) {
        if(unit) {
            check(frame-lost>=48 && frame-lost<=64 && unit->UniqueID!=childId &&
                !_stricmp(unit->Type->ID,"E1"),"infantry guard is replaced after the configured delay");
            post->UnInit();
            check(unit->IsOnMap && unit->Owner==house,"surviving infantry guard stays with its owner after post removal");
            done=true;
        } else if(frame-lost>64) check(false,"infantry guard replacement was due");
    } else if(shot) {
        check(unit && unit->UniqueID==childId,"infantry guard fired its native weapon");
        auto enemy=outpost(enemySite);
        int damage=unit->Health+1000;
        unit->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
        if(enemy) enemy->UnInit();
        lost=frame;
    } else if(frame==440) check(false,"infantry guard did not attack within its guard range");
}
}
