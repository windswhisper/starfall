#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {
    std::ofstream("condition-formula.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestConditionFormula,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && frame>=25) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if((!loading && frame!=20) || (loading && frame!=30)) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    DWORD id=0;if(loading) std::ifstream("condition-formula.id")>>id;
    UnitClass *u=nullptr,*enemy=nullptr;
    for(auto unit:UnitClass::Array) {
        if(!u && unit->Owner==house && !_stricmp(unit->Type->ID,"MTNK") && (!id || id==unit->UniqueID)) u=unit;
        if(!enemy && unit->Owner==foe) enemy=unit;
    }
    check(u && enemy,"units");
    if(!loading) {
        check(skills.contains("stackgate") && skills.contains("valuesgate") && skills.contains("snapshotgate") &&
              !skills.contains("badsyntax") && !skills.contains("oldhealth") && !skills.contains("baddamagestate"),"configuration");
        grantToUnit(u,PassiveGrant{{"stackgate","valuesgate","snapshotgate","eventdamagegate","targetgate"},-1});
        passiveStacks[u->UniqueID]["stackgate"]=4;tickStates();
        check(u->FirepowerMultiplier==1.0,"below stack threshold");
        passiveStacks[u->UniqueID]["stackgate"]=5;tickStates();
        check(u->FirepowerMultiplier==2.0,"inclusive stack threshold");
        passiveStacks[u->UniqueID]["stackgate"]=4;tickStates();
        check(u->FirepowerMultiplier==1.0,"stack withdrawal");
        auto oldAmmoType=u->Type->Ammo, oldAmmo=u->Ammo, oldPassengers=u->Passengers.NumPassengers;
        int max=UnitHealth::read(u).maximum;
        u->Type->Ammo=5;u->Ammo=2;u->Passengers.NumPassengers=1;UnitHealth::heal(u,max/2);
        tickStates();check(u->ArmorMultiplier==2.0,"ammo passengers health true");
        u->Ammo=1;tickStates();check(u->ArmorMultiplier==1.0,"ammo below threshold");
        u->Ammo=2;u->Passengers.NumPassengers=0;tickStates();check(u->ArmorMultiplier==1.0,"empty passenger false");
        u->Passengers.NumPassengers=1;UnitHealth::heal(u,max);tickStates();check(u->ArmorMultiplier==1.0,"health above threshold");
        u->Type->Ammo=-1;u->Ammo=-1;UnitHealth::heal(u,max/2);tickStates();check(u->ArmorMultiplier==1.0,"unlimited ammo minus one");
        check(u->SpeedMultiplier==1.0,"missing target false");
        u->SetTarget(enemy);tickStates();check(u->SpeedMultiplier==1.5,"live target condition");
        u->SetTarget(nullptr);tickStates();check(u->SpeedMultiplier==1.0,"lost target false");
        u->Type->Ammo=5;u->Ammo=3;u->Passengers.NumPassengers=1;UnitHealth::heal(u,max/2);
        int points=Research::balance(house->ArrayIndex);
        capture("onkill",u,enemy,u,enemy,0);
        u->Ammo=0;u->Passengers.NumPassengers=0;UnitHealth::heal(u,max);dispatch();
        check(Research::balance(house->ArrayIndex)==points+3,"event condition snapshot");
        u->Ammo=3;u->Passengers.NumPassengers=1;
        points=Research::balance(house->ArrayIndex);capture("ontakedamage",u,enemy,enemy,u,10);
        u->Ammo=0;u->Passengers.NumPassengers=0;dispatch();
        check(Research::balance(house->ArrayIndex)==points+4,"event numeric formula snapshot");
        u->Type->Ammo=oldAmmoType;u->Ammo=oldAmmo;u->Passengers.NumPassengers=oldPassengers;
        passiveStacks[u->UniqueID]["stackgate"]=5;tickStates();
        check(skills.contains("resetclock") && skills.contains("resetonkill"),"reset configuration");
        grantToUnit(u,PassiveGrant{{"resetclock","resetonkill"},-1});
        auto hp=UnitHealth::read(u);auto bits=stateOf(u);unsigned t=static_cast<unsigned>(Unsorted::CurrentFrame);
        auto& clock=skills.at("resetclock");
        check(!stateActive(u,clock,bits,hp,t-15) && stateActive(u,clock,bits,hp,t),"timer charged");
        capture("onkill",u,enemy,u,enemy,0);dispatch();
        check(!stateActive(u,clock,bits,hp,t),"event reset timer");
        check(stateActive(u,clock,bits,hp,t+15),"timer charges after reset");
        std::ofstream("condition-formula.id")<<u->UniqueID;
        check(ScenarioClass::SaveGame("Saved Games\\ConditionFormula.sav",L"Condition Formula"),"saved");
    } else {
        tickStates();
        check(stateTimers.contains({id,"resetclock"}),"reset timer survives save");
        check(stackCount(id,"stackgate")==5 && u->FirepowerMultiplier==2.0,"stack and condition loaded");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
