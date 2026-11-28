#include "../../../src/Starfall.cpp"
#include <IsometricTileTypeClass.h>
namespace {
void check(bool ok,const char* label) {
    std::ofstream("jump.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
int takeoffSound=-1,landingSound=-1;
vector<CoordStruct> takeoffs,landings;
FootClass* place(TechnoTypeClass* type,HouseClass* house,CellStruct cell) {
    auto foot=static_cast<FootClass*>(type->CreateObject(house));
    check(foot && foot->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"place");
    // Keep targets and blockers stationary until the jump assigns its own orders.
    foot->ForceMission(Mission::Sleep);
    return foot;
}
CellStruct end(DWORD id) {
    const auto& m=ForceMove::motions.at(id);
    return CellClass::Coord2Cell({m.x+m.dx,m.y+m.dy,0});
}
}
DEFINE_HOOK(0x7509E0,SF_TestVehicleJumpSound,5) {
    const int sound=R->ECX<int>();
    if(sound<0 || (sound!=takeoffSound && sound!=landingSound)) return 0;
    const auto at=*R->EDX<CoordStruct*>();
    const bool launch=sound==takeoffSound;
    auto type=AnimTypeClass::Find(launch?"SMKPUFF":"DROPLAND");
    int count=0;
    for(auto anim:AnimClass::Array)
        if(anim->Type==type && anim->Location==at && !anim->OwnerObject && anim->Owner==HouseClass::CurrentPlayer &&
           anim->IsInert && anim->RemainingIterations==1) ++count;
    check(count==1,"one grounded animation at sound position");
    (launch?takeoffs:landings).push_back(at);
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestVehicleJump,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int frame=Unsorted::CurrentFrame;
    if(takeoffSound<0 && VocClass::Array.Count) {
        takeoffSound=VocClass::FindIndex("HornetTakeoff");landingSound=VocClass::FindIndex("HornetLanding");
    }
    if(frame<20 || frame>52) return 0;
    auto player=HouseClass::CurrentPlayer;
    const CellStruct goal{72,68};
    bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto tank=UnitTypeClass::Find("MTNK");
    if(frame==20 && !load) {
        Research::ensureReady();
        const auto& specs=ActiveSkills::specs;
        auto index=[&](const string& id) {
            for(size_t i=0;i<specs.size();++i) if(specs[i].id==id) return int(i);
            check(false,"skill registration");return -1;
        };
        int skill=index("vehiclejump"),script=index("scriptjump");
        auto a=place(tank,player,{68,68}),b=place(tank,player,{68,69});
        auto c=place(tank,player,{68,70});
        auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        const CellStruct farBank{46,83};
        auto heavy=place(UnitTypeClass::Find("BFRT"),player,{42,83});
        auto armored=place(tank,foe,farBank);
        auto crossing=MapClass::Instance.GetCellAt(CellStruct{44,83});
        for(int i=0;i<IsometricTileTypeClass::Array.Count;++i) {
            crossing->IsoTileTypeIndex=i;
            if(crossing->Tile_Is_Water()) break;
        }
        crossing->RecalcAttributes(-1);
        check(crossing->LandType==LandType::Water && !ForceMove::jumpSlot(heavy,crossing->MapCoords,{}),"water is not a tracked landing site");
        auto originalLevel=crossing->Level;
        crossing->Level=originalLevel+8;
        std::vector<CellStruct> otherSlots;
        for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) if(x || y)
            otherSlots.push_back({short(farBank.X+x),short(farBank.Y+y)});
        check(!ForceMove::jumpSlot(heavy,farBank,{6,1,15},otherSlots),"low arc blocked by cliff");
        crossing->Level=originalLevel;
        check(ForceMove::jump(heavy,farBank,{}) && end(heavy->UniqueID)==farBank,"cross water toward crushable vehicle");
        std::ofstream("heavy.ids")<<heavy->UniqueID<<' '<<armored->UniqueID;
        auto enemy=place(InfantryTypeClass::Find("E1"),HouseClass::FindByPlayerAt(HouseClass::PlayerAtB),goal);
        auto cast=[&](FootClass* u,int k){return ActiveSkills::execute(player->ArrayIndex,k,u->UniqueID,0,goal);};
        check(cast(a,skill) && ActiveSkills::remaining(a,specs[skill])==120 && end(a->UniqueID)==goal,"native cast and cooldown");
        check(cast(b,script) && ForceMove::motions.contains(b->UniqueID),"Lua cast");
        auto alternate=end(b->UniqueID);
        check(alternate!=goal,"distinct reservations");
        auto blocker=place(tank,player,alternate);
        for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) {
            CellStruct at{short(goal.X+x),short(goal.Y+y)};
            if(x*x+y*y<=4 && at!=goal && at!=alternate) place(tank,player,at);
        }
        check(!cast(c,skill) && !ActiveSkills::remaining(c,specs[skill]) && !ForceMove::locked(c),"full landing zone preserves cooldown");
        check(takeoffs==vector<CoordStruct>{a->Location,b->Location} && landings.empty(),"takeoff effects only on accepted jumps");
        // Leave an alternate slot available for the in-flight vehicle.
        auto cleared=MapClass::Instance.GetCellAt(CellStruct{72,69})->GetUnit(false);
        if(!cleared || cleared==blocker) cleared=MapClass::Instance.GetCellAt(CellStruct{73,68})->GetUnit(false);
        check(cleared && cleared!=blocker,"fallback fixture");cleared->UnInit();
        std::ofstream("jump.ids")<<a->UniqueID<<' '<<b->UniqueID<<' '<<blocker->UniqueID<<' '<<enemy->UniqueID;
    }
    if(frame==26 && !load) {
        const char* save=GetModuleHandleA("CnCNet-Spawner.dll")?"VehicleJump.sav":"Saved Games\\VehicleJump.sav";
        check(ScenarioClass::SaveGame(save,L"VehicleJump"),"saved in flight");
    }
    if(frame==27 || frame==52) {
        DWORD a,b,blocker,enemy;
        check(bool(std::ifstream("jump.ids")>>a>>b>>blocker>>enemy),"read handles");
        Objects objects;
        for(auto t:TechnoClass::Array) if(alive(t)) objects.emplace(t->UniqueID,t);
        if(frame==27) {
            check(ForceMove::motions.contains(a) && ForceMove::motions.contains(b) && end(a)==goal &&
                ForceMove::motions.at(a).landing && objects.at(a)->IsInAir(),"restored flight");
        } else {
            DWORD heavy,armored;
            check(bool(std::ifstream("heavy.ids")>>heavy>>armored) && objects.contains(heavy) &&
                !ForceMove::locked(objects.at(heavy)) && objects.at(heavy)->GetMapCoords()==CellStruct{46,83} &&
                !objects.contains(armored),"vehicle crush after crossing water");
            check(objects.contains(a) && !ForceMove::locked(objects.at(a)) && objects.at(a)->GetMapCoords()==goal && !objects.contains(enemy),"landed and crushed");
            check(objects.contains(b) && objects.contains(blocker) && !ForceMove::locked(objects.at(b)) &&
                objects.at(b)->GetMapCoords()!=objects.at(blocker)->GetMapCoords() && objects.at(b)->GetMapCoords()!=goal,"diverted around friendly");
            check(landings==vector<CoordStruct>{objects.at(a)->Location,objects.at(b)->Location} &&
                takeoffs.size()==(load?0u:2u),"landing effects once at final positions");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    return 0;
}
