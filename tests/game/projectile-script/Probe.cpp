#include "../../../src/Starfall.cpp"

namespace FlightTest {
void check(bool ok,const char* msg) {
    std::ofstream("flight.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<msg<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
int blasts=0;
CoordStruct last{};
int returnBlasts=0,returnDamage=0;
CoordStruct returnEnd{};
std::vector<int> slowImpacts,fastImpacts;
CellClass* cell(int x,int y) {return MapClass::Instance.GetCellAt(CellStruct{short(x),short(y)});}
void post(int x,int y) {
    check(BuildingSpawn::createAt(BuildingTypeClass::Find("SFReturnPost"),HouseClass::FindByPlayerAt(HouseClass::PlayerAtB),
                                  {short(x),short(y)}),"placed piercing target");
}
UnitClass* place(const char* type,HouseClass* house,int x,int y) {
    auto u=static_cast<UnitClass*>(UnitTypeClass::Find(type)->CreateObject(house));
    check(u && u->Unlimbo(cell(x,y)->GetCoords(),DirType::North),"placed unit");
    u->ForceMission(Mission::Sleep);return u;
}
void shoot(TechnoClass* gun,const char* id,AbstractClass* aim,int x,int y) {
    auto w=WeaponTypeClass::Find(id);check(w && w->Projectile,"weapon available");
    auto b=w->Projectile->CreateBullet(aim,gun,100,w->Warhead,w->Speed,false);
    b->SetWeaponType(w);auto at=cell(x,y)->GetCoords();at.Z+=32;
    check(b->MoveTo(at,{64,0,0}),"launched");
    std::ofstream("flight.log",std::ios::app)<<"start "<<b->Type->ID<<" "<<b->UniqueID<<" script="<<ProjectileScript::rules(b->Type).script<<" flights="<<ProjectileScript::flights.size()<<" z="<<at.Z<<"\n";
}
}
DEFINE_HOOK(0x4690B0,SF_TestScriptBlast,6) {
    auto b=R->ECX<BulletClass*>();
    if(b->WH==WarheadTypeClass::Find("HE") && b->Type==BulletTypeClass::Find("Invisible") && b->Health==40) {
        auto at=*R->Stack<CoordStruct*>(4);
        auto xy=CellClass::Coord2Cell(at);
        if(xy.Y==84 || xy.Y==75) {
            auto ground=MapClass::Instance.GetCellAt(xy)->GetCoordsWithBridge();
            FlightTest::check(at==ground && EntityWalls::firingHouse(b)==HouseClass::CurrentPlayer,"ground blast location and house");
            (xy.Y==84?FlightTest::slowImpacts:FlightTest::fastImpacts).push_back(xy.X);
        }
    }
    if(b->Type==BulletTypeClass::Find("SFReturnRound")) {
        FlightTest::check(b->WH==WarheadTypeClass::Find("SFReturnEnd"),"used configured end warhead");
        ++FlightTest::returnBlasts;FlightTest::returnDamage=b->Health;
        FlightTest::returnEnd=*R->Stack<CoordStruct*>(4);
    }
    if(b->Type==BulletTypeClass::Find("SFTestRound")) {
        ++FlightTest::blasts;FlightTest::last=*R->Stack<CoordStruct*>(4);
        const auto at=FlightTest::last;
        std::ofstream("flight.log",std::ios::app)<<"blast "<<Unsorted::CurrentFrame<<" "<<at.X<<","<<at.Y<<","<<at.Z<<" target="<<(b->Target?int(b->Target->WhatAmI()):0)<<"\n";
    }
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestScriptFlight,5) {
    using namespace FlightTest;
    auto frame=Unsorted::CurrentFrame;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto us=HouseClass::CurrentPlayer,foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(!load && frame==30) {
        for(int i=UnitClass::Array.Count-1;i>=0;--i) {
            auto unit=UnitClass::Array[i];
            if(unit->Type==UnitTypeClass::Find("MTNK")) unit->UnInit();
        }
        auto gun=place("AMCV",us,64,70);
        auto aim=place("HTK",foe,71,68);
        shoot(gun,"SFTestFlight",aim,65,68);
        shoot(gun,"SFTestHover",cell(75,74),65,74);
        shoot(gun,"SFSlowLine",cell(78,84),65,84);
        post(67,80);post(70,80);
        shoot(gun,"SFReturnGun",cell(67,80),65,80);
    }
    if(!load && frame==36) {
        bool climbing=false,hovering=false,trail=false;
        for(auto b:BulletClass::Array) if(auto it=ProjectileScript::flights.find(b->UniqueID);it!=ProjectileScript::flights.end()) {
            auto& f=it->second;
            std::ofstream("flight.log",std::ios::app)<<"round "<<b->UniqueID<<" age "<<f.age<<" speed "<<f.weaponSpeed<<" velocity "<<f.velocity.X<<","<<f.velocity.Y<<","<<f.velocity.Z<<" z "<<b->Location.Z<<" floor "<<cell(65,74)->GetCoords().Z<<"\n";
            if(f.weaponSpeed==64) climbing=b->Location.Z>cell(65,68)->GetCoords().Z+120 && f.velocity.Z==32;
            if(f.weaponSpeed==24) {
                hovering=b->Location.Z==cell(65,74)->GetCoords().Z+32;
                auto& module=scriptModules.at("testflight.lua");
                ProjectileScript::Invocation probe{b,f};
                auto terrain=cell(66,84),bridge=cell(67,84);
                auto level=terrain->Level;auto flags=bridge->Flags;
                check(ProjectileScript::callback(probe,module,"probeGround"),"read ground baseline");
                terrain->Level+=2;bridge->Flags|=CellFlags::BridgeHead;
                // Only exercise height queries while these synthetic map flags are set.
                bool valid=ProjectileScript::callback(probe,module,"probeGround");
                terrain->Level=level;bridge->Flags=flags;
                check(valid,"groundPoint uses raised terrain and bridge deck");
            }
        }
        for(auto a:AnimClass::Array) if(!_stricmp(a->Type->ID,"SMOKEY2") && a->GetMapCoords()==cell(65,68)->MapCoords) trail=true;
        check(climbing && hovering,"independent climb and hover from weapon Speed");
        check(trail,"native smoke trailer on climbing missile");
        check(slowImpacts==std::vector<int>{66,67},"slow line accumulates partial cells before save");
        bool child=false;
        for(auto b:BulletClass::Array) if(b->Type==BulletTypeClass::Find("SFTestChild")) {
            auto expected=cell(65,74)->GetCoords();expected.Z+=32;
            child=b->SourceCoords==expected && b->Owner && b->Owner->Owner==us &&
                b->Target && b->Target->WhatAmI()==AbstractType::Unit;
        }
        check(child,"queried enemy and launched child from parent with firer credit");
        check(cell(67,80)->GetBuilding()->Health==4900 && cell(70,80)->GetBuilding()->Health==4950 && returnBlasts==0,
              "scripted wave pierces past aim with falloff before save");
        check(ScenarioClass::SaveGame("Saved Games\\Flight.sav",L"Script flight"),"saved mid-flight");
        TerminateProcess(GetCurrentProcess(),0);
    }
    static UnitClass* guard=nullptr;
    static CoordStruct nativeOrigin{};
    static int wallHP=0;
    if(load && frame==37) post(68,80);
    if(load && frame>=36 && frame<=65 && (frame%5==0 || frame==37)) {
        std::ofstream log("flight.log",std::ios::app);log<<"frame "<<frame<<" flights "<<ProjectileScript::flights.size()<<" blasts "<<blasts<<"\n";
        for(auto b:BulletClass::Array) if(b->Type==BulletTypeClass::Find("SFTestRound")) log<<"round "<<b->UniqueID<<" position "<<b->Location.X<<","<<b->Location.Y<<","<<b->Location.Z<<"\n";
    }
    if(load && frame==65) {
        check(slowImpacts==std::vector<int>{68,69,70},"ground blast spacing resumes across load");
        check(cell(67,80)->GetBuilding()->Health==4900 && cell(70,80)->GetBuilding()->Health==4950 &&
              cell(68,80)->GetBuilding()->Health==4970 && returnBlasts==1 && returnDamage==30 &&
              CellClass::Coord2Cell(returnEnd)==CellStruct{65,80},"return after reload skips old hits and expires at Lua endpoint");
        check(blasts==2 && ProjectileScript::flights.empty() && CellClass::Coord2Cell(last)==cell(65,74)->MapCoords,"target hit and Life expiry after reload");
        guard=place("HTNK",foe,69,75);
        check(BuildingSpawn::createAt(BuildingTypeClass::Find("GAPILL"),foe,{69,70}),"placed physical blocker");
    }
    if(load && frame==69) {
        auto gun=place("AMCV",us,63,73);
        shoot(gun,"SFTestFlight",cell(75,75),66,75);
        shoot(gun,"SFTestFlight",cell(75,70),66,70);
    }
    if(load && frame==75) {
        std::ofstream("flight.log",std::ios::app)<<"guard hp "<<guard->Health<<"/"<<guard->Type->Strength<<" remaining "<<ProjectileScript::flights.size()<<" blasts "<<blasts<<"\n";
        check(guard->Health==guard->Type->Strength && ProjectileScript::flights.empty() && blasts==3 &&
              last.X==69*256,"shield and wall clip four-cell movement at contact");
        auto launcher=place("MTNK",us,73,61);
        auto aim=cell(77,61);
        launcher->SetTarget(aim);
        auto facing=launcher->GetTargetDirection(aim);
        launcher->PrimaryFacing.SetCurrent(facing);launcher->SecondaryFacing.SetCurrent(facing);
        launcher->RearmTimer.Stop();
        auto round=launcher->Fire(aim,1);
        check(round && ProjectileScript::flights.contains(round->UniqueID),"native Fire uses projectile controller");
        nativeOrigin=round->Location;
        launcher->ForceMission(Mission::Sleep);
    }
    if(load && frame==86) {
        check(blasts==4 && last.X==nativeOrigin.X+512 && last.Y==nativeOrigin.Y && last.Z==nativeOrigin.Z,
              "moveTo and explicit detonation use the requested position");
        post(72,70);post(72,75);
        wallHP=cell(69,70)->GetBuilding()->Health;
        auto gun=place("AMCV",us,63,76);
        shoot(gun,"SFReturnGun",cell(75,70),66,70);
        shoot(gun,"SFReturnGun",cell(75,75),66,75);
    }
    if(load && frame==92) {
        check(cell(72,70)->GetBuilding()->Health==5000 && cell(72,75)->GetBuilding()->Health==5000 &&
              cell(69,70)->GetBuilding()->Health==wallHP-100 && guard->Health==guard->Type->Strength &&
              returnBlasts==2 && returnDamage==100 && returnEnd.X==69*256 && ProjectileScript::flights.empty(),
              "combined wall and shield hits stop before rear targets");
        auto gun=place("AMCV",us,62,76);
        shoot(gun,"SFFastLine",cell(76,75),64,75);
        gun->UnInit();
    }
    if(load && frame==97) {
        check(fastImpacts==std::vector<int>{65,66,67} && ProjectileScript::flights.empty(),
              "fast line emits multiple ground blasts only up to shield after firer removal");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
