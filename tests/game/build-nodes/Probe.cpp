#include "../../../src/Starfall.cpp"
#include <DisplayClass.h>
#include <FactoryClass.h>

namespace {
void check(bool ok,const char* name) {
    std::ofstream("build-nodes.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<'\n';
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x55B4EB,ActSp_TestBuildNodes,5) {
    if(Unsorted::CurrentFrame!=28) return 0;
    auto house=HouseClass::CurrentPlayer;
    auto type=BuildingTypeClass::Find("GAPILL");
    CellStruct site{69,68},nodeCell{67,68};
    auto pos=MapClass::Instance.GetCellAt(nodeCell)->GetCoords();
    auto preview=[&](BuildingTypeClass* product){
        return DisplayClass::Instance.PassesProximityCheck(product,house->ArrayIndex,product->GetFoundationData(false),&site);
    };
    check(!preview(type),"isolated site");
    auto tank=static_cast<UnitClass*>(UnitTypeClass::Find("AMCV")->CreateObject(house));
    check(tank->Unlimbo(pos,DirType::North) && preview(type),"grounded node");
    check(!preview(BuildingTypeClass::Find("ATESLA")),"building whitelist");
    check(tank->Limbo() && !preview(type),"removed node");
    auto air=static_cast<UnitClass*>(UnitTypeClass::Find("ZEP")->CreateObject(house));
    check(air->Unlimbo(pos,DirType::North),"placed Kirov");
    air->SetHeight(768);
    check(air->IsInAir() && preview(type),"airborne Kirov node");
    CellStruct blockedCell{72,68};
    if(GetModuleHandleA("Phobos.dll")) {
        check(BuildingSpawn::createAt(BuildingTypeClass::Find("GAPOWR"),house,blockedCell),"prohibit fixture");
        check(!preview(type),"separate Phobos exclusion radius vetoes hovering node");
        auto blocker=MapClass::Instance.GetCellAt(blockedCell)->GetBuilding();
        check(blocker->Limbo() && preview(type),"node works after exclusion removed");
    }
    auto yardType=BuildingTypeClass::Find("GACNST");
    check(BuildingSpawn::createAt(yardType,house,{59,65}),"yard");
    auto yard=MapClass::Instance.GetCellAt(CellStruct{59,65})->GetBuilding();
    auto factory=YRMemory::AllocateChecked(sizeof(FactoryClass));
    auto queue=reinterpret_cast<FactoryClass*(__thiscall*)(void*)>(0x4C98B0)(factory);
    queue->Owner=house;
    auto product=static_cast<BuildingClass*>(type->CreateObject(house));
    queue->SetObject(product);
    house->SetPrimaryFactory(queue,AbstractType::BuildingType,false,BuildCat::Combat);
    yard->Factory=queue;
    auto place=[&] {
        reinterpret_cast<bool(__thiscall*)(HouseClass*,AbstractType,int,bool,const CellStruct*)>(0x4FB0E0)(
            house,AbstractType::BuildingType,type->ArrayIndex,false,&site);
    };
    check(air->Limbo() && !preview(type),"Kirov left");
    place();
    check(!MapClass::Instance.GetCellAt(site)->GetBuilding() && queue->Object==product,"stale placement rejected");
    check(tank->Unlimbo(pos,DirType::North),"returned ground node");
    place();
    check(MapClass::Instance.GetCellAt(site)->GetBuilding()==product,"paid placement");
    TerminateProcess(GetCurrentProcess(),0);
    return 0;
}
