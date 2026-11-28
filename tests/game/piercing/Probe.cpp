#include "../../../src/Starfall.cpp"
#include <fstream>

namespace PiercingTest {
void check(bool ok,const char* text) {
    std::ofstream("piercing.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<text<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
CellClass* cell(int x,int y=68) {return MapClass::Instance.GetCellAt(CellStruct{short(x),short(y)});}
BuildingClass* post(int x) {return cell(x)->GetBuilding();}
UnitClass* unit(const char* id,HouseClass* house,int x,int y) {
    auto u=static_cast<UnitClass*>(UnitTypeClass::Find(id)->CreateObject(house));
    check(u && u->Unlimbo(cell(x,y)->GetCoords(),DirType::North),id);
    u->ForceMission(Mission::Sleep);
    return u;
}
BulletClass* liveRound(DWORD id) {
    for(auto b:BulletClass::Array) if(b->UniqueID==id) return b;
    return nullptr;
}
void shoot(TechnoClass* gun,const char* weapon="PWGun",int aimX=68) {
    auto w=WeaponTypeClass::Find(weapon);
    auto b=w->Projectile->CreateBullet(post(aimX),gun,100,w->Warhead,w->Speed,false);
    b->SetWeaponType(w);
    auto from=cell(65)->GetCoords();
    from.Z+=32;
    check(b->MoveTo(from,{64,0,0}),"launched");
}
}
namespace PiercingTest {
int blasts=0,lastDamage=0;
CoordStruct lastPoint{};
}
DEFINE_HOOK(0x4690B0,PWTestEndBlast,6) {
    using namespace PiercingTest;
    auto b=R->ECX<BulletClass*>();
    if(b->Type==BulletTypeClass::Find("PWEndRound") && b->WH==WarheadTypeClass::Find("PWBurstWH")) {
        ++blasts;lastDamage=b->Health;lastPoint=*R->Stack<CoordStruct*>(4);
    }
    return 0;
}
DEFINE_HOOK(0x55B4EB,PWTestFrame,5) {
    using namespace PiercingTest;
    static int stage=0,start=0;
    static UnitClass* gun=nullptr;
    static CoordStruct nativeOrigin{};
    static DWORD nativeShot=0;
    static int witnessHP=0;
    static bool sawAbsorption=false;
    static UnitClass* guard=nullptr;
    const auto f=Unsorted::CurrentFrame;
    auto player=HouseClass::CurrentPlayer;
    auto enemy=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(f<28) return 0;
    if(stage==0 && GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES) {
        stage=3;start=f;
    }
    if(stage==0) {
        for(int i=UnitClass::Array.Count-1;i>=0;--i) if(UnitClass::Array[i]->Type==UnitTypeClass::Find("MTNK")) UnitClass::Array[i]->UnInit();
        for(int x:{68,70,72,74,79}) check(BuildingSpawn::createAt(BuildingTypeClass::Find("PWPOST"),enemy,{short(x),68}),"placed target");
        for(int x:{69,76}) check(BuildingSpawn::createAt(BuildingTypeClass::Find("PWBYSTANDER"),enemy,{short(x),70}),"placed blast witness");
        gun=unit("MTNK",player,64,67);
        shoot(gun);start=f;stage=1;
    } else if(stage==1 && f-start==50) {
        std::ofstream("piercing.log",std::ios::app)<<"health "<<post(68)->Health<<" "<<post(70)->Health<<" "<<post(72)->Health<<" "<<post(74)->Health<<" "<<post(79)->Health<<"\n";
        check(post(68)->Health==4900 && post(70)->Health==4950 && post(72)->Health==4970 && post(74)->Health==4970,"one hit each and falloff floor beyond aim");
        check(post(79)->Health==5000,"out of lifetime range");
        shoot(gun,"PWEndGun");stage=2;
    } else if(stage==2 && post(68)->Health==4800) {
        check(ScenarioClass::SaveGame("Saved Games\\Piercing.sav",L"Piercing round"),"saved overlapping first target");
        TerminateProcess(GetCurrentProcess(),0);
    } else if(stage==3 && f-start==55) {
        check(post(68)->Health==4800 && post(70)->Health==4900 && post(72)->Health==4940 && post(74)->Health==4940 && post(79)->Health==5000,"restored direction lifetime hit history and falloff");
        auto expiryWitness=cell(76,70)->GetBuilding();
        std::ofstream("piercing.log",std::ios::app)<<"expiry count "<<blasts<<" damage "<<lastDamage<<" point "<<lastPoint.X<<","<<lastPoint.Y<<","<<lastPoint.Z<<" witness hp "<<expiryWitness->Health<<"\n";
        check(blasts==1 && lastDamage==30 && CellClass::Coord2Cell(lastPoint)==CellStruct{76,68} &&
              expiryWitness->Health<5000,"restored expiry detonates end warhead once at last step");
        guard=unit("HTNK",enemy,70,69);
        gun=unit("MTNK",player,64,69);stage=4;start=f;
    } else if(stage==4 && f-start==3) {
        shoot(gun,"PWEndGun");stage=5;
    } else if(stage==5 && f-start==60) {
        check(guard->Health==985 && post(68)->Health==4700 && post(72)->Health==4940,"shield takes attenuated hit and protects rear");
        std::ofstream("piercing.log",std::ios::app)<<"shield count "<<blasts<<" damage "<<lastDamage<<" point "<<lastPoint.X<<","<<lastPoint.Y<<","<<lastPoint.Z<<" witness hp "<<cell(69,70)->GetBuilding()->Health<<"\n";
        check(blasts==2 && lastDamage==15 && CellClass::Coord2Cell(lastPoint).X==69 &&
              cell(69,70)->GetBuilding()->Health<5000,"shield detonates end warhead once at contact");
        auto aim=post(68);
        auto facing=gun->GetTargetDirection(aim);
        gun->SetTarget(aim);
        gun->PrimaryFacing.SetCurrent(facing);
        gun->SecondaryFacing.SetCurrent(facing);
        gun->RearmTimer.Stop();
        auto round=gun->Fire(aim,0);
        check(round && round->Type==BulletTypeClass::Find("PWRound"),"native weapon launches piercing projectile");
        nativeShot=round->UniqueID;
        nativeOrigin=round->Location;
        guard->UnInit();
        aim->UnInit();
        start=f;stage=6;
    } else if(stage==6 && f-start==18) {
        auto round=liveRound(nativeShot);
        check(round && round->Location.DistanceFrom(nativeOrigin)>1500,"continues past destroyed aim");
    } else if(stage==6 && f-start==20) {
        check(!liveRound(nativeShot),"expires at configured lifetime");
        check(BuildingSpawn::createAt(BuildingTypeClass::Find("PWPOST"),enemy,{68,68}),"replaced aim");
        check(BuildingSpawn::createAt(BuildingTypeClass::Find("PWENDWALL"),enemy,{71,68}),"placed intercepting wall");
        witnessHP=cell(69,70)->GetBuilding()->Health;
        shoot(gun,"PWEndGun",74);stage=7;start=f;
    } else if(stage==7 && f-start==23) {
        std::ofstream("piercing.log",std::ios::app)<<"wall count "<<blasts<<" damage "<<lastDamage<<" point "<<lastPoint.X<<","<<lastPoint.Y<<","<<lastPoint.Z<<" hp "<<post(71)->Health<<" witness "<<cell(69,70)->GetBuilding()->Health<<"/"<<witnessHP<<"\n";
        check(blasts==3 && lastDamage==30 && lastPoint.X==71*256 && post(71)->Health==4970 &&
              cell(69,70)->GetBuilding()->Health<witnessHP,"wall detonates at its face without duplicate damage");
        guard=unit("HTNK",enemy,70,69);
        guard->Health=guard->EstimatedHealth=400;
        witnessHP=cell(69,70)->GetBuilding()->Health;
        stage=8;start=f;
    } else if(stage==8 && f-start==3) {
        shoot(gun,"PWEndGun");stage=9;
    } else if(stage==9) {
        for(auto anim:AnimClass::Array) {
            if(anim->IsInert && anim->Owner==player && !_stricmp(anim->Type->ID,"PIFFPIFF") &&
               CellClass::Coord2Cell(anim->Location)==CellStruct{69,68}) sawAbsorption=true;
        }
        if(f-start==25) {
            check(sawAbsorption && blasts==3 && guard->Health==400 && cell(69,70)->GetBuilding()->Health==witnessHP,
                  "full shield plays end animation without damage");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    if(f-start>150) check(false,"stage timeout");
    return 0;
}
