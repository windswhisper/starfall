#include "../../../src/Starfall.cpp"

namespace {
void check(bool ok,const char* message) {
    std::ofstream("jumpjet-transform.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<message<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestJumpjetTransform,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int frame=Unsorted::CurrentFrame;
    if(frame<30) return 0;
    static UnitClass* unit=nullptr;
    static bool airborne=false,landed=false;
    auto drive=UnitTypeClass::Find("MTNK"),flight=UnitTypeClass::Find("SHAD");
    auto& map=MapClass::Instance;
    CellStruct start{56,80},end{62,80};
    if(frame==30) {
        auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
        unit=static_cast<UnitClass*>(drive->CreateObject(house));
        check(unit && unit->Unlimbo(map.GetCellAt(start)->GetCoords(),DirType::North),"placed");
        check(!canTransformUnit(unit,flight,nullptr) && !canTransformUnit(unit,UnitTypeClass::Find("ZEP")),"unsupported forms rejected");
        grantToUnit(unit,PassiveGrant{{"testtakeflight"},-1});
        capture("onpromote",unit,nullptr,unit,unit,0);dispatch();
        check(unit->Type==flight && locomotion_cast<JumpjetLocomotionClass*>(unit->Locomotor),"passive switched locomotor");
        unit->SetDestination(map.GetCellAt(end),true);unit->ForceMission(Mission::Move);
    } else if(!airborne && unit->IsInAir()) {
        check(!transformUnit(unit,drive) && unit->Type==flight,"airborne rejected");
        airborne=true;
    } else if(airborne && !landed && unit->GetMapCoords()==end && canTransformUnit(unit,drive)) {
        check(unit->GetHeight()==0,"landed at destination");
        check(transformUnit(unit,drive) && locomotion_cast<DriveLocomotionClass*>(unit->Locomotor),"reverse locomotor");
        landed=true;
        unit->SetDestination(map.GetCellAt(start),true);unit->ForceMission(Mission::Move);
    } else if(landed && unit->GetMapCoords()==start) {
        check(map.GetCellAt(end)->Jumpjet!=unit,"released landing cell");
        check(true,"reverse and drive");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(frame>=900) check(false,"movement timed out");
    return 0;
}
