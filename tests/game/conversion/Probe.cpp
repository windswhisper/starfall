#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* label) {
    std::ofstream("conversion.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x640A49,SF_TestConversion_NoPreview,6) {
    if(GetFileAttributesA("without-phobos.flag")!=INVALID_FILE_ATTRIBUTES) return 0x6410B6;
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestConversion,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    bool standalone=GetFileAttributesA("without-phobos.flag")!=INVALID_FILE_ATTRIBUTES;
    if(loading && frame<=20) return 0;
    if(frame!=20 && frame!=30) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto from=UnitTypeClass::Find("MTNK"),to=UnitTypeClass::Find("HTNK");
    UnitClass* unit=nullptr;DWORD id=0;
    if(frame==30) std::ifstream("conversion.id")>>id;
    for(auto u:UnitClass::Array) if(u->Owner==house && (id?u->UniqueID==id:u->Type==from)) {unit=u;break;}
    check(unit!=nullptr,"unit");
    if(!standalone && !publicPhobosConverter()) {
        check(GetModuleHandleW(L"Phobos.dll") && !publicPhobosConverter() &&
            !conversionAvailable() && !Research::available(0) && !transformUnit(unit,to) && unit->Type==from,"legacy Phobos unavailable");
        TerminateProcess(GetCurrentProcess(),0);return 0;
    }
    if(frame==20) {
        if(!standalone) check(publicPhobosConverter()!=nullptr,"public Phobos API");
        check(from && to && IsEqualGUID(from->Locomotor,to->Locomotor),"compatible fixture");
        check(Research::available(0),"registered research");
        if(standalone) check(!GetModuleHandleW(L"Phobos.dll"),"Phobos absent");
        auto unitId=unit->UniqueID;auto oldOwner=unit->Owner;
        int beforeOld=house->CountOwnedNow(from),beforeNew=house->CountOwnedNow(to);
        unit->Veterancy.Veterancy=1.0f;
        if(standalone) {check(UnitHealth::change(unit,100),"increased max health");UnitHealth::heal(unit,UnitHealth::read(unit).maximum/2);}
        auto before=UnitHealth::read(unit);
        int beforeAmmo=unit->Ammo;
        if(standalone) {
            auto originalSpawns=to->Spawns;
            to->Spawns=AircraftTypeClass::Find("ORCA");
            bool guarded=to->Spawns && !canTransformUnit(unit,to);
            to->Spawns=originalSpawns;
            check(guarded,"unsupported complex type skipped");
        }
        grantToUnit(unit,PassiveGrant{{"conversionmarker"},-1});
        passiveStacks[unitId]["conversionmarker"]=2;
        check(transformUnit(unit,to),"conversion");
        check(unit->UniqueID==unitId && unit->Owner==oldOwner && unit->Type==to && unit->Veterancy.Veterancy==1.0f,"identity and ownership");
        check(house->CountOwnedNow(from)==beforeOld-1 && house->CountOwnedNow(to)==beforeNew+1,"house tracking");
        if(standalone) {
            auto hp=UnitHealth::read(unit);
            int expectedMax=to->Strength+before.maximum-from->Strength;
            int expectedHp=static_cast<int>((static_cast<int64_t>(before.current)*expectedMax+before.maximum/2)/before.maximum);
            int expectedAmmo=to->Ammo<0?-1:(beforeAmmo<0?to->Ammo:std::min(beforeAmmo,to->Ammo));
            check(hp.maximum==expectedMax && hp.current==expectedHp && unit->Ammo==expectedAmmo,"builtin health and ammo");
        }
        tickStates();
        check(stackCount(unitId,"conversionmarker")==2 && std::abs(unit->FirepowerMultiplier-1.25)<0.0001,"passive retained");
        UnitClass* actionUnit=nullptr;
        for(auto other:UnitClass::Array) if(other->Owner==house && other->Type==from && other!=unit) {actionUnit=other;break;}
        check(actionUnit!=nullptr,"passive action unit");
        grantToUnit(actionUnit,PassiveGrant{{"conversionaction"},-1});
        capture("onpromote",actionUnit,nullptr,actionUnit,actionUnit,0);dispatch();
        check(actionUnit->Type==to,"passive action conversion");
        auto infFrom=InfantryTypeClass::Find("E1"),infTo=InfantryTypeClass::Find("E2");
        check(infFrom && infTo && IsEqualGUID(infFrom->Locomotor,infTo->Locomotor),"infantry fixture types");
        check(spawnUnit(infFrom,house,unit->GetCoords()),"infantry fixture");
        InfantryClass* infantry=nullptr;
        for(auto candidate:InfantryClass::Array) if(candidate && candidate->Owner==house && candidate->Type==infFrom) {infantry=candidate;break;}
        check(infantry && transformUnit(infantry,infTo) && infantry->Type==infTo,"infantry conversion");
        auto airFrom=AircraftTypeClass::Find("ORCA"),airTo=AircraftTypeClass::Find("BEAG");
        check(airFrom && airTo && IsEqualGUID(airFrom->Locomotor,airTo->Locomotor),"aircraft fixture types");
        auto aircraft=static_cast<AircraftClass*>(airFrom->CreateObject(house));
        auto airAt=unit->GetCoords();airAt.Z+=512;
        check(aircraft && aircraft->Unlimbo(airAt,DirType::North),"aircraft fixture");
        check(transformUnit(aircraft,airTo) && aircraft->Type==airTo,"aircraft conversion");
        Research::award(house->ArrayIndex,100);
        check(Research::purchase(house->ArrayIndex,0,house->Type->ID),"research conversion");
        std::ofstream("conversion.id")<<unitId;
        check(ScenarioClass::SaveGame("Saved Games\\Conversion.sav",L"Conversion"),"saved");
    }
    if(frame==30) {
        check(unit->Type==to && unit->Veterancy.Veterancy==1.0f,"loaded conversion");
        check(stackCount(unit->UniqueID,"conversionmarker")==2 && std::abs(unit->FirepowerMultiplier-1.25)<0.0001,"loaded passive");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
