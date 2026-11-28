#include "../../../src/Starfall.cpp"
#include <FactoryClass.h>
#include <DisplayClass.h>
#include <OverlayClass.h>
#include <fstream>

namespace WallTest {
constexpr CellStruct post{66,68},end{70,68},crossing{68,68};
void check(bool ok,const char* name) {
    std::ofstream("entity-walls.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<'\n';
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
BuildingClass* at(CellStruct c) { return MapClass::Instance.GetCellAt(c)->GetBuilding(); }
BuildingClass* place(const char* id,HouseClass* owner,CellStruct c) {
    auto type=BuildingTypeClass::Find(id);
    check(type && BuildingSpawn::createAt(type,owner,c),id);
    return at(c);
}
void buy(BuildingTypeClass* type,HouseClass* owner,BuildingClass* yard,CellStruct cell) {
    auto factory=static_cast<FactoryClass*>(YRMemory::AllocateChecked(sizeof(FactoryClass)));
    reinterpret_cast<void(__thiscall*)(FactoryClass*)>(0x4C98B0)(factory);
    factory->Owner=owner;
    auto b=static_cast<BuildingClass*>(type->CreateObject(owner));
    factory->SetObject(b);
    owner->SetPrimaryFactory(factory,AbstractType::BuildingType,false,BuildCat::Combat);
    yard->Factory=factory;
    reinterpret_cast<bool(__thiscall*)(HouseClass*,AbstractType,int,bool,const CellStruct*)>(0x4FB0E0)(
        owner,AbstractType::BuildingType,type->ArrayIndex,false,&cell);
    check(at(cell)==b,"native paid endpoint");
    yard->Factory=nullptr;
    owner->SetPrimaryFactory(nullptr,AbstractType::BuildingType,false,BuildCat::Combat);
    for(auto live:FactoryClass::Array) if(live==factory) {factory->Object=nullptr;delete factory;break;}
}
}
DEFINE_HOOK(0x55B4EB,EntityWallProductionCheck,5) {
    using namespace WallTest;
    auto owner=HouseClass::CurrentPlayer;
    const int f=Unsorted::CurrentFrame;
    if(!owner || f<28) return 0;
    auto segment=BuildingTypeClass::Find("ASWSEG");
    if(GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES) {
        check(at(post) && at(end) && at(end)->Type==segment && !at(crossing) && (at({67,68})->GetCurrentFrame()&15)==8,
            "saved gap and walls restored");
        TerminateProcess(GetCurrentProcess(),0);
    }
    static BuildingClass* wall;
    static BuildingClass* target;
    static TechnoClass* gun;
    static int initial,wallHP;
    static int shotNumber=0;
    constexpr const char* shots[]={"ASWStraightGun","ASWArcGun","ASWInstantGun"};
    if(f==28) {
        MapClass::Instance.Reveal(owner);
        auto yard=place("NACNST",owner,{60,65});
        buy(BuildingTypeClass::Find("ASWPOST"),owner,yard,post);
        auto node=at(post);
        auto buildup=node->Type->LoadBuildup();
        check(buildup && node->GetImage()==buildup,"node with construction art plays its buildup");
        check(segment && EntityWalls::rules(segment) && EntityWalls::rules(node->Type),"loaded rules for both building types");
        auto cursor=end;
        check(DisplayClass::Instance.PassesProximityCheck(segment,owner->ArrayIndex,segment->GetFoundationData(true),&cursor),
            "native placement preview accepts a matching endpoint");
        const auto balance=owner->Available_Money();
        buy(segment,owner,yard,end);
        check(owner->Available_Money()==balance,"no additional charge for filled cells");
        for(short x=67;x<=70;++x) {
            auto section=at({x,68});
            check(section && section->Type==segment,"independent section");
            check(section->GetImage() && section->GetImage()==segment->GetImage() &&
                section->GetCurrentFrame()==(x==70?8:10),"correct visible body on the placement frame");
        }
        buy(segment,owner,yard,{70,71});
        check(at({70,69}) && at({70,70}) && at({70,69})->Type==segment && at(post)==node,"node and segment lines");
        place("GAPILL",owner,{67,71});
        buy(segment,owner,yard,{65,71});
        check(!at({66,71}) && !at({68,71}),"occupied line leaves its path empty");
        wall=at(crossing);
        check((wall->GetCurrentFrame()&15)==10,"connected wall frame");
        // Compare the end cap directly with the stock overlay's bit order.
        auto overlay=OverlayTypeClass::Find("NAWALL");
        GameCreate<OverlayClass>(overlay,CellStruct{74,68},owner->ArrayIndex);
        GameCreate<OverlayClass>(overlay,CellStruct{75,68},owner->ArrayIndex);
        check((MapClass::Instance.GetCellAt(CellStruct{74,68})->OverlayData&15)==2,"east end cap uses stock wall art orientation");
        auto enemy=HouseClass::FindNeutral();
        target=place("ASWGATE",enemy,{68,70});
        gun=static_cast<TechnoClass*>(UnitTypeClass::Find("MTNK")->CreateObject(owner));
        check(gun->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{68,65})->GetCoords(),DirType::North),"placed firing unit");
        gun->ForceMission(Mission::Sleep);
        auto weapon=gun->GetWeapon(0)->WeaponType;
        auto p=weapon->Projectile;
        bool oldCliff=p->SubjectToCliffs,oldWall=p->SubjectToWalls;
        p->SubjectToWalls=false;p->SubjectToCliffs=true;
        check(EntityWalls::blocksAttack(gun,target,weapon) && !EntityWalls::blocksAttack(gun,wall,weapon),"high wall blocks crossing but can be targeted");
        check(gun->GetFireError(target,0,true)==FireError::ILLEGAL,"native attack command rejects the target behind the high wall");
        p->SubjectToCliffs=false;
        check(!EntityWalls::blocksAttack(gun,target,weapon),"unrestricted projectile passes");
        p->SubjectToCliffs=oldCliff;p->SubjectToWalls=oldWall;
        check(oldCliff,"native tank round obeys cliffs");
        wallHP=wall->Health;initial=target->Health;
        auto shot=p->CreateBullet(target,gun,80,weapon->Warhead,180,false);
        shot->SetWeaponType(weapon);
        auto source=gun->GetCoords();source.Z+=100;
        check(shot->MoveTo(source,BulletVelocity{0,200,0}),"launched real projectile");
    }
    if(f>=32 && f<120 && wall && wall->Health<wallHP) {
        check(target->Health==initial,"projectile hit wall");
        if(shotNumber<3) {
            wall->Health=wall->Type->Strength;
            wallHP=wall->Health;
            auto weapon=WeaponTypeClass::Find(shots[shotNumber++]);
            check(weapon && weapon->Projectile && weapon->Projectile->SubjectToCliffs,"loaded test projectile");
            auto shot=weapon->Projectile->CreateBullet(target,gun,80,weapon->Warhead,180,false);
            shot->SetWeaponType(weapon);
            auto origin=gun->GetCoords();origin.Z+=100;
            check(shot->MoveTo(origin,BulletVelocity{0,200,0}),weapon->ID);
            return 0;
        }
        wall->UnInit();wall=nullptr;
        check((at({67,68})->GetCurrentFrame()&15)==8 && !EntityWalls::blocksAttack(gun,target,gun->GetWeapon(0)->WeaponType),"breach opens firing lane and updates adjacent frame");
        auto weapon=gun->GetWeapon(0)->WeaponType;
        auto p=weapon->Projectile;
        const bool sw=p->SubjectToWalls,sc=p->SubjectToCliffs,breach=weapon->Warhead->Wall;
        const bool allied=RulesClass::Instance->AlliedWallTransparency;
        p->SubjectToWalls=true;p->SubjectToCliffs=false;weapon->Warhead->Wall=false;
        RulesClass::Instance->AlliedWallTransparency=false;
        auto normal=place("ASWNORM",owner,crossing);
        check(EntityWalls::blocksAttack(gun,target,weapon),"ordinary wall blocks a wall-sensitive projectile");
        RulesClass::Instance->AlliedWallTransparency=true;
        check(!EntityWalls::blocksAttack(gun,target,weapon),"ordinary wall honors allied transparency");
        RulesClass::Instance->AlliedWallTransparency=false;
        normal->UnInit();
        auto gate=place("ASWGATE",owner,crossing);
        p->SubjectToCliffs=true;
        check(!EntityWalls::blocksAttack(gun,target,weapon),"pass-through building honors both switches off");
        gate->UnInit();
        p->SubjectToWalls=sw;p->SubjectToCliffs=sc;weapon->Warhead->Wall=breach;
        RulesClass::Instance->AlliedWallTransparency=allied;
        check(ScenarioClass::SaveGame("Saved Games\\EntityWalls.sav",L"Entity walls"),"saved entity walls");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(f>=120) check(false,"timed out waiting for wall impact");
    return 0;
}
