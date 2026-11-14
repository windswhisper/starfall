#include "../../../src/Starfall.cpp"

namespace {
void check(bool success,const char* label) {
    std::ofstream("teleport.log",std::ios::app)<<(success?"PASS ":"FAIL ")<<label<<"\n";
    if(!success) TerminateProcess(GetCurrentProcess(),2);
}
TechnoClass* place(TechnoTypeClass* type,HouseClass* owner,CellStruct p) {
    auto u=static_cast<TechnoClass*>(type->CreateObject(owner));
    auto cell=MapClass::Instance.TryGetCellAt(p);
    check(u && cell && u->Unlimbo(cell->GetCoordsWithBridge(),DirType::North),"place fixture");
    return u;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestChronoTeleport,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int f=Unsorted::CurrentFrame;
    auto house=HouseClass::CurrentPlayer;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(f==20 && !loading) {
        Research::ensureReady();
        check(skills.contains("fixedwarp") && skills.contains("scriptwarp") &&
              !skills.contains("badcoordinates") && !skills.contains("badradius"),"config validation");
        auto carrier=static_cast<UnitClass*>(place(UnitTypeClass::Find("BFRT"),house,{46,72}));
        auto peer=place(UnitTypeClass::Find("MTNK"),house,{49,74});
        auto passenger=static_cast<FootClass*>(InfantryTypeClass::Find("E1")->CreateObject(house));
        carrier->EnterAsPassenger(passenger);passenger->Transporter=carrier;
        auto gi=place(InfantryTypeClass::Find("E1"),house,{48,70});
        auto blocker=place(BuildingTypeClass::Find("GAPOWR"),house,{50,65});
        grantToUnit(carrier,PassiveGrant{{"fixedwarp"},-1});
        grantToUnit(gi,PassiveGrant{{"scriptwarp"},-1});
        capture("ondeploy",carrier,nullptr,carrier,carrier,0);dispatch();
        auto a=CellClass::Coord2Cell(carrier->ChronoDestCoords);
        check(carrier->IsImmobilized && a==CellStruct{50,70},"exact destination");
        capture("onfire",gi,peer,gi,peer,0);dispatch();
        auto b=CellClass::Coord2Cell(peer->ChronoDestCoords);
        int dx=b.X-a.X,dy=b.Y-a.Y;
        check(peer->IsImmobilized && dx*dx+dy*dy==1,"distinct fallback");
        auto g=CellClass::Coord2Cell(gi->ChronoDestCoords);
        int gx=g.X-50,gy=g.Y-65;
        check(gi->IsImmobilized && gx*gx+gy*gy>0 && gx*gx+gy*gy<=9 &&
              !MapClass::Instance.GetCellAt(g)->GetBuilding(),"Lua moves target");
        std::ofstream("teleport.ids")<<carrier->UniqueID<<" "<<peer->UniqueID<<" "<<gi->UniqueID<<" "<<passenger->UniqueID<<" "<<blocker->UniqueID<<" "<<b.X<<" "<<b.Y<<" "<<g.X<<" "<<g.Y;
        check(ScenarioClass::SaveGame("Saved Games\\Teleport.sav",L"ChronoTeleport"),"saved in transit");
    }
    if(f==240) {
        DWORD a,b,c,p,d; CellStruct peerCell{},giCell{};
        std::ifstream("teleport.ids")>>a>>b>>c>>p>>d>>peerCell.X>>peerCell.Y>>giCell.X>>giCell.Y;
        Objects live;for(auto u:TechnoClass::Array) if(alive(u)) live.emplace(u->UniqueID,u);
        auto at=[&](DWORD id,CellStruct pos) {
            auto u=findLive(live,id);
            return u && u->Owner==house && u->IsOnMap && !u->IsImmobilized && u->GetMapCoords()==pos;
        };
        check(at(a,{50,70}) && at(b,peerCell) && at(c,giCell),"all arrive with original identity");
        auto passenger=findLive(live,p);
        check(MapClass::Instance.GetCellAt(CellStruct{50,65})->GetBuilding()==findLive(live,d) && passenger && passenger->Transporter==live.at(a) &&
              live.at(a)->Passengers.NumPassengers==1,"blocker and passengers intact");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
