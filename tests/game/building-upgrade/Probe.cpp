#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* message) {
    std::ofstream("building-upgrade.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<message<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
BuildingClass* place(const char* name,HouseClass* house,CellStruct pos) {
    auto type=BuildingTypeClass::Find(name);
    auto building=type?static_cast<BuildingClass*>(type->CreateObject(house)):nullptr;
    auto cell=MapClass::Instance.TryGetCellAt(pos);
    check(building && cell && building->Unlimbo(cell->GetCoordsWithBridge(),DirType::North),"placed host");
    return building;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestBuildingUpgrade,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    int f=Unsorted::CurrentFrame;
    if(!loading && f==20) {
        auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
        auto host=place("GAPOWR",house,{48,65});
        auto other=place("NAPOWR",house,{60,65});
        auto type=BuildingTypeClass::Find("GAPILE");
        auto bad=static_cast<BuildingClass*>(type->CreateObject(house));
        auto before=pending.size();
        bool rejected=!bad->Unlimbo(other->GetCoords(),DirType::North);
        check(rejected && pending.size()==before,"failed upgrade ignored");
        auto upgrade=static_cast<BuildingClass*>(type->CreateObject(house));
        int max=UnitHealth::read(host).maximum;
        bool success=upgrade->Unlimbo(host->GetCoords(),DirType::North);
        check(success && pending.size()==before+1 && pending.back().holder==host->UniqueID && pending.back().victim==host->UniqueID,"native upgrade event");
        std::ofstream("building-upgrade.ids")<<host->UniqueID<<" "<<max;
        check(ScenarioClass::SaveGame("Saved Games\\BuildingUpgrade.sav",L"Building Upgrade"),"saved pending event");
    }
    if(f>=25) {
        DWORD id=0;int max=0;std::ifstream("building-upgrade.ids")>>id>>max;
        BuildingClass* host=nullptr;for(auto b:BuildingClass::Array) if(b && b->UniqueID==id) host=b;
        check(host && UnitHealth::read(host).maximum==max+150,"upgraded target receives result");
        check(pending.empty(),"no repeat after load");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
