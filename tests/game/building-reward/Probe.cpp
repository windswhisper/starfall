#include "../../../src/Starfall.cpp"
namespace {
DWORD actualId=0;
void check(bool ok,const char* label) {
    std::ofstream("building-reward.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
BuildingClass* place(const char* id,HouseClass* house,CellStruct site) {
    auto type=BuildingTypeClass::Find(id);
    auto b=type?static_cast<BuildingClass*>(type->CreateObject(house)):nullptr;
    bool placed=false;
    for(short y=site.Y;b && !placed && y<site.Y+8;++y) for(short x=site.X;!placed && x<site.X+8;++x) {
        CellStruct c{x,y};auto cell=MapClass::Instance.TryGetCellAt(c);
        if(cell && MapClass::Instance.IsWithinUsableArea(c,false)) placed=b->Unlimbo(cell->GetCoordsWithBridge(),DirType::North);
    }
    check(placed,"placed building");b->QueueMission(Mission::Guard,false);return b;
}
InfantryClass* infantry(const char* id,HouseClass* house) {
    auto type=InfantryTypeClass::Find(id);
    auto u=type?static_cast<InfantryClass*>(type->CreateObject(house)):nullptr;
    check(u!=nullptr,"infantry");return u;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestBuildingReward,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    int frame=Unsorted::CurrentFrame;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(!loading && frame==20) {
        Research::ensureReady();check(house && foe,"houses");
        auto closed=place("GAPILE",foe,{48,65});
        auto engBuilding=place("NAPOWR",foe,{57,65});
        auto spyBuilding=place("NAHAND",foe,{65,65});
        auto spyTransfer=place("NAHAND",foe,{73,65});
        auto claimed=place("NAPOWR",foe,{81,65});
        auto real=place("GAPOWR",foe,{89,65});
        check(!rewardRule(closed->Type).engineer && !rewardRule(closed->Type).spy &&
              rewardRule(engBuilding->Type).engineer && !rewardRule(engBuilding->Type).spy &&
              !rewardRule(spyBuilding->Type).engineer && rewardRule(spyBuilding->Type).spy &&
              rewardRule(real->Type).engineer && rewardRule(real->Type).spy,"configuration");
        auto engineer=infantry("ENGINEER",house);auto spy=infantry("E1",house);
        tickBuildings();
        check(closed->SetOwningHouse(house,false),"transfer closed");tickBuildings();
        authorizeBuildingReward(engineer,closed,Research::BuildingRewardSource::Engineer);
        authorizeBuildingReward(spy,closed,Research::BuildingRewardSource::Spy);
        check(Research::buildingOptions(closed->UniqueID,house->ArrayIndex).empty(),"defaults closed");
        authorizeBuildingReward(spy,engBuilding,Research::BuildingRewardSource::Spy);
        check(Research::buildingOptions(engBuilding->UniqueID,house->ArrayIndex).empty(),"engineer only excludes spy");
        authorizeBuildingReward(spy,spyBuilding,Research::BuildingRewardSource::Spy);
        check(!Research::buildingOptions(spyBuilding->UniqueID,house->ArrayIndex).empty(),"spy enabled");
        check(spyTransfer->SetOwningHouse(house,false),"spy building transfer");tickBuildings();
        authorizeBuildingReward(engineer,spyTransfer,Research::BuildingRewardSource::Engineer);
        check(Research::buildingOptions(spyTransfer->UniqueID,house->ArrayIndex).empty(),"independent flags");
        check(spyTransfer->SetOwningHouse(foe,false),"transfer back");tickBuildings();
        authorizeBuildingReward(spy,spyTransfer,Research::BuildingRewardSource::Spy);
        check(!Research::buildingOptions(spyTransfer->UniqueID,house->ArrayIndex).empty(),"spy permission");
        check(spyTransfer->SetOwningHouse(house,false),"spy owner change");tickBuildings();
        check(Research::buildingOptions(spyTransfer->UniqueID,house->ArrayIndex).empty(),"spy privilege revoked on transfer");
        check(engBuilding->SetOwningHouse(house,false),"engineer building transfer");tickBuildings();
        authorizeBuildingReward(engineer,engBuilding,Research::BuildingRewardSource::Engineer);
        check(!Research::buildingOptions(engBuilding->UniqueID,house->ArrayIndex).empty(),"engineer privilege");
        check(claimed->SetOwningHouse(house,false),"claim building transfer");tickBuildings();
        authorizeBuildingReward(engineer,claimed,Research::BuildingRewardSource::Engineer);
        auto choices=Research::buildingOptions(claimed->UniqueID,house->ArrayIndex);
        check(!choices.empty() && Research::claimBuilding(claimed->UniqueID,house->ArrayIndex,choices[0]),"claim reward");
        check(claimed->SetOwningHouse(foe,false),"claimed transfer");tickBuildings();
        authorizeBuildingReward(spy,claimed,Research::BuildingRewardSource::Spy);
        check(Research::buildingOptions(claimed->UniqueID,house->ArrayIndex).empty() && Research::buildingRewards.at(claimed->UniqueID).claimed,"claimed once");
        real->Type->Capturable=true;
        auto realEngineer=infantry("ENGINEER",house);
        auto at=real->GetMapCoords();bool placed=false;
        for(short dy=-5;dy<=5 && !placed;++dy) for(short dx=-5;dx<=5 && !placed;++dx) {
            if(std::abs(dx)+std::abs(dy)<3) continue;
            CellStruct c{static_cast<short>(at.X+dx),static_cast<short>(at.Y+dy)};auto cell=MapClass::Instance.TryGetCellAt(c);
            if(cell && MapClass::Instance.IsWithinUsableArea(c,false)) placed=realEngineer->Unlimbo(cell->GetCoordsWithBridge(),DirType::North);
        }
        check(placed,"placed real engineer");
        realEngineer->SetDestination(real,true);realEngineer->SetTarget(real);realEngineer->QueueMission(Mission::Capture,true);
        actualId=real->UniqueID;
        std::ofstream("building-reward.ids")<<engBuilding->UniqueID<<" "<<spyBuilding->UniqueID<<" "<<claimed->UniqueID<<" "<<actualId;
    } else if(!loading && frame>20 && actualId) {
        BuildingClass* real=nullptr;for(auto b:BuildingClass::Array) if(b && b->UniqueID==actualId) real=b;
        if(real && real->Owner==house && Research::buildingRewards.at(actualId).source==Research::BuildingRewardSource::Engineer) {
            check(!Research::buildingOptions(actualId,house->ArrayIndex).empty(),"native engineer capture");
            check(ScenarioClass::SaveGame("Saved Games\\BuildingReward.sav",L"Building Reward"),"saved");
            TerminateProcess(GetCurrentProcess(),0);
        }
        if(frame>=620){check(false,"native engineer capture");}
    } else if(loading && frame>=35) {
        DWORD eng=0,spy=0,claimed=0,real=0;std::ifstream("building-reward.ids")>>eng>>spy>>claimed>>real;
        check(Research::buildingRewards.contains(eng) && Research::buildingRewards.at(eng).source==Research::BuildingRewardSource::Engineer &&
              Research::buildingRewards.contains(spy) && Research::buildingRewards.at(spy).source==Research::BuildingRewardSource::Spy &&
              !Research::buildingOptions(eng,house->ArrayIndex).empty() && !Research::buildingOptions(spy,house->ArrayIndex).empty(),"saved permissions");
        check(Research::buildingRewards.contains(claimed) && Research::buildingRewards.at(claimed).claimed,"saved claimed");
        check(Research::buildingOptions(claimed,house->ArrayIndex).empty(),"loaded no duplicate");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
