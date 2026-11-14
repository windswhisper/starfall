#include "../../../src/Starfall.cpp"
#include <SuperClass.h>

namespace {
void checkImmune(bool ok,const char* label) {
    std::ofstream("immune.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
int damageUnit(UnitClass* unit,UnitClass* enemy,bool forced=false) {
    int damage=24;
    int before=unit->Health;
    unit->ReceiveDamage(&damage,0,WarheadTypeClass::Find("AP"),enemy,forced,false,enemy->Owner);
    return before-unit->Health;
}
void controlShot(UnitClass* controller,UnitClass* target,const char* name="MindControl") {
    auto weapon=WeaponTypeClass::Find(name);
    checkImmune(weapon && weapon->Projectile && weapon->Warhead,"mind weapon");
    auto bullet=weapon->Projectile->CreateBullet(target,controller,weapon->Damage,weapon->Warhead,weapon->Speed,false);
    checkImmune(bullet!=nullptr,"mind bullet created");
    bullet->SetWeaponType(weapon);
    auto at=target->GetCoords();bullet->SetLocation(at);bullet->Detonate(at);bullet->UnInit();
}
}
DEFINE_HOOK(0x55B4EB,SF_TestImmunity,5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    const int f=Unsorted::CurrentFrame;
    if(f>=88) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if(f!=18 && f!=23 && f!=45 && f!=56 && f!=64 && f!=82) return 0;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    UnitClass *tank=nullptr,*peer=nullptr,*enemy=nullptr;
    auto human=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    for(auto u:UnitClass::Array) {
        if(!enemy && u->Owner==HouseClass::FindByPlayerAt(HouseClass::PlayerAtB)) enemy=u;
        if(u->Owner!=human || _stricmp(u->Type->ID,"MTNK")) continue;
        if((warheadGrants.contains(u->UniqueID) && warheadGrants.at(u->UniqueID).skills.contains("lowhealthimmune")) || (!tank && f==18)) tank=u;
        else if(!peer) peer=u;
    }
    checkImmune(tank && peer && enemy && tank->Type==peer->Type,"test units");
    if(f==18) {
        tank->Health=tank->Type->Strength/2;
        grantToUnit(tank,PassiveGrant{{"lowhealthimmune"},-1});
        grantToUnit(tank,PassiveGrant{{"stationaryimmune"},60});
    }
    if(f==23) {checkImmune(damageUnit(tank,enemy)>0,"delayed entry");checkImmune(!mindImmuneUnits.contains(tank->UniqueID),"mind delay");}
    if(f==45 && !loading) {
        checkImmune(!tank->Type->Immune && damageUnit(tank,enemy)==0 && damageUnit(peer,enemy)>0,"same type isolation");
        int hp=tank->Health;
        MapClass::DamageArea(tank->GetCoords(),40,enemy,WarheadTypeClass::Find("HE"),false,enemy->Owner);
        checkImmune(tank->Health==hp,"area damage");
        checkImmune(damageUnit(tank,enemy,true)>0,"bypass");
        auto cursor=enemy->MouseOverObject(tank,true);
        tank->Type->Immune=true;
        const auto native=enemy->MouseOverObject(peer,true);
        tank->Type->Immune=false;
        checkImmune(cursor==native,"native cursor");
        checkImmune(!tank->Type->ImmuneToPsionics && mindImmuneUnits.contains(tank->UniqueID) &&
            !mindImmuneUnits.contains(peer->UniqueID),"mind isolation");
        if(!enemy->CaptureManager) enemy->CaptureManager=GameCreate<CaptureManagerClass>(enemy,1,false);
        bool tankCan=enemy->CaptureManager && enemy->CaptureManager->CanCapture(tank);
        bool peerCan=enemy->CaptureManager && enemy->CaptureManager->CanCapture(peer);
        std::ofstream("immune.log",std::ios::app)<<"capture checks tank="<<tankCan<<" peer="<<peerCan<<"\n";
        checkImmune(!tankCan && peerCan,"mind can capture");
        checkImmune(!enemy->CaptureManager->CaptureUnit(tank) && tank->Owner==human,"mind direct blocked");
        checkImmune(enemy->CaptureManager->CaptureUnit(peer) && peer->Owner==enemy->Owner,"mind direct peer captured");
        checkImmune(enemy->CaptureManager->FreeUnit(peer) && peer->Owner==human,"mind direct peer released");
        controlShot(enemy,tank);
        checkImmune(!tank->IsMindControlled() && tank->Owner==human,"mind bullet blocked");
        controlShot(enemy,peer);
        checkImmune(peer->IsMindControlled() && peer->Owner==enemy->Owner,"mind peer captured");
        checkImmune(enemy->CaptureManager->FreeUnit(peer) && peer->Owner==human,"mind peer released");
        controlShot(enemy,tank,"SuperMindControl");
        checkImmune(!tank->IsMindControlled() && tank->Owner==human,"permanent mind bullet blocked");
        controlShot(enemy,peer,"SuperMindControl");
        checkImmune(peer->IsMindControlled() && peer->Owner==enemy->Owner,"permanent mind peer captured");
        peer->MindControlledByAUnit=false;peer->SetOwningHouse(human,false);
        auto oldOwner=PsyDom::Owner;auto oldCoords=PsyDom::Coords;
        auto rules=RulesClass::Instance;int oldDamage=rules->DominatorDamage;
        rules->DominatorDamage=0;PsyDom::Owner=enemy->Owner;PsyDom::Coords=tank->GetMapCoords();
        PsyDom::Fire();
        checkImmune(tank->Owner==human && !tank->IsMindControlled(),"mind dominator blocked");
        PsyDom::Coords=peer->GetMapCoords();PsyDom::Fire();
        checkImmune(peer->Owner==enemy->Owner && peer->IsMindControlled(),"mind dominator peer captured");
        peer->MindControlledByAUnit=false;peer->SetOwningHouse(human,false);
        rules->DominatorDamage=oldDamage;PsyDom::Owner=oldOwner;PsyDom::Coords=oldCoords;
        checkImmune(skills.contains("irontarget") && !skills.contains("invalidironduration") &&
                    !skills.contains("ironmissingduration"),"iron duration validation");
        grantToUnit(tank,PassiveGrant{{"irontarget"},-1});
        auto fire=[&](TechnoClass* target){capture("onfire",tank,target,tank,target,0);dispatch();};
        peer->IronCurtain(60,human,false);fire(peer);
        checkImmune(peer->IronCurtainTimer.GetTimeLeft()==60 && !triggerTotals[tank->UniqueID]["irontarget"],"longer iron preserved");
        peer->IronCurtain(10,human,true);fire(peer);
        checkImmune(peer->ForceShielded && peer->IronCurtainTimer.GetTimeLeft()==10 &&
                    !triggerTotals[tank->UniqueID]["irontarget"],"force shield preserved");
        peer->IronCurtain(5,human,false);fire(peer);
        checkImmune(!tank->IsIronCurtained() && peer->IsIronCurtained() && !peer->ForceShielded &&
                    peer->IronCurtainTimer.GetTimeLeft()==30 && triggerTotals[tank->UniqueID]["irontarget"]==1,"apply iron to target");
        checkImmune(skills.contains("testemp"),"EMP configuration");
        grantToUnit(peer,PassiveGrant{{"testemp","testemp2"},-1});
        tickStates();
        checkImmune(empLocked(peer) && !peer->IsControllable() && !ActiveSkills::readyCaster(peer,human),"EMP locks vehicle");
        auto aircraft=static_cast<AircraftClass*>(AircraftTypeClass::Find("ORCA")->CreateObject(human));
        checkImmune(aircraft && aircraft->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{42,72})->GetCoords(),DirType::North),"EMP aircraft placed");
        aircraft->SetHeight(1024);
        checkImmune(aircraft->IsInAir(),"EMP aircraft airborne");
        TargetClass airHandle(aircraft);
        grantToUnit(aircraft,PassiveGrant{{"testemp"},-1});
        tickStates();
        auto air=airHandle.As_Foot();
        checkImmune(!air || air->IsCrashing || !air->IsAlive || air->Health<=0,"EMP aircraft crashes");
        std::ofstream("iron-target.id")<<peer->UniqueID;
        checkImmune(ScenarioClass::SaveGame(GetModuleHandleA("CnCNet-Spawner.dll")?"Immune.sav":"Saved Games\\Immune.sav",L"Immune"),"saved");
    }
    if(f==56 || f==82) {
        DWORD id=0;std::ifstream("iron-target.id")>>id;
        UnitClass* protectedUnit=nullptr;
        for(auto u:UnitClass::Array) if(u->UniqueID==id) protectedUnit=u;
        checkImmune(protectedUnit!=nullptr,"iron target exists");
        if(f==56) {
            checkImmune(empLocked(protectedUnit) && !protectedUnit->IsControllable(),"EMP restored");
            removeHeldPassive(protectedUnit,"testemp");tickStates();
            checkImmune(empLocked(protectedUnit),"EMP overlapping source retained");
            removeHeldPassive(protectedUnit,"testemp2");tickStates();
            checkImmune(!empLocked(protectedUnit) && protectedUnit->IsControllable() &&
                protectedUnit->CurrentMission==Mission::Guard,"EMP release");
        }
        if(f==56) checkImmune(protectedUnit->IronCurtainTimer.GetTimeLeft()==19 && damageUnit(protectedUnit,enemy)==0,"iron timer and protection restored");
        else checkImmune(!protectedUnit->IsIronCurtained() && damageUnit(protectedUnit,enemy)>0,"iron expires naturally");
    }
    if(f==56) {
        checkImmune(damageUnit(tank,enemy)==0,"loaded immunity");
        checkImmune(mindImmuneUnits.contains(tank->UniqueID),"mind loaded");
        tank->Health=tank->Type->Strength;
    }
    if(f==64) {checkImmune(damageUnit(tank,enemy)==0,"one source left");checkImmune(mindImmuneUnits.contains(tank->UniqueID),"mind one source left");}
    if(f==82) {
        checkImmune(damageUnit(tank,enemy)>0,"immunity ends");
        checkImmune(!mindImmuneUnits.contains(tank->UniqueID),"mind immunity ends");
        checkImmune(enemy->CaptureManager && enemy->CaptureManager->CanCapture(tank),"mind capture after expiry");
        controlShot(enemy,tank);
        checkImmune(tank->IsMindControlled() && tank->Owner==enemy->Owner,"mind bullet after expiry");
        checkImmune(enemy->CaptureManager->FreeUnit(tank) && tank->Owner==human,"mind release after expiry");
        tank->Type->Immune=true;
        checkImmune(damageUnit(tank,enemy)==0 && damageUnit(tank,enemy,true)>0,"innate immunity preserved");
        tank->Type->Immune=false;
    }
    return 0;
}
