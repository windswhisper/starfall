#include "../../../src/Starfall.cpp"
int laserCount=0;
DEFINE_HOOK(0x54FE60,SF_TestExtraLasers,6) {
    ++laserCount;
    return 0;
}
namespace {
void check(bool ok,const char* name) {
    std::ofstream("multishot.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
UnitClass* place(HouseClass* owner,CellStruct p) {
    auto unit=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(owner));
    check(unit && unit->Unlimbo(MapClass::Instance.GetCellAt(p)->GetCoordsWithBridge(),DirType::North),"place");
    if(owner!=HouseClass::CurrentPlayer) unit->Health=unit->EstimatedHealth=20000;
    return unit;
}
void fire(UnitClass* shooter,UnitClass* primary,std::set<DWORD> expected,const char* label) {
    shooter->SetTarget(primary);shooter->RearmTimer.Stop();
    auto facing=shooter->GetTargetDirection(primary);
    shooter->PrimaryFacing.SetCurrent(facing);shooter->SecondaryFacing.SetCurrent(facing);
    auto fireError=shooter->GetFireError(primary,0,false);
    int before=triggerTotals[shooter->UniqueID]["countshots"];
    std::set<DWORD> old;
    for(auto b:BulletClass::Array) old.insert(b->UniqueID);
    auto main=shooter->Fire(primary,0);
    for(auto b:BulletClass::Array) if(b->Owner==shooter && b->Target==primary && !old.contains(b->UniqueID)) main=b;
    if(!main || main->Target!=primary) std::ofstream("multishot.log",std::ios::app)<<label<<" primary error "<<int(fireError)
        <<" weapon "<<shooter->GetWeapon(0)->WeaponType->ID<<" distance "<<shooter->DistanceFrom(primary)<<" bullets "<<BulletClass::Array.Count<<"\n";
    check(main && main->Target==primary,"primary bullet");
    std::set<DWORD> actual;int size=0;
    for(auto b:BulletClass::Array) if(b->Owner==shooter && !old.contains(b->UniqueID)) {
        ++size;check(b->WeaponType==main->WeaponType && b->Health==main->Health && b->WH==main->WH &&
            b->Type==main->Type && b->Target,"same weapon and damage");
        actual.insert(b->Target->UniqueID);
    }
    dispatch();
    std::ofstream("multishot.log",std::ios::app)<<label<<" bullets "<<size<<" expected "<<expected.size()<<"\n";
    if(actual!=expected) {
        std::ofstream out("multishot.log",std::ios::app);
        for(auto id:expected) if(!actual.contains(id)) {
            for(auto u:TechnoClass::Array) if(u->UniqueID==id)
                out<<"missing "<<u->GetTechnoType()->ID<<" hp "<<u->Health<<" err "<<int(shooter->GetFireError(u,0,false))<<" dist "<<shooter->DistanceFrom(u)<<"\n";
        }
    }
    check(shooter->Target==primary && actual==expected && size==static_cast<int>(expected.size()) &&
          triggerTotals[shooter->UniqueID]["countshots"]==before+1,label);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestMultiShot,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    auto f=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && f==20) {
        Research::ensureReady();
        check(skills.contains("splitfire") && !skills.contains("invalidsplit") &&
            !skills.contains("badcount") && !skills.contains("badformula"),"configuration");
        auto human=HouseClass::CurrentPlayer,foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        auto u=place(human,{45,73}),p=place(foe,{47,73});
        place(human,{45,72});place(foe,{55,73});
        auto hidden=place(foe,{44,73});hidden->CloakState=CloakState::Cloaked;
        grantToUnit(u,PassiveGrant{{"splitfire","countshots"},-1});
        u->FirepowerMultiplier=1.5;
        fire(u,p,{p->UniqueID},"zero stacks");
        changeStacks(u->UniqueID,"splitfire",1,6);
        fire(u,p,{p->UniqueID},"lone target");
        auto n=place(foe,{46,73}),q=place(foe,{45,76});
        fire(u,p,{p->UniqueID,n->UniqueID},"nearest extra");
        changeStacks(u->UniqueID,"splitfire",1,6);
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"two distinct extras");
        auto weapon=u->GetWeapon(0)->WeaponType;
        bool laser=weapon->IsLaser;
        weapon->IsLaser=true;laserCount=0;
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"laser targets");
        check(laserCount==3,"three laser beams");
        weapon->IsLaser=laser;
        changeStacks(u->UniqueID,"splitfire",4,6);
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"fewer enemies than layers");
        u->Health=1;
        fire(u,p,{p->UniqueID},"conditional exit");
        auto third=place(foe,{45,77});
        grantToUnit(u,PassiveGrant{{"fixedshot"},-1});
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID,third->UniqueID},"three extra without stacks");
        grantToUnit(u,PassiveGrant{{"formulashot"},-1});
        check(multiShotTargetCount(u,p)==4,"fixed plus formula");
        removeHeldPassive(u,"fixedshot");
        changeStacks(u->UniqueID,"formulashot",1,6);
        fire(u,p,{p->UniqueID,n->UniqueID},"fraction rounded down");
        changeStacks(u->UniqueID,"formulashot",1,6);
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"formula follows stacks");
        removeHeldPassive(u,"formulashot");
        third->Limbo();
        u->Health=u->Type->Strength;
        hidden->Limbo();
        std::ofstream("multishot.ids")<<u->UniqueID<<" "<<p->UniqueID<<" "<<n->UniqueID<<" "<<q->UniqueID;
        check(ScenarioClass::SaveGame("Saved Games\\MultiShot.sav",L"MultiShot"),"saved");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(loading && f==21) {
        DWORD uid,pid,nid,qid;std::ifstream("multishot.ids")>>uid>>pid>>nid>>qid;
        UnitClass *u=nullptr,*p=nullptr;
        for(auto unit:UnitClass::Array) {
            if(unit->UniqueID==uid) u=unit;
            if(unit->UniqueID==pid) p=unit;
        }
        check(u && p && stackCount(uid,"splitfire")==6,"restored units and stacks");
        u->Type->Ammo=1;
        u->Ammo=1;
        fire(u,p,{pid,nid,qid},"restored layered shot");
        check(u->Ammo==0,"one round consumed");
        u->Ammo=1;
        removeHeldPassive(u,"splitfire");
        fire(u,p,{pid},"removed passive");
        for(auto unit:UnitClass::Array) unit->Deactivate();
    }
    if(loading && f==90) {
        DWORD uid,pid,nid,qid;
        std::ifstream("multishot.ids")>>uid>>pid>>nid>>qid;
        std::set<DWORD> hit;
        for(auto unit:UnitClass::Array)
            if((unit->UniqueID==nid || unit->UniqueID==qid) && unit->Health<20000) hit.insert(unit->UniqueID);
        check(hit.size()==2,"both extra targets took damage");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
