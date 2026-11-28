#include "../../../src/Starfall.cpp"
#include <IsometricTileTypeClass.h>

namespace {
void check(bool ok,const char* name) {
    std::ofstream("ejection.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
FootClass* passenger(FootClass* carrier,TechnoTypeClass* type,int hp) {
    auto p=static_cast<FootClass*>(type->CreateObject(carrier->Owner));
    auto entry=MapClass::Instance.GetCellAt(CellStruct{67,69})->GetCoords();
    check(p && p->Unlimbo(entry,DirType::North) && p->Limbo(),"board previously deployed passenger");
    p->NextObject=nullptr;p->SetLocation(carrier->Location);p->FrozenStill=true;
    p->Health=hp;carrier->EnterAsPassenger(p);p->Transporter=carrier;
    if(carrier->GetTechnoType()->OpenTopped) carrier->EnteredOpenTopped(p);
    return p;
}
FootClass* byId(DWORD id) {
    TargetClass ref;ref.m_ID=id;ref.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    if(auto found=ref.As_Foot()) return found;
    for(auto foot:FootClass::Array) if(foot && foot->UniqueID==id) return foot;
    return nullptr;
}
vector<WORD> drawPixels(FootClass* rider,int drawY,bool shadows,TechnoClass* behind=nullptr,
                        RectangleStruct patch={166,165,68,36}) {
    auto surface=DSurface::Temp;
    auto bounds=DSurface::ViewBounds;
    const bool previous=std::exchange(Game::bDrawShadow,shadows);
    ZBuffer::Instance->FillRect(0xFFFF,patch);
    ABuffer::Instance->FillRect(0x7F,patch);
    surface->FillRect(&patch,0xFFFF);
    if(behind) {
        Point2D rearAnchor{200,183};
        behind->DrawIt(&rearAnchor,&bounds);
        if(auto building=abstract_cast<BuildingClass*>(behind))
            reinterpret_cast<void(__thiscall*)(ObjectClass*,Point2D*,RectangleStruct*)>(0x43DA80)(building,&rearAnchor,&bounds);
    }
    Point2D anchor{200,drawY};
    if(rider) rider->DrawIt(&anchor,&bounds);
    Game::bDrawShadow=previous;
    const auto pitch=surface->GetPitch();
    auto bytes=static_cast<const BYTE*>(surface->Lock(patch.X,patch.Y));
    check(bytes!=nullptr,"read ejection shadow pixels");
    vector<WORD> pixels;
    for(int y=0;y<patch.Height;++y) {
        auto row=reinterpret_cast<const WORD*>(bytes+y*pitch);
        pixels.insert(pixels.end(),row,row+patch.Width);
    }
    surface->Unlock();
    return pixels;
}
vector<WORD> risingShadow;
void checkForeground(FootClass* rider,TechnoClass* behind) {
    const int anchor=183+ForceMove::screenLift(rider);
    auto tactical=TacticalClass::Instance;
    auto camera=tactical->TacticalPos;
    tactical->TacticalPos=TacticalClass::CoordsToScreen(behind->GetRenderCoords())-Point2D{200,183};
    RectangleStruct patch{100,83,200,200};
    auto rear=drawPixels(nullptr,0,false,behind,patch);
    int left=patch.Width,right=-1,top=patch.Height,bottom=-1;
    for(int y=0;y<patch.Height;++y) for(int x=0;x<patch.Width;++x) if(rear[y*patch.Width+x]!=0xFFFF) {
        left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
    }
    std::ofstream("ejection.log",std::ios::app)<<"rear bounds "<<left<<','<<top<<' '<<right<<','<<bottom<<'\n';
    const int aligned=anchor+(top+bottom)/2-100;
    auto body=drawPixels(rider,aligned,false,nullptr,patch);
    auto both=drawPixels(rider,aligned,false,behind,patch);
    tactical->TacticalPos=camera;
    int overlap=0,visible=0;
    for(size_t n=0;n<body.size();++n) if(body[n]!=0xFFFF && rear[n]!=0xFFFF && body[n]!=rear[n]) {
        ++overlap;
        if(both[n]==body[n]) ++visible;
    }
    std::ofstream("ejection.log",std::ios::app)<<"overlap "<<behind->GetTechnoType()->ID<<' '<<visible<<'/'<<overlap<<'\n';
    check(rider->GetYSort()>behind->GetYSort() && overlap>8 && visible==overlap,
          "airborne body remains visible over rear sprite");
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPassengerEjection,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int f=Unsorted::CurrentFrame;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(f<20 || f>540) return 0;
    if(f==20 && !loading) {
        Research::ensureReady();
        auto house=HouseClass::CurrentPlayer;
        auto c=static_cast<UnitClass*>(UnitTypeClass::Find("BFRT")->CreateObject(house));
        check(c && c->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{67,68})->GetCoords(),DirType::North),"transport placed");
        c->ForceMission(Mission::Sleep);
        auto list=ActiveSkills::held(c);check(list.size()==1,"skill registered");
        auto cast=[&]{return ActiveSkills::execute(house->ArrayIndex,list[0],c->UniqueID,0,{72,68});};
        check(!cast() && !ActiveSkills::remaining(c,ActiveSkills::specs[list[0]]),"empty carrier keeps cooldown");
        auto water=MapClass::Instance.GetCellAt(CellStruct{70,68});
        for(int i=0;i<IsometricTileTypeClass::Array.Count;++i) {
            water->IsoTileTypeIndex=i;
            if(water->Tile_Is_Water()) break;
        }
        water->RecalcAttributes(-1);
        check(water->LandType==LandType::Water,"cross-water fixture");
        auto type=InfantryTypeClass::Find("E1");
        auto first=passenger(c,type,73),last=passenger(c,type,77);
        auto cargo=passenger(c,UnitTypeClass::Find("MTNK"),100);
        auto next=passenger(c,type,79);
        check(cast() && ActiveSkills::remaining(c,ActiveSkills::specs[list[0]])==120,"accepted all-cargo order");
        check(ForceMove::motions.at(next->UniqueID).duration==6,"five cells take six frames at 12.5 cells per second");
        check(c->Passengers.NumPassengers==3 && next->IsOnMap && !next->Transporter &&
            ForceMove::locked(next) && last->Transporter==c && first->Transporter==c,"sequential launch");
        auto other=static_cast<FootClass*>(UnitTypeClass::Find("HTK")->CreateObject(house));
        check(other && other->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{63,75})->GetCoords(),DirType::North),"second transport placed");
        auto waiting=passenger(other,type,61),launched=passenger(other,type,63);
        auto tank=static_cast<FootClass*>(UnitTypeClass::Find("FV")->CreateObject(HouseClass::FindByPlayerAt(HouseClass::PlayerAtB)));
        auto engineer=static_cast<FootClass*>(InfantryTypeClass::Find("ENGINEER")->CreateObject(house));
        check(tank && tank->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{63,77})->GetCoords(),DirType::North) &&
              engineer && engineer->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{63,78})->GetCoords(),DirType::North),"place on-map Lua targets");
        tank->ForceMission(Mission::Sleep);
        std::ofstream("unit-ejection.ids")<<tank->UniqueID<<' '<<engineer->UniqueID;
        grantToUnit(other,PassiveGrant{{"scriptedejection"},-1});
        auto script=ActiveSkills::held(other);
        check(script.size()==1 && ActiveSkills::execute(house->ArrayIndex,script.front(),other->UniqueID,0,{66,75}) &&
            ForceMove::locked(launched) && waiting->Transporter==other,"Lua launches infantry and queues next rider");
        std::ofstream("second.ids")<<other->UniqueID<<' '<<launched->UniqueID;
        check(ForceMove::locked(tank) && ForceMove::locked(engineer),"Lua launches a selected enemy vehicle and friendly infantry");
        std::ofstream("ejection.ids")<<c->UniqueID<<' '<<first->UniqueID<<' '<<last->UniqueID<<' '<<next->UniqueID<<' '<<cargo->UniqueID;
    }
    if(f!=24 && f!=30 && f!=31 && f!=39 && f!=44 && f!=90 && f<340) return 0;
    DWORD id,a,b,d,v;if(!(std::ifstream("ejection.ids")>>id>>a>>b>>d>>v)) check(false,"read handles");
    auto c=byId(id),first=byId(a),second=byId(b),third=byId(d),cargo=byId(v);
    if(f>340) {
        if(first && first->GetMapCoords()==CellStruct{75,68}) {
            check(true,"infantry follows move order after landing");
            TerminateProcess(GetCurrentProcess(),0);
        }
        if(f==540) check(false,"infantry follows move order after landing");
        return 0;
    }
    check(c && first && second && third && cargo,"all original handles live");
    DWORD tankId,engineerId;
    check(bool(std::ifstream("unit-ejection.ids")>>tankId>>engineerId),"read direct ejection handles");
    auto tank=byId(tankId),engineer=byId(engineerId);
    if(f==31) check(ForceMove::locked(tank) && ForceMove::locked(engineer),"direct ejection survives midair save");
    if(f==90) check(tank && engineer && tank->HasParachute && tank->Parachute && tank->IsFallingDown &&
                    engineer->HasParachute && engineer->Parachute && engineer->IsFallingDown,
                    "selected vehicle and infantry both deploy native parachutes");
    if(f==24) check(third->GetMapCoords()!=c->GetMapCoords() && !third->HasParachute && third->IsInAir() && c->Passengers.NumPassengers==3 &&
        static_cast<InfantryClass*>(third)->SequenceAnim==Sequence::Crawl,"ballistic crawl before opening");
    constexpr int groundY=183;
    if(f==24) {
        const auto groundOrder=third->Location.X+third->Location.Y;
        check(third->GetYSort()>groundOrder,"ejected rider sorts in front of ground objects at its footprint");
        risingShadow=drawPixels(third,groundY,true);
        auto hidden=drawPixels(third,groundY,false);
        check(risingShadow!=hidden && std::all_of(hidden.begin(),hidden.end(),[](WORD p){return p==0xFFFF;}),
              "ejected passenger has a visible ground shadow");
        checkForeground(third,c);
        auto rearCell=MapClass::Instance.GetCellAt(CellStruct{65,63});
        check(BuildingSpawn::create(BuildingTypeClass::Find("GAPILL"),c->Owner,rearCell->GetCoords(),0),
              "place rear building for depth comparison");
        auto pillbox=rearCell->GetBuilding();
        pillbox->QueueMission(Mission::Sleep,false);pillbox->NextMission();
        checkForeground(third,pillbox);
        pillbox->UnInit();
    }
    if(f==24 || f==30 || f==31) {
        DWORD carrierId,airborneId;
        check(bool(std::ifstream("second.ids")>>carrierId>>airborneId),"second transport handles");
        if(f==24) {
            auto other=byId(carrierId);
            const auto& flight=ForceMove::motions.at(airborneId);
            auto target=CellClass::Coord2Cell({flight.x+flight.dx,flight.y+flight.dy,0});
            auto blocker=UnitTypeClass::Find("MTNK")->CreateObject(other->Owner);
            check(blocker && blocker->Unlimbo(MapClass::Instance.GetCellAt(target)->GetCoords(),DirType::North),
                  "tank occupies the assigned drop site after launch");
            std::ofstream("blocked-drop.ids")<<blocker->UniqueID<<' '<<target.X<<' '<<target.Y;
            int lethal=100000;
            other->ReceiveDamage(&lethal,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
        } else {
            auto airborne=byId(airborneId);
            check(!ForceMove::ejectOrders.contains(carrierId) && airborne && airborne->Health==63 &&
                (ForceMove::locked(airborne) || airborne->IsFallingDown),"launched passenger survives transport loss");
        }
    }
    if(f==30 || f==31) {
        const auto& nearer=ForceMove::motions.at(b);
        check(nearer.dx==4*256 && !nearer.dy && nearer.duration==5 && nearer.landing->ejectSpeed==12.5,
              "nearer assigned slot opens in five frames at the same speed");
        check(c->Passengers.NumPassengers==2 && first->Transporter==c && !second->Transporter &&
              ForceMove::locked(second) && !ForceMove::locked(third) && third->HasParachute && third->Parachute && static_cast<InfantryClass*>(third)->SequenceAnim==Sequence::Paradrop &&
              ForceMove::ejectOrders.contains(id) && ForceMove::descending.contains(d),"queue flight and parachute restored");
        if(!loading && f==30) {
            // Native parachutists enter DrawIt at their projected airborne position.
            const int drawY=groundY-TacticalClass::AdjustForZ(third->GetHeight());
            check(drawPixels(third,drawY,true)==risingShadow,"launch shadow matches native parachute shadow pixel for pixel");
            const auto path=GetModuleHandleA("CnCNet-Spawner.dll")?"Ejection.sav":"Saved Games\\Ejection.sav";
            check(ScenarioClass::SaveGame(path,L"Ejection"),"midair save");
        }
    }
    if(f==39) check(c->Passengers.NumPassengers==1 && cargo->Transporter==c && ForceMove::locked(first) &&
        !ForceMove::ejectOrders.contains(id),"only infantry ejected at configured intervals");
    if(f==44) {
        DWORD carrierId,riderId,blockerId; CellStruct original{};
        check(bool(std::ifstream("second.ids")>>carrierId>>riderId) &&
              bool(std::ifstream("blocked-drop.ids")>>blockerId>>original.X>>original.Y),"read obstructed drop handles");
        auto diverted=byId(riderId),blocker=byId(blockerId);
        check(diverted && diverted->IsFallingDown && diverted->HasParachute && !ForceMove::locked(diverted) &&
              std::abs(diverted->GetMapCoords().X-original.X)<=4 && std::abs(diverted->GetMapCoords().Y-original.Y)<=4 &&
              diverted->GetMapCoords()!=original && blocker && blocker->GetMapCoords()==original,
              "late obstruction diverts the passenger to a free nearby parachute site");
        std::set<std::pair<int,int>> sites;
        for(auto rider:{a,b,d}) {
            check(ForceMove::descending.contains(rider),"all three native parachutes open");
            const auto at=ForceMove::descending.at(rider);
            sites.emplace(at.X,at.Y);
        }
        check(sites.size()==3 && cargo->Transporter==c && c->Passengers.NumPassengers==1,"three distinct reserved drop sites");
    }
    if(f==340) {
        check(tank && engineer && !ForceMove::locked(tank) && !ForceMove::locked(engineer) &&
              !tank->IsFallingDown && !engineer->IsFallingDown && tank->IsOnMap && engineer->IsOnMap &&
              tank->GetMapCoords()==CellStruct{67,77} && engineer->GetMapCoords()==CellStruct{67,78},
              "directly launched vehicle and infantry land at scripted destinations");
        DWORD carrierId,riderId; std::ifstream("second.ids")>>carrierId>>riderId;
        auto diverted=byId(riderId);
        check(diverted && diverted->IsOnMap && !diverted->InLimbo && !diverted->IsFallingDown &&
              !ForceMove::locked(diverted) && !ForceMove::descending.contains(riderId),"diverted passenger lands");
        for(auto p:{first,second,third}) {
            auto at=p->GetMapCoords();
            check(p->IsOnMap && !p->Transporter && !p->InLimbo && !p->IsInAir() && !p->IsFallingDown &&
                !ForceMove::locked(p) && !ForceMove::descending.contains(p->UniqueID),"native landing complete");
            check(std::abs(at.X-72)<=4 && std::abs(at.Y-68)<=4,"landed near target");
        }
        check(first->Health==73 && second->Health==77 && third->Health==79,"three separate parachute landings");
        first->QueueMission(Mission::Move,true);
        first->SetDestination(MapClass::Instance.GetCellAt(CellStruct{75,68}),true);
    }
    return 0;
}
