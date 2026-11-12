#include "../../../src/Starfall.cpp"

namespace {
void check(bool ok,const char* label) {
    std::ofstream("reapply.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void event(const char* trigger,TechnoClass* holder,TechnoClass* other) {
    capture(trigger,holder,other,holder,other,0);dispatch();dispatch();
}
void hit(UnitClass* shooter,UnitClass* target,WarheadTypeClass* wh) {
    auto weapon=WeaponTypeClass::Find("105mm");
    auto b=weapon->Projectile->CreateBullet(target,shooter,16,wh,10,false);
    check(b!=nullptr,"created test round");
    b->SetWeaponType(weapon);
    auto at=target->GetCoords();b->SetLocation(at);b->Detonate(at);b->UnInit();
}
}
DEFINE_HOOK(0x55B4EB,SF_TestReapply,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    bool loaded=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    int frame=Unsorted::CurrentFrame;
    if((!loaded && frame!=20 && frame!=22) || (loaded && frame!=24 && frame!=34)) return 0;
    DWORD id=0,pid=0;
    if(frame!=20) std::ifstream("reapply.ids")>>id>>pid;
    UnitClass *u=nullptr,*peer=nullptr,*enemy=nullptr;
    auto player=HouseClass::CurrentPlayer,foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    for(auto unit:UnitClass::Array) {
        if(unit->Owner==player && !_stricmp(unit->Type->ID,"MTNK")) {
            if(!u && (!id || unit->UniqueID==id)) u=unit;
            else if(!peer && (!pid || unit->UniqueID==pid)) peer=unit;
        } else if(unit->Owner==foe && !enemy) enemy=unit;
    }
    check(u && peer && enemy,"test units");
    id=u->UniqueID;pid=peer->UniqueID;
    if(frame==20) {
        auto wh=WarheadTypeClass::Find("AP");
        check(grantRules.contains(wh) && !grantRules.contains(WarheadTypeClass::Find("HE")) &&
            !skills.contains("wrongaction") && !skills.contains("badswitch"),"configuration");
        grantToUnit(u,PassiveGrant{{"single","double","fullprobe","defaultgrant","testaura"},-1});refreshAuras();
        event("ondeploy",u,enemy);event("onkill",u,enemy);
        check(stackCount(id,"stacked")==0 && triggerTotals[id]["stacked"]==1,"first grant and opt out");
        event("onpromote",u,enemy);
        check(stackCount(id,"stacked")==2 && triggerTotals[id]["stacked"]==1,"two actions two stacks");
        event("onfire",u,enemy);
        check(counts[id]["fullprobe"].next==0 && triggerTotals[id]["stacked"]==1,"no change no cooldown");
        float previous=wh->CellSpread;wh->CellSpread=1;
        const int hp=peer->Health;
        hit(enemy,peer,wh);
        check(peer->Health<hp && stackCount(pid,"shellbuff")==0 && stackCount(pid,"nativebuff")==1 &&
            stackCount(pid,"aurabuff")==1 && reapplyImpacts.empty(),"direct and area deduplicated");
        hit(enemy,peer,wh);
        MapClass::DamageArea(peer->GetCoords(),8,enemy,wh,false,foe);
        wh->CellSpread=0;
        hit(enemy,peer,wh);
        int damage=8;peer->ReceiveDamage(&damage,0,wh,enemy,false,false,foe);
        wh->CellSpread=previous;
        check(stackCount(pid,"shellbuff")==4 && stackCount(pid,"nativebuff")==5 && stackCount(pid,"aurabuff")==5 &&
            reapplyImpacts.empty(),"independent hits in one frame");
        std::ofstream("reapply.ids")<<id<<" "<<pid;
    } else if(frame==22) {
        event("onfire",u,enemy);
        check(stackCount(id,"stacked")==2 && counts[id]["fullprobe"].next==52 &&
            warheadGrants.at(id).skills.at("stacked").remaining(frame)==10 && triggerTotals[id]["stacked"]==1,"capped renewal");
        check(ScenarioClass::SaveGame("Saved Games\\Reapply.sav",L"Reapply"),"saved");
        TerminateProcess(GetCurrentProcess(),0);
    } else if(frame==24) {
        check(stackCount(id,"stacked")==2 && triggerTotals[id]["stacked"]==1 &&
            warheadGrants.at(id).skills.at("stacked").remaining(frame)==8 && stackCount(pid,"shellbuff")==4,"restored stacks and duration");
    } else {
        check(stackCount(id,"stacked")==0 && stackCount(pid,"shellbuff")==0 && stackCount(pid,"nativebuff")==5,
            "expiry preserves other sources");
        check(stackCount(pid,"aurabuff")==5,"aura refresh is not an application");
        event("ondeploy",u,enemy);
        check(stackCount(id,"stacked")==0 && triggerTotals[id]["stacked"]==2,"reacquired at zero");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
