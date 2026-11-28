#include "../../../src/Starfall.cpp"

namespace {
void check(bool ok,const char* message) {
    std::ofstream("research-counter.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<message<<'\n';
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
TechnoClass* place(const char* id,HouseClass* owner) {
    auto type=TechnoTypeClass::Find(id);
    auto unit=static_cast<TechnoClass*>(type->CreateObject(owner));
    for(short y=40;y<95;y+=4) for(short x=40;x<95;x+=4) {
        CellStruct cell{x,y};
        if(BuildingSpawn::available(BuildingTypeClass::Find("NALASR"),owner,cell) &&
            unit->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoords(),DirType::North)) return unit;
    }
    check(false,"fixture placement");return nullptr;
}
void killUnit(TechnoClass* killer,TechnoClass* victim) {
    int damage=100000;
    victim->ReceiveDamage(&damage,0,WarheadTypeClass::Find("AP"),killer,true,false,killer->Owner);
    dispatch();tickStates();
}
}
DEFINE_HOOK(0x55B4EB,SF_TestResearchCounter,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int frame=Unsorted::CurrentFrame;
    if(frame!=20 && frame!=35) return 0;
    if(frame==20 && GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES) return 0;
    Research::ensureReady();
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    vector<UnitClass*> tanks;
    UnitClass* victim=nullptr;
    for(auto unit:UnitClass::Array) if(unit) {
        if(unit->Owner==house && !_stricmp(unit->Type->ID,"MTNK")) tanks.push_back(unit);
        if(unit->Owner==foe) victim=unit;
    }
    check(house && foe && tanks.size()>=2 && victim,"fixture units");
    int h=house->ArrayIndex,f=foe->ArrayIndex;
    const string key="antiarmor.vehicle_kills",reserve="antiarmor.reserve";
    auto grizzly=TechnoTypeClass::Find("MTNK"),sniper=TechnoTypeClass::Find("SNIPE");
    auto get=[&](const char* id){return Research::grantNamed(h,house->Type->ID,id)>=0;};
    auto licensed=[&]{return Research::Detail::hasProductionLicense(house,TechnoTypeClass::Find("CLEG"));};
    if(frame==20) {
        WritePrivateProfileStringA("Starfall.Research","Repeatable","yes",configPath);
        Research::readConfiguration(configPath);
        check(Research::options[0].repeatable && !Research::options[10].repeatable,"global repeat and project opt out");
        WritePrivateProfileStringA("Starfall.Research","Repeatable",nullptr,configPath);
        Research::readConfiguration(configPath);
        check(!Research::options[0].repeatable && Research::options[12].repeatable,"project opt in overrides default");
        check(skills.contains("trackkills") && skills.contains("veteranfire") && skills.contains("spendcounter") &&
            !skills.contains("unknowncounter") && !skills.contains("unknownformula"),"counter references validated");
        check(!Research::changeCounter(h,key,1) && !Research::counter(h,key),"unowned counter ignores writes");
        check(Research::describe(0,h)==L"战绩 0/100, reserve 0, left 100","preview reads zero before acquisition");
        check(Research::describe(2,h)==L"Missing —, unit —, zero —, valid 5, open —" && Research::available(2),
            "bad detail formulas affect only their own text");
        check(Research::grantNamed(h,house->Type->ID,"antiarmor")>=0 && Research::grantNamed(f,foe->Type->ID,"antiarmor")>=0,"both houses acquire research");
        auto truck=place("TRUCKA",house),soldier=place("E1",house);
        const int infantryCost=soldier->GetTechnoType()->GetActualCost(house);
        const int cash=house->Available_Money(),price=grizzly->GetActualCost(house);
        check(Research::grantNamed(h,house->Type->ID,"campaign")>=0 && house->Available_Money()==cash+9 &&
            !Research::Detail::hasProductionLicense(house,sniper),"stage-only grant checks initial formula");
        check(Research::changeCounter(h,key,98) && Research::changeCounter(f,reserve,7),"independent counters initialized");
        check(house->Available_Money()==cash+9+50+11 && Research::owns(h,"stagefollowup") &&
            Research::Detail::hasProductionLicense(house,sniper) && !Research::Detail::hasProductionLicense(foe,sniper) &&
            grizzly->GetActualCost(house)==price-25,"crossed stages grant cash price license and chained research in order");
        check(truck->GetTechnoType()==TechnoTypeClass::Find("TRUCKB") && soldier->GetTechnoType()==TechnoTypeClass::Find("E2") &&
            TechnoTypeClass::Find("E1")->GetActualCost(house)==(infantryCost+1)/2 &&
            Research::stageProgress.at({h,"campaign","logistics"}).contains("replaceifv"),
            "stage converts current infantry and vehicles and grants category discount and replacement");
        check(place("TRUCKA",house)->GetTechnoType()==TechnoTypeClass::Find("TRUCKA"),"later trucks keep original type");
        killUnit(tanks[0],place("MTNK",house));
        killUnit(tanks[0],place("E2",foe));
        check(Research::counter(h,key)==98,"friendly kills excluded");
        killUnit(tanks[0],place("MTNK",foe));
        check(Research::counter(h,key)==99 && tanks[0]->FirepowerMultiplier==1.0,"first contributor below threshold");
        killUnit(tanks[1],place("MTNK",foe));
        check(Research::counter(h,key)==100 && tanks[0]->FirepowerMultiplier==1.25 &&
            tanks[1]->FirepowerMultiplier==1.25 && !Research::counter(f,key),"second contributor enables shared bonus");
        check(Research::describe(0,h)==L"战绩 100/100, reserve 0, left 0" &&
            Research::describe(0,f)==L"战绩 0/100, reserve 7, left 100","detail reads the displayed player");
        check(Research::describe(1,h)==L"Credits 25, ratio 33.33" && Research::describe(1,f)==L"Credits 25, ratio 0.00",
            "legacy placeholders and fixed precision coexist");
        auto reinforcement=place("MTNK",house);tickStates();
        check(reinforcement->ArmorMultiplier==1.5 && Research::pendingFreeRewards(h).size()==1,"stage passive reaches reinforcements and free choice is issued");
        check(reinforcement->FirepowerMultiplier==1.25,"reinforcement inherits threshold bonus");
        int before=house->Available_Money();
        check(get("bonda") && get("bondb") && get("bondc") && house->Available_Money()==before+233 &&
            licensed() && ownsPassive(tanks[0],"bondmarker") && Research::describe(6,h)==L"Hunter 3" &&
            Research::describe(6,f)==L"Hunter 0","overlapping bonds grant distinct members and owner-only rewards");
        check(get("bonda2") && house->Available_Money()==before+234 && licensed() &&
            Research::describe(6,h)==L"Hunter 3" && !Research::bondProgress.at({h,"supply","supply2"}).active,
            "atomic replacement preserves one bond while dropping the other");
        auto heavy=TechnoTypeClass::Find("HTNK");
        int originalPrice=heavy->GetActualCost(house),originalMoney=house->Available_Money();
        check(get("fieldlogistics") && get("fieldlogistics") && house->Available_Money()==originalMoney+120 &&
            heavy->GetActualCost(house)==static_cast<int>(std::round(originalPrice*.80)),"repeat grants pay twice and stack price");
        auto fresh=static_cast<FootClass*>(place("MTNK",house));tickStates();
        check(unitStacks(fresh,"fieldlogisticsbonus")==2 && std::abs(fresh->SpeedMultiplier-1.2)<0.00001 &&
            eventSnapshot("onkill",fresh,victim,fresh,victim,0).stackSnapshot["fieldlogisticsbonus"]==2 &&
            unitStacks(victim,"fieldlogisticsbonus")==0,"repeat passive stacks reach reinforcements and event snapshots");
        check(ScenarioClass::SaveGame("Saved Games\\ResearchCounter.sav",L"Research counter"),"saved");
    } else {
        check(Research::acquisitionCount(h,"fieldlogistics")==2 && unitStacks(tanks[0],"fieldlogisticsbonus")==2 &&
            get("fieldlogistics") && unitStacks(tanks[0],"fieldlogisticsbonus")==3,"repeat stacks survive save and advance again");
        check(Research::counter(h,key)==100 && Research::counter(f,reserve)==7 &&
            !Research::counter(h,reserve) && !Research::counter(f,key),"progress preserved per project variable and player");
        grantToUnit(tanks[0],PassiveGrant{{"spendcounter"},-1});
        capture("ondeploy",tanks[0],nullptr,nullptr,nullptr,0);dispatch();tickStates();
        check(Research::counter(h,key)==99 && tanks[1]->FirepowerMultiplier==1.0,"signed formula withdraws bonus");
        check(Research::describe(0,h)==L"战绩 99/100, reserve 0, left 1" &&
            Research::describe(1,h)==L"Credits 25, ratio 33.00","tooltips refresh from restored and changed progress");
        const int cash=house->Available_Money(),discounted=grizzly->GetActualCost(house);
        check(tanks[0]->ArmorMultiplier==1.5 && Research::Detail::hasProductionLicense(house,sniper) &&
            Research::pendingFreeRewards(h).size()==1,"persistent stage grants survive save");
        Research::changeCounter(h,key,INT_MAX);
        check(house->Available_Money()==cash && Research::pendingFreeRewards(h).size()==1,
            "threshold reentry does not duplicate credits or tickets");
        check(Research::grantNamed(h,house->Type->ID,"campaignupgrade")>=0,"retire stage parent");
        tickStates();
        check(tanks[0]->ArmorMultiplier==1 && !Research::Detail::hasProductionLicense(house,sniper) &&
            grizzly->GetActualCost(house)==discounted+25 && house->Available_Money()==cash+1 &&
            Research::owns(h,"stagefollowup") && Research::pendingFreeRewards(h).size()==1,"retirement removes entitlements and keeps issued rewards");
        check(Research::counter(h,key)==INT_MAX,"positive overflow saturates");
        Research::changeCounter(h,key,INT_MIN);
        check(!Research::counter(h,key) && Research::counter(f,reserve)==7,"negative change clamps at zero");
        int before=house->Available_Money();
        check(licensed() && !Research::bondProgress.at({h,"supply","supply2"}).active &&
            get("exitb") && !licensed() && !ownsPassive(tanks[0],"bondmarker") && Research::describe(6,h)==L"Hunter 2",
            "loaded active bond loses its permanent grants below threshold");
        check(get("bondd") && licensed() && ownsPassive(tanks[0],"bondmarker") &&
            house->Available_Money()==before+2,"restored bond reactivates without a second payout");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
