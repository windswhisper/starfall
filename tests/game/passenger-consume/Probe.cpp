#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {
    std::ofstream("passenger-consume.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
FootClass* addPassenger(UnitClass* carrier,TechnoTypeClass* type,int hp) {
    auto unit=static_cast<FootClass*>(type->CreateObject(carrier->Owner));
    if(!unit) return nullptr;
    unit->Health=hp;
    carrier->EnterAsPassenger(unit);unit->Transporter=carrier;
    if(carrier->Type->OpenTopped) carrier->EnteredOpenTopped(unit);
    return unit;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPassengerConsume,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && frame>=25) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if((!loading && frame!=20) || (loading && frame!=30)) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    DWORD id=0;if(loading) std::ifstream("passenger-consume.id")>>id;
    UnitClass *carrier=nullptr,*enemy=nullptr;
    for(auto unit:UnitClass::Array) {
        if(!carrier && unit->Owner==house && !_stricmp(unit->Type->ID,"MTNK") && (!id || id==unit->UniqueID)) carrier=unit;
        if(!enemy && unit->Owner==foe) enemy=unit;
    }
    check(carrier && enemy,"units");
    if(!loading) {
        check(skills.contains("feed") && skills.contains("whenfed") && !skills.contains("invalidamount"),"configuration");
        auto infantry=InfantryTypeClass::Find("E1");check(infantry && infantry->Cost>0 && infantry->Strength>2,"infantry type");
        auto first=addPassenger(carrier,infantry,infantry->Strength);
        auto fort=UnitTypeClass::Find("BFRT"),ifv=UnitTypeClass::Find("FV");
        check(fort && ifv,"transport types");
        auto second=static_cast<UnitClass*>(addPassenger(carrier,fort,fort->Strength/2));
        check(second!=nullptr,"direct transport passenger");
        auto inner=static_cast<UnitClass*>(addPassenger(second,ifv,ifv->Strength));
        auto beside=addPassenger(second,infantry,infantry->Strength);
        auto deep=inner?addPassenger(inner,infantry,infantry->Strength):nullptr;
        auto deepSibling=inner?addPassenger(inner,infantry,infantry->Strength):nullptr;
        check(inner && beside && deep && deepSibling,"nested cargo fixture");
        std::set<DWORD> cargoIDs{second->UniqueID,inner->UniqueID,beside->UniqueID,deep->UniqueID,deepSibling->UniqueID};
        grantToUnit(inner,PassiveGrant{{"whenfed","consumeddeath","nestedonly"},-1});
        grantToUnit(deep,PassiveGrant{{"consumeddeath"},-1});
        Event unused{};
        ++inner->Passengers.NumPassengers;
        check(!consumePassenger(carrier,unused) && carrier->Passengers.NumPassengers==2 &&
            second->Passengers.NumPassengers==2 && beside->Transporter==second,"invalid nested list remains intact");
        --inner->Passengers.NumPassengers;
        check(first && second && carrier->Passengers.NumPassengers==2 && carrier->Passengers.FirstPassenger==second,"two passengers");
        int removedHp=second->Health,removedCost=fort->Cost,cost=infantry->Cost,enemyCost=enemy->Type->Cost;
        grantToUnit(second,PassiveGrant{{"consumeddeath","cargopower","cargosupport"},-1});
        changeStacks(second->UniqueID,"cargopower",5,INT_MAX);
        refreshTransportGrants();
        check(stackCount(carrier->UniqueID,"cargopower")==1,"passenger supplies temporary stack before consumption");
        grantToUnit(carrier,PassiveGrant{{"feed","whenfed","originaltarget","digesta","digestb"},-1});
        int balance=Research::balance(house->ArrayIndex),enemyHp=enemy->Health;
        capture("onkill",carrier,enemy,carrier,enemy,0);dispatch();
        check(carrier->Passengers.NumPassengers==1 && carrier->Passengers.FirstPassenger==first,"one passenger consumed");
        bool stillAlive=false;
        for(auto unit:TechnoClass::Array) if(unit && unit->IsAlive && cargoIDs.contains(unit->UniqueID)) {
            stillAlive=true;
            std::ofstream("passenger-consume.log",std::ios::app)<<"remaining cargo "<<unit->UniqueID<<" "<<unit->GetTechnoType()->ID<<"\n";
        }
        check(!stillAlive,"passenger removed");
        check(first->Transporter==carrier && !stillAlive,"nested subtree removed and sibling retained");
        check(Research::balance(house->ArrayIndex)==balance+removedCost+enemyCost,"target cost and original event isolated");
        check(enemy->Health==enemyHp,"removed target is not original enemy");
        check(ownsPassive(carrier,"cargopower") && stackCount(carrier->UniqueID,"cargopower")==2 &&
              !ownsPassive(carrier,"nestedonly"),"same action list steals direct passenger stacks");
        check(pending.size()==1 && pending[0].trigger=="onpassengerconsumed" && pending[0].holder==carrier->UniqueID && pending[0].kind=="vehicle" && pending[0].targetHp==removedHp && pending[0].targetMaxHp==fort->Strength && pending[0].targetCost==removedCost && pending[0].passengers==1,"consumed event snapshot");
        std::ofstream("passenger-consume.id")<<carrier->UniqueID<<" "<<balance+removedCost+enemyCost<<" "<<removedHp+removedCost;
        check(ScenarioClass::SaveGame("Saved Games\\PassengerConsume.sav",L"Passenger Consume"),"saved");
        dispatch();
        check(stackCount(carrier->UniqueID,"cargopower")==5 && ownsPassive(carrier,"consumeddeath") &&
              !ownsPassive(carrier,"nestedonly"),"queued abilities share remaining stacks and zero stack ownership");
        check(Research::balance(house->ArrayIndex)==balance+removedCost+enemyCost+removedHp+removedCost,"consumed event reward");
        carrier->Passengers.RemoveFirstPassenger();first->Transporter=nullptr;first->RegisterDestruction(nullptr);first->UnInit();
        balance=Research::balance(house->ArrayIndex);
        capture("onkill",carrier,enemy,carrier,enemy,0);dispatch();dispatch();
        check(Research::balance(house->ArrayIndex)==balance+enemyCost,"empty skips chained reward");
        bool open=carrier->Type->OpenTopped;carrier->Type->OpenTopped=true;
        auto openPassenger=addPassenger(carrier,infantry,infantry->Strength);
        check(openPassenger && openPassenger->InOpenToppedTransport,"open transport fixture");
        DWORD openId=openPassenger->UniqueID;
        capture("onkill",carrier,enemy,carrier,enemy,0);dispatch();
        bool openAlive=false;for(auto unit:TechnoClass::Array) if(unit && unit->UniqueID==openId && unit->IsAlive) openAlive=true;
        carrier->Type->OpenTopped=open;
        check(!openAlive && carrier->Passengers.NumPassengers==0,"open transport cleanup");
        UnitClass* helper=nullptr;
        for(auto unit:UnitClass::Array) if(unit->Owner==house && unit!=carrier && !_stricmp(unit->Type->ID,"MTNK")) {helper=unit;break;}
        auto enemyFirst=addPassenger(enemy,infantry,infantry->Strength);
        auto enemySecond=addPassenger(enemy,infantry,infantry->Strength/2);
        check(helper && enemyFirst && enemySecond && enemy->Passengers.NumPassengers==2,"other carrier fixture");
        grantToUnit(helper,PassiveGrant{{"feedother"},-1});
        grantToUnit(enemy,PassiveGrant{{"whenfed"},-1});
        balance=Research::balance(house->ArrayIndex);int foeBalance=Research::balance(foe->ArrayIndex);
        capture("onkill",helper,enemy,helper,enemy,0);dispatch();
        check(enemy->Passengers.NumPassengers==1 && Research::balance(house->ArrayIndex)==balance+infantry->Strength/2,"consume target carrier");
        dispatch();check(Research::balance(foe->ArrayIndex)==foeBalance+infantry->Strength/2+cost,"target carrier receives event");
        auto buildingType=BuildingTypeClass::Find("GAPOWR");
        auto building=buildingType?static_cast<BuildingClass*>(buildingType->CreateObject(house)):nullptr;
        CellStruct buildingAt{50,65};auto cell=MapClass::Instance.TryGetCellAt(buildingAt);
        check(building && cell && building->Unlimbo(cell->GetCoordsWithBridge(),DirType::North),"building fixture");
        auto buildingPassenger=static_cast<FootClass*>(infantry->CreateObject(house));
        check(buildingPassenger!=nullptr,"building passenger fixture");
        building->Passengers.AddPassenger(buildingPassenger);buildingPassenger->Transporter=building;
        grantToUnit(building,PassiveGrant{{"feed"},-1});
        balance=Research::balance(house->ArrayIndex);
        capture("onkill",building,enemy,building,enemy,0);dispatch();
        check(building->Passengers.NumPassengers==0 && Research::balance(house->ArrayIndex)==balance+cost && house->RecheckPower,"building passenger consumed");
        TerminateProcess(GetCurrentProcess(),0);
    } else {
        int before=0,hp=0;std::ifstream("passenger-consume.id")>>id>>before>>hp;
        check(carrier->Passengers.NumPassengers==1,"load pending event");
        dispatch();check(Research::balance(house->ArrayIndex)==before+hp,"loaded event uses passenger snapshot");
        check(stackCount(carrier->UniqueID,"cargopower")==5 && ownsPassive(carrier,"consumeddeath") &&
              !ownsPassive(carrier,"nestedonly"),"loaded remaining stacks and zero stack ownership");
        dispatch();check(Research::balance(house->ArrayIndex)==before+hp,"event once after load");
        check(stackCount(carrier->UniqueID,"cargopower")==5,"no duplicate inherited stacks");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
