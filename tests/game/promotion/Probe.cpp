#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {
    std::ofstream("promotion.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void checkKillTraining(UnitClass* unit) {
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    check(foe && !unit->Owner->IsAlliedWith(foe),"enemy for training");
    auto victim=static_cast<UnitClass*>(unit->Type->CreateObject(foe));
    check(victim!=nullptr,"training victim");
    auto recordKill=[&] {
        const auto score=unit->Owner->PointTotal;
        unit->Veterancy.Reset();
        victim->RegisterDestruction(unit);
        return std::pair{unit->Veterancy.Veterancy,unit->Owner->PointTotal-score};
    };
    const auto [base,score]=recordKill();
    check(base>0 && score>0,"native experience and score");
    grantToUnit(unit,PassiveGrant{{"killtraining","trainingformula"},-1});
    tickStates();
    const auto [boosted,boostedScore]=recordKill();
    check(std::abs(boosted-base*3)<0.0001 && boostedScore==score,"stacked kill experience preserves score");
    grantToUnit(unit,PassiveGrant{{"notraining"},-1});
    tickStates();
    const auto [zero,zeroScore]=recordKill();
    check(zero==0 && zeroScore==score,"zero experience preserves score");
    warheadGrants.erase(unit->UniqueID);
    tickStates();
    check(recordKill().first==base,"experience restored after removal");
    victim->UnInit();
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPromotion,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && frame>=25) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if((!loading && frame!=20) || (loading && frame!=30)) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    DWORD id=0;int expected=0;if(loading) std::ifstream("promotion.id")>>id>>expected;
    UnitClass *unit=nullptr,*other=nullptr;
    for(auto u:UnitClass::Array) if(u->Owner==house && !_stricmp(u->Type->ID,"MTNK")) {
        if((!id || id==u->UniqueID) && !unit) unit=u;
        else if(!other) other=u;
    }
    check(unit && other,"units");
    if(!loading) {
        checkKillTraining(unit);
        check(skills.contains("anypromotion") && skills.contains("veteranpromotion") && skills.contains("elitepromotion") && observePromotions,"configuration");
        grantToUnit(unit,PassiveGrant{{"anypromotion","veteranpromotion","elitepromotion"},-1});
        unit->Veterancy.Veterancy=0;previousState.erase(unit->UniqueID);observeTransitions();dispatch();
        int start=Research::balance(house->ArrayIndex);
        observeTransitions();dispatch();check(Research::balance(house->ArrayIndex)==start,"baseline");
        unit->Veterancy.Veterancy=1;observeTransitions();dispatch();
        check(Research::balance(house->ArrayIndex)==start+11,"veteran");
        unit->Veterancy.Veterancy=2;observeTransitions();dispatch();
        check(Research::balance(house->ArrayIndex)==start+112,"elite");
        unit->Veterancy.Veterancy=0;observeTransitions();dispatch();
        unit->Veterancy.Veterancy=2;observeTransitions();dispatch();
        check(Research::balance(house->ArrayIndex)==start+213,"direct elite");
        grantToUnit(other,PassiveGrant{{"veteranpromotion"},-1});
        other->Veterancy.Veterancy=0;previousState.erase(other->UniqueID);observeTransitions();dispatch();
        other->Veterancy.Veterancy=1;observeTransitions();dispatch();
        check(Research::balance(house->ArrayIndex)==start+223,"specific only");
        unit->Veterancy.Veterancy=0;observeTransitions();dispatch();
        unit->Veterancy.Veterancy=1;observeTransitions();
        std::ofstream("promotion.id")<<unit->UniqueID<<" "<<start+234;
        check(ScenarioClass::SaveGame("Saved Games\\Promotion.sav",L"Promotion"),"saved");
        dispatch();
    } else {
        dispatch();check(Research::balance(house->ArrayIndex)==expected,"queued promotion after load");
        observeTransitions();dispatch();check(Research::balance(house->ArrayIndex)==expected,"no repeated promotion");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
