#include "../../../src/Starfall.cpp"

namespace {
std::array<DWORD,2> firingUnits{};
std::array<std::vector<int>,2> fireFrames;
void checkRate(bool success,const char* label) {
    std::ofstream("rate.log",std::ios::app)<<(success?"PASS ":"FAIL ")<<label<<"\n";
    if(!success) TerminateProcess(GetCurrentProcess(),2);
}
// Use the same random delay for each query without changing the match RNG.
int rearm(UnitClass* unit,int burstIndex) {
    const auto previous=unit->CurrentBurstIndex;
    const auto random=ScenarioClass::Instance->Random;
    unit->CurrentBurstIndex=burstIndex;
    int frames=unit->GetROF(0);
    unit->CurrentBurstIndex=previous;
    ScenarioClass::Instance->Random=random;
    return frames;
}
}

DEFINE_HOOK(0x6FF66C,SF_TestRateShot,6) {
    auto id=R->ESI<TechnoClass*>()->UniqueID;
    for(size_t i=0;i<firingUnits.size();++i)
        if(firingUnits[i]==id) fireFrames[i].push_back(Unsorted::CurrentFrame);
    return 0;
}

DEFINE_HOOK(0x55B4EB,SF_TestRateFrame,5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    const int f=Unsorted::CurrentFrame;
    if(f>=252) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if(f!=22 && f!=240 && f!=246) return 0;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    UnitClass *tank=nullptr,*peer=nullptr;
    for(auto u:UnitClass::Array) if(!_stricmp(u->Type->ID,"MTNK") && u->Owner==HouseClass::FindByPlayerAt(HouseClass::PlayerAtA)) {
        if(f==22 && !loading) {
            if(!tank) tank=u;else if(!peer) peer=u;
        } else if(fireRateBase.contains(u->UniqueID)) tank=u;
        else if(!peer) peer=u;
    }
    checkRate(tank && peer,"two Grizzlies");
    auto weapon=tank->GetWeapon(0)->WeaponType;
    checkRate(weapon && weapon==peer->GetWeapon(0)->WeaponType,"shared weapon");
    const int reference=rearm(peer,weapon->Burst);
    if(f==22 && !loading) {
        checkRate(reference>4 && rearm(tank,weapon->Burst)==reference,"unmodified rate");
        grantToUnit(tank,PassiveGrant{{"rategain","ratelowhealth","rateloss"},-1});
        for(int i=0;i<3;++i) {
            capture("onkill",tank,peer,tank,peer,0);
            dispatch();
        }
        checkRate(rearm(tank,weapon->Burst)==std::lround(reference/2.0),"upper limit");
        // Both test units must survive until the saved-game checks even if
        // the enemy AI counterattacks the damaged shooter.
        tank->IronCurtain(360,tank->Owner,false);
        peer->IronCurtain(360,peer->Owner,false);
        tank->Health=tank->Type->Strength/2;
        tickStates();
        checkRate(rearm(tank,weapon->Burst)==std::lround(reference/3.0),"stacked rate");
        checkRate(rearm(tank,0)==rearm(peer,0),"burst cadence");
        auto cell=tank->GetMapCoords();cell.Y+=3;cell.X+=1;
        auto opponent=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        auto target=static_cast<UnitClass*>(tank->Type->CreateObject(opponent));
        checkRate(target && target->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"placed target");
        target->Health=30000;target->Deactivate();target->DiscoveredBy(tank->Owner);
        firingUnits={tank->UniqueID,peer->UniqueID};
        for(auto unit:{tank,peer}) {unit->SetTarget(target);unit->QueueMission(Mission::Attack,true);}
    }
    if(f==240 && !loading) {
        auto mean=[](const std::vector<int>& frames) {
            return frames.size()>1?(frames.back()-frames.front())/double(frames.size()-1):0.0;
        };
        const double fast=mean(fireFrames[0]),normal=mean(fireFrames[1]);
        std::ofstream("rate.log",std::ios::app)<<"shot intervals "<<fast<<" / "<<normal<<" frames\n";
        checkRate(fireFrames[0].size()>=3 && fireFrames[1].size()>=3 && fast>0 && fast*2.5<normal,"real shots");
        checkRate(ScenarioClass::SaveGame("Saved Games\\FireRate.sav",L"FireRate"),"saved");
    }
    if(f==246) {
        std::ofstream("rate.log",std::ios::app)<<"after save shooter "<<tank->UniqueID<<" ref "<<peer->UniqueID
            <<" hp "<<tank->Health<<"/"<<tank->Type->Strength<<" stage "<<unitFactor(tank->UniqueID,fireRateBase,fireRateState)
            <<" base "<<reference<<" rearm "<<rearm(tank,weapon->Burst)<<"\n";
        checkRate(rearm(tank,weapon->Burst)==std::lround(reference/3.0),"retained rate");
        tank->Health=tank->Type->Strength;tickStates();
        checkRate(rearm(tank,weapon->Burst)==std::lround(reference/2.0),"state exit");
        for(int i=0;i<3;++i) {
            capture("ondeploy",tank,nullptr,nullptr,nullptr,0);
            dispatch();
        }
        checkRate(rearm(tank,weapon->Burst)==2*reference,"lower limit");
    }
    return 0;
}
