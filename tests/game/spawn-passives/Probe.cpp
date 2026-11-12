#include "../../../src/Starfall.cpp"

namespace {
void check(bool ok,const char* label) {
    std::ofstream("spawns.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestSpawnPassives,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int frame=Unsorted::CurrentFrame;
    if(frame!=28 && frame!=37 && frame!=125) return 0;
    auto loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(frame==28 && loading) return 0;
    UnitClass *carrier=nullptr,*other=nullptr;
    for(auto u:UnitClass::Array) if(u->Owner==HouseClass::CurrentPlayer && u->SpawnManager && u->SpawnManager->SpawnedNodes.Count>=2) {
        if(!carrier) carrier=u;
        else if(u->Owner==carrier->Owner) {other=u;break;}
    }
    check(carrier && other,"two native carriers");
    auto child=carrier->SpawnManager->SpawnedNodes[0]->Unit;
    auto wing=carrier->SpawnManager->SpawnedNodes[1]->Unit;
    auto peer=other->SpawnManager->SpawnedNodes[0]->Unit;
    check(child && wing && peer,"native children");
    static DWORD original=0;
    if(frame==125) {
        check(child->UniqueID!=original && interested(child,"onfire"),"regenerated child inherits");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(frame==28) {
        check(skills.contains("carrierlink") && !skills.contains("badlink"),"validate references");
        grantToUnit(carrier,PassiveGrant{{"carrierlink"},-1});
        refreshSpawnGrants();
        check(ScenarioClass::SaveGame("Saved Games\\Spawns.sav",L"Spawn passives"),"saved");
    } else {
        check(interested(child,"onfire") && interested(wing,"onfire") && !interested(peer,"onfire"),"own children only");
        check(interested(child,"whilestate"),"child state skill");
        int balance=Research::balance(child->Owner->ArrayIndex);
        capture("onfire",child,peer,child,peer,0);dispatch();
        check(Research::balance(child->Owner->ArrayIndex)==balance+7,"child event");
        grantToUnit(wing,PassiveGrant{{"dronereward"},-1});
        carrier->Health=1;
        refreshSpawnGrants();
        check(!interested(child,"onfire") && interested(wing,"onfire"),"withdraw and preserve other source");
        carrier->Health=carrier->Type->Strength;
        refreshSpawnGrants();
        check(interested(child,"onfire"),"reactivate");
        original=child->UniqueID;
        carrier->SpawnManager->KillNodes();
        carrier->SpawnManager->ResetTarget();
    }
    return 0;
}
