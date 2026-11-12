#include "../../../src/Starfall.cpp"

namespace {
DWORD shooterID=0,targetID=0;
int originalReach=0;
bool distantShot=false;
void requireRange(bool ok,const char* label) {
    std::ofstream("range.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x6FF66C,SF_TestCommittedRangeShot,6) {
    auto firer=R->ESI<TechnoClass*>();
    auto aim=R->Base<AbstractClass*>(8);
    if(shooterID && firer->UniqueID==shooterID && aim && aim->UniqueID==targetID)
        distantShot|=firer->DistanceFrom(aim)>originalReach;
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestRangeFrame,5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    const int frame=Unsorted::CurrentFrame;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(frame>=110) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if(frame!=27 && frame!=95 && frame!=102) return 0;
    UnitClass* tank=nullptr;
    UnitClass* control=nullptr;
    for(auto u:UnitClass::Array) if(!_stricmp(u->Type->ID,"MTNK")) {
        if(!tank) tank=u; else {control=u;break;}
    }
    requireRange(tank && control,"two Grizzlies");
    auto weapon=tank->GetWeapon(0)->WeaponType;
    requireRange(weapon && control->GetWeapon(0)->WeaponType==weapon,"shared weapon");
    const int base=control->GetWeaponRange(0);
    if(frame==27 && !loading) {
        requireRange(tank->GetWeaponRange(0)==base && base>0,"original range");
        grantToUnit(tank,PassiveGrant{{"rangegrowth","rangestate","rangeloss"},-1});
        for(int i=0;i<3;++i) {
            capture("onkill",tank,control,tank,control,0);
            dispatch();
        }
        requireRange(tank->GetWeaponRange(0)==2*base,"event upper limit");
        tank->Health=tank->Type->Strength/2;
        tickStates();
        requireRange(tank->GetWeaponRange(0)==3*base,"two layers");
        requireRange(control->GetWeaponRange(0)==base && weapon->Range==base,"unmodified peer and weapon");
        // Check a cell two terrain tiles beyond the native range for each firer.
        auto distant=[](UnitClass* u,int range) {
            auto cell=u->GetMapCoords();cell.X+=static_cast<short>(2+range/256);
            return MapClass::Instance.GetCellAt(cell);
        };
        auto remoteCell=distant(tank,base);
        requireRange(tank->IsCloseEnough(remoteCell,0) && !control->IsCloseEnough(distant(control,base),0),"firing boundary");
        auto opponent=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        auto dummy=static_cast<UnitClass*>(tank->Type->CreateObject(opponent));
        requireRange(dummy && dummy->Unlimbo(remoteCell->GetCoordsWithBridge(),DirType::North),"placed enemy beyond base range");
        dummy->DiscoveredBy(tank->Owner);
        auto position=tank->GetCoords();
        auto target=tank->GreatestThreat(ThreatType::Range|ThreatType::Vehicles,&position,false);
        std::ofstream("range.log",std::ios::app)<<"ranges "<<base<<" "<<tank->GetWeaponRange(0)
            <<" enemy "<<tank->DistanceFrom(dummy)<<" scan "<<(target?tank->DistanceFrom(target):-1)<<"\n";
        requireRange(target && tank->DistanceFrom(target)>base && tank->IsCloseEnough(target,0),"acquired distant enemy");
        dummy->Deactivate();
        shooterID=tank->UniqueID;targetID=target->UniqueID;originalReach=base;
        tank->SetTarget(target);
        tank->QueueMission(Mission::Attack,true);
    }
    if(frame==95 && !loading) {
        requireRange(distantShot,"shot at distant enemy");
        requireRange(ScenarioClass::SaveGame("Saved Games\\WeaponRange.sav",L"WeaponRange"),"saved");
    }
    if(frame==102) {
        requireRange(tank->GetWeaponRange(0)==3*base && control->GetWeaponRange(0)==base,"retained per-unit range");
        tank->Health=tank->Type->Strength;tickStates();
        requireRange(tank->GetWeaponRange(0)==2*base,"state exit");
        for(int i=0;i<3;++i) {
            capture("onpromote",tank,nullptr,nullptr,nullptr,0);
            dispatch();
        }
        requireRange(tank->GetWeaponRange(0)==static_cast<int>(std::lround(base*0.5)),"event lower limit");
    }
    return 0;
}
